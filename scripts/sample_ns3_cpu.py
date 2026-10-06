#!/usr/bin/env python3
"""Attach a bounded perf CPU sample to a running master; never stop the target."""
import argparse
import datetime
import json
import math
from pathlib import Path
import shlex
import shutil
import subprocess


def record_command(pid, seconds, frequency, data_path):
    return ["perf", "record", "-e", "cpu-clock", "-F", str(frequency),
            "--call-graph", "dwarf,16384", "-o", str(data_path), "-p", str(pid),
            "--", "sleep", str(seconds)]


def has_detailed_timing(argv):
    for i, arg in enumerate(argv):
        if arg.startswith("--perfDetailed="):
            if arg.split("=", 1)[1].lower() not in ("0", "false"):
                return True
        if arg == "--perfDetailed":
            if i + 1 >= len(argv) or argv[i + 1].lower() not in ("0", "false"):
                return True
    return False


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pid", type=int, required=True, help="master PID, not ./ns3 wrapper PID")
    p.add_argument("--seconds", type=float, default=60)
    p.add_argument("--frequency", type=int, default=49)
    p.add_argument("--output-dir", type=Path, default=Path("results/perf"))
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args()
    if args.pid <= 0 or not math.isfinite(args.seconds) or args.seconds <= 0 or args.frequency <= 0:
        p.error("pid, seconds and frequency must be positive finite values")
    try:
        proc = Path(f"/proc/{args.pid}")
        argv = [x.decode(errors="replace") for x in (proc / "cmdline").read_bytes().split(b"\0") if x]
        exe = str((proc / "exe").resolve(strict=True))
    except (OSError, RuntimeError) as exc:
        p.error(f"Cannot inspect target PID: {exc}")
    if not argv or "master" not in Path(exe).name:
        p.error("Specify the ns-3 master executable PID, not the wrapper or another process")
    if has_detailed_timing(argv):
        p.error("Use a separate run with perfDetailed disabled to avoid mixing profiler overhead")
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    out = args.output_dir / f"cpu_sample_{stamp}_pid{args.pid}"
    command = record_command(args.pid, args.seconds, args.frequency, out / "perf.data")
    print(shlex.join(command), flush=True)
    if args.dry_run:
        return
    if not shutil.which("perf"):
        p.error("perf is not installed/available. No simulation was launched or altered. See doc/performance_timing.md")
    out.mkdir(parents=True, exist_ok=False)
    (out / "metadata.json").write_text(json.dumps({
        "pid": args.pid, "target_exe": exe, "target_argv": argv,
        "seconds": args.seconds, "frequency": args.frequency, "command": command,
        "note": "CPU-time sample of a window, not whole-run wall-clock timing",
    }, indent=2) + "\n")
    with (out / "record.log").open("w") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(f"perf record failed ({result.returncode}); inspect {out / 'record.log'}. "
                         "No automatic sudo/sysctl changes were made.")
    for name, flags in (("self.txt", ["--no-children"]), ("callgraph.txt", ["--children"])):
        command = ["perf", "report", "--stdio", "--percent-limit", "0.5", *flags,
                   "-i", str(out / "perf.data")]
        with (out / name).open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise SystemExit(f"perf report failed; inspect {out / name}")
    print(f"Saved: {out}")


if __name__ == "__main__":
    main()
