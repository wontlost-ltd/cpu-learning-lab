"""End-to-end native WAV decoding/teaching with generated, independent PCM fixtures."""
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    def tone(name, frequency, gain=0.6, phase=0.0, rate=16000, duration=0.1):
        path = root / (name + '.wav')
        with wave.open(str(path), 'wb') as wav:
            wav.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
            wav.writeframes(b''.join(struct.pack('<h', round(32767*gain*math.sin(2*math.pi*frequency*i/rate+phase))) for i in range(round(rate*duration))))
        return path
    low = tone('low', 500)
    high = tone('high', 2000)
    variants = [tone('exam1',500,0.4,0.3), tone('exam2',2000,0.5,0.7), tone('exam3',500,0.7,0.8,32000,0.15)]
    unknown = tone('unknown',1000)
    silence = tone('silence',0)
    model = root / 'sounds.state'
    exam = root / 'sounds.exam'
    exam.write_text('CPUAI_AUDIO_EXAM1\n'+'\n'.join(json.dumps(label)+' '+json.dumps(str(path)) for label,path in zip(['low','high','low'],variants))+'\n')
    overlap = root / 'overlap.exam'
    overlap.write_text(exam.read_text()+f'"low" {json.dumps(str(low))}\n')
    malformed = root / 'malformed.exam'
    malformed.write_text(exam.read_text()+'"unterminated')
    bad = root / 'bad.wav'
    data = bytearray(low.read_bytes());data[22:24] = struct.pack('<H',2);bad.write_bytes(data)
    truncated = root / 'truncated.wav';truncated.write_bytes(low.read_bytes()[:-1])
    def q(p):return json.dumps(str(p))
    def run(commands):
        r = subprocess.run([binary],input='\n'.join(commands+['quit'])+'\n',capture_output=True,text=True,check=True)
        return r.stdout,r.stderr
    stdout,stderr = run([
        f'audio-ask {q(low)}',f'audio-teach l low {q(low)}',f'audio-teach h high {q(high)}',
        f'audio-ask {q(variants[0])}',f'audio-ask {q(unknown)}',f'audio-ask {q(silence)}',
        f'audio-exam {q(exam)}',f'audio-exam {q(overlap)}',f'audio-exam {q(malformed)}',
        f'audio-teach duplicate low {q(low)}',f'audio-save {q(model)}',
        f'audio-teach l corrected {q(low)}',f'audio-ask {q(variants[0])}',
        f'audio-load {q(model)}',f'audio-ask {q(variants[0])}',f'audio-ask {q(bad)}',f'audio-ask {q(truncated)}',
        f'observe watch 1 1000 -10 4 {q(low)}',f'observe phone 1 970 20 7 {q(high)}',
        'align watch phone 10','align watch phone 11',f'observe watch 1 1000 -10 4 {q(low)}','stream-stats',
    ])
    assert 'audio exam revision=2 cases=3 correct=3 unknown=0' in stdout, (stdout,stderr)
    assert 'label="corrected" known=1' in stdout and stdout.count('label="low" known=1')==2,stdout
    assert stdout.count(' known=0')==3,stdout
    assert 'aligned pairs=0' in stdout and 'aligned pairs=1' in stdout and 'worst_ms=11' in stdout,stdout
    assert 'accepted=2 rejected=1 peak=2' in stdout,stdout
    assert stderr.count('error:')==5,(stdout,stderr)
    # Decoder rejects inconsistent headers, unsupported encodings, duplicate chunks and oversized duration.
    invalid=[]
    for name,offset,replacement in [('riff',4,struct.pack('<I',1)),('encoding',20,struct.pack('<H',3)),('rate',24,struct.pack('<I',1)),('bits',34,struct.pack('<H',8))]:
        content=bytearray(low.read_bytes());content[offset:offset+len(replacement)]=replacement
        path=root/(name+'.wav');path.write_bytes(content);invalid.append(path)
    long=tone('long',500,duration=2.01);invalid.append(long)
    too_short=tone('short',500,duration=0.01);invalid.append(too_short)
    out,err=run([f'audio-ask {q(path)}' for path in invalid])
    assert err.count('error:')==len(invalid),(out,err)
    # Duplicate labels with distinct PCM are allowed; duplicate examples and corrupt features are not.
    lines=model.read_text().splitlines()
    for content in [lines[0]+'\n'+lines[1]+'\n'+lines[1]+'\n','\n'.join([lines[0], lines[1].rsplit(' ',1)[0]+' nan',lines[2]])+'\n']:
        if content == model.read_text():continue
        path=root/'invalid.state';path.write_text(content)
        out,err=run([f'audio-load {q(path)}'])
        assert 'error:' in err,(out,err)
    # Restart: serialized examples preserve predictions and duplicate-exam guard.
    out,err=run([f'audio-load {q(model)}',f'audio-ask {q(variants[1])}',f'audio-exam {q(overlap)}'])
    assert 'label="high" known=1' in out and 'overlaps teaching' in err,(out,err)
    # Ambiguous conflicting feature evidence abstains; malformed model load preserves current state.
    near=tone('near',500,0.6001)
    corrupt=root/'corrupt.state';corrupt.write_text(model.read_text()+'junk')
    out,err=run([f'audio-load {q(model)}',f'audio-load {q(corrupt)}',f'audio-teach c conflict {q(near)}',f'audio-ask {q(variants[0])}'])
    assert 'trailing audio model' in err and 'known=0' in out,(out,err)
print('audio CLI tests passed: independent holdout, correction, restart, unknown, malformed input, clocks')
