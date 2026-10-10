# 80/100端末のバックホール容量比較

目的は「100端末のRTTを必ず大きくする」ことではなく、両条件が常時飽和する状態を避け、
負荷増加に対するRTT/損失/満足度の応答を観測すること。

## 追加機能（既定の実験条件は維持）

- `--pgwCerRate=80Mbps`: PGW–CERの両方向の速度。既定80Mbps。
- `--appTypesPath=...`: 端末順のアプリIDを空白/改行区切りで指定。
  1=browser, 2=video, 3=voice, 4=game。端末数と同じ個数が必要。
  省略時は従来の乱数による生成。指定時も既存乱数の消費順序は維持。
- `--outputRoot=...`: 実験ケースごとの出力分離。省略時は従来OUTPUT。
- 診断schema=2に `link_load.csv` と `initial_terminals.csv` を追加。
  既存 `queues.csv` / master_log の列は変更なし。

`link_load.csv` はPGW–CER各方向のroot QueueDiscについて、
受け入れ・AQM破棄より前の累積流入バイト/パケット数、デバイスへ渡した累積数、
破棄数、滞留量、QueueDisc型、帯域を保存する。
バイトはIP層の値（PPPヘッダを除く）。`sent_*` はリンクを通過し終えた量や
受信アプリのgoodputではなくデバイス側へ渡した量。
RLCや上流リンクより前のアプリ総送信負荷とは区別する。

```bash
python3 scripts/analyze_link_load.py <診断ディレクトリ>/link_load.csv --output /tmp/link_intervals.csv
```

`incoming_mbps = Δreceived_bytes_total * 8 / Δtime / 1e6`。
同一時間窓の平均はバイト差分/時間で算出し、異なる長さの区間率を単純平均しない。
0.1秒区間の瞬間的な超過だけで恒常的飽和とは判定しない。queues.csvの満杯率と併用。

## 比較条件

- 方策: 最初はlogistic。random等の比較は別suiteで行う。
- 端末数: 80 / 100。
- seed: 1001 / 1002（まず探索。最終評価では追加seedが望ましい）。
- 帯域: 80 / 120 / 160 Mbps。
- アプリ比率: browser20%, video40%, voice15%, game25%を厳密に固定。
  - 80端末: 16 / 32 / 12 / 20。
  - 100端末: 20 / 40 / 15 / 25。
  seed別に20端末ブロック内をシャッフルし、最初の80端末のアプリ列は共通。
- 上記比率は従来の抽選確率と同じだが、従来の個別ログの実現構成とは異なる。
  以前の100端末seed1001の動画48台と今回の40台を同一条件として比較しない。
- 同一端末数・seedでは帯域間で同じアプリ構成・初期割り当てを用いる。
  80/100間はAP台数/割り当て結果まで同一にはできず、ログで実際のAP0負荷を確認する。
- 片道20ms、デバイス600p、QueueDisc、RLC上限、アプリ送信条件は変更しない。
- 元のdata/setting.jsonは編集しない。ケース別JSONを保存する。

## 準備・段階実行

```bash
./ns3 build master -j 2
python3 scripts/backhaul_capacity_experiment.py prepare results/backhaul_capacity_20261010
# まず80Mbps、seed1001の80端末→100端末の順で実行（長時間）
python3 scripts/backhaul_capacity_experiment.py run results/backhaul_capacity_20261010 --rate 80 --seed 1001
# 結果を確認後に追加する。自動的に全12ケースを開始しない。
python3 scripts/backhaul_capacity_experiment.py run results/backhaul_capacity_20261010 --rate 80 --seed 1002
python3 scripts/backhaul_capacity_experiment.py run results/backhaul_capacity_20261010 --rate 120 --seed 1001
```

runnerはstdout.log、設定、アプリ一覧、実行引数、バイナリSHA256、開始終了時刻、終了コード、
pending/running/completed/failed/interruptedの状態をmanifest.jsonへ保存する。
同一suiteの並列launcherはrunner.lockで拒否。実験中のビルドやモデルファイル変更は避ける。
completedはプロセス正常終了であり、解析完了やQoE改善を意味しない。
強制終了でlockが残った場合はPIDが生きていないことを確認してから手動復旧する。

## 判定

1. 80/100の流入負荷とキュー満杯率、AP0のアプリ構成を確認する。
2. 80Mbpsで両方飽和なら120Mbpsを比較。120でも飽和なら160Mbps等へ拡張。
   どの速度で差が出るかは未確認。FqCoDelによりpingの遅延差が小さい場合も正常な結果。
3. master_logのRTT、調和平均、不満足端末数、端末当たりTP、キュー破棄数を複数seedで比較。
   監視pingの損失率専用CSVは今回まだ追加していない。必要なら送信/応答イベントの計測を別途追加。

## テスト

```bash
python3 -m unittest discover -s tests -p test_backhaul_capacity.py -v
python3 -m unittest discover -s tests -p test_queue_diagnostics.py -v
```

比率・seed再現性・80端末prefix一致・区間流入量・カウンタリセットを検証。
小規模スモークで帯域引数、アプリ指定、分離出力、CSVの整合性を確認してから大規模実行する。

## 2026-10-10 実施記録

- ビルド成功（初回はccache一時領域のサンドボックス制限で失敗、承認環境で再実行）。
- 上記単体テスト計4件成功。
- 3端末・1サイクルの既定80Mbpsと、アプリ指定付き120Mbpsのスモーク実行が完走。
- 既定80Mbpsのmaster_logは過去の同条件ログと実時間列以外一致。
- 120Mbpsの実行時速度、アプリID指定、両方向カウンタ単調性、キュー600p/20ms維持、解析を確認。
- 検証結果・ログ: `results/backhaul_capacity_validation/`。
- 12ケースの設定を準備。80Mbps/seed1001の80端末→100端末のみ逐次実行を開始。
- その他のseed/帯域は未実行。大規模結果の解析・結論は未完了。
- 実行状況: `results/backhaul_capacity_20261010/manifest.json`。
- 起動コマンド:
  `python3 scripts/backhaul_capacity_experiment.py run results/backhaul_capacity_20261010 --rate 80 --seed 1001 > results/backhaul_capacity_20261010/runner.log 2>&1`
