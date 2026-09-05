# Centralized DQN 改良設計書: one-hot 特徴量と Factorized Q Model

作成日: 2026-09-05  
対象プロジェクト: ns-3.44 QoE-aware 5G/Wi-Fi AP/base station selection  
対象 method: `centralized_dqn`  
対象実装: `rl/centralized_protocol.py`, `rl/export_centralized_bc_dataset.py`, `rl/pretrain_centralized.py`, `rl/centralized_agent.py`, `rl/centralized_server.py`, `contrib/kameda/model/server/APselection.cc`

---

## 1. 目的

現状の `centralized_dqn` は，全体状態 `state` から `action_id = target_ue_id * num_aps + selected_bs_id` を直接予測する構成である。
80 UE, 3 AP 条件では `action_dim = 240` となり，事前学習では 240 クラスの完全一致問題として扱われている。

この設計書では，以下 5 点を実装するための方針を定義する。

1. `app_type` one-hot 化
2. `current_bs_id` one-hot 化
3. AP ID / AP type one-hot 化
4. validation 指標に `target_ue_acc` と `selected_ap_acc` を追加
5. factorized Q model を既存 MLP と比較

目的は，`centralized_dqn` がアプリ要求と AP 特性をカテゴリとして正しく扱い，かつ `(UE, AP)` 完全一致に過度依存しない構造を導入することで，未知 seed への汎化性能を改善することである。

---

## 2. 現状実装の課題

### 2.1 `app_type` が整数値のまま

現状の UE 特徴では `app_type` が 1, 2, 3, 4 の数値として格納される。

```text
1: browser
2: video
3: voice
4: game
```

この表現では MLP が `game > voice > video > browser` のような順序関係を誤って利用する可能性がある。
しかし研究上重要なのは番号の大小ではなく，以下の QoE 要求の違いである。

```text
browser / video: TP 重視
voice / game: RTT 重視
```

### 2.2 `current_bs_id` / AP ID が整数値のまま

現状では `current_bs_id` も 0, 1, 2 の数値として入る。

```text
0: 5G gNB
1: Wi-Fi AP1
2: Wi-Fi AP2
```

これもカテゴリ変数であり，数値距離に意味はない。
したがって one-hot 化する。

### 2.3 AP 特性が弱い

現状の AP 特徴は以下である。

```text
num_users
monitor_rtt
mean_tp
mean_satisfaction
num_unsatisfied
```

これらは動的負荷情報として有用だが，AP の静的属性が不足している。
特に AP0 が 5G gNB，AP1/AP2 が Wi-Fi AP であるという種別情報を明示する必要がある。

### 2.4 `(target UE, AP)` 完全一致問題になっている

現状の BC 事前学習は `CrossEntropyLoss(Q(s), expert_action_id)` であり，以下をすべて同じ不正解として扱う。

```text
教師: UE 10 -> AP 2
予測: UE 10 -> AP 1  # UE は正しいが AP が違う
予測: UE 11 -> AP 2  # AP は正しいが UE が違う
予測: UE 9  -> AP 2  # 近い候補の可能性がある
```

このため，モデルが「どの UE を動かすべきか」と「どの AP へ動かすべきか」を分けて学習・評価できない。

---

## 3. 実装方針の全体像

既存実装を壊さないため，以下の 2 系統を並行維持する。

| 系統 | 目的 | 互換性 |
|---|---|---|
| `centralized_mlp_v1` | 現状 MLP の baseline | 既存 checkpoint と互換 |
| `centralized_factorized_v2` | one-hot + factorized Q model | 新 checkpoint のみ |

初期実装では C++/ns-3 との通信プロトコルは極力維持する。
Python 側で受け取る `state.features` は v2 用に次元が変わるため，checkpoint metadata に以下を必ず保存する。

```json
{
  "model_type": "centralized_factorized_v2",
  "schema_version": "centralized_state_v2",
  "num_ues": 80,
  "num_aps": 3,
  "state_dim": <v2_state_dim>,
  "action_dim": 240,
  "feature_schema": {...},
  "normalization": {...}
}
```

---

## 4. 特徴量設計 v2

### 4.1 app_type one-hot 化

現状の `app_type` 1 次元を削除するか，互換確認用に残す場合も model 入力には原則使わない。

新規 UE 特徴:

```text
app_browser
app_video
app_voice
app_game
is_tp_app
is_rtt_app
tp_need_mbps
rtt_need_ms
```

アプリ要求値:

| app | app_type | is_tp_app | is_rtt_app | tp_need_mbps | rtt_need_ms |
|---|---:|---:|---:|---:|---:|
| browser | 1 | 1 | 0 | 2.3 | 0 |
| video | 2 | 1 | 0 | 8.0 | 0 |
| voice | 3 | 0 | 1 | 0 | 100 |
| game | 4 | 0 | 1 | 0 | 40 |

実装上は値のスケール差を避けるため，`tp_need_mbps` と `rtt_need_ms` は正規化対象に含める。

### 4.2 current_bs_id one-hot 化

新規 UE 特徴:

```text
current_is_ap0
current_is_ap1
current_is_ap2
```

`current_bs_id` の数値特徴は v2 では model 入力から外す。ただしログや action decode 用には従来通り保持する。

### 4.3 AP ID / AP type one-hot 化

新規 AP 特徴:

```text
ap_id_is_0
ap_id_is_1
ap_id_is_2
ap_type_5g
ap_type_wifi
```

AP mapping:

| AP ID | 種別 |
|---:|---|
| 0 | 5G gNB |
| 1 | Wi-Fi AP |
| 2 | Wi-Fi AP |

AP v2 特徴:

```text
ap_id_is_0
ap_id_is_1
ap_id_is_2
ap_type_5g
ap_type_wifi
num_users
monitor_rtt
mean_tp
mean_satisfaction
num_unsatisfied
```

---

## 5. v2 state schema

### 5.1 UE features v2

```text
ue_id_normalized
current_is_ap0
current_is_ap1
current_is_ap2
app_browser
app_video
app_voice
app_game
is_tp_app
is_rtt_app
tp_need_mbps
rtt_need_ms
tp_mbps
rtt_ms
satisfaction
measurement_valid
handover_cooldown_flag
last_switch_age
estimated_satisfaction_if_ap0
estimated_satisfaction_if_ap1
estimated_satisfaction_if_ap2
estimated_h_delta_if_ap0
estimated_h_delta_if_ap1
estimated_h_delta_if_ap2
best_estimated_h_delta
```

UE 特徴数は 25。

### 5.2 AP features v2

```text
ap_id_is_0
ap_id_is_1
ap_id_is_2
ap_type_5g
ap_type_wifi
num_users
monitor_rtt
mean_tp
mean_satisfaction
num_unsatisfied
```

AP 特徴数は 10。

### 5.3 Global features v2

初期は現状と同じ 6 特徴を維持する。

```text
cycle_id
harmonic_mean
num_unsatisfied_users
previous_switch_count
previous_num_degraded_users
previous_measured_reward
```

### 5.4 v2 次元

80 UE, 3 AP の場合:

```text
UE:     80 × 25 = 2000
AP:      3 × 10 =   30
global:           =    6
state_dim_v2      = 2036
action_dim        = 240
```

既存 v1 は `state_dim = 1301` であるため，v2 checkpoint は既存 checkpoint と互換性がない。

---

## 6. Factorized Q Model 設計

### 6.1 目的

現状の MLP は以下である。

```text
state vector -> MLP -> Q[240]
```

この構造では `UE i -> AP j` の関係を action head ごとに個別学習するため，UE 間・AP 間で知識が共有されにくい。

Factorized Q Model では，以下のように分解する。

```text
UE encoder:     UE_i features -> ue_emb_i
AP encoder:     AP_j features -> ap_emb_j
Global encoder: global features -> global_emb
Pair scorer:    [ue_emb_i, ap_emb_j, global_emb, pair_features_i_j] -> Q_i_j
```

出力形式は従来と同じにする。

```text
Q_matrix: [num_ues, num_aps]
Q_flat:   [num_ues * num_aps]
action_id = ue_index * num_aps + ap_id
```

これにより C++ 側の action decode は変更不要にできる。

### 6.2 pair features

各 UE–AP ペア `i,j` に対して以下を構成する。

```text
same_ap_flag
estimated_satisfaction_if_target_ap
estimated_h_delta_if_target_ap
target_ap_num_users
target_ap_monitor_rtt
target_ap_mean_tp
target_ap_mean_satisfaction
target_ap_num_unsatisfied
source_ap_num_users
source_ap_monitor_rtt
```

最小実装では以下だけでもよい。

```text
same_ap_flag
estimated_satisfaction_if_target_ap
estimated_h_delta_if_target_ap
target_ap_num_users
target_ap_monitor_rtt
```

### 6.3 推奨モデル構造

```python
class FactorizedQNetwork(nn.Module):
    def __init__(self, ue_dim, ap_dim, global_dim, pair_dim, hidden_dim, emb_dim):
        self.ue_encoder = MLP(ue_dim, emb_dim)
        self.ap_encoder = MLP(ap_dim, emb_dim)
        self.global_encoder = MLP(global_dim, emb_dim)
        self.pair_scorer = MLP(emb_dim * 3 + pair_dim, 1)

    def forward(self, state_vector):
        ue_x, ap_x, g_x = unpack_state(state_vector)
        ue_emb = self.ue_encoder(ue_x)      # [B, U, E]
        ap_emb = self.ap_encoder(ap_x)      # [B, A, E]
        g_emb  = self.global_encoder(g_x)   # [B, E]
        q = score_all_pairs(ue_emb, ap_emb, g_emb, pair_features)
        return q.reshape(B, U * A)
```

### 6.4 Masking

推論時は現状と同様に `valid_action_ids` を使う。

```text
invalid target UE
same AP
handover cooldown
already switched in cycle
safety_h_delta threshold 未満
```

は C++ 側で除外し，Python 側は valid action の中から最大 Q を選ぶ。

---

## 7. 2段階評価指標の追加

### 7.1 現状指標の問題

現在の BC 評価は `top1`, `top5`, `top10`, `top20` で，`expert_action_id` の完全一致だけを見ている。
このままだと，失敗の原因が以下のどちらか分からない。

```text
target UE 選択を間違えた
selected AP 選択を間違えた
```

### 7.2 追加する validation 指標

事前学習時に以下を出力する。

```text
target_ue_acc
selected_ap_acc
selected_ap_acc_given_target_ue_correct
pair_top1
pair_top5
pair_top10
pair_top20
```

定義:

```text
pred_action_id = argmax Q_flat
pred_target_ue = pred_action_id // num_aps + 1
pred_ap        = pred_action_id % num_aps

true_target_ue = target_ue_id
true_ap        = selected_bs_id

target_ue_acc = mean(pred_target_ue == true_target_ue)
selected_ap_acc = mean(pred_ap == true_ap)
selected_ap_acc_given_target_ue_correct =
    mean(pred_ap == true_ap | pred_target_ue == true_target_ue)
```

### 7.3 Factorized model で追加推奨する指標

```text
ue_top5_acc
ue_top10_acc
ap_acc_for_true_ue
best_estimated_delta_rank
```

`ap_acc_for_true_ue` は，真の target UE を与えた場合に AP head / pair score が正しい AP を選べるかを見る指標である。

```text
ap_acc_for_true_ue = mean(argmax_j Q[true_ue, j] == true_ap)
```

これにより，UE 選択の難しさと AP 選択の難しさを分離できる。

---

## 8. 既存 MLP との比較設計

### 8.1 比較対象

以下 3 条件を比較する。

| 条件名 | 特徴量 | モデル |
|---|---|---|
| `mlp_v1` | 現状 1301 次元 | 既存 MLP |
| `mlp_v2_onehot` | one-hot v2 2036 次元 | MLP |
| `factorized_v2` | one-hot v2 2036 次元 | Factorized Q |

`mlp_v2_onehot` を入れる理由は，改善が one-hot 化によるものか，factorized 構造によるものかを分離するためである。

### 8.2 Offline BC 比較

同一 dataset split で以下を比較する。

```text
train_loss
val_loss
pair_top1 / top5 / top10 / top20
target_ue_acc
selected_ap_acc
ap_acc_for_true_ue
```

seed split は現状通り有効にする。

```text
--seed-split true
```

### 8.3 ns-3 閉ループ評価

offline 指標だけで結論を出さず，eval-only で ns-3 閉ループ評価を行う。

比較指標:

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

特に以下を確認する。

```text
factorized_v2 が mlp_v1 より H を改善するか
同程度の H で switch_count を削減するか
同程度の H で num_degraded_users を削減するか
validation seed でのばらつきが小さくなるか
```

---

## 9. 実装ファイル案

### 9.1 Python

新規または拡張候補:

```text
rl/centralized_protocol_v2.py
rl/centralized_models.py
rl/pretrain_centralized.py
rl/export_centralized_bc_dataset.py
rl/centralized_server.py
```

推奨方針:

- `centralized_protocol.py` は v1 互換のため残す。
- v2 用に `centralized_protocol_v2.py` を追加する。
- モデルは `rl/centralized_models.py` に分離する。
- `pretrain_centralized.py` に `--model-type {mlp_v1,mlp_v2_onehot,factorized_v2}` を追加する。
- checkpoint metadata から `model_type` と `schema_version` を読んで server が適切なモデルを構築する。

### 9.2 C++ / ns-3

候補:

```text
contrib/kameda/model/server/APselection.cc
contrib/kameda/model/server/APselection.h
```

追加方針:

- 既存 `BuildCentralizedDqnStateJson()` は v1 として維持する。
- v2 用に `BuildCentralizedDqnStateJsonV2()` を追加する。
- CLI オプションで schema を選べるようにする。

例:

```bash
--centralizedDqnStateSchema v1
--centralizedDqnStateSchema v2_onehot
```

既存実験再現性のため，default は当面 `v1` とする。

---

## 10. 実装手順

### Step 1: 評価指標だけ先に追加

最初に `pretrain_centralized.py` へ以下を追加する。

```text
target_ue_acc
selected_ap_acc
selected_ap_acc_given_target_ue_correct
ap_acc_for_true_ue
```

これは state schema やモデル構造を変えずに実装できるため，既存 MLP の弱点を定量化できる。

### Step 2: v2 dataset export を追加

`export_centralized_bc_dataset.py` に `--schema-version {v1,v2_onehot}` を追加する。

v2 では `f0...f2035` を出力し，meta に feature schema を記録する。

### Step 3: `mlp_v2_onehot` を学習

v2 特徴量を使うが，モデルは既存 MLP と同じにする。
これにより one-hot 化単体の効果を見る。

### Step 4: `factorized_v2` を学習

`FactorizedQNetwork` を追加し，同じ v2 dataset / split で学習する。

### Step 5: ns-3 eval-only 比較

同一 seed・同一 K・同一 safety threshold で以下を比較する。

```text
centralized_dqn_mlp_v1
centralized_dqn_mlp_v2_onehot
centralized_dqn_factorized_v2
logistic
```

---

## 11. 注意点

### 11.1 checkpoint 互換性

v1 と v2 では `state_dim` が異なるため checkpoint は互換性がない。
server 起動時に checkpoint metadata と CLI 指定が一致しない場合は即座に error とする。

### 11.2 実験条件の保存

以下は必ずログまたは metadata に保存する。

```text
model_type
schema_version
state_dim
action_dim
num_ues
num_aps
feature_schema
normalization enabled/mean/std
training seeds
validation seeds
safety threshold
maxSwitches
bootstrap cycles
```

### 11.3 既存結果を壊さない

既存 `centralized_dqn` の v1 実験と比較するため，以下は維持する。

```text
existing master_log columns
existing decision_log columns
action_id decode rule
valid_action_ids protocol
```

新しい列を追加する場合も，既存列名は変更しない。

---

## 12. 完了条件

実装完了の最低条件:

- [ ] `app_type` が one-hot 化された v2 dataset を出力できる。
- [ ] `current_bs_id` が one-hot 化された v2 dataset を出力できる。
- [ ] AP ID / AP type one-hot を含む AP v2 特徴を出力できる。
- [ ] `pretrain_centralized.py` が `target_ue_acc` と `selected_ap_acc` を出力する。
- [ ] `mlp_v1`, `mlp_v2_onehot`, `factorized_v2` を同一 split で比較できる。
- [ ] checkpoint metadata で schema/model type を識別できる。
- [ ] ns-3 eval-only で v1 と v2 を同一条件比較できる。

---

## 13. 期待される研究上の効果

この改良により，`centralized_dqn` は単なる 240 クラス分類器ではなく，以下を明示的に扱う制御器になる。

```text
UE のアプリ要求
UE の現在接続先
AP の種別と負荷状態
UE–AP ペアの推定満足度改善
全体調和平均への影響
```

そのため，以下の主張を検証しやすくなる。

```text
Factorized Q model は，UE–AP ペアの構造を利用することで，既存 MLP より未知 seed での汎化性能を改善する。
さらに，アプリ要求と AP 特性を one-hot / type feature として明示することで，QoE 指標に基づく AP 選択を安定化できる。
```

