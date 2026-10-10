#!/usr/bin/env python3
"""Prepare and sequentially run isolated 80/100 UE capacity comparisons."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import random
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def app_profile(seed, terminals):
    if terminals % 20:
        raise ValueError('terminals must be a multiple of 20 for exact 20/40/15/25% ratios')
    rng = random.Random(seed)
    apps = []
    for _ in range(terminals // 20):
        block = [1]*4 + [2]*8 + [3]*3 + [4]*5
        rng.shuffle(block)
        apps.extend(block)
    return apps


def save(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2) + '\n')
    temp.replace(path)


def prepare(args):
    base = json.loads(args.setting.read_text())
    directory = args.directory.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    items = []
    for rate in args.rates:
        for seed in args.seeds:
            for n in [80, 100]:
                case = directory / f'{args.method}_n{n}_seed{seed}_{rate}Mbps'
                case.mkdir()
                cfg = dict(base, terminals=n, rngSeed=seed)
                save(case / 'setting.json', cfg)
                apps = app_profile(seed, n)
                (case / 'apps.txt').write_text('\n'.join(map(str, apps)) + '\n')
                command = [str(ROOT / 'build/master/ns3.44-master-optimized'),
                           f'--method={args.method}', f'--rngSeed={seed}',
                           f'--settingPath={case / "setting.json"}',
                           f'--appTypesPath={case / "apps.txt"}',
                           f'--outputRoot={case / "OUTPUT"}', f'--pgwCerRate={rate}Mbps',
                           '--queueDiagnostics=1', '--queueSampleSec=0.1']
                items.append(dict(case=str(case), terminals=n, seed=seed, rate_mbps=rate,
                                  app_counts=dict(Counter(apps)), command=command, status='pending'))
    save(directory / 'manifest.json', dict(method=args.method,
         app_profile='seeded shuffled blocks of 20; exact 20/40/15/25%; nested UE prefixes',
         items=items))
    print(f'Prepared {len(items)} cases in {directory}; none executed.')


def run(args):
    def interrupt(signum, frame):
        raise KeyboardInterrupt(f'Received signal {signum}')

    signal.signal(signal.SIGTERM, interrupt)
    directory = args.directory.resolve()
    manifest_path = directory / 'manifest.json'
    # Refuse concurrent launchers for this suite. Remove a stale lock only after checking its PID.
    lock = directory / 'runner.lock'
    with lock.open('x') as f:
        f.write(str(os.getpid()) + '\n')
    try:
        manifest = json.loads(manifest_path.read_text())
        for item in manifest['items']:
            if item['status'] != 'pending' or item['rate_mbps'] != args.rate or item['seed'] != args.seed:
                continue
            binary = Path(item['command'][0])
            fingerprint = hashlib.sha256(binary.read_bytes()).hexdigest()
            if manifest.get('binary_sha256', fingerprint) != fingerprint:
                raise SystemExit('Binary changed within suite; prepare a new suite for a fair comparison.')
            manifest['binary_sha256'] = fingerprint
            item.update(status='running', started_unix=time.time(),
                        binary_sha256=fingerprint)
            save(manifest_path, manifest)
            print('Running', item['case'], flush=True)
            env = os.environ.copy()
            env['LD_LIBRARY_PATH'] = str(ROOT / 'build/lib') + ':' + env.get('LD_LIBRARY_PATH', '')
            try:
                with (Path(item['case']) / 'stdout.log').open('w') as log:
                    child = subprocess.Popen(item['command'], cwd=ROOT, env=env,
                                             stdout=log, stderr=subprocess.STDOUT)
                    item['pid'] = child.pid
                    save(manifest_path, manifest)
                    try:
                        code = child.wait()
                    except BaseException:
                        child.terminate()
                        child.wait()
                        raise
                item.update(status='completed' if code == 0 else 'failed', returncode=code)
            except BaseException:
                item['status'] = 'interrupted'
                raise
            finally:
                item['finished_unix'] = time.time()
                save(manifest_path, manifest)
            if code:
                raise SystemExit(f'Case failed ({code}); see stdout.log')
            print('Completed', item['case'], flush=True)
    finally:
        lock.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('directory', type=Path)
    p.add_argument('--setting', type=Path, default=ROOT / 'data/setting.json')
    p.add_argument('--method', choices=['logistic', 'random', 'no_switch'], default='logistic')
    p.add_argument('--rates', type=int, nargs='+', default=[80, 120, 160])
    p.add_argument('--seeds', type=int, nargs='+', default=[1001, 1002])
    p.set_defaults(func=prepare)
    p = sub.add_parser('run')
    p.add_argument('directory', type=Path)
    p.add_argument('--rate', type=int, required=True)
    p.add_argument('--seed', type=int, required=True)
    p.set_defaults(func=run)
    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
