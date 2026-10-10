#!/usr/bin/env python3
"""Reproduce seed1001 AP0 40/20 Mbps logistic summaries (stdlib only)."""
import csv
import hashlib
import json
from pathlib import Path
from statistics import mean
from check_ap0_capacity import check


def read(p):
    with p.open() as f:
        return list(csv.DictReader(f))


def one(paths):
    paths = list(paths)
    assert len(paths) == 1, paths
    return paths[0]


def write(p, rows):
    with p.open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def main():
    out = Path('results/ap0_capacity_logistic/analysis_40_vs_20')
    out.mkdir(parents=True, exist_ok=True)
    global_rows, app_rows, sources, assignments = [], [], {}, []
    settings = []
    for rate, root in [(40, 'ap0_capacity_logistic'), (20, 'ap0_20Mbps_capacity_logistic')]:
        directory = Path('results') / root / 'variable/80/logistic'
        log = one(directory.glob('master_log_*.csv'))
        run = one((directory / 'ap0_capacity').iterdir())
        check(run, log)
        settings.append(json.loads((run / 'setting.json').read_text()))
        assignments.append(read(run / 'initial_terminals.csv'))
        for p in [log, *run.iterdir(), Path('contrib/kameda/model/server/APselection.cc')]:
            sources[str(p)] = hashlib.sha256(p.read_bytes()).hexdigest()
        rows = read(log)
        for cycle in range(1, 6):
            g = [r for r in rows if int(r['cycle_id']) == cycle]
            h = len(g) / sum(1 / float(r['satisfaction']) for r in g)
            assert abs(h - float(g[0]['harmonic_mean'])) < 1e-5
            global_rows.append(dict(low_rate_mbps=rate, cycle_id=cycle,
                harmonic_mean=float(g[0]['harmonic_mean']),
                unsatisfied=int(g[0]['num_unsatisfied_users']),
                switches_selected=sum(int(r['switch_flag']) for r in g),
                invalid=sum(r['measurement_valid'] != '1' for r in g)))
            for bs in range(3):
                for app in range(1, 5):
                    z = [r for r in g if int(r['current_bs_id']) == bs and int(r['app_type']) == app]
                    if z:
                        app_rows.append(dict(low_rate_mbps=rate, cycle_id=cycle, bs_id=bs,
                            app_type=app, n=len(z), tp_mbps=mean(float(r['tp_mbps']) for r in z),
                            rtt_ms=mean(float(r['rtt_ms']) for r in z),
                            satisfaction=mean(float(r['satisfaction']) for r in z)))
    assert settings[0] == settings[1]
    assert assignments[0] == assignments[1]
    write(out / 'global_by_cycle.csv', global_rows)
    write(out / 'by_bs_app_cycle.csv', app_rows)
    (out / 'manifest.json').write_text(json.dumps(dict(sources=sources,
        note='Measured current-cycle QoE precedes selected action; RTT is BS monitor proxy.'), indent=2))
    print('Saved:', out)


if __name__ == '__main__':
    main()
