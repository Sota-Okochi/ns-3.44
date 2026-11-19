#!/usr/bin/env python3
"""Summarize the newest per-seed random logs; propose link-model settings."""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import re

import numpy as np


def summarize(source):
    latest = {}
    for path in source.glob('master_log_*.csv'):
        match = re.fullmatch(r'master_log_(\d+)_(\d{8}_\d{6})\.csv', path.name)
        if match:
            seed, timestamp = match.groups()
            if seed not in latest or timestamp > latest[seed][0]:
                latest[seed] = (timestamp, path)
    if not latest:
        raise ValueError('No master logs found')
    samples = [[], [], []]
    manifest = []
    apps = collections.Counter()
    for seed, (_, path) in sorted(latest.items(), key=lambda item: int(item[0])):
        with path.open(newline='') as stream:
            rows = list(csv.DictReader(stream))
        cycles = collections.defaultdict(list)
        for row in rows:
            if row['seed'] != seed or row['method'] != 'random':
                raise ValueError(f'Unexpected seed/method: {path}')
            cycles[int(row['cycle_id'])].append(row)
        if not cycles:
            raise ValueError(f'Empty latest log: {path}')
        manifest.append(dict(file=path.name, seed=int(seed), rows=len(rows),
                             cycles=len(cycles), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        apps.update(row['app_type'] for row in cycles[min(cycles)])
        for cycle, group in sorted(cycles.items()):
            if len(group) != 100 or len({r['ue_id'] for r in group}) != 100:
                raise ValueError(f'Expected 100 unique UEs: {path}, cycle {cycle}')
            for ap in range(3):
                members = [r for r in group if int(r['current_bs_id']) == ap]
                counts = {int(r[f'num_users_ap{ap}']) for r in group}
                rtts = {float(r[f'monitor_rtt_ap{ap}']) for r in group}
                if counts != {len(members)} or len(rtts) != 1:
                    raise ValueError(f'Inconsistent AP snapshot: {path}, cycle {cycle}')
                # Matches APselection::logistic_assignment(), not per-UE
                # counterfactual estimated_satisfaction_if_ap*.
                tp = [float(r['tp_mbps']) for r in members if float(r['tp_mbps']) > 0]
                rtt = rtts.pop()
                if not tp or rtt <= 0 or not np.isfinite([rtt, *tp]).all():
                    raise ValueError(f'Invalid AP quality: {path}, cycle {cycle}, AP {ap}')
                samples[ap].append([len(members), rtt, float(np.mean(tp))])
    model = dict(base_rtt_ms=[], base_tp_mbps=[], rtt_alpha_ms_per_user=[],
                 tp_alpha_per_user=[0.020, 0.025, 0.030],
                 tp_scale_range=[], rtt_offset_range_ms=[], min_rtt_ms=0.01,
                 min_tp_mbps=0.01)
    stats = []
    for ap, values in enumerate(samples):
        values = np.asarray(values)
        n, rtt, tp = values.T
        # Nonnegative RTT slope. This is observational, not a causal load model.
        slope = max(0.0, float(np.cov(n, rtt, ddof=0)[0, 1] / np.var(n))) if np.var(n) else 0.0
        base_rtt = float(np.mean(rtt) - slope * np.mean(n))
        if base_rtt <= 0:
            raise ValueError('Nonpositive RTT intercept; review calibration')
        # Retain prior TP slopes: narrow load coverage cannot identify zero-load TP.
        factor = 1 / (1 + model['tp_alpha_per_user'][ap] * n)
        base_tp = float(np.dot(factor, tp) / np.dot(factor, factor))
        model['base_rtt_ms'].append(base_rtt)
        model['base_tp_mbps'].append(base_tp)
        model['rtt_alpha_ms_per_user'].append(slope)
        model['tp_scale_range'].append(np.quantile(tp / (base_tp * factor), [.05, .95]).tolist())
        model['rtt_offset_range_ms'].append(np.quantile(rtt - base_rtt - slope*n, [.05, .95]).tolist())
        stats.append(dict(ap=ap, snapshots=len(values), columns=['users', 'rtt_ms', 'tp_mbps'],
                          quantiles=[0, .05, .5, .95, 1],
                          values=np.quantile(values, [0, .05, .5, .95, 1], axis=0).tolist(),
                          means=np.mean(values, axis=0).tolist()))
    return dict(selection='latest filename timestamp per seed; all cycles; one AP snapshot per cycle',
                sources=manifest, first_cycle_app_counts=dict(sorted(apps.items())),
                statistics=stats, link_model=model)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2] / 'OUTPUT/100/random')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = summarize(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
