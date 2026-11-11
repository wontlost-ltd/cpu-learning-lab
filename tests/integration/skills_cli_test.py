"""Independent processes exercise published skills, composition, rollback and restore."""
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
fixtures = Path(sys.argv[2]).resolve()
def exam(name):
    return f'"{(fixtures / (name + ".exam")).as_posix()}"'
def run(commands):
    p = subprocess.run([binary], input=commands, capture_output=True, text=True, timeout=30)
    assert p.returncode == 0, p.stderr
    assert not p.stderr, p.stderr
    return p.stdout
with tempfile.TemporaryDirectory() as tmp:
    archive = (Path(tmp) / "skill versions.library").as_posix()
    out = run(f'''teach shift 0 1
teach shift 1 2
teach shift 2 3
publish shift {exam('shift-v1')}
teach square 0 0
teach square 1 1
teach square 2 4
publish square {exam('square-v1')}
teach fourth 0 1
teach fourth 1 16
teach fourth 2 81
plan fourth
compose fourth {exam('fourth-v1')}
run fourth 4
teach shifted_square 0 1
teach shifted_square 1 4
teach shifted_square 2 9
compose shifted_square {exam('shifted-square-v1')}
mine
teach chunk 0 1
teach chunk 1 4
teach chunk 2 9
abstract chunk 0 {exam('chunk-v1')}
run chunk 5
teach shift 0 2
teach shift 1 3
teach shift 2 4
run shift 10
publish shift {exam('shift-v2')}
run shift 10
run fourth 4
versions shift
rollback shift 1
run shift 10
replay
save-library "{archive}"
quit
''')
    assert "accepted fourth@3 steps=3" in out, out
    assert out.count("625 [accepted fourth@3]") == 2, out
    assert "pattern=0 support=2 shift@1 square@2" in out, out
    assert "36 [accepted chunk@5]" in out, out
    assert "12 [accepted shift@6]" in out and out.count("11 [accepted shift@1]") == 2, out
    assert "replay cases=15 correct=15" in out, out
    restored = run(f'load-library "{archive}"\nrun fourth 4\nrun shift 10\nreplay\nquit\n')
    assert "625 [accepted fourth@3]" in restored and "11 [accepted shift@1]" in restored, restored
print("PASS: CLI publication, composition, mined abstraction, pinned dependencies and rollback after restart")
