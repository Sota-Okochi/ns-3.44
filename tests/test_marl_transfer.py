"""Transfer and runner orchestration tests; no ns-3 processes launched."""
import contextlib
import csv
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from test_marl_ppo import Config, Learner, message, observation, runner, torch, ROOT


class TransferTest(unittest.TestCase):
    def test_weights_only_and_real_100_user_update(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = Learner(Config(rollout_episodes=8))
            # Populate optimizer histories so reset is not a vacuous assertion.
            for net, opt in ((source.actor, source.actor_opt), (source.critic, source.critic_opt)):
                sum(p.sum() for p in net.parameters()).backward()
                opt.step()
            source.updates = 104
            path = root/'source.pt'
            source.save(path, terminals=80, completed_episodes=832, best_score=999.)
            target = Learner(Config(rollout_episodes=10), seed=2)
            rng = torch.get_rng_state().clone()
            meta = target.initialize_weights(path)
            self.assertEqual(meta['source_completed_episodes'], 832)
            self.assertEqual(target.updates, 0)
            self.assertEqual(target.seed, 2)
            self.assertFalse(target.actor_opt.state)
            self.assertFalse(target.critic_opt.state)
            torch.testing.assert_close(rng, torch.get_rng_state())
            for name in ('actor', 'critic'):
                for k, v in getattr(source, name).state_dict().items():
                    torch.testing.assert_close(v, getattr(target, name).state_dict()[k])
            episodes = []
            for e in range(10):
                directory = root/str(e); directory.mkdir()
                session = runner.Session(target, 100, 7, e, 'train', directory)
                msg = message(n=100)
                for cycle in range(1, 6):
                    msg.update(cycle=cycle, done=cycle == 5)
                    reply = session.handle(msg).split()
                    for user, ap in zip(msg['users'], reply[4:]):
                        user['bs'] = int(ap)
                episodes.append(session.transitions)
            with self.assertRaises(ValueError):
                target.update(episodes[:8])
            metrics = target.update(episodes)
            self.assertEqual(metrics['update'], 1)
            self.assertTrue(all(torch.isfinite(torch.tensor(v)) for v in metrics.values()))
            with self.assertRaises(ValueError):
                target.initialize_weights(path)
            for config in (Config(epsilon=.02), Config(width=32)):
                with self.assertRaisesRegex(ValueError, 'config mismatch'):
                    Learner(config).initialize_weights(path)

    def test_runner_schedule_provenance_and_resume(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            binary = root/'master'; binary.write_text('not executable: mocked collection')
            source = root/'source.pt'
            Learner(Config(rollout_episodes=8)).save(
                source, terminals=80, completed_episodes=832, best_score=999.)
            common = ['--mode', 'train', '--terminals', '100', '--workers', '10',
                      '--parallel-validated', '--binary', str(binary),
                      '--config', str(ROOT/'configs/marl/ppo_rollout10.json'),
                      '--setting', str(ROOT/'configs/marl/transfer100_setting.json'),
                      '--validation-interval', '50']
            calls = []

            def collect(tasks, workers, execute, cache, identity, record, reuse):
                calls.append((identity['phase'], len(tasks), workers))
                # No simulation: exercise scheduling, persistence and selection only.
                results = [(None, {'mean_h_after': .2}) for _ in tasks]
                for result in results:
                    record(result, False)
                return results

            def update(learner, episodes):
                self.assertEqual(len(episodes), 10)
                learner.updates += 1
                return {'update': learner.updates}

            with patch.object(runner, 'collect_durable', side_effect=collect), \
                 patch.object(Learner, 'update', update), \
                 contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                run = root/'run'
                runner.main(common + ['--episodes', '1000', '--init-checkpoint', str(source), '--output', str(run)])
                self.assertEqual(sum(n for phase,n,_ in calls if phase == 'train'), 1000)
                self.assertEqual(sum(n for phase,n,_ in calls if phase == 'validation'), 105)
                self.assertTrue(all(w == 10 for _,_,w in calls))
                with (run/'validation.csv').open() as f:
                    self.assertEqual([int(r['episodes']) for r in csv.DictReader(f)], list(range(0,1001,50)))
                latest = torch.load(run/'latest.pt', weights_only=False)
                best = torch.load(run/'best.pt', weights_only=False)
                self.assertEqual(latest['updates'], 100)
                self.assertEqual(latest['completed_episodes'], 1000)
                self.assertEqual(best['completed_episodes'], 0)  # old score not inherited; ties keep first
                self.assertEqual(best['best_score'], .2)
                self.assertEqual(latest['transfer']['source_terminals'], 80)
                manifest = json.loads((run/'manifest.json').read_text())
                self.assertEqual(manifest['transfer'], latest['transfer'])
                resume = root/'resume'
                runner.main(common + ['--episodes', '1050', '--resume', '--checkpoint', str(run/'latest.pt'), '--output', str(resume)])
                resumed = torch.load(resume/'latest.pt', weights_only=False)
                self.assertEqual(resumed['updates'], 105)
                self.assertEqual(resumed['transfer'], latest['transfer'])
                runner.main(common + ['--episodes', '1000', '--recover-run', str(run), '--output', str(root/'recovered')])
                for i, extra in enumerate((['--validation-interval', '64'],
                                           ['--validation-interval', '100'], ['--terminals', '80'])):
                    with self.assertRaises(SystemExit):
                        runner.main(common + ['--episodes', '1050', '--resume', '--checkpoint', str(run/'latest.pt'), '--output', str(root/f'bad{i}')] + extra)
                with self.assertRaises(SystemExit):
                    runner.main(common + ['--episodes', '1000', '--resume', '--init-checkpoint', str(source), '--output', str(root/'invalid')])


if __name__ == '__main__':
    unittest.main()
