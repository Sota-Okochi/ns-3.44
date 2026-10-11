# 共通バックホール容量シナリオの実行

## 目的と互換性

ns-3.44の共有バックホール等の利用可能サービス容量を抽象化する。
無線劣化や実際の背景トラフィックではない。キュー・TCP・送信中パケットはリセットしない。

**研究用ラッパーのデフォルトを固定変動とした。** `./ns3 run master` 自体のデフォルトは
変更していない。旧AP別オプション・既存設定・過去の結果も維持する。
ラッパーにシナリオを指定しないと `configs/capacity/fixed_all.json` を使う。
固定/ランダムウォーク/単発ランダム/一定は同じC++イベント適用器を通る。

## 実行

リポジトリルートから：

```bash
./ns3 build master -j 4
# デフォルト：80台、seed1001、5周期、固定位置、no_switch
python3 scripts/run_capacity_scenario.py --no-build
# 同じ固定変動をlogisticで確認
python3 scripts/run_capacity_scenario.py --method logistic --no-build
# モード指定で切り替え（fixed は従来の固定イベントであり容量一定ではない）
python3 scripts/run_capacity_scenario.py --capacity-mode fixed --no-build
python3 scripts/run_capacity_scenario.py --capacity-mode random_walk --event-seed 2001 --no-build
# ランダム系列（イベント用seedはns-3のseedとは独立）
python3 scripts/run_capacity_scenario.py --scenario configs/capacity/random_ap0.json --event-seed 2002 --no-build
# 容量一定の比較
python3 scripts/run_capacity_scenario.py --scenario configs/capacity/constant.json --no-build
```

`--no-build` を省けばns3ラッパーがビルドを行う。
`--rng-seed` は端末/通信側のseed、`--event-seed` はランダム容量系列用。
seedを省略しても時刻乱数は使わない。同じ設定・event_seedで同じ系列になる。
`--method` は現行masterの方式を選択する。設計段階のreactive_dqnはまだ使用不可。

標準固定イベント：cycle2（13.5秒）でAP0/1/2を40/60/10 Mbpsへ、
cycle4（32.5秒）で80/40/40 Mbpsへ変更。初期は80/40/20 Mbps。変更方向は両方向。
AP0はPGW送信/ CER送信、Wi-FiはAP送信/router送信をそれぞれ変更する。
変更は周期開始であり、その後に測った品質のみ方策に届く。未来情報を状態へ追加しない。

## JSONスキーマ v1

- `schema_version`: 1
- `setting_path`: 既存実験setting.jsonへのパス（相対パスはリポジトリルート基準）
- `capacity_variation.mode`: `fixed` / `random_walk` / `random` / `constant`

`--capacity-mode` は上記4種類の既定シナリオを選ぶ。
それぞれ `fixed_all.json` / `random_walk_all.json` / `random_ap0.json` / `constant.json`。
`--scenario` も指定する場合は JSON の mode と一致する必要がある。
両方省略すると従来どおり fixed_all.json。`--scenario` 単独指定も従来どおり。

random_walk:

- 現在のプリセット（値の正本は JSON）: AP0 は25〜85 Mbps、初期55 Mbps、毎サイクル -15/0/+15 Mbps。
- AP1: 20〜60 Mbps、初期40 Mbps、毎サイクル -5/0/+5 Mbps。
- AP2: 15〜40 Mbps、初期20 Mbps、毎サイクル -5/0/+5 Mbps。
- 各増分は等確率。範囲外は上下限に留まり、据え置きも CSV に保存する。
- ウォームアップは初期容量、cycle 1 の開始から抽選結果を両方向へ適用する。
- 専用 event_seed で全系列を事前生成。サイクル昇順、AP ID 昇順で抽選する。
- 範囲・刻み・初期値の正本は `random_walk_all.json`。各リンクの `initial_bps` を
  C++ の `pgwCerRate` / `ap1BackhaulRate` / `ap2BackhaulRate` に渡し、ウォームアップから反映する。
  上限と初期値は下限から step_bps の整数倍であること。更新間隔は1、境界処理はclipのみ。
  C++ オプション追加後は `./ns3 build master -j 4` を一度実行してから `--no-build` を使う。
- 詳細は [ランダムウォーク設計](capacity_random_walk_design.md) を参照。

`--event-seed` は random/random_walk にのみ使える。

fixed:
```json
{"mode":"fixed","events":[
  {"cycle_id":2,"ap_id":0,"rate_bps":40000000,"direction":"both"},
  {"cycle_id":4,"ap_id":0,"rate_bps":80000000,"direction":"both"}
]}
```

cycleは1始まり、APは0始まり。AP1/AP2のイベントも同じリストに記述できる。
同時刻の別AP変更は可能。同一cycle/APの重複は拒否する。回復も明示イベントで記述する。
rateは正の整数bps、cycleは1..numCycles、directionは初期版ではbothのみ。

random:
```json
{"mode":"random","event_seed":2001,"ap_id":0,
 "low_rates_bps":[20000000,40000000],
 "drop_cycle_range":[2,3],"duration_cycles_range":[1,2]}
```

1episodeに指定した1リンクの低下・回復を1組生成する。
範囲は両端を含む整数。開始周期・継続周期・低下帯域を独立に抽選する。
最も遅い回復がnumCyclesを超える設定は、値を丸めずエラーにする。
回復先は標準初期帯域（AP0/1/2=80/40/20 Mbps）。複数回のランダム変動は未対応。
学習用の長いepisodeは別setting.jsonを作り、setting_pathと範囲を変更する。
実装のランダム例は5周期の動作確認用であり、学習シナリオの完成形ではない。

constantはイベント0件。初期帯域だけを記録する。
random_walk 以外のモードでは従来どおり初期帯域80/40/20 Mbps。
全モードで実際に指定する初期帯域を manifest の `initial_bps` と `argv` に保存する。

## 生成だけ行う／同じ系列を再利用する

```bash
python3 scripts/run_capacity_scenario.py --scenario configs/capacity/random_ap0.json --generate-only
# 上で表示されたrunディレクトリ内のCSVを指定する
python3 scripts/run_capacity_scenario.py --events /path/to/run/planned_events.csv --method logistic --no-build
```

`--events` はJSONのイベント生成より優先し、保存済み系列を適用する。
同時に `--event-seed` または `--capacity-mode` を指定したらエラー。
random_walk の再実行は元と同じ `--scenario` も指定する。
CSVだけでは初期容量・端末数等は復元しない。JSON を編集した場合は元の初期容量も復元すること。
全手法で同じsetting、seed、イベントCSVを使う。初期割当/アプリも出力して一致を確認する。
方策によって乱数消費が変わり得るため、同seedだけで全通信過程まで一致すると主張しない。

## 出力

既定：`results/capacity_scenarios/<UTC日時_識別子>/`。実行ごとに新規ディレクトリ。
`--output-root` で変更可能。既存結果は上書きしない。

- `effective_config.json`: 展開設定、seed、生成済みイベント、実行argv、設定/関連ソースhash
- `setting.json`: CLI seedを反映した実効シミュレーション設定
- `planned_events.csv`: 再利用可能なcycle/AP/bps/方向の系列
- `console.log`: ns3 stdout/stderr（実行中の進捗はこのファイルを参照）
- `process_status.json`: ns3ラッパーの終了コード
- `simulation/<端末数>/<方式>/`: master等の既存ログ
- その下の `capacity_schedule/<run_id>/`: 初期割当、設定、予定系列、実適用events、metadata、完了マーカー

random_walk の manifest には `capacity_generation` としてアルゴリズム識別子、
確率、初回更新、抽選順なども保存する。replay では生成情報を付与せず、
`replay_source_sha256` を保存する（capacity_variation は入力シナリオの記録）。

`events.csv` のcycle0は初期帯域。変更イベントはcycle>=1。
完了マーカーはSimulator::Runから戻った証拠であり、プロセス終了コードと併用する。
汎用イベント適用器は専用キューサンプラを追加しない。必要なら別途既存queueDiagnosticsを
直接master実行で併用できるが、ラッパーはその任意追加CLIをまだ公開しない。

```bash
python3 scripts/check_capacity_scenario.py results/capacity_scenarios/<run_id>
```

両方向の予定/実イベント、初期帯域、完了時間、プロセス終了、UE×周期数、seed/methodを検証する。
検証成功はQoE改善を意味しない。masterのTP/RTT/Hとアプリ構成を別途分析する。

## 直接masterから利用する場合

`--capacityEventsPath=/absolute/path/events.csv` を指定する。CSVはラッパーで生成することを推奨。
ファイル先頭は `cycle_id,ap_id,rate_bps,direction`、行は例 `2,0,40000000,both`。
C++側でも重複・範囲外・不正帯域・対象リンクを検証し、全行確認後にScheduleする。
旧 `ap0/1/2CapacityVariation` または `ap0/1/2CapacityTrace` と同時有効化したらエラー。
cycleイベントはアプリ/制御イベントの登録より前に登録し、同じ時刻では容量変更を先に実行する。
新規オプション未指定の既存コマンドは以前の動作を保持する。

## テスト・検証記録

- `python3 -m unittest discover -s tests -p test_capacity_scenario.py`: 5件成功。
- 固定/ランダム `--generate-only`: 成功、固定seedで系列生成を確認。
- 通常sandbox buildはccache一時領域の権限で失敗。承認付きbuildへ切り替え。
- 下記の結合試験結果は実行後に追記する。80端末本実験・RL学習は今回未実行。

### 結合試験（実施済み）

本番settingをコピーし、端末数のみ3にした `/tmp/capacity-smoke-setting.json` を使用。
対応するシナリオJSONも `/tmp/capacity-smoke-<fixed_ap0|random_ap0|constant>.json` へ保存。

```bash
./ns3 build master -j 4
python3 scripts/run_capacity_scenario.py --scenario /tmp/capacity-smoke-fixed_ap0.json --no-build --output-root /tmp/capacity-smoke-fixed-final
python3 scripts/run_capacity_scenario.py --scenario /tmp/capacity-smoke-random_ap0.json --no-build --output-root /tmp/capacity-smoke-random
python3 scripts/run_capacity_scenario.py --scenario /tmp/capacity-smoke-constant.json --no-build --output-root /tmp/capacity-smoke-constant
python3 scripts/run_capacity_scenario.py --scenario /tmp/capacity-smoke-fixed_ap0.json --events /tmp/capacity-smoke-random/20261010T125658Z_98ab76b7/planned_events.csv --no-build --output-root /tmp/capacity-smoke-replay
python3 scripts/run_capacity_scenario.py --scenario /tmp/capacity-smoke-all.json --no-build --output-root /tmp/capacity-smoke-all
```

最後のall設定はcycle2でAP0/1/2を40/20/10 Mbpsへ、cycle4で80/40/20 Mbpsへ回復。
上記は全てexit0、5周期完了、各出力に対するcheck_capacity_scenario.pyも成功。
ランダム生成と保存CSV再実行のmaster全列は計算時間列を除き一致。
新constantと旧イベント未指定のno_switchも同じ比較で一致。
C++直接入力の重複cycle/AP、旧variationとの競合がエラーになることも確認した。

既存APテスト12件中11件成功、AP1のscenarioテスト1件は既存設定が100端末なのに
80端末を期待しており失敗（今回その設定は変更していない）。
`git diff --check` と新Pythonスクリプトの構文検査は成功。
80端末本番実験、長時間ランダム系列、学習性能は未確認。

### JSON を初期容量の正本に変更（2026-10-11）

`random_walk_all.json` 自体の値は変更せず、initial_bps と固定値の一致制約を撤廃。
初期容量をシミュレータへ渡し、ウォームアップ・cycle0ログ・manifest を一致させた。
他モードの既定容量、遅延、キュー設定は維持。実行コマンドと確認結果:

- `python3 -m unittest discover -s tests -p test_capacity_scenario.py`: 14件成功。
  AP0/1/2 の独自初期値と replay 時の manifest/argv を検証。
- `./ns3 build master -j 4`: sandbox 内では ccache 書き込み権限で失敗、承認付き再実行で成功。
- `python3 scripts/run_capacity_scenario.py --capacity-mode random_walk --rng-seed 1002 --generate-only --output-root /tmp/capacity-json-master-generate`: 成功。
- `python3 scripts/run_capacity_scenario.py --scenario /tmp/capacity-json-master-smoke/scenario.json --method no_switch --rng-seed 1002 --no-build --output-root /tmp/capacity-json-master-smoke/runs`: exit0。
  元設定から端末数3、初期容量55/35/25 Mbpsへ変更した検証設定を /tmp に保存。5周期完了。
- `python3 scripts/check_capacity_scenario.py /tmp/capacity-json-master-smoke/runs/20261011T092323Z_e814e99b`: 成功。
  全3リンク両方向の初期容量・変更イベント・端末ログを確認。
- `git diff --check`: 成功。

80端末本実験と QoE 改善の評価は未実行。次は本設定での実行と、同一系列での手法比較を行う。
