"""Integration against the already-built optimized ns-3 libraries (no research config edits)."""
import csv
from pathlib import Path
import subprocess
import tempfile
import unittest
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[1]


class WifiIntegrationTest(unittest.TestCase):
    def test_event_and_packet_equivalence_and_counters(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            exe = tmp / "smoke"
            modules = ("wifi", "spectrum", "propagation", "internet", "mobility", "network", "core")
            command = ["g++", "-std=c++20", "-O2", "-I" + str(ROOT / "build/include"),
                       str(ROOT / "tests/research_wifi_perf_smoke.cc"),
                       "-L" + str(ROOT / "build/lib"), "-Wl,-rpath," + str(ROOT / "build/lib"),
                       *[f"-lns3.44-{m}-optimized" for m in modules], "-o", str(exe)]
            subprocess.run(command, check=True)
            off = subprocess.check_output([str(exe)], text=True)
            on = subprocess.check_output([str(exe), str(tmp / "on")], text=True)
            self.assertEqual(off, on)
            self.assertIn("bytes=200", on)
            with (tmp / "on/devices.csv").open() as stream:
                rows = [r for r in csv.DictReader(stream) if r["snapshot"] == "final"]
            counts = defaultdict(lambda: defaultdict(int))
            for row in rows:
                key = (row["node_id"], row["net_device_index"], row["selection"])
                counts[key][row["metric"]] += int(row["count"])
                self.assertEqual(row["ap_id_1based"], "2")
                self.assertEqual(row["rat"], "wifi")
            self.assertTrue(any(k[2] == "selected" for k in counts))
            self.assertTrue(any(k[2] == "unselected" for k in counts))
            outcomes = ("wifi_inactive_phy", "wifi_foreign", "wifi_disabled", "wifi_weak",
                        "wifi_cannot_start", "wifi_preamble")
            for group in counts.values():
                self.assertEqual(group["wifi_start_rx"], sum(group[x] for x in outcomes))
                self.assertEqual(group["rx_arrival"], group["rx_scheduled"])
            with (tmp / "on/functions.csv").open() as stream:
                names = {r["function"] for r in csv.DictReader(stream) if r["snapshot"] == "final"}
            self.assertTrue({"WifiRx.sampled.regular_bands", "WifiRx.sampled.he_ru_bands",
                             "WifiRx.sampled.band_power", "WifiRx.sampled.map_insert",
                             "WifiRx.sampled.post_power", "WifiRx.sampled.start_preamble"} <= names)
            subprocess.run(["python3", str(ROOT / "scripts/summarize_perf.py"), str(tmp / "on")],
                           check=True, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    unittest.main()
