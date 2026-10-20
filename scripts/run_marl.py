#!/usr/bin/env python3
"""Run isolated marl_ppo ns-3 episodes; default is a ONE-episode, no-update smoke run.

Never builds ns-3. Workers only collect from one frozen learner. Use --workers 1
until the 1 -> 2 -> 8 resource audit has been completed.
"""
import argparse
import csv
from dataclasses import asdict
import hashlib
import json
import math
import os
import signal
from pathlib import Path
import socketserver
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT/'scripts'))
import torch
from rl.marl.core import Config, Learner, SCHEMA, derive, environment_seed, observation
from run_capacity_scenario import FIELDS, generate_events
from rl.marl.collection import collect_durable, policy_digest, digest


# Fixed evaluation protocol for the new staged MARL experiment.
VALIDATION_SEEDS = tuple(range(1011, 1016))
VALIDATION_INTERVAL = 64


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, allow_nan=False)+'\n')


def append_csv(path, row):
    exists = path.exists()
    with path.open('a', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(row))
        if not exists:
            writer.writeheader()
        writer.writerow(row)


def write_capacity_events(path, events):
    # The ns-3 capacity reader uses getline plus exact header/row matching.
    # csv's default CRLF leaves a trailing CR and is rejected by that reader.
    with path.open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS, lineterminator='\n')
        writer.writeheader()
        writer.writerows(events)


class Session:
    """One episode only. No state is shared with other TCP endpoints."""
    def __init__(self, learner, n, env_seed, episode, phase, directory):
        self.learner, self.n, self.env_seed = learner, n, env_seed
        self.episode, self.phase, self.directory = episode, phase, directory
        self.cycle, self.previous, self.pending, self.ended = 0, None, None, False
        self.transitions, self.hs = [], []
        self.error = None
        tag = 'action_train' if phase in ('train', 'smoke') else 'action_'+phase
        args = (learner.seed, episode, env_seed) if tag == 'action_train' else (learner.seed, env_seed, 0)
        self.action_seed = derive(tag, *args)
        self.generator = torch.Generator().manual_seed(self.action_seed)
        self.run_id = f'{phase}_m{learner.seed}_e{episode}_s{env_seed}_p{learner.updates}'

    def handle(self, msg):
        if self.ended or msg.get('cycle') != self.cycle+1 or msg.get('seed') != self.env_seed or len(msg.get('users', [])) != self.n:
            raise ValueError('episode identity/cycle mismatch')
        obs, raw = observation(msg, self.learner.config, self.previous)
        with (self.directory/'observations.jsonl').open('a') as f:
            f.write(json.dumps(dict(run_id=self.run_id, policy_version=self.learner.updates,
                                   message=msg, evaluated=raw), allow_nan=False)+'\n')
        if raw['h'] is None:
            missing = [dict(ue_id=u['ue_id'], bs=u['bs'], app=u['app'],
                            tp_status=u['tp_status'], rtt_status=u['rtt_status'])
                       for u in raw['users'] if u['s_eff'] is None]
            raise ValueError(f'Evaluation missing at cycle {msg["cycle"]}: H/reward remain null; '
                             f'episode quarantined; missing_users={missing}. See observations.jsonl')
        applied = [u['bs'] for u in msg['users']]
        reward = None
        if self.pending is not None:
            if self.pending['action'].tolist() != applied:
                raise ValueError('Requested/applied assignment mismatch')
            reward = raw['h']
            self.transitions.append(dict(**self.pending, reward=reward, done=msg['done'], next_obs=obs))
            with (self.directory/'transitions.jsonl').open('a') as f:
                f.write(json.dumps(dict(run_id=self.run_id, policy_version=self.learner.updates,
                    decision_id=self.cycle, next_cycle=msg['cycle'], reward=reward,
                    requested_assignment=self.pending['action'].tolist(), applied_assignment=applied,
                    old_log_probs=self.pending['log_probs'].tolist(), old_value=float(self.pending['value']),
                    done=msg['done']), allow_nan=False)+'\n')
        action = torch.tensor(applied)
        self.pending = None
        if not msg['done']:
            action, lp, value = self.learner.act(obs, self.generator)
            self.pending = dict(obs=obs, action=action, log_probs=lp, value=value)
        for i, u in enumerate(raw['users']):
            old = self.previous[i] if self.previous is not None else u
            append_csv(self.directory/'qoe.csv', dict(run_id=self.run_id, seed=self.env_seed,
                model_seed=self.learner.seed, action_seed=self.action_seed, cycle_id=msg['cycle'],
                sim_time=msg['sim_time'], ue_id=i, current_bs_id=u['bs'], previous_bs_id=old['bs'],
                requested_bs_id=int(action[i]), app_type=u['app'], tp_mbps=u['tp_mbps'],
                rtt_ms=u['rtt_ms'], tp_status=u['tp_status'], rtt_status=u['rtt_status'],
                satisfaction_raw=u['s_raw'], satisfaction=u['s_eff'], satisfaction_class='legacy_not_used',
                delta_satisfaction=u['delta_satisfaction'], rsrp_or_rssi='', position_x='', position_y='',
                position_status='see_initial_positions', num_users_on_bs=raw['aps'][u['bs']]['num_users'],
                harmonic_mean=raw['h'], num_unsatisfied_users='', switch_flag=int(old['bs'] != u['bs']),
                requested_switch_flag=int(action[i] != u['bs']), reward=reward, policy_version=self.learner.updates))
        self.previous = raw['users']; self.cycle += 1
        self.hs.append(raw['h']); self.ended = msg['done']
        return f'MARL_PPO_V1 {self.cycle} {self.learner.updates} {raw["h"]:.17g} '+ ' '.join(map(str, action.tolist()))+'\n'


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        session = self.server.session
        try:
            self.connection.settimeout(30)
            wire = self.rfile.readline(2_000_001)
            if len(wire) > 2_000_000 or not wire.endswith(b'\n'):
                raise ValueError('Oversized/incomplete request')
            reply = session.handle(json.loads(wire))
        except Exception as exc:
            session.error = str(exc)
            reply = 'ERROR '+str(exc).replace('\n', ' ')+'\n'
        self.wfile.write(reply.encode())


class Server(socketserver.TCPServer):
    allow_reuse_address = False


def run_episode(learner, args, episode, env_seed, phase, base):
    directory = base/f'{phase}_{episode:06d}_seed{env_seed}'
    directory.mkdir(parents=True, exist_ok=False)
    setting = json.loads(args.setting.read_text())
    setting.update(terminals=args.terminals, rngSeed=env_seed, numCycles=5)
    write_json(directory/'setting.json', setting)
    scenario = json.loads(args.capacity.read_text())
    variation = dict(scenario['capacity_variation'])
    capacity_seed = 100000+env_seed
    if variation['mode'] in ('random', 'random_walk'):
        variation['event_seed'] = capacity_seed
    events = generate_events(variation, 5)
    write_capacity_events(directory/'planned_events.csv', events)
    initial = [80_000_000, 40_000_000, 20_000_000]
    if variation['mode'] == 'random_walk':
        for link in variation['links']:
            initial[link['ap_id']] = link['initial_bps']
    session = Session(learner, args.terminals, env_seed, episode, phase, directory)
    with Server(('127.0.0.1', 0), Handler) as server:
        server.session = session
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        command = [str(args.binary), f'--settingPath={directory/"setting.json"}', '--method=marl_ppo',
                   f'--rngSeed={env_seed}', '--mob=1', f'--outputRoot={directory/"simulation"}',
                   f'--capacityEventsPath={directory/"planned_events.csv"}',
                   f'--pgwCerRate={initial[0]}bps', f'--ap1BackhaulRate={initial[1]}bps',
                   f'--ap2BackhaulRate={initial[2]}bps', f'--drlServerPort={server.server_address[1]}',
                   '--drlTimeoutMs=30000', f'--marlProbeCount={args.probe_count}',
                   f'--marlProbeInterval={args.probe_interval}', f'--marlProbeTimeout={args.probe_timeout}',
                   f'--marlReportConnTimeout={args.report_conn_timeout}']
        write_json(directory/'manifest.json', dict(schema=SCHEMA, command=command,
            run_id=session.run_id, environment_seed=env_seed, capacity_seed=capacity_seed,
            action_seed=session.action_seed, model_seed=learner.seed, phase=phase, episode_id=episode,
            policy_version=learner.updates, capacity=variation, report_conn_timeout=args.report_conn_timeout,
            events_sha256=hashlib.sha256((directory/'planned_events.csv').read_bytes()).hexdigest()))
        start = time.monotonic(); result = None
        error = None
        try:
            # GNU time measures this child's peak RSS; does not inspect/stop unrelated jobs.
            timed = ['/usr/bin/time', '-f', '%M', '-o', str(directory/'max_rss_kib.txt'), *command]
            with (directory/'console.log').open('w') as log:
                process = subprocess.Popen(timed, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=True)
                try:
                    code = process.wait(timeout=args.episode_timeout)
                    result = subprocess.CompletedProcess(timed, code)
                except BaseException:
                    # Only the process group created for THIS episode, never existing jobs.
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
                    raise
        except Exception as exc:
            error = str(exc)
        finally:
            server.shutdown(); thread.join()
        status = dict(returncode=result.returncode if result is not None else None,
                      error=error or session.error, seconds=time.monotonic()-start,
                      terminal=session.ended, transitions=len(session.transitions))
        rss = directory/'max_rss_kib.txt'
        status['max_rss_kib'] = int(rss.read_text().splitlines()[-1]) if rss.exists() and rss.read_text().splitlines()[-1].isdigit() else None
        ok = status['returncode'] == 0 and not status['error'] and session.ended and len(session.transitions) == 4
        status['valid'] = ok
        write_json(directory/'status.json', status)
        if not ok:
            raise RuntimeError(f'Episode quarantined: {directory}; {status}')
    summary = dict(phase=phase, episode_id=episode, environment_seed=env_seed, model_seed=learner.seed,
                   policy_version=learner.updates, return_sum=sum(session.hs[1:]),
                   mean_h_after=sum(session.hs[1:])/4, mean_h_all=sum(session.hs)/5,
                   initial_h=session.hs[0], source_directory=str(directory), seconds=status['seconds'],
                   max_rss_kib=status['max_rss_kib'])
    return session.transitions, summary


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mode', choices=['smoke', 'train', 'validation', 'test'], default='smoke')
    p.add_argument('--episodes', type=int, default=1)
    p.add_argument('--episode-start', type=int, default=0,
                   help='Smoke only: original episode index to reproduce environment/action seeds')
    p.add_argument('--terminals', type=int, default=30)
    p.add_argument('--model-seed', type=int, default=1)
    p.add_argument('--workers', type=int, choices=[1, 2, 8], default=1)
    p.add_argument('--parallel-validated', action='store_true', help='Explicit acknowledgement: resource audit for requested parallelism completed')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--setting', type=Path, default=ROOT/'data/setting.json')
    p.add_argument('--capacity', type=Path, default=ROOT/'configs/capacity/constant.json')
    p.add_argument('--config', type=Path, help='Optional JSON Config overrides, saved in manifest')
    p.add_argument('--binary', type=Path, required=True, help='Already built master binary; this runner never builds')
    p.add_argument('--checkpoint', type=Path, help='Trusted local marl checkpoint only')
    p.add_argument('--recover-run', type=Path, help='Trusted stopped run: restore recovery.pt and reuse matching completed episodes into NEW output')
    p.add_argument('--report-conn-timeout', type=float, default=.2)
    p.add_argument('--resume', action='store_true', help='Resume latest at an update boundary, to total --episodes')
    p.add_argument('--episode-timeout', type=float, default=3600)
    p.add_argument('--probe-count', type=int, default=10)
    p.add_argument('--probe-interval', type=float, default=.2)
    p.add_argument('--probe-timeout', type=float, default=1.)
    args = p.parse_args(argv)
    if args.recover_run:
        if args.checkpoint or args.resume or args.mode != 'train':
            p.error('--recover-run requires train and cannot combine with --checkpoint/--resume')
        args.recover_run = args.recover_run.resolve()
        args.checkpoint = args.recover_run/'recovery.pt'
        args.resume = True
    if args.episode_start < 0 or (args.episode_start and args.mode != 'smoke'):
        p.error('--episode-start must be nonnegative and is only supported in smoke mode')
    for name in ('output', 'setting', 'capacity', 'binary'):
        setattr(args, name, getattr(args, name).resolve())
    if args.checkpoint: args.checkpoint = args.checkpoint.resolve()
    if args.episodes < 1 or args.terminals < 1 or args.model_seed < 1 or not math.isfinite(args.episode_timeout) or args.episode_timeout <= 0:
        p.error('Counts, model seed and timeout must be positive')
    if args.workers > 1 and not args.parallel_validated:
        p.error('Perform the 1 -> 2 -> 8 resource audit; acknowledge with --parallel-validated')
    if not args.binary.is_file(): p.error('Build master separately and supply --binary')
    if args.mode in ('validation', 'test') and not args.checkpoint: p.error('Evaluation requires --checkpoint')
    if args.resume and (args.mode != 'train' or not args.checkpoint): p.error('--resume needs train and --checkpoint')
    if args.mode == 'train' and args.checkpoint and not args.resume: p.error('Training checkpoint requires --resume')
    config = Config(**(json.loads(args.config.read_text()) if args.config else {})); config.validate()
    if args.mode == 'train' and args.episodes % config.rollout_episodes:
        p.error('Training episode budget must be a multiple of rollout_episodes; use smoke for one episode')
    if args.probe_count < 1 or not all(math.isfinite(v) and v > 0 for v in (args.probe_interval, args.probe_timeout, args.report_conn_timeout)):
        p.error('Invalid probe configuration')
    args.output.mkdir(parents=True, exist_ok=False)
    torch.set_num_threads(1)
    torch.use_deterministic_algorithms(True)
    learner = Learner(config, args.model_seed)
    checkpoint = learner.load(args.checkpoint) if args.checkpoint else {}
    start_episode = checkpoint.get('completed_episodes', 0) if args.resume else 0
    if args.resume and (checkpoint.get('terminals') != args.terminals or checkpoint.get('capacity') != json.loads(args.capacity.read_text()) or checkpoint.get('setting') != json.loads(args.setting.read_text()) or checkpoint.get('probe') != [args.probe_count, args.probe_interval, args.probe_timeout] or checkpoint.get('report_conn_timeout') != args.report_conn_timeout):
        p.error('Resume environment differs from checkpoint')
    manifest = dict(args={k:str(v) if isinstance(v, Path) else v for k,v in vars(args).items()},
                    config=asdict(config), report_transport='internal_callback_v1', validation_seeds=list(VALIDATION_SEEDS), validation_interval=VALIDATION_INTERVAL, schema=SCHEMA, seed_schema='marl_seed_v1', torch_version=torch.__version__,
                    python=sys.version, binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest())
    sources = list((ROOT/'rl/marl').glob('*.py'))+[Path(__file__), ROOT/'contrib/kameda/model/server/marl-ppo.cc',
               ROOT/'contrib/kameda/model/client/APMonitorTerminal.cc', ROOT/'master/applications.cc']
    manifest['source_sha256'] = {str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources}
    # Bind reused data to actual runtime libraries, not just the launcher binary.
    libraries = args.binary.parent/'lib'
    manifest['library_sha256'] = {f.name:hashlib.sha256(f.read_bytes()).hexdigest()
                                  for f in sorted(libraries.glob('*.so*')) if f.is_file()}
    experiment = dict(validation_seeds=list(VALIDATION_SEEDS), validation_interval=VALIDATION_INTERVAL, config=asdict(config), model_seed=args.model_seed, terminals=args.terminals,
                      capacity=json.loads(args.capacity.read_text()), setting=json.loads(args.setting.read_text()),
                      probe=[args.probe_count,args.probe_interval,args.probe_timeout],
                      report_conn_timeout=args.report_conn_timeout, report_transport='internal_callback_v1', binary=manifest['binary_sha256'],
                      libraries=manifest['library_sha256'], sources=manifest['source_sha256'],
                      torch=manifest['torch_version'], python=sys.version, seed_schema='marl_seed_v1')
    experiment_id = digest(experiment)
    manifest['experiment_id'] = experiment_id
    if args.resume and checkpoint.get('experiment_id') != experiment_id:
        p.error('Checkpoint experiment identity differs; do not mix conditions/code/model versions')
    write_json(args.output/'manifest.json', manifest)
    if start_episode > args.episodes or start_episode % config.rollout_episodes:
        p.error('Invalid resume progress / total episode budget')
    launch = time.monotonic()
    best_score = checkpoint.get('best_score', -math.inf)
    if args.resume:
        source_best = args.checkpoint.parent/'best.pt'
        if math.isfinite(best_score):
            if not source_best.is_file():
                p.error('Resume needs best.pt alongside latest to preserve previous selected model')
            import shutil
            shutil.copy2(source_best, args.output/'best.pt')

    completed = start_episode

    def collect(tasks, phase, base):
        # Snapshot BEFORE collecting; recovery also works during initial/final validation.
        save('recovery.pt', completed, best_score, pending_phase=phase)
        identity = dict(experiment_id=experiment_id, policy=policy_digest(learner),
                        policy_version=learner.updates, phase=phase)
        def record(result, reused):
            row = dict(result[1], reused=reused, wall_elapsed_seconds=time.monotonic()-launch)
            append_csv(args.output/'episodes.csv', row)
            print(json.dumps(row), flush=True)
        try:
            results = collect_durable(tasks, args.workers,
                lambda task: run_episode(learner,args,task[0],task[1],phase,base),
                args.output/'trajectory_cache', identity, record,
                args.recover_run/'trajectory_cache' if args.recover_run else None)
        except (Exception, KeyboardInterrupt) as exc:
            write_json(args.output/'interrupted.json', dict(status='collection_incomplete',
                       completed_episodes=completed, phase=phase, error=str(exc)))
            raise
        return [r[0] for r in results], [r[1] for r in results]

    def save(name, count, score, **extra):
        learner.save(args.output/name, completed_episodes=count, terminals=args.terminals,
                     capacity=json.loads(args.capacity.read_text()), best_score=score,
                     validation_seeds=list(VALIDATION_SEEDS), validation_interval=VALIDATION_INTERVAL,
                     setting=json.loads(args.setting.read_text()), seed_schema='marl_seed_v1',
                     probe=[args.probe_count, args.probe_interval, args.probe_timeout],
                     report_conn_timeout=args.report_conn_timeout, experiment_id=experiment_id, **extra)

    def validate(count):
        nonlocal best_score
        _, rows = collect(list(enumerate(VALIDATION_SEEDS)), 'validation', args.output/f'checkpoint_{count:06d}')
        score = sum(r['mean_h_after'] for r in rows)/len(VALIDATION_SEEDS)
        append_csv(args.output/'validation.csv', dict(episodes=count, steps=count*4, model_seed=learner.seed,
                   score=score, wall_elapsed_seconds=time.monotonic()-launch))
        # Save every evaluated checkpoint, including untrained, for audit.
        if score > best_score:
            best_score = score; save('best.pt', count, best_score)
        save(f'checkpoint_{count:06d}.pt', count, best_score)

    if args.mode in ('validation', 'test'):
        seeds = VALIDATION_SEEDS if args.mode == 'validation' else range(1001,1011)
        collect(list(enumerate(seeds)), args.mode, args.output)
    elif args.mode == 'smoke':
        collect([(e, environment_seed(learner.seed,e))
                 for e in range(args.episode_start, args.episode_start+args.episodes)], 'smoke', args.output)
    else:
        interval = VALIDATION_INTERVAL
        if not args.resume: validate(0)
        elif args.recover_run and checkpoint.get('pending_phase') == 'validation': validate(start_episode)
        for begin in range(start_episode, args.episodes, config.rollout_episodes):
            count = begin+config.rollout_episodes
            episodes, _ = collect([(e, environment_seed(learner.seed,e)) for e in range(begin,count)], 'train', args.output)
            metrics = learner.update(episodes)
            completed = count
            append_csv(args.output/'updates.csv', dict(episodes=count, **metrics))
            save('latest.pt', count, best_score)
            if count % interval == 0 or count == args.episodes:
                validate(count)
                save('latest.pt', count, best_score)
    if args.mode == 'train':
        save('recovery.pt', completed, best_score, pending_phase='complete')
    write_json(args.output/'completion.json', dict(status='complete', mode=args.mode,
               seconds=time.monotonic()-launch, updates=learner.updates))


if __name__ == '__main__':
    main()
