# 標準的な反応型DQN baseline：実装設計書

作成日：2026-10-10 / 本文は当初設計。初期実装済みの範囲・差分・実行手順は [実装記録](reactive_dqn_running.md) を参照。
対象：ns-3.44、80端末、AP0=5G/AP1・AP2=Wi-Fi（IDは0始まり）。
提案method名：`reactive_dqn`。本書中の新規CLI/APIはまだ使用できない。

## 1. 目的と今回の範囲

未来の容量・変更時刻を知らず、現在の通信品質を観測して、端末と接続先を選択する
標準的なDQNを実装する。目的は実測の端末満足度の調和平均を時間を通じて高く保つこと。
これは新規提案の比較対象であり、RLそのものを新規性と主張しない。

`missing_observation_fair_comparison_design.md` の全面実装は前提にしない。
ただし有限値・欠測マスク・時間対応など、学習を成立させる最小限の契約は本書で定める。
既存logisticの再学習、共通QoE v2、共通行動制約器は今回実装しない。
このためlegacy logisticとの比較はシステム比較に留め、アルゴリズムの優劣を確定しない。

初期版で採用しないもの：容量予測、LSTM、MARL、他端末への寄与分解、模倣学習、
推定Hによる安全フィルタ、優先度付きreplay、Double/Dueling DQN。

## 2. 既存実装との関係

既に本リポジトリにはRL実装があるため、ゼロからTCP基盤を作り直さない。

| 既存要素 | 今回の扱い |
|---|---|
| `rl/server.py`, `rl/centralized_server.py` | TCP/JSON通信・モデル保存の参考。契約を確認して再利用可能部のみ使用 |
| `rl/agent.py`, `rl/replay_buffer.py` | DQN/replayの再利用候補。target式・done・mask対応をテストして採否を決める |
| `rl/centralized_agent.py`, `rl/centralized_models.py` | 既存方式は保持。factorized構造等をbaselineに自動継承しない |
| `rl/train_online.py` | ns-3起動・エピソード制御を調査して再利用 |
| `APselection.cc/.h` | 新方式の観測・行動・遷移処理を追加する入口 |
| `master/config.cc`, `master/NetSim.h` | method/接続先/設定の受け渡し |
| `doc/centralized_DQN.md` 等 | 参考資料。既存方式の推定報酬・周期内複数行動を今回へ持ち込まない |

推奨新規ファイル案：
`rl/reactive_protocol.py`, `rl/reactive_agent.py`, `rl/reactive_server.py`,
`rl/train_reactive.py`, `rl/evaluate_reactive.py`,
`data/scenarios/reactive_dqn_train.json`, `tests/test_reactive_*.py`。
実装時に共通コードを利用できるなら重複は避ける。ただし既存checkpointのschemaは変更しない。
既存PyTorch等を利用し、新依存が必要になった場合のみ理由・導入手順を追記する。

## 3. 最小構成の決定

| 項目 | 初期版 |
|---|---|
| エージェント | 中央制御DQN 1つ |
| 観測 | 現在周期の全UE/AP状態。未来情報なし |
| 時間 | 1制御周期=1環境step |
| 行動 | 接続維持、または1 UEを1 APへ切り替え |
| 最大切り替え数 | 1台/周期、全UEを候補にする |
| 報酬 | 次周期の実測ログH − 切り替えコスト |
| 学習 | ns-3を反復実行、経験再生・target networkを使う標準DQN |
| 評価 | 学習済み重み固定、epsilon=0、optimizer/replay更新なし |
| 初期接続 | 既存seedによる配置。logistic bootstrapなし |
| QoE | 既存評価を保持し `legacy_v1` と明示 |

1台/周期は標準baselineを小さく作るための制約であって、最適な制御粒度ではない。
大規模な再配置に時間がかかる。5周期でlogisticの初回60台移動と公平に比べられるとは言わない。
まず長いepisodeで動作を確認し、K>1は別versionの拡張として比較する。

## 4. 状態（schema `reactive_v1`）

### 4.1 UEごと（UE ID順の固定80スロット）

- 現在AP：3次元one-hot
- アプリ：4次元one-hot
- 今周期TP Mbps、AP監視RTT ms、現行満足度S
- TP適用マスク（ブラウザ/動画=1、音声/ゲーム=0）
- TP正値観測マスク、RTT有効観測マスク
- 前回切り替えからの経過周期（上限を設定）、過去に切り替えたかのマスク

計15特徴量/UE。TP正値観測マスクは旧計測の意味を明示したもの。
実際に報告された0と未到着を完全に分離できるとは主張しない。
RTTはUE個別値でなく接続APの監視値。評価と同様、この限界を記録する。

### 4.2 APごと（AP ID順）

- ブラウザ/動画/音声/ゲームの接続人数（4値）
- 監視RTTと有効マスク
- 正TP観測があるTP対象端末の平均TP、その観測数、その有無マスク

計9特徴量/AP。平均TPは利用可能容量ではない。
TP対象0台なら平均値=0、観測数=0、有無マスク=0。推論は続行する。

### 4.3 全体

- 現行H
- 現行不満足端末数/N（閾値S<0.5）
- UEの `measurement_valid` 比率

初期次元は80*15+3*9+3=1230。schemaテストで固定する。
状態は将来の実装段階で変更可能だが、その場合は新schema・再学習とする。
cycle番号、残り時間、容量イベント時刻、設定DataRate、候補APの推定満足度、
推定H改善量、教師行動、未来の計測値は入力しない。

### 4.4 欠測・正規化の最小仕様

- 未観測値の数値入力は0、別maskで識別。前値補完・prior学習は今回はしない。
- TP/RTTが欠けるAPがあっても全体の接続維持へ早期returnしない。
- カウントはNで割る。連続値は非負を確認した上でlog1p変換し、学習分割だけでfitした
  scalerで正規化する。mask/one-hotは変換しない。設定・scalerをcheckpointと保存する。
- 初回scalerは学習用warmup収集から固定する。評価値でfitしない。
- NaN/Infは警告とmaskで処理し、モデルへ渡さない。schema破損はepisode失敗とする。
- 現行Sには欠測時の代替値が含まれるため、Sとmaskは常に対で提供する。

## 5. 行動空間

`action_id=0` は全UE接続維持。
`action_id=1+ue_index*3+bs_id` はUEを指定基地局へ移す（ue_index=0..79）。
出力数は241。ログのue_id（1始まり）と混同しない。

現在と同じAPへのpair actionはmaskし、接続維持はaction 0へ集約する。
それ以外の「低満足度でないから」「推定改善が負だから」というmaskは使わない。
全3 APへのアクセスを持つ現行トポロジを前提とする。実行不能な候補は物理的実行可否だけでmask。
maskはepsilon探索・greedy選択・targetのmaxすべてで適用する。
接続維持は必ず有効。Q同値時は小さいaction_idを選び再現可能にする。

## 6. 観測・行動・実測報酬の時間対応

```text
周期tの測定が完了 -> s_tを固定
前周期のpending行動があれば (s_(t-1), a_(t-1), r_(t-1), s_t) を確定
終端でなければ a_tを選び、既存handover経路へ送る
周期t+1を実際にシミュレーション -> s_(t+1)からr_tを計算
```

- 周期内に仮想的に接続を更新して複数の「実測遷移」を作らない。
- 接続変更をまだ反映していない観測へ、変更後のラベルを付けない。
- 最新周期の全UE測定が同じ窓・接続状態に対応していることを確認する。
- 最終測定周期では新規行動を出さず、直前行動の遷移だけをterminalとして確定する。
  M周期のepisodeから得られる遷移はM-1個。5周期なら4個。
- 正常な実験終端はdone=true。通信断/クラッシュは正常終端扱いせずepisode失敗として記録。
  部分遷移を勝手に補完しない。
- action_id、要求/適用時刻、実適用AP、次周期APを照合する。
  不正行動や通信失敗を接続維持として学習に混ぜない。原則episodeを失敗にする。

既存 measured_reward_log の生成処理を流用する場合も、現在方式の推定報酬や複数stepと
混ざらないことを検証する。新方式では専用transitionログを正とする。

## 7. 報酬

r_t = H_logged(t+1) - lambda_switch * applied_switch_count(t) / N

初期案：lambda_switch=1.0（80台で1切り替えのコスト0.0125）。
この値は最適値ではない。0/0.1/1.0の感度分析をvalidationだけで行い、テスト前に固定する。
Hの平均的な水準と比較してコストが支配しないか確認する。

- Hは推定値ではなく次周期の現行計測ログ値を使う。
- 他端末悪化ペナルティ、差分報酬、個別貢献報酬は初期版で加えない。
- 容量回復によるH上昇も報酬に含む。行動単独の因果効果とは解釈しない。
- 全体Hを高く保つ目的を明確にするため、H差分だけの報酬にしない。
- Hの現行欠測代替（TP欠測0.1、RTT要求値代用等）は残る。欠測が有利な行動が
  学習された場合、成功と見なさず共通評価器の整備へ戻る。
- coverageと欠測理由を必ず併記し、欠測を除いた端末だけのHを主指標に置き換えない。

## 8. DQN学習器の仕様（初期案、全て設定に保存）

標準target：y=r + gamma*(1-done)*max_valid Q_target(s_next,a_next)。
Double DQNのonline argmax/target評価の組み合わせは使わない。

| 項目 | 初期値 |
|---|---|
| Q network | MLP 1230 -> 256 -> 256 -> 241、ReLU |
| optimizer | Adam、lr=1e-4 |
| loss | Huber |
| gamma | 0.99 |
| replay | 100000遷移、uniform sampling |
| batch | 64 |
| 学習開始 | 実測遷移1000件後 |
| 更新頻度 | 1実測遷移につき1勾配更新 |
| target更新 | 500勾配更新ごとhard copy |
| epsilon | 実環境行動step基準で1.0->0.05、10000 stepで線形減衰 |
| gradient clipping | norm 10 |

ns-3の実行コストを測ってから本学習の予算を決める。5周期1runだけで学習完了としない。
Python/NumPy/PyTorch/ns-3のseedをそれぞれ記録し、決定性設定とデバイスも保存する。
checkpointにはonline/target/optimizer/step数/乱数状態/schema/scalerを保存する。
厳密な再開にはreplayも必要。保存しない再開は別runとして扱う。

## 9. 通信契約と失敗処理

既存TCP/JSONを参考に、version付きのメッセージを定義する。

- reset: episode_id、N/AP数、schema、seed、config hash
- observe: episode_id、cycle_id、sim_time、state、valid_action_mask、前行動の実適用結果、done
- action: episode_id、cycle_id、action_id、epsilon、policy_version
- ack/end/error: エピソード終了と失敗理由

episode/cycle不一致、重複、遅延応答、不正次元を拒否する。
再送時に同じ遷移を二重登録しない。reset後に前episodeのpendingを残さない。
容量設定など環境manifestはrunner/解析側に保存し、policyのobserve入力に混ぜない。
ns-3は応答を待つが、wall-clock待ち時間をシミュレーション時間へ加算しない。
timeoutは設定に保存し、失敗数・推論時間も結果に含める。

## 10. 学習・評価シナリオ

### 動作確認（既存条件）

80端末、seed1001、5周期、9.5秒/周期、warmup3秒、mob=1。
AP0 80->40->80および80->20->80、drop=2/recovery=4。
容量一定80 Mbpsも実施。これは学習性能ではなく通信・遷移対応の確認。
最初は3/10端末のテスト用可変Nでもプロトコルが動くことを確認するが、
モデルの入力出力はN依存なので80台モデルとcheckpointを混用しない。

### 学習用（案）

M=40周期/episodeに延長。初期割り当て・アプリ構成をseedで変える。
容量一定、AP0低下40、AP0低下20のケースをepisodeごとに抽選。
低下周期は8..16、継続は8..16周期から事前生成し、実現系列を保存。
回復周期がMを超えないよう検証する。生成乱数を方策乱数と分離する。
初期版はAP0変動に限定。AP1/AP2変動は汎化評価または拡張学習として区別する。
変更時刻はモデルへ入力しない。周期番号を除いても規則を暗記し得るので固定系列のみで学習しない。

train/validation/testでseed・変動系列を分離し、scalerもtrainのみ。
seed1001の既知結果は開発用とし、最終の未知条件評価は別の未使用seedで実施する。
評価は複数環境seed×複数学習seed、全手法へ同じ外部イベント系列を与える。
seedだけで初期条件が一致するとは仮定せず、実現した初期配列・イベント列を照合する。

## 11. 比較の位置付け

主な実装検証比較：
- no_switch
- ランダム1端末切り替え（接続維持も同じaction集合に含む）
- 観測品質に基づく1端末のルールベース（選択規則を事前固定）
- reactive_dqn（1端末/周期）

legacy logistic、既存centralized_dqnは追加比較とし、入力・欠測停止・K・推定フィルタ・
初期化・報酬の差を表に明記する。同じmethod名だけで条件同一としない。
logisticより良い結果が出ても、欠測停止の未修正段階では「RL固有の優位性」と結論しない。
容量一定・変動ありの両方を同じ方策で評価し、変化後の低下量・適応時間を比較する。
新規協調手法は将来、このbaselineと同じ入力/行動予算で追加効果を検証する。

## 12. ログ・出力と解析

既存列名は維持し、専用ログを追加する。保存先案：
`results/reactive_dqn/<experiment_id>/<train|validation|test>/<run_id>/`。

- manifest.json：ns-3 version、source/config/model hash、全seed、イベント系列、
  app要求値、unsatisfied閾値、QoE/状態schema、学習パラメータ、依存version
- transitions.jsonl：episode、action_cycle、next_cycle、state、mask、action、
  requested/applied BS、reward成分、next_state、next_mask、done、coverage
- training.csv：環境step、勾配step、loss、epsilon、Q統計、replayサイズ
- decision.csv：推論時間、requested/applied switch数、model version、fallback/error
- master/容量イベント/キューログ：既存出力を保持しrun_idで対応付け

指標：周期別/時間平均H、最低H・下位分位点、不満足人数、アプリ別TP・監視RTT、
切り替え数、非切り替え端末の満足度悪化数、推論/学習時間、欠測率、失敗run数。
非切り替え端末の悪化は隣接周期で比較し、容量変動の影響も含む記述統計と明記。
seed別値、平均、標準偏差/信頼区間を保存。グラフは軸・単位・凡例付きPNG/PDF。
新ログ追加時は同時に解析スクリプト・列検証を追加する。

## 13. 実装順序・テスト・受け入れ条件

1. **protocolと状態生成**：固定入力から1230次元/241 actionを検証。
   mask、one-hot、欠測APでも有限入力、未来イベント変更による入力不変をテスト。
2. **ダミーpolicy接続**：常に維持、次に指定1台だけ移す。
   K=1、ID変換、要求/適用/次周期AP、terminal時に余計な切り替えがないことを確認。
3. **実測遷移**：M周期でM-1遷移、episodeを跨ぐ遷移なし、報酬をCSVから再計算。
   容量回復周期も推定Hでなく実測Hが使われることを確認。
4. **DQN単体**：doneのbootstrap停止、next mask、探索の有効action制約、
   target更新、保存/再開、評価時の重み・replay不変をテスト。
5. **小規模結合試験**：3/10台の専用shapeで全周期終了、APにTP端末0台でも推論継続。
   失敗を正常なSTOP遷移へ置換しないことを確認。
6. **80台seed1001動作試験**：既存3容量条件で遷移・ログ照合。
7. **学習pilot**：実行時間/遷移数/欠測率を測り、学習予算を決める。
   loss低下やepsilon減少だけを学習成功の証拠としない。
8. **固定モデル評価**：未知seedでrandom/ルール/no_switchと比較。
   改善しなければそのまま報告し、観測/行動粒度/探索の限界を分析する。

各段階で変更ファイル・実行コマンド・成功/失敗/未確認を記録する。
既存online_dqn/centralized_dqn/logistic/no_switchの回帰テストも行う。

## 14. 今回の確認状況と次の作業

本書作成時：既存rlディレクトリ、DQN設計書、master CLIを確認した。
ソース変更・シミュレーション・学習・性能評価は未実行。
次は第13節の1から開始する。本書は実装後に実在するCLIと実行例を追記する。
特に「5周期で高H」「logisticより高性能」「未知変動へ適応」を設計段階では保証しない。
