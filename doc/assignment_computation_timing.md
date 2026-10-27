# Assignment computation time v2

## 定義と旧計測の監査

観測済みデータから全端末の接続先ベクトルを確定する計算時間を、意思決定
サイクルごとに記録する。特徴量生成・推論/探索・行動検証・ベクトル生成を含む。
通信待ち、通信のJSONシリアライズ、ログ出力とその専用計算、教師データ生成、
ハンドオーバー、モデルロード、学習更新、PPO critic を除く。

既存 `assignment_compute_ms` は `APselection::tmain` 内の関数全体を測っていた。
random/logistic/DQN はログとハンドオーバー呼び出しを含み、logistic は
`WriteCentralizedTeacherLogRows` の探索も含んでいた。DQN はTCP往復待ちを含み、
MARLはタイマーより前にreturnしていた。リポジトリ検索では4手法共通の旧時間集計処理は
見つからなかった。ユーザーが過去に使用した外部スクリプト・ノートブックは未確認。
旧master CSVは同じサイクル時間を端末ごとに繰り返すため、端末行の合計は禁止。

## 実装

- `AssignmentComputeTimer`: steady_clockの計算区間だけを加算。ログ・通信前で停止。
- random: RNG初期化・全端末サンプリング・ベクトル保存。
- logistic: AP負荷/平均TP・特徴量・標準化・クラス選択・ベクトル保存。
  推論後の推定H、教師データ探索、ログ、切り替えは区間外。
- centralized_dqn: C++要求値生成・状態特徴・安全候補探索・検証・ベクトル更新
  ＋Python状態変換/標準化・masked Q推論/選択。サイクル内全stepを合算。
  TCP接続/send/recv待ちは除外し、Python計算時間だけ応答に付加する。
  行動デコード・検証は含むが、計測用メタデータの解析自体は除外。
  Pythonの `--eval-only` が必要。古い応答/学習モード/失敗を有効なゼロとして扱わない。
- marl_ppo: C++観測値単位変換・Python `observation`・actor/サンプリング・配列検証/生成
  ＋C++側行動デコード/検証・全端末ベクトル保存。criticは計測区間外。
  observationが特徴量と同時に作るraw補助データの生成は同一関数内のため含むが、
  JSONL/CSVへのシリアライズ・書き込みは含まない。
  `assignment_timing_version=2` リクエストに `TIMING_V2 <ms>` を応答する。
  simulatorとrunnerはセットで更新する。旧テスト/旧要求への応答形式は維持。

必要な探索と、ログのためだけの探索を区別する。DQNの安全判定用推定Hは必要な
計算なので除外しない。従来の行動/RNG/ハンドオーバー呼び出し順序は維持する。
共通の観測前処理（測定RTT集約等）は観測の準備とし、その後の手法依存特徴を測る。

時計はCPU使用時間ではなく計算区間の経過時間なので、OSスケジューリングの影響は
残る。C++とPythonはそれぞれの経過時間を加算し、絶対時刻を比較しない。
専用比較runnerはCPU・Torch 1 thread・4手法逐次実行。初回推論の遅延は隠さず
cycle 1に含む（別途warmupを実行していない）。GPU DQNは同期して測る。

## ログと集計

各実行の出力ディレクトリに `assignment_timing_<seed>_<timestamp>.csv`。

```
timing_version,seed,cycle_id,method,effective_method,num_terminals,status,
cpp_compute_ms,python_compute_ms,assignment_compute_ms
```

1サイクル1行。`assignment_compute_ms = cpp_compute_ms + python_compute_ms`。
最終観測は `status=no_decision`、時間欄は空欄。欠測を0としない。
`fallback` / `request_failed` / `invalid_remote_timing` も合計欄を空欄にする。
legacy master CSVの列名は維持するが、4手法の比較では必ずv2専用CSVを使う。
未対応の他手法のmaster列は旧定義のまま。

`logistic_bootstrap` はDQN推論ではない。専用集計器はbootstrapを黙って含めず
エラーにする。今回の4サイクル比較runnerはbootstrap=0を明示して記録する。
既存のbootstrapあり手法を評価する場合は、bootstrapを別集計し、比較対象cycleも
揃える必要がある（本runnerはその比較を実装していない）。

```bash
python3 -B scripts/summarize_assignment_timing.py RESULTS_DIRECTORY \
  --output RESULTS_DIRECTORY/recomputed_summary.csv
```

重複行、旧形式、非有限値、失敗、不揃いseed/端末数/サイクル、bootstrapを拒否。
4手法×4意思決定サイクルのmean/median/sample stdev/min/maxをmsで保存。
サイクル間SDはseed間のばらつきではない。1 seedだけで一般的優劣を断定しない。

## 同一seedでの実行

既存モデルを変更せず、明示されたチェックポイントで評価のみ実施。
出力先は新規ディレクトリ必須。設定、容量イベント、seed、実行コマンド、モデル/
ソース/バイナリ/共有ライブラリhash、CPU/Python/Torch情報を保存する。
MARLの標準評価用seed選択は維持し、単独runnerには `--eval-seed` を追加した。

```bash
python3 -B scripts/build_marl_isolated.py \
  --output build/assignment-timing-v2-validated \
  --base-libraries build/qoe-floor001-v1/lib --jobs 2

python3 -B scripts/run_assignment_timing.py \
  --binary build/assignment-timing-v2-validated/master \
  --seed 1001 --terminals 80 \
  --setting data/setting.json \
  --capacity configs/capacity/random_walk_all.json \
  --dqn-checkpoint models/centralized_factorized_v2_epoch10.pt \
  --marl-checkpoint results/marl/train80_new_episodes384_261025/best.pt \
  --max-switches 4 --safety-threshold 0.001 \
  --output results/assignment_timing/seed1001_v2
```

この例はbootstrapなしfactorized DQNの条件であり、過去のproposed_B等の設定を
自動復元するものではない。比較条件はmanifestを参照すること。
MARLのbest.ptは保存ディレクトリの最終episodeとは限らないので、モデルhashで識別する。
同一seedでも各手法の割当に応じて後続の観測データは変わる。またMARLは独自の
RTT/QoE測定経路を持つ。これは同じ数値入力でのmicrobenchmarkではなく、各手法が
同じ外部条件でオンライン意思決定する際の計算時間比較である。

## 検証

```bash
python3 -B -m unittest discover -s tests -p 'test_assignment_timing.py'
python3 -B -m unittest discover -s tests -p 'test_marl_*.py'
python3 -B tests/assignment_timing_integration.py \
  --lib-dir build/assignment-timing-v2-validated/lib
git diff --check
```

単体テストはfake clockで特徴2ms＋actor3ms、ログ90ms・critic70msの除外、
DQN区間合算、行動不変、最終観測、集計拒否条件を検証する。
結合テストは人工的な観測をC++の実APselectionに入力し、TCPサーバーの250ms待機と
ハンドオーバーcallbackの250ms待機を除外できることを4手法で確認する。
**結合テストの観測・モデルは検証用であり、研究用ネットワーク実験結果ではない。**

### 2026-10-27 実施状況

- 分離ビルド `build/assignment-timing-v2-validated/master`: 成功。
- 新規単体テスト4件、既存MARLテスト18件: 成功。
- C++/Python TCP結合テスト: 成功。最初のsandbox実行はsocket作成権限で失敗し、
  承認後に再実行して成功。上記の人工待機除外を確認。
- `git diff --check`: 成功。
- 上記例の条件で `results/assignment_timing/seed1001_v2` の本計測を開始。
  初回nohup起動は継続されず、プロセス不在と未開始を確認した後、管理セッションで
  起動し直した。`status.json` に `running/current_method=random`、シミュレータ開始を確認。
  **開始時点では4手法の結果は未確定。** 完了判定は `status.json` の `complete` と
  `summary.csv` の両方を確認する。`failed` の場合はその理由と各console.logを確認。
- 実ネットワーク条件での旧版との行動/ログ同一性比較、多seedでの時間比較は未実施。

## 1手法につき1ターミナルで実行

`run_assignment_timing.py --method random|logistic|centralized_dqn|marl_ppo`
で単独実行できる（省略時は従来どおり `all`）。DQN/MARLには `--port auto` を
指定可能で、サーバー起動・接続・終了まで同じコマンドが管理する。
MARLは内部TCPServerのport=0による自動割当、DQNは `--port auto` をサーバーへ渡す。
単独実行はその手法に必要なチェックポイントのみ要求し、完了後の `summary.csv` も
その1手法を集計する。既存の4手法一括モードの厳密な完了検証は維持する。

```bash
# リポジトリルートで実行。出力先は毎回新規ディレクトリにする。
python3 -B scripts/run_assignment_timing.py --method random \
  --binary build/assignment-timing-v2-validated/master --seed 1001 --terminals 80 \
  --output results/assignment_timing/manual_seed1001/random

python3 -B scripts/run_assignment_timing.py --method logistic \
  --binary build/assignment-timing-v2-validated/master --seed 1001 --terminals 80 \
  --output results/assignment_timing/manual_seed1001/logistic

python3 -B scripts/run_assignment_timing.py --method centralized_dqn --port auto \
  --binary build/assignment-timing-v2-validated/master --seed 1001 --terminals 80 \
  --dqn-checkpoint models/centralized_factorized_v2_epoch10.pt \
  --max-switches 4 --safety-threshold 0.001 \
  --output results/assignment_timing/manual_seed1001/centralized_dqn

python3 -B scripts/run_assignment_timing.py --method marl_ppo --port auto \
  --binary build/assignment-timing-v2-validated/master --seed 1001 --terminals 80 \
  --marl-checkpoint results/marl/train80_new_episodes384_261025/best.pt \
  --output results/assignment_timing/manual_seed1001/marl_ppo
```

デフォルト設定は `data/setting.json`、容量設定は `configs/capacity/random_walk_all.json`。
各手法の実行間で変更しない。時間比較ではターミナルを分けても同時実行せず、
他手法の終了後に次を開始する。既存の一括実行をこの変更で停止することはない。
全手法完了後は以下で合算せず手法別に比較できる。

```bash
python3 -B scripts/summarize_assignment_timing.py \
  results/assignment_timing/manual_seed1001 \
  --output results/assignment_timing/manual_seed1001/comparison.csv
```

単独CSVを再集計する際も `summarize_assignment_timing.py --method random` 等を指定する。
追加検証：単独4手法の集計、random/logisticでRLチェックポイント不要・指定1手法だけを
起動するmock試験。新たなネットワークシミュレーションはこのCLI変更では起動しない。
