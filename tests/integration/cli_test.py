"""Black-box native CLI tests. Python is only a stdlib test driver, not the runtime."""
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
suite = Path(sys.argv[2]).resolve().as_posix()

def run(commands):
    result = subprocess.run([binary], input=commands, text=True, capture_output=True, timeout=20)
    assert result.returncode == 0, result.stderr
    return result

with tempfile.TemporaryDirectory() as tmp:
    state = (Path(tmp) / "lesson data.state").as_posix()
    first = run(f'''teach square 0 0
teach square 1 1
ask square 2
next square
exam square "{suite}"
teach square 2 4
ask square 3
exam square "{suite}"
save "{state}"
quit
''')
    assert not first.stderr, first.stderr
    assert "uncertain: candidates disagree" in first.stdout, first.stdout
    assert "please teach square at x=" in first.stdout, first.stdout
    assert "passed_on_suite=0" in first.stdout and "passed_on_suite=1" in first.stdout, first.stdout
    assert "9 [candidate consensus" in first.stdout, first.stdout
    second = run(f'load "{state}"\nask square 4\nquit\n')
    assert not second.stderr, second.stderr
    assert "16 [candidate consensus" in second.stdout, second.stdout
    malformed = run('teach bad 0 0 extra\nshow bad\nload "missing.state"\nshow bad\nquit\n')
    assert "unexpected trailing argument" in malformed.stderr
    assert "cannot load" in malformed.stderr
    assert malformed.stdout.count("hypotheses=0") == 2
print("PASS: CLI ambiguity, active question, held-out exam, restart and malformed commands")
