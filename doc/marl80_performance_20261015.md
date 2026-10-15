# 80端末MARLの実行時間調査（2026-10-15）

## 新規計測：正常完了（以下の末尾に結果を追記）

- 内部通知方式 `build/marl-internal-report-v1/master`。
- 80端末、動的容量 `configs/capacity/random_walk_all.json`、環境seed384、モデルseed1。
- 新規未学習モデル、episode_id45、1 worker、5周期、4意思決定。旧seed384の更新2回モデルの再現ではない。
- smoke実行でありPPO更新なし。学習データへ流用しない。
- `--perfDetailed=1 --perfWifiSampleEvery=1024`。時間はinclusive（入れ子は重複）。
  Wi-Fi sampled行は1024受信に1回の標本だけの時間。全受信累積と直接比較しない。
- 計測オーバーヘッドを含む。改善率を評価する際は同じ条件の計測なしA/Bも必要。
- 出力 `results/perf/marl80_internal_seed384_20261015/`。
  wrapper・バイナリ/ライブラリhash・設定・seed・詳細起動引数を保存。
- `episode/completion.json`が成功完了の印。失敗は`episode/interrupted.json`。
  read-only watcherが終了後に`scripts/summarize_perf.py`を実行し、`summary.txt`と
  `measurement_status.json`を保存する。完了前のファイルを全期間結果として扱わない。
- 外部perfサンプリングはWSLカーネル対応のperfが未インストールで失敗。
  追加インストールは行わず、既存ns-3内部計測を使用する。

## 過去の80端末測定からの候補（今回のMARL結果とは区別）

`results/perf/random_seed1001_1791248764605668_pid5274/` のfinal:

| 関数 | 累積inclusive時間（秒） | 呼び出し数 |
|---|---:|---:|
| Simulator::Run | 11770.72 | 1 |
| MultiModelSpectrumChannel::StartRx | 5498.98 | 174470598 |
| SpectrumWifiPhy::StartRx | 3869.73 | 78377787 |
| ThreeGppSpectrumPropagationLossModel::DoCalcRxPowerSpectralDensity | 1306.87 | 96092811 |
| MultiModelSpectrumChannel::StartTx | 458.84 | 2153958 |

この旧random法の結果では、受信イベント大量発生と無線PHY/伝搬計算が主な候補。
時間は包含関係があるため足し合わせて寄与率を作らない。
その後Wi-Fi最適化が実施されているため、上記の時間/割合を現在の実装へ転用しない。

より後の `wifi_local_hint_ab_20261009_114423/seed1001/after/perf/`
`random_seed1001_1791520795688147_pid100954` は粗粒度計測のみ:
Simulator::Run=6668.499秒、TP集計5回合計0.148秒、切替バッチ4回合計0.041秒。
約8.32億イベント（境界差の合計）。これはMARL推論時間を測った結果ではない。

## 新しい測定で判定する項目

1. StartRx/StartTx、Wi-Fi受信、NR伝搬・スケジューラ、FlowMonitorの累積時間と呼出数。
2. devices.csvのAP別・選択中/非選択デバイス別の受信カウンタ。
3. 周期別wall時間とイベント数。初期化/終了/制御処理との比較。
4. Wi-Fi標本内訳（RUバンド積分・パワー格納・preamble）の相対時間。

無線インターフェース停止、干渉の省略、精度低下、パケット数削減は実験条件を変えるため、
単なる高速化として実施しない。新しい実測結果に基づき数値と乱数消費順序を保てる
重複計算削減などの候補を選び、同一seed・同一行動で結果同値性を確認する。
本調査ではソースのリファクタリング、実験パラメータ変更、既存ジョブ停止は実施していない。


## 完了後の分析

`measurement_status.json=complete`、smokeは5観測/4遷移を正常完了。
プロセス実時間6070.310秒（約1時間41分10秒）、Simulator::Run 6069.878秒、
最大RSS 1191548 KiB（約1.136 GiB）。1プロセス・計測オーバーヘッド込み。
これは旧失敗モデルではなく未学習policy_version=0での結果。旧条件との改善率には使わない。
内部通知は3AP×5周期=15件すべて期限内受理。本seed・本行動列で通知方式が動作したことを確認。

| 対象 | inclusive秒 | Simulator::Run比 | 呼出数 |
|---|---:|---:|---:|
| MultiModelSpectrumChannel::StartRx | 3512.818 | 57.87% | 145270422 |
| SpectrumWifiPhy::StartRx | 2438.238 | 40.17% | 81960741 |
| ThreeGppSpectrumPropagationLossModel::DoCalcRxPowerSpectralDensity | 826.831 | 13.62% | 63309681 |
| MultiModelSpectrumChannel::StartTx | 343.321 | 5.66% | 1793462 |
| NetSim::CollectTerminalThroughput | 0.160 | 0.003%未満 | 5 |
| NetSim::ApplyHandoverBatch | 0.027 | 0.001%未満 | 4 |

重要: StartRxはWi-Fi/NR伝搬処理を内部に含むため表の割合は加算禁止。
計測したFlowMonitor::の累積行は合計約9.121秒。これだけでFlowMonitorの全影響を
測定したとはいえないが、TP集計や切り替え処理より無線受信系を優先する根拠になる。
本smokeでPPO更新は実施していないため、PPO更新コスト自体について結論は出さない。

総イベント801915769（約8.02億）。1周期目1312.308秒、以後1040.598/1102.947/
1036.765/1032.457秒。最終観測51.3秒から停止56.5秒まで544.805秒（約8.98%）。
末尾短縮は最終出力や遅延到着の扱いを確認してからであり、即時削除しない。

端末デバイスの受信カウンタ（インフラ除外）では選択中46349210、非選択96090058、
非選択比67.46%。選択はアプリの接続先を意味し、PHY電源状態ではない。
非選択も干渉・キャリアセンス・次回切替に関係するため、無条件停止やイベント削除は
結果同値なリファクタリングではない。この比率をそのまま高速化可能割合と扱わない。

優先順位:
1. Wi-Fi StartRxをさらに分解。既存sampled.he_ru_bands=1.378秒/80040標本、
   post_power=1.929秒（内start_preamble=1.886秒）。標本時間から全期間へ単純外挿しない。
   RUパワー積分の重複、preamble/干渉状態更新・一時データ生成の内訳を調査。
2. NR伝搬のCalcBeamformingGain=720.960秒（内GenSpectrumChannelMatrix=340.520秒）。
   固定位置でもチャネル・ビーム・時刻が変化するため、キャッシュの無効化条件と
   入力同一性を確認してから重複計算削減を設計する。
3. 受信イベント関連の割当・解放/スケジューラ等の未分解時間を追加測定。
   inclusive値だけでは排他的な100%内訳や具体的なデータ構造の支配率は確定できない。

次の比較は同じモデル・行動列・seed・計測設定で実施。観測、H、RTT/TP、割当が
一致するか確認した上で、計測なしA/Bと別seedによる時間評価を行う。
速度改善値はまだ未測定。本分析ではリファクタリングを実施していない。

## Wi-Fi受信詳細計測の追加（結果同値な削減の準備）

今回の段階は診断のみ。無線処理自体の省略や最適化はまだ行っていない。
`SpectrumWifiPhy::StartRx`で選ばれた1/1024受信の同期呼出し木にだけ標本フラグを
伝播し、RAIIで復元する。予約された将来イベントへフラグを伝播しない。
RNG、シミュレーション時刻、イベントの追加/削除、演算順序を変えない。

追加行（全て `WifiRx.sampled.` 接頭辞）:
- `layout_and_storage`: 帯域レイアウト取得と受信電力格納領域の生成。
- `ppdu_prepare`: 受信用PPDU取得。
- `do_get_event`: PHYイベント取得/生成（HE派生実装を含む）。
- `interference_event_create`: 干渉Event生成（受信電力のmoveを維持）。
- `AppendEvent`: 帯域ごとの干渉履歴探索・削除・挿入・電力更新。
- `phy_state_dispatch`: 受信状態の分岐と処理。
- `AddPreambleEvent`, `DropPreambleEvent`, `GetEnergyDuration`, `UpdateEvent`。

入れ子の包含時間であり加算しない。これで`AppendEvent`が支配的なら、その内部の
map探索/履歴削除/挿入/電力加算のどれが大きいか次に分解する。候補として、同一Eventの
start/endTime取得と同一freqRangeのm_rxing検索を帯域ループ外へ出せるかを調べる。
帯域内の浮動小数点加算をprefix-sum等で並べ替える最適化はビット一致を壊し得るため
この段階では採用しない。状態を変えるPHY停止・干渉省略も行わない。

ビルド:
`python3 results/perf/wifi_rx_detail_20261015/build.py`
通常buildの実行物は上書きせず、3つのWi-Fiオブジェクトのみ独立再コンパイル・再リンク。
`build/wifi-rx-detail-v1/run-master`が私有ライブラリを明示選択する。

テスト:
`python3 -m unittest discover -s tests -p test_wifi_rx_detail.py`
成功。小規模2seedの変更前後で受信bytes、総イベント数、周期境界とデバイスカウンタが一致。
seed1001で計測ON/OFFも受信bytesと総イベント数が一致。追加計測行の出現も検証。
80台・干渉が重い条件全体の同値性をこれだけで保証するものではない。

80端末の同条件計測を開始:
`results/perf/wifi_rx_detail_20261015/`
seed384/episode45/modelseed1/未学習モデル/動的容量/5周期/1worker。
完了後、`analyze.py`が`summary.txt`と`comparison.json`を生成する。
比較対象は前回の内部通知方式の計測。観測JSONL、遷移JSONL、QoE CSVのバイト一致と
周期境界のシミュレーション時刻・イベント数一致を検査する。
実行中の時点では同値性・時間内訳・速度改善について完了したと報告しない。

### ユーザー指示による50端末への切り替え

80端末の詳細計測のみSIGTERMで停止（PID39249、事前にcmdline確認）。
元の80端末ログは途中結果として保存し、完了結果としない。
50端末で新規計測を開始：`results/perf/wifi_rx_detail_50_20261015/`。
seed384、modelseed1、未学習モデル、動的容量、計測間隔1024は維持。学習更新なし。
終了後summary.txtとmeasurement_status.jsonを自動生成する。
50端末と80端末の結果同値性は比較しない。削減候補の探索後、同じ端末数でA/B検証する。
