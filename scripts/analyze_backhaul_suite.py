#!/usr/bin/env python3
"""Audit completed manual capacity cases and aggregate cycles 2..5 separately from startup."""
import argparse
import csv
from collections import Counter, defaultdict
from datetime import datetime, timezone, timedelta
import json
from pathlib import Path
import re
import statistics


def read(p):
    with p.open(newline='') as f:
        return list(csv.DictReader(f))


def write(p, rows):
    with p.open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def analyze(root, out):
    out.mkdir(parents=True, exist_ok=True)
    cycles, audit, identities = [], [], {}
    for case in sorted(root.glob('logistic_n*')):
        n, seed, rate = map(int, re.fullmatch(r'logistic_n(\d+)_seed(\d+)_(\d+)Mbps', case.name).groups())
        masters = list(case.glob('manual_OUTPUT/*/logistic/master_log*.csv'))
        if len(masters) != 1:
            raise ValueError(f'{case}: expected exactly one master log, got {len(masters)}')
        master = masters[0]
        mr = read(master)
        mg = defaultdict(list)
        for r in mr:
            mg[int(r['cycle_id'])].append(r)
        assert set(mg) == set(range(1, 6)) and all(len(v) == n for v in mg.values())
        assert all(len({r['ue_id'] for r in v}) == n for v in mg.values())
        valid, excluded = [], []
        for d in sorted(case.glob('manual_OUTPUT/*/logistic/queue_diagnostics/*')):
            cfg = json.loads((d / 'setting.json').read_text())
            meta = dict(line.split('=', 1) for line in (d / 'metadata.txt').read_text().splitlines() if '=' in line)
            lr = read(d / 'link_load.csv')
            end = float(lr[-1]['sim_time']) if lr else 0
            offset = float(meta['cycle_start_offset_sec'])
            expected_end = offset + cfg['cycleTimeSec'] * cfg['numCycles'] + 5
            if abs(end - expected_end) > 1e-6:
                excluded.append(dict(directory=str(d), end_sec=end))
                continue
            assert int(meta['seed']) == seed and cfg['terminals'] == n
            stamp = int(d.name.split('_')[-2]) / 1e6
            run_time = datetime.fromtimestamp(stamp, timezone(timedelta(hours=9)))
            master_time = datetime.strptime(master.stem.split('_', 3)[-1], '%Y%m%d_%H%M%S')
            assert abs((run_time.replace(tzinfo=None) - master_time).total_seconds()) < 2
            valid.append((d, cfg, meta, lr))
        assert len(valid) == 1, (case, len(valid))
        d, cfg, meta, lr = valid[0]
        init = read(d / 'initial_terminals.csv')
        assert Counter(int(r['app_type']) for r in init) == {1:n//5, 2:n*2//5, 3:n*3//20, 4:n//4}
        initial = [(r['ue_id'], r['app_type'], r['initial_bs_id']) for r in init]
        assert initial == [(r['ue_id'], r['app_type'], r['current_bs_id']) for r in mg[1]]
        key = (n, seed)
        assert key not in identities or identities[key] == initial
        identities[key] = initial
        load = {round(float(r['sim_time']), 6):r for r in lr if r['direction'] == 'dl'}
        assert all(int(r['link_bps']) == rate*1000000 for r in lr)
        qg = defaultdict(list)
        with (d / 'queues.csv').open() as f:
            for r in csv.DictReader(f):
                if r['direction'] != 'dl':
                    continue
                t = float(r['sim_time'])
                for c in range(1, 6):
                    base = float(meta['cycle_start_offset_sec']) + (c-1)*cfg['cycleTimeSec']
                    if base+cfg['monitorStartSec']-1e-8 <= t < base+cfg['monitorStopSec']-1e-8:
                        qg[c, r['layer']].append(r)
                        break
        for c, rows in sorted(mg.items()):
            base = float(meta['cycle_start_offset_sec']) + (c-1)*cfg['cycleTimeSec']
            start, end = round(base+cfg['monitorStartSec'],6), round(base+cfg['monitorStopSec'],6)
            a, b = load[start], load[end]
            delta = lambda name:int(b[name])-int(a[name])
            dev, disc, rlc = (qg[c, x] for x in ['p2p_device','p2p_qdisc','nr_rlc_um'])
            assert all(r['capacity']=='600p' and float(r['channel_delay_ms'])==20 for r in dev)
            assert all(r['capacity']=='10240p' for r in disc)
            assert all(r['capacity']=='999999999B' for r in rlc)
            r = rows[0]
            cycles.append(dict(case=case.name, terminals=n, seed=seed, rate_mbps=rate, cycle_id=c,
                window_start=start, window_end=end, ap0_users=int(r['num_users_ap0']),
                ap0_video=sum(x['current_bs_id']=='0' and x['app_type']=='2' for x in rows),
                ap0_voice=sum(x['current_bs_id']=='0' and x['app_type']=='3' for x in rows),
                ap0_rtt_ms=float(r['monitor_rtt_ap0']), harmonic_mean=float(r['harmonic_mean']),
                unsatisfied=int(r['num_unsatisfied_users']),switches=int(r['switch_count']),
                video_tp_mbps=statistics.mean(float(x['tp_mbps']) for x in rows if x['app_type']=='2'),
                incoming_mbps=delta('received_bytes_total')*8/(end-start)/1e6,
                handed_to_device_mbps=delta('sent_bytes_total')*8/(end-start)/1e6,
                qdisc_drop_packets=delta('dropped_packets_total'),
                qdisc_drop_fraction=delta('dropped_packets_total')/delta('received_packets_total') if delta('received_packets_total') else 0,
                device_full_fraction=statistics.mean(int(x['queue_entries'])==600 for x in dev),
                device_mean_bytes=statistics.mean(int(x['queue_bytes']) for x in dev),
                qdisc_mean_bytes=statistics.mean(int(x['queue_bytes']) for x in disc),
                rlc_max_hol_ms=max(float(x['hol_delay_ms']) for x in rlc),
                monitor_rlc_max_hol_ms=max(float(x['hol_delay_ms']) for x in rlc if x['ue_id']=='-1')))
        audit.append(dict(case=case.name,master=str(master),diagnostic=str(d),excluded=excluded,
                          master_rows=len(mr),end_sec=max(load),qdisc_types=sorted({r['queue_disc_type'] for r in lr})))
    for seed in {key[1] for key in identities}:
        assert identities[80,seed] == identities[100,seed][:80]
    write(out / 'cycles.csv', cycles)
    metrics = ['ap0_rtt_ms','harmonic_mean','unsatisfied','video_tp_mbps','incoming_mbps',
               'handed_to_device_mbps','qdisc_drop_packets','qdisc_drop_fraction','device_full_fraction',
               'device_mean_bytes','qdisc_mean_bytes','rlc_max_hol_ms','monitor_rlc_max_hol_ms']
    cases = []
    for name in sorted({r['case'] for r in cycles}):
        rows = [r for r in cycles if r['case']==name and r['cycle_id']>=2]
        cases.append(dict(case=name,terminals=rows[0]['terminals'],seed=rows[0]['seed'],rate_mbps=rows[0]['rate_mbps'],
                          **{k:statistics.mean(r[k] for r in rows) for k in metrics}))
    write(out/'cases.csv', cases)
    groups=[]
    for rate in [80,120,160]:
        for n in [80,100]:
            rows=[r for r in cases if r['rate_mbps']==rate and r['terminals']==n]
            summary=dict(rate_mbps=rate,terminals=n,seeds=len(rows))
            for k in metrics:
                summary[k]=statistics.mean(r[k] for r in rows)
                summary[k+'_seed_sd']=statistics.stdev(r[k] for r in rows)
            groups.append(summary)
    write(out/'groups.csv',groups)
    (out/'audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    for r in groups:
        print({k:round(r[k],4) for k in ['rate_mbps','terminals','ap0_rtt_ms','harmonic_mean','unsatisfied','video_tp_mbps','incoming_mbps','device_full_fraction','qdisc_drop_fraction','qdisc_mean_bytes']})


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    analyze(args.root,args.output)
