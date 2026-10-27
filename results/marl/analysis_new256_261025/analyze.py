import csv,json,statistics as s,math,hashlib,subprocess,sys,os
from pathlib import Path
out=Path(__file__).resolve().parent
roots=[out.parent/n for n in ['train80_new_episodes128_261021','train80_new_episodes256_261023']]
read=lambda p:list(csv.DictReader(p.open()))
ms=[json.loads((p/'manifest.json').read_text()) for p in roots];assert ms[0]['experiment_id']==ms[1]['experiment_id']
(out/'manifest.json').write_text(json.dumps(ms[-1],indent=2))
merged={}
for file in ['episodes.csv','validation.csv','updates.csv']:
 rows=sum([read(p/file) for p in roots],[]);merged[file]=rows
 with (out/file).open('w') as f:
  w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
r=merged['episodes.csv'];train=sorted([x for x in r if x['phase']=='train'],key=lambda x:int(x['episode_id']));assert [int(x['episode_id']) for x in train]==list(range(256))
summary={'validation':{},'training_blocks':{},'status':{}}
for lo,hi in [(0,64),(64,128),(128,192),(192,256)]:
 a=train[lo:hi];summary['training_blocks'][f'{lo+1}-{hi}']={k:s.mean(float(x[k]) for x in a) for k in ['mean_h_after','initial_h']}
for n in [0,64,128,192,256]:
 a=sorted([x for x in r if x['phase']=='validation' and int(x['policy_version'])==n//8],key=lambda x:int(x['environment_seed']));assert len(a)==5
 summary['validation'][n]={x['environment_seed']:float(x['mean_h_after']) for x in a}
for a,b in [(128,256),(0,256),(192,256)]:
 d=[summary['validation'][b][k]-v for k,v in summary['validation'][a].items()];m=s.mean(d);se=s.stdev(d)/math.sqrt(5);summary[f'delta_{a}_{b}']={'mean':m,'positive_seeds':sum(x>0 for x in d),'t95':[m-2.776*se,m+2.776*se]}
for p in roots:
 st=[json.loads(x.read_text()) for x in p.glob('**/status.json')];summary['status'][p.name]={'count':len(st),'valid':sum(x.get('valid') is True for x in st)}
summary['sources']=[str(p) for p in roots]
summary['source_hashes']={str(p/f):hashlib.sha256((p/f).read_bytes()).hexdigest() for p in roots for f in ['manifest.json','episodes.csv','validation.csv','updates.csv']}
(out/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps({k:v for k,v in summary.items() if k!='source_hashes'},indent=2))
env=dict(os.environ,MPLCONFIGDIR='/tmp/marl-matplotlib');subprocess.run([sys.executable,'scripts/plot_marl.py',str(out),'--output',str(out/'figures')],check=True,env=env)
os.environ['MPLCONFIGDIR']='/tmp/marl-matplotlib'
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,ax=plt.subplots(figsize=(9,4.5))
for seed in summary['validation'][0]:
 ns=list(summary['validation']);ax.plot(ns,[summary['validation'][n][seed] for n in ns],'o-',label=f'environment seed {seed}')
ax.set(xlabel='Completed training episodes',ylabel='Mean H, cycles 2–5 (dimensionless)',title='Fixed validation seeds: model seed 1, satisfaction floor 0.01');ax.grid(alpha=.25);ax.legend();fig.tight_layout()
for ext in ['png','pdf']:fig.savefig(out/'figures'/f'validation_by_seed.{ext}',dpi=180)
