"""Regression checks for exact log comparisons, including failure cases."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("verify", ROOT / "scripts/verify_wifi_insert.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


class CompareTest(unittest.TestCase):
    def check_pair(self, a, b, ignore=()):
        with tempfile.TemporaryDirectory() as directory:
            left, right = [Path(directory) / n for n in ("a.csv", "b.csv")]
            left.write_text(a)
            right.write_text(b)
            return verify.equal_csv(left, right, ignore)

    def test_only_explicit_wall_time_is_ignored(self):
        self.assertEqual(self.check_pair("qoe,assignment_compute_ms\n0.5,1\n",
                                        "qoe,assignment_compute_ms\n0.5,2\n",
                                        {"assignment_compute_ms"}), 1)

    def test_qoe_mismatch_fails(self):
        with self.assertRaises(ValueError):
            self.check_pair("qoe\n0.500000\n", "qoe\n0.500001\n")

    def test_event_mismatch_fails(self):
        with self.assertRaises(ValueError):
            self.check_pair("event_count\n378\n", "event_count\n379\n")

    def test_missing_rows_fail(self):
        with self.assertRaises(ValueError):
            self.check_pair("qoe\n1\n2\n", "qoe\n1\n")

    def test_empty_or_truncated_fail(self):
        for a in ("qoe\n", "qoe,tp\n1\n"):
            with self.assertRaises(ValueError):
                self.check_pair(a, a)


if __name__ == "__main__":
    unittest.main()
