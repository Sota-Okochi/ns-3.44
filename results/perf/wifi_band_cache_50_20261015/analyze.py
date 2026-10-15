from pathlib import Path
import json,subprocess,sys,csv
p=Path(__file__).resolve().parent
old=p.parent/'wifi_append_parts_50_20261015/episode/smoke_000045_seed384'
new=p/'episode/smoke_000045_seed384'
result={'completed':(p/'episode/completion.json').exists()}
for name in ('observations.jsonl','transitions.jsonl','qoe.csv'):
 result[name+'_identical']=new.joinpath(name).exists() and old.joinpath(name).read_bytes()==new.joinpath(name).read_bytes()
for f in (p/'timing').glob('*/functions.csv'):
 out=subprocess.run([sys.executable,'scripts/summarize_perf.py',str(f.parent)],capture_output=True,text=True)
 (p/'summary.txt').write_text(out.stdout+'\n'+out.stderr)
 base=next((p.parent/'wifi_append_parts_50_20261015/timing').glob('*/boundaries.csv'))
 def boundaries(path):
  with path.open() as stream: rows=list(csv.DictReader(stream))
  for row in rows:row.pop('interval_wall_ms',None)
  return rows
 result['boundary_event_counts_identical']=boundaries(base)==boundaries(f.parent/'boundaries.csv')
(p/'comparison.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result))

# Timings are comparable conditions but still a single measured A/B run.
def functions(path):
 with path.open() as stream:
  return {r['function']:float(r['total_wall_ms'])/1000 for r in csv.DictReader(stream) if r['snapshot']=='final'}
if result['completed']:
 oldf=functions(next((p.parent/'wifi_append_parts_50_20261015/timing').glob('*/functions.csv')))
 newf=functions(next((p/'timing').glob('*/functions.csv')))
 comparison={k:{'before_seconds':v,'after_seconds':newf[k], 'reduction_percent':100*(v-newf[k])/v} for k,v in oldf.items() if k in newf and v>0}
 (p/'timing_comparison.json').write_text(json.dumps(comparison,indent=2))
