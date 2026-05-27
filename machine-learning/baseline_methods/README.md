# QoE ベース基地局割り当て baseline

## プロジェクトの説明

このプロジェクトは、複数の基地局と端末が存在する環境で、端末ごとの QoE（Quality of Experience）を考慮した基地局割り当てを検討するための Python baseline です。


現在の機械学習側の方針は以下です。

- 教師ラベル: ハンガリアン法が最終的に選んだ AP `assigned_ap`
- AP 数: 3 台固定
- 端末数: 基本 80 台
- 推論手法: ロジスティック回帰
- ns-3 側では、学習済み係数 JSON/CSV を読み込んで `--method=logistic` として利用する想定
- AP 容量超過時の補正は現時点では行わない

ロジスティック回帰で使用する特徴量は次の通りです。

```text
app_type
current_ap
num_users_ap0, num_users_ap1, num_users_ap2
rtt_ap0, rtt_ap1, rtt_ap2
estimated_tp_ap0, estimated_tp_ap1, estimated_tp_ap2
```

---

## ディレクトリの構造

```text
baseline_methods/
├── README.md
├── main.py
├── config/
│   ├── sim.json
│   ├── ap.json
│   └── app.json
├── simulation/
│   ├── config.py
│   ├── entities/
│   │   ├── ap.py
│   │   ├── term.py
│   │   └── term_ap.py
│   ├── services/
│   │   ├── create.py
│   │   ├── rand.py
│   │   └── cal.py
│   ├── algorithms/
│   │   ├── hungarian_kai.py
│   │   └── hungarian.py
│   ├── visualization/
│   │   ├── graph.py
│   │   └── output.py
│   └── results/
│       └── result.py
├── scripts/
│   ├── generate_logistic_teacher.py
│   └── train_logistic.py
├── data/
│   ├── raw/
│   │   └── logistic_teacher.csv
│   ├── models/
│   │   └── logistic_model.json
│   └── results/
└── tools/
    └── setup_gpu.py
```

### 主なファイル

- `main.py`
  - baseline シミュレーションのエントリーポイントです。
  - 端末・基地局の生成、ランダムな初期接続、アプリ割り当て、ハンガリアン法による接続先最適化、調和平均の出力を行います。

- `config/sim.json`
  - 端末数、基地局数、シミュレーション回数、初期 RTT、アプリ利用時間などの設定です。

- `config/app.json`
  - アプリ種別ごとの要求 TP / RTT を定義します。

- `simulation/services/cal.py`
  - AP ごとの RTT / TP 計算、端末満足度、調和平均の計算を行います。

- `simulation/algorithms/hungarian_kai.py`
  - 調和平均最大化を目的としたハンガリアン法ベースの割り当て処理を実装しています。

- `scripts/generate_logistic_teacher.py`
  - ハンガリアン法の結果を教師ラベルとして、ロジスティック回帰用の教師データ CSV を生成します。

- `scripts/train_logistic.py`
  - 教師データ CSV からロジスティック回帰モデルを学習し、ns-3 側で読み込める係数 JSON を出力します。

---

## 実行コマンド

### 1. 構文チェック

```bash
python -m py_compile scripts/generate_logistic_teacher.py scripts/train_logistic.py
```

成功した場合、何も表示されません。

---

### 2. baseline シミュレーション実行

```bash
python main.py
```

標準出力に、割り当て前後の調和平均やハンガリアン法で得られた接続先が表示されます。

注意: `config/sim.json` の `termNum` が 80 の場合、ハンガリアン法の計算時間が非常に長くなる可能性があります。

---

### 3. 小規模な教師データ生成テスト

まずは小さな端末数で動作確認します。

```bash
python scripts/generate_logistic_teacher.py \
  --num-runs 1 \
  --term-num 3 \
  --output /tmp/logistic_teacher_small.csv
```

確認例:

```bash
head -5 /tmp/logistic_teacher_small.csv
```

---

### 4. 本番用教師データ生成

80端末・100回分の教師データを生成する例です。

```bash
python scripts/generate_logistic_teacher.py \
  --num-runs 100 \
  --output data/raw/logistic_teacher_term80_runs100_seed001.csv
```

---

### 5. 教師ラベルの分布確認

`assigned_ap` が1種類しかない場合、ロジスティック回帰は学習できません。以下で分布を確認します。

```bash
python - <<'PY'
import csv
from collections import Counter

path = "data/raw/logistic_teacher_term80_runs100_seed001.csv"
cnt = Counter()
with open(path, newline="") as f:
    for row in csv.DictReader(f):
        cnt[row["assigned_ap"]] += 1
print(cnt)
PY
```

---

### 6. ロジスティック回帰の学習

```bash
python scripts/train_logistic.py \
  --input data/raw/logistic_teacher_term80_runs100_seed001.csv \
  --output data/models/logistic_term80_runs100_seed001.json
```

学習後、accuracy、precision、recall、f1-score が標準出力に表示されます。

---

## 出力ファイルの説明

### `data/raw/logistic_teacher_*.csv`

ハンガリアン法を教師として作成したロジスティック回帰用の教師データです。

主な列は以下です。

```text
run_id
seed
ue_id
app_type
current_ap
num_users_ap0
num_users_ap1
num_users_ap2
rtt_ap0
rtt_ap1
rtt_ap2
estimated_tp_ap0
estimated_tp_ap1
estimated_tp_ap2
assigned_ap
```

各列の意味:

- `run_id`
  - 教師データ生成時の試行番号です。

- `seed`
  - 乱数 seed です。

- `ue_id`
  - 端末 ID です。

- `app_type`
  - 端末が使用しているアプリ種別です。

- `current_ap`
  - ハンガリアン法適用前の接続先 AP です。

- `num_users_ap0/1/2`
  - 各 AP に接続している端末数です。

- `rtt_ap0/1/2`
  - 各 AP の RTT です。
  - ns-3 側では検査用端末で取得する RTT に対応させる想定です。

- `estimated_tp_ap0/1/2`
  - 各 AP ごとの予測 TP / 推定 TP です。
  - 現在の baseline では AP ごとの `ap.tp` を保存しています。
  - ns-3 側では、各基地局ごとの予測 TP を同じ列として入力する想定です。

- `assigned_ap`
  - ハンガリアン法が最終的に選んだ AP です。
  - ロジスティック回帰の教師ラベルです。

---

### `data/models/logistic_*.json`

ロジスティック回帰の学習済みモデルです。ns-3 C++ 側で読み込むことを想定し、係数を JSON として保存します。

主な項目:

```text
model_type
feature_columns
label_column
classes
scaler_mean
scaler_scale
coef
intercept
```

各項目の意味:

- `feature_columns`
  - 学習時に使用した特徴量の順番です。
  - ns-3 側でも必ずこの順番で入力ベクトルを作成してください。

- `classes`
  - 予測対象の AP ID です。

- `scaler_mean`
  - 標準化に使う平均値です。

- `scaler_scale`
  - 標準化に使う標準偏差です。

- `coef`
  - ロジスティック回帰の係数です。

- `intercept`
  - ロジスティック回帰の切片です。

ns-3 側では、以下の手順で推論します。

```text
1. feature_columns と同じ順番で特徴量 x を作る
2. x_scaled = (x - scaler_mean) / scaler_scale
3. logit = coef * x_scaled + intercept
4. logit が最大の class を assigned_ap として選ぶ
```

---

## 注意事項

- 現在の baseline は Python 側の教師データ作成・ロジスティック回帰学習までを対象としています。
- ns-3.44 の `APselection.cc` への `logistic` 割り当て関数実装は今後の作業です。
- `estimated_satisfaction_ap0/1/2` は現在使っていません。
- AP 数は 3 台固定、端末数は基本 80 台固定の前提です。
- AP 容量超過時の補正は現時点では行いません。
