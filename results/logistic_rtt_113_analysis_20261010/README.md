# logistic AP0の113ms停滞：観測と仮説

2026-10-10分析。設定変更・追加実験なし。

## 読み取り対象
- OUTPUT/100/logistic/master_log_1001_20261009_210519.csv
- OUTPUT/80/logistic/master_log_1002_20260910_145710.csv
- OUTPUT/80/logistic/master_log_1003_20260910_145733.csv
- OUTPUT/80/logistic/master_log_1008_20260910_145802.csv
- OUTPUT/80/logistic/master_log_1014_20260910_145848.csv
- OUTPUT/100/logistic/queue_diagnostics/logistic_seed1001_1791560160825745_pid200299/queues.csv
- results/queue_diagnostics_validation/smoke.log （3端末の低負荷比較）

80端末ログは過去の別seed・実行日時であり、実行バイナリ/全設定の同一性までは保証しない。
100端末の元の完了ログではcycle2〜5のAP0 RTT=113ms、AP0は動画48台。
80端末ログのcycle2以降はAP0台数30/36/35/32台でも113ms。

## 新しいlogistic診断の途中観測
分析時点で0〜17.4秒、35848行。対応する新masterはcycle1のみ。
旧実行のcycle2以降のRTTと新実行の途中キュー観測を同時実測として扱わない。
14.6〜17.4秒のDLデバイスキュー29サンプルはすべて600パケット。
滞留量630400〜632400 bytes、平均632216.7931034482 bytes。
同窓のDL RLC HOL最大0.49387ms。QueueDiscも大きく滞留しており、混雑は消えていない。

## 数値的に整合する説明（主仮説）
動画のUDPペイロード上限1024 bytes + UDP8 + IPv4 20 + PPP2 = 1054 bytes。
600 * 1054 = 632400 bytesは診断値と一致する。
632400 * 8 / 80000000 * 1000 = 63.24 ms。
低負荷比較実行のAP0 RTTは約50ms。従って50 + 63.24 = 113.24ms。
APMonitorTerminal::OnRttMeasuredはTime::GetMilliSeconds()で整数msへ切り捨て、
ReportRTTToServerは末尾最大6応答を平均する。
この組み合わせで113.000000msが反復することは説明可能。
ただし同一パケットのキュー入出時刻による分解は未測定なので、数値一致は因果の完全な証明ではない。

FqCoDelは上位でフロー別に扱うが、デバイス側FIFOの既存600パケットを飛び越せない。
端末が増えても固定サイズFIFOの飽和待ち時間は約63msのまま。
追加負荷は上位QueueDiscの滞留/破棄や端末当たりTPへ現れる可能性がある。
113msはプログラムのRTT上限ではなく、全トラフィック条件での上限でもない。
測定経路に113msのクリップは見つからず、randomログでは113ms超過を実際に記録。

## 未確認
- 新logistic診断実行の完了後のRTTとキューの同時照合。
- 80端末の同じ診断（過去CSVにはキューなし）。
- ping個別のデバイスキュー/QueueDiscのsojourn time、送受信成功数。
- 大幅遅延/喪失した応答が平均から外れる影響。タイムアウト時は受信済みサンプルを報告する。

## 次の切り分け実験候補（未実施）
帯域80Mbps・伝搬遅延・QueueDisc上限を固定し、デバイスFIFOだけ600→300p。
主仮説なら追加待ちは約31.62ms、合計RTTは概ね82msへ低下する見込み（予測であって結果ではない）。
