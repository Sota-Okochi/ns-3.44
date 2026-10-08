# ns-3 実時間計測（3.44）

最新の構造最適化・2seed A/B手順は [wifi_shared_bands.md](wifi_shared_bands.md) を参照。

## Wi-Fi map挿入の最小最適化とA/B検証（2026-10-06）

`SpectrumWifiPhy::StartRx` の2つの挿入経路（通常/計測対象）を
`rxPowers.insert({band, power})` から `rxPowers.try_emplace(band, power)` に変更。
一時的な `pair<const WifiSpectrumBandInfo, Watt_u>` のconstキーからのコピーを
避け、ノード内に直接キーを構築する。キーは2つのvectorを持つ。
mapの型・比較順序・重複時に先の値を保持する仕様は維持する。
全帯域の電力計算、干渉、端末数、通信量、イベント、乱数、QoE式は省略しない。
全メモリ確保がなくなる変更ではなく、速度改善率は未確認。

変更前（git `575ed3c7704838867a0896edc0021a111606f64b`）の実行物を
`results/perf/wifi_insert_before/` に保存済み：master、全共有ライブラリ、
setting.json、変更前ソース、SHA256 manifest。通常ビルドではここを更新しない。
このローカルスナップショットはGit管理の配布物ではない。

### 本実験：同じ設定・seedで変更前→変更後を自動実行

他のmasterが終了してから、リポジトリルートで以下を実行する。
テスト中は再ビルド・設定編集・別のmaster実行をしない。

```bash
python3 scripts/verify_wifi_insert.py --seed 1001
```

- 80端末・5サイクル等の現在の条件を減らさず、2回を**順次**実行する。
- `random --rngSeed=1001 --perfTiming=1 --perfDetailed=0` を両方に指定。
- 保存したmasterを直接起動するため、`./ns3 run` は介さない。
  `LD_LIBRARY_PATH`で各版のライブラリを選び、実際の解決先をldd.txtに記録。
- baseline manifestと設定を検査。別masterが見える場合は停止（killしない）。
- 出力：`results/perf/wifi_insert_ab_YYYYMMDD_HHMMSS/{before,after}/`。
  console.log、runtime.json（コマンド・ハッシュ）、ldd.txt、perf/、master.csv、
  reward.csv、elapsed_seconds.txtを保存。元のOUTPUTログも保持。
- 全サイクル＋run_endの時刻・イベント数、masterの全列
  （実時間のassignment_compute_msだけ除く）、measured_rewardの全列を完全比較。
- 空/未完了ログ、ヘッダ/行数不一致、設定/seed不一致はエラー。
- 成功時のみPASS.jsonを作成。結果の一致はCSV出力精度での一致を意味し、
  内部状態のビット単位一致や一般の全seedに対する証明ではない。
- 旧詳細計測実験との時間比較ではなく、このA/Bの通常計測同士で比較する。

再比較（シミュレーションは実行しない）：

```bash
python3 scripts/verify_wifi_insert.py --compare-only results/perf/wifi_insert_ab_実際の日時
```

### 実施済みの検証と未実施事項

実行コマンド：

```bash
cmake --build cmake-cache -j 4 --target master
python3 tests/test_wifi_perf_integration.py
python3 tests/test_verify_wifi_insert.py
g++ -std=c++20 -O2 -Ibuild/include tests/wifi_insert_equivalence.cc \
  -Lbuild/lib -Wl,-rpath,"$PWD/build/lib" \
  -lns3.44-wifi-optimized -lns3.44-core-optimized -o /tmp/wifi-insert-equivalence
/tmp/wifi-insert-equivalence
LD_LIBRARY_PATH="$PWD/results/perf/wifi_insert_before/lib" results/perf/wifi_insert_before/smoke
LD_LIBRARY_PATH="$PWD/build/lib" results/perf/wifi_insert_before/smoke
git diff --check
```

ビルドは初回ccacheのsandbox書き込み制限で失敗、権限許可後に成功。
実際のWifiSpectrumBandInfoを使った一意/重複/比較上同等/複数セグメントのキー試験成功。
比較スクリプトの5テスト成功。小規模Wi-Fi結合試験も成功。
保存済み変更前と変更後のライブラリを使ったスモーク試験では、両方
`bytes=200 events=378`。詳細出力も別一時フォルダへ出し、devices.csv全行と
boundaries.csvの実時間以外の全項目一致を確認。
比較器のend-to-end動作は過去ログの一時コピーで確認したが、これは今回の
最適化後の本実験結果ではない。

**80端末の最適化前後A/B実験は未実行。通信/QoEの本条件での一致と速度改善は未確認。**

## 目的・不変条件

80端末の `random` でも約3時間かかる原因を調べるための観測機構。
端末数、seed、通信量、イベント登録順序、乱数消費、計測窓、QoE式は変更しない。
`APselection::tmain()` / `WriteMasterLog()` / DQNに新しい個別タイマーは追加しない。
それらの時間も `Simulator::Run` には含まれるので、未分類の残り時間をすべて
「無線処理」と見なしてはいけない。既存の `assignment_compute_ms` はそのまま。

## 実行

既存の optimized 設定でビルドする。本作業環境には `build/`（Debug）と
`cmake-cache/`（optimized）の両方に CMakeCache が存在したため、検証には後者を明示。

```bash
cmake --build cmake-cache -j 4 --target master

# 低頻度の区間計測のみ（最初はこちら）
./ns3 run master --no-build -- --method=random --rngSeed=1001 --perfTiming=1

# NR / Wi-Fi / FlowMonitor内部も集計（高頻度タイマーの負荷あり）
./ns3 run master --no-build -- --method=random --rngSeed=1001 --perfDetailed=1
```

`--perfDetailed=1` は `--perfTiming=1` を含む。指定しなければ計測は無効。
`--perfOutputDir=results/perf` は親ディレクトリ。各実行は
`<method>_seed<seed>_<epoch_microseconds>_pid<pid>/` に保存し、既存結果を上書きしない。
実際の保存先は `[Perf] output=...` に表示される。

```bash
python3 scripts/summarize_perf.py results/perf/random_seed1001_実際のID
```

実時間の比較には以前と同じ `/usr/bin/time -v` を併用する。
内部計測の `main::lifetime_before_report` は NetSim破棄までを含むが、
最終CSV書き出し、静的オブジェクト破棄、ns3ラッパーの起動/終了は含まない。
`main.cc` の従来の「総実行時間」は、今回から NetSimメンバー破棄後に表示する。

## 出力

- `metadata.txt`: run_id、実効seed、method、詳細計測フラグ、ビルドプロファイル、argv。
- `setting.json`: 使用した設定のコピー（rngSeed欄よりCLIの実効seedを優先）。
- `functions.csv`: 関数/区間別の呼び出し回数・合計/平均/最大実時間（ms）。
- `boundaries.csv`: シミュレーション時刻、区間実時間、実行イベント数差分。

`functions.csv` の各snapshotは **開始からの累積値**。サイクル別負荷は同じ関数の
累積値を差し引く。最終結果は `snapshot=final` の行を使う。
関数名に `::` を使い、列名は snake_case。単位は列名に明記。
計測途中の外側スコープは完了するまで記録されない（例: 実行中の `Simulator::Run`）。
早期returnでもスコープタイマーのデストラクタで集計される。

**時間は inclusive（子関数を含む）。親子の値を合算しない。**
例えば `RunSim` は `Run` / `Destroy` / 初期化を含み、NR親関数は行列生成を含む。
サイクル境界は既存の `KamedaAppServer::Ending()` の入口。
前境界からの区間には前回の割当/ログ/切り替えも含まれる。
最初の区間はウォームアップを含み、最後はcycle 5からSimulator停止までの残り区間。
計測用の新しいシミュレーションイベントは登録しない。

## 対象

### 通常計測

- main: NetSim生成、Init、RunSim、**メンバーを含む完全な破棄**。
- RunSim: 設定、トポロジ、データリンク層、ネットワーク層、アプリ設定、
  FlowMonitor Install、Simulator Run/Destroy。
- データリンク層: NR、Wi-Fi AP1/AP2設定、NR IP設定。
- TP: BuildTerminalIpMap、ResetTerminalFlowStats、CollectTerminalThroughput。
- ハンドオーバー: HandoverRequest、ApplyHandoverBatch、各RAT方向の切り替え、RebindTerminalApps。
- 既存サイクル境界: 実時間とSimulator::GetEventCountの差分。

### 詳細計測（サンプリングではなく全呼び出し）

- ThreeGppSpectrumPropagationLossModel: DoCalcRxPowerSpectralDensity、GetLongTerm、
  CalcBeamformingGain、GenSpectrumChannelMatrix。
- ThreeGppChannelModel: GetChannel、GenerateChannelParameters、GetNewChannel。
- NR: OFDMA AssignDLRBG/AssignULRBG、AMC CalculateTbSize、EESM GetTbDecodificationStats。
- Wi-Fi: SpectrumWifiPhy::StartRx、IdealWifiManager::DoGetDataTxVector、
  InterferenceHelper::CalculatePayloadSnrPer/CalculatePhyHeaderSnrPer。
- FlowMonitor: ReportFirstTx/ReportForwarding/ReportLastRx/ReportDrop、
  CheckForLostPackets(Time)、ResetAllStats。

呼ばれなかった関数は行が存在しない。全ns-3関数の網羅ではない。
イベントキュー操作や `ProcessOneEvent()` 全件のタイマーは追加しない。

## 計測負荷・再現性

`steady_clock` を使用し、Simulator::Nowはタイムスタンプ専用。
高頻度関数ではメモリ内の統計更新だけを行い、サイクル境界と終了時のみCSVを書き出す。
詳細計測は時計取得・統計検索による負荷があり、通常計測と同じ速度ではない。
無効時もスコープの有効判定は残るので、過去バイナリとの差には注意する。
DefaultSimulatorImplの単一シミュレーションスレッド専用。並列/MPI環境用ではない。
計測追加により現実の経過時間は変わるため、外部DQNのタイムアウト等を含む場合は
結果一致を別途検証する。実時間同期シミュレータでの同等性は保証しない。

比較時は同一seed・設定・ビルドで、計測OFF/通常/詳細の順に比較。
CSVのTP/RTT/H/割当（実行時刻などを除く）が一致することを確認する。
異常終了では `final` 行はない。直近境界までの累積値のみ利用可能。

## 検証コマンド

```bash
python3 tests/test_research_wall_profiler.py
git diff --check
cmake --build cmake-cache -j 4 --target master
./ns3 run master --no-build -- --PrintHelp

# ns-3 core上で5イベントを実行し、計測OFF/ONの乱数値とイベント数を比較
g++ -std=c++20 -O2 -Ibuild/include tests/research_wall_profiler_smoke.cc \
  -Lbuild/lib -lns3.44-core-optimized -Wl,-rpath,"$PWD/build/lib" \
  -o /tmp/ns3-perf-core-smoke
OUT=$(mktemp -d /tmp/ns3-perf-smoke.XXXXXX)
/tmp/ns3-perf-core-smoke > "$OUT/off.txt"
/tmp/ns3-perf-core-smoke "$OUT/on" > "$OUT/on.txt"
diff -u "$OUT/off.txt" "$OUT/on.txt"
python3 scripts/summarize_perf.py "$OUT/on"
```

単体テストは一時ディレクトリにC++テストをコンパイルし、OFF/通常/詳細、
早期return、親子集計、累積snapshot、イベント数差分、上書き防止を検証する。
外部Pythonパッケージは追加しない。
80端末・5サイクルの完全実験と性能改善効果は別途検証が必要。

### 実装時の確認結果

- `python3 tests/test_research_wall_profiler.py`: 成功。
- `git diff --check`: 成功。
- `cmake --build cmake-cache -j 4 --target master`: optimizedでビルド成功。
- `./ns3 run master --no-build -- --PrintHelp`: 終了コード0、追加した3引数を確認。
- 上記coreスモークテスト: OFF/詳細ONの両方で `rng_total=291 events=5`。
  `boundaries.csv` のイベント差分は0/3/2、要約スクリプトも実行成功。
- 初回の `./ns3 build master -j 4`: 別のbuildキャッシュを参照し、ccacheの
  sandbox外一時領域への書き込み制限で失敗。権限承認後、既存optimizedの
  `cmake-cache` を明示して検証した。既存実験条件は変更していない。
- 完全なネットワーク実験での計測OFF/ONの一致、計測オーバーヘッド、
  80端末・3シードの再実行は未実行。

## 第2段階：イベント数の解釈と詳細内訳

### 「約8.3億イベント」の意味

`DefaultSimulatorImpl::ProcessOneEvent()` はイベントキューから1件取り出し、
時刻・コンテキストを更新し、`m_eventCount` を増やして `EventImpl::Invoke()` を呼ぶ。
このカウンタは送信パケット数でも正常受信数でもない。キャンセル済みだがキューに
残って取り出されたイベントも、この位置ではカウントされ得る。終了時刻より未来に
残っているイベントや、通常の直接関数呼び出しをすべて数える値ではない。

例えば無線送信1回から複数の受信端末への通知が派生し、その後も受信終了、ACK、
再送、PHY/MACタイマーなどが別イベントとして登録される。NRの周期制御も存在する。
同じ時刻のイベントも別々に処理される。1イベント内で多数の関数を呼ぶため、
Wifi StartRx回数とNRスペクトル計算回数を足しても全イベント数にはならない。

実測した832,191,919イベント / 56.5シミュレーション秒は約14,729,061件/秒。
この「秒」は実時間ではない。イベント数が多いだけではバグとは断定できず、
端末・デバイス数、無線送信回数、受信先への展開数、各イベントのコストを分けて調べる。
サイクル2～5の約1.47～1.53億件は、少なくともサイクルごとの倍増を示していない。

### 実装順位・競合回避

1. Wi-Fi内部の区間タイマー。
2. デバイス/AP/選択状態別カウンタ。同じ受信経路なので1と一緒に実装・テスト。
3. OSのCPUサンプリング。実行スクリプトを用意し、**1・2とは別の実行**で測る。
   高頻度の全イベントタイマーは実装しない。計測負荷の混入を避け、実際のサンプリング
   実験は次段階とする。対象プロセスを停止したり、シミュレーション時間を短縮しない。

### Wi-Fi区間タイマー

```bash
./ns3 run master --no-build -- --method=random --rngSeed=1001 \
  --perfDetailed=1 --perfWifiSampleEvery=1024
```

`perfWifiSampleEvery` は正整数。全Wifi StartRx呼び出し通算の1, 1+N, 1+2N, ...回目で
内訳を測る。乱数は使わない。元の `SpectrumWifiPhy::StartRx` 全体時間・カウンタは全件。
デバイス別の均等/無作為抽出ではなく、周期処理と相関する可能性があるため、必要なら
別実行で1009等に変え、偏りを確認する。全件の内訳計測は1（負荷大）。

`functions.csv` の `WifiRx.sampled.*` は **抽出された受信だけの合計**：

- `regular_bands`: 通常の帯域電力集計ブロック。
- `he_ru_bands`: 802.11ax以降のRU電力集計ブロック。
- `band_power`: GetBandPowerW呼び出し（通常帯域とRUを合算）。
- `map_insert`: 受信電力マップへの挿入（同上）。
- `post_power`: 電力集計後のトレース、判定、干渉追加、後続受信処理。
- `start_preamble`: StartReceivePreamble呼び出し（開始成功を意味しない）。

regular/he_ruにはband_power/map_insertが含まれ、post_powerにはstart_preambleが含まれる。
band_power/map_insertは帯域単位、他は受信呼び出し単位なので呼び出し回数も異なる。
全体のStartRx合計と抽出内訳の合計を直接比較・減算しない。
計測・カウンタの管理、ローカルオブジェクト破棄など、内訳で網羅しない部分もある。

### devices.csv

新規CSV。関数CSVと同じsnapshotの **累積** カウンタを出力する。
`node_id` と `net_device_index` がデバイス識別子（IPv4インターフェース番号ではない）。
`ap_id_1based`: 1=NR、2=Wi-Fi AP1、3=Wi-Fi AP2。これは登録先の所属AP。
`role`: terminal/base_station/monitor/unknown。
`selection`: その観測時点のアプリ用選択APとデバイス所属APが一致すればselected、
異なればunselected。基地局・検査端末はinfrastructure。未登録はunknown。

**selectedはPHYがON、接続済み、または宛先であるという意味ではない。**
`WIFI_INACTIVE_PHY` の内部PHYインターフェース判定とも別概念。
ハンドオーバー時には観測ラベルだけを更新し、無線状態は操作しない。

カウンタ（MultiModelSpectrumChannelを通る無線信号が対象、P2Pは対象外）：

| metric | 意味 |
|---|---|
| tx_signal | チャネルStartTxへの入力回数。送信元デバイス側 |
| rx_scheduled | 受信通知をScheduleする回数。受信先デバイス側 |
| rx_arrival | チャネルStartRxが呼ばれた回数。伝搬損失判定等の前 |
| wifi_start_rx | Wi-Fi PHYのStartRx入口 |
| wifi_inactive_phy | 非アクティブな内部PHYインターフェースの分岐 |
| wifi_foreign | Wi-Fi信号型ではない分岐 |
| wifi_disabled | Wi-Fi受信同期を禁止している分岐 |
| wifi_weak | 受信感度未満の分岐 |
| wifi_cannot_start | CanStartRxがfalseの分岐 |
| wifi_preamble | プリアンブル受信処理へ進む分岐。正常受信パケット数ではない |

最後の6分岐は完了したWifi StartRxに対して排他的。合計はwifi_start_rxと一致する。
停止直前に予約された通知は未実行の可能性がある。予約時と到着時の選択状態も変わり得る。
したがってrx_scheduledとrx_arrivalの各selection別件数が必ず一致するわけではない。
一送信が多数受信先へ展開されると、rx_scheduledはtx_signalより多くなる。

`MultiModelSpectrumChannel::StartTx/StartRx` の全件時間も詳細モードで追加した。
StartRx時間にはWi-Fi/NR側の後続処理を含むため、既存の受信関数時間と加算しない。

### ③ CPUサンプリングの実行方法（別実行）

ターミナルA：詳細タイマー・デバイスカウンタをOFFにして通常どおり最後まで実行。

```bash
./ns3 run master --no-build -- --method=random --rngSeed=1001 --perfTiming=1
```

ターミナルB：実行中master本体のPIDを確認し、60秒間だけCPUをサンプリング。

```bash
pgrep -af 'build/master/ns3.44-master'
python3 scripts/sample_ns3_cpu.py --pid MASTERのPID --seconds 60 --frequency 49
```

`--dry-run` はコマンド表示のみ。複数masterがある場合はseed等を確認してPIDを選ぶ。
サンプリング終了後もmasterは動き続ける。開始位置による偏りを避け、序盤・中盤・終盤で
別々に採取する。これはイベント回数のランダム抽出ではなく、CPU時間のサンプリング。

前提は対象カーネルで利用可能なLinux `perf` とアクセス権限。今回の実装環境には
`perf` が見つからなかったため、実際のサンプリングは未実行。自動インストール、
自動sudo、sysctlの変更は行わない。カーネル/WSL環境に適したperfの用意が必要。

スクリプトは詳細計測ONのプロセスを拒否する。`perf record -e cpu-clock -F 49
--call-graph dwarf,16384 -p PID -- sleep 60` を実行し、独立した保存先に
`perf.data` / `record.log` / `metadata.json` / `self.txt` / `callgraph.txt` を生成。
`self.txt` で関数自身、`callgraph.txt` で呼び出し元を含む寄与を見る。
未分類時間を自動的に「イベントキュー時間」と扱わない。

optimizedビルドではインライン化等で関数境界が消える可能性があり、[unknown]が多ければ
シンボル/アンワインド情報を確認する。必要時は同じ-O3を保ったデバッグ情報付きビルドを
別途検討し、ビルド条件を保存する（debug/-O0に切り替えて速度比較しない）。

### 第2段階の検証記録

```bash
cmake --build cmake-cache -j 4 --target master
python3 tests/test_research_wall_profiler.py
python3 tests/test_sample_ns3_cpu.py
python3 tests/test_wifi_perf_integration.py
./ns3 run master --no-build -- --PrintHelp
python3 scripts/sample_ns3_cpu.py --help
git diff --check
```

- optimizedビルド成功。最初のsandbox内ビルドはccache書き込み権限で失敗し、
  承認後のビルドで成功。
- 単体テスト: サンプリング間隔、計測無効、selected/unselected/infrastructure/unknown、
  切り替え前後カウンタ、スクリプトの詳細計測との混在拒否、bounded attachコマンドを確認。
- Wi-Fi統合テスト: 計測OFF/ONとも **200バイト・378イベント**。
  Wi-Fi内訳6区間の出力、デバイス分類、分岐件数の合計一致、旧形式の結果も読める要約を確認。
  テストは独立した2ノードの短い検証で、研究用設定ファイル・端末数は変更しない。
- 80端末の完全実験、性能オーバーヘッドの定量化、perfによる実採取は未実行。
