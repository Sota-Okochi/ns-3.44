# AP0のバックホール・RLCキュー診断（ns-3.44）

通信条件を変更せず、PGW–CERの送信キュー、同リンク上のroot QueueDisc、
NR UMデータベアラの送信バッファを周期的に読み取る。標準では無効。
既存のmaster_logの列・計算方法は変更していない。外部ライブラリの追加なし。

## 実行

`data/setting.json` の terminals が100であることを確認して実行する。
既に走っているプロセスや既存CSVに診断を後付けすることはできない。

```bash
./ns3 build master -j 2
./ns3 run 'master --method=random --rngSeed=1001 --queueDiagnostics=1 --queueSampleSec=0.1' --no-build
./ns3 run 'master --method=random --rngSeed=1002 --queueDiagnostics=1 --queueSampleSec=0.1' --no-build
```

`--settingPath=/path/to/setting.json` で別の設定JSONを指定できる。
省略時は従来の `data/setting.json`。診断ON/OFFどちらでも使用可能。
研究パラメータ（帯域、遅延、バッファサイズ、アプリ負荷）は本機能で変更しない。

出力先（起動時にも表示）:

```text
OUTPUT/<端末数>/<手法>/queue_diagnostics/<method>_seed<seed>_<timestamp>_pid<pid>/
  queues.csv
  setting.json
  metadata.txt
```

metadataには実行引数、seed、RNG run、計測間隔、サイクル時間・オフセットを保存する。
リンク速度・伝搬遅延・キュー上限は実行時属性からCSVに保存する。
master_logとの対応は同じ実行のseed、手法、起動時ログで確認する（run_idは診断独自）。

```bash
python3 scripts/summarize_queue_diagnostics.py \
  OUTPUT/100/random/queue_diagnostics/<run_id>/queues.csv \
  --output /tmp/queue_summary.csv
```

## 列の意味

- `layer`: `p2p_device` / `p2p_qdisc` / `nr_rlc_um`。
- `direction`: `dl` はCER→PGW側の送信、またはgNB側RLC。
  `ul` はPGW→CER側の送信、またはUE側RLC。
- `source`: ノード・デバイス・ベアラを特定するConfigパス。
- `ue_id`: master_logと同じ1始まり。監視端末やリンク全体は `-1`。
  RLCは `imsi` と `rnti` も記録する。現在の単一gNB構成を対象とする。
- `current_bs_id`: 通常端末の実際の接続先（0始まり）、対象外は `-1`。
  Wi-Fiへ切り替えた端末のNRバッファも残存分を観測する。
- `sim_time`: 秒。`cycle_window_id` は設定上の時間窓
  `[offset + (k-1)*duration, offset + k*duration)` のk。
  warm-upおよび全サイクル後の時間は0。**制御ノードの判定イベント番号ではない**。
- `queue_entries`, `queue_bytes`: サンプル時点の滞留量。
  RLCのentriesには分割後の残りSDUも含む。送信中・HARQ・受信再構成バッファは含まない。
- `hol_delay_ms`: RLC先頭要素の待ち時間。分割後も元の到着時刻を使用。
  空のRLCは0、P2P/QueueDiscは未計測なので空欄。
- `dropped_packets_total`, `dropped_bytes_total`: オブジェクト作成以降の累積値。
  RLCは**容量超過で受け付けなかったSDUのみ**。無線損失、HARQ失敗、PDCP期限通知は含まない。
  現状のシナリオはPDCP discarding無効。
- `capacity`: 上限（p=パケット、B=bytes）。RLCの約1 GBもそのまま記録する。
- `link_bps`, `channel_delay_ms`: P2Pの帯域と片道伝搬遅延。RLCでは空欄。

各サンプルをflushするため、途中停止でも保存済み部分を解析可能。
生成前のRLCには行がなく、未生成とゼロ滞留を混同しない。
未対応のRLCモード（AM等）はゼロと記録せずエラーで停止する。

## 判定の目安と限界

1. RTT増大時に `p2p_device` / `p2p_qdisc` のDL滞留が続き、RLC待ち時間が小さい:
   バックホールの待ちが疑わしい。
2. P2P側の滞留が小さく、RLCのbytesとHOLが増加:
   NR側の送信待ちが疑わしい。
3. 両方増加: 複合要因。帯域変更前に同じ時間帯・割り当てで比較する。

P2Pキューbytesから `bytes * 8 / link_bps` で排出所要時間を概算できるが、
実測のパケット遅延ではない。QueueDiscとデバイスキューは分けて見る。
実測RTTは監視端末の経路なので、RLCも全端末集計だけでなく監視端末（ue_id=-1）を見る。

0.1秒間隔のスナップショットは短いピークを取り逃がす。必要なら0.01秒にして再確認するが、
イベント・I/O・Config検索の負荷と出力量は増える。乱数は使用しないが、実行時間は増加し得る。
累積破棄数を時間方向に足してはいけない。集計スクリプトは同一時刻・層・方向でのみ合算する。
同じsourceの差分が区間破棄数。ベアラ再生成や消滅がある場合は累積値のリセット・消失に注意。
作成から破棄までがサンプル間に完結したベアラは観測できない。
今回の固定gNB・NRベアラ維持型切り替えを対象とし、ベアラライフサイクル全般の追跡は未実装。

## 検証コマンド

```bash
python3 -m unittest discover -s tests -p test_queue_diagnostics.py -v
./ns3 run 'master --method=random --settingPath=results/queue_diagnostics_validation/setting.json --queueDiagnostics=1 --queueSampleSec=0.1' --no-build
./ns3 run 'master --method=random --settingPath=results/queue_diagnostics_validation/setting.json --queueDiagnostics=0' --no-build
```

小規模設定はseed=19001、3端末、1サイクル。元の100端末設定を変更せず別ファイルで実施。
RLCの空バッファ、投入、容量超過、HOL、分割送信、排出、集計の単体テストを実施。
ON/OFF比較はmaster_logの `assignment_compute_ms`（実時間）を除いた全列を比較する。
100端末での診断実験・ボトルネック判定は別途実施する。

### 今回の確認結果（2026-10-09）

- ビルド成功。初回はccache一時ディレクトリのサンドボックス制限で失敗し、許可された環境で再実行した。
- 単体テスト2件成功（DropTailキューの容量超過・累積破棄、RLC各状態、CSV集計）。
- 3端末・1サイクルのON/OFF実行が完走。実時間列以外のmaster_log全列が一致。
- 診断CSVは2224行。両方向のデバイスキュー・QueueDisc・RLC、通常端末と監視端末の識別を確認。
- 同時刻・同一sourceの重複なし。CSV集計スクリプト成功。
- 詳細は `results/queue_diagnostics_validation/validation.json` と同ディレクトリの実行ログ。
- 100端末条件の実行および因果関係の判定は未実施。
