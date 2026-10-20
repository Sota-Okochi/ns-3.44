"""Plot one resumed training series; no independent model-seed averaging."""
import csv,json,hashlib,os
from pathlib import Path
os.environ.setdefault('MPLCONFIGDIR','/tmp/marl-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
out=Path(__file__).resolve().parent
base=out.parent
roots=[base/n for n in ['train80_b8_episodes64_261016','train80_b8_episodes192_261017','train80_b8_episodes256_261019']]
read=lambda p:list(csv.DictReader(p.open()))
assert len({json.loads((p/'manifest.json').read_text())['experiment_id'] for p in roots})==1
train=sorted([r for p in roots for r in read(p/'episodes.csv') if r['phase']=='train'],key=lambda r:int(r['episode_id']))
assert [int(r['episode_id']) for r in train]==list(range(256))
val=sorted([r for p in roots for r in read(p/'validation.csv')],key=lambda r:int(r['episodes']))
updates=sorted([r for p in roots for r in read(p/'updates.csv')],key=lambda r:int(r['episodes']))
x=list(range(1,257)); y=[float(r['mean_h_after']) for r in train]
ma=[sum(y[i-15:i+1])/16 if i>=15 else float('nan') for i in range(256)]
vx=[int(r['episodes']) for r in val];vy=[float(r['score']) for r in val]
plt.rcParams.update({'font.size':10,'figure.dpi':130,'savefig.dpi':180})
fig,axs=plt.subplots(2,1,figsize=(10,7),sharex=True)
axs[0].plot(x,y,color='#9aabc0',alpha=.65,lw=.7,label='Training episode (different environment seeds)')
axs[0].plot(x,ma,color='#1765ad',lw=2,label='Trailing 16-episode mean')
axs[0].set_ylabel('Mean H, cycles 2–5\n(dimensionless)');axs[0].legend(loc='upper left',fontsize=9)
axs[0].set_title('MARL PPO learning curves: 80 UEs, model seed 1, epsilon=1e-6')
axs[1].plot(vx,vy,'o-',color='#db6b24',label='Validation mean: fixed seeds 1011–1015')
for a,b in zip(vx,vy):axs[1].annotate(f'{b:.5f}',(a,b),xytext=(0,8),textcoords='offset points',ha='center',fontsize=9)
axs[1].set_ylim(.275,.31);axs[1].set_ylabel('Validation mean H\n(dimensionless)');axs[1].set_xlabel('Completed training episodes');axs[1].legend(loc='lower right',fontsize=9)
for ax in axs:ax.grid(alpha=.25);ax.set_xlim(-8,264);ax.set_xticks([0,32,64,96,128,160,192,224,256])
fig.tight_layout()
for ext in ['png','pdf']:fig.savefig(out/f'learning_curves.{ext}')
plt.close(fig)
fig,axs=plt.subplots(2,2,figsize=(10,6.5),sharex=True)
ux=[int(r['episodes']) for r in updates]
for ax,key,label in zip(axs.flat,['entropy','kl','critic_loss','clip_fraction'],['Policy entropy (nats)','KL divergence (nats)','Critic mean squared error','Clip fraction']):
 ax.plot(ux,[float(r[key]) for r in updates],'o-',ms=3,label=key);ax.set_ylabel(label);ax.set_xlabel('Completed training episodes');ax.grid(alpha=.25);ax.legend(fontsize=8)
fig.suptitle('PPO update diagnostics: 32 updates, 8 episodes per rollout')
fig.tight_layout()
for ext in ['png','pdf']:fig.savefig(out/f'ppo_diagnostics.{ext}')
plt.close(fig)
for name,rows in [('training',train),('validation',val),('updates',updates)]:
 with (out/f'{name}.csv').open('w') as f:
  w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
(out/'plot_config.json').write_text(json.dumps({'runs':[str(p) for p in roots],'window':16,'training_x':'episode_id+1','validation_smoothing':False,'model_seed':1,'epsilon':1e-6,'note':'Validation y axis is cropped. No confidence bands: one model seed. Training environment seeds differ by episode.','source_sha256':{str(p/n):hashlib.sha256((p/n).read_bytes()).hexdigest() for p in roots for n in ['episodes.csv','validation.csv','updates.csv','manifest.json']}},indent=2))
print('Saved PNG/PDF, merged CSV and plot configuration to',out)
