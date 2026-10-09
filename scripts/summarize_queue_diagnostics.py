#!/usr/bin/env python3
"""Aggregate diagnostic snapshots, never sum cumulative drops over time."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path


def summarize(rows):
    groups = defaultdict(list)
    for row in rows:
        groups[(row['run_id'], row['sim_time'], row['cycle_window_id'],
                row['layer'], row['direction'])].append(row)
    for key, group in groups.items():
        hol = [float(r['hol_delay_ms']) for r in group if r['hol_delay_ms']]
        yield dict(zip(('run_id', 'sim_time', 'cycle_window_id', 'layer', 'direction'), key),
                   sources=len(group),
                   queue_entries=sum(int(r['queue_entries']) for r in group),
                   queue_bytes=sum(int(r['queue_bytes']) for r in group),
                   max_hol_delay_ms=max(hol) if hol else '',
                   dropped_packets_total=sum(int(r['dropped_packets_total']) for r in group),
                   dropped_bytes_total=sum(int(r['dropped_bytes_total']) for r in group))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error('output must differ from input')
    with args.input.open(newline='') as stream:
        rows = list(summarize(csv.DictReader(stream)))
    if not rows:
        parser.error('input has no samples')
    with args.output.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    layers = {r['layer'] for r in rows}
    if 'nr_rlc_um' not in layers:
        print('WARNING: No NR RLC samples; simulation may have stopped before bearer creation.')
    print(f'Wrote {len(rows)} snapshots to {args.output}')


if __name__ == '__main__':
    main()
