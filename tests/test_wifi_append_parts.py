"""Diagnostic-only Wi-Fi instrumentation: before/after and on/off equivalence."""
import csv
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'build/wifi-append-hoist-v1'
AFTER = ROOT / 'build/wifi-append-parts-v1'


class WifiAppendPartsTest(unittest.TestCase):
    @unittest.skipUnless((AFTER/'build_manifest.json').exists(), 'private diagnostic build required')
    def test_sampled_call_tree_preserves_results(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            modules = ('wifi', 'spectrum', 'propagation', 'internet', 'mobility', 'network', 'core')
            outputs = {}
            for label, runtime in (('before', BASE), ('after', AFTER)):
                exe = tmp / label
                subprocess.run(['g++', '-std=c++20', '-O2', '-I'+str(ROOT/'build/include'),
                                str(ROOT/'tests/research_wifi_perf_smoke.cc'),
                                '-L'+str(runtime/'lib'), '-Wl,-rpath,'+str(runtime/'lib'),
                                *[f'-lns3.44-{m}-optimized' for m in modules], '-o', str(exe)], check=True)
                env = dict(os.environ, LD_LIBRARY_PATH=str(runtime/'lib'))
                # Default seed1001: profiling on/off must preserve packets and events.
                off = subprocess.check_output([str(exe)], env=env, text=True)
                for seed in (1001, 1002):
                    folder = tmp / f'{label}_{seed}'
                    output = subprocess.check_output([str(exe), str(folder), str(seed)], env=env, text=True)
                    self.assertIn('bytes=200', output)
                    if seed == 1001:
                        self.assertEqual(off, output)
                    comparable = []
                    for filename in ('devices.csv', 'boundaries.csv'):
                        with (folder/filename).open() as stream:
                            rows = list(csv.DictReader(stream))
                        for row in rows:
                            row.pop('interval_wall_ms', None)
                        comparable.append(rows)
                    outputs[label, seed] = output, comparable
                    if label == 'after':
                        with (folder/'functions.csv').open() as stream:
                            names = {r['function'] for r in csv.DictReader(stream)}
                        self.assertTrue({'WifiRx.sampled.do_get_event',
                                         'WifiRx.sampled.AppendEvent',
                                         'WifiRx.sampled.interference_event_create',
                                         'WifiRx.sampled.phy_state_dispatch',
                                         'WifiRx.sampled.layout_and_storage',
                                         'WifiRx.sampled.append.band_find',
                                         'WifiRx.sampled.append.time_bounds',
                                         'WifiRx.sampled.append.erase',
                                         'WifiRx.sampled.append.insert',
                                         'WifiRx.sampled.append.add_power',
                                         'WifiRx.sampled.append.empty_reference'} <= names)
            for seed in (1001, 1002):
                self.assertEqual(outputs['before', seed], outputs['after', seed])


if __name__ == '__main__':
    unittest.main()
