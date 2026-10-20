# seed1014 / UE43 診断結果

## 実行と再現性

run3は終了コード0。実行時間6182.75秒。
comparison.jsonで observations.jsonl / transitions.jsonl / qoe.csv は元の
checkpoint256検証と全てバイト一致。診断による観測結果・行動の変更はない。

集計コマンド:
```
python3 results/marl/replay1014_ue43_261020_run3/analyze_ue43.py
```
出力: analysis_summary.json、cycle4_arp_evidence.csv。乱数不使用。
本解析では学習・再実行・シミュレーションコードの変更を行っていない。

## 確定できた直接的な失敗箇所

MARL周期4の計測窓は33.6〜41.3秒。UE43=node50、AP2=node3。
UE43のAP2側IPは10.1.2.46、MACは00:00:00:00:00:d9。

- サーバ側の対象UDPフロー: 6771 packets、7122466 bytes送信、受信0。
- AP2へ届いた対象UDPパケット: IPv4 Rxで3524件。
- AP2で対象UDPのARP Drop: 3497件。
- AP2のARP request: 36件。
- UE43は36件ともMACで受信し、ARP replyを36件送信。
- その36件全てが、同一packet UIDのAP2 MacRxDropとして記録される。
- UE43宛の対象IPv4 Rxは計測窓内0。

ARP replyがAP2のMACで破棄され、アドレス解決が成立せず、
下りデータがARP段階で失われるという証拠がある。
単なるアプリ送信休止やFlowMonitorだけの見かけのゼロではない。
全6771件が同じ原因で落ちたとまでは主張しない。

例（同一UID 2313260）:
- 33.659203720: UE43がARP replyをMacTx
- 33.659306736: AP2がそのARP replyをMacRxDrop

## 根本原因の有力候補と未確定点

UE側のAP2 Assocは1.243473164秒に記録され、その後DeAssocはない。
APからのARP broadcastを受信してreplyまで生成している。
一方、APはそのreplyをMACで破棄している。

実行バイナリのmanifestと一致するap-wifi-mac.ccを確認:
ApWifiMac::Receiveでは、通常の上りデータ受理条件に
IsAssociated(送信元)、ToDS/FromDS、受信先MACの一致が含まれる。
従って「STA側は接続済みだがAP側では未接続扱い」という状態不整合が
有力候補。ただし今回のトレースはAP側IsAssociatedや破棄分岐の条件値、
association response/ACKの履歴を保存していない。
MACヘッダ不整合等との切り分けは未完了で、ACK失敗やns-3バグを断定しない。

## 診断ログ自体の注意

- flow_per_cycle.csvのcycle列はMARL周期と1ずれる。
  time_s=41.30の行を周期4として解析する。UE43はterm_idx=44。
- FlowMonitor lost_packetsは前の窓で送ったパケットの遅延判定を含むため、
  tx-rxの厳密な同窓内損失内訳としては使わない。
- ue43_sink.csvは再インストールごとの複数ofstreamが同じファイルへ
  バッファ付き追記する診断実装であり、少なくとも1行の破損と重複行がある。
  厳密な受信件数の根拠には使用しない。パース可能行の周期4受信は0だが、
  結論はIPv4 RxとFlowMonitor、および同一UIDのMACイベントに基づく。
  次回は共有ストリーム1本に統一する必要がある。
- 受動MACログはA-MSDUを網羅しない。今回のARPは個別にUID対応が取れている。

## 学習への判断

epsilonを変更する根拠ではない。今回のTPゼロは実際の配信失敗と整合する。
しかし、その配信失敗が妥当な無線挙動か、接続状態管理の不具合かは未確定。
追加学習を急ぐより、AP/STA接続状態の確認を優先する。
原因未確定の段階でモデルを初期化する必要もない。

次の検証（未実行）:
1. AP側のUE43 association状態とARPフレーム破棄分岐を受動記録。
   association responseの送信成功/失敗・ACKとSTA側状態を時系列で照合。
2. 原因に沿った最小修正を別ビルドで試験し、再現seedでARP成立とTPを確認。
   状態や観測を変える修正なら元runと完全一致する必要はない。
3. 他のTPゼロseedでも検証。現在の1例から全47件の原因を同一とは断定しない。
