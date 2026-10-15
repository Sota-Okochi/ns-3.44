from pathlib import Path
import json,subprocess,sys,csv
p=Path(__file__).resolve().parent
old=p.parent/'wifi_append_hoist_50_20261015/episode/smoke_000045_seed384'
new=p/'episode/smoke_000045_seed384'
result={'completed':(p/'episode/completion.json').exists()}
for name in ('observations.jsonl','transitions.jsonl','qoe.csv'):
 result[name+'_identical']=new.joinpath(name).exists() and old.joinpath(name).read_bytes()==new.joinpath(name).read_bytes()
for f in (p/'timing').glob('*/functions.csv'):
 out=subprocess.run([sys.executable,'scripts/summarize_perf.py',str(f.parent)],capture_output=True,text=True)
 (p/'summary.txt').write_text(out.stdout+'\n'+out.stderr)
 base=next((p.parent/'wifi_append_hoist_50_20261015/timing').glob('*/boundaries.csv'))
 def boundaries(path):
  with path.open() as stream: rows=list(csv.DictReader(stream))
  for row in rows:row.pop('interval_wall_ms',None)
  return rows
 result['boundary_event_counts_identical']=boundaries(base)==boundaries(f.parent/'boundaries.csv')
(p/'comparison.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result))

# Sampled per-band phase totals; not full-run exclusive percentages.
for f in (p/'timing').glob('*/functions.csv'):
 with f.open() as stream:
  rows=[r for r in csv.DictReader(stream) if r['snapshot']=='final' and r['function'].startswith('WifiRx.sampled.append.')]
 rows.sort(key=lambda r:float(r['total_wall_ms']),reverse=True)
 with (p/'append_parts.csv').open('w') as stream:
  if rows:
   w=csv.DictWriter(stream,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
