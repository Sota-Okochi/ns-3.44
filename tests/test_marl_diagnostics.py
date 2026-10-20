import copy
import importlib.util
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import torch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from rl.marl.core import Config, Learner, observation
from rl.marl.diagnostics import update_statistics

spec = importlib.util.spec_from_file_location('diagnostic_runner', ROOT/'scripts/run_marl.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
torch.set_num_threads(1)



class DiagnosticTest(unittest.TestCase):
    def test_statistics(self):
        g = torch.tensor([1., 2., 3.])
        old = torch.zeros(3)
        lp = torch.tensor([.5, .5]).log().expand(3, 4, 2)
        new_lp = torch.tensor([.75, .25]).log().expand(3, 4, 2)
        d = update_statistics(g, old, g, lp, new_lp)
        self.assertAlmostEqual(d['explained_variance_before'], 0)
        self.assertAlmostEqual(d['explained_variance_after'], 1)
        self.assertAlmostEqual(d['advantage_raw_mean'], 2)
        self.assertAlmostEqual(d['advantage_raw_std'], (2/3)**.5)
        self.assertAlmostEqual(d['joint_kl'], 4 * .5 * math.log(4/3), places=6)
        d = update_statistics(torch.ones(3), old, g, lp, lp)
        self.assertIsNone(d['explained_variance_before'])
        self.assertIsNone(d['explained_variance_after'])

    def test_passive_update_and_session(self):
        a = Learner(Config(rollout_episodes=2), seed=1)
        b = Learner(Config(rollout_episodes=2), seed=1)
        episodes = []
        with tempfile.TemporaryDirectory() as tmp:
            for e in range(2):
                path = Path(tmp)/str(e)
                path.mkdir()
                s = runner.Session(a, 4, 7, e, 'train', path)
                reference_path = Path(tmp)/f'reference_{e}'
                reference_path.mkdir()
                reference = runner.Session(b, 4, 7, e, 'train', reference_path)
                m = dict(schema='marl_v1', seed=7, sim_time=1.,
                         users=[dict(ue_id=i, app=i % 4, bs=0, tp_mbps=0. if i == 0 else 4.,
                                     tp_status='ok') for i in range(4)],
                         aps=[dict(ap_id=i, status='ok', rtt_ms=50.) for i in range(3)])
                for cycle in range(1, 6):
                    m.update(cycle=cycle, done=cycle == 5)
                    reply = s.handle(copy.deepcopy(m)).split()
                    self.assertEqual(reply, reference.handle(copy.deepcopy(m)).split())
                    for u, ap in zip(m['users'], reply[4:]):
                        u['bs'] = int(ap)
                for filename in ('qoe.csv', 'observations.jsonl', 'transitions.jsonl'):
                    self.assertEqual((path/filename).read_bytes(),
                                     (reference_path/filename).read_bytes())
                episodes.append(s.transitions)
            rng = torch.get_rng_state().clone()
            python_rng = random.getstate()
            metrics = a.update(episodes)
            self.assertTrue(torch.equal(rng, torch.get_rng_state()))
            self.assertEqual(python_rng, random.getstate())
            with patch('rl.marl.core.update_statistics', return_value={}):
                b.update(episodes)
            for left, right in [(a.actor, b.actor), (a.critic, b.critic)]:
                for k, v in left.state_dict().items():
                    self.assertTrue(torch.equal(v, right.state_dict()[k]))
            # Equality also persists through the next update (optimizer state).
            a.update(episodes)
            b.update(episodes)
            for left, right in [(a.actor, b.actor), (a.critic, b.critic)]:
                for k, v in left.state_dict().items():
                    self.assertTrue(torch.equal(v, right.state_dict()[k]))
            self.assertIn('joint_kl', metrics)
            self.assertAlmostEqual(metrics['joint_kl'], metrics['kl']*4, places=6)
            runner.append_csv(Path(tmp)/'updates.csv', metrics)
            self.assertIn('explained_variance_after', (Path(tmp)/'updates.csv').read_text())

    def test_plot_new_and_legacy_csv(self):
        import json
        import os
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path/'manifest.json').write_text(json.dumps(dict(args=dict(terminals=4, model_seed=1))))
            runner.append_csv(path/'episodes.csv',
                              dict(phase='train', episode_id=0, mean_h_after=.5))
            runner.append_csv(path/'updates.csv',
                              dict(episodes=8, explained_variance_before=None,
                                   explained_variance_after=.3, joint_kl=.01))
            env = dict(os.environ, MPLCONFIGDIR=str(path/'mpl'))
            subprocess.run([sys.executable, str(ROOT/'scripts/plot_marl.py'), str(path),
                            '--output', str(path/'plots')], check=True, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            for name in ('training', 'validation', 'update_diagnostics'):
                for ext in ('png', 'pdf'):
                    self.assertTrue((path/'plots'/f'{name}.{ext}').is_file())
            self.assertFalse((path/'plots'/'failure_diagnostics.png').exists())
            (path/'updates.csv').write_text('episodes,kl\n8,0.001\n')
            (path/'episodes.csv').write_text('phase,episode_id,mean_h_after\ntrain,0,0.5\n')
            subprocess.run([sys.executable, str(ROOT/'scripts/plot_marl.py'), str(path),
                            '--output', str(path/'legacy_plots')], check=True, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertFalse((path/'legacy_plots'/'update_diagnostics.png').exists())
            self.assertFalse((path/'legacy_plots'/'failure_diagnostics.png').exists())


if __name__ == '__main__':
    unittest.main()
