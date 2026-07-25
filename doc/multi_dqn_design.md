# Multi-DQN 設計仕様書

作成日: 2026-07-25  
対象 method: `multi_dqn` または拡張版 `dqn`  
対象プロジェクト: ns-3.44 QoE-aware AP/base station selection

---

## 1. 目的

現在の DQN 実装は、1 サイクルにつき **1 台の対象 UE** を選び、その UE の接続先基地局 `selected_bs_id` を DQN が決める最小構成である。

しかし、本研究の最終目的は、5G と Wi-Fi が混在する異種無線ネットワークで、各サイクルにおいて複数端末を切り替え候補とし、端末満足度の調和平均 `H` を早期に高め、その後のサイクルでも高い `H` を維持することである。

そのため、今後は現在の 1 UE DQN を拡張し、**各サイクルで複数 UE を候補にし、最大 `K` 台まで切り替える Multi-DQN 方式**へ移行する。

本仕様書では、現状プロジェクトから変更した方がよい部分を、ファイル単位・処理単位で整理する。

---

## 2. 現状実装の整理

### 2.1 現在の DQN パイプライン

現在の DQN は Python 側で学習・推論し、ns-3 側は推論済み action CSV を読む構成である。

```text
OUTPUT/<num_ues>/<method>/master_log_*.csv
  ↓
scripts/dqn/dataset/build_transitions.py
  ↓
episodes/dqn/transitions/*.csv
  ↓
scripts/dqn/train/train_dqn.py
  ↓
models/dqn/checkpoints/*.pt
  ↓
scripts/dqn/infer/infer_actions.py
  ↓
episodes/dqn/actions/*.csv
  ↓
./ns3 run "master --method=dqn --dqnActionCsv=<action_csv>"
```

### 2.2 現在の DQN モデル

実装ファイル:

- `scripts/dqn/train/train_dqn.py`
- `scripts/dqn/infer/infer_actions.py`

現在の DQN は、候補 UE 1 台に対して基地局選択を行う Q network である。

```text
入力: 9 特徴量
出力: 3 action Q 値
```

現在の特徴量は以下である。

```text
cycle_id
current_bs_id
app_type
tp_mbps
rtt_ms
satisfaction
num_users_on_current_bs
harmonic_mean
num_unsatisfied_users
```

出力 action は以下である。

```text
0: 5G gNB / AP0
1: Wi-Fi AP1
2: Wi-Fi AP2
```

### 2.3 現在の action CSV 形式

現在の action CSV は、実質的に **1 cycle 1 action** を前提にしている。

```csv
seed,cycle_id,target_ue_id,selected_bs_id,q_bs0,q_bs1,q_bs2
1,1,12,0,...,...,...
1,2,25,2,...,...,...
```

ns-3 側では `cycle_id` を key として 1 件の action を保持している。

関連ファイル:

- `contrib/kameda/model/server/APselection.h`
- `contrib/kameda/model/server/APselection.cc`

現在の保持構造は以下の形である。

```cpp
std::map<uint32_t, std::pair<int, int>> m_dqnActions;
```

意味:

```text
cycle_id -> (target_ue_id, selected_bs_id)
```

このため、同じ cycle に複数行 action があると、後勝ちで上書きされる。

### 2.4 現在の経験データとのズレ

`OUTPUT/` 配下の経験データには、`random`, `rulebase`, `multi_greedy` など、1 サイクルで複数 UE が切り替わるログが含まれている。

一方、現在の `build_transitions.py --target-mode flag_only` は、各 cycle で `target_ue_flag == 1` の 1 UE のみを transition 化する。

この場合、以下のズレが生じる。

```text
reward: 複数 UE 切り替え後の全体 H 変化
action: target_ue_flag == 1 の 1 UE の selected_bs_id
```

つまり、複数 UE の行動結果として発生した reward を、1 UE の action に割り当てている。これは最小動作確認には使えるが、本命の性能評価には不十分である。

---

## 3. Multi-DQN の基本設計

### 3.1 方針

最初から全 UE の割当ベクトルを DQN が一括出力する方式は避ける。

代わりに、現在の DQN モデルを活かし、**候補 UE ごとに DQN を適用し、1 サイクル内で複数 action を採用する方式**にする。

これを本仕様では `candidate-wise Multi-DQN` と呼ぶ。

### 3.2 1 サイクルの処理

```text
cycle t
  1. master_log から候補 UE を複数抽出する
  2. 各候補 UE の 9 特徴量を DQN に入力する
  3. 各候補 UE について Q(s_i, bs_id) を計算する
  4. selected_bs_id = argmax_a Q(s_i, a) を得る
  5. 現在 BS より有利な候補だけを残す
  6. advantage または Q 値で順位付けする
  7. 最大 MaxSwitches 台まで action CSV に出力する
  8. ns-3 側で同一 cycle の複数 action を適用する
```

### 3.3 action 採用基準

推奨する採用指標は advantage である。

```text
advantage_i = max_a Q(s_i, a) - Q(s_i, current_bs_id)
```

理由:

- 現在接続先を維持する価値との差分を見られる。
- `selected_bs_id == current_bs_id` の候補を自然に除外できる。
- 複数候補を並べる際に「切り替える価値が高い UE」を選びやすい。

採用条件の初期案:

```text
selected_bs_id != current_bs_id
advantage > 0
上位 MaxSwitches 件まで
```

---

## 4. 変更対象ファイル一覧

優先的に変更するファイルは以下である。

| 種別 | ファイル | 変更内容 |
|---|---|---|
| action CSV 仕様 | `data/dqn/configs/action_format.yaml` | 1 cycle 複数行、`step_id`, `advantage` を追加 |
| 推論 | `scripts/dqn/infer/infer_actions.py` | 複数候補抽出、上位 K 件出力 |
| transition 作成 | `scripts/dqn/dataset/build_transitions.py` | `switched_only`, `unsatisfied`, `multi_candidate` モード追加 |
| 学習 | `scripts/dqn/train/train_dqn.py` | 当面は大変更不要。ただし metadata に multi 設定を保存 |
| ns-3 action 読み込み | `contrib/kameda/model/server/APselection.h` | `m_dqnActions` を複数 action 対応に変更 |
| ns-3 action 適用 | `contrib/kameda/model/server/APselection.cc` | 同一 cycle の複数 action を順に適用 |
| 実行手順 | `scripts/README.md` | Multi-DQN 手順を追記 |
| 設定 | `data/dqn/configs/dqn.yaml` | `max_switches`, `candidate_mode` などを追加 |
| 評価設定 | `data/dqn/configs/evaluation.yaml` | `multi_dqn` を比較対象に追加 |

---

## 5. action CSV 仕様の変更

### 5.1 現状

現在の action CSV は以下である。

```csv
seed,cycle_id,target_ue_id,selected_bs_id,q_bs0,q_bs1,q_bs2
```

### 5.2 変更後

Multi-DQN では、同じ `cycle_id` に複数行を許可する。

```csv
seed,cycle_id,step_id,target_ue_id,current_bs_id,selected_bs_id,advantage,q_bs0,q_bs1,q_bs2
1,1,0,12,2,0,0.123456,...,...,...
1,1,1,25,1,2,0.080000,...,...,...
1,1,2,41,0,1,0.050000,...,...,...
1,2,0,7,2,1,0.110000,...,...,...
```

### 5.3 追加列

| 列名 | 意味 |
|---|---|
| `step_id` | 同一 cycle 内の action 順序。0 始まり |
| `current_bs_id` | 推論時点の現在接続先。0-based |
| `advantage` | `max_q - q_current_bs` |
| `q_bs0` | 5G/AP0 の Q 値 |
| `q_bs1` | Wi-Fi AP1 の Q 値 |
| `q_bs2` | Wi-Fi AP2 の Q 値 |

### 5.4 互換性

`step_id` がない旧 action CSV も読めるようにするかは選択肢がある。

推奨は以下である。

- `--method=dqn`: 旧形式、1 cycle 1 action
- `--method=multi_dqn`: 新形式、1 cycle 複数 action

このように method を分けると baseline と本命方式を比較しやすい。

---

## 6. ns-3 側の修正

### 6.1 `APselection.h` の変更

現在:

```cpp
std::map<uint32_t, std::pair<int, int>> m_dqnActions;
```

変更後案:

```cpp
struct DqnAction
{
    uint32_t stepId = 0;
    int targetUeId = -1;    // 1-based
    int currentBsId = -1;   // 0-based, optional validation
    int selectedBsId = -1;  // 0-based
    double advantage = 0.0;
};

std::map<uint32_t, std::vector<DqnAction>> m_dqnActions;
```

### 6.2 `LoadDqnActions()` の変更

現在は同じ `cycle_id` の action が来ると上書きしている。

```cpp
m_dqnActions[cycleId] = std::make_pair(targetUeId, selectedBsId);
```

Multi-DQN では以下に変更する。

```cpp
m_dqnActions[cycleId].push_back(action);
```

読み込み時の検証:

```text
target_ue_id が 1 <= target_ue_id <= terms
selected_bs_id が 0 <= selected_bs_id < aps
step_id が同一 cycle 内で重複していない
同一 cycle 内で同一 target_ue_id が重複していない
seed が m_rngSeed と一致する
```

読み込み後、各 cycle の action を `step_id` 昇順に sort する。

### 6.3 `dqn_assignment()` の変更

現在は 1 UE のみを変更している。

変更後は、同一 cycle の action を順番に適用する。

疑似コード:

```cpp
void APselection::multi_dqn_assignment()
{
    std::vector<int> assignment = initial_AP;
    auto it = m_dqnActions.find(m_cycleIndex);

    if (it == m_dqnActions.end())
    {
        KeepCurrentAssignment("no multi_dqn action for cycle");
        return;
    }

    uint32_t applied = 0;
    std::set<int> seenTargets;

    for (const DqnAction& action : it->second)
    {
        if (applied >= m_MaxSwitches)
        {
            break;
        }

        const int targetIdx = action.targetUeId - 1;
        if (targetIdx < 0 || targetIdx >= terms)
        {
            continue;
        }
        if (seenTargets.count(targetIdx) > 0)
        {
            continue;
        }

        const int nextAp = action.selectedBsId + 1; // internal AP ID is 1-based
        if (nextAp < 1 || nextAp > aps)
        {
            continue;
        }

        if (assignment[targetIdx] == nextAp)
        {
            continue;
        }

        assignment[targetIdx] = nextAp;
        seenTargets.insert(targetIdx);
        applied++;
    }

    const double hBefore =
        m_cycleHarmonicMeans.empty() ? calculate_harmonic_mean_for_assignment(initial_AP)
                                     : m_cycleHarmonicMeans.back();

    PrepareDecisionLogState(initial_AP, assignment, hBefore, hBefore);
    m_lastAssignment = assignment;

    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}
```

### 6.4 method 分岐

`APselection::tmain()` の method 分岐に `multi_dqn` を追加する。

```cpp
else if (m_assignmentMethod == "multi_dqn")
{
    multi_dqn_assignment();
}
```

既存 `dqn_assignment()` は 1 UE DQN baseline として残す。

---

## 7. `infer_actions.py` の修正

### 7.1 現状

現在は `target_ue_flag == 1` の行のみを推論対象にする。

```python
selected = df[pd.to_numeric(df["target_ue_flag"]) == 1]
```

### 7.2 追加する引数

```bash
--candidate-mode flag_only|unsatisfied|top_k_low|all_users|switched_only
--satisfaction-threshold 0.7
--max-switches 5
--min-advantage 0.0
--exclude-current-bs-action
```

推奨デフォルト:

```text
candidate-mode: unsatisfied
satisfaction-threshold: 0.7
max-switches: 5
min-advantage: 0.0
exclude-current-bs-action: true
```

### 7.3 candidate-mode

| mode | 内容 | 用途 |
|---|---|---|
| `flag_only` | `target_ue_flag == 1` のみ | 旧 DQN 互換 |
| `unsatisfied` | `satisfaction < threshold` | 本命の初期候補生成 |
| `top_k_low` | 満足度が低い順に候補抽出 | 候補数制御したい場合 |
| `all_users` | 全 UE | 網羅的推論、デバッグ |
| `switched_only` | `switch_flag == 1` | 経験データ分析用。推論用 master_log では原則使わない |

### 7.4 推論処理

cycle ごとに候補 UE を処理する。

疑似コード:

```python
for cycle_id, cycle_df in df.groupby("cycle_id"):
    candidates = select_candidates(cycle_df, mode=args.candidate_mode)
    states = numeric_matrix(candidates, feature_columns)
    q_values = model(states)

    for each candidate:
        current_bs = int(row["current_bs_id"])
        selected_bs = argmax(q_values)
        advantage = max(q_values) - q_values[current_bs]

    keep candidates where:
        selected_bs != current_bs
        advantage > min_advantage

    sort by advantage descending
    take max_switches
    assign step_id = 0, 1, 2, ...
    write action rows
```

### 7.5 注意点

現在の DQN は候補 UE ごとに独立に Q 値を出す。つまり、同一 cycle 内で前の action を適用した後の状態更新は推論時には行わない。

これは最小 Multi-DQN としては許容するが、厳密な sequential DQN ではない。将来的には、1 action 適用ごとに AP 接続数や推定満足度を更新して、次候補の state を再計算する方式に拡張できる。

---

## 8. `build_transitions.py` の修正

### 8.1 現状

現在の target mode は以下である。

```text
flag_only
all_users
```

`flag_only` は 1 cycle 1 transition になりやすい。

### 8.2 追加すべき target mode

Multi-DQN 用に以下を追加する。

```text
switched_only
unsatisfied
top_k_low
```

### 8.3 `switched_only`

```text
switch_flag == 1 の UE を transition にする
```

目的:

- 経験データで実際に切り替えられた UE を学習対象にする。
- 複数 UE 切り替えログを活かす。

注意:

- reward は複数 UE の合成効果であるため、1 UE ごとの厳密な貢献度ではない。
- ただし、Multi-DQN 初期実装では「切り替えられた UE 群が全体 H 改善に関与した」とみなす近似として使う。

### 8.4 `unsatisfied`

```text
satisfaction < threshold の UE を transition にする
```

目的:

- 推論時の candidate-mode `unsatisfied` と学習分布を近づける。
- 切り替えられていない不満足 UE も学習候補に含める。

注意:

- `action_selected_bs_id` が現在 BS と同じ場合、実質 no-op action になる。
- no-op を action として学習に含めるかは方針を決める必要がある。

### 8.5 reward の扱い

現状の reward は以下である。

```text
reward = next_harmonic_mean - harmonic_mean
```

Multi-DQN 初期版でもこのままでよい。ただし、複数 action に同じ cycle reward を割り当てることになる。

仕様として以下を明記する。

```text
初期 Multi-DQN では、同一 cycle 内の候補 transition に同じ global reward を付与する。
これは簡易な credit assignment であり、厳密な UE 単位貢献度ではない。
```

将来的には以下を検討する。

```text
- step ごとの推定 H 差分を reward にする
- multi_greedy の探索時ログから step_reward を出力する
- counterfactual reward を計算する
- switch penalty を追加する
```

---

## 9. `train_dqn.py` の修正

### 9.1 初期 Multi-DQN ではモデル構造は維持

DQN 本体は当面そのままでよい。

```text
入力: UE 単位の 9 特徴量
出力: 3 基地局 Q 値
```

変更しない理由:

- 既存の学習・推論コードを活かせる。
- 複数 UE 化は推論側と ns-3 action 適用側で実現できる。
- いきなり高次元 action 空間にしないことでデバッグしやすい。

### 9.2 追加した方がよい metadata

checkpoint 保存時に以下を追加するとよい。

```json
{
  "dqn_design": "candidate_wise_multi_dqn",
  "candidate_mode": "switched_only",
  "max_switches": 5,
  "feature_dim": 9,
  "uses_cycle_id": true,
  "reward_type": "delta_harmonic_mean_global"
}
```

### 9.3 学習データのフィルタ

`measurement_valid == 0` の行を学習に含めるかどうかを設定化した方がよい。

候補:

```yaml
filter:
  require_measurement_valid: false
  require_next_measurement_valid: false
```

初期は現状維持でもよいが、品質評価時には valid/invalid の影響を確認する。

---

## 10. 設定ファイルの修正

### 10.1 `data/dqn/configs/dqn.yaml`

追加案:

```yaml
implementation_plan: candidate_wise_multi_dqn

multi_dqn:
  enabled: true
  max_switches: 5
  candidate_mode: unsatisfied
  satisfaction_threshold: 0.7
  min_advantage: 0.0
  exclude_current_bs_action: true
  ranking_metric: advantage
```

### 10.2 `data/dqn/configs/action_format.yaml`

変更案:

```yaml
action_file:
  columns:
    - seed
    - cycle_id
    - step_id
    - target_ue_id
    - current_bs_id
    - selected_bs_id
    - advantage
    - q_bs0
    - q_bs1
    - q_bs2

  multi_action_per_cycle: true
  max_switches_column: step_id
```

### 10.3 `data/dqn/configs/transition.yaml`

`target_mode` の説明を追加する。

```yaml
transition:
  supported_target_modes:
    - flag_only
    - all_users
    - switched_only
    - unsatisfied
    - top_k_low
```

### 10.4 `data/dqn/configs/evaluation.yaml`

評価対象に `multi_dqn` を追加する。

```yaml
methods:
  - no_switch
  - random
  - rulebase
  - multi_greedy
  - multi_offload
  - dqn
  - multi_dqn
```

---

## 11. master_log の追加検討

現在の master_log には以下がある。

```text
target_ue_flag
action_selected_bs_id
switch_flag
h_after_estimated
reward
```

Multi-DQN を評価するには、以下を追加するか、別ログを作るとよい。

### 11.1 追加候補

```text
action_step_id
action_advantage
num_actions_in_cycle
max_switches
candidate_mode
```

ただし master_log は全 UE 行に出力されるため、列を増やすと既存解析への影響がある。

推奨は、master_log は大きく変えず、別ファイルを追加する方式である。

### 11.2 推奨: decision_log.csv の追加

新規ログ:

```text
OUTPUT/<num_ues>/<method>/decision_log_<seed>_<timestamp>.csv
```

列:

```text
seed
method
cycle_id
step_id
target_ue_id
previous_bs_id
selected_bs_id
satisfaction_before
harmonic_mean_before
harmonic_mean_after_estimated
advantage
q_bs0
q_bs1
q_bs2
applied
skip_reason
```

このログがあると、Multi-DQN がどの UE をなぜ選んだかを後で検証できる。

---

## 12. 評価指標

Multi-DQN の評価では、既存指標に加えて以下を見る。

### 12.1 必須指標

```text
harmonic_mean
num_unsatisfied_users
average_tp_mbps
average_rtt_ms
switch_count
num_degraded_users
reward
seed 間の平均・標準偏差
```

### 12.2 Multi-DQN 特有の指標

```text
num_candidate_users
num_selected_actions
num_applied_switches
num_skipped_actions
mean_advantage
max_advantage
selected_ue_satisfaction_before
selected_ue_satisfaction_after
non_selected_ue_degraded_count
```

### 12.3 研究目的に対する評価

特に以下を確認する。

```text
cycle 1, 2 で baseline より高い H を作れているか
cycle 3 以降で H を維持できているか
切り替え回数が過剰でないか
不満足端末数だけでなく H が改善しているか
切り替え対象外端末の満足度が悪化していないか
```

---

## 13. 実装順序

### Phase 1: action CSV と ns-3 側の複数 action 対応

目的:

```text
同一 cycle に複数 action を渡すと、ns-3 側で複数 UE を切り替えられることを確認する。
```

変更:

```text
APselection.h
APselection.cc
action_format.yaml
```

この段階では DQN 推論を使わず、手作業または簡単なスクリプトで action CSV を作ってよい。

### Phase 2: infer_actions.py の Multi-DQN 化

目的:

```text
1つの checkpoint を候補 UE 複数に適用し、最大 K action を出力する。
```

変更:

```text
scripts/dqn/infer/infer_actions.py
```

確認:

```bash
python3 scripts/dqn/infer/infer_actions.py \
  --input OUTPUT/80/no_switch/master_log_1_*.csv \
  --checkpoint models/dqn/checkpoints/<new_9dim_model>.pt \
  --output episodes/dqn/actions/actions_multi_dqn_seed1.csv \
  --candidate-mode unsatisfied \
  --max-switches 5
```

### Phase 3: build_transitions.py の複数候補対応

目的:

```text
switched_only / unsatisfied の transition を作れるようにする。
```

変更:

```text
scripts/dqn/dataset/build_transitions.py
```

確認:

```bash
python3 scripts/dqn/dataset/build_transitions.py \
  --input "OUTPUT/80/multi_greedy/master_log_*.csv" \
  --output-dir episodes/dqn/transitions \
  --target-mode switched_only
```

### Phase 4: 再学習

目的:

```text
Multi-DQN 用の 9 次元 checkpoint を作る。
```

確認:

```bash
python3 scripts/dqn/train/train_dqn.py \
  --input "episodes/dqn/transitions/*.csv" \
  --config data/dqn/configs/dqn.yaml
```

### Phase 5: baseline 比較

比較対象:

```text
no_switch
random
rulebase
greedy
multi_greedy
multi_offload
dqn
multi_dqn
```

---

## 14. 注意点とリスク

### 14.1 credit assignment 問題

複数 UE を同時に切り替えた場合、`H_after - H_before` は複数 action の合成効果である。

初期 Multi-DQN では同じ reward を各候補 transition に付けるが、これは近似である。

将来的には以下を検討する。

```text
step ごとの推定 reward
counterfactual reward
各 UE を外した場合の H 差分
multi_greedy の探索ログから step_reward を保存
```

### 14.2 同時切り替えによる状態変化

候補 UE ごとに独立に Q 値を出すと、1 台目を切り替えた後に AP 接続数や H が変わる影響を 2 台目の state に反映できない。

初期実装では許容するが、将来的には sequential 推論に拡張する。

### 14.3 既存 checkpoint との互換性

現在 DQN は `cycle_id` を含む 9 次元入力になっている。

8 次元時代の checkpoint は使えない。

### 14.4 action CSV と seed の一致

`seed` が `data/setting.json` や ns-3 実行時の `rngSeed` と一致していない場合、評価が崩れる。

`LoadDqnActions()` では引き続き seed 不一致を警告する。

---

## 15. 最終的な到達形

初期 Multi-DQN の到達形は以下である。

```text
DQN 本体:
  UE 単位 9 特徴量 → 3 基地局 Q 値

推論:
  各 cycle の複数候補 UE に DQN を適用
  advantage 順に最大 MaxSwitches 件を選択

ns-3:
  同一 cycle の複数 action を読み込み
  複数 UE の割当を一括反映

評価:
  cycle 1, 2 で H を早期改善できるか
  cycle 3 以降で H を維持できるか
  baseline より H / 不満足端末数 / 切り替え副作用が改善するか
```

この設計により、現在の最小 DQN 実装を捨てずに、研究目的である「複数端末候補を用いた全体 QoE 改善」へ段階的に拡張できる。
