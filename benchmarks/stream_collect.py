"""Reproducible processing benchmark; synthetic PCM, no device/energy claim."""
import argparse
import json
import math
from pathlib import Path
import platform
import struct
import subprocess
import time

parser=argparse.ArgumentParser()
parser.add_argument('binary')
parser.add_argument('--output',required=True)
args=parser.parse_args()
results=[]
for rate,window_ms,windows in [(16000,100,100),(48000,2000,20)]:
    frames=rate*window_ms//1000
    one=b''.join(struct.pack('<h',round(20000*math.sin(2*math.pi*500*i/rate))) for i in range(frames))
    begin=time.perf_counter()
    process=subprocess.run([str(Path(args.binary).resolve()),'--stdin',str(rate),str(window_ms),'-',str(windows)],input=one*windows,capture_output=True,check=True)
    wall=time.perf_counter()-begin
    rows=[json.loads(row) for row in process.stdout.splitlines()]
    predictions=[row for row in rows if row['type']=='prediction']
    assert len(predictions)==windows and all(row['label'] is None for row in predictions)
    times=sorted(row['processing_ms'] for row in predictions)
    results.append({'rate':rate,'window_ms':window_ms,'windows':windows,'lessons':0,
                    'processing_p50_ms':times[(windows-1)//2],'processing_p95_ms':times[math.ceil(windows*0.95)-1],
                    'wall_seconds':wall,'audio_seconds':window_ms*windows/1000,'summary':rows[-1]})
report={'scope':'synthetic fast pipe; untaught model; processing timer excludes capture and stdout; wall includes process/pipes; no microphone or energy measurement',
        'platform':platform.platform(),'results':results}
Path(args.output).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
