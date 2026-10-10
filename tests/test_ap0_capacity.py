import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('check_ap0', ROOT / 'scripts/check_ap0_capacity.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class Ap0CheckerTest(unittest.TestCase):
    def fixture(self, p, variable):
        (p / 'metadata.txt').write_text(
            f'enabled={int(variable)}\nnormal_bps=80000000\nlow_bps=40000000\n'
            'drop_cycle=2\nrecovery_cycle=4\ncycle_start_offset_sec=4\n'
            'cycle_duration_sec=9.5\nmethod=no_switch\nseed=1001\nlink=pgw_cer\nbs_id_1based=1\n')
        (p / 'setting.json').write_text(json.dumps(dict(terminals=1, numCycles=5)))
        (p / 'initial_terminals.csv').write_text('ue_id,initial_bs_id_1based\n1,2\n')
        events = [(0, 0, 80000000, 80000000)]
        if variable:
            events += [(13.5, 2, 80000000, 40000000), (32.5, 4, 40000000, 80000000)]
        with (p / 'events.csv').open('w') as f:
            w = csv.writer(f)
            w.writerow(['sim_time', 'cycle_id', 'direction', 'old_bps', 'new_bps'])
            for t, c, old, new in events:
                for d in ('pgw_to_cer', 'cer_to_pgw'):
                    w.writerow([t, c, d, old, new])
        log = p / 'master.csv'
        log.write_text('seed,cycle_id,ue_id,switch_flag,current_bs_id,previous_bs_id,tp_mbps,rtt_ms,harmonic_mean,num_unsatisfied_users\n' +
                       ''.join(f'1001,{c},1,0,1,1,1,10,1,0\n' for c in range(1, 6)))
        (p / 'simulation_completed.txt').write_text('sim_time=56.5\n')
        with (p / 'queue_samples.csv').open('w') as f:
            w = csv.writer(f)
            w.writerow(['sim_time', 'direction', 'link_bps'])
            for t in (0, 13.4, 13.5, 23, 32.4, 32.5, 56.5):
                for d in ('pgw_to_cer', 'cer_to_pgw'):
                    w.writerow([t, d, 40000000 if variable and 13.5 <= t < 32.5 else 80000000])
        return log

    def test_constant_and_variable(self):
        for variable in (False, True):
            with tempfile.TemporaryDirectory() as tmp:
                p = Path(tmp)
                log = self.fixture(p, variable)
                with contextlib.redirect_stdout(io.StringIO()):
                    checker.check(p, log)

    def test_missing_direction_detected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            log = self.fixture(p, True)
            lines = (p / 'events.csv').read_text().splitlines()
            (p / 'events.csv').write_text('\n'.join(lines[:-1]) + '\n')
            with self.assertRaises(AssertionError):
                checker.check(p, log)

    def test_wrong_sample_rate_detected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            log = self.fixture(p, True)
            path = p / 'queue_samples.csv'
            path.write_text(path.read_text().replace('13.5,pgw_to_cer,40000000', '13.5,pgw_to_cer,80000000'))
            with self.assertRaises(AssertionError):
                checker.check(p, log)

    def test_logistic_allows_switching(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            log = self.fixture(p, True)
            meta = p / 'metadata.txt'
            meta.write_text(meta.read_text().replace('method=no_switch', 'method=logistic'))
            content = log.read_text().splitlines()
            content[0] += ',method'
            content[1:] = [line.replace(',1,0,1,1,', ',1,1,0,1,') + ',logistic' for line in content[1:]]
            log.write_text('\n'.join(content) + '\n')
            with contextlib.redirect_stdout(io.StringIO()):
                checker.check(p, log)

    def test_scenario(self):
        s = json.loads((ROOT / 'data/scenarios/ap0_capacity_80_seed1001.json').read_text())
        self.assertEqual((s['terminals'], s['rngSeed'], s['numCycles']), (80, 1001, 5))


if __name__ == '__main__':
    unittest.main()
