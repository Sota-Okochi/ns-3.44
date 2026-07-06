# DQN 実行手順メモ

この README は、`--method=dqn` を実行して `OUTPUT/<端末数>/master_log_*_dqn_*.csv` を得るまでに必要な作業を記録するためのメモです。

前提として、ns-3 の configure / build は完了しており、`./ns3 run "master --method=random"` など既存手法が実行できる状態から始めます。

## 1. DQN 学習用の master_log を集める

DQN は `random` や `rulebase` などの実行ログから transition データを作って学習するため、まず学習用の `master_log` を収集します。

記載予定:

- `data/setting.json` の `rngSeed`、`terminals`、`numCycles` などを確認する
- 学習用 seed と評価用 seed を分ける
- `random` を複数 seed で実行する
- 必要に応じて `rulebase` も複数 seed で実行する
- 生成された `OUTPUT/<端末数>/master_log_<端末数>_<method>_<日時>.csv` を確認する

例:

```bash
./ns3 run "master --method=random"
./ns3 run "master --method=rulebase"
```

## 2. master_log から transition CSV を作る

収集した `master_log` を DQN 学習用の transition CSV に変換します。

記載予定:

- 使用スクリプトは `scripts/dqn/dataset/build_transitions.py`
- 入力は `OUTPUT/<端末数>/master_log_*.csv`
- 出力先は `episodes/dqn/transitions/`
- 基本は `--target-mode flag_only` を使う
- transition CSV に必要な列が揃っているか確認する

例:

```bash
python3 scripts/dqn/dataset/build_transitions.py \
  --input "OUTPUT/50/master_log_50_random_*.csv" "OUTPUT/50/master_log_50_rulebase_*.csv" \
  --output-dir episodes/dqn/transitions \
  --target-mode flag_only
```

## 3. DQN モデルを学習する

作成した transition CSV を使って、DQN モデルをオフライン学習します。

記載予定:

- 使用スクリプトは `scripts/dqn/train/train_dqn.py`
- 設定ファイルは `data/dqn/configs/dqn.yaml`
- 入力は `episodes/dqn/transitions/*.csv`
- 学習済みモデルは `models/dqn/checkpoints/` に保存される
- loss と metadata は `results/dqn/` に保存される

例:

```bash
python3 scripts/dqn/train/train_dqn.py \
  --input "episodes/dqn/transitions/*.csv" \
  --config data/dqn/configs/dqn.yaml
```

## 4. DQN 推論用の入力 master_log を用意する

現在の実装では、`--method=dqn` 実行時に action CSV を事前に読み込みます。そのため、DQN action CSV を作るための入力 `master_log` が必要です。

記載予定:

- 評価したい seed に `data/setting.json` の `rngSeed` を合わせる
- `no_switch` または `rulebase` などで一度実行し、推論用の状態ログを作る
- この `master_log` を次の DQN 推論スクリプトに渡す

例:

```bash
./ns3 run "master --method=no_switch"
```

## 5. 学習済み checkpoint から DQN action CSV を作る

推論用 `master_log` と学習済み checkpoint を使って、`--method=dqn` が読む action CSV を生成します。

記載予定:

- 使用スクリプトは `scripts/dqn/infer/infer_actions.py`
- 入力は推論用 `master_log`
- checkpoint は `models/dqn/checkpoints/*.pt`
- 出力先は `episodes/dqn/actions/`
- action CSV には `cycle_id`, `target_ue_id`, `selected_bs_id` が必要

例:

```bash
python3 scripts/dqn/infer/infer_actions.py \
  --input OUTPUT/50/master_log_50_no_switch_YYYYMMDD_HHMMSS.csv \
  --checkpoint models/dqn/checkpoints/学習済みモデル.pt \
  --output episodes/dqn/actions/actions_dqn_seed3.csv \
  --target-mode flag_only
```

## 6. `--method=dqn` で ns-3 を実行する

生成した action CSV を指定して DQN 手法を実行します。

記載予定:

- `--method=dqn` を指定する
- `--dqnActionCsv=<path>` で action CSV を明示する
- `data/setting.json` の `rngSeed` と action CSV の `seed` を一致させる
- action CSV がない場合、DQN は実行開始時に失敗する

例:

```bash
./ns3 run "master --method=dqn --dqnActionCsv=episodes/dqn/actions/actions_dqn_seed3.csv"
```

## 7. DQN 実行結果を確認する

DQN 実行後に、`OUTPUT/<端末数>/` 以下の `master_log` を確認します。

記載予定:

- `OUTPUT/<端末数>/master_log_<端末数>_dqn_<日時>.csv` が生成されたか確認する
- `harmonic_mean` が記録されているか確認する
- `target_ue_flag` が各 cycle で立っているか確認する
- `action_selected_bs_id` が出力されているか確認する
- `measurement_valid` が極端に 0 ばかりでないか確認する

確認する主な列:

```text
seed
cycle_id
ue_id
current_bs_id
satisfaction
harmonic_mean
num_unsatisfied_users
target_ue_flag
action_selected_bs_id
measurement_valid
```

## 8. baseline と比較する

DQN 単体では有効性を判断できないため、同じ seed・同じ設定で baseline と比較します。

記載予定:

- `no_switch`
- `random`
- `rulebase`
- `dqn`

を同じ条件で実行する。

比較する指標:

- 調和平均 `harmonic_mean`
- 不満足端末数 `num_unsatisfied_users`
- 平均 TP
- 平均 RTT
- 切り替え回数
- reward（transition 作成時に `harmonic_mean` 差分から再計算）
- seed 間のばらつき

例:

```bash
./ns3 run "master --method=no_switch"
./ns3 run "master --method=random"
./ns3 run "master --method=rulebase"
./ns3 run "master --method=dqn --dqnActionCsv=episodes/dqn/actions/actions_dqn_seed3.csv"
```

## 9. 注意事項を確認する

DQN 実行時に間違えやすい点をまとめます。

記載予定:

- `--method=dqn` は action CSV が事前に必要
- action CSV は `scripts/dqn/infer/infer_actions.py` で作る
- action CSV の `seed` と `data/setting.json` の `rngSeed` を合わせる
- `selected_bs_id` は 0-based
  - `0`: AP0 / 5G gNB
  - `1`: AP1 / Wi-Fi
  - `2`: AP2 / Wi-Fi
- ns-3 内部の AP 番号は 1-based に変換される
- `target_ue_id` は 1-based
- `OUTPUT/` を掃除してもよいが、`episodes/dqn/`, `models/dqn/`, `results/dqn/` は消さない
- `data/setting.json` の端末数と DQN 設定ファイルのシナリオ名・想定端末数がずれていないか確認する

## 未確認事項

この README は手順整理用の初稿です。ここに書いたコマンドの一連実行は、まだこの作業では未実行です。
