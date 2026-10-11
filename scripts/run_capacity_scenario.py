#!/usr/bin/env python3
"""Generate/save a capacity schedule, then run ns-3; fixed variation is the default."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
NORMAL = {0: 80_000_000, 1: 40_000_000, 2: 20_000_000}
FIELDS = ['cycle_id', 'ap_id', 'rate_bps', 'direction']
SCENARIOS = dict(fixed='fixed_all.json', random_walk='random_walk_all.json',
                 random='random_ap0.json', constant='constant.json')


def integer(value, name, minimum=0):
    if type(value) is not int or value < minimum:
        raise ValueError(f'{name} must be an integer >= {minimum}')
    return value


def validate_events(events, cycles):
    seen = set()
    result = []
    for event in events:
        if set(event) != set(FIELDS):
            raise ValueError(f'Event must have exactly {FIELDS}')
        cycle = integer(event['cycle_id'], 'cycle_id', 1)
        ap = integer(event['ap_id'], 'ap_id')
        rate = integer(event['rate_bps'], 'rate_bps', 1)
        if cycle > cycles or ap not in NORMAL or rate > 2**64-1 or event['direction'] != 'both':
            raise ValueError(f'Invalid capacity event: {event}')
        if (cycle, ap) in seen:
            raise ValueError('Duplicate event for same cycle and AP')
        seen.add((cycle, ap))
        result.append(dict(cycle_id=cycle, ap_id=ap, rate_bps=rate, direction='both'))
    return sorted(result, key=lambda e: (e['cycle_id'], e['ap_id']))


def random_walk_links(config):
    """Validate before drawing; JSON order must not affect the capacity process."""
    integer(config['event_seed'], 'event_seed')
    if config['boundary'] != 'clip':
        raise ValueError('random_walk requires boundary=clip')
    if integer(config['update_interval_cycles'], 'update_interval_cycles', 1) != 1:
        raise ValueError('random_walk requires update_interval_cycles=1')
    links = config['links']
    if not isinstance(links, list) or len(links) != len(NORMAL):
        raise ValueError('random_walk requires exactly three links')
    by_ap = {}
    for link in links:
        if not isinstance(link, dict):
            raise ValueError('Each random_walk link must be an object')
        ap = integer(link['ap_id'], 'ap_id')
        if ap not in NORMAL or ap in by_ap:
            raise ValueError('Unknown or duplicate random_walk AP')
        for key in ('initial_bps', 'min_bps', 'max_bps', 'step_bps'):
            if integer(link[key], key, 1) > 2**64 - 1:
                raise ValueError(f'{key} must fit uint64')
        lo, hi, initial, step = (link[k] for k in
                                ('min_bps', 'max_bps', 'initial_bps', 'step_bps'))
        if not lo < hi or not lo <= initial <= hi:
            raise ValueError('Invalid random_walk capacity bounds/initial value')
        if (hi-lo) % step or (initial-lo) % step:
            raise ValueError('random_walk bounds and initial value must lie on the step grid')
        by_ap[ap] = link
    return [by_ap[ap] for ap in sorted(NORMAL)]


def generate_events(config, cycles):
    integer(cycles, 'numCycles', 1)
    mode = config['mode']
    if mode == 'constant':
        return []
    if mode == 'fixed':
        return validate_events(config['events'], cycles)
    if mode == 'random_walk':
        links = random_walk_links(config)
        rng = random.Random(config['event_seed'])
        current = {link['ap_id']: link['initial_bps'] for link in links}
        events = []
        for cycle in range(1, cycles + 1):
            for link in links:
                ap = link['ap_id']
                candidate = current[ap] + rng.choice([-1, 0, 1]) * link['step_bps']
                current[ap] = min(max(candidate, link['min_bps']), link['max_bps'])
                # Keep no-op updates: the saved CSV is the complete per-cycle schedule.
                events.append(dict(cycle_id=cycle, ap_id=ap,
                                   rate_bps=current[ap], direction='both'))
        return validate_events(events, cycles)
    if mode != 'random':
        raise ValueError('mode must be fixed, random_walk, random or constant')
    seed = integer(config['event_seed'], 'event_seed')
    rng = random.Random(seed)  # Independent of ns-3 and policy RNGs.
    ap = integer(config['ap_id'], 'ap_id')
    rates = config['low_rates_bps']
    if ap not in NORMAL or not isinstance(rates, list) or not rates:
        raise ValueError('Invalid random AP/rates')
    for rate in rates:
        integer(rate, 'low_rate_bps', 1)
        if rate >= NORMAL[ap]:
            raise ValueError('Reduced rate must be below normal capacity')
    def bounds(name):
        value = config[name]
        if not isinstance(value, list) or len(value) != 2:
            raise ValueError(f'{name} must contain two integers')
        lo, hi = [integer(x, name, 1) for x in value]
        if lo > hi:
            raise ValueError(f'Reversed {name}')
        return lo, hi
    drop_range = bounds('drop_cycle_range')
    duration_range = bounds('duration_cycles_range')
    # Reject ambiguous/out-of-range configs instead of silently clamping draws.
    if drop_range[1] + duration_range[1] > cycles:
        raise ValueError('Latest possible recovery exceeds numCycles')
    drop = rng.randint(*drop_range)
    recover = drop + rng.randint(*duration_range)
    low = rng.choice(rates)
    return validate_events([
        dict(cycle_id=drop, ap_id=ap, rate_bps=low, direction='both'),
        dict(cycle_id=recover, ap_id=ap, rate_bps=NORMAL[ap], direction='both')], cycles)


def read_events(path, cycles):
    with path.open(newline='') as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != FIELDS:
            raise ValueError('Invalid replay CSV header')
        events = []
        for row in reader:
            if None in row or any(v is None for v in row.values()):
                raise ValueError('Malformed replay row')
            events.append({k: int(v) if k != 'direction' else v for k, v in row.items()})
    return validate_events(events, cycles)


def resolve(path):
    p = Path(path)
    return p if p.is_absolute() else ROOT / p


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scenario', help='Scenario JSON; defaults to the selected mode preset')
    parser.add_argument('--capacity-mode', choices=SCENARIOS,
                        help='Select capacity preset; default fixed (not constant)')
    parser.add_argument('--method', default='no_switch')
    parser.add_argument('--rng-seed', type=int)
    parser.add_argument('--event-seed', type=int)
    parser.add_argument('--events', type=Path, help='Replay a saved CSV; overrides event generation')
    parser.add_argument('--output-root', default='results/capacity_scenarios')
    parser.add_argument('--generate-only', action='store_true')
    parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    if args.events and (args.capacity_mode is not None or args.event_seed is not None):
        raise ValueError('--events cannot be combined with --capacity-mode or --event-seed')
    scenario = resolve(args.scenario or
                       'configs/capacity/' + SCENARIOS[args.capacity_mode or 'fixed'])
    config = json.loads(scenario.read_text())
    if config.get('schema_version') != 1:
        raise ValueError('Unsupported scenario schema')
    setting_path = resolve(config['setting_path'])
    setting = json.loads(setting_path.read_text())
    cycles = integer(setting['numCycles'], 'numCycles', 1)
    if setting['baseStations'] != 3:
        raise ValueError('This experiment requires three base stations')
    seed = integer(args.rng_seed if args.rng_seed is not None else setting['rngSeed'], 'rng_seed', 1)
    if seed >= 2**32:
        raise ValueError('rng_seed must fit uint32')
    variation = dict(config['capacity_variation'])
    if args.capacity_mode is not None and args.capacity_mode != variation['mode']:
        raise ValueError('--capacity-mode must match the scenario mode')
    if args.event_seed is not None:
        if variation['mode'] not in ('random', 'random_walk'):
            raise ValueError('--event-seed is only for generated random schedules')
        variation['event_seed'] = args.event_seed
    # The scenario is the source of truth, including the warm-up before cycle 1.
    initial_bps = ({link['ap_id']: link['initial_bps']
                    for link in random_walk_links(variation)}
                   if variation['mode'] == 'random_walk' else dict(NORMAL))
    events = read_events(resolve(args.events), cycles) if args.events else generate_events(variation, cycles)
    run_id = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '_' + uuid.uuid4().hex[:8]
    directory = resolve(args.output_root) / run_id
    directory.mkdir(parents=True, exist_ok=False)
    # Make the effective simulation settings self-contained, including seed override.
    setting['rngSeed'] = seed
    saved_setting = directory / 'setting.json'
    saved_setting.write_text(json.dumps(setting, indent=2) + '\n')
    saved_events = directory / 'planned_events.csv'
    with saved_events.open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS, lineterminator='\n')
        writer.writeheader()
        writer.writerows(events)
    # These are simulator argv tokens, not a shell command. Preserve paths with spaces.
    command = [str(ROOT / 'ns3'), 'run', 'master']
    if args.no_build:
        command.append('--no-build')
    command += ['--', f'--settingPath={saved_setting}', f'--method={args.method}',
                f'--rngSeed={seed}', '--mob=1', f'--pgwCerRate={initial_bps[0]}bps',
                f'--ap1BackhaulRate={initial_bps[1]}bps',
                f'--ap2BackhaulRate={initial_bps[2]}bps',
                '--ap0CapacityVariation=0', '--ap1CapacityVariation=0', '--ap2CapacityVariation=0',
                f'--capacityEventsPath={saved_events}', f'--outputRoot={directory / "simulation"}']
    effective = dict(schema_version=1, run_id=run_id, method=args.method, seed=seed,
                     ns3_version='3.44', mode='replay' if args.events else variation['mode'],
                     capacity_variation=variation, events=events, initial_bps=initial_bps,
                     setting=setting, argv=command, source_scenario=str(scenario),
                     replay_source=str(resolve(args.events)) if args.events else None,
                     planned_events_sha256=digest(saved_events), setting_sha256=digest(saved_setting),
                     python_version=sys.version)
    # Replay provenance is the CSV, not the seed of the accompanying scenario.
    if args.events:
        effective['replay_source_sha256'] = digest(resolve(args.events))
    elif variation['mode'] == 'random_walk':
        effective['capacity_generation'] = dict(
            algorithm='clipped_random_walk_v1', event_seed=variation['event_seed'],
            boundary=variation['boundary'], first_update_cycle=1,
            update_interval_cycles=variation['update_interval_cycles'],
            step_multipliers=[-1, 0, 1], probabilities=[1/3, 1/3, 1/3],
            draw_order='cycle_ascending_then_ap_id_ascending', ap_order=sorted(NORMAL))
    # Source snapshots are fingerprints, not a claim that archived binaries are identical.
    effective['source_sha256'] = {str(p.relative_to(ROOT)): digest(p) for p in
        [Path(__file__).resolve(), ROOT/'master/capacity-schedule.cc', ROOT/'master/config.cc',
         ROOT/'master/NetSim.h', ROOT/'master/topology.cc']}
    (directory/'effective_config.json').write_text(json.dumps(effective, indent=2) + '\n')
    print('Run directory:', directory, flush=True)
    print('Events:', events, flush=True)
    if args.generate_only:
        return 0
    with (directory/'console.log').open('w') as output:
        result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
    (directory/'process_status.json').write_text(json.dumps(dict(returncode=result.returncode))+'\n')
    print('Exit code:', result.returncode, '(see console.log)')
    return 0 if result.returncode == 0 else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, KeyError, OSError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
