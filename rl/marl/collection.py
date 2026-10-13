"""Durable on-policy episode collection. Only trusted local torch artifacts are loaded."""
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
import hashlib
import json
from pathlib import Path
import shutil

import torch


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     allow_nan=False).encode()).hexdigest()


def policy_digest(learner):
    h = hashlib.sha256()
    for label, network in [('actor', learner.actor), ('critic', learner.critic)]:
        for name, tensor in sorted(network.state_dict().items()):
            h.update(f'{label}:{name}:{tuple(tensor.shape)}:{tensor.dtype}'.encode())
            h.update(tensor.detach().cpu().contiguous().numpy().tobytes())
    return h.hexdigest()


def atomic_save(path, value):
    path = Path(path)
    temp = path.with_suffix('.tmp')
    torch.save(value, temp)
    temp.replace(path)


def validate_result(result, phase):
    transitions, summary = result
    if len(transitions) != 4 or [t['done'] for t in transitions] != [False, False, False, True]:
        raise ValueError('Only complete four-step episodes may be cached')
    for tr in transitions:
        if not torch.isfinite(torch.tensor(tr['reward'])):
            raise ValueError('Invalid cached reward')
        for name in ('log_probs', 'value'):
            if not torch.isfinite(tr[name]).all():
                raise ValueError('Invalid cached policy statistic')
    if summary['phase'] != phase:
        raise ValueError('Cached phase mismatch')


def collect_durable(tasks, workers, execute, cache, identity, on_result, reuse=None):
    """Refill only after checking ALL newly finished futures for errors.

    Successful running episodes are drained and atomically saved on failure.
    A rejected/missing episode is never substituted or silently dropped.
    Returns in original task order, independent of completion order.
    """
    cache = Path(cache); cache.mkdir(parents=True, exist_ok=True)
    results = {}; remaining = []; failures = []
    for task in tasks:
        meta = dict(identity, episode_id=task[0], environment_seed=task[1])
        key = digest(meta)
        target = cache / (key+'.pt')
        source = Path(reuse)/(key+'.pt') if reuse else None
        if source and source.is_file():
            artifact = torch.load(source, map_location='cpu', weights_only=False)
            if artifact.get('identity') != meta:
                raise ValueError('Cache identity mismatch')
            validate_result(artifact['result'], identity['phase'])
            shutil.copy2(source, target)
            results[task] = artifact['result']
            on_result(artifact['result'], True)
        else:
            remaining.append((task, meta, target))
    with ThreadPoolExecutor(max_workers=workers) as pool:
        active = {}; iterator = iter(remaining)
        def submit():
            entry = next(iterator, None)
            if entry is None: return False
            task, meta, target = entry
            active[pool.submit(execute, task)] = entry
            return True
        for _ in range(workers): submit()
        try:
            while active:
                done, _ = wait(active, return_when=FIRST_COMPLETED)
                for future in done:
                    task, meta, target = active.pop(future)
                    try:
                        result = future.result()
                        validate_result(result, identity['phase'])
                        atomic_save(target, dict(identity=meta, result=result))
                        results[task] = result
                        on_result(result, False)
                    except Exception as exc:
                        failures.append(f'{task}: {exc}')
                if not failures:
                    for _ in done: submit()
        except KeyboardInterrupt:
            # No new tasks. Finish and persist only the processes we already launched.
            for future, (task, meta, target) in active.items():
                try:
                    result = future.result()
                    validate_result(result, identity['phase'])
                    atomic_save(target, dict(identity=meta, result=result))
                    on_result(result, False)
                except Exception:
                    pass
            raise
    if failures:
        raise RuntimeError('Collection paused; successful episodes preserved; no PPO update. '
                           + ' | '.join(failures))
    return [results[task] for task in tasks]
