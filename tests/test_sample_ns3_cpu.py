import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("sampler", Path(__file__).resolve().parents[1] / "scripts/sample_ns3_cpu.py")
sampler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sampler)


class SamplerTest(unittest.TestCase):
    def test_bounded_attach_not_launch(self):
        command = sampler.record_command(1234, 60, 49, Path("/tmp/test.data"))
        self.assertEqual(command[command.index("-p") + 1], "1234")
        self.assertEqual(command[-3:], ["--", "sleep", "60"])
        self.assertIn("dwarf,16384", command)
        self.assertNotIn("sudo", command)

    def test_reject_mixed_detailed_profile(self):
        for args in (["--perfDetailed=1"], ["--perfDetailed=true"], ["--perfDetailed"], ["--perfDetailed", "true"]):
            self.assertTrue(sampler.has_detailed_timing(args))
        for args in ([], ["--perfTiming=1"], ["--perfDetailed=0"], ["--perfDetailed", "false"]):
            self.assertFalse(sampler.has_detailed_timing(args))


if __name__ == "__main__":
    unittest.main()
