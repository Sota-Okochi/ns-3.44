import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from analyze_ap1_capacity import summarize


class SummaryTest(unittest.TestCase):
    def test_empty_group_is_not_zero_performance(self):
        self.assertEqual(summarize([]), {'n': 0})

    def test_threshold_and_harmonic_mean(self):
        rows = [dict(satisfaction=str(s), tp_mbps='2', rtt_ms='100', measurement_valid='1')
                for s in [.25, .5, 1.0]]
        result = summarize(rows)
        self.assertAlmostEqual(result['harmonic_mean'], 3/7)
        self.assertEqual(result['unsatisfied'], 1)
        self.assertEqual(result['below_requirement'], 2)
        self.assertEqual(result['invalid'], 0)

    def test_nonpositive_rejected(self):
        with self.assertRaises(ValueError):
            summarize([dict(satisfaction='0')])


if __name__ == '__main__':
    unittest.main()
