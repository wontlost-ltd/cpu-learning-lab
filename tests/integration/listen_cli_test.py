"""Pipe deterministic PCM to the native streaming process; no microphone needed."""
import json
import math
import os
import shlex
import signal
import time
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

listen,learn=(str(Path(p).resolve()) for p in sys.argv[1:3])
def pcm(frames,frequency=500,phase=0):
    return b''.join(struct.pack('<h',round(20000*math.sin(2*math.pi*frequency*i/16000+phase))) for i in range(frames))

def invoke(args,data=b'',ok=True):
    r=subprocess.run([listen]+args,input=data,capture_output=True,timeout=20)
    assert (r.returncode==0)==ok,(r.returncode,r.stdout,r.stderr)
    return [json.loads(row) for row in r.stdout.splitlines()],r.stderr

with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);wav=root/'train.wav';model=root/'model.state';capture=root/'captured'
    with wave.open(str(wav),'wb') as w:
        w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(pcm(1600))
    def teach(label,retain=False):
        commands=f'audio-teach example {json.dumps(label)} {json.dumps(str(wav))}\naudio-save {json.dumps(str(model))}\nquit\n'
        if retain:commands=f'audio-load {json.dumps(str(model))}\n'+commands
        r=subprocess.run([learn],input=commands,capture_output=True,text=True,check=True)
        assert not r.stderr,r.stderr
    teach('tone')
    args=['--stdin','16000','100',str(model),'10']
    rows,err=invoke(args,pcm(1600*3+317))
    predictions=rows[:-1];summary=rows[-1]
    assert len(predictions)==3 and all(p['label']=='tone' and p['revision']==1 for p in predictions),rows
    assert predictions[0]['fingerprint']==shlex.split(model.read_text().splitlines()[1])[2],predictions
    assert [p['start_frame'] for p in predictions]==[0,1600,3200],rows
    assert summary['discarded_partial_frames']==317 and summary['end']=='eof' and summary['consumed_frames']==5117,rows
    # A raw chunk and equivalent WAV produce exactly the same fingerprint.
    r=subprocess.run([learn],input=f'audio-load {json.dumps(str(model))}\naudio-ask {json.dumps(str(wav))}\nquit\n',text=True,capture_output=True,check=True)
    assert 'label="tone" known=1' in r.stdout,r.stdout
    rows,err=invoke(args+['--capture',str(capture),'2'],pcm(1600*4))
    assert rows[-1]['captures']==2 and len(list(capture.glob('*.wav')))==2,rows
    with wave.open(str(capture/'window-1.wav'),'rb') as w:assert w.readframes(1600)==pcm(1600)
    # Teach a captured window with an explicit user label, save, and recognize a new waveform.
    recorded=capture/'window-1.wav'
    r=subprocess.run([learn],input=f'audio-teach c user-label {json.dumps(str(recorded))}\naudio-save {json.dumps(str(model))}\nquit\n',text=True,capture_output=True,check=True)
    assert not r.stderr,r.stderr
    rows,err=invoke(args,pcm(1600,phase=0.5));assert rows[0]['label']=='user-label',rows
    # Hard output count limit stops reading input; surplus read frames are reported, never silently used.
    rows,err=invoke(['--stdin','16000','100','-','1'],pcm(3200))
    assert rows[0]['label'] is None and rows[-1]['end']=='window_limit' and rows[-1]['windows']==1,rows
    assert rows[-1]['consumed_frames']==1600 and rows[-1]['discarded_capture_frames']>0,rows
    # Exact EOF, short tail and empty input are distinct and deterministic.
    rows,_=invoke(args,b'');assert rows[-1]['windows']==0 and rows[-1]['discarded_partial_frames']==0,rows
    rows,_=invoke(args,pcm(23));assert rows[-1]['windows']==0 and rows[-1]['discarded_partial_frames']==23,rows
    for bad in [args+['--capture',str(capture),'1'],['--stdin','8001','51','-','1'],['--stdin','16000','100','-','0'],['--stdin','16000','100','-','1','--reload'],args+['--bad']]:
        invoke(bad,ok=False)
    invoke(args,b'\x00',ok=False)
    # A partial model write must preserve the last good snapshot, including revision.
    teach('tone');process=subprocess.Popen([listen]+args+['--reload'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    process.stdin.write(pcm(1792));process.stdin.flush()
    first=json.loads(process.stdout.readline());assert first['label']=='tone',first
    # Complete replacement is visible at a subsequent window boundary.
    teach('changed',retain=True)
    process.stdin.write(pcm(1600));process.stdin.flush()
    second=json.loads(process.stdout.readline());assert second['label']=='changed' and second['revision']==2,second
    model.write_text('incomplete write')
    process.stdin.write(pcm(1600));process.stdin.flush()
    third=json.loads(process.stdout.readline());assert third['label']=='changed' and third['revision']==2,third
    process.stdin.close();rest=[json.loads(row) for row in process.stdout];errors=process.stderr.read();assert process.wait(timeout=20)==0,errors
    assert rest[-1]['reload_failures']==1,rest
    teach('tone')
    # Transport can split a single PCM sample between writes; only EOF with a dangling byte is invalid.
    process=subprocess.Popen([listen]+args,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    payload=pcm(1600);process.stdin.write(payload[:1]);process.stdin.flush();time.sleep(0.03)
    process.stdin.write(payload[1:]);process.stdin.close();rows=[json.loads(row) for row in process.stdout]
    errors=process.stderr.read();assert process.wait(timeout=20)==0,errors
    assert rows[-1]['windows']==1,rows
    if os.name!='nt':
        process=subprocess.Popen([listen]+args,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        process.stdin.write(pcm(1792));process.stdin.flush()
        first=json.loads(process.stdout.readline());assert first['type']=='prediction',first
        process.send_signal(signal.SIGTERM)
        assert process.wait(timeout=3)==0,process.stderr.read()
        rows=[json.loads(row) for row in process.stdout];assert rows[-1]['end']=='signal',rows
        process.stdin.close()
    capabilities,_=invoke(['--capabilities'])
    if not capabilities[0]['microphone']:
        _,error=invoke(['--microphone','16000','100','-','1'],ok=False);assert b'not built' in error,error
print('listen CLI tests passed: streaming inference, capture/user teaching, reload, EOF and limits')
