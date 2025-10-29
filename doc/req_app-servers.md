## Specification

- Webブラウザの再現（短いTCPフローの連続）（applications.cc/SetBrowserApp()）
    - モジュール: BulkSendApplication（クライアント側）＋PacketSink（サーバ側）、ns3::TcpSocketFactoryを指定。
    - 設定: 1リクエストごとに MaxBytesを固定で750 kBとし、BulkSendApplication をリクエスト単位で生成する。
    - リクエスト間隔は Simulator::Schedule で 1 s 後に次の BulkSend を開始する。総リクエスト数はパラメータ化し、デフォルトは 10 件。
    - HTTP/2 風バーストやサイズ分布の再現は行わず、BulkSend＋PacketSink の構成のみを実装する。
    - アプリ番号は 1（`kBrowserAppId = 1`）のユーザが本アプリケーションを使用する想定。

- ビデオ会議（applications.cc/SetWebmeeting()）
    - 概要: 音声（低レイテンシ）と映像（中ビットレート）を同時に扱う双方向アプリケーション。端末は映像・音声を MCU 役の `server_live` へ送信し、`server_live` は各端末に再配信する。実環境を厳密に再現する必要はないため、抽象化した 2 本の UDP フロー（映像／音声）でビデオ会議らしいトラフィックを再現する。遅延監視のために RTT 計測も並行実施する。
    - モジュール構成:
        - 映像アップリンク: `ns3::OnOffApplication`（端末）＋`ns3::PacketSinkHelper`（`server_live`）、`ns3::UdpSocketFactory` を利用。
        - 映像ダウンリンク: `ns3::OnOffApplication`（`server_live`）＋`ns3::PacketSinkHelper`（端末）。アップリンクとは別ポートを使用。
        - 音声往復（抽象化）: `ns3::UdpEchoClientHelper`（端末）＋`ns3::UdpEchoServerHelper`（`server_live`）。RTT 計測は `Config::ConnectWithoutContext("/NodeList/*/ApplicationList/*/$ns3::UdpEchoClient/Rtt", …)` で収集する。
    - ポートとデータレート:
        - 映像アップリンク: ベースポート 13000 + 端末インデックス。`PacketSize = 1200` バイト、`DataRate = 1.2Mbps`、`OnTime = Constant(0.95)`、`OffTime = Constant(0.05)`、`MaxBytes = 0`（無制限）。
        - 映像ダウンリンク: ベースポート 13400 + 端末インデックス。`PacketSize = 1200` バイト、`DataRate = 1.5Mbps`、`OnTime = Constant(1.0)`、`OffTime = Constant(0.0)`、`MaxBytes = 0`。
        - 音声: ベースポート 14000 + 端末インデックス。クライアントは `Interval = 20ms`、`MaxPackets = 0`（無制限）、`PacketSize = 160` バイト。サーバ応答パケットサイズも 160 バイトで統一する。
    - スケジューリング:
        - 全アプリケーションの開始時刻は 1.0 s。音声応答の揺らぎを抑えるため、映像ダウンリンク開始のみ `Simulator::Schedule(Seconds(1.05), …)` で 50 ms 後ろにずらす。
        - 停止時刻は 7.0 s とし、テスト用途では `m_simulationDuration` と連動できるようパラメータ化する。
    - 端末ごとの設定:
        - `m_termData[i].use_appli == 4` の端末のみセットアップする。
        - 上り・下り双方の `InetSocketAddress` は `GetPrimaryIpv4()` で取得した端末／サーバの第1グローバルアドレスにバインドする。
        - 映像ダウンリンクを端末に届けるため、端末側 `PacketSink` には `Ipv4Address::GetAny()` を指定。
    - ログ・統計:
        - RTT 応答は `NS_LOG_INFO("Webmeeting RTT", …)` で RemoteHost 出力にまとめ、AP 選択用の RTT フィードバックに流用する。
        - 映像トラフィックは `AsciiTraceHelper` を使い `webmeeting_video-uplink.tr` / `webmeeting_video-downlink.tr` にダンプする（必要に応じて有効化）。
    - アプリ番号は 4（`APConstants::AppType::LIVE_STREAM`）のユーザが本アプリケーションを使用する。
