# Multi Online DQN 設計書

本書は，5G/Wi-Fi 混在環境における QoE 指向基地局選択に対して，1 サイクル内で複数端末を切り替えつつ，サイクル進行に応じて切替端末数を段階的に減少させる Online DQN 手法の設計を整理する。

## 1. 目的

現状の `online_dqn` は，各サイクルで候補端末に対して DQN が接続先基地局を選択する方式である。基本的な行動空間は以下である。

```text
0: AP0 / 5G gNB
1: AP1 / Wi-Fi AP1
2: AP2 / Wi-Fi AP2
```

一方で，複数端末を同一サイクル内で切り替える場合，低満足端末を短時間で救済できる可能性がある反面，基地局負荷が急激に変化し，切替対象外端末の QoE 悪化や接続状態の振動を招く可能性がある。

そこで本手法では，以下を目的とする。

- 初期サイクルでは複数端末の切替を許容し，低満足端末を早期に救済する。
- 後続サイクルでは切替可能台数を段階的に減らし，接続状態を安定化する。
- DQN による接続先選択と，サイクルごとの切替数制御を組み合わせる。
- 調和平均だけでなく，切替回数，悪化端末数，満足度変動幅も評価する。

本書では，この方式を **Multi Online DQN** と呼ぶ。

## 2. 基本方針

各サイクル `t` において，最大切替数 `K_t` を設定する。

```text
cycle 1: K_1
cycle 2: K_2
cycle 3: K_3
...
```

ここで `K_t` は固定値ではなく，サイクルが進むにつれて小さくする。

例:

```text
K_schedule = [5, 4, 3, 2, 1]
```

この場合，

| cycle | 最大切替数 `K_t` |
|---:|---:|
| 1 | 5 |
| 2 | 4 |
| 3 | 3 |
| 4 | 2 |
| 5 以降 | 1 |

とする。

より一般的には，以下のように定義できる。

```text
K_t = max(K_min, K_max - decay_rate * (t - 1))
```

例:

```text
K_max = 5
K_min = 1
decay_rate = 1
```

このとき，

```text
K_t = 5, 4, 3, 2, 1, 1, ...
```

となる。

## 3. 現状 online_dqn との差分

### 3.1 現状 online_dqn

現状の `online_dqn` は，主に以下の構造で動作する。

```text
for candidate UE in candidate_set:
    if applied >= m_MaxSwitches:
        break

    state を作成
    DQN が selected_bs_id を選択

    if safety filter を満たす:
        切替を適用
        applied += 1
```

この方式では，1 サイクルあたりの最大切替数は `m_MaxSwitches` により固定的に与えられる。

### 3.2 Multi Online DQN

Multi Online DQN では，`m_MaxSwitches` を全サイクル共通の固定値として扱うのではなく，各サイクルで有効な切替上限 `K_t` を計算する。

```text
effective_max_switches = K_t
```

そして，

```text
if applied >= effective_max_switches:
    break
```

により，サイクルごとの切替数を制御する。

この変更により，初期は積極的に切り替え，後半は切替回数を抑える制御が可能になる。

## 4. 候補端末の選択

候補端末は，低満足端末の直接救済だけでなく，混雑基地局からの offload も考慮する。

### 4.1 rescue candidate

満足度が閾値未満の端末を救済候補とする。

```text
S_i < theta_rescue
```

例:

```text
theta_rescue = 0.5
```

### 4.2 offload candidate

混雑基地局に接続している高満足端末を offload 候補とする。

条件例:

```text
S_i >= theta_offload
N_current_bs > average_users_per_bs
```

さらに，切替後の推定満足度が大きく低下しないことを確認する。

```text
estimated_S_i_after >= theta_offload
or
estimated_S_i_after >= S_i_before * retention_ratio
```

例:

```text
theta_offload = 1.0
retention_ratio = 0.9
```

### 4.3 候補優先順位

候補端末は以下の順で並べる。

1. rescue candidate を優先
2. rescue candidate 内では満足度が低い端末を優先
3. offload candidate 内では混雑 BS 上の端末を優先
4. 推定調和平均改善量が大きい端末を優先

## 5. DQN の状態

状態は，対象端末状態，基地局状態，全体 QoE，候補切替効果から構成する。

推奨する特徴量は以下である。

```python
STATE_FEATURES = [
    "cycle_id",
    "current_bs_id",
    "app_type",
    "tp_mbps",
    "rtt_ms",
    "satisfaction",
    "num_users_on_current_bs",
    "harmonic_mean",
    "num_unsatisfied_users",
    "candidate_type",
    "num_users_ap0",
    "num_users_ap1",
    "num_users_ap2",
    "monitor_rtt_ap0",
    "monitor_rtt_ap1",
    "monitor_rtt_ap2",
    "estimated_satisfaction_if_ap0",
    "estimated_satisfaction_if_ap1",
    "estimated_satisfaction_if_ap2",
    "estimated_h_delta_if_ap0",
    "estimated_h_delta_if_ap1",
    "estimated_h_delta_if_ap2",
    "best_estimated_h_delta",
    "effective_max_switches",
    "applied_switches_in_cycle",
    "remaining_switch_budget",
]
```

既存の `online_dqn` との互換性を重視する場合，まずは `effective_max_switches`，`applied_switches_in_cycle`，`remaining_switch_budget` を C++ 側ログには追加し，Python 側の特徴量として使うかどうかは別実験として分けてもよい。

## 6. DQN の行動

### 6.1 最小構成

最小構成では，行動空間は現状の `online_dqn` と同じにする。

```text
0: AP0 / 5G gNB
1: AP1 / Wi-Fi AP1
2: AP2 / Wi-Fi AP2
```

DQN は対象 UE の接続先を選択し，サイクル内の最大切替数は `K_t` により制御する。

この構成は実装しやすく，既存の `online_dqn` との比較が容易である。

### 6.2 拡張構成: STOP 行動

より DQN に切替数制御を学習させる場合，STOP 行動を追加する。

```text
0: AP0 / 5G gNB
1: AP1 / Wi-Fi AP1
2: AP2 / Wi-Fi AP2
3: STOP
```

STOP が選択された場合，そのサイクルの追加切替を終了する。

```text
if action == STOP:
    break
```

この方式では，`K_t` は絶対上限として機能し，実際の切替数は DQN の STOP 判断により `0 <= switch_count <= K_t` の範囲で決まる。

ただし，STOP 行動を導入すると学習が難しくなる可能性があるため，実験順序としては以下を推奨する。

1. 固定減衰スケジュール + AP 選択 DQN
2. STOP 行動付き DQN

## 7. Safety filter

Multi Online DQN でも，推定上明らかに悪い切替は適用しない。

基本条件:

```text
selected_bs_id が範囲外なら skip
selected_bs_id == current_bs_id なら skip
estimated_h_delta_if_selected <= safety_threshold なら skip
offload candidate で満足度維持条件を満たさないなら skip
```

例:

```text
safety_threshold = 0.0
```

これにより，DQN が未熟な行動を選択した場合でも，推定上の調和平均を悪化させる切替を抑制する。

## 8. 報酬設計

基本報酬は，次サイクルで観測された調和平均の変化とする。

```text
reward = H_after_measured - H_before
```

ただし，本手法は安定性を重視するため，切替回数と悪化端末数へのペナルティを加える。

```text
reward =
    H_after_measured - H_before
    - alpha * switch_count
    - beta  * num_degraded_users
```

ここで，

- `H_before`: 切替前の調和平均
- `H_after_measured`: 次サイクルで実測された調和平均
- `switch_count`: 当該サイクルで実際に適用された切替数
- `num_degraded_users`: 前サイクルと比較して満足度が低下した端末数
- `alpha`: 切替回数ペナルティ係数
- `beta`: 悪化端末数ペナルティ係数

である。

初期値の例:

```text
alpha = 0.001
beta  = 0.0002
```

係数は seed 複数回実験により調整する。

## 9. 処理フロー

```text
for each cycle t:

    1. 全 UE の TP / RTT を計測する

    2. 各 UE の満足度 S_i を計算する

    3. 調和平均 H_t と不満足端末数を計算する

    4. rescue candidate と offload candidate を作成する

    5. サイクル t の切替上限 K_t を計算する

    6. 候補 UE を優先度順に並べる

    7. applied = 0 とする

    8. for target UE in candidate_set:

           if applied >= K_t:
               break

           state を作成する

           action = DQN(state)

           if action == STOP:
               break

           safety filter を確認する

           if apply:
               assignment を更新する
               applied += 1

    9. 切替後の推定 H_after_estimated を記録する

   10. 次サイクルで H_after_measured と num_degraded_users を観測する

   11. reward を計算し，DQN を更新する
```

## 10. 実験比較

提案手法の有効性を確認するため，以下の手法を比較する。

| 手法 | 内容 |
|---|---|
| no switch | 初期接続を維持 |
| rulebase | 不満足端末をルールベースで切替 |
| online_dqn K=1 | 現状 Online DQN に近い設定 |
| online_dqn fixed K | `K=2,3,4,5` など固定切替上限 |
| proposed schedule K | `K=[5,4,3,2,1]` などの減衰スケジュール |
| proposed schedule K + STOP | 減衰上限に加えて DQN が停止判断 |

特に，固定 K と提案手法を比較することで，以下を確認する。

- 固定 K は初期改善が速いか。
- 固定 K は後半で過剰切替や QoE 振動を起こすか。
- 減衰スケジュールは H を維持しながら切替回数を抑えられるか。
- STOP 行動は不要な切替をさらに抑制できるか。

## 11. 評価指標

必ず以下を評価する。

```text
harmonic_mean
num_unsatisfied_users
switch_count
num_degraded_users
mean_satisfaction_delta
median_satisfaction_delta
max_satisfaction_drop
mean_tp
mean_rtt
H の cycle 間変動
seed 間の標準偏差
```

特に本手法では，平均 TP だけでなく，以下を重視する。

- 調和平均の改善
- 不満足端末数の減少
- 切替対象外端末の満足度悪化数
- cycle 後半の切替数減少
- 満足度変動幅の抑制

## 12. ログ設計

既存ログに加えて，以下の列を追加することを推奨する。

```text
effective_max_switches
applied_switches_in_cycle
remaining_switch_budget
k_schedule_type
k_max
k_min
k_decay_rate
stop_action_flag
```

`decision_log` には，各 DQN action ごとに以下を記録する。

```text
cycle_id
step_id
target_ue_id
candidate_type
previous_bs_id
selected_bs_id
applied
skip_reason
effective_max_switches
applied_switches_in_cycle
remaining_switch_budget
selected_estimated_h_delta
q_bs0
q_bs1
q_bs2
q_stop
```

STOP 行動を使わない実験では，`stop_action_flag = 0`，`q_stop` は空欄または `NaN` とする。

## 13. 実装方針

### 13.1 C++ 側

`APselection::online_dqn_assignment()` をベースに，サイクルごとの有効切替上限を計算する。

```cpp
uint32_t effectiveMaxSwitches = GetEffectiveMaxSwitches(m_cycleIndex);
```

例:

```cpp
uint32_t
APselection::GetEffectiveMaxSwitches(uint32_t cycleIndex) const
{
    const uint32_t decayed =
        (cycleIndex > 0 && m_KMax > m_KDecayRate * (cycleIndex - 1))
            ? m_KMax - m_KDecayRate * (cycleIndex - 1)
            : m_KMin;
    return std::max(m_KMin, decayed);
}
```

そして，既存の

```cpp
if (applied >= m_MaxSwitches)
{
    break;
}
```

を以下に置き換える。

```cpp
if (applied >= effectiveMaxSwitches)
{
    break;
}
```

### 13.2 Python 側

最小構成では，`rl/protocol.py` の `STATE_FEATURES` に切替予算関連特徴量を追加する。

```python
"effective_max_switches",
"applied_switches_in_cycle",
"remaining_switch_budget",
```

STOP 行動を導入する場合は，`rl/server.py` の `--action-dim` を `4` にする。

```bash
python3 rl/server.py --action-dim 4
```

C++ 側では，`selected_bs_id == 3` を STOP として扱う。

## 14. 想定される研究上の主張

論文・発表では，以下のように説明できる。

```text
複数端末を同一サイクルで切り替えることにより，低満足端末を短時間で救済できる可能性がある。
一方で，基地局負荷が急激に変化するため，切替対象外端末の QoE 悪化や接続状態の振動を招く可能性がある。
そこで本研究では，サイクル初期では複数端末の切替を許容し，サイクルが進むにつれて切替可能台数を段階的に減少させる DQN ベースの基地局選択手法を提案する。
これにより，初期段階の QoE 改善速度と後半段階の接続安定性の両立を図る。
```

## 15. 優先実装項目

QoE 最大化を目的とした Multi Online DQN として成立させるため，現状の `method=online_dqn` に対して，以下の順で機能を追加する。

重要な前提として，`K_t` や `K_hard_max` は **DQN の自由度を安全に制限するための実験・安全パラメータ** であり，最終的には DQN が状態に応じて STOP 行動を選ぶことで，実際の切替端末数を自律的に決定することを目指す。

### 15.1 `K_t` 減衰スケジュール

最初の実装段階では，サイクルごとの最大切替数 `K_t` を導入する。

```text
K_t = max(K_min, K_max - decay_rate * (t - 1))
```

例:

```text
K_max = 5
K_min = 1
decay_rate = 1
```

このとき，

```text
cycle 1: K_t = 5
cycle 2: K_t = 4
cycle 3: K_t = 3
cycle 4: K_t = 2
cycle 5以降: K_t = 1
```

となる。

ただし，`K_max = 5` は理論的に固定された値ではなく，初期検証用の候補値である。研究上は，以下のように複数の上限値で感度分析する。

```text
K_max ∈ {3, 5, 8, 10}
```

この段階での目的は，以下を確認することである。

- 固定 `K=1` より初期 QoE 改善が速くなるか。
- 固定 `K=3,5` と比較して，後半サイクルの切替回数を抑えられるか。
- `K_t` 減衰により H の振動や悪化端末数が抑制されるか。

C++ 側では，既存の `m_MaxSwitches` を直接使うのではなく，cycle ごとの有効上限を計算する。

```cpp
uint32_t effectiveMaxSwitches = GetEffectiveMaxSwitches(m_cycleIndex);
```

既存の条件，

```cpp
if (applied >= m_MaxSwitches)
{
    break;
}
```

を以下に置き換える。

```cpp
if (applied >= effectiveMaxSwitches)
{
    break;
}
```

### 15.2 budget 特徴量

複数台切替では，DQN が「今 cycle 内で何台目の判断をしているのか」を知らないと，適切に切替継続・抑制を判断できない。

そのため，状態特徴量に以下を追加する。

```text
effective_max_switches
applied_switches_in_cycle
remaining_switch_budget
```

意味は以下である。

| 特徴量 | 意味 |
|---|---|
| `effective_max_switches` | 現 cycle の有効切替上限 `K_t` |
| `applied_switches_in_cycle` | 現 cycle で既に適用された切替数 |
| `remaining_switch_budget` | 現 cycle で追加可能な残り切替数 |

これらは，人間が「この cycle では何台切り替える」と直接決めるためではなく，DQN が切替の進行状況を把握するために使う。

Python 側では `rl/protocol.py` の `STATE_FEATURES` に追加する。

```python
STATE_FEATURES = [
    ...
    "effective_max_switches",
    "applied_switches_in_cycle",
    "remaining_switch_budget",
]
```

ログにも同じ列を追加し，後処理で以下を確認できるようにする。

```text
cycle ごとの K_t
実際に適用された切替数
残り budget がある状態で終了したか
```

### 15.3 `estimated_marginal_delta`

1 cycle 内で複数端末を切り替えると，次 cycle で観測される実測 reward は，その cycle で行った切替集合全体の結果になる。

したがって，

```text
measured_reward = H_after_measured - H_before
```

だけでは，どの UE の切替がどれだけ調和平均に寄与したかを直接分離できない。

そこで，cycle 内で切替を逐次適用するたびに，各 step の推定上の限界寄与を記録する。

```text
estimated_marginal_delta_k
    = H_est(assignment_after_step_k)
      - H_est(assignment_before_step_k)
```

ここで重要なのは，`estimated_marginal_delta` は **対象 UE 自身の満足度上昇量ではなく，全体調和平均 H の推定増分** である点である。

対象 UE 自身の満足度変化は別列として記録する。

```text
target_satisfaction_delta_estimated
    = estimated_satisfaction_after
      - satisfaction_before
```

推奨ログ列:

```text
h_before_step_estimated
h_after_step_estimated
estimated_marginal_delta
target_satisfaction_before
target_satisfaction_after_estimated
target_satisfaction_delta_estimated
```

学習・解析上は以下のように役割を分ける。

| 指標 | 単位 | 用途 |
|---|---|---|
| `estimated_marginal_delta` | action / step 単位 | 各 UE 切替の推定寄与 |
| `target_satisfaction_delta_estimated` | action / step 単位 | 対象 UE 自身の救済効果 |
| `measured_reward` | cycle 単位 | 切替集合全体の実測効果 |
| `num_degraded_users` | cycle 単位 | 安定性評価 |

### 15.4 STOP 行動

最終的には，サイクルごとの実際の切替端末数を DQN が状態に応じて決定できるようにする。

そのため，行動空間に STOP を追加する。

```text
0: AP0 / 5G gNB
1: AP1 / Wi-Fi AP1
2: AP2 / Wi-Fi AP2
3: STOP
```

STOP が選択された場合，その cycle の追加切替を終了する。

```cpp
if (selectedBsId == 3)
{
    stopActionFlag = true;
    break;
}
```

この設計では，`K_t` または `K_hard_max` は安全上の最大値として扱い，実際の切替数は以下で決まる。

```text
実際の切替数 = DQN が STOP を出すまでに適用された切替数
ただし上限は K_t または K_hard_max
```

これにより，DQN は以下を学習できる。

```text
この状態ではまだ切替を続けるべき
この状態ではこれ以上切り替えると悪化しやすいので STOP するべき
```

STOP 行動を導入する場合，Python server は以下のように起動する。

```bash
python3 rl/server.py --action-dim 4
```

ログには以下を追加する。

```text
stop_action_flag
q_stop
```

### 15.5 measured reward + degraded penalty

QoE 最大化の最終評価では，推定値ではなく次 cycle で観測された実測値を重視する。

現在のログでは，

```text
measured_reward
switch_count
num_degraded_users
```

が出力されているが，現状の Python DQN 学習では，これらの値が十分に reward として利用されていない。

提案手法では，cycle 単位の reward を以下のように定義する。

```text
cycle_reward =
    H_after_measured - H_before
    - alpha * switch_count
    - beta  * num_degraded_users
```

ここで，

- `H_after_measured - H_before`: 実測された調和平均の改善量
- `switch_count`: 当該 cycle の実切替数
- `num_degraded_users`: 前 cycle と比べて満足度が低下した UE 数
- `alpha`: 切替回数ペナルティ
- `beta`: 悪化端末数ペナルティ

である。

初期値の例:

```text
alpha = 0.001
beta  = 0.0002
```

ただし，これらの係数は固定せず，複数 seed と複数 `K_hard_max` で感度分析する。

実装上は，C++ 側で確定した前 cycle の実測情報を，次回の DQN request に含める。

```json
{
  "prev_cycle_measured_reward": 0.0123,
  "prev_cycle_switch_count": 3,
  "prev_cycle_num_degraded_users": 12
}
```

Python 側では，この情報を用いて replay buffer に入れる reward を計算する。

```python
reward = (
    prev_cycle_measured_reward
    - alpha * prev_cycle_switch_count
    - beta  * prev_cycle_num_degraded_users
)
```

複数 action への信用割当については，以下の方針とする。

- AP 選択 action の細かい寄与は `estimated_marginal_delta` で記録・解析する。
- cycle 全体の良否は `cycle_reward` で評価する。
- STOP 行動または切替数制御の学習には，cycle 単位の `cycle_reward` を重視する。
- 1 cycle 内の全 action に同じ実測 reward を単純配分する場合は，信用割当が粗い baseline として扱う。

## 16. 実験順序

まずは以下の順で実験する。

1. `online_dqn K=1`
2. `online_dqn fixed K=2`
3. `online_dqn fixed K=3`
4. `online_dqn fixed K=4`
5. `online_dqn fixed K=5`
6. `proposed K=[5,4,3,2,1]`
7. `proposed K=[4,3,2,1,1]`
8. `proposed K=[5,3,2,1,1]`
9. STOP 行動付き proposed

各設定は 1 seed だけで判断せず，複数 seed で比較する。

上記に加えて，STOP 行動付きの最終提案では以下を比較する。

```text
STOP なし: K_t 減衰スケジュールのみ
STOP あり: DQN が切替終了を判断
```

この比較により，単なる人手スケジュールの効果と，DQN による自律的な切替数制御の効果を分離する。

## 17. 学習フロー

Multi Online DQN は，最初から ns-3 実行中のオンライン学習だけで方策を獲得するのではなく，以下の段階で進める。

```text
Phase 1: 経験データ収集
Phase 2: 事前学習
Phase 3: STOP 行動付き拡張
Phase 4: オンライン学習
Phase 5: eval-only 評価
```

この流れにより，初期方策が完全にランダムな状態で ns-3 上の QoE を大きく壊すことを避け，既存 baseline の経験を使って DQN を初期化した上で，オンライン実測 reward により fine-tuning する。

### 17.1 Phase 1: 経験データ収集

まず，既存手法を複数 seed で実行し，DQN の事前学習に使う経験データを収集する。

対象手法:

```text
multi_greedy
multi_offload
rulebase
```

保存するログ:

```text
master_log_*.csv
decision_log_*.csv
measured_reward_log_*.csv
```

ただし，`rulebase` は現状 `decision_log` や `measured_reward_log` が十分に出ない場合があるため，まずは `master_log` を AP 選択の Behavior Cloning 用データとして使う。`multi_greedy` / `multi_offload` / `online_dqn` 系では，可能であれば `decision_log` も利用する。

#### 17.1.1 必要な経験データ量の目安

経験データ量は，「CSV 行数」ではなく，DQN が実際に学習する **action 単位のサンプル数** で考える。

1 run あたりの概算サンプル数は以下で見積もる。

```text
action_samples_per_run
    ≒ num_cycles × average_decisions_per_cycle
```

例:

```text
num_cycles = 5
average_decisions_per_cycle = 5
```

なら，

```text
1 run ≒ 25 action samples
```

である。

一方，`master_log` は 80 UE × 5 cycle で 400 行程度出るが，全行が有効な action sample ではない。`switch_flag=1` の行，または candidate として扱える行を中心に使う必要がある。

#### 17.1.2 最小データ量

最小構成の AP 選択 DQN，すなわち STOP 行動なしで，

```text
action = AP0 / AP1 / AP2
```

のみを Behavior Cloning する場合，最低限の目安は以下である。

```text
有効 action samples: 1,000 以上
seed 数: 30〜50 程度
```

これは，1 run あたり 20〜30 action samples 程度しか得られない場合を想定している。

```text
30 runs × 25 samples/run = 750 samples
50 runs × 25 samples/run = 1,250 samples
```

したがって，まずは **50 seed 前後** を目標にする。

#### 17.1.3 推奨データ量

安定した事前学習を行うには，以下を目標にする。

```text
有効 action samples: 5,000〜10,000
seed 数: 100〜300 程度
```

特に，以下の条件を変えて収集する。

```text
method ∈ {multi_greedy, multi_offload, rulebase}
K ∈ {1, 2, 3, 5, 8}
seed ∈ 複数
```

例:

```text
3 methods × 5 K values × 20 seeds = 300 runs
```

1 run あたり 25 action samples とすると，

```text
300 runs × 25 samples/run = 7,500 samples
```

程度が得られる。

#### 17.1.4 STOP 行動用データ量

STOP 行動を学習する場合は，AP 選択よりも多くのデータが必要になる。

理由は，STOP は各候補 UE に対する AP 選択ではなく，

```text
この cycle でこれ以上切り替えるべきか
```

という切替数制御の判断であり，`measured_reward`，`num_degraded_users`，`switch_count` との関係を学習する必要があるためである。

STOP 行動付き DQN の目安:

```text
有効 action samples: 10,000〜30,000
cycle-level samples: 1,000 以上
seed 数: 200〜500 程度
```

ここで，

```text
cycle-level samples = runs × num_cycles
```

である。

例:

```text
200 runs × 5 cycles = 1,000 cycle-level samples
```

となる。

STOP 行動は，初期段階では完全な教師ラベルを作るのが難しいため，以下のような疑似ラベルまたは RL reward を使う。

```text
次の候補切替の estimated_marginal_delta が小さい，または負
直近 cycle の measured_reward が悪化
num_degraded_users が多い
remaining_switch_budget はあるが，H 改善余地が小さい
```

このような条件を満たす場合に STOP を選ぶ疑似ラベルを作り，初期方策として事前学習する。

#### 17.1.5 データ分布の注意

経験データ収集では，以下の偏りに注意する。

- `switch_flag=0` が多すぎると，DQN が no-op に偏る。
- rescue candidate だけだと，offload 行動を学習できない。
- 特定 K のデータだけだと，複数台切替時の状態分布を学習できない。
- 1 seed だけでは配置やアプリ分布に依存した方策になる。
- 固定 `K=1` だけでは STOP 行動や切替数制御を学習できない。

そのため，事前学習用データでは以下のバランスを確認する。

```text
AP0 / AP1 / AP2 の action 数
switch_flag=1 / 0 の比率
candidate_type=0 / 1 の比率
K ごとのサンプル数
seed ごとの H 分布
```

### 17.2 Phase 2: 事前学習

収集したログから，DQN に AP 選択を Behavior Cloning で学習させる。

最初は STOP 行動なしとし，教師ラベルは以下を使う。

```text
教師ラベル = selected_bs_id
```

または `master_log` を使う場合，

```text
教師ラベル = action_selected_bs_id
```

を用いる。

既存コードでは，以下のスクリプトが該当する。

```text
rl/pretrain_from_logs.py
```

この段階の目的は，DQN を完全ランダム初期方策から始めないことである。

### 17.3 Phase 3: STOP 行動付き拡張

AP 選択の事前学習後，STOP 行動を追加する。

```text
0: AP0
1: AP1
2: AP2
3: STOP
```

STOP の教師信号は直接観測できないため，初期段階では以下を用いる。

```text
estimated_marginal_delta
cycle_reward
num_degraded_users
switch_count
```

例えば，以下の条件では STOP の疑似ラベルを付ける。

```text
estimated_marginal_delta <= 0
または
直近 cycle_reward が負
または
num_degraded_users が閾値以上
```

ただし，STOP 疑似ラベルは設計者の仮定を含むため，最終評価ではオンライン学習後の eval-only 結果で検証する。

### 17.4 Phase 4: オンライン学習

事前学習済み checkpoint を読み込み，ns-3 実行中にオンラインで fine-tuning する。

オンライン学習では，最終的に以下の reward を使う。

```text
reward =
    H_after_measured - H_before
    - alpha * switch_count
    - beta  * num_degraded_users
```

現状コードでは `H_after_measured - H_before` に近い reward は使われているが，`switch_count` と `num_degraded_users` のペナルティは DQN 学習にはまだ入っていない。したがって，この phase では Python server に前 cycle の実測情報を渡す実装が必要である。

### 17.5 Phase 5: eval-only 評価

最後に，学習を止めた固定 policy で複数 seed 評価を行う。

```bash
python3 comand/main_comand.py \
  --preset custom \
  --method online_dqn \
  --checkpoint models/proposed.pt \
  --eval-only \
  --seeds 1 2 3 ...
```

eval-only では，学習中の探索や重み更新を止め，提案手法そのものの性能を評価する。

評価では以下を必ず確認する。

```text
H の平均・標準偏差
不満足端末数
切替回数
悪化端末数
最大満足度低下幅
seed 間ばらつき
```

## 18. 注意点

- `K_t` 減衰スケジュールによる改善を DQN の効果と混同しない。
- 固定 K の baseline を必ず用意する。
- H が改善しても，悪化端末数や最大満足度低下幅が大きい場合は安定とは言えない。
- 不満足端末数だけで評価しない。
- 平均 TP だけで QoE 改善を判断しない。
- 実験結果は推測で補完せず，未実行の場合は未実行と明記する。
