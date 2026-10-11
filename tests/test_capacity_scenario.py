import importlib.util
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

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


class RandomWalkTest(unittest.TestCase):
    def setUp(self):
        self.config = json.loads((ROOT/'configs/capacity/random_walk_all.json').read_text())['capacity_variation']

    def test_grid_bounds_steps_and_repeatability(self):
        for seed in range(20):
            c = dict(self.config, event_seed=seed)
            events = m.generate_events(c, 100)
            self.assertEqual(len(events), 300)
            self.assertEqual(events, m.generate_events(c, 100))
            self.assertEqual(events, m.generate_events(dict(c, links=c['links'][::-1]), 100))
            previous = {link['ap_id']: link['initial_bps'] for link in c['links']}
            for index, event in enumerate(events):
                ap, rate = event['ap_id'], event['rate_bps']
                link = c['links'][ap]
                self.assertEqual((event['cycle_id'], ap), (index//3+1, index % 3))
                self.assertEqual(event['direction'], 'both')
                self.assertTrue(link['min_bps'] <= rate <= link['max_bps'])
                self.assertEqual((rate-link['min_bps']) % link['step_bps'], 0)
                self.assertIn(rate-previous[ap], [-link['step_bps'], 0, link['step_bps']])
                previous[ap] = rate

    def test_first_cycle_and_rng_isolation(self):
        with patch.object(m.random, 'Random') as factory:
            factory.return_value.choice.side_effect = [-1, 0, 1]
            events = m.generate_events(self.config, 1)
            factory.assert_called_once_with(2001)
            self.assertEqual(factory.return_value.choice.call_count, 3)
            self.assertEqual([e['rate_bps'] for e in events], [max(link['min_bps'], min(link['max_bps'],
                link['initial_bps'] + draw * link['step_bps']))
                for link, draw in zip(self.config['links'], [-1, 0, 1])])
        expected = m.generate_events(self.config, 5)
        m.random.seed(123)
        for _ in range(100):
            m.random.random()
        self.assertEqual(expected, m.generate_events(self.config, 5))

    def test_clipping_and_noop_rows(self):
        for draw, field in [(-1, 'min_bps'), (1, 'max_bps'), (0, 'initial_bps')]:
            with patch.object(m.random, 'Random') as factory:
                factory.return_value.choice.return_value = draw
                events = m.generate_events(self.config, 20)
                self.assertEqual(factory.return_value.choice.call_count, 60)
                self.assertEqual(len(events), 60)
                self.assertEqual([e['rate_bps'] for e in events[-3:]],
                                 [link[field] for link in self.config['links']])

    def test_invalid_configs_before_rng(self):
        changes = [dict(boundary='reflect'), dict(update_interval_cycles=2),
                   dict(update_interval_cycles=True), dict(event_seed=-1), dict(event_seed=True),
                   dict(links=[]), dict(links=self.config['links'][:2]),
                   dict(links=[None]*3)]
        for key, value in [('ap_id', 1), ('ap_id', 3), ('ap_id', True),
                           ('min_bps', 0), ('min_bps', 90000000),
                           ('max_bps', 40000000), ('max_bps', 2**64),
                           ('max_bps', 100000001), ('step_bps', 0),
                           ('step_bps', True), ('step_bps', 1.5),
                           ('initial_bps', self.config['links'][0]['min_bps'] - 1), ('initial_bps', 80000001),
                           ('initial_bps', 110000000)]:
            links = copy.deepcopy(self.config['links'])
            links[0][key] = value
            changes.append(dict(links=links))
        for change in changes:
            with self.subTest(change=change), patch.object(m.random, 'Random') as factory:
                with self.assertRaises(ValueError):
                    m.generate_events(dict(self.config, **change), 5)
                factory.assert_not_called()
        for cycles in (0, -1, True):
            with self.assertRaises(ValueError):
                m.generate_events(self.config, cycles)


class CapacityCliTest(unittest.TestCase):
    def run_cli(self, output, *args, ok=True):
        result = subprocess.run([sys.executable, str(ROOT/'scripts/run_capacity_scenario.py'),
                                 '--generate-only', '--output-root', str(output), *args],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0 if ok else 1, result.stdout + result.stderr)
        return result

    def test_presets_defaults_and_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            for mode in [None, 'fixed', 'random', 'constant', 'random_walk']:
                output = Path(directory)/str(mode)
                self.run_cli(output, *([] if mode is None else ['--capacity-mode', mode]))
                manifest = json.loads(next(output.glob('*/effective_config.json')).read_text())
                expected_mode = mode or 'fixed'
                self.assertEqual(manifest['mode'], expected_mode)
                config = json.loads((ROOT/'configs/capacity'/m.SCENARIOS[expected_mode]).read_text())
                self.assertEqual(manifest['events'], m.generate_events(config['capacity_variation'], 5))
                if mode == 'random_walk':
                    generation = manifest['capacity_generation']
                    self.assertEqual(generation['algorithm'], 'clipped_random_walk_v1')
                    self.assertEqual(generation['first_update_cycle'], 1)
                    self.assertEqual(generation['ap_order'], [0, 1, 2])

    def test_custom_initial_capacities_and_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = json.loads((ROOT/'configs/capacity/random_walk_all.json').read_text())
            rates = [55000000, 35000000, 25000000]
            for link, rate in zip(config['capacity_variation']['links'], rates):
                link.update(initial_bps=rate, min_bps=15000000,
                            max_bps=85000000, step_bps=5000000)
            scenario = root/'scenario.json'
            scenario.write_text(json.dumps(config))
            self.run_cli(root/'generate', '--scenario', str(scenario))
            run = next((root/'generate').iterdir())
            self.run_cli(root/'replay', '--scenario', str(scenario),
                         '--events', str(run/'planned_events.csv'))
            for output in (root/'generate', root/'replay'):
                meta = json.loads(next(output.glob('*/effective_config.json')).read_text())
                self.assertEqual(meta['initial_bps'], dict(zip(['0', '1', '2'], rates)))
                for option, rate in zip(['pgwCerRate', 'ap1BackhaulRate', 'ap2BackhaulRate'], rates):
                    self.assertIn(f'--{option}={rate}bps', meta['argv'])

    def test_override_and_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            scenario = 'configs/capacity/random_walk_all.json'
            self.run_cli(root/'generate', '--scenario', scenario, '--capacity-mode', 'random_walk',
                         '--event-seed', '20', '--rng-seed', '42')
            run = next((root/'generate').iterdir())
            config = json.loads((run/'effective_config.json').read_text())
            self.assertEqual(config['seed'], 42)
            self.assertEqual(config['capacity_generation']['event_seed'], 20)
            self.run_cli(root/'replay', '--scenario', scenario, '--events', str(run/'planned_events.csv'))
            replay = next((root/'replay').iterdir())
            meta = json.loads((replay/'effective_config.json').read_text())
            self.assertEqual(meta['mode'], 'replay')
            self.assertNotIn('capacity_generation', meta)
            self.assertEqual(meta['replay_source_sha256'], config['planned_events_sha256'])
            self.assertEqual((replay/'planned_events.csv').read_bytes(), (run/'planned_events.csv').read_bytes())
            # Legacy replay does not require --scenario or the new mode option.
            self.run_cli(root/'legacy_replay', '--events', str(run/'planned_events.csv'))

    def test_conflicts_do_not_create_output(self):
        cases = [
            ['--capacity-mode', 'random_walk', '--scenario', 'configs/capacity/fixed_all.json'],
            ['--capacity-mode', 'fixed', '--event-seed', '1'],
            ['--capacity-mode', 'constant', '--event-seed', '1'],
            ['--capacity-mode', 'random_walk', '--event-seed', '-1'],
            ['--events', 'unused.csv', '--capacity-mode', 'random_walk'],
            ['--events', 'unused.csv', '--event-seed', '1']]
        with tempfile.TemporaryDirectory() as directory:
            for index, args in enumerate(cases):
                with self.subTest(args=args):
                    output = Path(directory)/str(index)
                    self.run_cli(output, *args, ok=False)
                    self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
