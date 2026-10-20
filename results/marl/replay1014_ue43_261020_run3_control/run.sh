#!/usr/bin/env bash
set -u
cd /home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44 || exit 1
control=results/marl/replay1014_ue43_261020_run3_control
exec >> "$control/runner.log" 2>&1
printf 'Started: '; date --iso-8601=seconds
printf '%s\n' "$$" > "$control/runner.pid"
python3 -u scripts/diagnose_marl_rtt.py \
 --source-episode results/marl/train80_b8_episodes256_261019/checkpoint_000256/validation_000003_seed1014 \
 --checkpoint results/marl/train80_b8_episodes256_261019/checkpoint_000256.pt \
 --binary build/marl-ue43-trace-v1/run-master \
 --output results/marl/replay1014_ue43_261020_run3 --trace
code=$?
printf '%s\n' "$code" > "$control/exit_code.txt"
printf 'Finished: '; date --iso-8601=seconds
printf 'Exit code: %s\n' "$code"
exit "$code"
