import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('capacity', ROOT/'scripts/run_capacity_scenario.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class CapacityTest(unittest.TestCase):
    def event(self, **kw):
        return dict(dict(cycle_id=2, ap_id=0, rate_bps=40000000, direction='both'), **kw)

    def test_fixed_sort_and_multiple_links(self):
        events = [self.event(cycle_id=4), self.event(ap_id=2), self.event()]
        self.assertEqual([(e['cycle_id'], e['ap_id']) for e in m.validate_events(events, 5)],
                         [(2, 0), (2, 2), (4, 0)])

    def test_invalid(self):
        for change in [dict(cycle_id=0), dict(cycle_id=6), dict(ap_id=3), dict(rate_bps=0),
                       dict(rate_bps=-1), dict(rate_bps=2**64), dict(direction='one'), dict(cycle_id=True)]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                m.validate_events([self.event(**change)], 5)
        with self.assertRaises(ValueError):
            m.validate_events([self.event(), self.event()], 5)

    def random_config(self):
        return dict(mode='random', event_seed=2001, ap_id=0, low_rates_bps=[20000000,40000000],
                    drop_cycle_range=[2,3], duration_cycles_range=[1,2])

    def test_random_repeatable_independent(self):
        c = self.random_config()
        expected = m.generate_events(c, 5)
        m.random.seed(9)
        for _ in range(100):
            m.random.random()
        self.assertEqual(expected, m.generate_events(c, 5))
        for seed in range(50):
            events = m.generate_events(dict(c, event_seed=seed), 5)
            self.assertTrue(2 <= events[0]['cycle_id'] <= 3)
            self.assertTrue(1 <= events[1]['cycle_id']-events[0]['cycle_id'] <= 2)
            self.assertEqual(events[1]['rate_bps'], 80000000)
        with self.assertRaises(ValueError):
            m.generate_events(c, 4)

    def test_constant_and_replay(self):
        self.assertEqual(m.generate_events(dict(mode='constant'), 5), [])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'events.csv'
            path.write_text('cycle_id,ap_id,rate_bps,direction\n2,0,40000000,both\n')
            self.assertEqual(m.read_events(path, 5), [self.event()])

    def test_default_config(self):
        c = m.json.loads((ROOT/'configs/capacity/fixed_ap0.json').read_text())
        self.assertEqual(m.generate_events(c['capacity_variation'], 5),
                         [self.event(), self.event(cycle_id=4, rate_bps=80000000)])

    def test_all_ap_scenario(self):
        c = m.json.loads((ROOT/'configs/capacity/fixed_all.json').read_text())
        events = m.generate_events(c['capacity_variation'], 5)
        self.assertEqual([(e['cycle_id'], e['ap_id'], e['rate_bps']) for e in events],
            [(2,0,40000000),(2,1,60000000),(2,2,10000000),
             (4,0,80000000),(4,1,40000000),(4,2,40000000)])


if __name__ == '__main__':
    unittest.main()
