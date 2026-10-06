"""Standalone profiler tests: no full ns-3 experiment or external Python packages."""
import csv
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "research-wall-profiler.h"
#include <stdexcept>
#include <string>
using P = ns3::ResearchWallProfiler;
void earlyReturn() { P::Scope scope("early_return"); return; }
int main(int, char** argv) {
    auto& p = P::Get();
    { P::Scope off("disabled"); }
    p.Record("disabled_record", 99);
    for (bool detailed : {false, true}) {
        const std::string dir = std::string(argv[1]) + (detailed ? "/detail" : "/coarse");
        p.Start(dir, detailed, 4);
        p.RegisterDevice(7, 2, 2, "wifi", "terminal");
        p.RegisterDevice(8, 1, 2, "wifi", "base_station");
        p.SelectAp(7, 2);
        p.Count(7, 2, P::Metric::WIFI_START);
        p.SelectAp(7, 1);
        p.Count(7, 2, P::Metric::WIFI_START);
        p.Count(8, 1, P::Metric::TX_SIGNAL);
        p.Count(9, 1, P::Metric::RX_ARRIVAL);
        unsigned samples = 0;
        for (int i = 0; i < 9; ++i) samples += p.SampleWifiRx();
        if (samples != (detailed ? 3u : 0u)) return 3;
        { P::Scope unsampled("not_selected", true, false); }
        p.Boundary("run_start", 0, 0, 0);
        {
            P::Scope outer("outer");
            earlyReturn(); earlyReturn();
            { P::Scope inner("detail_only", true); }
            p.Record("known", 2.5); p.Record("known", 7.5);
            p.Boundary("cycle_end", 1, 9.5, 30);
        }
        p.Boundary("run_end", 0, 10, 35);
        p.Finish();
        try { p.Start(dir, detailed); return 2; }
        catch (const std::runtime_error&) {}
    }
}
'''


class ProfilerTest(unittest.TestCase):
    def test_modes_scopes_boundaries_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            source = tmp / "test.cc"
            source.write_text(SOURCE)
            exe = tmp / "test"
            subprocess.run([
                "g++", "-std=c++20", "-O2", "-Wall", "-Wextra",
                "-I", str(ROOT / "src/core/model"), str(source),
                str(ROOT / "src/core/model/research-wall-profiler.cc"),
                "-o", str(exe),
            ], check=True)
            subprocess.run([str(exe), str(tmp)], check=True)
            for mode in ("coarse", "detail"):
                with (tmp / mode / "functions.csv").open() as f:
                    rows = list(csv.DictReader(f))
                final = {r["function"]: r for r in rows if r["snapshot"] == "final"}
                self.assertNotIn("not_selected", final)
                self.assertNotIn("disabled", final)
                self.assertNotIn("disabled_record", final)
                self.assertEqual("detail_only" in final, mode == "detail")
                self.assertEqual(final["early_return"]["call_count"], "2")
                self.assertEqual(float(final["known"]["total_wall_ms"]), 10)
                self.assertEqual(float(final["known"]["mean_wall_ms"]), 5)
                self.assertEqual(float(final["known"]["max_wall_ms"]), 7.5)
                self.assertFalse(any(r["function"] == "outer" and r["snapshot"] == "cycle_end"
                                     for r in rows))
                self.assertGreaterEqual(float(final["outer"]["total_wall_ms"]),
                                        float(final["early_return"]["total_wall_ms"]))
                with (tmp / mode / "boundaries.csv").open() as f:
                    boundaries = list(csv.DictReader(f))
                self.assertEqual([r["interval_event_count"] for r in boundaries], ["0", "30", "5"])
                self.assertEqual(float(boundaries[2]["interval_sim_s"]), 0.5)
                with (tmp / mode / "devices.csv").open() as f:
                    counters = [r for r in csv.DictReader(f) if r["snapshot"] == "final"]
                if mode == "coarse":
                    self.assertEqual(counters, [])
                else:
                    self.assertEqual(len(counters), 4)
                    self.assertEqual({r["selection"] for r in counters},
                                     {"selected", "unselected", "infrastructure", "unknown"})
                    self.assertTrue(all(r["count"] == "1" for r in counters))

                self.assertTrue(all(float(r["interval_wall_ms"]) >= 0 for r in boundaries))


if __name__ == "__main__":
    unittest.main()
