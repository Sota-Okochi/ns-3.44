# seed 1016・学習後validationのRTT timeout調査

対象：`results/marl/train30_m1_16_8w_v/checkpoint_000016/validation_000005_seed1016/`
比較：同runの `checkpoint_000000/validation_000005_seed1016/`。
解析対象は既存ログとローカルC++実装。再シミュレーション・パラメータ変更は未実施。

## 結論と限界

第4周期のWi-Fi AP1（0始まり）は、計測アプリが実際に1秒超のEcho Replyを複数受信している。
既存証拠は、単なる報告欠測や期限内応答の誤除外ではなく、監視経路の大きな遅延と整合する。
ただし、遅延の発生箇所（無線送信待ち/MAC再送/有線キュー/ARP等）は未確定。
「各UEのアプリRTTも同じ値に悪化した」とは言えない：現在はAP監視端末のRTTを代理指標としている。

## 遅延応答の時系列

周期開始=4+9.5*3=32.5秒、計測開始=33.6秒。
成功送信10回、失敗0回、間隔0.2秒。前3周期も10回送信しており、seq=30..39。
下表の送信時刻は設定・スケジューリング実装から再構成した値であり、個別送信traceではない。
受信時刻はconsole.logの丸め表示。従ってRTTは概算。

|seq|送信時刻(s、再構成)|受信時刻(s)|概算RTT(ms)|
|---:|---:|---:|---:|
|30|33.6|35.1290|1529.0|
|31|33.8|35.1318|1331.8|
|32|34.0|35.8594|1859.4|
|33|34.2|35.9290|1729.0|
|36|34.8|36.6307|1830.7|
|37|35.0|36.6332|1633.2|

6応答は実際に届いているが、いずれも1秒未満の条件を満たさない。
残り4応答は既存ログから所在を確定できず、「永久に失われた」とは断定しない。
最終プローブ送信=35.4秒、その期限=36.4秒。
timeout報告完全受信=36.5005秒、報告期限=38.5秒で、期限内に受理されている。
報告のlate=4は報告時点までの値。その後の2応答はconsole.logにのみ記録され、評価は遡及変更しない。

## 実装の確認

- APMonitorTerminal::SendMarlPing：各成功送信時のSimulator::Nowを保存し、送信から1秒後に期限イベント。
- HandlePingReply：ICMP Echo Replyと識別子・sequenceを照合し、正のRTTかつ1秒未満のみ有効。
- MarlDeadline：有効応答がないsequenceをexpiredへ移す。時間切れ後の応答はlate扱い。
- FinishMarlReport：全10回の送信試行と、成功送信の応答/期限切れの解決後に確定。
- StartContinuousMonitoring：周期ごとの状態を初期化。sequenceは周期間で継続する。
- 第4周期の切り替えは32.3秒、計測開始33.6秒、計測終了38.5秒。
  今回の遅延応答を周期末リセットが1秒未満で破棄した、という時系列ではない。
- AttachMonitorApplication：測定先は `server_rtt`、報告先はremote host。
  Wi-Fi監視端末は固定配置・各AP固定接続で、UE切り替え処理の対象ではない。

これらの照合で直ちに修正すべき期限判定バグは発見していない。
ただし、パケットtraceなしで下位層を含む実装全体の正当性まで証明したものではない。

## 学習前との対比

第3回判断で、UE0（動画）の選択が学習前AP2→学習後AP1へ変化。
他29端末の当該行動は同一。第4周期のAP1人数は12→13、動画人数は7→8。
AP1のTP対象端末平均は4.4168→3.9757 Mbps（端末集合も変わるため純粋な同一群比較ではない）。
学習前AP1平均RTTは86.1627 ms、学習後はtimeout。
第5周期の割り当て配列は学習前後で同じだが、AP1 RTTは81.3741→136.1849 ms。
負荷・キュー・通信状態の履歴の影響が疑われるが、乱数消費順の変化も含め、原因を単一には断定しない。

## QoEへの増幅

同APのゲーム2台(UE11,17)・音声1台(UE24)に共通timeoutを適用し、S_eff=1e-6。
第4周期Hは9.999886e-6。この低下はHの計算ミスではなく、監視RTTの共有とepsilon規約の帰結。
AP平均RTTを代理利用するモデルの妥当性と、計測実装の正しさは分けて検討する。

## 次の切り分け

既存ログにはpcapや当該経路のキューtraceがないため、それ以上の遅延箇所の断定はできない。
必要な追加診断は、同一checkpoint・seed・行動乱数での1 episode再実行と、
各ICMP送受信の正確な時刻/sequence/アドレス、監視STA/AP/server_rttのIP trace、
関連するWi-Fi MAC再送/キュー、有線キューの記録。
イベントやトラフィックを追加しない受動traceに限定し、元runの行動・Hとの一致を先に確認する。
第3回判断のUE0だけをAP2に戻す対照実験は、別の診断実験として区別し、学習データに混ぜない。

## 追加実測：完全再現と遅延区間の特定

上記は既存ログ段階の調査。以下はその後に実行した追加診断であり、
「追加実行未実施」「遅延区間未確定」という上記の状態を更新する。

### 1. 元のv4バイナリで再現

```bash
python3 scripts/diagnose_marl_rtt.py \
  --source-episode results/marl/train30_m1_16_8w_v/checkpoint_000016/validation_000005_seed1016 \
  --checkpoint results/marl/train30_m1_16_8w_v/latest.pt \
  --binary build/marl-ppo-v4/master --output results/marl/replay1016_v4
```

正常終了、約1144.7秒。`comparison.json` に記録したとおり、observations.jsonl、
transitions.jsonl、qoe.csvは元runとバイト単位で完全一致。5周期のH・4回の行動とも再現した。

### 2. 受動トレース付きで再現

`master/queue-diagnostics.cc` にICMP識別子0xa000〜0xa002のみを対象とする受動callbackを追加。
IPv4 Tx/Rx、Wi-Fi MAC/queue/PHY送信、P2P MacTx/PHY送受信を記録する。
`--marlRttTrace=1` を指定した診断時のみ有効。トラフィック・乱数抽選・定期イベントは追加しない。
A-MSDUはMAC内部の解析対象外で、IPv4 traceは残す。

```bash
python3 scripts/build_marl_isolated.py --output build/marl-ppo-v5-trace-b --jobs 1
python3 scripts/diagnose_marl_rtt.py \
  --source-episode results/marl/train30_m1_16_8w_v/checkpoint_000016/validation_000005_seed1016 \
  --checkpoint results/marl/train30_m1_16_8w_v/latest.pt \
  --binary build/marl-ppo-v5-trace-b/master --trace --output results/marl/trace1016_v5
```

正常終了、約1152.3秒。この実行も上記3ファイルが元runとバイト単位で完全一致。
最初のtraceビルドはcallback bound値の比較演算子不足で失敗し、追加後に別隔離先で成功。
既存v4バイナリ・checkpoint・学習結果は変更していない。

### 3. 遅延の主成分

監視STA=Node5、AP1=Node2、AP1側router=Node38、RTTサーバー=Node42。
返信はrouter38のIPv4 interface2（10.0.2.2）からP2P device0を経てAP1へ向かう。

最初のプローブseq30（送信33.600000000秒、受信35.128991522秒）：

|区間|所要時間(ms)|
|---|---:|
|監視STA→RTTサーバーの往路|15.755527|
|RTTサーバー→AP1側routerの送出処理まで|1.034440|
|router IPv4 Tx→P2P MacTx（デバイスへ渡る前）|**1447.686745**|
|P2P MacTx→PhyTxBegin（デバイス側待ち）|49.151400|
|P2P送信開始→AP1受信|14.246000|
|AP1受信→監視STA受信|1.117410|
|合計|1528.991522|

6個の受信できた応答全体では、router IPv4 Tx→P2P MacTxの間が1250.594〜1769.096 ms。
Wi-Fiを含むAP1→監視STAの区間は0.116〜1.383 ms。
遅延主成分はWi-Fi無線再送ではなく、**AP1側routerの有線下り出口で、P2Pデバイスへ渡される前の滞留**。

コードの経路確認：PointToPointNetDevice::NeedsArp()はfalse、Ipv4Interface::Sendは
TrafficControlLayer::Sendへ渡すため、この長い待ちはARPではなくTraffic Control側の送出待ち区間。
単一Tx queueへの既定qdisc導入はIpv4AddressHelperとTrafficControlHelperで行われ、ソース上はFqCoDel。
ただしqdisc内の個々の内部queueや廃棄理由までは今回のtraceで記録していないため、
AQMの内部挙動や他フローとの競合割合まで断定しない。

残り4個(seq34,35,38,39)もRTTサーバー到着と返信のrouter38 IPv4 Txまでは記録されるが、
P2P MacTx以降の記録はない。今回のtraceにはqdisc Drop理由がないため、廃棄理由は未確定。
計測timeoutは実際のネットワーク内の待ちと整合し、期限を緩めて隠す修正は行わない。

再計算用：

```bash
python3 scripts/analyze_marl_rtt_trace.py \
  results/marl/trace1016_v5/validation_000005_seed1016/simulation/30/marl_ppo/marl_rtt_path.csv \
  --output results/marl/trace1016_v5/delay_breakdown.csv
```

ノード番号はこのrun専用。別端末数/トポロジでは `marl_trace_nodes.csv` を確認し、
解析引数のmonitor/server/router/AP nodeを変更する。

### 4. 他seed・端末数への解釈

同じ待ち行列を使う以上、他seedや他端末数でも起こり得る。
端末増加で当該APの送信需要が増えるなら発生リスクが増す可能性があるが、
アプリ構成・割り当て・TCP状態・容量によって変わり、端末数だけで単調性は主張しない。
今回の調査は30台の1条件のみ。80/100台の発生頻度やPPOの優劣は未検証。
CPUの壁時計時間の増加自体をシミュレーション時刻のRTT増加と混同しない。

確認コマンド：`python3 -m unittest discover -s tests -p 'test_marl*.py' -q`（13件成功）、
Python構文確認、`git diff --check`。学習更新・実験条件の変更は行わない。
