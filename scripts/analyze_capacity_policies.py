#!/usr/bin/env python3
"""Reproduce random/logistic simultaneous-capacity comparisons; no simulation."""
import csv
import hashlib
import json
from pathlib import Path
from statistics import mean, stdev
from check_capacity_scenario import check


def read(p):
    with p.open() as f:return list(csv.DictReader(f))


def one(paths):
    p=list(paths);assert len(p)==1,p
    return p[0]


def write(p, rows):
    with p.open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)


def main():
    output=Path('results/capacity_policy_analysis');output.mkdir(exist_ok=True)
    global_rows=[];app_rows=[];totals=[];sources={};paired={}
    for method in ['random','logistic']:
        for root in sorted(Path('results',f'capacity_{method}').iterdir()):
            if not (root/'effective_config.json').exists():continue
            check(root)
            cfg=json.loads((root/'effective_config.json').read_text())
            run=one((root/'simulation').glob('*/*/capacity_schedule/*'))
            log=one((root/'simulation').glob('*/*/master_log_*.csv'))
            data=read(log);seed=cfg['seed']
            assert (seed,method) not in paired, 'Multiple runs for a seed/method need explicit selection'
            paired[seed,method]=(cfg,read(run/'initial_terminals.csv'),data)
            for path in [log,root/'effective_config.json',root/'console.log',run/'events.csv',run/'initial_terminals.csv']:
                sources[str(path)]=hashlib.sha256(path.read_bytes()).hexdigest()
            hs=[];us=[];sw=[]
            for cycle in range(1,cfg['setting']['numCycles']+1):
                g=[r for r in data if int(r['cycle_id'])==cycle]
                h=float(g[0]['harmonic_mean']);u=int(g[0]['num_unsatisfied_users']);s=sum(int(r['switch_flag']) for r in g)
                assert abs(h-len(g)/sum(1/float(r['satisfaction']) for r in g))<1e-5
                assert u==sum(float(r['satisfaction'])<.5 for r in g)
                hs.append(h);us.append(u);sw.append(s)
                global_rows.append(dict(method=method,seed=seed,cycle=cycle,harmonic_mean=h,unsatisfied=u,
                    switches=s,invalid=sum(r['measurement_valid']!='1' for r in g)))
                for ap in range(3):
                    for app in range(1,5):
                        z=[r for r in g if int(r['current_bs_id'])==ap and int(r['app_type'])==app]
                        if z:app_rows.append(dict(method=method,seed=seed,cycle=cycle,ap=ap,app=app,n=len(z),
                            mean_tp=mean(float(r['tp_mbps']) for r in z),mean_rtt=mean(float(r['rtt_ms']) for r in z),
                            mean_satisfaction=mean(float(r['satisfaction']) for r in z),
                            reciprocal_sum=sum(1/float(r['satisfaction']) for r in z)))
            totals.append(dict(method=method,seed=seed,mean_h=mean(hs),post_initial_mean_h=mean(hs[1:]),
                               mean_unsatisfied=mean(us),switches=sum(sw)))
    for seed in sorted({k[0] for k in paired}):
        a,b=paired[seed,'random'],paired[seed,'logistic']
        assert a[0]['setting']==b[0]['setting'] and a[0]['events']==b[0]['events']
        assert a[1]==b[1]
        for x,y in zip(a[2][:80],b[2][:80]):
            for key in ['ue_id','current_bs_id','app_type','tp_mbps','rtt_ms','satisfaction']:
                assert x[key]==y[key]
    write(output/'global_by_cycle.csv',global_rows);write(output/'by_ap_app_cycle.csv',app_rows)
    write(output/'summary_by_seed.csv',totals)
    aggregate=[]
    for method in ['random','logistic']:
        x=[r for r in totals if r['method']==method]
        aggregate.append(dict(method=method,n_seeds=len(x),mean_h=mean(r['mean_h'] for r in x),
            sample_sd_h=stdev(r['mean_h'] for r in x) if len(x)>1 else 0,
            mean_switches=mean(r['switches'] for r in x)))
    write(output/'summary_across_seeds.csv',aggregate)
    (output/'manifest.json').write_text(json.dumps(dict(sources=sources,
        note='Legacy QoE; H is measured before the same-cycle selected action; two seeds only.'),indent=2))
    print(totals);print(aggregate)


if __name__=='__main__':main()
