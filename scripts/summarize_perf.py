#!/usr/bin/env python3
"""Summarize a --perfTiming/--perfDetailed run using only the standard library."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dir", type=Path)
    args = parser.parse_args()
    with (args.run_dir / "functions.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise SystemExit("No completed function measurements yet.")
    snapshot = (rows[-1]["snapshot"], rows[-1]["cycle_id"])
    rows = [r for r in rows if (r["snapshot"], r["cycle_id"]) == snapshot]
    print(f"Snapshot: {snapshot[0]}, cycle={snapshot[1]}")
    if snapshot[0] != "final":
        print("Partial run: active outer scopes (including Simulator::Run) are not counted yet.")
    print("Cumulative inclusive time: nested rows overlap; DO NOT sum them.")
    print("WifiRx.sampled.* rows cover sampled receptions ONLY; do not compare their totals to full-run totals.")
    print(f"{'total_s':>12} {'calls':>12} {'mean_ms':>12} {'max_ms':>12}  function")
    for row in sorted(rows, key=lambda r: float(r["total_wall_ms"]), reverse=True):
        print(f"{float(row['total_wall_ms']) / 1000:12.3f} "
              f"{int(row['call_count']):12d} "
              f"{float(row['mean_wall_ms']):12.6f} "
              f"{float(row['max_wall_ms']):12.6f}  {row['function']}")
    with (args.run_dir / "boundaries.csv").open() as stream:
        boundaries = list(csv.DictReader(stream))
    print("\nBoundary intervals (first interval includes warmup; final interval is the tail):")
    print(f"{'label':>12} {'cycle':>5} {'sim_s':>10} {'delta_sim_s':>12} "
          f"{'wall_s':>12} {'events':>14}")
    for row in boundaries:
        print(f"{row['label']:>12} {row['cycle_id']:>5} "
              f"{float(row['sim_time_s']):10.3f} {float(row['interval_sim_s']):12.3f} "
              f"{float(row['interval_wall_ms']) / 1000:12.3f} "
              f"{int(row['interval_event_count']):14d}")


    device_file = args.run_dir / "devices.csv"
    if device_file.exists():
        with device_file.open() as stream:
            devices = list(csv.DictReader(stream))
        selected_rows = [r for r in devices if (r["snapshot"], r["cycle_id"]) == snapshot]
        groups = defaultdict(int)
        for row in selected_rows:
            groups[(row["rat"], row["ap_id_1based"], row["role"], row["selection"], row["metric"])] += int(row["count"])
        print("\nDevice counters (AP IDs are 1-based; cumulative, not packet counts):")
        for key, count in sorted(groups.items()):
            print(" / ".join(key), count)
        print("Per-node/NetDevice details: devices.csv. Selected means application's chosen AP, not PHY power state.")


if __name__ == "__main__":
    main()
