#!/usr/bin/env python3
"""Compare fixed-assignment capacity runs; standard library only, no simulation changes."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics

from check_ap1_capacity import check

APPS = {1: 'browser', 2: 'video', 3: 'voice', 4: 'game'}


def read(path):
    with path.open(newline='') as f:
        return list(csv.DictReader(f))


def one(paths):
    paths = list(paths)
    if len(paths) != 1:
        raise ValueError(f'Expected exactly one run/file, found {paths}')
    return paths[0]


def summarize(rows):
    sat = [float(r['satisfaction']) for r in rows]
    if not sat:
        return dict(n=0)
    if min(sat) <= 0:
        raise ValueError('Non-positive logged satisfaction')
    return dict(n=len(rows), mean_tp_mbps=statistics.mean(float(r['tp_mbps']) for r in rows),
                mean_rtt_ms=statistics.mean(float(r['rtt_ms']) for r in rows),
                mean_satisfaction=statistics.mean(sat), harmonic_mean=len(sat)/sum(1/x for x in sat),
                min_satisfaction=min(sat), max_satisfaction=max(sat),
                unsatisfied=sum(x < .5 for x in sat), below_requirement=sum(x < 1 for x in sat),
                invalid=sum(r['measurement_valid'] != '1' for r in rows))


def write(path, rows):
    keys = list(dict.fromkeys(k for r in rows for k in r))
    with path.open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=keys)
        w.writeheader()
        w.writerows(rows)


def analyze(root, output):
    output.mkdir(parents=True, exist_ok=True)
    data, initials, files, queues = {}, {}, [], []
    summary, paired, global_rows = [], [], []
    for condition in ('constant', 'variable'):
        directory = root / condition / '80/no_switch'
        log = one(directory.glob('master_log_*.csv'))
        run = one((directory / 'ap1_capacity').iterdir())
        check(run, log)
        data[condition] = read(log)
        initials[condition] = read(run / 'initial_terminals.csv')
        files += [log, *sorted(run.iterdir())]
        for cycle in range(1, 6):
            group = [r for r in data[condition] if int(r['cycle_id']) == cycle]
            stats = summarize(group)
            assert abs(stats['harmonic_mean'] - float(group[0]['harmonic_mean'])) < 1e-5
            assert stats['unsatisfied'] == int(group[0]['num_unsatisfied_users'])
            global_rows.append(dict(condition=condition, cycle_id=cycle, **stats))
            for bs in range(3):
                for app in (0, 1, 2, 3, 4):
                    selected = [r for r in group if int(r['current_bs_id']) == bs and
                                (app == 0 or int(r['app_type']) == app)]
                    summary.append(dict(condition=condition, cycle_id=cycle, bs_id_0based=bs,
                                        app_type=app, app=APPS.get(app, 'all'), **summarize(selected)))
        samples = read(run / 'queue_samples.csv')
        truncated = [r for r in samples if any(v is None for v in r.values())]
        if truncated:
            print(f'WARNING: {condition}: {len(truncated)} incomplete queue row(s); ignoring these only')
        samples = [r for r in samples if all(v is not None for v in r.values())]
        for cycle in range(1, 6):
            for direction in ('ap_to_router', 'router_to_ap'):
                start = 4 + (cycle - 1) * 9.5
                # Half-open complete cycle. Counter differences include the end sample.
                selected = [r for r in samples if r['direction'] == direction and
                            start <= float(r['sim_time']) < start + 9.5]
                first = one(r for r in samples if r['direction'] == direction and float(r['sim_time']) == start)
                last = max((r for r in samples if r['direction'] == direction and start <= float(r['sim_time']) <= start + 9.5), key=lambda r: float(r['sim_time']))
                queues.append(dict(condition=condition, cycle_id=cycle, direction=direction,
                    observed_end_sec=last['sim_time'], complete_cycle=float(last['sim_time']) == start + 9.5,
                    mean_queue_packets=statistics.mean(float(r['queue_packets']) for r in selected),
                    max_queue_packets=max(float(r['queue_packets']) for r in selected),
                    mean_qdisc_bytes=statistics.mean(float(r['qdisc_bytes'] or 0) for r in selected),
                    device_drop_delta=int(last['dropped_packets_total'])-int(first['dropped_packets_total']),
                    qdisc_drop_delta=int(last['qdisc_dropped_packets_total'] or 0)-int(first['qdisc_dropped_packets_total'] or 0)))
    assert initials['constant'] == initials['variable'], 'Initial composition differs'
    assert {(r['seed'], r['method']) for r in data['constant']+data['variable']} == {('1001', 'no_switch')}
    lookup = {(r['cycle_id'], r['ue_id']): r for r in data['constant']}
    for r in data['variable']:
        base = lookup[(r['cycle_id'], r['ue_id'])]
        assert (base['app_type'], base['current_bs_id']) == (r['app_type'], r['current_bs_id'])
        paired.append(dict(cycle_id=r['cycle_id'], ue_id=r['ue_id'], bs_id_0based=r['current_bs_id'],
                           app_type=r['app_type'], **{f'delta_{key}': float(r[key])-float(base[key])
                           for key in ('tp_mbps', 'rtt_ms', 'satisfaction')}))
    write(output/'by_bs_app_cycle.csv', summary)
    write(output/'global_by_cycle.csv', global_rows)
    write(output/'paired_ue_deltas.csv', paired)
    write(output/'ap1_queue_by_cycle.csv', queues)
    manifest = {'seed': 1001, 'unsatisfied_threshold': .5,
                'note': 'Logged RTT is a shared BS monitor proxy, not individual application RTT. Zero TP for RTT apps is not no traffic.',
                'sources': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('Saved analysis to', output)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=Path('results/ap1_capacity'))
    p.add_argument('--output', type=Path, default=Path('results/ap1_capacity/analysis'))
    a = p.parse_args()
    analyze(a.root, a.output)
