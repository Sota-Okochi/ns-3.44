# logistic 教師生成設定の再校正（2026-11-21）

## 入力・変更範囲

ユーザー指定の 2026-11-20 の master_log 5 ファイルのみを使用。
seed 1001/1002/1004 は各 3 サイクル、1003/1005 は各 2 サイクル。
100 UE × 合計 13 サイクル = 1,300 行であり、現行 setting.json の
numCycles=5 と一致しない。欠けたサイクルは補完しない。
ファイル名、seed、行数、SHA-256、集計値、係数は
`data/results/random100_calibration_20261120.json` に保存した。
旧レポート・学習済みモデル・ユーザー変更済みの data/setting.json は変更していない。

`config/sim.json` の link_model、app_distribution、calibration_report を更新。
端末数 100、AP 数 3、要求品質 3/8 Mbps と 100/40 ms は維持。
アプリ比率は master/config.cc の実装に合わせて 20/40/15/25% に変更した。
ログ初回のアプリ件数は 96/209/82/113（合計 500）。
要求品質は contrib/kameda/model/server/APselection.h と一致する。
Python の appUseSec は ns-3 の cycleTimeSec と同義ではないため変更しない。

## 推定方法

従来同様、各 cycle・AP を 1 サンプルとする。TP は現在接続端末の
正の tp_mbps の平均、RTT は monitor_rtt_ap*、負荷は num_users_ap*。
これは APselection::logistic_assignment の入力定義に合わせている。
ランダム割当を教師ラベルにせず、教師は引き続きハンガリアン法で生成する。

TP の負荷係数 0.02/0.025/0.03 は維持して基準 TP を最小二乗推定。
RTT は傾き >= 0、切片 >= min_rtt_ms の制約付き最小二乗に変更。
切片が下限に達する場合、切片を固定して傾きを再推定する。
TP 比率と RTT 残差の 5–95 パーセンタイルを一様乱数の範囲とする。

| 設定 | AP0 | AP1 | AP2 |
|---|---:|---:|---:|
| base_tp_mbps | 6.629661 | 3.904484 | 1.643219 |
| base_rtt_ms | 0.010000 | 89.653831 | 43.318343 |
| rtt_alpha_ms_per_user | 6.116921 | 0 | 0.582210 |
| 平均観測 TP (Mbps) | 3.934507 | 2.113872 | 0.836535 |
| 平均観測 RTT (ms) | 196.105131 | 89.653831 | 62.128200 |

## 注意点

AP0 の RTT は中央値 97 ms、最大 884.8 ms。従来の非負傾きのみの
回帰では負の切片になり、最初の校正コマンドは ValueError で失敗した。
正値制約追加後に再実行して成功した。下限に達した切片 0.01 ms は
実測の無負荷 RTT を意味しない。負荷の因果効果も示さない。
AP0 の残差範囲は約 -142.84～445.33 ms と広い。独立一様乱数モデルは
低 RTT の下限クリップを発生させ得るし、観測中央値や裾を再現する保証はない。
これは暫定的な尺度合わせであり、そのまま評価済みモデルとは扱わない。
特に学習前に RTT 分布・下限クリップ率を確認すること。
サイクル数が異なるため seed の寄与も均等ではない。

## 再現コマンド（リポジトリルート）

```bash
python3 baseline_methods/scripts/calibrate_random100.py --files \
  OUTPUT/100/random/master_log_1001_20261120_222615.csv \
  OUTPUT/100/random/master_log_1002_20261120_222635.csv \
  OUTPUT/100/random/master_log_1003_20261120_222643.csv \
  OUTPUT/100/random/master_log_1004_20261120_222651.csv \
  OUTPUT/100/random/master_log_1005_20261120_222700.csv \
  --output baseline_methods/data/results/random100_calibration_20261120.json
python3 -m unittest discover -s baseline_methods/tests -v
python3 baseline_methods/scripts/generate_logistic_teacher.py --num-runs 1 --base-seed 123 --term-num 3 --output /tmp/logistic_random100_20261120_smoke.csv
```

上記は修正後すべて成功。5 テストで再集計の完全一致、設定一致、
特徴量の再現性、品質の正値性、要求値維持を確認。3 端末の教師生成も成功。
校正コマンドはレポートのみ出力する。config/sim.json の link_model には
そのレポートの link_model をコピーした。追加依存なし。
100 端末の本格教師生成・再学習・ns-3 評価は未実行。

次の検証: 生成品質分布の確認、seed 分離した再学習・分類評価、
未使用 seed での調和平均と非切り替え端末への影響の比較。
