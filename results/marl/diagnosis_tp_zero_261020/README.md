# MARL TP=0 調査（2026-10-20）

## 範囲と実行状態

対象は train80_b8_episodes64_261016、train80_b8_episodes192_261017、
train80_b8_episodes256_261019 の既存 qoe.csv とローカル実装。
学習・再シミュレーション・ソース修正・パラメータ変更は未実施。
baselineとの実測比較も未実施。以下は根本原因の確定ではなく一次診断。

実行した調査:
- `rg -n 'm_marlTpStatus|m_marlTp|demand_unconfirmed' contrib/kameda`
- `sed` による master/applications.cc、master/handover.cc、
  master/config.cc、master/topology.cc、src/flow-monitor/model/flow-monitor.cc の確認
- Python標準ライブラリ csv/json/collections/hashlib による全対象qoe.csv集計

## 再集計用の定義・保存物

- 学習: train_* のみ、cycle_id=2..5、satisfaction <= 1e-6。
- TP対象: app_type=0,1。AP IDは0始まり（0=5G、1/2=Wi-Fi）。
- floor_cases.csv: 検証を含む下限値到達端末の全該当行と出典。
- seed1014_ue43_timeline.csv: checkpointごとの端末43の全周期。
- summary.json: 学習集計と入力CSVのSHA256。解析は乱数不使用。

## 確認できた事実

学習256エピソード、行動後1024周期のうち、下限値を含むのは
56周期（約5.47%）、40エピソード（約15.63%）。
下限値に達した端末・周期は140件。うちTP対象47件は全件raw満足度0、
AP2接続、tp_status=ok。残り93件はRTT対象。
TPゼロ47件中、29件は直前周期から切り替えあり、18件は切り替えなし。
したがって「切り替え直後だけの現象」とは言えない。
AP別曝露数で補正した故障率ではなく、該当件数の集計である。

seed1014・UE43は、checkpoint 0/64では周期2と4、
128/192/256では周期4にAP2でTPゼロとなる。各ゼロ周期は
5G→AP2の切り替え後であり、次周期の5G復帰後にはTPが正に戻る。
同一seed・同一行動乱数系列の比較なので独立な再現試行とは扱わない。

checkpoint256のUE43:

| 周期 | 現接続先 | TP Mbps |
|---|---|---:|
| 1 | 5G | 3.044858 |
| 2 | 5G | 2.113697 |
| 3 | 5G | 1.719139 |
| 4 | AP2 | 0 |
| 5 | 5G | 2.299451 |

周期4のAP2では、他のTP対象7端末は約3.51〜3.85 Mbpsを受信している。
AP2監視RTTもok。ただし監視端末のRTTはUE43自身の到達性の証明ではない。

## TPゼロの意味と計測窓

master/applications.cc の CollectTerminalThroughput は、
端末に対応する宛先IPへのFlowMonitor受信バイト数を集計する。
MARLでは受信バイト数>0 または送信パケット数>0なら observed=true。
接続先が窓内で安定し、observed=trueなら tp_status=ok。
よって今回の「TP=0、ok」は、現行実装に従えば
対応するフローのtxPackets>0、rxBytes=0を意味する。
単なる送信休止を0として評価したケースとは区別される。
ただしIP対応付け・FlowMonitor自体の不具合を完全に排除したわけではない。

設定とスケジューラから再構成:
- 周期4開始: 32.5 s
- 周期3終了・切り替え: 32.3 s
- 新アプリ開始予定: 切り替えの50 ms後、32.35 s
- TP窓: 33.6〜41.3 s（7.7秒）
- 報告・次の判断: 41.8 s

窓内切り替えはMARLのassertで禁止されており、対象runは正常完了。
50 msのアプリ再起動待ちだけでは7.7秒全体の受信ゼロを説明できない。
一方、接続不成立・ARP未解決等が長引く可能性は残る。

## 未確定事項

既存runはflow_per_cycle出力が無効で、UE43のIP/MAC別の送受信・
ドロップ・ARP・Wi-Fi association記録がない。
経路不備、ARP、association、MAC/PHY、キュー損失、受信計測の
どれが原因かはまだ確定できない。epsilonは受信ゼロの原因ではなく、
その影響をHへ強く反映する後段処理。

## 次の検証（未実行）

1. 元の学習バイナリを保持し、隔離した診断用ビルドで
   seed1014/checkpoint256の1エピソードを再現する。
   まず観測・行動が元ログと一致するか検証する。
2. UE43についてフローtx/rx、宛先IP/ルート、ARP、AP2 association、
   MAC/PHYドロップ、アプリ受信を受動記録する。
   FlowMonitorだけでなく実際の受信との照合が必要。
   既存marlRttTraceは監視ICMP向けなのでUE動画の診断を代替しない。
3. 再現した上で、周期3のUE43のみ5Gを維持する対照実験を別runで実施。
   他UEへの影響も保存する。この対照は変更条件なので元runとの
   完全一致を要求しない。

診断に伴う学習条件変更はまだ決定しない。真の通信失敗なら現報酬を
維持した追加学習が可能。計測や通信実装の修正が必要なら修正後の
主実験は別系列で学習し直す。
