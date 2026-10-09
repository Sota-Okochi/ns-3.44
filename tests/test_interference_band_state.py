"""Compare full time-ordered interference histories with the saved pre-change build."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASELINE = Path(os.environ.get("NS3_INTERFERENCE_AB_BASELINE",
                               ROOT / "results/perf/wifi_interference_record_before")).resolve()


class BandStateTest(unittest.TestCase):
    @unittest.skipUnless((BASELINE / "manifest.json").exists(), "saved baseline required")
    def test_history_and_reference_power(self):
        with tempfile.TemporaryDirectory() as tmp:
            outputs = []
            for name, runtime in (("before", BASELINE), ("after", ROOT / "build")):
                exe = Path(tmp) / name
                command = ["g++", "-std=c++20", "-O2", "-I" + str(runtime / "include"),
                           str(ROOT / "tests/interference_band_state_regression.cc"),
                           "-L" + str(runtime / "lib"), "-Wl,-rpath," + str(runtime / "lib"),
                           "-lns3.44-wifi-optimized", "-lns3.44-spectrum-optimized",
                           "-lns3.44-network-optimized", "-lns3.44-core-optimized", "-o", str(exe)]
                if name == "after" or "struct BandInterferenceState" in (runtime / "include/ns3/interference-helper.h").read_text():
                    command.append("-DNS3_UNIFIED_BAND_STATE")
                subprocess.run(command, check=True)
                env = os.environ.copy()
                env["LD_LIBRARY_PATH"] = str(runtime / "lib")
                outputs.append(subprocess.check_output([str(exe)], text=True, env=env))
            self.assertEqual(*outputs)
            self.assertIn("events=6", outputs[1])


if __name__ == "__main__":
    unittest.main()
