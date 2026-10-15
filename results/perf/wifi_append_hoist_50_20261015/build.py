"""Build three diagnostic Wi-Fi objects and relink a private library only."""
from pathlib import Path
import json,shlex,subprocess,shutil,hashlib
from concurrent.futures import ThreadPoolExecutor
root=Path.cwd();out=root/'build/wifi-append-hoist-v1';out.mkdir(exist_ok=False)
base=root/'build/marl-internal-report-v1';shutil.copytree(base/'lib',out/'lib');shutil.copy2(base/'master',out/'master')
files={'spectrum-wifi-phy.cc','phy-entity.cc','interference-helper.cc'}
entries=[e for e in json.loads((root/'cmake-cache/compile_commands.json').read_text()) if Path(e['file']).name in files and '/src/wifi/model/' in e['file']]
assert len(entries)==3
mapping={};jobs=[]
for e in entries:
 cmd=shlex.split(e['command']);i=cmd.index('-o')+1;old=cmd[i];obj=out/'objects'/old;obj.parent.mkdir(parents=True,exist_ok=True);mapping[old]=str(obj);cmd[i]=str(obj);jobs.append((cmd,e['directory']))
def run(x):subprocess.run(x[0],cwd=x[1],check=True)
with ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(run,jobs))
raw=shlex.split(subprocess.check_output(['ninja','-C',str(root/'cmake-cache'),'-t','commands','wifi'],text=True).splitlines()[-1]);cmd=raw[2:-2] if raw[:2]==[':', '&&'] and raw[-2:]==['&&', ':'] else raw
assert not any(x in cmd for x in ('&&',';',':'))
i=cmd.index('-o')+1;cmd[i]=str(out/'lib'/Path(cmd[i]).name)
for j,x in enumerate(cmd):
 if x in mapping:cmd[j]=mapping[x]
 elif x.startswith('-Wl,-rpath,'):cmd[j]='-Wl,-rpath,'+str(out/'lib')
 elif x.endswith('.so') and '/build/lib/' in x and j!=i:cmd[j]=str(out/'lib'/Path(x).name)
subprocess.run(cmd,cwd=root/'cmake-cache',check=True)
# Copied master retains its previous RUNPATH; wrapper explicitly selects private libs.
w=out/'run-master';w.write_text('#!/bin/sh\nexport LD_LIBRARY_PATH='+shlex.quote(str(out/'lib'))+'\nexec '+shlex.quote(str(out/'master'))+' "$@"\n');w.chmod(0o755)
(out/'build_manifest.json').write_text(json.dumps({'compile':jobs,'link':cmd,'source_sha256':{e['file']:hashlib.sha256(Path(e['file']).read_bytes()).hexdigest() for e in entries},'profiler_header_sha256':hashlib.sha256((root/'src/core/model/research-wall-profiler.h').read_bytes()).hexdigest()},indent=2))
print(out)
