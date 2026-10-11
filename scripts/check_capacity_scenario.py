#!/usr/bin/env python3
"""Validate one wrapper run: process completion, both-direction events and full UE cycles."""
import argparse
import csv
import hashlib
import json
from pathlib import Path


def read(p):
    with p.open() as f:
        return list(csv.DictReader(f))


def one(paths):
    paths = list(paths)
    assert len(paths) == 1, paths
    return paths[0]


def check(root):
    cfg = json.loads((root/'effective_config.json').read_text())
    planned = root/'planned_events.csv'
    assert hashlib.sha256(planned.read_bytes()).hexdigest() == cfg['planned_events_sha256']
    planned_rows = [dict(cycle_id=int(r['cycle_id']), ap_id=int(r['ap_id']),
                         rate_bps=int(r['rate_bps']), direction=r['direction']) for r in read(planned)]
    assert planned_rows == cfg['events']
    if cfg['mode'] == 'random_walk':
        links = {r['ap_id']: r for r in cfg['capacity_variation']['links']}
        assert set(links) == {0, 1, 2}
        assert len(planned_rows) == 3 * cfg['setting']['numCycles']
        previous = {ap: r['initial_bps'] for ap, r in links.items()}
        for index, event in enumerate(planned_rows):
            ap, rate = event['ap_id'], event['rate_bps']
            link = links[ap]
            assert (event['cycle_id'], ap) == (index//3+1, index % 3)
            assert event['direction'] == 'both'
            assert link['min_bps'] <= rate <= link['max_bps']
            assert (rate-link['min_bps']) % link['step_bps'] == 0
            assert rate-previous[ap] in (-link['step_bps'], 0, link['step_bps'])
            previous[ap] = rate
    assert json.loads((root/'process_status.json').read_text())['returncode'] == 0
    run = one((root/'simulation').glob('*/*/capacity_schedule/*'))
    assert (run/'planned_events.csv').read_bytes() == planned.read_bytes()
    metadata = dict(line.split('=', 1) for line in (run/'metadata.txt').read_text().splitlines() if '=' in line)
    start = float(metadata['cycle_start_offset_sec'])
    duration = float(metadata['cycle_duration_sec'])
    expected = []
    current = {int(ap): rate for ap, rate in cfg['initial_bps'].items()}
    for ap, rate in current.items():
        for direction in (('pgw_to_cer', 'cer_to_pgw') if ap == 0 else ('ap_to_router', 'router_to_ap')):
            expected.append((0., 0, ap, direction, rate, rate))
    for event in cfg['events']:
        c, ap, rate = event['cycle_id'], event['ap_id'], event['rate_bps']
        for direction in (('pgw_to_cer', 'cer_to_pgw') if ap == 0 else ('ap_to_router', 'router_to_ap')):
            expected.append((start+(c-1)*duration, c, ap, direction, current[ap], rate))
        current[ap] = rate
    actual = [(float(r['sim_time']), int(r['cycle_id']), int(r['ap_id']), r['direction'],
               int(r['old_bps']), int(r['new_bps'])) for r in read(run/'events.csv')]
    assert actual == expected, (actual, expected)
    completed = dict(line.split('=', 1) for line in (run/'simulation_completed.txt').read_text().splitlines())
    assert abs(float(completed['sim_time'])-(start+duration*cfg['setting']['numCycles']+5)) < 1e-6
    initial = read(run/'initial_terminals.csv')
    assert len(initial) == cfg['setting']['terminals']
    initial_bs = {r['ue_id']: int(r['initial_bs_id_1based'])-1 for r in initial}
    rows = read(one((root/'simulation').glob('*/*/master_log_*.csv')))
    n, cycles = cfg['setting']['terminals'], cfg['setting']['numCycles']
    assert len(rows) == n*cycles
    assert len({(r['cycle_id'], r['ue_id']) for r in rows}) == n*cycles
    assert all(r['seed'] == str(cfg['seed']) and r['method'] == cfg['method'] for r in rows)
    for cycle in range(1, cycles+1):
        group = [r for r in rows if int(r['cycle_id']) == cycle]
        assert len(group) == n
        if cfg['method'] == 'no_switch':
            assert all(r['switch_flag'] == '0' and r['previous_bs_id'] == r['current_bs_id'] and int(r['current_bs_id']) == initial_bs[r['ue_id']] for r in group)
    print('PASS: process exit, capacity events, completion, complete master cycles, seed/method')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('run_directory', type=Path)
    check(p.parse_args().run_directory)
