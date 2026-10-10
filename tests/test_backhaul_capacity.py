import importlib.util
from collections import Counter
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class CapacityTest(unittest.TestCase):
    def test_fixed_nested_composition(self):
        make = module('backhaul_capacity_experiment').app_profile
        for seed in [1001, 1002]:
            self.assertEqual(Counter(make(seed, 80)), {1: 16, 2: 32, 3: 12, 4: 20})
            self.assertEqual(Counter(make(seed, 100)), {1: 20, 2: 40, 3: 15, 4: 25})
            self.assertEqual(make(seed, 100)[:80], make(seed, 80))
            self.assertEqual(make(seed, 100), make(seed, 100))
        with self.assertRaises(ValueError):
            make(1001, 81)

    def test_incoming_before_drop_rate(self):
        analyze = module('analyze_link_load').intervals
        def row(t, incoming, sent, dropped):
            return dict(run_id='test', seed='1', direction='dl', sim_time=str(t),
                        received_bytes_total=str(incoming), sent_bytes_total=str(sent),
                        dropped_packets_total=str(dropped), link_bps='80000000', queue_bytes='10')
        rows = list(analyze([row(0, 0, 0, 0), row(.1, 2000000, 1000000, 3),
                             row(.2, 4000000, 2000000, 7)]))
        self.assertAlmostEqual(rows[0]['incoming_mbps'], 160)
        self.assertAlmostEqual(rows[0]['incoming_to_capacity_ratio'], 2)
        self.assertEqual(rows[1]['dropped_packets'], 4)
        with self.assertRaises(ValueError):
            list(analyze([row(1, 10, 10, 1), row(2, 0, 0, 0)]))


if __name__ == '__main__':
    unittest.main()
