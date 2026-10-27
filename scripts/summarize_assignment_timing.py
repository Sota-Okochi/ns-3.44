#!/usr/bin/env python3
"""Strict per-decision-cycle summary of assignment timing v2 (never old master logs)."""
import argparse
import csv
import math
from pathlib import Path
import statistics

METHODS = ('random', 'centralized_dqn', 'logistic', 'marl_ppo')


def summarize(paths, expected_decisions=4, methods=METHODS):
    if not methods or len(set(methods)) != len(methods) or any(m not in METHODS for m in methods):
        raise ValueError("Invalid requested methods")
    groups, seen = {}, set()
    for path in paths:
        with Path(path).open() as stream:
            rows = list(csv.DictReader(stream))
        if not rows:
            raise ValueError(f'Empty timing log: {path}')
        for row in rows:
            if row.get('timing_version') != '2':
                raise ValueError(f'Only timing_version=2 is comparable: {path}')
            method, seed = row['method'], int(row['seed'])
            key = method, seed, int(row['cycle_id'])
            if key in seen:
                raise ValueError(f'Duplicate decision (including repeated terminal rows): {key}')
            seen.add(key)
            if method not in methods:
                raise ValueError(f'Unexpected method: {method}')
            if row['status'] == 'no_decision':
                if row['assignment_compute_ms'] or int(row['cycle_id']) != expected_decisions + 1:
                    raise ValueError(f'Invalid terminal row: {key}')
                continue
            if row['status'] != 'ok':
                raise ValueError(f'Invalid/incomplete decision: {key}: {row["status"]}')
            if row['effective_method'] != method:
                raise ValueError(f'Bootstrap is not {method} inference: {key}')
            total, cpp, python = [float(row[k]) for k in
                ('assignment_compute_ms', 'cpp_compute_ms', 'python_compute_ms')]
            if not all(math.isfinite(v) and v >= 0 for v in (total, cpp, python)):
                raise ValueError(f'Invalid time: {key}')
            if not math.isclose(total, cpp + python, rel_tol=1e-9, abs_tol=1e-9):
                raise ValueError(f'Component sum mismatch: {key}')
            groups.setdefault((seed, method), []).append((key[2], int(row['num_terminals']), total))
    if not groups:
        raise ValueError('No measured decisions')
    result = []
    for seed in sorted({k[0] for k in groups}):
        sizes = set()
        for method in methods:
            rows = groups.get((seed, method), [])
            if sorted(r[0] for r in rows) != list(range(1, expected_decisions + 1)):
                raise ValueError(f'Missing decision cycles: seed={seed}, method={method}')
            sizes.update(r[1] for r in rows)
            values = [r[2] for r in rows]
            result.append(dict(seed=seed, method=method, decision_cycles=len(values),
                num_terminals=rows[0][1], mean_ms=statistics.mean(values),
                median_ms=statistics.median(values),
                stdev_ms=statistics.stdev(values) if len(values) > 1 else 0.,
                min_ms=min(values), max_ms=max(values)))
        if len(sizes) != 1:
            raise ValueError(f'Terminal counts differ for seed={seed}')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--method', choices=('all', *METHODS), default='all')
    p.add_argument('--expected-decisions', type=int, default=4)
    args = p.parse_args()
    rows = summarize(sorted(args.root.rglob('assignment_timing_*.csv')), args.expected_decisions,
                     METHODS if args.method == 'all' else (args.method,))
    with args.output.open('x', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)
    for row in rows:
        print(f'{row["seed"]} {row["method"]}: {row["mean_ms"]:.6f} ms/decision cycle')


if __name__ == '__main__':
    main()
