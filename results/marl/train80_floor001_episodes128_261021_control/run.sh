#!/usr/bin/env bash
set -u
cd /home/sota/workspace_ns-3.44/ns-allinone-3.44/ns-3.44 || exit 1
control=results/marl/train80_floor001_episodes128_261021_control
exec >> "$control/runner.log" 2>&1
printf 'Started: '; date --iso-8601=seconds
printf '%s\n' "$$" > "$control/runner.pid"
python3 -u scripts/run_marl.py \
 --mode train --episodes 128 \
 --terminals 80 --model-seed 1 \
 --workers 8 --parallel-validated \
 --config configs/marl/ppo_rollout8.json \
 --capacity configs/capacity/random_walk_all.json \
 --episode-timeout 14400 \
 --report-conn-timeout 0.2 \
 --binary build/qoe-floor001-v1/master \
 --output results/marl/train80_floor001_episodes128_261021
code=$?
printf '%s\n' "$code" > "$control/exit_code.txt"
printf 'Finished: '; date --iso-8601=seconds
printf 'Exit code: %s\n' "$code"
exit "$code"
