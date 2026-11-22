# 100台 MARL PPO 転移学習（2026-11-22）

## 合意した条件

80台のvalidation最良モデル（832 episodes）からActor/Criticの重みのみ移す。
100台で新規1,000 episodes、10 episodes/更新、10並列。計100 PPO更新。
初期と50 episodesごとに固定seed 1011〜1015でvalidationする。
validationは21回、105 episodes、学習と合計1,105実行（smoke/testを除く）。
初期validationはランダム初期値ではなく、転移直後の100台での性能を測る。
改善は未確認。初期モデルを含め、100台validationのH2〜H5平均でbestを選ぶ。

- `configs/marl/ppo_rollout10.json`: 80台の最終run manifestの全PPO設定を固定し、rolloutだけ8→10。
- `configs/marl/transfer100_setting.json`: 従来の`results/marl/resume384_repaired/setting.json`のコピー。
  元ファイルのterminalsも100だが、80台実験はCLIで80に上書きされていた。
  各episodeでCLIの端末数・環境seed・5周期を設定コピーへ適用する。
- 容量条件は従来の`configs/capacity/random_walk_all.json`。
- clip=0.1、epsilon=0.01、Actor/Critic lr=1e-4/3e-4、epochs=4、minibatch=16を維持。
  40共同遷移を16/16/8のminibatchで処理する。端末数・rollout数増加は同じ学習量を意味しない。
- 状態・行動・報酬・観測/計測規約・C++・ns-3.44バイナリは変更しない。
- 学習環境seedは1〜1000（新しいepisode番号0から）、testは1001〜1010。
  同じseedを80台で使用済みでも、100台で再収集し、80台の軌跡は使用しない。

## 転移と再開の区別

`--init-checkpoint`はfresh trainまたはsmoke専用。`--resume`、`--checkpoint`、
`--recover-run`と併用できない。ローカルの信頼できるcheckpointのみ指定する。
schemaとconfig（rollout_episodes以外すべて）の一致を確認して重みをロードする。
optimizer、学習進捗、乱数状態、best_scoreは引き継がない。
転移元パス・SHA256・端末数・seed・進捗・config・環境設定・experiment_idを
manifestと各checkpointの`transfer`に保存する。以後のresume/recoveryにも保持する。

通常resumeの厳密な実験identityチェックは維持する。
**今回のPythonソース変更により、変更前に作成したrunは通常resume/recoveryで拒否される。**
過去runをそのまま継続する場合は当時のソース/実行環境が必要。チェックを迂回しない。
新機能で作成した100台runは、同じコード・設定・実行環境で通常resumeできる。

## 本学習コマンド（未実行）

リポジトリルートで実行。出力先は必ず未使用のディレクトリにする。
`--parallel-validated`は資源検証の完了を利用者が確認するフラグであり、自動検証ではない。
**100台×10並列の資源検証後に実行する。** 既存80台×8並列の成功で代用しない。

```bash
python3 -u scripts/run_marl.py \
  --mode train --episodes 1000 --terminals 100 --model-seed 1 \
  --workers 10 --parallel-validated \
  --setting configs/marl/transfer100_setting.json \
  --config configs/marl/ppo_rollout10.json \
  --capacity configs/capacity/random_walk_all.json \
  --validation-interval 50 \
  --episode-timeout 14400 --report-conn-timeout 0.2 \
  --binary build/qoe-floor001-v1/master \
  --init-checkpoint results/marl/train80_new_episodes1024_261110/best.pt \
  --output results/marl/train100_transfer80_b10_episodes1000_261122
```

事前確認は同じ引数で`--mode smoke --episodes 1 --workers 1`へ変更し、
`--parallel-validated`を外し、別の出力先にする。smokeは更新・validationを行わない。
その後、監視下で2→8→10並列のsmokeを別出力先で実施する。
10並列の例は`--mode smoke --episodes 10 --workers 10 --parallel-validated`。
各statusのvalid/terminal/4遷移/終了コード、RSS、swap、既存ジョブへの影響を確認する。
smoke軌跡は学習runへ流用しない。

通常の学習継続は`--init-checkpoint`を外し、`--resume --checkpoint <100台run>/latest.pt`に置換、
同じconfigと`--validation-interval 50`を指定して新しい出力先を使う。
`--episodes`は100台での累計予算。50ごとに保存済みモデルを評価でき、1,000完了まで待つ必要はない。
収集途中の中断は代わりに`--recover-run <停止済み100台run>`で復旧する。
これは同一方策・条件の成功済みキャッシュのみ再利用する既存の仕組み。

## 検証コマンド

```bash
python3 -m unittest discover -s tests -p 'test_marl*.py' -v
python3 -m unittest discover -s tests -p 'test_reactive*.py' -v
python3 -m py_compile rl/marl/core.py scripts/run_marl.py tests/test_marl_transfer.py
git diff --check
```

追加テストは重み一致、optimizer/進捗/乱数の非継承、100台10 episodesの実PPO更新、
設定不一致拒否、モック収集による1,000+105実行のスケジュール、初期best選択、
転移メタデータ保存、通常resume/recovery、端末数変更拒否を確認する。
モック収集はns-3実測や10並列の資源検証ではない。
実ns-3のsmoke、本学習、性能改善、所要時間は未確認。

### 今回の実行結果

- 上記unittest: 20件成功（初回は追加テストの模擬観測seed不一致で1件失敗、fixture修正後に全件成功）。
- py_compile、git diff --check: 成功。
- 既存reactive DQN回帰テスト: 5件成功。
- 実際の転移元best.ptを`Learner.initialize_weights`で読み取る確認も実施（保存/更新/シミュレーションなし）。
  80台・832 episodes・104更新、SHA256は
  `b211cb732ff7681466a1645836636ffa845ee09cc0fa1968bfec72efde65e8fc`。
  転移後は更新回数0、Actor/Critic両optimizerのstate件数0。
- 新settingと旧settingのJSON一致を確認。元モデル・結果・バイナリは変更していない。
