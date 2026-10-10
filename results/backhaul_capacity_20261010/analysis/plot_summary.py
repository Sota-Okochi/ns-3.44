"""Run after scripts/analyze_backhaul_suite.py; requires existing matplotlib installation."""
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT=Path(__file__).resolve().parent
rows=list(csv.DictReader((ROOT/'groups.csv').open()))
fig,axes=plt.subplots(2,2,figsize=(10,7),layout='constrained')
for ax,key,label in zip(axes.flat,
 ['ap0_rtt_ms','harmonic_mean','incoming_mbps','device_full_fraction'],
 ['AP0 monitor RTT (ms)','Mean cycle harmonic mean','AP0 downlink input (Mbps)','Device queue full samples (%)']):
 for n,marker in [(80,'o'),(100,'s')]:
  rs=[r for r in rows if int(r['terminals'])==n]
  scale=100 if key=='device_full_fraction' else 1
  ax.errorbar([int(r['rate_mbps']) for r in rs],
              [float(r[key])*scale for r in rs],
              yerr=[float(r[key+'_seed_sd'])*scale for r in rs],
              marker=marker,capsize=4,label=f'{n} UEs')
 ax.set(xlabel='PGW-CER capacity (Mbps)',ylabel=label,xticks=[80,120,160])
 ax.grid(alpha=.3);ax.legend()
 if key=='incoming_mbps':
  ax.plot([80,120,160],[80,120,160],'k--',label='Link capacity');ax.legend()
 if key=='device_full_fraction':ax.set_ylim(97,101)
fig.suptitle('Logistic: cycles 2–5, two seeds (error bars: sample SD across seeds)')
fig.savefig(ROOT/'capacity_comparison.png',dpi=180)
fig.savefig(ROOT/'capacity_comparison.pdf')
