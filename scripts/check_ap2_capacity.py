#!/usr/bin/env python3
"""Validate one completed AP2 experiment and print per-cycle QoE (stdlib only)."""
import argparse
import csv
import json
from pathlib import Path


def rows(path):
    with path.open(newline='') as f:
        return list(csv.DictReader(f))


def check(directory, master_log):
    meta = dict(line.split('=', 1) for line in (directory / 'metadata.txt').read_text().splitlines()
                if '=' in line)
    setting = json.loads((directory / 'setting.json').read_text())
    varying = meta['enabled'] == '1'
    normal, low = int(meta['normal_bps']), int(meta['low_bps'])
    drop, recover = int(meta['drop_cycle']), int(meta['recovery_cycle'])
    start, duration = float(meta['cycle_start_offset_sec']), float(meta['cycle_duration_sec'])
    expected = [(0., 0, normal, normal)]
    if varying:
        expected += [(start + (drop - 1) * duration, drop, normal, low),
                     (start + (recover - 1) * duration, recover, low, normal)]
    events = rows(directory / 'events.csv')
    assert len(events) == 2 * len(expected), 'Wrong event count'
    for direction in ('ap_to_router', 'router_to_ap'):
        actual = [(float(r['sim_time']), int(r['cycle_id']), int(r['old_bps']), int(r['new_bps']))
                  for r in events if r['direction'] == direction]
        assert actual == expected, (direction, actual, expected)
    assert meta['link'] == 'wifi_ap2_router' and meta['bs_id_1based'] == '3'
    status = dict(line.split('=', 1) for line in (directory / 'simulation_completed.txt').read_text().splitlines())
    expected_stop = start + setting['numCycles'] * duration + 5.0
    assert abs(float(status['sim_time']) - expected_stop) < 1e-6, 'Unexpected completion time'
    samples = rows(directory / 'queue_samples.csv')
    assert samples and all(all(v is not None for v in r.values()) for r in samples), 'Truncated samples'
    for r in samples:
        t = float(r['sim_time'])
        rate = low if varying and start + (drop-1)*duration <= t < start + (recover-1)*duration else normal
        assert int(r['link_bps']) == rate, ('Wrong sampled rate', r)
    assert float(samples[-1]['sim_time']) == float(status['sim_time']), 'Missing final sample'
    initial = rows(directory / 'initial_terminals.csv')
    assert len(initial) == setting['terminals']
    initial_bs = {r['ue_id']: int(r['initial_bs_id_1based']) - 1 for r in initial}
    logs = rows(master_log)
    assert meta['method'] == 'no_switch', 'This checker expects fixed connections'
    assert len(logs) == setting['terminals'] * setting['numCycles'], 'Missing UE/cycle rows'
    for r in logs:
        assert r['seed'] == meta['seed']
        assert int(r['switch_flag']) == 0
        assert int(r['current_bs_id']) == int(r['previous_bs_id']) == initial_bs[r['ue_id']]
    print('cycle,mean_tp_mbps,mean_rtt_ms,harmonic_mean,num_unsatisfied_users')
    for cycle in range(1, setting['numCycles'] + 1):
        group = [r for r in logs if int(r['cycle_id']) == cycle]
        assert len({r['ue_id'] for r in group}) == setting['terminals']
        print(cycle, sum(float(r['tp_mbps']) for r in group) / len(group),
              sum(float(r['rtt_ms']) for r in group) / len(group),
              group[0]['harmonic_mean'], group[0]['num_unsatisfied_users'], sep=',')
    print('PASS: event timing/rates, complete cycles, fixed assignments, seed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='ap2_capacity/<run_id> directory')
    parser.add_argument('master_log', type=Path)
    args = parser.parse_args()
    check(args.directory, args.master_log)
