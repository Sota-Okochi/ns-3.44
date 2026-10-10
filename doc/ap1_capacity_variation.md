# Wi-Fi AP1バックホール容量変動（ns-3.44）

共有上流回線等による利用可能サービス容量の変化の抽象モデル。
無線品質劣化・背景通信・物理障害を直接再現するものではない。
既定は無効。既存のdata/setting.json、他リンクの帯域・遅延・キューは変更しない。

## 条件

専用設定: `data/scenarios/ap1_capacity_80_seed1001.json`
80端末、seed 1001、5周期、9.5秒/周期、cycle 1基準時刻4秒。
既存のアプリ出現確率と初期AP選択を使用する（アプリ別人数を固定しない）。
`--method=no_switch --mob=1` で接続先・位置を固定。
Wi-Fi AP1（内部インデックス1、1始まり接続先ID 2）のAP–ルータのみ、
両端のPointToPointNetDevice送信速度を変更する。

|cycle|AP1 Mbps|
|---|---:|
|1|40|
|2|20|
|3|20|
|4|40|
|5|40|

13.5秒で低下、32.5秒で回復。キュー・アプリをリセットしない。
送信開始済みパケットの完了イベントは再計算しない。
変更イベントは周期開始を基準に予約し、サンプリングより先に登録する。
回復時の帯域は対象デバイスの初期設定から取得する。
5周期は動作確認用で、定常状態の保証や複数seed評価の代替ではない。

## コマンド（リポジトリルート）

```bash
./ns3 build master -j 4

# A: 全周期40 Mbps（比較用）
./ns3 run 'master --settingPath=data/scenarios/ap1_capacity_80_seed1001.json --method=no_switch --rngSeed=1001 --mob=1 --ap1CapacityTrace=1 --outputRoot=results/ap1_capacity/constant' --no-build

# B: 容量変動（自動でAP1診断ログも出力）
./ns3 run 'master --settingPath=data/scenarios/ap1_capacity_80_seed1001.json --method=no_switch --rngSeed=1001 --mob=1 --ap1CapacityVariation=1 --outputRoot=results/ap1_capacity/variable' --no-build
```

パラメータ: `--ap1LowRate=20Mbps --ap1DropCycle=2 --ap1RecoveryCycle=4`
（上記が既定値）。周期番号は1始まり。低下周期 < 回復周期 <= 総周期数、
0 < 低下帯域 < 初期帯域を検証する。サンプル間隔は`--ap1SampleSec=0.1`。

## 保存内容と検証

各出力ルートの `80/no_switch/` に既存のmaster_logを保存。
追加ログは `80/no_switch/ap1_capacity/<run_id>/`。

- `setting.json`: 実行時設定のコピー。
- `metadata.txt`: seed、run ID、CLI、対象リンク・方向、時間・帯域設定。
- `initial_terminals.csv`: アプリと初期接続。`initial_bs_id_1based`は1始まり。
  既存master_logの基地局IDは0始まりなので比較時は1を引く。
- `events.csv`: 初期設定（cycle 0）と実際の変更イベント、両方向の変更前後帯域。
- `queue_samples.csv`: AP1両端のNetDeviceキューと上位QueueDiscの滞留・ドロップ。
  `received_bytes_total`はNetDeviceキューの既存累積カウンタで、実受信TPや
  厳密なリンク利用率ではない。QueueDiscがなければ該当欄は空。
  サンプルのcycle 0はウォームアップおよび全周期終了後を示す。
  境界時刻のイベントとサンプルを合わせて読む。停止時の最終サンプルも保存する。

```bash
python3 -m unittest discover -s tests -p 'test_ap1_capacity.py'
python3 scripts/check_ap1_capacity.py \
  results/ap1_capacity/variable/80/no_switch/ap1_capacity/<run_id> \
  results/ap1_capacity/variable/80/no_switch/master_log_<対応するファイル名>.csv
```

チェッカーは同じ実行のログを指定する。変更時刻・双方向帯域・seed・全端末全周期・
切り替えなしを検証し、周期別平均TP/RTT・調和平均・不満足端末数を表示する。
容量変更でQoEが改善・悪化するかは実測で判断し、AP1利用端末のTP/RTTも確認する。
単純な全体平均RTTにはTP重視端末も含まれるため、アプリ別評価を併用する。

## 実装確認コマンド履歴

- `./ns3 build master -j 4`: 初回はccache一時領域のサンドボックス制約で失敗。
  承認後の再試行では存在しないQueue APIのコンパイルエラーを修正し、ビルド成功。
- `python3 -m unittest discover -s tests -p 'test_ap1_capacity.py'`: 3件成功。
- 上記A/Bと同一引数、出力ルートのみ`results/ap1_capacity_smoke/{constant,variable}`として実行。
- 80端末A/Bの初期アプリ・初期接続CSVの一致を確認。両実行は完了を待ち切れず
  手動中断（終了コード130）。`results/ap1_capacity_smoke/` は未完了ログであり、
  QoE比較や成功した80端末実験として使用しない。動作中の検証プロセスは残していない。
- `/tmp/ap1-small.json`（上記専用設定のterminalsのみ3）でA/Bを実行し5周期完走。
  `python3 scripts/check_ap1_capacity.py /tmp/ap1-small-run/3/no_switch/ap1_capacity/* /tmp/ap1-small-run/3/no_switch/master_log_*.csv`
  と、比較側`/tmp/ap1-small-constant/3/no_switch/`に対する同じチェックが成功。
  13.5秒/32.5秒のイベント、双方向帯域、全周期ログ、固定接続を確認。
  これは制御動作の検証であり、80端末のQoEへの影響の検証ではない。
- `--ap1DropCycle=4 --ap1RecoveryCycle=2` は意図どおり実行開始前に拒否。
- `python3 -m unittest discover -s tests -p 'test_backhaul_capacity.py'`: 既存2件成功。
- `git diff --check`: 成功。
