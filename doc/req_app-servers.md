## Specification

- Webブラウザの再現（短いTCPフローの連続）（applications.cc/SetBrowserApp()）
    - モジュール: BulkSendApplication（クライアント側）＋PacketSink（サーバ側）、ns3::TcpSocketFactoryを指定。
    - 設定: 1リクエストごとに MaxBytesを固定で750 kBとし、BulkSendApplication をリクエスト単位で生成する。
    - リクエスト間隔は Simulator::Schedule で 1 s 後に次の BulkSend を開始する。総リクエスト数はパラメータ化し、デフォルトは 10 件。
    - HTTP/2 風バーストやサイズ分布の再現は行わず、BulkSend＋PacketSink の構成のみを実装する。
    - アプリ番号は 1（`kBrowserAppId = 1`）のユーザが本アプリケーションを使用する想定。

- オンラインゲーム（applications.cc/SetOnlineGameApp()）
    - 概要: MOBA 系を想定した双方向 UDP アプリケーション。上り／下りとも小パケットを一定周期で連続送信し、低遅延通信を模擬する。
    - モジュール構成:
        - 上り: `ns3::OnOffApplication`（端末）＋`ns3::PacketSinkHelper`（`server_onlineGame`）、`ns3::UdpSocketFactory` を利用。
        - 下り: `ns3::OnOffApplication`（`server_onlineGame`）＋`ns3::PacketSinkHelper`（端末）、`ns3::UdpSocketFactory` を利用。
    - ポートとデータレート:
        - 上り: ベースポート `13000 + 端末インデックス`
        - 下り: ベースポート `13400 + 端末インデックス`
        - 送信条件: `PacketSize = 200` バイト、`Interval = 33ms` 相当、`DataRate ≈ 48kbps`、`OnTime = 1.0`、`OffTime = 0.0`、`MaxBytes = 0`
    - スケジューリング:
        - 開始時刻は 1.0 s（上り／下りとも同時開始）
        - 停止時刻は 7.0 s をデフォルトとし、`m_simulationDuration` が有効な場合はそれに追従
    - 端末ごとの設定:
        - `m_termData[i].use_appli == 4` の端末のみセットアップ
        - 宛先は `GetPrimaryIpv4()` で取得した端末／サーバの第1グローバルアドレスを使用
    - 評価指標:
        - AP 選択に使う要求値は RTT のみ（`APConstants::ONLINE_GAME_REQUIRED_RTT`）
    - アプリ番号は 4（`APConstants::AppType::ONLINE_GAME`）のユーザが本アプリケーションを使用する。
