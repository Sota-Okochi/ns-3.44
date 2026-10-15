"""Read-only watcher: summarize this profiling run after success/failure."""
from pathlib import Path
import subprocess
import sys
import time
import json
root=Path(__file__).resolve().parent
for _ in range(250):
    complete=root/'episode/completion.json'
    interrupted=root/'episode/interrupted.json'
    if complete.exists() or interrupted.exists():
        for p in (root/'timing').glob('*/functions.csv'):
            r=subprocess.run([sys.executable,'scripts/summarize_perf.py',str(p.parent)],capture_output=True,text=True)
            (root/'summary.txt').write_text(r.stdout+'\n'+r.stderr)
        (root/'measurement_status.json').write_text(json.dumps({'state':'complete' if complete.exists() else 'interrupted','record':str(complete if complete.exists() else interrupted)},indent=2))
        break
    time.sleep(60)
else:
    (root/'measurement_status.json').write_text(json.dumps({'state':'watcher_timeout_no_completion_claim'}))
