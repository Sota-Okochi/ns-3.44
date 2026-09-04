# Centralized DQN 設計書

作成日: 2026-08-31  
更新日: 2026-08-31  
対象プロジェクト: ns-3.44 QoE-aware 5G/Wi-Fi AP/base station selection  
対象 method 案: `centralized_dqn` または `online_centralized_dqn`

---

## 1. 目的

本設計書は，既存の `online_dqn` よりも全体最適化を強く意識した **Centralized DQN** を実装するための方針を整理する。

現在の `online_dqn` は，C++ 側が候補 UE を選び，DQN はその UE の接続先 BS/AP を選択する。

```text
入力: 候補 UE 1 台の状態
出力: selected_bs_id ∈ {0, 1, 2}
```

一方，logistic baseline は全 UE に対して一括で接続先を決めるため，短い cycle 数でも良い全体割当に到達しやすい。実験結果では，現状の online DQN より logistic の方が高い調和平均を示している。

Centralized DQN の目的は，DQN 自身が

```text
どの UE を動かすか
どの BS/AP へ動かすか
いつ切替を止めるか
```

を全体状態から判断し，端末満足度の調和平均 `H` を改善することである。

---

## 1.1 確定した初期方針

本設計では，初期実装の目標と制約を以下で固定する。

```text
目標: logistic に近い H を，より少ない切替数・degraded users で達成する
1 cycle 内の最大行動数: K = 80
STOP action: 追加しない
停止条件: 全候補の予測改善が threshold 未満，または safety filter を通過する候補がない
初期 reward penalty: alpha = 0.001, beta = 0.001
```

ここで，`K=80` は「最大 80 回まで試行できる」という上限であり，必ず 80 UE を切り替えるという意味ではない。
実際の切替数は，DQN の action，same-BS 判定，cooldown 判定，safety filter によって 80 未満に抑えられる想定である。

## 2. 既存手法との違い

### 2.1 logistic baseline

logistic は以下の性質を持つ。

```text
全 UE に対して一括で接続先 AP を予測
教師は Hungarian 法由来の調和平均が高い割当
K 制限なし
safety filter なし
online learning なし
```

利点:

- 一度に多数 UE を切り替えられる
- 調和平均が高い割当に早く到達しやすい
- 推論が軽い

制約:

- ns-3 実測 reward による適応はしない
- 長期的な cycle 間効果を学習しない
- 切替回数や悪化端末数を reward として直接最適化しない

### 2.2 現状 `online_dqn`

現状 `online_dqn` は以下の性質を持つ。

```text
C++ 側が rescue/offload candidate を選ぶ
DQN は候補 UE の移動先 AP のみ選ぶ
K 制限あり
safety filter あり
online learning 可能
```

課題:

- 候補 UE 選択が C++ ルールに依存する
- 全 UE 一括割当に比べて良い全体配置へ到達しにくい
- K=20/40/80 など学習時に少ない値を見ていない場合，budget 特徴量が分布外入力になり挙動が崩れる可能性がある
- safety filter による `safety_h_delta` skip が多く，有効な大規模再配置が進みにくい

### 2.3 Centralized DQN

Centralized DQN は，全体状態を入力し，UE と移動先 AP の組を直接選ぶ。

```text
入力: 全 UE 状態 + 全 AP 状態 + 全体 QoE
出力: action = (target_ue_id, selected_bs_id)
```

80 UE, 3 AP の場合，行動数は以下となる。

```text
80 × 3 = 240 actions
```

初期実装では STOP 専用 action は追加しない。停止判断は，DQN の全候補 action のうち，safety filter を通過し，かつ予測改善が threshold 以上の action が存在しない場合に行う。

---

## 3. 期待される利点

Centralized DQN によって logistic に対して期待できる利点は以下である。

1. **どの UE を動かすかも学習できる**
   - rescue UE と offload UE の選択を C++ ルールに固定しない。

2. **複数 cycle にわたる効果を学習できる**
   - `H_{t+1} - H_t` だけでなく，将来的な累積 reward を考慮できる。

3. **H・切替回数・悪化端末数のトレードオフを直接最適化できる**
   - logistic より少ない切替で同程度の H を目指せる。

4. **ns-3 実測 reward に適応できる**
   - Hungarian/logistic 教師と ns-3 実測のズレを online fine-tuning で補正できる。

5. **提案手法としての主張が明確になる**
   - logistic は教師あり一括分類器，Centralized DQN は実測 reward に基づく逐次全体制御器として差別化できる。

---

## 4. 状態設計

### 4.1 基本方針

Centralized DQN の状態は，1 UE だけではなく，全 UE と全 AP の状態を含む。

```text
state_t = [UE matrix, AP matrix, global features]
```

最初の実装では端末数を 80 に固定し，固定長ベクトルとして扱う。

将来，端末数可変に対応する場合は GNN / Transformer / padding mask を検討する。

### 4.2 UE 特徴量

各 UE `i` について以下を持つ。

```text
ue_id_normalized
current_bs_id
app_type
tp_mbps
rtt_ms
satisfaction
measurement_valid
handover_cooldown_flag
last_switch_age
```

候補効果を入れる場合は以下も追加する。

```text
estimated_satisfaction_if_ap0
estimated_satisfaction_if_ap1
estimated_satisfaction_if_ap2
estimated_h_delta_if_ap0
estimated_h_delta_if_ap1
estimated_h_delta_if_ap2
best_estimated_h_delta
```

### 4.3 AP 特徴量

各 AP `b` について以下を持つ。

```text
num_users_ap_b
monitor_rtt_ap_b
mean_tp_ap_b
mean_satisfaction_ap_b
num_unsatisfied_ap_b
```

### 4.4 global 特徴量

全体特徴量は以下とする。

```text
cycle_id
harmonic_mean
num_unsatisfied_users
previous_switch_count
previous_num_degraded_users
previous_measured_reward
```

### 4.5 budget 特徴量について

現状の online DQN では，以下の budget 特徴量が K=20/40/80 で分布外入力になる可能性があった。

```text
effective_max_switches
applied_switches_in_cycle
remaining_switch_budget
```

Centralized DQN の初期実装では，これらは **入力から外す** 方針とする。

理由:

- まずは「どの UE をどの AP に移すべきか」を学習することを優先する。
- K の絶対値に依存した方策を避ける。
- 大規模 K での分布外問題を避ける。

切替数制御は，初期段階では C++ 側の hard limit と safety filter による停止で扱う。

---

## 5. 行動設計

### 5.1 最小構成

80 UE, 3 AP 固定の場合，行動は以下とする。

```text
a = target_ue_id * 3 + selected_bs_id
```

対応:

```text
target_ue_id = a / 3
selected_bs_id = a % 3
```

行動数:

```text
action_dim = num_ues × num_aps = 80 × 3 = 240
```

同じ BS を選んだ場合は no-op とする。

```text
selected_bs_id == current_bs_id => no handover
```

### 5.2 STOP 行動

初期実装では STOP 専用 action は入れない。

採用方針:

```text
action_dim = num_ues × num_aps = 240
STOP action なし
最大 K = 80 まで行動候補を評価
全候補の予測改善が threshold 未満なら，その cycle の追加切替を終了
```

理由:

- STOP 教師ラベルを logistic 教師から作るのが難しい。
- action_dim を 241 に増やすより，まず UE 選択と AP 選択を学習する方が重要である。
- 無駄な切替は STOP action ではなく safety filter と小さい switch penalty で抑える。

後続で「DQN 自身に明示的に停止判断を学習させる」必要が出た場合のみ，以下を追加する。

```text
action_dim = num_ues × num_aps + 1
last action = STOP
```

---

## 6. 報酬設計

基本報酬は，次 cycle で観測された調和平均の変化とする。

```text
reward = H_after_measured - H_before
```

切替安定性を考慮する場合は以下を用いる。

```text
reward =
    H_after_measured - H_before
    - alpha * switch_count
    - beta  * num_degraded_users
```

ここで，

- `H_before`: cycle t の切替前調和平均
- `H_after_measured`: cycle t+1 で測定された調和平均
- `switch_count`: cycle t で実際に切り替えた UE 数
- `num_degraded_users`: cycle t の行動前満足度より cycle t+1 の満足度が下がった UE 数

初期値:

```text
alpha = 0.001
beta  = 0.001
```

初期段階では，まず H を落とさないことを優先し，switch/degraded penalty は小さく始める。
その後，logistic と同程度の H を維持できる範囲で `alpha`, `beta` を大きくし，切替回数と悪化端末数を抑えられるか確認する。

---

## 7. Safety filter

Centralized DQN でも，初期段階では safety filter を残す。

ただし，現状の `online_dqn` と同じ強い filter では大規模再配置を妨げる可能性があるため，以下の 3 条件を比較する。

```text
threshold = 0.0
threshold = -0.005
threshold = -0.01
```

基本条件:

```text
invalid action => skip
same_bs => no-op
handover cooldown 中の UE => skip
estimated_h_delta_if_selected < threshold => skip
```

ただし，最終的には reward で悪い行動を抑える方が望ましいため，safety filter は実験条件として明記する。

---

## 8. 学習手順

### Phase 1: 教師データ作成

初期方策は Behavior Cloning で作る。最初の教師データは logistic の出力割当を中心に作成する。

教師候補:

```text
1. logistic の成功割当
2. Hungarian 教師割当
3. multi_greedy / multi_offload の best cycle assignment
```

特に logistic が高い H を示しているため，最初は logistic の出力割当を主教師として用いる。

教師データ形式:

```text
state_t, target_ue_id, selected_bs_id, expert_action
```

または，1 cycle 内で複数 step に展開する。

```text
state_before_step_k -> action_k = (ue_i, bs_b)
```



### 8.1 logistic 教師データで本当に学習できること・できないこと

logistic を教師データにすることで，以下は **初期方策としては実現可能** である。
ただし，Behavior Cloning だけで全てが実現できるわけではなく，一部は online fine-tuning によって初めて可能になる。

#### 可能になること

1. **どの UE を動かすかも学習できる**

   logistic の最終割当と現在割当の差分を action に分解すれば，

   ```text
   UE i を BS/AP b に移す
   ```

   という教師 action を作れる。
   これにより，rescue UE / offload UE の選択を C++ ルールに固定せず，DQN が全 UE の中から target UE を選ぶ初期方策を学習できる。

2. **logistic に近い全体割当へ向かう初期方策を得られる**

   logistic は Hungarian 教師に近い高 H 割当を出せているため，その差分を模倣することで，centralized DQN は最初から完全ランダムではなく，高 H 割当に近づく方向の行動を取りやすくなる。

3. **提案手法としての出発点が明確になる**

   logistic は教師あり一括分類器，Centralized DQN はその模倣から開始し，その後に実測 reward で更新する逐次制御器，という位置づけにできる。

#### Behavior Cloning だけでは不十分なこと

1. **複数 cycle にわたる累積 reward の最適化**

   logistic 教師からの BC は基本的に「その状態で logistic ならどの割当を選ぶか」を模倣するだけである。
   そのため，`H_{t+1} - H_t` だけでなく将来的な累積 reward を考慮する能力は，BC だけでは獲得できない。
   これは online DQN fine-tuning で，

   ```text
   Q(s_t, a_t) ≈ r_t + gamma max_a Q(s_{t+1}, a)
   ```

   を学習して初めて狙える。

2. **H・切替回数・悪化端末数の直接トレードオフ最適化**

   logistic 教師は，切替回数や `num_degraded_users` を直接 reward として最適化していない。
   したがって，BC だけでは logistic より少ない切替や少ない degraded users を保証できない。
   これも以下の reward で fine-tuning する必要がある。

   ```text
   reward = H_after - H_before
            - alpha * switch_count
            - beta  * num_degraded_users
   ```

3. **ns-3 実測 reward への適応**

   logistic/Hungarian 教師は推定値に基づく割当であり，ns-3 実測の TP/RTT/QoE とずれる可能性がある。
   このずれを補正するには，eval-only ではなく，実測 reward を replay buffer に入れて online fine-tuning する必要がある。

まとめると，logistic 教師は **良い初期方策を作るためには有効** だが，以下の研究上の利点を本当に主張するには，BC 後の online fine-tuning と比較実験が必要である。

```text
logistic に近い H
より少ない switch_count
より少ない num_degraded_users
ns-3 実測 reward への適応
```

### Phase 2: Centralized DQN 事前学習

教師 action を Q network の分類問題として学習する。

```text
loss = CrossEntropyLoss(Q(s), expert_action)
```

最初は DQN loss ではなく，分類 loss による imitation でよい。

### Phase 3: eval-only 閉ループ評価

学習済み centralized model を ns-3 に接続し，重み更新なしで評価する。

```text
epsilon = 0
update = false
```

比較対象:

```text
logistic
online_dqn_K10/K20/K40/K80
centralized_dqn
multi_greedy
rulebase
random
no_switch
```

### Phase 4: online fine-tuning

事前学習済みモデルを初期値として，ns-3 実測 reward で fine-tuning する。

```text
(s_t, a_t, r_t, s_{t+1})
```

DQN 改良候補:

```text
Double DQN
Dueling DQN
Prioritized Replay
```

---

## 9. モデル構成

### 9.1 初期モデル

最初は MLP で固定長状態を扱う。

```text
input_dim = num_ues × ue_feature_dim + num_aps × ap_feature_dim + global_feature_dim
hidden_dim = 256 or 512
action_dim = num_ues × num_aps
```

出力:

```text
Q(s, ue0->ap0), Q(s, ue0->ap1), ..., Q(s, ue79->ap2)
```

### 9.2 推奨拡張

性能が不足する場合は以下を検討する。

```text
Dueling Double DQN
NoisyNet exploration
Prioritized Experience Replay
Transformer encoder over UE set
GNN over UE-AP bipartite graph
```

ただし，初期実装では複雑化を避け，MLP + Double DQN から始める。

---

## 10. C++ / ns-3 側の実装方針

追加 method 名候補:

```text
centralized_dqn
online_centralized_dqn
```

初期は TCP/JSON server 方式を既存 `online_dqn` から流用する。

必要な主な関数案:

```cpp
std::string BuildCentralizedDqnStateJson(...);
bool SendCentralizedStateReceiveAction(...);
void centralized_dqn_assignment();
```

`centralized_dqn_assignment()` の流れ:

```text
1. 現在 assignment を取得
2. H_before を計算
3. 全 UE / AP / global state を JSON 化
4. Python server に送信
5. action = (target_ue_id, selected_bs_id) を受信
6. safety filter を確認
7. 適用可能なら assignment を更新
8. 最大 step 数まで 3〜7 を繰り返す
9. H_after_estimated, switch_count を記録
10. handover callback を呼ぶ
```

初期の最大 step 数:

```text
maxCentralizedSteps = 80
```

ただし，実際に 80 回必ず切り替えるわけではない。各 step で全候補 action を評価し，safety filter を通過する有効 action がなければ，その cycle の追加切替を終了する。

ただし，same_bs や safety skip が多い場合でも無限ループしないよう，同一 action の再選択管理が必要。

---

## 11. Python 側の実装方針

新規ファイル案:

```text
rl/centralized_protocol.py
rl/centralized_agent.py
rl/centralized_server.py
rl/pretrain_centralized.py
```

既存の `rl/server.py` と混在させるより，初期は別ファイルに分ける。

理由:

- 状態形式が 1 UE vector から global vector に変わる
- action の意味が selected_bs_id から `(target_ue_id, selected_bs_id)` に変わる
- checkpoint の互換性がない

JSON 応答例:

```json
{
  "type": "action",
  "target_ue_id": 12,
  "selected_bs_id": 0,
  "action_id": 36,
  "q_value": 0.123
}
```

---

## 12. ログ設計

`master_log` は既存列を維持する。

`decision_log` には centralized DQN 用に以下を追加する。

```text
action_id
target_ue_id
selected_bs_id
previous_bs_id
applied
skip_reason
q_selected
q_top1
q_top2
q_top3
selected_estimated_h_delta
h_before_step_estimated
h_after_step_estimated
estimated_marginal_delta
```

cycle 単位では以下を必ず記録する。

```text
h_before
h_after_estimated
h_after_measured
measured_reward
cycle_reward
switch_count
num_degraded_users
```

---

## 13. 評価指標

必須指標:

```text
harmonic_mean
num_unsatisfied_users
switch_count
num_degraded_users
min satisfaction
p10 satisfaction
median satisfaction
mean TP
mean RTT
APごとの接続数
seed間平均・標準偏差
```

logistic との比較では，特に以下を重視する。

```text
logistic と同等 H を少ない切替で達成できるか
logistic と同等 H を少ない degraded users で達成できるか
logistic より H が高いか
logistic より degraded users を抑えられるか
seed 依存性が小さいか
```

---

## 14. 実験順序

### Step 1: offline imitation

```text
logistic / Hungarian / best assignment から centralized DQN を事前学習
```

### Step 2: eval-only 動作確認

```text
seed = 1002, 1003, 1007
safetyThreshold = 0.0
maxCentralizedSteps = 80
STOP action = disabled
```

### Step 3: safety threshold 感度分析

```text
threshold = 0.0, -0.005, -0.01
```

### Step 4: baseline 比較

```text
logistic
online_dqn_K40
online_dqn_K80
centralized_dqn
multi_greedy
random
rulebase
no_switch
```

### Step 5: online fine-tuning

training seed と test seed を分ける。

```text
training seeds: 1〜50
validation seeds: 1001〜1020
test seeds: 2001〜2030
```

---

## 15. 実装時の注意

- ns-3.44 前提を崩さない。
- 既存 `online_dqn` は壊さず，新 method として追加する。
- checkpoint は既存 26 次元 online DQN と互換性がないため，保存名を分ける。
- 端末数 80 固定の初期実装でよいが，その制約をログ・README に明記する。
- `num_degraded_users` は全 method と同じ定義で比較する。
- 実験結果を推測で補完しない。

---

## 16. 初期実装 TODO

- [ ] `centralized_dqn` method 名を C++ に追加する。
- [ ] 全 UE/AP/global state を JSON 化する `BuildCentralizedDqnStateJson()` を追加する。
- [ ] Python 側に `rl/centralized_server.py` を追加する。
- [ ] `action_id -> (target_ue_id, selected_bs_id)` の変換を実装する。
- [ ] logistic / best assignment ログから centralized 教師データを作る。
- [ ] MLP + Behavior Cloning の最小モデルを作る。
- [ ] eval-only で seed 1002/1003/1007 を実行する。
- [ ] logistic と H / switch_count / num_degraded_users を比較する。
- [ ] 問題がなければ online fine-tuning に進む。

---

## 17. 研究上の位置づけ

Centralized DQN は，logistic baseline と同じく全体割当を意識しつつ，logistic にはない実測 reward 適応と逐次制御を導入する手法である。

論文上は以下のように位置づける。

```text
logistic は Hungarian 教師を模倣する一括分類型 baseline である。
一方，Centralized DQN は全 UE・全 AP の状態を観測し，UE と接続先 AP の組を行動として選択することで，調和平均，切替回数，悪化端末数のトレードオフを reward により直接学習する。
```

最初の目標は，logistic を常に H で上回ることではなく，以下のいずれかを達成することである。

```text
1. logistic と同程度の H をより少ない切替数で達成する
2. logistic と同程度の H をより少ない degraded users で達成する
3. logistic より seed 間ばらつきが小さい方策を得る
4. online fine-tuning 後に logistic を上回る H を達成する
```
---

## 18. Logistic bootstrap + Centralized DQN

Centralized DQN が logistic の一括割当を最初から逐次的に完全模倣するのは難しいため、初期 cycle だけ logistic による一括割当を行い、その後を centralized DQN で微調整する hybrid 構成を追加する。

### 18.1 目的

```text
cycle 1: logistic で高い初期 H を作る
cycle 2以降: centralized_dqn で H を壊さず、切替数・degraded users を抑えながら調整する
```

この方式は DQN 単体で logistic を置き換える手法ではなく、以下の役割分担として扱う。

```text
logistic: 初期一括割当
centralized_dqn: 実測 reward に基づく逐次的な再調整
```

### 18.2 実行オプション

```bash
--method centralized_dqn \
--centralizedDqnBootstrapCycles 1
```

`centralizedDqnBootstrapCycles` の意味は以下の通り。

| 値 | 動作 |
|---:|---|
| 0 | 従来通り cycle 1 から centralized_dqn |
| 1 | cycle 1 のみ logistic、cycle 2 以降 centralized_dqn |
| N | cycle 1〜N を logistic、cycle N+1 以降 centralized_dqn |

出力先は通常の `centralized_dqn_K<maxSwitches>` と区別するため、bootstrap 有効時は以下の形式にする。

```text
OUTPUT/<端末数>/centralized_dqn_K<maxSwitches>_bootstrap<N>/
```

### 18.3 ログ方針

`method` は実験条件として `centralized_dqn` のまま残し、実際にその cycle で使った手法は `effective_method` に記録する。

```text
cycle 1: method=centralized_dqn, effective_method=logistic_bootstrap
cycle 2: method=centralized_dqn, effective_method=centralized_dqn
```

`master_log`、`decision_log`、`measured_reward_log` には比較用に以下を追加する。

```text
effective_method
bootstrap_cycle_flag
```

### 18.4 online fine-tuning 時の扱い

bootstrap cycle の logistic 行動は DQN が選んだ行動ではないため、DQN の行動系列としては扱わない。cycle 2 以降に centralized DQN が選択した行動を online fine-tuning の対象とする。

初期検証コマンド例:

```bash
python3 comand/main_comand.py \
    --preset custom \
    --method centralized_dqn \
    --maxSwitches 80 \
    --centralizedDqnBootstrapCycles 1 \
    --seeds 1002 1003 1007 \
    --checkpoint models/centralized_dqn_bc_logistic_seed1to54_positive_norm.pt \
    --eval-only \
    --epsilon 0.0 \
    --onlineDqnSafetyThreshold 0.005 \
    --port auto
```
