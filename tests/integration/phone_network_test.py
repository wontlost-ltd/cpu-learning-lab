"""Apple Network.framework client interoperates with native OpenSSL receiver."""
import contextlib
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave
phone,link,sync=(str(Path(p).resolve()) for p in sys.argv[1:4])
def run(*args,ok=True):
    r=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=30)
    assert (r.returncode==0)==ok,(args[:2],r.returncode,r.stdout,r.stderr)
    return r
@contextlib.contextmanager
def server(store,identity,count=1,extra=()):
    process=subprocess.Popen([link,'serve',str(store),str(identity),'0',str(count),*extra],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        first=process.stdout.readline();assert first.startswith('ready port='),(first,process.stderr.read())
        yield int(first.strip().split('=')[1])
        assert process.wait(timeout=15)==0,process.stderr.read()
    finally:
        if process.poll() is None:process.kill();process.wait(timeout=5)
        process.stdout.close();process.stderr.close()
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);source=root/'phone.journal';target=root/'mac.journal';identity=root/'identity';bundle=root/'phone.pair';wav=root/'tone.wav'
    run(sync,'init',source,'phone');run(sync,'init',target,'desktop');run(link,'init',identity,'desktop');run(link,'pair',identity,'phone',bundle)
    with wave.open(str(wav),'wb') as output:
        output.setparams((1,2,16000,0,'NONE','not compressed'));output.writeframes(b''.join(struct.pack('<h',round(20000*math.sin(2*math.pi*500*i/16000))) for i in range(1600)))
    for i in range(3):run(sync,'record',source,'fixture','-',wav)
    with server(target,identity,extra=('--drop-after-commit',)) as port:run(phone,source,bundle,'127.0.0.1',port,ok=False)
    assert 'peers=0' in run(sync,'stats',source).stdout
    with server(target,identity,count=2) as port:
        result=run(phone,source,bundle,'127.0.0.1',port);assert 'delivered=0 duplicates=3 complete=1' in result.stdout,result.stdout
        before=(source.read_bytes(),target.read_bytes());run(phone,source,bundle,'127.0.0.1',port);assert before==(source.read_bytes(),target.read_bytes())
    values=bundle.read_text().split();bad=root/'bad.pair'
    for field in [3,4]:
        fields=values.copy();fields[field]='0'*64;bad.write_text(' '.join(fields)+'\n');before=target.read_bytes()
        with server(target,identity) as port:run(phone,source,bad,'127.0.0.1',port,ok=False)
        assert target.read_bytes()==before
    with server(target,identity) as port:
        run(link,'revoke',identity,'phone');run(phone,source,bundle,'127.0.0.1',port,ok=False)
    rotated=root/'rotated.pair';run(link,'pair',identity,'phone',rotated)
    with server(target,identity) as port:run(phone,source,rotated,'127.0.0.1',port)
print('Apple client tests passed: pinned TLS, cross-language batch hash, durable ACK, replay, wrong pin/key and revocation')
