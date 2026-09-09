# Proposed Centralized DQN 仕様書

作成日: 2026-09-09  
対象プロジェクト: ns-3.44 QoE-aware 5G/Wi-Fi AP/base station selection  
対象 method: `centralized_dqn`  
提案手法名: **Logistic Bootstrap Online Fine-tuning Centralized DQN**

---

## 1. 目的

本仕様書は，今後の `centralized_dqn` の研究方針を以下の方向へ変更するための設計を整理する。

従来の設計では，調和平均 `H` の改善に加えて，以下も評価・報酬設計上の重要指標として扱っていた。

```text
switch_count
num_degraded_users
```

しかし今後は，主評価指標からこれらを外し，以下を中心に研究を進める。

```text
1. 端末満足度の調和平均 H の上昇
2. 割当・推論・学習に要する計算時間
```

研究上の主張は，以下である。

```text
logistic regression は初期割当において非常に高い調和平均を達成できるが，
一度の一括割当で終了し，その後の cycle における実測環境変化には適応しない。

提案手法では，最初の cycle で logistic により高品質な初期割当を得た後，
cycle 2 以降で Centralized DQN が ns-3 実測 reward に基づいて online fine tuning を行い，
logistic-only よりも調和平均をさらに改善することを目指す。
```

---

## 2. 背景と問題設定

### 2.1 logistic baseline の性質

現在の ns-3 環境では，logistic regression による割当が高い調和平均を算出できることが確認されている。

logistic の特徴は以下である。

```text
入力: 各 UE の特徴量
出力: 各 UE の接続先 BS/AP
方式: 教師あり一括分類
学習元: Hungarian 法などで得た高 H 割当
online 更新: なし
cycle 間適応: なし
```

利点:

- 1 回の割当で高い調和平均に到達しやすい。
- 推論が軽い。
- 実装・評価が単純である。

制約:

- 一括割当後に追加の改善行動を行わない。
- ns-3 実測 reward に基づいて方策を更新しない。
- 推定 QoE と実測 QoE のズレを実行時に補正できない。
- cycle 間の通信品質変化・負荷変化に適応できない。

### 2.2 DQN を使う意義

DQN は logistic と異なり，逐次的な意思決定を行う。

```text
state_t -> action_t -> measured H_{t+1} -> reward_t -> online update
```

そのため，logistic 後の状態を初期解として利用し，その後に残る改善余地を online fine tuning によって探索できる。

本研究では DQN の有効性を以下の観点で示す。

```text
logistic による初期高 H 割当の後でも，
Centralized DQN が cycle 2 以降に追加的な H 改善を達成できるか。
```

---

## 3. 提案手法の概要

### 3.1 手法名

```text
Logistic Bootstrap Online Fine-tuning Centralized DQN
```

### 3.2 基本方針

任意の seed に対して，最初の cycle は必ず logistic を使用する。

```text
cycle 1:
    logistic regression による一括割当

cycle 2 以降:
    Centralized DQN による逐次割当
    ns-3 実測 H の変化を reward として online fine tuning
```

### 3.3 実装上の対応

既存実装では以下の仕組みを利用する。

```text
method = centralized_dqn
centralizedDqnBootstrapCycles = 1
```

実行時のコマンドライン引数としては，`--method=centralized_dqn` を指定する。
C++ 内部ではこの値が `m_assignmentMethod` / `APselectionInput::assignmentMethod` に渡される。

これにより，`centralized_dqn` 実行時に cycle 1 のみ `logistic_bootstrap` として動作し，cycle 2 以降は `centralized_dqn` として動作する。

---

## 4. 研究上の主張

本提案手法で示すべき主張は以下である。

```text
logistic-only は一度の一括割当で高 H に到達する強力な baseline である。
しかし，その後の cycle では割当を更新しないため，実測環境変化への適応性を持たない。

提案手法は logistic による高品質な初期割当を出発点とし，
Centralized DQN が cycle 2 以降の実測 reward に基づいて online fine tuning することで，
logistic-only より高い最終 H または高い H-AUC を達成することを目的とする。
```

ただし，「すべての seed で必ず H が上昇する」とは主張しない。

無線環境・トラフィック・測定ノイズにより，特定 seed では logistic 後に改善余地が小さい場合がある。
したがって，評価では複数 seed に対する平均，標準偏差，信頼区間を用いて統計的に比較する。

---

## 5. 評価指標

### 5.1 主評価指標

今後の主評価指標は以下とする。

| 指標 | 説明 |
|---|---|
| `H_t` | cycle `t` における端末満足度の調和平均 |
| `H_final` | 最終 cycle の調和平均 |
| `ΔH_after_logistic` | logistic bootstrap 後から最終 cycle までの H 改善量 |
| `AUC_H_after_logistic` | logistic bootstrap 後の H 推移の面積 |
| `decision_time_ms` | 1 cycle の割当判断に要した時間 |
| `server_roundtrip_time_ms` | ns-3 と Python DQN server 間の通信時間 |
| `inference_time_ms` | DQN の action 推論時間 |
| `online_update_time_ms` | DQN の online update 時間 |

### 5.2 主評価から外す指標

以下は主評価指標から外す。

```text
switch_count
num_degraded_users
```

ただし，デバッグ・異常解析・補足評価のため，ログ列としては残してよい。
研究上の主張，reward 設計，主要グラフでは使用しない。

### 5.3 推奨する派生指標

logistic 後の改善を明確に示すため，以下を集計する。

```text
H_logistic = cycle 2 で測定された H
H_final = 最終 cycle で測定された H
ΔH_after_logistic = H_final - H_logistic
AUC_H_after_logistic = sum_{t=2}^{T} H_t
```

`cycle 1` は logistic の割当実行 cycle であり，その結果が実測 H として安定して観測されるのは次 cycle になる可能性がある。
そのため，実験ログを確認し，`H_logistic` をどの cycle の H と定義するかは実装上明記する。

---

## 6. 報酬設計

### 6.1 旧 reward

従来設計では，以下のように切替回数と悪化端末数の penalty を含めていた。

```text
reward = measured_reward
       - alpha * switch_count
       - beta  * num_degraded_users
```

### 6.2 新 reward

新方針では，reward は調和平均の実測上昇のみとする。

```text
reward_t = H_{t+1}^{measured} - H_t^{measured}
```

実装上は，既存の `prev_cycle_measured_reward` をそのまま reward として使う。

```python
reward = prev_cycle_measured_reward
```

### 6.3 penalty の扱い

以下の penalty は使用しない。

```text
reward_switch_penalty_alpha = 0.0
reward_degraded_penalty_beta = 0.0
```

C++ 側で `prev_cycle_reward` を作る場合も，以下のように単純化する。

```cpp
const double prevCycleReward = m_lastMeasuredRewardFromPrevious;
```

Python server 側でも同様に，online update 用 reward を以下にする。

```python
reward = prev_cycle_measured_reward
```

---

## 7. Cycle 設計

### 7.1 基本シーケンス

```text
cycle 1:
    1. 現在の全 UE/AP 状態を測定
    2. logistic regression により一括割当を決定
    3. 割当を ns-3 に適用
    4. bootstrap cycle としてログに記録

cycle 2:
    1. logistic 割当後の実測 H を観測
    2. Centralized DQN が全体状態から action を選択
    3. action を ns-3 に適用
    4. 次 cycle で reward を確定

cycle 3 以降:
    1. 前 cycle の reward = H_t - H_{t-1} を用いて DQN を online update
    2. 現 cycle の状態から action を選択
    3. 割当を適用
    4. 次 cycle で reward を確定
```

### 7.2 bootstrap cycle の扱い

logistic bootstrap cycle は DQN の性能評価から分けて扱う。

```text
bootstrap_cycle_flag = 1: logistic bootstrap
bootstrap_cycle_flag = 0: DQN decision cycle
```

評価時には，以下を分けて集計する。

```text
logistic bootstrap による H 改善
DQN online fine tuning による H 改善
```

---

## 8. 状態設計

### 8.1 当面の推奨

既存実装との互換性を重視し，当面は以下を使用する。

```text
schema_version = centralized_state_v2_onehot
model_type = factorized_v2
```

理由:

- `app_type` と `current_bs_id` が one-hot 化されている。
- AP type も特徴量に含められる。
- `factorized_v2` は UE/AP/global 特徴を分けて扱える。
- 既存の C++/Python protocol を大きく壊さずに利用できる。

### 8.2 将来的な v3 schema

新方針をより明確に反映する場合，新しい state schema を追加する。

```text
centralized_state_v3_honly
```

v3 では，主評価から外した以下の特徴量を global state から削除する。

```text
previous_switch_count
previous_num_degraded_users
```

代わりに，logistic 後の改善を明示する特徴量を追加する。

```text
GLOBAL_FEATURES_V3 = [
    "cycle_id",
    "harmonic_mean",
    "previous_measured_reward",
    "h_after_logistic",
    "delta_from_logistic",
]
```

---

## 9. 行動設計

Centralized DQN の行動は現状を維持する。

```text
action_id = target_ue_index * num_aps + selected_bs_id
```

80 UE，3 AP の場合，行動数は以下である。

```text
action_dim = 80 * 3 = 240
```

DQN は以下を同時に選ぶ。

```text
1. どの UE を対象にするか
2. どの BS/AP へ接続させるか
```

### 9.1 valid action

C++ 側で以下の action は valid action から除外する。

```text
現在と同じ BS/AP への action
handover cooldown 中の UE に対する action
同一 cycle 内ですでに切り替えた UE に対する action
banned action
```

### 9.2 safety threshold

新方針では H 改善を主目的とするため，基本設定は以下とする。

```text
onlineDqnSafetyThreshold = 0.0
```

ただし，測定ノイズを考慮する比較実験では以下も検討する。

```text
onlineDqnSafetyThreshold = 0.0001
```

強すぎる safety filter は greedy baseline に近づき，DQN の寄与が見えにくくなるため注意する。

---

## 10. Online fine tuning 設計

### 10.1 初期モデル

当面の方針では，**logistic teacher による事前学習は行わない**。

理由は，本研究で DQN に担わせたい役割が，logistic の一括割当を模倣することではなく，以下だからである。

```text
logistic 後の高 H 状態を初期解として利用し，
その後の cycle で少数端末を追加調整することで，
どの程度 H をさらに上昇させられるかを検証する。
```

したがって，最初の実験では以下の構成を採用する。

```text
cycle 1:
    logistic regression による bootstrap 割当

cycle 2 以降:
    ランダム初期化または既存 DQN 初期化から，
    H 差分 reward のみを用いて online fine tuning
```

この構成により，提案手法は logistic imitation ではなく，**post-logistic residual improvement** を行う DQN として位置づけられる。

### 10.1.1 事前学習を使わない初期方針: 案 A

初期方針は以下とする。

```text
案 A:
    事前学習なし
    cycle 1 のみ logistic
    cycle 2 以降は DQN が online fine tuning により追加改善 action を探索
```

利点:

- logistic の挙動を毎 cycle 模倣するモデルにならない。
- DQN の役割が「logistic 後の追加改善」であることを明確に示せる。
- 提案手法の独自性を説明しやすい。

懸念:

- ns-3 の cycle 数が少ない場合，online 学習サンプルが不足しやすい。
- 初期 DQN action が不安定になり，H を下げる可能性がある。
- seed によって改善量のばらつきが大きくなる可能性がある。

### 10.1.2 不安定な場合の移行方針: 案 B

案 A が不安定な場合のみ，案 B に移行する。

ただし，案 B でも logistic の一括割当そのものを教師として模倣するのではなく，**logistic 後の残差改善 action** を教師にする。

```text
案 B:
    logistic 後状態のみを対象にする
    教師 action は logistic final assignment ではなく，
    logistic 後に推定 ΔH が正となる少数追加切替 action とする
```

案 B の目的は，DQN に logistic を再現させることではなく，以下を学習させることである。

```text
logistic 後の状態で，どの UE を少数だけ移動させると H が上がりやすいか
```

案 B は，案 A の online 学習が不安定な場合の安定化手段として扱う。

### 10.2 transition

online fine tuning では以下の transition を replay buffer に保存する。

```text
(s_t, a_t, r_t, s_{t+1}, done)
```

ここで，

```text
r_t = H_{t+1}^{measured} - H_t^{measured}
```

である。

### 10.3 update timing

既存の `rl/centralized_server.py` と同様，前 cycle の pending action に対して，次 cycle の状態が届いたタイミングで reward を確定して update する。

```text
cycle t で action 実行
cycle t+1 の request 到着時に reward 確定
DQN update
cycle t+1 の action 選択
```

---

## 11. 計算時間ログ設計

今後は計算時間を主評価指標に含めるため，以下をログに追加する。

### 11.1 C++ 側で測定する時間

`centralized_dqn_assignment()` 内で測定する。

```text
state_build_time_ms
server_roundtrip_time_ms
action_apply_time_ms
decision_total_time_ms
```

### 11.2 Python 側で測定する時間

`rl/centralized_server.py` の `handle()` 内で測定する。

```text
python_state_vectorize_time_ms
python_update_time_ms
python_inference_time_ms
python_total_handle_time_ms
```

### 11.3 ログ出力先

基本的には `decision_log` に追加する。

追加列案:

```text
state_build_time_ms
server_roundtrip_time_ms
python_inference_time_ms
python_update_time_ms
python_total_handle_time_ms
decision_total_time_ms
```

---

## 12. 比較実験設計

### 12.1 比較手法

提案手法の有効性を示すため，以下を比較する。

| 手法 | 内容 |
|---|---|
| `logistic_only` | cycle 1 で logistic，その後は割当維持 |
| `logistic_random` | cycle 1 で logistic，cycle 2 以降は valid action から random |
| `logistic_greedy` | cycle 1 で logistic，cycle 2 以降は推定 ΔH 最大 action |
| `logistic_dqn_evalonly` | cycle 1 で logistic，cycle 2 以降は DQN 推論のみ，online update なし |
| `proposed_A` | cycle 1 で logistic，cycle 2 以降は事前学習なし DQN + online fine tuning |
| `proposed_B_optional` | 案 A が不安定な場合のみ使用。logistic 後の残差改善 action で軽く事前学習した DQN + online fine tuning |

### 12.2 seed 設計

1 seed のみで結論を出さない。

推奨:

```text
seed 数: 10 以上
可能なら: 30 seed 以上
```

各 seed で同じ初期条件を使い，手法のみを変える。

### 12.3 集計項目

各 seed で以下を集計する。

```text
H_logistic
H_final
ΔH_after_logistic
AUC_H_after_logistic
mean_decision_time_ms
p95_decision_time_ms
mean_inference_time_ms
mean_online_update_time_ms
```

### 12.4 推奨グラフ

```text
1. cycle ごとの H 推移
2. ΔH_after_logistic の boxplot
3. H_final の手法別 bar plot
4. AUC_H_after_logistic の手法別 bar plot
5. decision_time_ms の手法別 bar plot
6. ΔH_after_logistic と decision_time_ms の scatter plot
```

---

## 13. 実装変更方針

### Step 1: reward penalty の除去

対象:

```text
rl/centralized_server.py
contrib/kameda/model/server/APselection.cc
```

変更:

```text
reward = prev_cycle_measured_reward
```

`reward_switch_penalty_alpha` と `reward_degraded_penalty_beta` は 0 とする。

### Step 2: 計算時間ログの追加

対象:

```text
contrib/kameda/model/server/APselection.cc
rl/centralized_server.py
```

追加:

```text
decision_total_time_ms
server_roundtrip_time_ms
python_inference_time_ms
python_update_time_ms
python_total_handle_time_ms
```

### Step 3: logistic bootstrap 実験条件の固定

基本実行条件:

```text
method = centralized_dqn
centralizedDqnBootstrapCycles = 1
centralizedDqnStateSchema = centralized_state_v2_onehot
model_type = factorized_v2
checkpoint = none  # 案 A: logistic teacher 事前学習は使わない
rewardSwitchPenaltyAlpha = 0.0
rewardDegradedPenaltyBeta = 0.0
onlineDqnSafetyThreshold = 0.0
```

ns-3 実行時には `--method=centralized_dqn` として指定する。

### Step 4: 比較実験スクリプトの整備

同一 seed に対して以下を自動実行・集計する。

```text
logistic_only
logistic_random
logistic_greedy
logistic_dqn_evalonly
proposed_A
proposed_B_optional
```

### Step 5: v3 state schema の検討

必要に応じて，`centralized_state_v3_honly` を追加する。

---

## 14. 成功条件

提案手法が有効であると判断する条件は以下である。

```text
1. 複数 seed 平均で logistic_only より H_final が高い
または
2. 複数 seed 平均で logistic_only より AUC_H_after_logistic が高い

かつ

3. 計算時間が実験 cycle 間隔に対して十分小さい
```

例:

```text
mean(ΔH_after_logistic_proposed) > mean(ΔH_after_logistic_logistic_only)
mean(AUC_H_after_logistic_proposed) > mean(AUC_H_after_logistic_logistic_only)
mean(decision_time_ms) が cycle interval より十分小さい
```

---

## 15. 注意点

- logistic が非常に強い baseline であるため，DQN が全 seed で必ず改善するとは限らない。
- 初期方針では logistic teacher による事前学習を行わない。これは DQN が logistic の一括割当を毎 cycle 模倣することを避けるためである。
- 改善量が小さい場合は，平均値だけでなく seed 別の分布を見る。
- safety filter が強すぎると DQN ではなく greedy 的な改善になるため，比較実験で切り分ける。
- switch_count と num_degraded_users は主評価から外すが，異常解析用ログとしては残す。
- 実験結果を推測で補完しない。未実行の結果は必ず未実行と明記する。

---

## 16. 現時点の推奨実装設定

まずは以下の設定を標準条件とする。

```text
method: centralized_dqn  # ns-3 CLI: --method=centralized_dqn
bootstrapCycles: 1       # ns-3 CLI: --centralizedDqnBootstrapCycles=1
state_schema: centralized_state_v2_onehot
model_type: factorized_v2
checkpoint: none  # 案 A: 事前学習なし
online_update: enabled
reward: measured H delta only
switch penalty: disabled
num degraded penalty: disabled
safety threshold: 0.0
```

この条件を提案手法の最小構成である案 A とし，logistic-only および online update なし DQN と比較する。案 A が不安定な場合のみ，logistic 後の残差改善 action を用いた案 B に移行する。

---

## 17. 初回検証条件

案 A の最初の検証では，以下の条件で固定する。

```text
method: centralized_dqn
centralizedDqnBootstrapCycles: 1
maxSwitches: 1
numCycles: 5
seed 数: 10 個程度
epsilon: 0.02
checkpoint: none
online_update: enabled
```

この条件の意図は以下である。

```text
1. cycle 1 で logistic により高 H な初期割当を作る。
2. cycle 2 以降，DQN は 1 cycle あたり最大 1 台のみ追加切替を行う。
3. 事前学習なしで，online fine tuning のみで post-logistic な改善余地を探索する。
4. まずは 5 cycle / 約 10 seed の小規模条件で，H が改善するか，または不安定化するかを確認する。
```

初回検証では，性能の最終結論は出さない。主目的は以下の確認である。

```text
cycle 1 が logistic_bootstrap として動作するか
cycle 2 以降が centralized_dqn として動作するか
checkpoint なしで DQN server が動作するか
reward が H 差分のみになっているか
K=1 で少数端末の追加調整になっているか
H_logistic から H_final までの変化を seed ごとに確認できるか
```

案 A の初回検証で明らかに不安定な場合のみ，案 B として logistic 後の残差改善 action による軽い事前学習を検討する。
