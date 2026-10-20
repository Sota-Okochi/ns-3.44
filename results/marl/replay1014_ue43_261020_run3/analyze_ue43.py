"""Read-only replay analysis. Run: python3 <this file>. No random sampling."""
import csv,json
from pathlib import Path
from collections import Counter
root=Path(__file__).resolve().parent
p=root/'validation_000003_seed1014/simulation/80/marl_ppo'
rows=list(csv.DictReader((p/'ue43_path.csv').open()))
window=[r for r in rows if 33.6<=float(r['time'])<41.3]
counts=lambda rr:dict(Counter('|'.join(r[k] for k in ('event','node','interface_or_device','detail')) for r in rr))
flow=[r for r in csv.DictReader((p/'flow_per_cycle.csv').open()) if r['term_idx']=='44' and r['time_s']=='41.30']
arp=[r for r in window if r['node'] in ('3','50') and r['detail'].startswith('arp')]
replies={r['uid'] for r in arp if r['node']=='50' and r['event']=='MacTx' and r['detail']=='arp_reply'}
drops={r['uid'] for r in arp if r['node']=='3' and r['event']=='MacRxDrop' and r['detail']=='arp_reply'}
sink_bad=[];sink_window=0
for i,r in enumerate(csv.reader((p/'ue43_sink.csv').open()),1):
 try:
  t=float(r[0]);int(r[1]);int(r[2]);sink_window+=int(33.6<=t<41.3)
 except (ValueError,IndexError):sink_bad.append({'line':i,'row':r})
summary={'exact_equal':json.loads((root/'comparison.json').read_text())['exact_equal'],'window_s':[33.6,41.3],'ue43_flows':flow,'arp_events':counts(arp),'arp_reply_unique_uids':len(replies),'reply_uids_dropped_at_ap':len(replies&drops),'ip_path':counts([r for r in window if r['dst']=='10.1.2.46' and r['event'].startswith('ip')]),'arp_drop':counts([r for r in window if r['event']=='arp_drop']),'association_events':[r for r in rows if r['event'] in ('Assoc','DeAssoc')],'sink_window_rows_parseable':sink_window,'sink_malformed_rows':sink_bad}
(root/'analysis_summary.json').write_text(json.dumps(summary,indent=2))
with (root/'cycle4_arp_evidence.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(arp)
print(json.dumps(summary,indent=2))
