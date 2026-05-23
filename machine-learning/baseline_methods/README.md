# 卒業研究
## プロジェクト概要
- Wi-Fi 基地局と端末の接続割り当てをシミュレーションし、ハンガリアン法で端末満足度（RTT/スループット要求に対する達成度）の調和平均を最大化するプロジェクトです。  
- 自由空間伝搬モデル（FSPL）と、より減衰の大きい非自由空間モデルの双方を用意し、モデル別に求めた最適な接続先の差分や満足度を比較します。  
- 端末台数・基地局数・アプリ要求仕様を設定ファイルで切り替え、実行時に満足度や不一致数、実行時間をコンソールへ出力します。グラフ出力や結果ファイル保存はコメントを外すことで利用できます。

## ディレクトリ構成 / ファイル説明
- `main.py`：シミュレーションのエントリーポイント。端末/基地局生成、乱択による接続・アプリ割り当て、伝搬モデル計算、ハンガリアン法による最適化、結果出力を一括で実行。
- `config/`：設定 JSON を集約。
  - `config/sim.json`：シミュレーション条件（端末/基地局数、繰り返し回数、使用時間、初期 RTT など）。
  - `config/ap.json`：基地局ごとのデータ容量上限や価格などの初期値。
  - `config/app.json`：アプリ種別ごとの要求指標（RTT または TP）、必要値、利用時間分布。
- `simulation/config.py`：設定ファイルローダー。どのモジュールからも一貫して `config/` 以下の JSON を参照。
- `simulation/entities/`：データ構造群 (`Ap`, `Term`, `TERM_AP` など)。
- `simulation/services/`：作成・乱択・計算ロジック (`create.py`, `rand.py`, `cal.py`, `model.py`)。
- `simulation/algorithms/`：ハンガリアン法実装 (`hungarian_kai.py`, `hungarian.py`)。
- `simulation/visualization/`：グラフ描画とファイル出力 (`graph.py`, `output.py`)。
- `simulation/results/`：結果オブジェクトの定義。
- `tools/setup_gpu.py`：GPU/ CuPy 環境チェックとセットアップの補助スクリプト（任意）。

## 実行方法
- 前提：Python 3.10 以上を推奨。依存ライブラリは `numpy`（必須）、グラフ描画を行う場合は `matplotlib` を追加でインストールしてください。  
```
pip install numpy matplotlib
```
```
git clone https://github.com/Sota-Okochi/Bachelor_research.git
cd Bachelor_research
```
- シミュレーションパラメータを必要に応じて `config/sim.json`（端末数や繰り返し回数など）、`config/ap.json`（基地局性能）、`config/app.json`（アプリ要求値）で調整します。
- プロジェクトの実行
```
python main.py
```
- 標準出力に以下が表示されます。
  - 各シミュレーションでの調和平均（FSPL/非 FSPL）、モデル間の割り当て不一致数、各試行の実行時間
  - 最終的な調和平均配列や不一致数の配列
- グラフ出力や結果ファイル保存が必要な場合は、`main.py` 内の `gp.exportGraph` などのコメントを外して利用してください。
