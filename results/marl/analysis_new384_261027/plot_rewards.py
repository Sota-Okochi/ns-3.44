#!/usr/bin/env python3
"""Plot observed rewards from three contiguous resumed runs; no simulations."""
import json
from pathlib import Path
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
RUNS = [ROOT / 'results/marl' / name for name in (
    'train80_new_episodes128_261021',
    'train80_new_episodes256_261023',
    'train80_new_episodes384_261025',
)]
frames = [pd.read_csv(p / 'episodes.csv') for p in RUNS]
train = pd.concat(frames, ignore_index=True)
train = train[train.phase == 'train'].sort_values('episode_id').copy()
assert train.episode_id.tolist() == list(range(384)), 'Duplicate or missing training episodes'
assert ((train.return_sum - 4 * train.mean_h_after).abs() < 1e-9).all()
train['completed_episodes'] = train.episode_id + 1
train['return_ma16'] = train.return_sum.rolling(16, min_periods=16).mean()
train['reward_ma16'] = train.mean_h_after.rolling(16, min_periods=16).mean()
val_rows = []
for run in RUNS:
    for checkpoint in sorted(run.glob('checkpoint_*')):
        if not checkpoint.is_dir():
            continue
        count = int(checkpoint.name.split('_')[1])
        for file in sorted(checkpoint.glob('*/transitions.jsonl')):
            transitions = [json.loads(line) for line in file.read_text().splitlines()]
            assert len(transitions) == 4
            seed = int(file.parent.name.split('seed')[1])
            total = sum(t['reward'] for t in transitions)
            val_rows.append(dict(completed_episodes=count, environment_seed=seed,
                                 return_sum=total, mean_reward=total / 4))
val = pd.DataFrame(val_rows)
assert not val.duplicated(['completed_episodes', 'environment_seed']).any()
assert (val.groupby('completed_episodes').size() == 5).all()
summary = val.groupby('completed_episodes').agg(
    return_mean=('return_sum', 'mean'), return_sd=('return_sum', 'std'),
    reward_mean=('mean_reward', 'mean'), reward_sd=('mean_reward', 'std')).reset_index()
train.to_csv(OUT / 'reward_training.csv', index=False)
val.to_csv(OUT / 'reward_validation_by_seed.csv', index=False)
summary.to_csv(OUT / 'reward_validation_summary.csv', index=False)
fig, axes = plt.subplots(2, 1, figsize=(11, 9), sharex=True)
for ax, raw, smooth, vm, vs, ylabel in (
    (axes[0], 'return_sum', 'return_ma16', 'return_mean', 'return_sd',
     'Episode return: sum of 4 rewards (dimensionless)'),
    (axes[1], 'mean_h_after', 'reward_ma16', 'reward_mean', 'reward_sd',
     'Mean step reward: H, cycles 2-5 (dimensionless)'),
):
    ax.plot(train.completed_episodes.to_numpy(), train[raw].to_numpy(), color='#3b82f6', alpha=.22,
            linewidth=.8, label='Training: individual episode')
    ax.plot(train.completed_episodes.to_numpy(), train[smooth].to_numpy(), color='#1d4ed8',
            linewidth=2, label='Training: trailing 16 episodes')
    ax.errorbar(summary.completed_episodes.to_numpy(), summary[vm].to_numpy(), yerr=summary[vs].to_numpy(),
                color='#dc2626', marker='o', linewidth=1.8, capsize=4,
                label='Validation: mean +/- 1 SD across 5 environment seeds')
    for boundary in (128, 256):
        ax.axvline(boundary, color='gray', linestyle=':', linewidth=1)
    ax.set_ylabel(ylabel)
    ax.set_ylim(bottom=0)
    ax.grid(alpha=.2)
    ax.legend(loc='upper left', fontsize=8)
axes[0].set_title('MARL PPO reward curves | 80 terminals | model seed 1')
axes[1].set_xlabel('Completed training episodes (dotted lines: resume boundaries)')
axes[1].set_xlim(0, 384)
fig.text(.5, .01, 'Reward = H_after; return = sum over cycles 2-5. Validation is unsmoothed. '
         'SD is not a confidence interval.', ha='center', fontsize=8)
fig.tight_layout(rect=(0, .025, 1, 1))
for ext in ('png', 'pdf'):
    fig.savefig(OUT / f'reward_curves.{ext}', dpi=180)
plt.close(fig)
(OUT / 'plot_config.json').write_text(json.dumps(dict(
    runs=[str(p) for p in RUNS], model_seed=1, environment_seeds=[1011,1012,1013,1014,1015],
    rolling_window=16, reward='H_after', return_definition='sum of 4 step rewards',
    validation_band='sample standard deviation over 5 environment seeds',
    command='python3 results/marl/analysis_new384_261027/plot_rewards.py'), indent=2)+'\n')
print(summary.to_string(index=False))
print(OUT / 'reward_curves.png')
