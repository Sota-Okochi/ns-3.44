# 3リンク同時変動と反応型DQNの実行

## 実装済みの範囲

2026-10-10。新method `reactive_dqn` は中央制御の標準DQN。
1周期あたりSTOPまたは1 UEの移動。推定Hフィルタ・logistic bootstrapなし。
現在の全UE/AP観測のみを送信し、未来容量やイベント周期は入力しない。
学習はPyTorch（既存依存）を利用し、新しい外部依存は追加していない。

研究用 `scripts/run_capacity_scenario.py` のデフォルトは
`configs/capacity/fixed_all.json` へ変更した。master自体の既存デフォルトは維持。

| 周期 | AP0 | AP1 | AP2 |
|---|---:|---:|---:|
| 1 | 80 | 40 | 20 |
| 2,3 | 40 | 60 | 10 |
| 4,5 | 80 | 40 | 40 |

単位Mbps、両方向。最後は元の状態への回復ではなく再変動。
旧AP0単独設定は `--scenario configs/capacity/fixed_ap0.json` で利用可能。

## ビルドと容量変動の確認

```bash
./ns3 build master -j 4
python3 scripts/run_capacity_scenario.py --no-build
```

## DQNの最初の動作確認（80台、seed1001）

```bash
python3 rl/reactive_train.py \
  --scenario configs/capacity/fixed_all.json \
  --episodes 2 --seed-start 1001 --fixed-seed --agent-seed 1 \
  --learning-starts 2 --batch-size 2 \
  --output results/reactive_dqn/smoke_80_seed1001
```

これは小batchで勾配更新経路を確認するコマンド。性能評価用の学習条件ではない。
既存出力ディレクトリへの上書きを拒否するので、再実行は別output名にする。
ビルドは自動実行しない。内部でlocalhostのTCPサーバを立ててns-3を起動する。

## 1台ずつ動かすための長い固定シナリオで学習

`configs/capacity/train_fixed_all.json` は80台・40周期。
初期10周期、低下/増加状態15周期、再変動後15周期（変更cycle11,26）。
帯域は上表と同じ。周期長9.5秒・warmup3秒など他の条件は維持する。
5周期では1episode4遷移しか得られず、大規模な再配置を評価しにくいため別設定を用意した。

```bash
python3 rl/reactive_train.py \
  --scenario configs/capacity/train_fixed_all.json \
  --episodes 300 --seed-start 1001 --fixed-seed --agent-seed 1 \
  --output results/reactive_dqn/train_fixed_seed1001
```

1episode39遷移、300episodeで11700遷移。1000遷移まではreplay蓄積のみ。
300episodeは動作予算の例であり、収束や性能を保証する値ではない。
実行時間をpilotで測ってから長時間学習を開始すること。
`--fixed-seed`を外すとns-3 seedは1001,1002,...となる。
固定seed/固定容量系列での学習は特定シナリオ向けであり、未知変動への汎化を主張しない。

## 学習済みモデルを更新せず評価

```bash
python3 rl/reactive_train.py \
  --scenario configs/capacity/train_fixed_all.json \
  --episodes 1 --seed-start 1001 --fixed-seed \
  --checkpoint results/reactive_dqn/train_fixed_seed1001/model.pt \
  --eval-only --output results/reactive_dqn/eval_seed1001
```

epsilon=0、replay/optimizer更新なし。未知seed評価はseed-startを未使用値に変更する。
同じ容量系列をno_switch・1台制約ルール等にも与えて比較する。
legacy logisticは欠測停止と切り替え予算が異なるため、公平性の制約を併記する。

## 出力と時間対応

- manifest.json：設定、状態schema、PyTorch version、seed、主要ソースhash
- model.pt：学習後の重み、target、optimizer、step数、乱数状態、引数
- episode_xxxxx/setting.json、planned_events.csv、command.json
- console.log、status.json：ns-3出力・終了状態
- transitions.jsonl：実測次周期H、行動、報酬、状態、次状態、mask、終端
- learning.json：step数、勾配更新数、loss
- simulation/：masterログと容量イベントログ

周期tの行動を周期t+1で実測評価。報酬はH(t+1)-lambda*実切り替え数/N、lambda既定1。
次周期の実割当が要求割当と一致することを検証する。最後の周期では新規行動を出さない。
40周期なら39遷移。正常終了していないepisodeの遷移はreplayへ入れず、学習を停止する。
容量変動によるH変化も報酬に含まれるので、行動だけの因果効果と解釈しない。

## 設計書からの初期実装上の差分・制約

- schemaは `reactive_v1_log`。15*N+30入力、3*N+1出力。80台なら1230/241。
- 学習データfit型scalerの代わりに、固定log1p変換を採用。countはNで正規化。
  テスト情報漏洩はないが、最適な正規化と主張しない。
- 1episodeを重み固定で収集し、正常終了後、遷移1件につき最大1更新する。
  失敗episodeの混入防止のため、設計案の各周期直後更新から変更した。
- checkpoint読み込みは評価専用。replayを保存しておらず学習再開は未対応。
- hidden256x2、Adam1e-4、Huber、gamma0.99、buffer100000、batch64、
  target500更新、epsilonは10000実行動stepで1->0.05、gradient norm10。
- 現行legacy QoEのTP欠測0.1/RTT欠測代用を維持。共通欠測処理は未実装。
- 完全な物理ハンドオーバではなく既存handover実装をそのまま使う。
- 80台モデルを異なる端末数に転用不可。モデル次元不一致を拒否。
- ランダムモードは既存生成器と同様1AP低下/回復のみ。3APの同時ランダム生成は未対応。

## 検証記録

- ビルド：承認付きビルド成功。通常sandboxではccache書き込みで失敗。
- `python3 -m unittest discover -s tests -p test_reactive_dqn.py`：5件成功。
  終端、mask、target値、評価時更新なし、失敗episode除外を検証。
- localhost socketはsandboxで拒否されたため、承認付きで結合試験を実行。
- 結合試験結果と未確認事項は以下に追記する。

### 結合試験結果（実施済み）

80台設定を一時コピーし端末数のみ3台へ変更、5周期、seed1001、全3リンク同時変動。

```bash
python3 rl/reactive_train.py --scenario /tmp/reactive-smoke.json --episodes 2 --fixed-seed --learning-starts 2 --batch-size 2 --output /tmp/reactive-smoke-training-approved
python3 rl/reactive_train.py --scenario /tmp/reactive-smoke.json --episodes 1 --fixed-seed --checkpoint /tmp/reactive-smoke-training-approved/model.pt --eval-only --output /tmp/reactive-smoke-eval
python3 scripts/check_reactive_run.py /tmp/reactive-smoke-training-approved
python3 scripts/check_reactive_run.py /tmp/reactive-smoke-eval
```

学習：2episode正常終了、実測8遷移、7更新、model.pt保存。
評価：1episode正常終了、実測4遷移、更新0。
masterとのH照合、切り替え最大1台、最終周期切り替え0、報酬再計算、完了を検証。
全3リンク両方向のイベント18行（初期6+変更12）も確認。
80台学習、本格的な収束・QoE改善・未知seed性能は未実行/未確認。
新規PythonテストはDQN5件と容量6件が成功、git diff --check成功。

実行後の検証は `python3 scripts/check_reactive_run.py <学習または評価出力ディレクトリ>`。
