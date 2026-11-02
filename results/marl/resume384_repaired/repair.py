"""Repair only inconsistent base-setting metadata; preserve original artifacts."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import sys
import torch
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from rl.marl.collection import digest
from rl.marl.core import Config, Learner
from dataclasses import asdict
OUT = Path(__file__).resolve().parent
SOURCE = ROOT / 'results/marl/train80_new_episodes384_261025'
def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()
def equal(a, b):
    assert type(a) is type(b), (type(a), type(b))
    if isinstance(a, torch.Tensor):
        assert a.dtype == b.dtype and a.shape == b.shape and torch.equal(a, b)
    elif isinstance(a, dict):
        assert a.keys() == b.keys()
        for k in a: equal(a[k], b[k])
    elif isinstance(a, (list, tuple)):
        assert len(a) == len(b)
        for x, y in zip(a, b): equal(x, y)
    else:
        assert a == b, (a, b)
original_hash = sha(SOURCE/'latest.pt')
ck = torch.load(SOURCE/'latest.pt', map_location='cpu', weights_only=False)
manifest = json.loads((SOURCE/'manifest.json').read_text())
assert ck['completed_episodes'] == 384 and ck['updates'] == 48 and ck['terminals'] == 80
fixed = copy.deepcopy(ck)
assert fixed['setting']['terminals'] == 80
fixed['setting']['terminals'] = 100
config = asdict(Config(**json.loads((ROOT/'configs/marl/ppo_rollout8.json').read_text())))
sources = list((ROOT/'rl/marl').glob('*.py')) + [ROOT/p for p in (
    'scripts/run_marl.py', 'contrib/kameda/model/server/marl-ppo.cc',
    'contrib/kameda/model/client/APMonitorTerminal.cc', 'master/applications.cc')]
binary = ROOT/'build/qoe-floor001-v1/master'
experiment = dict(validation_seeds=list(range(1011,1016)), validation_interval=64,
    config=config, model_seed=1, terminals=80,
    capacity=json.loads((ROOT/'configs/capacity/random_walk_all.json').read_text()),
    setting=fixed['setting'], probe=[10,.2,1.], report_conn_timeout=.2,
    report_transport='internal_callback_v1', binary=sha(binary),
    libraries={p.name:sha(p) for p in sorted((binary.parent/'lib').glob('*.so*')) if p.is_file()},
    sources={str(p.relative_to(ROOT)):sha(p) for p in sources},
    torch=torch.__version__, python=sys.version, seed_schema='marl_seed_v1')
assert digest(experiment) == ck['experiment_id'] == manifest['experiment_id']
assert config == ck['config'] and experiment['capacity'] == ck['capacity']
assert ck['probe'] == experiment['probe'] and ck['report_conn_timeout'] == .2
# Confirm actual newly collected episode settings, not just base metadata.
settings = list(SOURCE.glob('train_*/setting.json'))
assert len(settings) == 128
assert all(json.loads(p.read_text())['terminals'] == 80 for p in settings)
for key in ck:
    if key != 'setting': equal(ck[key], fixed[key])
for key in ck['setting']:
    if key != 'terminals': equal(ck['setting'][key], fixed['setting'][key])
assert not (OUT/'latest.pt').exists(), 'Do not overwrite a prior repair'
torch.save(fixed, OUT/'latest.pt')
shutil.copy2(SOURCE/'best.pt', OUT/'best.pt')
(OUT/'setting.json').write_text(json.dumps(fixed['setting'], indent=2)+'\n')
loaded = torch.load(OUT/'latest.pt', map_location='cpu', weights_only=False)
equal(fixed, loaded)
learner = Learner(Config(**config), 1)
learner.load(OUT/'latest.pt')
assert learner.updates == 48
assert sha(SOURCE/'latest.pt') == original_hash
assert sha(OUT/'best.pt') == sha(SOURCE/'best.pt')
audit = dict(source=str(SOURCE/'latest.pt'), source_sha256=original_hash,
    repaired_sha256=sha(OUT/'latest.pt'), change={'setting.terminals': {'before':80,'after':100}},
    actual_simulation_terminals=80, completed_episodes=384, updates=48,
    experiment_id=ck['experiment_id'], experiment_identity_verified=True,
    all_other_checkpoint_fields_exactly_equal=True, original_unchanged=True,
    actual_training_settings_checked=len(settings), best_preserved=True,
    command='python3 results/marl/resume384_repaired/repair.py')
(OUT/'repair_audit.json').write_text(json.dumps(audit,indent=2)+'\n')
print(json.dumps(audit,indent=2))
