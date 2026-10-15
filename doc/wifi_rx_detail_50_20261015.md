# 50端末Wi-Fi受信詳細計測の分析

入力: `results/perf/wifi_rx_detail_50_20261015/`。seed384、modelseed1、policy0、動的容量、1worker。
正常終了、4遷移、15件のAP内部通知が期限内受理。PPO更新なし。
実時間2675.300秒（44分35秒）、最大RSS807248 KiB（約788 MiB）。総イベント462466291。

## 全期間（inclusive、加算禁止）

| 処理 | 秒 | Simulator::Run比 |
|---|---:|---:|
| Simulator::Run | 2674.738 | 100% |
| MultiModelSpectrumChannel::StartRx | 1501.219 | 56.13% |
| SpectrumWifiPhy::StartRx | 932.900 | 34.88% |
| NR受信電力/伝搬計算 | 450.956 | 16.86% |
| MultiModelSpectrumChannel::StartTx | 159.583 | 5.97% |

Wi-Fi受信46241751回。標本数45158（1024受信ごと）。

## 同じ受信標本の同期呼び出し内訳

| 対象 | 標本累積秒 | 標本あたり平均µs |
|---|---:|---:|
| HE RU帯域処理 | 0.703 | 15.570 |
| preamble開始全体 | 0.748 | 16.553 |
| その中のDoGetEvent | 0.708 | 15.672 |
| その中のAppendEvent（干渉履歴更新） | 0.665 | 14.718 |
| PHY状態分岐 | 0.021 | 0.460 |
| 受信電力格納領域/レイアウト | 0.024 | 0.527 |
| Event生成自体 | 約0.0016 | 0.035 |

AppendEventはDoGetEventの93.91%、preamble開始の88.91%（標本inclusive時間）。
従ってイベントオブジェクトの生成自体ではなく帯域ごとの干渉履歴操作を優先する。
標本計測の時計取得などのオーバーヘッドがある。0.665秒を1024倍して全期間の
AppendEvent時間と断定しない。RUの細粒度タイマは多数呼ばれ、特に計測負荷に注意。

## 次の修正候補

第一候補: `src/wifi/model/interference-helper.cc` の `AppendEvent`。
- Eventのstart/endTime取得とfreqRangeの受信状態検索を帯域ループ外へ移動可能か検証。
- `m_rxing.contains` と `at` の二重検索を1回のfindで処理する候補。
- 各帯域のm_niChanges.find、時刻境界探索、古い履歴のerase、新規insert、AddPowerの
  どれが支配的かはまだ分離できていない。大きな構造変更は追加計測後。
- 同一時刻ノードの挿入順、干渉電力加算順、iterator有効性は維持する。

第二候補: HE RU帯域の積分/ループ。共通レイアウト等は既に最適化が存在するため
既存設計を壊さない。prefix-sum等で加算順を変える案は丸め差を生むので別扱い。

最初は低リスクなループ不変量の外出しを小さく実施して50端末A/Bで評価する。
今回の50端末結果が修正前baseline。観測・行動・QoE・イベント数一致を確認後、
計測なしの時間比較を行う。効果が小さければ効果が小さいと報告し、追加分解へ進む。
非選択PHYの停止や干渉削除は行わない。最終的には80端末でも再検証する。

80端末の約101分と50端末の約45分との差は端末数/条件/計測負荷の差であり、
リファクタリングの改善率ではない。この時点では最適化の実装はまだ行っていない。

## 第一段階の実装・A/B計測

`AppendEvent`のstartTime/endTime取得、時刻整合性assert、freqRangeに対する
m_rxing検索を帯域ループ外に移した。contains+atをfind1回に統合。
帯域ループ中はm_rxingとEvent時刻を変更しないことをコードで確認。
履歴の探索・削除・挿入順、電力加算順、RNG、予約イベントは変更していない。

変更前ソースを`results/perf/wifi_append_hoist_50_20261015/interference-helper.before.cc`へ保存。
独立ビルド: `python3 results/perf/wifi_append_hoist_50_20261015/build.py`。
通常ビルドと既存baselineバイナリは上書きしない。
小規模テストは`tests/test_wifi_rx_detail.py`のBASEを`build/wifi-rx-detail-v1`、
AFTERを`build/wifi-append-hoist-v1`へ指定して実行（test.logに記録）。

50端末の計測出力先: `results/perf/wifi_append_hoist_50_20261015/`。
比較元: `results/perf/wifi_rx_detail_50_20261015/`。
seed384・モデルseed1・未学習モデル・episode45・動的容量・1worker・1024受信ごと標本。
完了後にsummary.txt、comparison.json、timing_comparison.jsonを自動生成する。
学習更新なし。観測・遷移・QoE・周期イベント数の一致が確認されるまでは
研究結果を維持する高速化が成功したとは扱わない。速度差は単一seedかつ計測負荷込み。

### 第一段階の計測結果：同値性は確認、高速化は確認できず

`wifi_append_hoist_50_20261015/comparison.json`はcompletedと全一致判定がtrue。
observations.jsonl/transitions.jsonl/qoe.csvはバイト一致、周期境界の時刻とイベント数も一致。
平均H(cycle2–5)=0.30179230176029526を維持。

| 指標 | 修正前 | 修正後 | 時間増加率 |
|---|---:|---:|---:|
| プロセス実時間 | 2675.300秒 | 2791.528秒 | 約4.34% |
| Simulator::Run | 2674.738秒 | 2791.021秒 | 4.35% |
| Wi-Fi StartRx | 932.900秒 | 972.815秒 | 4.28% |
| AppendEvent（標本のみ） | 0.664617秒 | 0.699735秒 | 5.28% |
| NR伝搬計算（今回未変更） | 450.956秒 | 463.654秒 | 2.82% |

今回の単一A/Bでは116秒程度遅く、速度改善とは報告しない。NRなど未変更の関数も
遅くなっており、CPU負荷/周波数等の実行条件差とコード変更の影響は分離できない。
外部負荷が原因だと断定することも、修正による回帰だと断定することもできない。
現時点では速度改善実証済みの版として採用しない。元バイナリを保持し、今回の小修正は
候補のままとする。大規模な長時間反復より先に、AppendEventの帯域find・時刻境界探索・
erase・insert・AddPowerを標本受信に限定して分解するのが次の候補。
同じ条件の短時間反復/マイクロベンチマークで有望性を確認後、50端末の計測なしA/Bへ進む。
本分析では追加コード変更や新たな実行・ロールバックはしていない。

## AppendEventの5項目分解（標本受信限定）

`WifiRxSampleActive()`のときだけ帯域ごとに以下を計測:
`append.band_find`（帯域検索）、`append.time_bounds`（開始/終了時刻境界検索）、
`append.erase`（古い履歴削除、!rxingの帯域のみ）、`append.insert`（開始/終了ノード挿入）、
`append.add_power`（電力加算ループ）。接頭辞は`WifiRx.sampled.`。
`append.empty_reference`を空の計測スコープの参考値として追加。
非標本受信では追加の時計取得・統計lookupは実行しない。操作順を維持。
空スコープ時間は実測の一部で、機械的に差し引いて正確な補正とは扱わない。
親AppendEvent時間は細粒度計測の負荷を含むため前版との速度比較には使わない。
標本の帯域数とerase呼出数は異なり得る。合計時間/回数/平均時間を併記して分析する。

独立ビルド成功:
`python3 results/perf/wifi_append_parts_50_20261015/build.py`
テスト成功:
`python3 -m unittest discover -s tests -p test_wifi_append_parts.py`
2seedで前版との受信bytes/イベント数/デバイスカウンタ一致、5項目と空計測の出現を検証。

50端末・seed384・動的容量・policy0・1worker・1024受信ごとに標本を取る計測を開始。
保存先`results/perf/wifi_append_parts_50_20261015/`。
終了後`summary.txt`、`comparison.json`、`append_parts.csv`を生成。
比較対象は`wifi_append_hoist_50_20261015`。結果の一致を検査する。
新しい高速化の導入ではなく診断追加。計測完了までは支配的操作の確定を保留する。

### 5項目分解の結果

正常終了（2805.561秒、46分46秒）。comparison.jsonは観測/遷移/QoEのバイト一致と
境界イベント数一致がすべてtrue。H(cycle2–5)=0.30179230176029526。
標本受信の帯域別計測（五項目合計1203.108334 ms。全実行時間の割合ではない）:

|項目|標本合計ms|回数|平均ns|五項目内割合|
|---|---:|---:|---:|---:|
|開始/終了時刻境界検索|563.367|4498894|125.22|46.83%|
|帯域検索|249.161|4498894|55.38|20.71%|
|開始/終了ノード挿入|171.413|4498894|38.10|14.25%|
|古い履歴削除|155.121|4401166|35.25|12.89%|
|電力加算|64.047|4498894|14.24|5.32%|
|空スコープ参考値|57.428|4498894|12.76|集計対象外|

時刻境界検索+帯域検索が五項目の約67.5%。第一の対象は時刻境界検索。
GetNextPositionはmultimap.upper_boundを呼び、AppendEventでは開始/終了について計2回。
挿入時には既にこのiteratorをhintとして再利用しており、同じ最適化を再提案しない。
電力加算は空スコープ参考値に近く正確なコスト分離が困難。ここを優先しない。
親AppendEvent標本時間は0.700秒から2.391秒になったが、追加計測の管理/時計取得負荷を
含むため「元処理が3倍遅くなった」と解釈しない。5項目を1024倍して全期間時間ともしない。

過去のdoc/wifi_time_cursor.mdとwifi_cursor_rollback.mdでは永続時刻カーソルが
2seedで6.42%/9.11%遅くなり撤回済み。単純再導入はしない。
次は履歴件数・開始/終了探索それぞれの短時間テストで改善候補を比較し、性能見込みを
確認してから実シミュレーションへ適用する。第二候補は帯域キーの繰り返しfindを
レイアウト対応情報で削減する方法だが、AddBand/RemoveBand/UpdateBands時の無効化が必要。
干渉履歴の同時刻順・電力の加算順・実際の受信イベントは維持する。
今回の分析では新しい最適化や長時間測定は開始していない。
