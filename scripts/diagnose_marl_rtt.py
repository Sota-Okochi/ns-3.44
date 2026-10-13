#!/usr/bin/env python3
"""Replay ONE saved validation episode; never trains or changes existing artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
from types import SimpleNamespace

import torch
from run_marl import Config, Learner, run_episode, write_json


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-episode',type=Path,required=True)
    p.add_argument('--checkpoint',type=Path,required=True)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--trace',action='store_true')
    a=p.parse_args()
    source=a.source_episode.resolve();output=a.output.resolve();binary=a.binary.resolve()
    old=json.loads((source/'manifest.json').read_text())
    original_root=source.parent.parent
    manifest=json.loads((original_root/'manifest.json').read_text())
    if old['phase'] not in ('validation','test'): p.error('Only frozen evaluation replay supported')
    output.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(1);torch.use_deterministic_algorithms(True)
    learner=Learner(Config(**manifest['config']),old['model_seed'])
    ck=learner.load(a.checkpoint.resolve())
    if learner.updates!=old['policy_version']:p.error('Policy version differs')
    # Verify archived sampled log probabilities via replay outcome, never fit to them.
    args=SimpleNamespace(setting=source/'setting.json',capacity=Path(manifest['args']['capacity']),
        terminals=manifest['args']['terminals'],binary=binary,episode_timeout=7200,
        probe_count=manifest['args']['probe_count'],probe_interval=manifest['args']['probe_interval'],
        probe_timeout=manifest['args']['probe_timeout'],
        report_conn_timeout=manifest['args'].get('report_conn_timeout',3.))
    if ck.get('report_conn_timeout')!=args.report_conn_timeout:p.error('Transport conditions differ')
    if a.trace:
        wrapper=output/'traced-master.sh'
        wrapper.write_text('#!/bin/sh\nexec '+shlex.quote(str(binary))+' "$@" --marlRttTrace=1\n')
        wrapper.chmod(0o700);args.binary=wrapper
    write_json(output/'diagnosis_manifest.json',dict(source=str(source),checkpoint=str(a.checkpoint.resolve()),
        binary=str(binary),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        trace=a.trace,environment_seed=old['environment_seed'],episode_id=old['episode_id'],
        phase=old['phase'],policy_version=learner.updates))
    _, summary=run_episode(learner,args,old['episode_id'],old['environment_seed'],old['phase'],output)
    current=output/f"{old['phase']}_{old['episode_id']:06d}_seed{old['environment_seed']}"
    equal={}
    for filename in ('observations.jsonl','transitions.jsonl','qoe.csv'):
        equal[filename]=(source/filename).read_bytes()==(current/filename).read_bytes()
    write_json(output/'comparison.json',dict(exact_equal=equal,summary=summary))
    print(json.dumps(dict(exact_equal=equal,summary=summary)),flush=True)
    if not all(equal.values()):raise RuntimeError('Replay differs; do not attribute changes to passive tracing without investigation')


if __name__=='__main__':main()
