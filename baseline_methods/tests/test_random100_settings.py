import contextlib
import io
import json
from pathlib import Path
import random
import sys
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from simulation.config import load_sim_config
from simulation.services import cal, create, rand
from scripts.generate_logistic_teacher import _build_feature_rows
from scripts.calibrate_random100 import summarize


class SettingsTest(unittest.TestCase):
    def test_calibration_matches_config(self):
        report = json.loads((ROOT / load_sim_config()['calibration_report']).read_text())
        self.assertEqual(load_sim_config()['link_model'], report['link_model'])
        self.assertEqual(load_sim_config()['termNum'], 100)
        self.assertEqual(len(report['sources']), 5)
        self.assertEqual(sum(s['rows'] for s in report['sources']), 1300)
        self.assertEqual(load_sim_config()['app_distribution'], [.2, .4, .15, .25])

    def test_explicit_calibration_reproducible(self):
        report = json.loads((ROOT / load_sim_config()['calibration_report']).read_text())
        source = ROOT.parent / 'OUTPUT/100/random'
        files = [source / entry['file'] for entry in report['sources']]
        self.assertEqual(summarize(source, files), report)
        with self.assertRaises(ValueError):
            summarize(source, [files[0], files[0]])

    def snapshot(self):
        random.seed(123)
        np.random.seed(123)
        aps, terms = create.createAp(3), create.createTerm(100)
        with contextlib.redirect_stdout(io.StringIO()):
            rand.randAp(terms, aps)
            rand.randApp(terms, aps)
        cal.calLink(terms, aps, 90)
        return _build_feature_rows(0, 123, terms, aps)

    def test_reproducible_features(self):
        rows = self.snapshot()
        self.assertEqual(rows, self.snapshot())
        self.assertEqual(len(rows), 100)
        self.assertEqual(sum(rows[0][f'num_users_ap{a}'] for a in range(3)), 100)

    def test_positive_quality_over_entire_assignment_range(self):
        aps, terms = create.createAp(3), create.createTerm(100)
        model = load_sim_config()['link_model']
        for endpoint in (0, 1):
            for a, ap in enumerate(aps):
                ap.setQualityVariation(model['tp_scale_range'][a][endpoint],
                                       model['rtt_offset_range_ms'][a][endpoint])
            for n in range(101):
                for i, term in enumerate(terms):
                    term.setSwitchAp(0 if i < n else 1)
                cal.calLink(terms, aps, 90)
                for ap in aps:
                    self.assertTrue(np.isfinite([ap.tp, ap.rtt]).all())
                    self.assertGreater(ap.tp, 0)
                    self.assertGreater(ap.rtt, 0)

    def test_application_requirements_unchanged(self):
        self.assertEqual([cal.calAppNeed(a).needTP for a in (1, 2)], [3.0, 8.0])
        self.assertEqual([cal.calAppNeed(a).needRTT for a in (3, 4)], [100.0, 40.0])


if __name__ == '__main__':
    unittest.main()
