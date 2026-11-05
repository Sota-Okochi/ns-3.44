"""Reproducible offline analysis of the contiguous 80-UE, model-seed-1 run."""
import os
os.environ.setdefault('MPLCONFIGDIR', '/tmp/marl-analysis-mpl')
import json
import math
import hashlib
from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parents[3]
OUT=Path(__file__).resolve().parent
RUNS=[ROOT/'results/marl'/n for n in (
 'train80_new_episodes128_261021','train80_new_episodes256_261023',
 'train80_new_episodes384_261025','train80_new_episodes512_261101')]
ms=[json.loads((p/'manifest.json').read_text()) for p in RUNS]
assert len({m['experiment_id'] for m in ms})==1
train=pd.concat([pd.read_csv(p/'episodes.csv') for p in RUNS],ignore_index=True)
train=train[train.phase=='train'].sort_values('episode_id').copy()
assert train.episode_id.tolist()==list(range(512))
assert train.environment_seed.nunique()==512
assert np.allclose(train.return_sum,4*train.mean_h_after)
train['episodes']=train.episode_id+1
train['reward_ma16']=train.mean_h_after.rolling(16).mean()
train['return_ma16']=train.return_sum.rolling(16).mean()
updates=pd.concat([pd.read_csv(p/'updates.csv') for p in RUNS],ignore_index=True).sort_values('episodes')
assert updates['update'].tolist()==list(range(1,65))
validation=[];cycles=[];policy=[];apps=[];statuses=[];inputs={}
for run in RUNS:
 for name in ('manifest.json','episodes.csv','updates.csv','validation.csv'):
  f=run/name; inputs[str(f.relative_to(ROOT))]=hashlib.sha256(f.read_bytes()).hexdigest()
 for f in run.glob('train_*/status.json'):
  statuses.append(json.loads(f.read_text()))
 for cp in sorted(run.glob('checkpoint_*')):
  if not cp.is_dir():continue
  ep=int(cp.name.split('_')[1])
  for f in sorted(cp.glob('*/qoe.csv')):
   statuses.append(json.loads((f.parent/'status.json').read_text()))
   d=pd.read_csv(f);seed=int(d.seed.iloc[0]);assert len(d)==400
   assert d.groupby('cycle_id').ue_id.nunique().tolist()==[80]*5
   assert json.loads((f.parent/'setting.json').read_text())['terminals']==80
   a=d[d.cycle_id>1].copy()
   a['unsat']=a.satisfaction<1
   a['floor']=a.satisfaction<=.010000001
   a['timeout']=(a.app_type>=2)&(a.rtt_status=='timeout')
   a['non_switch_degraded']=(a.switch_flag==0)&(a.delta_satisfaction<0)
   hs=a.groupby('cycle_id').harmonic_mean.first()
   validation.append(dict(episodes=ep,seed=seed,mean_h_after=hs.mean(),return_sum=hs.sum(),
    unsatisfied=a.unsat.sum()/4,switches=a.switch_flag.sum(),floor=a.floor.sum()/4,
    timeout=a.timeout.sum()/4,non_switch_degraded=a.non_switch_degraded.sum()/4,
    tp_mbps=a[a.app_type<2].tp_mbps.mean(),rtt_ms=a[a.app_type>=2].rtt_ms.mean()))
   for c,g in a.groupby('cycle_id'):
    assert math.isclose(80/(1/g.satisfaction).sum(),g.harmonic_mean.iloc[0],rel_tol=1e-10)
    cycles.append(dict(episodes=ep,seed=seed,cycle=c,h=g.harmonic_mean.iloc[0],unsatisfied=g.unsat.sum(),
     switches=g.switch_flag.sum(),floor=g.floor.sum(),timeout=g.timeout.sum()))
   for app,g in a.groupby('app_type'):
    apps.append(dict(episodes=ep,seed=seed,app=app,mean_s=g.satisfaction.mean(),
     unsatisfied_fraction=g.unsat.mean(),tp_mbps=g.tp_mbps.mean(),rtt_ms=g.rtt_ms.mean()))
   ts=[json.loads(l) for l in (f.parent/'transitions.jsonl').read_text().splitlines()]
   assert len(ts)==4 and np.allclose([t['reward'] for t in ts],hs)
   for t in ts:
    pr=np.exp(t['old_log_probs'])
    assert np.allclose(pr.sum(axis=1),1)
    policy.append(dict(episodes=ep,seed=seed,decision=t['decision_id'],
     p0=pr[:,0].mean(),p1=pr[:,1].mean(),p2=pr[:,2].mean(),
     within_state_probability_sd=pr.std(axis=0).mean(),
     entropy=-(pr*np.log(pr)).sum(axis=1).mean(),
     argmax0=(pr.argmax(axis=1)==0).mean(),argmax1=(pr.argmax(axis=1)==1).mean(),
     argmax2=(pr.argmax(axis=1)==2).mean()))
assert all(s['valid'] and s['returncode']==0 and s['transitions']==4 for s in statuses)
v=pd.DataFrame(validation);cy=pd.DataFrame(cycles);pol=pd.DataFrame(policy);app=pd.DataFrame(apps)
assert (v.groupby('episodes').size()==5).all()
summary=v.groupby('episodes').mean(numeric_only=True).drop(columns='seed')
summary['h_sd']=v.groupby('episodes').mean_h_after.std()
summary['return_sd']=v.groupby('episodes').return_sum.std()
for run in RUNS:
 logged=pd.read_csv(run/'validation.csv')
 for _,r in logged.iterrows():assert math.isclose(summary.loc[r.episodes,'mean_h_after'],r.score,rel_tol=1e-10)
paired=[]
for old in [0,128,256,384,448]:
 delta=v[v.episodes==512].set_index('seed').mean_h_after-v[v.episodes==old].set_index('seed').mean_h_after
 margin=2.7764451051977987*delta.std()/np.sqrt(5)
 paired.append(dict(reference=old,mean_difference=delta.mean(),ci95_low=delta.mean()-margin,
  ci95_high=delta.mean()+margin,improved_seeds=int((delta>0).sum()),
  relative_pct=100*(summary.loc[512,'mean_h_after']/summary.loc[old,'mean_h_after']-1)))
for name,df in [('training',train),('updates',updates),('validation_by_seed',v),('validation_cycles',cy),
 ('policy_probabilities',pol),('app_metrics',app),('paired_comparisons',pd.DataFrame(paired))]:df.to_csv(OUT/f'{name}.csv',index=False)
summary.to_csv(OUT/'validation_summary.csv')
def save(fig,name):
 fig.tight_layout()
 for ext in ('png','pdf'):fig.savefig(OUT/f'{name}.{ext}',dpi=170)
 plt.close(fig)
x=summary.index.to_numpy()
fig,axs=plt.subplots(2,1,figsize=(11,8),sharex=True)
for ax,raw,smooth,col,sd,label in [
 (axs[0],'return_sum','return_ma16','return_sum','return_sd','Episode return (sum of 4 rewards)'),
 (axs[1],'mean_h_after','reward_ma16','mean_h_after','h_sd','Mean reward / harmonic satisfaction H')]:
 ax.plot(train.episodes.to_numpy(),train[raw].to_numpy(),color='#2563eb',alpha=.18,lw=.7,label='Training: raw episode')
 ax.plot(train.episodes.to_numpy(),train[smooth].to_numpy(),color='#2563eb',lw=2,label='Training: trailing 16 episodes')
 ax.errorbar(x,summary[col].to_numpy(),yerr=summary[sd].to_numpy(),color='#dc2626',marker='o',capsize=3,label='Validation: mean +/- 1 SD (5 environment seeds)')
 ax.axvspan(384,512,color='green',alpha=.05)
 ax.set_ylabel(label+' (dimensionless)');ax.set_ylim(bottom=0);ax.grid(alpha=.2);ax.legend(fontsize=8)
axs[0].set_title('MARL PPO | 80 UEs | model seed 1 | reward = H_after')
axs[1].set_xlabel('Completed training episodes (green region: newest run)');axs[1].set_xlim(-5,517)
save(fig,'reward_curves')
fig,axs=plt.subplots(1,2,figsize=(12,4.5))
axs[0].plot(x,summary.mean_h_after.to_numpy(),'o-',color='#dc2626',label='Validation mean')
axs[0].axhline(summary.loc[128,'mean_h_after'],ls='--',color='gray',label='Previous best (128)')
axs[0].set_title('Validation mean (zoomed scale)');axs[0].set_ylabel('Mean H (dimensionless)')
for seed,g in v.groupby('seed'):axs[1].plot(g.episodes.to_numpy(),g.mean_h_after.to_numpy(),'o-',label=f'Environment seed {seed}')
axs[1].set_title('Paired validation by environment seed');axs[1].set_ylabel('Mean H (dimensionless)')
for ax in axs:ax.set_xlabel('Completed training episodes');ax.grid(alpha=.2);ax.legend(fontsize=8)
save(fig,'validation_curves')
fig,axs=plt.subplots(2,2,figsize=(11,8));pm=pol.groupby('episodes').mean(numeric_only=True)
for col in ['p0','p1','p2']:axs[0,0].plot(pm.index.to_numpy(),pm[col].to_numpy(),'o-',label=col.upper())
axs[0,0].set_ylabel('Mean action probability');axs[0,0].set_title('Logged validation action probabilities')
axs[0,1].plot(updates.episodes.to_numpy(),updates.entropy.to_numpy(),label='Training update entropy')
axs[0,1].axhline(np.log(3),color='gray',ls='--',label='Uniform 3-action entropy')
axs[0,1].set_ylabel('Entropy (nats)')
for col in ['explained_variance_before','explained_variance_after']:axs[1,0].plot(updates.episodes.to_numpy(),updates[col].to_numpy(),label=col.replace('explained_variance_','EV '))
axs[1,0].set_ylabel('Explained variance (dimensionless)')
axs[1,1].plot(updates.episodes.to_numpy(),updates.kl.to_numpy(),label='Mean per-UE KL')
axs[1,1].set_yscale('log');axs[1,1].set_ylabel('KL (nats, log scale)')
for ax in axs.flat:ax.set_xlabel('Completed training episodes');ax.grid(alpha=.2);ax.legend(fontsize=8)
save(fig,'policy_diagnostics')
(OUT/'analysis_manifest.json').write_text(json.dumps(dict(runs=[str(p) for p in RUNS],input_sha256=inputs,
 model_seed=1,validation_seeds=list(range(1011,1016)),experiment_id=ms[-1]['experiment_id'],
 statuses_checked=len(statuses),all_valid=True,training_episode_count=len(train),moving_average_window=16,
 validation_errorbars='sample SD, not CI',paired_interval='t interval, df=4; descriptive only, repeated validation/model selection',
 unsatisfied_definition='satisfaction < 1',command='python3 results/marl/analysis_new512_261103/analyze.py'),indent=2)+'\n')
print('SUMMARY\n',summary.round(6).to_string())
print('PAIRED\n',pd.DataFrame(paired).round(6).to_string(index=False))
print('BY SEED\n',v.pivot(index='seed',columns='episodes',values='mean_h_after').round(6).to_string())
print('POLICY\n',pm[['p0','p1','p2','within_state_probability_sd','entropy','argmax0','argmax1','argmax2']].round(6).to_string())
print('TRAIN BLOCKS\n',train.groupby(train.episode_id//64).mean_h_after.agg(['mean','std']).to_string())
print('CYCLES\n',cy[cy.episodes.isin([128,384,448,512])].groupby(['episodes','cycle']).mean(numeric_only=True).round(4).to_string())
print('VALID',len(statuses),'training',len(train))
