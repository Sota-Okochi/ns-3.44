#!/usr/bin/env python3
"""Convert QueueDisc cumulative counters to interval IP-layer rates (not wire goodput)."""
import argparse
import csv
from pathlib import Path


def intervals(rows):
    previous = {}
    for r in rows:
        key = (r['run_id'], r['direction'])
        old = previous.get(key)
        previous[key] = r
        if old is None:
            continue
        dt = float(r['sim_time']) - float(old['sim_time'])
        if dt <= 0:
            raise ValueError('Non-increasing time')
        changes = {k: int(r[k]) - int(old[k]) for k in
                   ['received_bytes_total', 'sent_bytes_total', 'dropped_packets_total']}
        if min(changes.values()) < 0:
            raise ValueError('Counter reset or wrap detected')
        incoming = changes['received_bytes_total'] * 8 / dt / 1e6
        yield dict(run_id=r['run_id'], seed=r['seed'], direction=r['direction'],
                   start_sec=old['sim_time'], end_sec=r['sim_time'], interval_sec=dt,
                   incoming_mbps=incoming,
                   handed_to_device_mbps=changes['sent_bytes_total'] * 8 / dt / 1e6,
                   incoming_to_capacity_ratio=incoming * 1e6 / int(r['link_bps']),
                   dropped_packets=changes['dropped_packets_total'], queue_bytes=r['queue_bytes'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.input.resolve() == args.output.resolve():
        p.error('input and output must differ')
    with args.input.open() as f:
        result = list(intervals(csv.DictReader(f)))
    if not result:
        p.error('Need at least two samples')
    with args.output.open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=result[0])
        w.writeheader()
        w.writerows(result)
    print(f'Wrote {len(result)} intervals')


if __name__ == '__main__':
    main()
