"""Native process restart/disconnect tests; file transport, no network or physical device."""
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

sync,listen=(str(Path(p).resolve()) for p in sys.argv[1:3])
def call(*args,expected=0):
    r=subprocess.run([sync]+list(map(str,args)),capture_output=True,text=True,timeout=20)
    assert r.returncode==expected,(args,r.returncode,r.stdout,r.stderr)
    return r.stdout,r.stderr
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);a=root/'watch.log';b=root/'phone.log';wav=root/'tone.wav'
    raw=b''.join(struct.pack('<h',round(20000*math.sin(2*math.pi*500*i/16000))) for i in range(1600))
    with wave.open(str(wav),'wb') as w:w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(raw)
    call('init',a,'watch');call('init',b,'phone')
    call('record',a,'session-one','-',wav);call('record',a,'session-one','explicit-user-label',wav)
    out,_=call('sync',a,b,'--stop-before-ack',expected=3)
    assert 'delivered=2' in out and 'acknowledged=0' in out,out
    out,_=call('stats',b);assert 'events=2' in out,out
    out,_=call('sync',a,b);assert 'delivered=0 duplicates=2 acknowledged=2 complete=1' in out,out
    before=(a.read_bytes(),b.read_bytes());call('sync',a,b);assert before==(a.read_bytes(),b.read_bytes())
    call('init',a,'different',expected=1)
    # Continuous producer persists before emitting a durable_event acknowledgement.
    journal=root/'listener.log'
    args=[listen,'--stdin','16000','100','-','2','--journal',str(journal),'desktop']
    r=subprocess.run(args,input=raw*2,capture_output=True,timeout=20,check=True)
    rows=[json.loads(line) for line in r.stdout.splitlines()];ids=[row['durable_event'] for row in rows if row['type']=='prediction']
    assert len(set(ids))==2 and all(item.startswith('desktop/') for item in ids),rows
    # No second process may read/sync a journal while the producer owns it.
    p=subprocess.Popen([listen,'--stdin','16000','100','-','1000','--journal',str(journal),'desktop'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    p.stdin.write(raw*2);p.stdin.flush();first=json.loads(p.stdout.readline())
    assert first['durable_event'] not in ids,first  # New recording session on restart.
    call('stats',journal,expected=1)
    p.kill();p.wait(timeout=20);p.stdin.close();p.stdout.close();p.stderr.close()
    out,_=call('stats',journal);assert any(f'events={n}' in out for n in [3,4]),out
    out,_=call('sync',journal,b);assert 'complete=1' in out,out
    # Explicit bound/failure leaves the existing committed journal untouched.
    before=journal.read_bytes()
    call('record',journal,'bad/session','-',wav,expected=1)
    assert journal.read_bytes()==before
print('sync CLI tests passed: disconnect/restart, idempotent delivery, capture confirmation and process kill')
