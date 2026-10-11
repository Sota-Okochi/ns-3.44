# バックホール容量のランダムウォーク／固定イベント切り替え設計

作成日: 2026-10-11  
状態: **2026-10-11 実装。検証記録は末尾の「実装・検証記録」を参照。**

## 1. 目的と対象範囲

ns-3.44 の基地局別バックホール（本書ではバックボーンの対象リンク）について、
通信容量だけを外生的に時間変動させ、基地局選択手法の適応性を比較する。
PointToPoint の `DataRate` を変更し、`Delay`、移動条件、無線設定は変更しない。
容量変動で生じる送信時間・キュー待ち・TCP の変化はシミュレータに任せる。
キュー、通信アプリ、TCP 接続、送信中パケットはリセットしない。

共有バックホール等で利用できるサービス容量の変動を抽象化したモデルであり、
背景トラフィックそのものや実測に基づく物理回線変動を再現したとは主張しない。
評価の中心は満足度の調和平均とし、対象外端末への影響も確認する。

## 2. 現行実装と互換性

関連ファイル:

- `scripts/run_capacity_scenario.py`: シナリオ読み込み、イベント生成・保存、実行。
- `configs/capacity/fixed_all.json`: 既定の固定イベント。
- `configs/capacity/random_ap0.json`: 既存の単発低下・復旧ランダム設定。
- `master/capacity-schedule.cc`: CSV 読み込み、サイクル開始時の適用・ログ。
- `scripts/check_capacity_scenario.py`、`tests/test_capacity_scenario.py`: 検証。
- [現行実行手順](capacity_scenario_runner.md)。

| モード | 意味 | 方針 |
|---|---|---|
| `fixed` | 指定されたサイクルに指定容量へ変更 | 維持 |
| `constant` | 実行中に容量変更しない | 維持 |
| `random` | 1リンクの容量低下・復旧を1組ランダム生成 | 維持 |
| `random_walk` | 全3リンクを毎サイクル、前回値から増減 | 新規追加 |

**固定設定 `fixed` は「容量一定」ではなく「事前に決めた変動イベント」の意味。**
現行の `fixed_all.json` は初期 80/40/20 Mbps、cycle 2 で 40/60/10 Mbps、
cycle 4 で 80/40/40 Mbps に変更する。この設定と既存のデフォルト動作は変更しない。
`random` の意味をランダムウォークへ置き換えない。

## 3. ランダムウォーク仕様

### 3.1 容量と更新規則

| AP ID（0始まり） | 対象リンク | 下限 | 上限 | 刻み幅／最大変化量 | 初期容量 |
|---|---|---:|---:|---:|---:|
| 0 | PGW–CER | 25 Mbps | 85 Mbps | 15 Mbps | 55 Mbps |
| 1 | AP–router | 20 Mbps | 60 Mbps | 5 Mbps | 40 Mbps |
| 2 | AP–router | 15 Mbps | 40 Mbps | 5 Mbps | 20 Mbps |

範囲・刻み幅は依頼内容に従う。以下の確率・境界処理・初回更新規則は本設計での提案とする。
範囲・刻み・初期容量の正本は `configs/capacity/random_walk_all.json`（表は現時点の値）。
Mbps は 1,000,000 bps とし、計算・保存は整数 bps で行う。

各リンク i、サイクル t = 1..numCycles について:

```text
C[i, 0] = 初期容量
u[i, t] = {-1, 0, +1} から等確率（各 1/3）で抽選
candidate = C[i, t-1] + u[i, t] * step[i]
C[i, t] = min(max(candidate, min[i]), max[i])
```

- 全3リンクを独立な変動過程として扱い、共通変動要因は初期版では設けない。
- 0 の抽選や境界処理により、値が変わらないサイクルも許容する。
- 境界外へ進もうとした場合は境界に留まる（再抽選や反射はしない）。
  例えば AP0 が 40 Mbps のとき、-10/0/+10 の結果は 40/40/50 Mbps。
  境界では実際の据え置き確率が 2/3 になる。
- **cycle 1 の開始時から抽選結果を適用**する。cycle 0 は初期容量の記録であり、
  ウォームアップには JSON の初期容量を使う。cycle 1 を初期値のまま固定しない。
- 毎サイクル開始時に更新し、次の開始時刻まで保持する。最終サイクル後の強制復旧はしない。
- 両方向を同じ容量にする。方向別の独立抽選は行わない。
- AP0 の相対変化幅は容量によって変わり、各 AP の相対変化率は同一ではない。

### 3.2 時刻と乱数の定義

適用時刻は既存 C++ の式を維持する。

```text
t(cycle_id) = cycle_start_offset + cycle_duration * (cycle_id - 1)
```

6秒などを新たに直書きせず、実効設定・既存メタデータから時刻を確認する。
現在の既定シナリオは5サイクル、cycleTimeSec=9.5。
容量変更は同時刻のアプリ・制御イベントより先に登録し、既存の適用順を維持する。
変更前に送信開始済みのパケットには既存モデルの動作を適用し、過去に遡って再計算しない。

容量専用の `random.Random(event_seed)` を1つ使い、ns-3・方策の乱数から分離する。
サイクル昇順、その中で AP ID 0, 1, 2 の順に `choice([-1, 0, 1])` を1回ずつ呼ぶ。
境界でも必ず抽選し、JSON の AP 記載順や手法によって抽選順を変えない。
同じ実装・設定・event_seed なら同じ系列を生成する。
厳密な再利用は Python バージョンの違い等に依存しない保存済み CSV を正本とする。

## 4. 設定ファイル案

設定ファイル: `configs/capacity/random_walk_all.json`。
既存スキーマ v1 の新モードとして拡張し、旧設定は変更しない。
旧プログラムで新モードが使えるという意味ではない。

```json
{
  "schema_version": 1,
  "setting_path": "data/scenarios/ap0_capacity_80_seed1001.json",
  "capacity_variation": {
    "mode": "random_walk",
    "event_seed": 2001,
    "boundary": "clip",
    "update_interval_cycles": 1,
    "links": [
      {"ap_id": 0, "initial_bps": 80000000, "min_bps": 40000000, "max_bps": 100000000, "step_bps": 10000000},
      {"ap_id": 1, "initial_bps": 40000000, "min_bps": 20000000, "max_bps": 60000000, "step_bps": 5000000},
      {"ap_id": 2, "initial_bps": 20000000, "min_bps": 10000000, "max_bps": 40000000, "step_bps": 5000000}
    ]
  }
}
```

初期版は `boundary=clip`、更新間隔1のみ対応。他の値は黙って無視せず拒否する。
等確率の {-1,0,+1} はアルゴリズム仕様とし、実効設定にも保存する。
初期容量は JSON の `initial_bps` を正本とし、範囲・刻みの検証後に C++ へ渡す。
PGW–CER と Wi-Fi AP1/AP2 の両方向をこの容量で初期化し、manifest に保存する。
fixed/random/constant と直接 master 実行の既定初期容量は変更しない。
容量範囲・刻み幅は JSON から取得し、コードに AP 別の数値を追加で直書きしない。

検証項目:

- AP ID は 0,1,2 が各1件。重複、欠落、不明 ID は拒否。
- 容量・刻み幅は bool を除く正の整数。容量は uint64 範囲内。
- `min_bps < max_bps`、`min_bps <= initial_bps <= max_bps`。
- `(max_bps-min_bps) % step_bps == 0`、`(initial_bps-min_bps) % step_bps == 0`。
- event_seed は bool を除く0以上の整数。numCycles は1以上。
- 全設定を検証してからイベント生成・実行する。

## 5. CLI 設計

Python ラッパーに `--capacity-mode {fixed,random_walk,random,constant}` を追加する。
C++ master に乱数生成モードは追加せず、既存 `--capacityEventsPath` を使う。

### 選択と競合の規則

1. `--scenario` と `--capacity-mode` の両方を省略: 現行どおり `fixed_all.json`。
2. `--capacity-mode` のみ指定: 次の既定シナリオを選択。
   - fixed → `configs/capacity/fixed_all.json`
   - random_walk → `configs/capacity/random_walk_all.json`
   - random → `configs/capacity/random_ap0.json`
   - constant → `configs/capacity/constant.json`
3. `--scenario` のみ指定: 現行どおり JSON 内の mode を採用。
4. 両方指定: JSON の mode と一致する場合のみ受理。不一致はエラー。
   異なる構造の設定を自動変換せず、設定の取り違えを防ぐ。
5. `--event-seed` は生成モード random / random_walk に限って JSON の seed を上書き。
6. `--events` は既存どおり生成より優先し、実効 mode を replay とする。
   `--events` と `--event-seed`、または新オプション `--capacity-mode` の併用は拒否。
   シミュレーション条件の指定には `--scenario` を使う。CSV だけで全条件は復元しない。

実装時は argparse の `--scenario` 未指定を識別できるようにし、解決後の既定値を維持する。
`--method`、`--rng-seed`、`--output-root`、`--generate-only`、`--no-build` は従来どおり。

### 利用コマンド

```bash
# 現行の固定イベント設定
python3 scripts/run_capacity_scenario.py --capacity-mode fixed --method no_switch

# ランダムウォーク: ns-3 seed とイベント seed は分離
python3 scripts/run_capacity_scenario.py --capacity-mode random_walk --rng-seed 1001 --event-seed 2001 --method no_switch

# シミュレーションせず、系列を生成・保存
python3 scripts/run_capacity_scenario.py --capacity-mode random_walk --event-seed 2001 --generate-only

# 上で得た CSV と同じ条件で別手法を比較（パスは実際の出力先へ置換）
python3 scripts/run_capacity_scenario.py --scenario configs/capacity/random_walk_all.json --events /path/to/run/planned_events.csv --method logistic
```

既存のオプションなし実行や `--scenario` を用いたコマンドの結果は変えない。

## 6. イベント適用・ログと再現性

既存 CSV ヘッダ `cycle_id,ap_id,rate_bps,direction` と `direction=both` を維持する。
全サイクル×3リンクの行を出力し、据え置きも省略しない（予定イベント数は 3×numCycles）。
既存適用器は各行を両端へ適用するので、初期 cycle 0 を除く実ログはその2倍の行数になる。
これは容量の適用記録であり、同値再設定を実容量変更や端末切り替えとして数えない。

保存するもの:

- `planned_events.csv`: 比較手法間で共通利用する抽選済み系列。
- `effective_config.json`: 解決済み mode、シナリオ、全リンクの範囲・刻み・初期値、
  event_seed、ns-3 seed、境界処理、確率、初回更新 cycle=1、更新間隔、乱数抽選順、
  生成アルゴリズム識別子（例 `clipped_random_walk_v1`）、Python バージョン、argv、hash。
- `setting.json`、実適用 `events.csv`、初期端末割当、既存のプロセス状態・完了マーカー。
- replay 時は入力 CSV のパス・hash を記録し、JSON の seed で生成したと誤記しない。

既存ログ列名・QoE ログ形式は変更しない。新しい manifest 項目は追加のみとし、
チェッカー・解析側も必要に応じて同時更新する。
将来の容量イベントを方策の観測状態に追加しない。
同じ seed だけで公平性を保証せず、同じ設定・初期割当・保存系列で比較する。

## 7. 実装単位と受け入れテスト

1. Python に random_walk の検証・生成を追加し、単体テストする。
2. JSON と CLI モード選択を追加する。既存 mode の回帰テストを行う。
3. 保存・検証・利用手順を更新し、小規模 ns-3 結合試験後に本実験へ進む。

C++ は原則として既存の汎用イベント適用器を流用する。
外部依存ライブラリの追加や ns-3 バージョン変更は不要。

受け入れ条件:

- 全値が範囲内・格子上、差分は AP0 が -10/0/+10、AP1/2 が -5/0/+5 Mbps。
- 乱数源を制御したテストで上下限・据え置き・最終サイクルを検証する。
- cycle 1 の更新を初期容量から計算し、numCycles=1 でも3件生成する。
- 同じ seed・設定なら完全一致。JSON の AP 順序を変えても同じ系列。
  異なる seed で必ず異なるとするテストは避ける（偶然一致は可能）。
- 上記の不正設定、モード不一致、replay との競合を拒否。
- 既存 fixed/random/constant の生成結果・省略時挙動・既存 replay を維持。
- 予定と実適用の時刻・容量・両方向が一致し、初期帯域も一致。
- replay でも同じ容量系列を適用。端末数×周期数のログと完了状態を確認。
- 容量適用時に端末接続先を変更しない。端末切り替えは選択手法だけが決定する。

## 8. 評価計画と限界

まず no_switch で容量系列と TP/RTT の応答を確認し、その後、ランダム切り替え、
ルールベース、DRL を実装済みの範囲から比較する。未実装手法の結果は出さない。
満足度の調和平均、不満足端末数、平均 TP/RTT、満足度分布、切り替え回数、
切り替え対象外の悪化数、実行／推論時間、seed 間ばらつきを記録する。
ns-3 seed と event_seed の組を保存し、複数組で評価する。

短い系列では設定範囲全体に到達しない。特に現行の5サイクル設定は動作確認用とし、
本評価で周期数を延長する場合は別設定へ保存する。既存設定を暗黙に延長しない。
設定範囲だけでなく実際に訪れた容量・負荷を確認し、容量変動がボトルネックになったかも確認する。
従来の fixed は急変条件、random_walk は時間相関を持つ条件として別々に報告する。
等確率・周期更新が実環境に適合するかは未検証であり、実測由来の設定とは扱わない。

## 9. 設計書作成時の作業記録（実装前）

- 成功: 現行 runner、JSON、容量適用 C++、既存設計書を読み、本設計書を追加。
- 実行した確認コマンド: `pwd`、`find . -name AGENTS.md`（生成物ディレクトリを除外）、
  `ls doc`、`cat` / `sed -n` による上記関連ファイルの閲覧、
  `git status --short`、`ls tests/*capacity*`、`git diff --check`。
- 未実行: 新モード実装、設定 JSON 追加、ビルド、単体・結合テスト、シミュレーション、QoE 評価。
- 既存の未追跡実験結果・解析スクリプトは変更しない。

次の検証: (1) イベント生成の単体試験、(2) 小規模の両方向適用／replay 試験、
(3) 複数 seed における容量系列と QoE の比較。

## 10. 実装・検証記録（2026-10-11）

### 実装内容

- runner に `random_walk` 生成、事前検証、`--capacity-mode` の解決・競合検証を追加。
- `configs/capacity/random_walk_all.json` を追加。既存設定 JSON は変更なし。
- manifest に生成仕様を追加。replay 時は入力 CSV の hash を保存し、生成仕様は付与しない。
- checker に予定 CSV の hash・manifest・C++ コピーとの一致、および
  random_walk の全周期行数・範囲・格子・最大差分の検証を追加。
- C++・無線条件・Delay・QoE ログ列は変更なし。既存容量適用器を利用する。

### 実行と結果

```bash
python3 -m unittest discover -s tests -p test_capacity_scenario.py
python3 -m py_compile scripts/run_capacity_scenario.py scripts/check_capacity_scenario.py
./ns3 build master -j 4
git diff --check
```

- 単体・CLI テスト13件成功。20 seed×100サイクルの範囲・刻み・再現性、
  強制した乱数による上下限・据え置き、初回更新、AP並び順非依存、
  入力拒否、旧モード、CLI選択、seed上書き、replayを検証。
- 構文検査・差分空白検査成功。
- build は終了コード0（`ninja: no work to do`、既存ターゲットを利用）。
  configure 中に ccache 設定用一時ファイルの read-only エラー表示あり。
  ビルド全体は成功したが、クリーンビルドは未実施。

結合試験は `/tmp/capacity-random-walk-smoke-5cinqeaq/` に設定・出力を保存。
Python の一時ディレクトリ生成スクリプトで既定 setting をコピーし、
**terminals のみ3に変更**（5周期、9.5秒/周期、seed1001は維持）。
各モードの既定シナリオの setting_path をそのコピーへ向けた。
同スクリプトから以下に相当するコマンドを順次実行した。

```bash
ROOT=/tmp/capacity-random-walk-smoke-5cinqeaq
for mode in random_walk fixed constant random; do
  python3 scripts/run_capacity_scenario.py --capacity-mode "$mode" \
    --scenario "$ROOT/$mode.json" --no-build --output-root "$ROOT/$mode"
done
python3 scripts/run_capacity_scenario.py --scenario "$ROOT/random_walk.json" \
  --events "$ROOT/random_walk/20261011T050521Z_b939ccdf/planned_events.csv" \
  --no-build --output-root "$ROOT/replay"
# 各実行直後、対応するrunディレクトリを検証（以下は新モードの例）
python3 scripts/check_capacity_scenario.py \
  "$ROOT/random_walk/20261011T050521Z_b939ccdf"
```

- 全5実行が終了コード0、各実行に対する checker 成功。
- 新モードは15予定イベント、両方向30適用行＋初期6行。
  cycle 1・最終cycle 5・同値適用を含め時刻と容量が一致。
- no_switch の全端末について、初期接続先維持と3端末×5周期のログを確認。
- 新モードと replay の予定CSVはバイト一致。両者の master_log を列比較し、
  差分は実測計算時間 `assignment_compute_ms` のみ。
- 既存の未追跡結果・解析スクリプトは変更していない。

**未確認:** 80端末での本実験、複数 seed の QoE 比較、DRL 学習性能。
検証成功は容量適用と再現性の確認であり、QoE 改善を示す結果ではない。
次は (1) 80端末 no_switch の応答確認、(2) 同じ保存系列による手法間比較、
(3) 複数 event_seed と必要に応じた長周期設定での評価を行う。
