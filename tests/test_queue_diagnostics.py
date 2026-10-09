"""Queue aggregation and real NR UM diagnostics regression tests."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('summary', ROOT / 'scripts/summarize_queue_diagnostics.py')
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


class QueueDiagnosticsTest(unittest.TestCase):
    def test_snapshot_aggregation(self):
        def row(t, direction, dropped, hol):
            return dict(run_id='test', sim_time=str(t), cycle_window_id='1', layer='nr_rlc_um',
                        direction=direction, queue_entries='2', queue_bytes='100',
                        hol_delay_ms=hol, dropped_packets_total=str(dropped),
                        dropped_bytes_total=str(100 * dropped))
        result = list(summary.summarize([
            row(1, 'dl', 2, '5'), row(1, 'dl', 3, '8'),
            row(2, 'dl', 2, '10'), row(2, 'ul', 0, '')]))
        self.assertEqual(len(result), 3)
        self.assertEqual(result[0]['queue_bytes'], 200)
        self.assertEqual(result[0]['max_hol_delay_ms'], 8)
        self.assertEqual(result[0]['dropped_packets_total'], 5)
        self.assertEqual(result[1]['dropped_packets_total'], 2)
        self.assertEqual(result[2]['max_hol_delay_ms'], '')

    def test_rlc_buffer_and_overflow(self):
        runtime = ROOT / 'build'
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'rlc-test'
            subprocess.run([
                'g++', '-std=c++20', '-O2', '-I' + str(runtime / 'include'),
                str(ROOT / 'tests/rlc_queue_diagnostics.cc'),
                '-L' + str(runtime / 'lib'), '-Wl,-rpath,' + str(runtime / 'lib'),
                '-lns3.44-nr-optimized', '-lns3.44-network-optimized',
                '-lns3.44-core-optimized', '-o', str(exe)], check=True)
            env = os.environ.copy()
            env['LD_LIBRARY_PATH'] = str(runtime / 'lib')
            subprocess.run([str(exe)], check=True, env=env)


if __name__ == '__main__':
    unittest.main()
