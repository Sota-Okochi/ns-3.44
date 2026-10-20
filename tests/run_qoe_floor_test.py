"""Run formula checks against an isolated build without changing ns-3's build cache."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, default=ROOT/'build/qoe-floor001-v1')
a = p.parse_args()
build = a.build.resolve()
commands = json.loads((build/'commands.json').read_text())
cmd, cwd = next(e for e in commands if e[0][-1].endswith('/master/main.cc'))
cmd = list(cmd)
cmd[cmd.index('-o')+1] = str(build/'qoe_floor_test.o')
cmd[-1] = str(ROOT/'tests/qoe_floor_test.cc')
link = json.loads((build/'link_commands.json').read_text())[-1]
link = [s for s in link if not s.endswith('.o')]
link.insert(1, str(build/'qoe_floor_test.o'))
link[link.index('-o')+1] = str(build/'qoe_floor_test')
(build/'qoe_floor_test_commands.json').write_text(json.dumps([cmd,link],indent=2))
subprocess.run(cmd, cwd=cwd, check=True)
subprocess.run(link, cwd=cwd, check=True)
subprocess.run([str(build/'qoe_floor_test')], cwd=build, check=True)
