#!/usr/bin/env python3
"""Check actual single-UE actions, next-cycle reward and completion in a reactive run."""
import argparse
import csv
import json
from pathlib import Path


def check(root):
    manifest=json.loads((root/'manifest.json').read_text())
    n=manifest['setting']['terminals'];cycles=manifest['setting']['numCycles']
    penalty=manifest['args']['switch_penalty']
    episodes=sorted(root.glob('episode_*'))
    assert len(episodes)==manifest['args']['episodes']
    for ep in episodes:
        status=json.loads((ep/'status.json').read_text())
        assert status['returncode']==0 and status['terminal'] and not status['error']
        logs=list(ep.glob('simulation/**/master_log_*.csv'));assert len(logs)==1
        with logs[0].open() as f: rows=list(csv.DictReader(f))
        assert len(rows)==n*cycles
        transitions=[json.loads(line) for line in (ep/'transitions.jsonl').read_text().splitlines()]
        assert len(transitions)==cycles-1
        for c in range(1,cycles+1):
            group=[r for r in rows if int(r['cycle_id'])==c]
            assert len({r['ue_id'] for r in group})==n
            assert sum(int(r['switch_flag']) for r in group)<=1
            if c==cycles:assert not any(int(r['switch_flag']) for r in group)
        for c,tr in enumerate(transitions,1):
            assert tr['cycle']==c and tr['next_cycle']==c+1 and tr['done']==(c==cycles-1)
            before=[r for r in rows if int(r['cycle_id'])==c]
            after=[r for r in rows if int(r['cycle_id'])==c+1]
            assert tr['switched']==sum(int(r['switch_flag']) for r in before)
            assert abs(tr['h_next']-float(after[0]['harmonic_mean']))<1e-6
            assert abs(tr['reward']-(tr['h_next']-penalty*tr['switched']/n))<1e-9
        assert len(list(ep.glob('simulation/**/capacity_schedule/*/simulation_completed.txt')))==1
        if manifest['args']['eval_only']:
            assert json.loads((ep/'learning.json').read_text())['updates']==0
        print('PASS',ep.name)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    check(p.parse_args().directory)
