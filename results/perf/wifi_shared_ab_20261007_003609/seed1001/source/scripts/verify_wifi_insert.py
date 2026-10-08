#!/usr/bin/env python3
"""Run saved-before/current-after sequentially; compare recorded results exactly.

No terminal count, traffic, seed stream, or simulation duration is reduced.
Run from an idle machine: do not run another master or rebuild during this test.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def read_csv(path):
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        if not reader.fieldnames or not rows:
            raise ValueError(f"Empty CSV: {path}")
        if any(None in row or None in row.values() for row in rows):
            raise ValueError(f"Malformed CSV: {path}")
        return reader.fieldnames, rows


def equal_csv(left, right, ignore=()):
    fields, a = read_csv(left)
    other, b = read_csv(right)
    if fields != other or len(a) != len(b):
        raise ValueError(f"Header/row count mismatch: {left.name}")
    for index, (x, y) in enumerate(zip(a, b), 2):
        for key in fields:
            if key not in ignore and x[key] != y[key]:
                raise ValueError(f"{left.name}:{index} {key}: {x[key]!r} != {y[key]!r}")
    return len(a)


def perf_dir(case):
    matches = list((case / "perf").glob("*/metadata.txt"))
    if len(matches) != 1:
        raise ValueError(f"Expected one profiler run: {case}")
    return matches[0].parent


def compare(directory):
    before, after = [directory / name for name in ("before", "after")]
    bp, ap = [perf_dir(case) for case in (before, after)]
    settings = [json.loads((p / "setting.json").read_text()) for p in (bp, ap)]
    if settings[0] != settings[1]:
        raise ValueError("setting.json mismatch")
    metadata = [dict(line.split("=", 1) for line in (p / "metadata.txt").read_text().splitlines()
                     if "=" in line) for p in (bp, ap)]
    for key in ("seed", "method", "build_profile", "ns3_version", "perf_detailed",
                "profiler_schema", "perf_wifi_sample_every"):
        if key not in metadata[0] or metadata[0][key] != metadata[1].get(key):
            raise ValueError(f"Metadata mismatch/missing: {key}")
    for p in (bp, ap):
        _, functions = read_csv(p / "functions.csv")
        _, boundaries = read_csv(p / "boundaries.csv")
        if functions[-1]["snapshot"] != "final" or boundaries[-1]["label"] != "run_end":
            raise ValueError(f"Incomplete run: {p}")
        if len([r for r in boundaries if r["label"] == "cycle_end"]) != settings[0]["numCycles"]:
            raise ValueError(f"Missing cycle boundaries: {p}")
    count = equal_csv(bp / "boundaries.csv", ap / "boundaries.csv", {"interval_wall_ms"})
    print(f"PASS: all {count} boundaries: simulation times and event counts", flush=True)
    count = equal_csv(before / "master.csv", after / "master.csv", {"assignment_compute_ms"})
    if count != settings[0]["terminals"] * settings[0]["numCycles"]:
        raise ValueError("Unexpected master log row count")
    print(f"PASS: all {count} master rows; only assignment_compute_ms excluded", flush=True)
    count = equal_csv(before / "reward.csv", after / "reward.csv")
    if count != settings[0]["numCycles"] - 1:
        raise ValueError("Unexpected measured reward row count")
    print(f"PASS: all {count} measured reward rows", flush=True)
    durations = []
    for p in (bp, ap):
        _, rows = read_csv(p / "boundaries.csv")
        durations.append(sum(float(r["interval_wall_ms"]) for r in rows) / 1000)
    print(f"Boundary wall seconds: before={durations[0]:.3f}, after={durations[1]:.3f}")
    print("PASS means exact agreement at CSV output precision, not all internal floating-point bits.")
    return durations


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_pair(args):
    baseline = args.baseline.resolve()
    manifest = json.loads((baseline / "manifest.json").read_text())
    for name, digest in manifest["sha256"].items():
        if sha256(baseline / name) != digest:
            raise ValueError(f"Baseline snapshot changed: {name}")
    settings_bytes = (ROOT / "data/setting.json").read_bytes()
    if settings_bytes != (baseline / "setting.json").read_bytes():
        raise ValueError("Current setting.json differs from saved baseline; do not overwrite it silently")
    # Fail closed if another master is visible. No process is killed.
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            name = (proc / "exe").resolve(strict=True).name
        except (OSError, RuntimeError):
            continue
        if name == "master" or name.startswith("ns3.44-master"):
            raise ValueError(f"Another master is running (PID {proc.name}); wait for completion")
    out = args.output.resolve() if args.output else ROOT / "results/perf" / time.strftime("wifi_insert_ab_%Y%m%d_%H%M%S")
    out.mkdir(parents=True, exist_ok=False)
    settings = json.loads(settings_bytes)
    (out / "setting.json").write_bytes(settings_bytes)
    (out / "source.diff").write_bytes(subprocess.check_output(["git", "diff"], cwd=ROOT))
    # Include the new header even before it is tracked by git.
    for relative in ("src/wifi/model/rx-power-band-map.h", "scripts/verify_wifi_insert.py"):
        source = ROOT / relative
        if source.exists():
            destination = out / "source" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
    output_dir = ROOT / "OUTPUT" / str(settings["terminals"]) / "random"
    # Freeze the after runtime before either long simulation starts.
    after_runtime = out / "after_runtime"
    after_runtime.mkdir()
    shutil.copytree(ROOT / "build/lib", after_runtime / "lib", symlinks=False)
    shutil.copy2(ROOT / "build/master/ns3.44-master-optimized", after_runtime / "master")
    for name in ("before", "after"):
        if (ROOT / "data/setting.json").read_bytes() != settings_bytes:
            raise ValueError("setting.json changed during verification")
        case = out / name
        case.mkdir()
        # Save both runtime variants so rebuilds cannot silently change this test.
        runtime = baseline if name == "before" else after_runtime
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = str(runtime / "lib")
        if env.get("LD_PRELOAD"):
            raise ValueError("Unset LD_PRELOAD for this comparison")
        command = [str(runtime / "master"), "--method=random", f"--rngSeed={args.seed}",
                   "--perfTiming=1", "--perfDetailed=0", f"--perfOutputDir={case / 'perf'}"]
        (case / "runtime.json").write_text(json.dumps({
            "command": command, "LD_LIBRARY_PATH": env["LD_LIBRARY_PATH"],
            "sha256": {str(p.relative_to(runtime)): sha256(p) for p in runtime.rglob("*")
                       if p.is_file() and (p.name == "master" or ".so" in p.name)}}, indent=2))
        (case / "ldd.txt").write_text(subprocess.check_output(["ldd", str(runtime / "master")], env=env, text=True))
        old = set(output_dir.glob("*.csv"))
        print(f"Starting {name}: {case} (full simulation; console.log records output)", flush=True)
        start = time.monotonic()
        with (case / "console.log").open("w") as log:
            subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        (case / "elapsed_seconds.txt").write_text(f"{time.monotonic() - start:.6f}\n")
        for prefix, dest in (("master_log_", "master.csv"), ("measured_reward_log_", "reward.csv")):
            new = [p for p in output_dir.glob(f"{prefix}{args.seed}_*.csv") if p not in old]
            if len(new) != 1:
                raise ValueError(f"Expected one new {prefix} log, found {len(new)}; inspect {case}")
            shutil.copy2(new[0], case / dest)
        if (ROOT / "data/setting.json").read_bytes() != settings_bytes:
            raise ValueError("setting.json changed during verification")
    durations = compare(out)
    (out / "PASS.json").write_text(json.dumps({"boundary_wall_seconds": durations}, indent=2))
    print(f"Results: {out}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=ROOT / "results/perf/wifi_insert_before")
    seeds = parser.add_mutually_exclusive_group()
    seeds.add_argument("--seed", type=int, default=1001)
    seeds.add_argument("--seeds", type=int, nargs="+", help="Run each before/after pair sequentially")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--compare-only", type=Path, help="Recheck an existing A/B directory without simulation")
    args = parser.parse_args()
    try:
        if args.compare_only:
            compare(args.compare_only)
        elif args.seeds:
            if len(set(args.seeds)) != len(args.seeds):
                raise ValueError("Duplicate seeds")
            batch = args.output.resolve() if args.output else ROOT / "results/perf" / time.strftime("wifi_shared_ab_%Y%m%d_%H%M%S")
            batch.mkdir(parents=True, exist_ok=False)
            print(f"Batch results: {batch}", flush=True)
            for seed in args.seeds:
                pair = argparse.Namespace(**vars(args))
                pair.seed = seed
                pair.output = batch / f"seed{seed}"
                run_pair(pair)
        else:
            run_pair(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"FAIL / incomplete: {exc}\n")


if __name__ == "__main__":
    main()
