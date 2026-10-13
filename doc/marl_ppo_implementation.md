# MARL PPO 初期実装・実行手順

更新：2026-10-13。設計の根拠・合意内容は [proposed_method_marl.md](proposed_method_marl.md)。

## 1. 実装範囲

- `rl/marl/core.py`：入力・QoE・欠測規約、用途別seed、共有Actor/全体Critic、PPO更新、checkpoint。
- `scripts/run_marl.py`：`--method=marl_ppo` を指定してns-3を起動。専用TCP endpointと出力先を各episodeに割り当て、同一方策で収集してから更新する。
- `contrib/kameda/model/server/marl-ppo.cc`：同一観測から端末ごとの行動配列を受け取り、既存の一括切り替え処理へ渡す。第5周期は切り替えない。
- `APMonitorTerminal` / `KamedaAppServer`：MARL専用の厳密なRTT応答期限、TCPフレーム受信・報告期限。既存手法の計測分岐は維持する。
- `master/applications.cc`：既存TP窓を維持し、観測時APへ帰属。送信需要が確認できる受信ゼロと需要未確認を区別する。
- `scripts/plot_marl.py`：学習の生値・後方16 episodes平均、非平滑化validation、モデルseed間の平均±標本標準偏差をPNG/PDFへ保存。

モデル・更新設定は設計の初期値を既定とする。入力13次元/UE、8次元/AP、全体3次元。
Criticのみ残り意思決定数/4を追加する。Actor/Criticの重みは分離し、端末間でActorを共有。
報酬は `H_next`、gamma=lambda=1、5周期から4共同遷移、16 episodesからPPO更新1回。
RTT要求値は100/40 ms、TP要求値は合意済みの3.0/8.0 Mbps。
分類・不満足端末数・位置・電波品質・仮想的な切り替え後品質は学習入力にも報酬にも使用しない。

### 通信・欠測・ログの注意

- AP/UE IDはPython・MARLログでは0始まり。内部の既存AP IDとは境界で変換する。
- 観測は `marl_v1` JSONの改行フレーム、応答は `MARL_PPO_V1 cycle policy_version H ap0 ...`。
- 要求配列は次周期の実接続配列と照合。不一致、周期不一致、真の評価欠測はepisodeを無効として停止し、学習更新に混ぜない。正常runだけの選別平均で評価を続行しない。
- 欠測Hはnull。過去値、0報酬、epsilon報酬で置き換えない。確認済みRTT timeoutと正常TPゼロだけS_effの下限1e-6を使う。
- FlowMonitorで送信/受信が確認できないTPは `demand_unconfirmed`。これが発生したら `observations.jsonl` と送信アプリを調査し、都合よくゼロへ読み替えない。
- RTTは10回・200 ms間隔、各送信から正かつ1秒未満の全有効応答の平均。報告完全受信は周期開始+6秒未満。追加プローブ・報告アプリ再接続は行わない。
- 報告後の遅延応答は `console.log` の `[MARL late]` に残し、確定済み観測・報酬を更新しない。報告のlate件数は報告時点までの値。
- 既定のTP窓は周期内+1.1〜+8.8秒、判断+9.3秒、周期9.5秒。窓内切り替え/リセットは拒否する。
- `qoe.csv` の `current_bs_id` は今回の観測先、`previous_bs_id` は前周期観測先、`requested_bs_id` は次周期向けの要求。`switch_flag` は今回までに適用された変更、`requested_switch_flag` は今回の要求。
- `satisfaction` はS_eff、`satisfaction_raw` はS_raw。未使用の旧分類は `legacy_not_used`、不満足数・RSSI/RSRPは空欄。座標は `simulation/` 配下の `marl_initial_positions.csv` に保存（入力には使用しない）。既存のmaster_log解析をそのままMARLへ適用しない。

## 2. 既存ジョブを変更しない隔離ビルド

この環境の既存CMake/Ninjaキャッシュとビルド済みns-3依存ライブラリを利用する。
外部ライブラリ追加・ns-3バージョン変更は行わない。Pythonは既存PyTorchとmatplotlibを使用する。

```bash
cd /home/sota/ns-3.44
python3 scripts/build_marl_isolated.py --output build/marl-ppo-v2 --jobs 1
```

**既に同名ディレクトリがある場合は再実行しない。** 作成済みの `build/marl-ppo-v2/master` を使うか、別名を指定する。
通常の `./ns3 build` やCMake configureは実行しない。
スクリプトはmaster/kamedaを専用objectへ再コンパイルし、依存共有ライブラリのコピーとリンクする。
既存binary・共有ライブラリ・`.lock-ns3` は更新しない。
これは既存ビルドを利用したローカルビルドであり、別環境向けのクリーンビルド手順ではない。
ビルド中のソース変更や別のビルドは避ける。コマンド・ソース/依存ライブラリのhashを隔離先へ保存する。

## 3. 最初に実行する30台・1 episodeの動作確認

以下は**ユーザーがこれから実行するコマンド**。シミュレーション結果・所要時間は未確認。

```bash
python3 scripts/run_marl.py \
  --mode smoke --episodes 1 --terminals 30 --model-seed 1 --workers 1 \
  --binary build/marl-ppo-v2/master \
  --output results/marl/smoke30_m1
```

- Pythonラッパが内部で `--method=marl_ppo` を指定する。専用サーバーが必要なのでbinary単独での起動ではなくラッパを使う。
- 30台、既存アプリ/初期配置/初期接続の抽選、移動なし、固定80/40/20 Mbps。
- `data/setting.json` を編集せず、各episode専用の設定コピーで端末数・seed・5周期を指定する。
- モデルseed=1から導出した学習環境seed順序の先頭を使う。環境seed=1固定という意味ではない。
- 初期Actorによる4回の意思決定まで。**smokeではPPO更新しない。** validationも起動しない。
- 出力先は新規ディレクトリ必須。既存結果を上書きしない。

確認するファイル：

| ファイル | 確認内容 |
|---|---|
| `manifest.json` | 設定、モデルseed、バージョン・hash |
| `episodes.csv` | H2〜H5平均/合計、全5周期平均、初期H、所要時間、最大RSS |
| `smoke_*/status.json` | valid=true、terminal=true、transitions=4、returncode=0 |
| `smoke_*/max_rss_kib.txt` | GNU timeによるns-3子プロセスの最大RSS（KiB） |
| `smoke_*/console.log` | ns-3診断・遅延応答・エラー |
| `smoke_*/observations.jsonl` | 5周期の生観測、評価結果、maskの根拠 |
| `smoke_*/transitions.jsonl` | 4遷移、要求/適用配列、H_next、terminal |
| `smoke_*/qoe.csv` | 各UEのQoEと切り替え履歴 |
| `smoke_*/planned_events.csv` | 共通の容量イベント列 |

`status.seconds` は起動・終了処理も含む壁時計時間。RSSはPython学習器を含まず、並列全体のメモリではない。
必要ならラッパ自体も `/usr/bin/time -v -o wrapper_time.txt python3 scripts/run_marl.py ...` で計測する。
並列時の合計メモリと既存ジョブの遅延は別途OS側で観測する必要がある。

## 4. 短期学習・評価・学習曲線（動作確認後）

```bash
python3 scripts/run_marl.py \
  --mode train --episodes 16 --terminals 30 --model-seed 1 --workers 1 \
  --binary build/marl-ppo-v2/master --output results/marl/train30_m1_16

python3 scripts/plot_marl.py results/marl/train30_m1_16 \
  --output figures/marl/train30_m1_16
```

16は学習episodes数。初期validation 8本＋最終validation 8本が追加されるため、
**このコマンドのns-3実行は計32 episodes、PPO更新は1回**。
32学習episodesなら計48実行・更新2回。本学習30台256 episodesなら初期8＋64ごとの8×4で計296実行。
途中のcheckpointは64 episodesごと（80台以上は128）、最終checkpointは必ず評価する。
`updates.csv` に損失・entropy・KL・clip率・clip前勾配norm、`validation.csv` に非平滑化選択指標を保存する。
`latest.pt` は更新ごと、`best.pt` はvalidationのH2〜H5平均が厳密に改善した場合のみ更新（同点は先着）。
初期未学習モデルも選択対象とする。

```bash
python3 scripts/run_marl.py \
  --mode test --terminals 30 --model-seed 1 --workers 1 \
  --checkpoint results/marl/train30_m1_16/best.pt \
  --binary build/marl-ppo-v2/master --output results/marl/test30_m1
```

testは1001〜1010の各環境seedを1回、validationは1011〜1018を各1回。
両方とも確率的選択、重み更新なし。test結果でcheckpointを選ばない。
checkpointは信頼できるローカル生成ファイルのみ読み込む。
動的容量は後工程で `--capacity configs/capacity/random_walk_all.json` を指定する。
容量seedは100000+環境seed。80/100台は別モデルとして独立学習する。

並列は**1→2→8の資源検証後**。既定は1のみ。
2並列を試す段階では `--mode smoke --episodes 2 --workers 2 --parallel-validated` と別出力先を使い、
既存ジョブへの影響を監視する。フラグは安全確認・監視の明示的な承認であり、安全性を自動検証するものではない。
問題なければ8へ進む。8 workersなら16 episodesを2波で収集し、その後1回更新する。
複数ターミナルから同じcheckpointを更新するのではなく、**1つの学習器が並列ns-3を管理**する。

補助的に `--resume --checkpoint .../latest.pt --episodes <累積予算>` を用意している。
設定一致・更新境界のみ受理し、新規出力先を使う。詳細な障害復旧規約は未確定。
再開前後の曲線CSVの自動結合は未実装（複数seedとして混ぜない）。

## 5. 確認状況と残る検証

実施コマンド：

```bash
python3 -m unittest discover -s tests -p 'test_marl_ppo.py' -v
python3 -m unittest discover -s tests -p 'test_reactive*.py' -v
python3 -m py_compile rl/marl/core.py scripts/run_marl.py scripts/plot_marl.py scripts/build_marl_isolated.py
git diff --check
```

Pythonテスト：QoE/ゼロ/欠測、端末数30/40/80/100、順列同変性、模擬5周期通信、PPO更新、checkpoint再現性、要求/適用不一致の拒否。
MARLテスト8件、既存reactive DQN回帰テスト5件は成功。Python構文確認・diff検査も成功。
隔離ビルドのコンパイル・リンク、`build/marl-ppo-v2/master --PrintHelp` に成功し、
`ldd` で隔離先kamedaライブラリのロードを確認した。既存コードの非推奨API警告は残る。
これらは実ns-3通信・無線計測の試験ではない。
実環境での5周期通し実行、同一seed再現性、実測時間/メモリ、2/8並列、学習収束、比較手法との公平性は**未確認**。
比較手法の計測規約移行とIEEE本文照合は合意どおり後工程。研究上の改善はまだ主張しない。

### 初回smokeでの容量CSV改行修正（2026-10-13）

ユーザーによる `results/marl/smoke30_m1` の実行は、sim_time=0で
`Invalid capacity CSV header` により異常終了（134）。PPO・意思決定には到達していない。
原因はラッパのCSV既定改行CRLFと、C++側のLF前提の厳密比較との不一致。
`write_capacity_events` でLFを明示し、空イベント/イベントあり双方のバイト列検査を追加した。
MARLテスト9件、Python構文確認、`git diff --check` は成功。修正後の実ns-3通し実行は未実行。
C++・既存binaryは変更不要。失敗結果は保持し、再実行は `--output results/marl/smoke30_m1_retry1`
など新規出力先で行う（他の引数は第3節と同じ）。

### 8並列学習での報告欠測の調査（2026-10-13）

`train30_m1_80_8w` は初期validation 8本成功後、最初の16学習episodes中の
`train_000009_seed734` が第4周期のAP2報告欠測で停止。他の15本はvalid。
AP2の監視端末は10/10のRTT応答を得ているが、制御サーバーの観測はreport_missing。
RTT対象の5端末が評価不能となった。PPO更新は未実施であり、欠測を捨てて15本で更新しない。
既存ログでは未到達・期限超過・実装不具合を識別できず、根本原因は未確定。

診断用にTCP接続開始/成功、送信キュー投入、監視終了、報告完全受信/拒否の時刻を追加。
計測・報告期限・再送規約は変更しない。`--mode smoke --episode-start 9 --episodes 1`
で、環境seed734と元のaction_train乱数を使って失敗episodeだけを再現できる。
診断binaryは `build/marl-ppo-v3-diag/master`。既存binary・失敗ログは保持する。
この変更を根本修正・通し実行成功とは扱わない。

ユーザー方針：30台は動作確認に限定し、原因解消後は8並列で16学習episodesを収集して
PPO更新1回を実施する。新規trainは初期/最終validation込みで計32本。

### 報告接続再送0.2秒・収集途中からの復旧（2026-10-13）

合意に基づき、MARL報告用TCPソケットのみ `ConnTimeout=0.2秒` を既定に変更。
`--report-conn-timeout` → ns-3 `--marlReportConnTimeout` として渡し、episode manifest、
実験identity、checkpointへ保存する。プローブtimeout=1秒・報告期限=周期+6秒・
アプリ再接続なしの規約は維持する。以前の3秒条件とのデータ混合は禁止。
新binaryは `build/marl-ppo-v4/master`（既存binaryを置換しない）。

- 正常episodeが終了するたびに `trajectory_cache/<identity>.pt` をatomic replaceで保存。
  4遷移の観測・行動・報酬・done・old_log_probs・old_valueと集計結果を含む。
- Actor/Critic双方の重みhash、方策バージョン、環境/行動seedの識別に用いるepisode番号、
  モデルseed、phase、設定、binaryと同梱共有ライブラリ、Pythonソース/実行環境を照合する。
- 完了順にログを書き、PPOへは元のepisode番号順で渡す。
- 失敗検知後は新規投入を止め、既に動いているepisodeを回収・保存して終了する。
  失敗episodeを除いた少数batchで更新しない。自動seed差し替え・無限再試行は行わない。
- `recovery.pt` は収集開始前のモデル/optimizerと進捗を保存。
  初期validation、学習収集、最終validationの中断に対応する。
- `--recover-run` は停止済みrunを指定し、新規出力先へ復旧する。
  同じ方策・条件の成功済みデータをコピーして再利用し、欠けたepisodeだけ同じseedで実行。
  失敗の原因が解消していなければ再度停止する。旧版runのログからの遡及復旧は未実装。
- Ctrl+Cは新規投入を止め、実行中episodeの完了・保存を待つため即時終了ではない。
  強制kill/OS停止では完了保存済みのデータのみ保証対象。ディスク障害まで保証しない。
- 設定/ソース/binaryを変更した場合、旧データを混ぜず新規実験として再収集する。
  復旧先CSVは再利用分を `reused=True` と記録し、元のログ先を `source_directory` に残す。
  再利用データのsecondsは元実行時間であり、再開後の実時間へ加算して見積もらない。

診断seed734（初期モデルのepisode番号9）:

```bash
python3 scripts/run_marl.py --mode smoke --episodes 1 --episode-start 9 \
  --terminals 30 --model-seed 1 --workers 1 --episode-timeout 7200 \
  --report-conn-timeout 0.2 --binary build/marl-ppo-v4/master \
  --output results/marl/smoke30_seed734_v4
```

成功確認後の16学習episodes・更新1回（初期/最終validation込み32実行）:

```bash
python3 scripts/run_marl.py --mode train --episodes 16 \
  --terminals 30 --model-seed 1 --workers 8 --parallel-validated \
  --episode-timeout 7200 --report-conn-timeout 0.2 \
  --binary build/marl-ppo-v4/master --output results/marl/train30_m1_16_8w_v4
```

上の学習runが収集途中で停止した場合（同じソース/条件を維持）:

```bash
python3 scripts/run_marl.py --mode train --episodes 16 \
  --terminals 30 --model-seed 1 --workers 8 --parallel-validated \
  --episode-timeout 7200 --report-conn-timeout 0.2 \
  --binary build/marl-ppo-v4/master \
  --recover-run results/marl/train30_m1_16_8w_v4 \
  --output results/marl/train30_m1_16_8w_v4_recovered
```

模擬試験では失敗後の投入停止・実行中成功データの保存・条件不一致時の拒否、
初期/最終validationの復旧、学習収集中の復旧後の更新結果が中断なしの更新結果と
Actor/Criticとも完全一致することを検査する。実ns-3の確認状況は別途追記する。
