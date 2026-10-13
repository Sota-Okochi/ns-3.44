#!/usr/bin/env python3
"""Rebuild master+kameda into a NEW directory using the existing ns-3.44 Ninja cache.

No configure, no existing object/library/binary replacement, no ns3 lock changes.
Unmodified ns-3 libraries are copied to the new directory. This is a local
incremental build, not a portable clean build; requires a working existing build.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'build/marl-ppo-v1')
    p.add_argument('--jobs',type=int,default=1)
    args=p.parse_args()
    if args.jobs<1: p.error('--jobs must be positive')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    cache=ROOT/'cmake-cache';lib=out/'lib';lib.mkdir()
    commands=json.loads((cache/'compile_commands.json').read_text())
    selected=[e for e in commands if '/master/' in e['file'] or '/contrib/kameda/' in e['file']]
    selected=[e for e in selected if '/test/' not in e['file'] and '/examples/' not in e['file']]
    if not any(e['file'].endswith('/marl-ppo.cc') for e in selected):
        ref=next(e for e in selected if e['file'].endswith('/reactive-dqn.cc'))
        old=ref['file'];new=str(ROOT/'contrib/kameda/model/server/marl-ppo.cc')
        selected.append(dict(ref,file=new,command=ref['command'].replace(old,new).replace('reactive-dqn.cc.o','marl-ppo.cc.o')))
    # Snapshot unchanged runtime libraries. Local rebuilt kameda will overwrite ONLY its copy.
    hashes={}
    for src in (ROOT/'build/lib').glob('*.so*'):
        if src.is_file():
            shutil.copy2(src,lib/src.name)
            hashes[src.name]=hashlib.sha256(src.read_bytes()).hexdigest()
    mappings={};compile_jobs=[]
    for e in selected:
        cmd=shlex.split(e['command']);i=cmd.index('-o')+1;old=cmd[i]
        obj=out/'objects'/old;obj.parent.mkdir(parents=True,exist_ok=True)
        mappings[old]=str(obj);cmd[i]=str(obj)
        compile_jobs.append((cmd,e['directory']))
    (out/'commands.json').write_text(json.dumps(compile_jobs,indent=2))
    def compile_one(job):
        cmd,cwd=job
        print('Compiling',cmd[-1],flush=True)
        subprocess.run(cmd,cwd=cwd,check=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:list(pool.map(compile_one,compile_jobs))
    links=[]
    for target in ('kameda','master'):
        lines=subprocess.check_output(['ninja','-C',str(cache),'-t','commands',target],text=True).splitlines()
        raw=shlex.split(lines[-1]);cmd=raw[2:-2] if raw[:2]==[':', '&&'] and raw[-2:]==['&&', ':'] else raw
        if any(x in cmd for x in ('&&',';',':')):raise RuntimeError('Unsupported cached link command')
        i=cmd.index('-o')+1
        destination=lib/Path(cmd[i]).name if target=='kameda' else out/'master'
        cmd[i]=str(destination)
        for j,arg in enumerate(cmd):
            if arg in mappings:cmd[j]=mappings[arg]
            elif arg.startswith('-L') and '/build/lib' in arg:cmd[j]='-L'+str(lib)
            elif arg.startswith('-Wl,-rpath,'):cmd[j]='-Wl,-rpath,'+str(lib)
            elif arg.endswith('.so') and '/build/lib/' in arg and j!=i:cmd[j]=str(lib/Path(arg).name)
        if target=='kameda':
            obj=next(v for k,v in mappings.items() if k.endswith('marl-ppo.cc.o'))
            if obj not in cmd:cmd.insert(1,obj)
        links.append(cmd)
        subprocess.run(cmd,cwd=cache,check=True)
    (out/'link_commands.json').write_text(json.dumps(links,indent=2))
    (out/'build_manifest.json').write_text(json.dumps(dict(ns3='3.44',dependency_sha256=hashes,
        source_sha256={e['file']:hashlib.sha256(Path(e['file']).read_bytes()).hexdigest() for e in selected},
        binary_sha256=hashlib.sha256((out/'master').read_bytes()).hexdigest()),indent=2))
    print('Isolated binary:',out/'master')


if __name__=='__main__':main()
