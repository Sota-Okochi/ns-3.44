"""Diagnostic-only Wi-Fi instrumentation: before/after and on/off equivalence."""
import csv
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'build/wifi-append-parts-v1'
AFTER = ROOT / 'build/wifi-band-cache-v1'


class WifiBandCacheTest(unittest.TestCase):
    @unittest.skipUnless((AFTER/'build_manifest.json').exists(), 'private build required')
    def test_cache_invalidation_and_ordered_history(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            modules = ('wifi', 'spectrum', 'network', 'core')
            def run(source, label, runtime, extra=()):
                exe = tmp/label
                env = dict(os.environ, LD_LIBRARY_PATH=str(runtime/'lib'))
                subprocess.run(['g++', '-std=c++20', '-O2', *extra,
                                '-I'+str(ROOT/'build/include'), str(ROOT/'tests'/source),
                                '-L'+str(runtime/'lib'), '-Wl,-rpath,'+str(runtime/'lib'),
                                *[f'-lns3.44-{m}-optimized' for m in modules], '-o', str(exe)], check=True)
                return subprocess.check_output([str(exe)], env=env, text=True)
            self.assertIn('PASS', run('wifi_band_cache_test.cc','lifecycle',AFTER))
            # Compile the old class against its own header, avoiding mixed class sizes.
            include = tmp/'include/ns3'; include.mkdir(parents=True)
            saved = ROOT/'results/perf/wifi_band_cache_50_20261015/interference-helper.before.h'
            (include/'interference-helper.h').write_text(saved.read_text().replace('\"phy-entity.h\"', '\"ns3/phy-entity.h\"'))
            old = run('interference_band_state_regression.cc','history_before',BASE,
                      ('-DNS3_UNIFIED_BAND_STATE','-I'+str(include.parent)))
            new = run('interference_band_state_regression.cc','history_after',AFTER,
                      ('-DNS3_UNIFIED_BAND_STATE',))
            self.assertEqual(old,new)

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
                                         'WifiRx.sampled.append.band_lookup',
                                         'WifiRx.sampled.append.layout_lookup',
                                         'WifiRx.sampled.append.time_bounds',
                                         'WifiRx.sampled.append.erase',
                                         'WifiRx.sampled.append.insert',
                                         'WifiRx.sampled.append.add_power',
                                         'WifiRx.sampled.append.empty_reference'} <= names)
            for seed in (1001, 1002):
                self.assertEqual(outputs['before', seed], outputs['after', seed])


if __name__ == '__main__':
    unittest.main()
