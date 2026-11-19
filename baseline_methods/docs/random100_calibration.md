# 100 台向け初期設定の校正（2026-11-19）

## 使用データ

`OUTPUT/100/random` から **ファイル名の日時が各 seed で最新**の
`master_log` を選択した。mtime はコピーで変化し得るため使用しない。
各 CSV は 100 UE × 5 サイクル、8 seed 合計 4,000 行。

| seed | ファイル名の日時 |
|---|---|
| 1001 | 20261117_022324 |
| 1002 | 20261117_022332 |
| 1003 | 20261117_022346 |
| 1004 | 20261117_022356 |
| 1005 | 20261117_022406 |
| 1006 | 20261117_022422 |
| 1007 | 20261117_022431 |
| 1008 | 20261117_022948 |

完全なファイル名と SHA-256 は `data/results/random100_calibration.json` に保存。
`measured_reward_log` は AP 別品質を含まないため校正には用いない。
ランダムな切り替え先を正解ラベルにはしない。教師は従来通りハンガリアン法。

## 集計方法と結果

- 各 seed・cycle・AP を 1 サンプルとして、AP ごとに 40 サンプル。
  RTT や接続台数を端末数分重複して重み付けしない。
- TP は現在接続中の端末のうち `tp_mbps > 0` の平均。
  `APselection::logistic_assignment()` の特徴量定義に合わせる。
  RTT 系アプリの 0 TP を混ぜない。反実仮想の推定満足度列は使用しない。
- RTT は `monitor_rtt_ap*`、負荷は `num_users_ap*`。
- 起動サイクルも含む全 5 サイクルを使用。最新ファイルに不整合があれば停止し、
  古いファイルへ自動的に差し替えない。

| AP | 平均接続台数 | 平均 TP (Mbps) | 平均 RTT (ms) | 観測接続台数範囲 |
|---|---:|---:|---:|---:|
| 0 | 33.600 | 4.8303 | 92.0833 | 23–43 |
| 1 | 33.675 | 2.3728 | 82.8750 | 25–44 |
| 2 | 32.725 | 0.9164 | 59.2000 | 23–45 |

## 変更した設定

`config/sim.json` に品質パラメータを集約し、`cal.py` と `create.py` が読む形にした。

```text
RTT = base_rtt_ms + rtt_alpha_ms_per_user * n + rttOffset
TP  = base_tp_mbps / (1 + tp_alpha_per_user * n) * tpScale
```

| パラメータ | AP0 | AP1 | AP2 |
|---|---:|---:|---:|
| base_rtt_ms | 89.316917 | 69.906596 | 48.248433 |
| rtt_alpha_ms_per_user | 0.082333 | 0.385105 | 0.334655 |
| base_tp_mbps | 8.061354 | 4.376952 | 1.818392 |
| tp_alpha_per_user（維持） | 0.020 | 0.025 | 0.030 |
| tpScale 範囲 | 0.810818–1.108195 | 0.780399–1.334863 | 0.611826–1.314523 |
| rttOffset 範囲 (ms) | −5.261338–4.313362 | −4.943952–2.945381 | −9.175940–8.676324 |

- RTT は非負の傾きを制約とする線形最小二乗。
- TP の負荷係数は従来値を維持し、基準 TP のみ最小二乗で合わせる。
  限られた負荷範囲からゼロ負荷 TP と負荷係数の両方を推定すると、
  観測範囲外を過大推定しやすいため。
- RTT 残差と TP の実測/予測比の 5–95 パーセンタイルをばらつき範囲とする。
  従来同様、run ごとに一様乱数で一度抽選し探索中は固定。
  これは信頼区間ではなく、極端値を除いた生成範囲の目安。
- `termNum`: 80 → 100。
- アプリ比率: 20/40/15/25% → 各 25%。最新 CSV の初回 800 UE は
  browser/video/voice/game = 202/202/213/183。
  標本の比率そのものではなく、確認した `master/config.cc` の各 25% に合わせた。
- 要求品質 3.0/8.0 Mbps、100/40 ms は維持。
  研究メモの browser 2.3 Mbps とは差があるが、現在の C++ 実装は 3.0 Mbps であり、
  今回 Python 側だけ要求値を変更しない。
- `initRTT=20` は維持。現行の `calLink()` はこれを使わず、AP 別基準 RTT が実効設定。
  利用時間や AP 容量、学習済み係数、ns-3 実験条件は変更しない。

## 制約・未確認

これは特徴量の尺度を合わせる簡易モデルの校正であり、QoE 改善の実証ではない。
RTT と台数の相関は弱く、係数を負荷の因果効果とみなせない。
AP 間・TP/RTT 間の相関、アプリごとの TP 差、切り替え直後の過渡状態や
RTT の極端値をこの独立な一様乱数モデルで完全には再現できない。
ハンガリアン探索では観測外の 0–100 台も評価するため、その予測精度は未確認。
同一 seed 内のサイクルも独立試行ではない。

100 台の教師生成・回帰モデル学習・ns-3 評価は **未実行**。
既存の `data/models` は上書きしていない。

## 実行記録（リポジトリルートから）

以下はすべて成功。追加依存はなく既存 NumPy を使用（1.26.4）。

```bash
python3 baseline_methods/scripts/calibrate_random100.py --output baseline_methods/data/results/random100_calibration.json
python3 -m unittest discover -s baseline_methods/tests -v
python3 baseline_methods/scripts/generate_logistic_teacher.py --num-runs 1 --base-seed 123 --term-num 3 --output /tmp/logistic_random100_smoke.csv
python3 baseline_methods/scripts/calibrate_random100.py --output /tmp/random100_calibration_check.json
cmp baseline_methods/data/results/random100_calibration.json /tmp/random100_calibration_check.json
```

4 テストで設定一致、同一 seed の 100 UE 特徴量再現性、0–100 台の負荷における
品質の有限性・正値性、要求品質の維持を確認。小規模教師生成も成功。
再集計スクリプトはレポートのみを書き、設定を自動上書きしない。

## 次の検証

1. 100 台の教師生成をまず 1 run 実行し、所要時間と割当ラベルの偏りを確認する。
2. 複数 run で学習し、run/seed を分離した評価データで特徴量範囲・分類性能を確認する。
3. 校正に使わない seed の ns-3 実験で調和平均、不満足端末数、非切り替え端末の悪化を比較する。
