#!/usr/bin/env python3
"""Single-policy or paired, sequential four-policy timing run. Evaluation only; no training or rebuild.

Uses a fresh output directory, shared seed/settings/capacity events, CPU/one Torch
thread. Runs full five-cycle episodes (four decisions); never reuses old timings.
"""
import argparse
from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import time
from types import SimpleNamespace

import torch
import run_marl as marl
from summarize_assignment_timing import summarize, METHODS

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, directory, timeout):
    marl.write_json(directory/'command.json', command)
    with (directory/'console.log').open('w') as out:
        process = subprocess.Popen(command, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
            if code != 0: raise RuntimeError(f'Command failed ({code}): {directory}')
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL); process.wait()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--method', choices=('all', *METHODS), default='all',
                   help='Run one method in this terminal, or all methods sequentially')
    p.add_argument('--port', choices=('auto',), default='auto',
                   help='Automatically allocate the local DQN/MARL server port')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--seed', type=int, required=True)
    p.add_argument('--terminals', type=int, default=80)
    p.add_argument('--setting', type=Path, default=ROOT/'data/setting.json')
    p.add_argument('--capacity', type=Path, default=ROOT/'configs/capacity/random_walk_all.json')
    p.add_argument('--dqn-checkpoint', type=Path)
    p.add_argument('--marl-checkpoint', type=Path)
    p.add_argument('--max-switches', type=int, default=4)
    p.add_argument('--safety-threshold', type=float, default=.001)
    p.add_argument('--episode-timeout', type=float, default=14400)
    p.add_argument('--probe-count', type=int, default=10)
    p.add_argument('--probe-interval', type=float, default=.2)
    p.add_argument('--probe-timeout', type=float, default=1.)
    p.add_argument('--report-conn-timeout', type=float, default=.2)
    args = p.parse_args()
    if args.seed < 1 or args.terminals < 1 or args.max_switches < 1 or args.episode_timeout <= 0:
        p.error('Seed, terminal count, switch budget and timeout must be positive')
    methods = METHODS if args.method == 'all' else (args.method,)
    required_models = []
    if 'centralized_dqn' in methods: required_models.append('dqn_checkpoint')
    if 'marl_ppo' in methods: required_models.append('marl_checkpoint')
    for name in required_models:
        if getattr(args, name) is None: p.error(f'--{name.replace("_", "-")} is required')
    for name in ('binary', 'setting', 'capacity', *required_models):
        path = getattr(args, name).resolve()
        if not path.is_file(): p.error(f'Missing {name}: {path}')
        setattr(args, name, path)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    # Local trusted checkpoints only; architecture/configuration come from the checkpoint.
    schema = None
    learner = None
    if 'centralized_dqn' in methods:
        dqn = torch.load(args.dqn_checkpoint, map_location='cpu', weights_only=False)
        dqn_schema = dqn.get('extra', {}).get('schema_version', 'v1')
        schema = 'v2_onehot' if 'v2' in dqn_schema else 'v1'
    torch.set_num_threads(1)
    torch.use_deterministic_algorithms(True)
    if 'marl_ppo' in methods:
        ck = torch.load(args.marl_checkpoint, map_location='cpu', weights_only=False)
        learner = marl.Learner(marl.Config(**ck['config']), ck['seed'])
        learner.load(args.marl_checkpoint)
    model_paths = [getattr(args, name) for name in required_models]
    if 'logistic' in methods:
        model_paths.append(ROOT/'baseline_methods/data/models/logistic_model_v3.json')
    setting = json.loads(args.setting.read_text())
    setting.update(terminals=args.terminals, rngSeed=args.seed, numCycles=5)
    marl.write_json(args.output/'setting.json', setting)
    scenario = json.loads(args.capacity.read_text())
    variation = scenario['capacity_variation']
    if variation['mode'] in ('random', 'random_walk'): variation['event_seed'] = 100000 + args.seed
    marl.write_json(args.output/'capacity.json', scenario)
    events = marl.generate_events(variation, 5)
    marl.write_capacity_events(args.output/'planned_events.csv', events)
    initial = [80_000_000, 40_000_000, 20_000_000]
    if variation['mode'] == 'random_walk':
        for link in variation['links']: initial[link['ap_id']] = link['initial_bps']
    source_files = [ROOT/'contrib/kameda/model/server'/f for f in
        ('APselection.cc', 'APselection.h', 'assignment-compute-timer.h', 'marl-ppo.cc')]
    source_files += [ROOT/'rl/centralized_server.py', ROOT/'rl/marl/core.py',
                     Path(__file__), ROOT/'scripts/run_marl.py', ROOT/'scripts/summarize_assignment_timing.py']
    manifest = dict(timing_version=2, args={k:str(v) if isinstance(v, Path) else v for k,v in vars(args).items()},
        method_order=methods, seed=args.seed, decision_cycles=4, torch_threads=1, device='cpu',
        python=sys.version, torch=torch.__version__, platform=platform.platform(),
        cpu=Path('/proc/cpuinfo').read_text() if Path('/proc/cpuinfo').exists() else platform.processor(),
        model_sha256={str(f):digest(f) for f in model_paths},
        binary_sha256=digest(args.binary),
        library_sha256={f.name:digest(f) for f in (args.binary.parent/'lib').glob('*.so*') if f.is_file()},
        source_sha256={str(f.relative_to(ROOT)):digest(f) for f in source_files},
        dqn_schema=schema, marl_config=asdict(learner.config) if learner else None,
        warmup='No discarded/warmed decision: cycle 1 is retained; model loading excluded',
        notes='Same seed does not imply identical observations: each policy changes subsequent network state; MARL uses its dedicated RTT/QoE measurement path.')
    marl.write_json(args.output/'manifest.json', manifest)
    common = [str(args.binary), f'--settingPath={args.output/"setting.json"}',
              f'--rngSeed={args.seed}', '--mob=1',
              f'--capacityEventsPath={args.output/"planned_events.csv"}',
              f'--pgwCerRate={initial[0]}bps', f'--ap1BackhaulRate={initial[1]}bps',
              f'--ap2BackhaulRate={initial[2]}bps', '--perfTiming=0', '--perfDetailed=0']
    status = dict(status='running', completed_methods=[])
    try:
        for method in methods:
            status['current_method'] = method
            marl.write_json(args.output/'status.json', status)
            print(f'START {method}, seed={args.seed}', flush=True)
            directory = args.output/method; directory.mkdir()
            command = common + [f'--method={method}', f'--outputRoot={directory/"simulation"}']
            if method == 'centralized_dqn':
                port_file = directory/'port.txt'
                server_command = [sys.executable, '-B', str(ROOT/'rl/centralized_server.py'),
                    '--port', args.port, '--port-file', str(port_file), '--checkpoint', str(args.dqn_checkpoint),
                    '--eval-only', '--device', 'cpu', '--torch-threads', '1', '--seed', str(args.seed)]
                marl.write_json(directory/'server_command.json', server_command)
                with (directory/'server.log').open('w') as out:
                    server = subprocess.Popen(server_command, cwd=ROOT, stdout=out, stderr=subprocess.STDOUT)
                    try:
                        deadline = time.monotonic() + 60
                        while not port_file.exists():
                            if server.poll() is not None or time.monotonic() > deadline:
                                raise RuntimeError('DQN server did not start; see server.log')
                            time.sleep(.1)
                        port = int(port_file.read_text())
                        command += [f'--drlServerPort={port}', '--drlTimeoutMs=30000',
                            '--centralizedDqnBootstrapCycles=0', f'--centralizedDqnStateSchema={schema}',
                            f'--maxSwitches={args.max_switches}', '--kScheduleType=fixed',
                            f'--onlineDqnSafetyThreshold={args.safety_threshold}']
                        run(command, directory, args.episode_timeout)
                    finally:
                        server.terminate()
                        try: server.wait(timeout=10)
                        except subprocess.TimeoutExpired: server.kill(); server.wait()
            elif method == 'marl_ppo':
                marl_args = SimpleNamespace(**vars(args))
                marl_args.setting = args.output/'setting.json'
                marl_args.capacity = args.output/'capacity.json'
                marl.run_episode(learner, marl_args, 0, args.seed, 'test', directory)
            else:
                run(command, directory, args.episode_timeout)
            status['completed_methods'].append(method)
            marl.write_json(args.output/'status.json', status)
            print(f'DONE {method}', flush=True)
        rows = summarize(sorted(args.output.rglob('assignment_timing_*.csv')), methods=methods)
        for row in rows: marl.append_csv(args.output/'summary.csv', row)
        status.update(status='complete', current_method=None)
        print(json.dumps(rows, indent=2), flush=True)
    except BaseException as exc:
        status.update(status='failed', error=str(exc))
        raise
    finally:
        marl.write_json(args.output/'status.json', status)


if __name__ == '__main__': main()
