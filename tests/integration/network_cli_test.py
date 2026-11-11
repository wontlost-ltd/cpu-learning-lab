"""Actual loopback TLS; independent processes, persisted journals, offline pairing."""
import contextlib
import json
import math
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import wave

link,sync=(str(Path(p).resolve()) for p in sys.argv[1:3])
def run(*args,ok=True):
    r=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=25)
    assert (r.returncode==0)==ok,(args[:2],r.returncode,r.stdout,r.stderr)
    return r
@contextlib.contextmanager
def server(store,identity,connections=1,extra=()):
    p=subprocess.Popen([link,'serve',str(store),str(identity),'0',str(connections),*extra],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        first=p.stdout.readline();assert first.startswith('ready port='),(first,p.stderr.read())
        yield int(first.strip().split('=')[1])
        assert p.wait(timeout=15)==0,p.stderr.read()
    finally:
        if p.poll() is None:p.kill();p.wait(timeout=5)
        p.stdout.close();p.stderr.close()
def send(connection,payload):
    payload=payload.encode();connection.sendall(struct.pack('!I',len(payload))+payload)
def receive(connection):
    def exact(n):
        data=b''
        while len(data)<n:
            part=connection.recv(n-len(data))
            if not part:raise EOFError
            data+=part
        return data
    size=struct.unpack('!I',exact(4))[0];assert size<=131072;return exact(size).decode()
def raw(port):
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT);context.check_hostname=False;context.verify_mode=ssl.CERT_NONE
    connection=context.wrap_socket(socket.create_connection(('127.0.0.1',port),timeout=3),server_hostname='localhost')
    assert connection.version()=='TLSv1.3';return connection
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);a=root/'source.journal';b=root/'target.journal';auth=root/'identity';pair=root/'watch.pair';wav=root/'tone.wav'
    with wave.open(str(wav),'wb') as w:
        w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(b''.join(struct.pack('<h',round(20000*math.sin(2*math.pi*500*i/16000))) for i in range(1600)))
    run(sync,'init',a,'watch');run(sync,'init',b,'desktop');run(link,'init',auth,'desktop');run(link,'pair',auth,'watch',pair)
    assert pair.stat().st_mode&0o077==0
    for i in range(3):run(sync,'record',a,'recording','-',wav)
    # Receiver fsync completes, socket disappears before ACK, source must not record a receipt.
    with server(b,auth,extra=('--drop-after-commit',)) as port:run(link,'push',a,pair,'127.0.0.1',port,ok=False)
    assert 'peers=0' in run(sync,'stats',a).stdout
    assert 'events=3' in run(sync,'stats',b).stdout
    with server(b,auth,connections=2) as port:
        first=run(link,'push',a,pair,'127.0.0.1',port);assert 'delivered=0 duplicates=3 complete=1' in first.stdout,first.stdout
        before=(a.read_bytes(),b.read_bytes());run(link,'push',a,pair,'127.0.0.1',port);assert before==(a.read_bytes(),b.read_bytes())
    # Pin failure happens before credentials/application payload are sent.
    values=pair.read_text().split();wrong=root/'wrong.pair';wrong.write_text(' '.join(values[:3]+['0'*64]+values[4:])+'\n');wrong.chmod(0o600)
    before=b.read_bytes()
    with server(b,auth) as port:run(link,'push',a,wrong,'127.0.0.1',port,ok=False)
    assert b.read_bytes()==before
    # A wrong device secret fails authorization without any journal mutation.
    wrong.write_text(' '.join(values[:4]+['1'*64])+'\n')
    with server(b,auth) as port:run(link,'push',a,wrong,'127.0.0.1',port,ok=False)
    assert b.read_bytes()==before
    # Authenticated device may not introduce another origin, malformed versions, or oversized frames.
    for mode in ['spoof','version','oversize','truncated']:
        with server(b,auth,extra=('--timeout-ms','500')) as port:
            with raw(port) as connection:
                send(connection,f'HELLO CPUAI_NET1 desktop watch {values[4]}');assert receive(connection)=='READY CPUAI_NET1 desktop'
                if mode=='spoof':send(connection,'CPUAI_BATCH1 1\n"rogue" "s" 1 1800000000000 0 0 16000 1600 "abc-16000-1600" "" '+'0 '*10+'\n')
                elif mode=='version':send(connection,'CPUAI_BATCH2 1\n')
                elif mode=='oversize':connection.sendall(struct.pack('!I',131073))
                else:connection.sendall(struct.pack('!I',100)+b'incomplete')
                try:assert not connection.recv(1024)
                except (ssl.SSLError,ConnectionError,socket.timeout):pass
        assert b.read_bytes()==before
    # Revocation is observed by an already running server at the next connection.
    with server(b,auth) as port:
        run(link,'revoke',auth,'watch');run(link,'push',a,pair,'127.0.0.1',port,ok=False)
    assert b.read_bytes()==before
    # Re-pairing rotates the secret. Old bundle fails, new bundle works.
    rotated=root/'rotated.pair';run(link,'pair',auth,'watch',rotated)
    with server(b,auth,connections=2) as port:
        run(link,'push',a,pair,'127.0.0.1',port,ok=False);run(link,'push',a,rotated,'127.0.0.1',port)
    # Enforce one absolute connection deadline, including a TLS client that sends no application data.
    with server(b,auth,extra=('--timeout-ms','200')) as port:
        with raw(port) as connection:
            try:assert not connection.recv(1024)
            except (ssl.SSLError,ConnectionError):pass
    # Refuse reuse/overwrites and cross-device credentials locally.
    run(link,'init',auth,'desktop',ok=False);run(link,'pair',auth,'watch',rotated,ok=False)
    run(link,'push',b,rotated,'127.0.0.1',1,ok=False)
    # Server contents are reconciled on every push, even if an old receiver snapshot lost data.
    empty=root/'empty.journal';run(sync,'init',empty,'desktop');b.write_bytes(empty.read_bytes())
    with server(b,auth) as port:
        result=run(link,'push',a,rotated,'127.0.0.1',port);assert 'delivered=3' in result.stdout,result.stdout
    # More than one batch on one authenticated connection.
    large=root/'large.journal';run(sync,'init',large,'watch')
    for i in range(129):run(sync,'record',large,'large-recording','-',wav)
    with server(b,auth) as port:
        result=run(link,'push',large,rotated,'127.0.0.1',port);assert 'delivered=129' in result.stdout,result.stdout
    # A pinned server with a malformed ACK must not cause a local durable receipt.
    fresh=root/'wrong-ack.journal';run(sync,'init',fresh,'watch');run(sync,'record',fresh,'ack-test','-',wav)
    listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);fake_port=listener.getsockname()[1];failures=[]
    def fake_receiver():
        try:
            ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.minimum_version=ssl.TLSVersion.TLSv1_3
            ctx.load_cert_chain(str(auth/'certificate.pem'),str(auth/'private.pem'))
            connection,_=listener.accept()
            with ctx.wrap_socket(connection,server_side=True) as secured:
                assert receive(secured).startswith('HELLO CPUAI_NET1 desktop watch ')
                send(secured,'READY CPUAI_NET1 desktop');receive(secured)
                send(secured,'ACK 1 1 '+'0'*64+' desktop')
        except Exception as error:failures.append(type(error).__name__)
        finally:listener.close()
    thread=threading.Thread(target=fake_receiver,daemon=True);thread.start()
    run(link,'push',fresh,rotated,'127.0.0.1',fake_port,ok=False);thread.join(timeout=5)
    assert not thread.is_alive() and not failures,failures
    assert 'peers=0' in run(sync,'stats',fresh).stdout
    # Revoke after one committed batch on an existing TLS connection.
    token=rotated.read_text().split()[4]
    def batch(seq):return f'CPUAI_BATCH1 1\n"watch" "live-revoke" {seq} 1800000000000 0 0 16000 1600 "abc-16000-1600" "" '+'0 '*10+'\n'
    with server(b,auth) as port:
        with raw(port) as connection:
            send(connection,f'HELLO CPUAI_NET1 desktop watch {token}');receive(connection)
            send(connection,batch(1));assert receive(connection).startswith('ACK 1 1 ')
            saved=b.read_bytes();run(link,'revoke',auth,'watch');send(connection,batch(2))
            try:assert not connection.recv(1024)
            except (ssl.SSLError,ConnectionError):pass
    assert b.read_bytes()==saved
print('network CLI tests passed: TLS pin, pair/rotate/revoke, origin ACL, limits, deadlines, disconnect and replay')
