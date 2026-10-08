"""Small two-seed before/after integration; not the 80-terminal research experiment."""
import csv
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / "results/perf/wifi_shared_bands_before"


class SharedBandsIntegration(unittest.TestCase):
    @unittest.skipUnless((BEFORE / "include/ns3/phy-entity.h").exists(), "saved baseline headers/libraries required")
    def test_two_seeds(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            modules = ("wifi", "spectrum", "propagation", "internet", "mobility", "network", "core")
            for name, runtime in (("before", BEFORE), ("after", ROOT / "build")):
                exe = tmp / name
                command = ["g++", "-std=c++20", "-O2", "-I" + str(runtime / "include"),
                           str(ROOT / "tests/research_wifi_perf_smoke.cc"),
                           "-L" + str(runtime / "lib"), "-Wl,-rpath," + str(runtime / "lib"),
                           *[f"-lns3.44-{m}-optimized" for m in modules], "-o", str(exe)]
                subprocess.run(command, check=True)
                for seed in (1001, 1002):
                    env = os.environ.copy()
                    env["LD_LIBRARY_PATH"] = str(runtime / "lib")
                    output = subprocess.check_output([str(exe), str(tmp / f"{name}_{seed}"), str(seed)],
                                                     env=env, text=True)
                    (tmp / f"{name}_{seed}.txt").write_text(output)
            for seed in (1001, 1002):
                self.assertEqual((tmp / f"before_{seed}.txt").read_text(),
                                 (tmp / f"after_{seed}.txt").read_text())
                for filename in ("devices.csv", "boundaries.csv"):
                    results = []
                    for name in ("before", "after"):
                        with (tmp / f"{name}_{seed}" / filename).open() as stream:
                            rows = list(csv.DictReader(stream))
                        for row in rows:
                            row.pop("interval_wall_ms", None)
                        results.append(rows)
                    self.assertEqual(*results)
                print(f"seed={seed}: " + (tmp / f"after_{seed}.txt").read_text().strip(), flush=True)


if __name__ == "__main__":
    unittest.main()
