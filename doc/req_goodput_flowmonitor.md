## Specification

### Goodput
- `master/applications.cc` の `NetSim::SetVideoApp()` にて、`monitorTerminals` も走査し監視端末へ `UdpTraceClientHelper`/`UdpServerHelper` をインストールする。`Ptr<Node> monitor` が `nullptr` でなく、かつ `GetPrimaryIpv4(monitor) != 0.0.0.0` の場合に、通常端末と同じ `Verbose_Jurassic.dat` を送信元トレースとして利用する。
- 監視端末側の受信アプリには `PacketSinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port))` を用い、`ApplicationContainer sinkApps = sinkHelper.Install(monitor);` として `sinkApps.Start(Seconds(0.9)); sinkApps.Stop(sinkStop);` を設定する。ポート番号は通常端末と重複しないよう `streamPort` をインクリメントまたは監視端末専用範囲（例: 15000 番台）を割り当てる。
- Goodput 計測用に監視端末アプリへ受信バイト数を集計するメンバ（例: `uint64_t m_totalRxBytes`）を追加し、`PacketSink` の `TraceConnectWithoutContext("Rx", ...)` で受信パケット長を累積する。シミュレーション終了直前に `Simulator::Schedule(m_simulationDuration - MilliSeconds(1), &NetSim::ReportMonitorGoodput, this);` を呼び出し、`goodput = (m_totalRxBytes * 8.0) / 実測時間[秒]` を算出してログ出力する。
- 実測時間はアプリ開始/終了時刻を基準とし、`Seconds(1.0)` 以降の受信のみを対象にする。必要であれば `APMonitorTerminal` 内に `StartApplication()` と連動したタイムスタンプを保持し、Goodput 計算時に `Simulator::Now() - m_trafficStartTime` を使用する。
- 結果は `NS_LOG_INFO ("MONITOR_AP" << apId << " goodput = " << (goodput / 1e6) << " Mbps");` のように記録し、`OUTPUT/monitor-goodput.csv` 等へ `std::ofstream` で書き出すと後処理が容易になる。
- 計測した Goodput を APselection サーバへ連携するため、`APMonitorTerminal::ReportRTTToServer()` で送信するメッセージ形式を `"MONITOR_AP%d,%f,%f"`（`apId,averageRtt,goodputBytesPerSec` など）へ拡張する。Goodput 算出後に `m_lastGoodputBps` のようなメンバへ保存し、送信時に併せて送る。
- `APselection::setData()` 側では `splitString(recvMessage, ",")` の結果が 3 要素になることを前提にし、`ret2[2]` を実効スループット（ビット/秒）として `m_monitor_tp[apNo]` に格納する。初期化時に `m_monitor_tp.resize(m_APNum, 0.0);` を行い、受信があった AP の `m_has_tp[apNo]` を true にする。
- `APselection::tmain()` の出力生成では `m_has_tp[i]` が true の場合に `TP:` 欄へ `m_monitor_tp[i] / 1024.0`（KB/s 表示）をそのまま出力し、未取得の場合は従来どおり `APConstants::INITIAL_TP_MULTIPLIER[0] / ave_rtt` を使う。TP の表示フォーマットは従来と同じ `std::setprecision(2)` を維持する。

### Flowmonitor
- `master/NetSim.cc`（`NetSim::RunSim()` 実装ファイル）で `#include "ns3/flow-monitor-module.h"` を追加し、ネットワーク設定完了後・アプリケーション設定前に `FlowMonitorHelper flowmonHelper; Ptr<FlowMonitor> flowMonitor = flowmonHelper.InstallAll();` を呼び出す。監視端末だけを対象にする場合は `NodeContainer monitors; monitors.Add(monitorTerminals.begin(), monitorTerminals.end()); flowMonitor = flowmonHelper.Install(monitors);` とする。
- 必要に応じて `flowmonHelper.SetMonitorAttribute("MaxPerHopDelay", TimeValue(MicroSeconds(200)));` などの属性を設定し、遅延ヒストグラムやフロー分解の粒度を調整する。
- シミュレーション停止前に `Simulator::Schedule(m_simulationDuration - MilliSeconds(1), &NetSim::CheckFlowMonitor, this, flowMonitor);` などの関数を追加し、`flowMonitor->SerializeToXmlFile("OUTPUT/monitor-flow.xml", true, true);` を実行する。ファイル名に `G_nth` などのパラメータを付けると複数試行を管理しやすい。
- FlowMonitor から Goodput を再取得する場合、`auto stats = flowMonitor->GetFlowStats();` をループし、監視端末のインタフェースに該当する `Ipv4FlowClassifier::FiveTuple`（`srcAddress` が監視端末、`dstPort` が動画ポート等）を抽出して `rxBytes` と `timeLastRxPacket - timeFirstTxPacket` から算出する。
- FlowMonitor はデフォルトで全インタフェースを監視するため、不要なフローを除外したい場合は `FlowMonitorHelper::Install()` 呼び出し時に対象 `NetDeviceContainer` を明示するか、XML 出力後に `flowmon-parse-results.py` でフィルタリングする。
