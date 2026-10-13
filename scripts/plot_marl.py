#!/usr/bin/env python3
"""Reproducible MARL curves: trailing-16 training means; unsmoothed validation.

One or more same-N, independent model-seed run directories. Never chooses a model.
Requires the project's existing matplotlib; no data generated or imputed.
"""
import argparse
import csv
import json
from pathlib import Path
import statistics


def trailing(values, window=16):
    result = []
    for i in range(len(values)):
        part = values[max(0,i-window+1):i+1]
        result.append(sum(part)/window if len(part)==window and all(v is not None for v in part) else None)
    return result


def read(path):
    if not path.is_file(): return []
    with path.open() as f: return list(csv.DictReader(f))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', nargs='+', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args=parser.parse_args()
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    args.output.mkdir(parents=True,exist_ok=True)
    manifests=[json.loads((p/'manifest.json').read_text()) for p in args.runs]
    sizes={m['args']['terminals'] for m in manifests}
    seeds=[m['args']['model_seed'] for m in manifests]
    if len(sizes)!=1 or len(set(seeds))!=len(seeds):
        parser.error('Use one N and one directory per independent model seed')
    for kind in ('training','validation'):
        fig,ax=plt.subplots()
        series=[]
        for directory,seed in zip(args.runs,seeds):
            if kind=='training':
                rows=[r for r in read(directory/'episodes.csv') if r['phase']=='train']
                if not rows: continue
                by_id={int(r['episode_id']):float(r['mean_h_after']) for r in rows}
                # Explicit gaps remain None; never compress missing episodes away.
                start,end=min(by_id),max(by_id)
                x=list(range(start+1,end+2));raw=[by_id.get(i) for i in range(start,end+1)]
                y=trailing(raw)
                ax.plot(x,raw,alpha=.15,linewidth=.6,label=f'seed {seed} raw')
                ax.plot(x,y,alpha=.5,label=f'seed {seed} trailing 16')
            else:
                rows=read(directory/'validation.csv')
                if not rows: continue
                x=[int(r['episodes']) for r in rows];y=[float(r['score']) for r in rows]
                ax.plot(x,y,marker='o',alpha=.5,label=f'seed {seed}')
            series.append(dict(zip(x,y)))
        if series:
            common=sorted(set.intersection(*(set(s) for s in series)))
            common=[x for x in common if all(s[x] is not None for s in series)]
            means=[statistics.mean(s[x] for s in series) for x in common]
            ax.plot(common,means,color='black',linewidth=2,label=f'mean ({len(series)} seeds)')
            if len(series)>1:
                sd=[statistics.stdev(s[x] for s in series) for x in common]
                ax.fill_between(common,[m-v for m,v in zip(means,sd)],[m+v for m,v in zip(means,sd)],
                                color='black',alpha=.15,label='±1 SD (model seeds)')
        ax.set_xlabel('Training episodes')
        ax.set_ylabel('Mean harmonic satisfaction, cycles 2–5 (dimensionless)')
        ax.set_title(f'MARL PPO: {kind}, N={next(iter(sizes))}')
        if series: ax.legend()
        fig.tight_layout()
        for ext in ('png','pdf'): fig.savefig(args.output/f'{kind}.{ext}')
        plt.close(fig)
    (args.output/'plot_config.json').write_text(json.dumps(dict(runs=[str(p.resolve()) for p in args.runs],
        window=16,validation_smoothing=False,band='sample SD across model seeds',terminals=next(iter(sizes))),indent=2))


if __name__=='__main__': main()
