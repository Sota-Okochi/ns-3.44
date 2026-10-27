"""Timing boundaries: deterministic fake clocks, plus standalone C++ pause/resume."""
import copy
import csv
import importlib
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from test_marl_ppo import runner, message
from rl.marl.core import Learner
import torch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'rl'))
sys.path.insert(0, str(ROOT/'scripts'))
from summarize_assignment_timing import summarize, METHODS
central = importlib.import_module('centralized_server')


class TimingTests(unittest.TestCase):
    def test_cpp_pause_resume(self):
        source = r'''
#include "assignment-compute-timer.h"
#include <cassert>
#include <thread>
int main() {
    double ms = 0;
    ns3::AssignmentComputeTimer timer(ms);
    timer.Pause(); const double before = ms;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    timer.Pause(); assert(ms == before);
    timer.Resume(); timer.Resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    timer.Pause(); assert(ms >= before + 4);
    const double after = ms;
    { ns3::AssignmentComputeTimer disabled(ms, false); }
    assert(ms == after);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            cpp = Path(temp)/'test.cc'; cpp.write_text(source)
            binary = Path(temp)/'test'
            subprocess.run(['g++', '-std=c++17', '-pthread', '-I'+str(ROOT/'contrib/kameda/model/server'),
                            str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_marl_excludes_logging_and_critic_preserves_actions(self):
        learner = Learner()
        now = [0]
        real_obs, real_act, real_critic = runner.observation, learner.assignment_action, learner.critic
        def obs(*args):
            now[0] += 2_000_000
            return real_obs(*args)
        def act(*args):
            now[0] += 3_000_000
            return real_act(*args)
        def critic(*args):
            now[0] += 70_000_000
            return real_critic(*args)
        real_csv = runner.append_csv
        def log(*args):
            now[0] += 90_000_000
            return real_csv(*args)
        with tempfile.TemporaryDirectory() as temp:
            a, b = Path(temp)/'a', Path(temp)/'b'; a.mkdir(); b.mkdir()
            session = runner.Session(learner, 4, 7, 0, 'test', a)
            reference = runner.Session(learner, 4, 7, 0, 'test', b)
            msg = message(n=4)
            expected = reference.handle(copy.deepcopy(msg)).split()[4:8]
            msg['assignment_timing_version'] = 2
            with patch.object(runner.time, 'perf_counter_ns', side_effect=lambda: now[0]), \
                 patch.object(runner, 'observation', side_effect=obs), \
                 patch.object(learner, 'assignment_action', side_effect=act), \
                 patch.object(learner, 'critic', side_effect=critic), \
                 patch.object(runner, 'append_csv', side_effect=log):
                reply = session.handle(msg).split()
            self.assertEqual(reply[4:8], expected)
            self.assertEqual(reply[-2:], ['TIMING_V2', '5'])
            self.assertEqual(session.last_compute_ms, 5.)
            # All four actions plus a final non-decision observation.
            for cycle in range(2, 6):
                for u, ap in zip(msg['users'], reply[4:8]): u['bs'] = int(ap)
                msg.update(cycle=cycle, done=cycle == 5)
                reply = session.handle(copy.deepcopy(msg)).split()
            self.assertEqual(reply[-2:], ['TIMING_V2', '0'])

    def test_dqn_feature_and_inference_sum(self):
        service = central.CentralizedDqnService.__new__(central.CentralizedDqnService)
        service.schema_version = 'v1'
        service.normalization_enabled = False
        service.args = SimpleNamespace(eval_only=True)
        service.last_cycle = None; service.steps = 0
        service.agent = SimpleNamespace(device=torch.device('cpu'),
            select_action_masked=lambda state, valid, epsilon: (2, [0., 0., 1.]))
        with patch.object(central, 'state_to_vector', return_value=[1.]), \
             patch.object(central.time, 'perf_counter_ns', side_effect=[0, 2_000_000, 100_000_000, 103_000_000]):
            reply = service.handle(dict(state={}, cycle_id=1, valid_action_ids=[2]))
        self.assertEqual(reply['assignment_compute_ms'], 5.)
        self.assertEqual(reply['assignment_timing_version'], 2)
        self.assertEqual((reply['target_ue_id'], reply['selected_bs_id']), (1, 2))

    def fixture(self, directory):
        paths = []
        for method in METHODS:
            path = directory/f'assignment_timing_{method}.csv'
            rows = [dict(timing_version=2, seed=1001, cycle_id=i, method=method,
                effective_method=method, num_terminals=80, status='ok' if i < 5 else 'no_decision',
                cpp_compute_ms=1, python_compute_ms=2, assignment_compute_ms=3 if i < 5 else '')
                for i in range(1, 6)]
            with path.open('w', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=rows[0]); writer.writeheader(); writer.writerows(rows)
            paths.append(path)
        return paths

    def test_summary_rejects_incomplete_duplicate_bootstrap_old_and_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = self.fixture(Path(temp))
            rows = summarize(paths)
            self.assertEqual(len(rows), 4)
            self.assertTrue(all(r['decision_cycles'] == 4 and r['mean_ms'] == 3 for r in rows))
            with self.assertRaises(ValueError): summarize(paths[:-1])
            with self.assertRaises(ValueError): summarize(paths + paths[:1])
            original = paths[0].read_text()
            for old, new in [('2,1001', '1,1001'), (',random,random,', ',random,logistic_bootstrap,'),
                             (',ok,', ',request_failed,'), (',1,2,3', ',1,2,nan')]:
                paths[0].write_text(original.replace(old, new))
                with self.assertRaises(ValueError): summarize(paths)
            paths[0].write_text(original)

    def test_single_method_summary(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = self.fixture(Path(temp))
            for method, path in zip(METHODS, paths):
                rows = summarize([path], methods=(method,))
                self.assertEqual([r['method'] for r in rows], [method])
            with self.assertRaises(ValueError): summarize(paths, methods=('random',))

    def test_single_baseline_needs_no_rl_checkpoints(self):
        import json
        import run_assignment_timing as suite
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixture_dir = root/'fixtures'; fixture_dir.mkdir()
            fixture_paths = dict(zip(METHODS, self.fixture(fixture_dir)))
            binary = root/'master'; binary.write_text('not executed')
            for method in ('random', 'logistic'):
                output = root/method
                commands = []
                def fake_run(command, directory, timeout):
                    commands.append(command)
                    (directory/'assignment_timing_test.csv').write_text(fixture_paths[method].read_text())
                argv = ['run_assignment_timing.py', '--method', method, '--port', 'auto',
                        '--binary', str(binary), '--seed', '1001', '--output', str(output)]
                with patch.object(sys, 'argv', argv), patch.object(suite, 'run', side_effect=fake_run), \
                     patch.object(suite.torch, 'load', side_effect=AssertionError('Unexpected model load')):
                    suite.main()
                self.assertEqual(len(commands), 1)
                self.assertIn('--method='+method, commands[0])
                self.assertEqual(json.loads((output/'status.json').read_text())['completed_methods'], [method])
                self.assertEqual(json.loads((output/'manifest.json').read_text())['method_order'], [method])


if __name__ == '__main__': unittest.main()
