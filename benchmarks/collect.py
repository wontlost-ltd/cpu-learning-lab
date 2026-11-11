"""Collect a native benchmark result; no Python in the AI execution path."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("binary")
parser.add_argument("--output", required=True)
parser.add_argument("--compiler", default="g++")
parser.add_argument("--flags", default="unknown; caller must record actual build flags")
args = parser.parse_args()
run = subprocess.run([str(Path(args.binary).resolve())], capture_output=True, text=True, check=True)
rss = None
if platform.system() == "Linux":
    import resource
    rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
measurements = json.loads(run.stdout)
assert measurements["correct_pairs"] == measurements["iterations"]
assert measurements["warm_combinations_total"] == 0
cpu = "unknown"
if Path("/proc/cpuinfo").exists():
    for line in Path("/proc/cpuinfo").read_text().splitlines():
        if line.startswith("model name"):
            cpu = line.split(":", 1)[1].strip()
            break
compiler = None
if shutil.which(args.compiler):
    compiler = subprocess.check_output([args.compiler, "--version"], text=True).splitlines()[0]
result = {
    "date": datetime.now(timezone.utc).date().isoformat(),
    "scope": "P2 cold vs warm and incremental paths, not P1 or GPU comparison",
    "environment": {"os": platform.platform(), "architecture": platform.machine(), "cpu": cpu,
                    "visible_logical_cpus": os.cpu_count(), "compiler": compiler,
                    "flags": args.flags, "shared_environment": True},
    "measurements": measurements, "process_peak_rss_kib": rss, "energy_measured": False,
}
output = Path(args.output)
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
