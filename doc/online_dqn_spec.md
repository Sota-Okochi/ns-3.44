# Online DQN 仕様メモ

この文書は，5G/Wi-Fi 混在環境における基地局選択で，Online DQN を用いる場合の現状仕様，問題点，および今後の設計方針を整理したものである。

## 1. 目的

本研究では，端末ごとの QoE を考慮して接続先基地局を選択し，全端末満足度の調和平均を改善することを目的とする。

Online DQN を用いる場合の狙いは，単にその時点の調和平均を直接探索することではなく，以下のような逐次的な基地局選択方策を学習することである。

- 不満足端末の救済
- 基地局負荷の分散
- 切替回数の抑制
- 切替対象外端末の QoE 悪化抑制
- 複数サイクルにわたる QoE 改善

ただし，現状の固定環境では，Online DQN は multi_greedy に調和平均で直接勝つことを目的にするより，multi_greedy や rulebase と比較して，切替安定性や推論型制御としての有効性を確認する位置づけが自然である。

## 2. multi_greedy との違い

`multi_greedy` は，各 step において候補端末と候補基地局の組を仮に適用し，その都度 `calculate_harmonic_mean_for_assignment()` により推定調和平均を計算する。

すなわち，multi_greedy は以下を直接探索する。

```text
UE i を BS b に移した場合の H_after
```

そのため，multi_greedy は強い探索型 baseline である。一方 Online DQN は，状態ベクトルを入力として Q 値を出力し，接続先基地局を選択する。

```text
state -> Q(s, BS0), Q(s, BS1), Q(s, BS2)
```

従って，Online DQN の役割は以下のいずれかで整理する。

1. multi_greedy の探索を近似する学習型制御器
2. 切替ペナルティを含めた長期 QoE 制御器
3. 学習済みモデルを実行時にオンライン推論する制御器

## 3. 現状実装の概要

現状の `online_dqn` は，ns-3 側から Python DQN server に TCP/JSON で状態を送信し，Python 側が選択基地局 ID を返す構成である。

関連ファイル:

```text
rl/server.py
rl/agent.py
rl/protocol.py
contrib/kameda/model/server/APselection.cc
```

C++ 側では `BuildDqnStateJson()` により，対象端末の状態および基地局負荷・推定 QoE 情報を JSON に含めて送信する。

Python 側では `rl/protocol.py` の `STATE_FEATURES` に定義された特徴量だけを状態ベクトルとして使用する。

## 4. 現状の問題点

以前の Online DQN 実験では，調和平均が安定して上昇しなかった。主な原因は以下である。

### 4.1 状態特徴量が不足していた

当初 Python 側で使用していた特徴量は以下の 9 個であった。

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

一方，C++ 側はすでに以下のような重要情報を JSON に含めていた。

```text
candidate_type
num_users_ap0/1/2
monitor_rtt_ap0/1/2
estimated_satisfaction_if_ap0/1/2
estimated_h_delta_if_ap0/1/2
best_estimated_h_delta
```

しかし，Python 側の `STATE_FEATURES` に含まれていなかったため，DQN はこれらを利用できていなかった。

### 4.2 報酬の信用割当が粗い

1 cycle 内で複数台を切り替え，次 cycle の調和平均変化をまとめて報酬として与えると，どの切替が良かったのか DQN が学習しにくい。

```text
cycle t: 複数 UE を切替
cycle t+1: H の変化を観測
```

この場合，各 action に対して同じような報酬が付与されるため，個別 action の良否が不明確になる。

### 4.3 切替数が多すぎた

`m_MaxSwitches` は上限値であるが，実験ログでは多くの cycle で上限近くまで切替が行われていた。

これは，不満足端末に対して DQN が現在と異なる基地局を選択すると，調和平均改善の有無に関係なく切替が適用されやすかったためである。

### 4.4 safety filter がなかった

multi_greedy は `delta > 0` の切替のみを選ぶが，Online DQN は DQN が選択した基地局が現在基地局と異なれば，悪化候補でも適用される可能性があった。

そのため，以下のような安全確認が必要である。

```text
estimated_h_delta_if_selected <= 0 の場合は切替しない
```

### 4.5 オンライン学習サンプルが少ない

1 回の ns-3 実行に含まれる cycle 数が少ない場合，DQN がオンラインで十分に学習するための transition 数が不足する。

## 5. 推奨する Online DQN 設計

Online learning DQN として成立させるためには，まず信用割当を明確にする必要がある。

### 5.1 最小構成: 1 cycle = 1 action

最初は，各 cycle で 1 台の端末だけを対象にする。

```text
1. 全端末の満足度を計算
2. 最も満足度が低い端末を 1 台選択
3. DQN が接続先基地局を選択
4. その 1 台だけ切替を適用
5. 次 cycle で H_after を観測
6. reward = H_after - H_before を与える
```

対象端末は，まず以下で選ぶ。

\[
i_t = \arg\min_i S_{i,t}
\]

DQN の行動は，対象端末の接続先基地局である。

\[
a_t \in \mathcal{B}
\]

現在の接続先と同じ基地局を選んだ場合は no-op とする。

### 5.2 拡張構成: 1 cycle = K actions

1 台版で学習できることを確認した後，対象端末数を増やす。

```text
K = 1, 2, 4, 8, 15
```

ただし，K 台を完全同時に決めるのではなく，候補端末を 1 台ずつ DQN に入力し，割り当てを逐次更新する。

全体最適化を考慮するため，候補端末は低満足 UE のみには限定しない。低満足 UE を直接救済する `rescue` 候補に加えて，混雑 BS 上にいる高満足 UE を別 BS へ逃がす `offload` 候補を含める。

```text
candidate_set = rescue_candidates + offload_candidates
sort candidate_set by priority

for target UE in candidate_set:
    if applied >= K:
        break
    state を作成
    DQN が selected_bs_id を出力
    safety filter を確認
    適用可能なら assignment を更新
```

`rescue` 候補は，満足度が閾値未満の UE とする。

\[
S_{i,t} < \theta_{\mathrm{rescue}}
\]

`offload` 候補は，混雑 BS に接続しており，かつ移動後も満足度を大きく損なわない高満足 UE とする。

\[
S_{i,t} \ge \theta_{\mathrm{offload}}
\]

かつ，接続先 BS \(c_{i,t}\) の負荷が平均または閾値より大きい場合に候補化する。

\[
N_{c_{i,t},t} > \bar{N}_t
\]

この設計により，DQN は「低満足 UE を直接救済する行動」だけでなく，「混雑 BS のリソースを空けることで他端末の QoE を改善する行動」も選択できる。

K を増やすほど改善速度は上がる可能性があるが，負荷振動や切替対象外端末の悪化が起きやすくなるため，H と切替回数のトレードオフを評価する。

### 5.3 candidate_type の扱い

`candidate_type` は，候補端末が `rescue` 目的なのか `offload` 目的なのかを DQN に伝えるための特徴量である。

```text
candidate_type = 0: rescue candidate
candidate_type = 1: offload candidate
```

低満足 UE のみを候補にする単純構成では `candidate_type` はほぼ常に 0 となるため情報量が小さい。一方，全体最適化を目的として `rescue` と `offload` を混在させる構成では，DQN が候補の役割を区別するために重要な特徴量となる。

## 6. 状態特徴量

DQN の状態は，対象端末状態，基地局負荷，全体 QoE，候補切替効果から構成する。特に `rescue` 候補と `offload` 候補を混在させる場合，候補の役割を識別するために `candidate_type` を含める。

推奨する状態特徴量は以下である。

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
]
```

### 6.1 特徴量の分類

| 分類 | 特徴量 |
|---|---|
| サイクル・端末状態 | `cycle_id`, `current_bs_id`, `app_type`, `tp_mbps`, `rtt_ms`, `satisfaction` |
| 現在接続 BS 状態 | `num_users_on_current_bs` |
| 全体 QoE | `harmonic_mean`, `num_unsatisfied_users` |
| 候補種別 | `candidate_type` |
| 基地局負荷 | `num_users_ap0`, `num_users_ap1`, `num_users_ap2` |
| 基地局 RTT | `monitor_rtt_ap0`, `monitor_rtt_ap1`, `monitor_rtt_ap2` |
| 候補切替時の端末満足度推定 | `estimated_satisfaction_if_ap0/1/2` |
| 候補切替時の調和平均差分推定 | `estimated_h_delta_if_ap0/1/2` |

`candidate_type` は，低満足 UE の直接救済だけではなく，混雑 BS から高満足 UE を逃がして全体 QoE を改善する offload 操作を DQN に学習させるために用いる。したがって，本仕様では `candidate_type` を拡張用ではなく，rescue/offload 混在候補を扱う Online DQN の基本特徴量として扱う。

### 6.2 論文上の表現

本文では，22 個の特徴量をすべて文章中に列挙するのではなく，次のように状態ベクトルとして定義する。

\[
\mathbf{s}_{i,t}
=
[
\mathbf{u}_{i,t},
\mathbf{b}_{t},
\mathbf{e}_{i,t}
]
\]

ここで，

- \(\mathbf{u}_{i,t}\): 対象端末の通信状態・アプリケーション要求
- \(\mathbf{b}_{t}\): 基地局負荷・RTT・全体 QoE
- \(\mathbf{e}_{i,t}\): 各候補基地局へ切り替えた場合の推定 QoE 変化

である。

候補切替効果については，基地局 \(b\) へ切り替えた場合の推定満足度を \(\hat{S}_{i,t}^{(b)}\)，推定調和平均差分を

\[
\Delta \hat{H}_{i,t}^{(b)}
=
\hat{H}_{i,t}^{(b)} - H_t
\]

と定義する。

## 7. 行動空間

行動は，対象端末の接続先基地局 ID とする。

```text
0: BS0 / 5G gNB
1: BS1 / Wi-Fi AP1
2: BS2 / Wi-Fi AP2
```

数式では以下のように表す。

\[
a_{i,t} \in \mathcal{B}
\]

現在の基地局 \(c_{i,t}\) と同じ行動を選んだ場合は切替なしとする。

\[
a_{i,t} = c_{i,t} \Rightarrow \text{no handover}
\]

## 8. 報酬設計

最小構成では，調和平均の変化を報酬とする。

\[
r_t = H_{t+1} - H_t
\]

切替回数を抑制する場合は，切替ペナルティを加える。

\[
r_t
=
H_{t+1} - H_t
-
\alpha M_t
\]

さらに，切替対象外端末を含む満足度悪化を抑える場合は，悪化端末数 \(D_t\) を用いる。

\[
r_t
=
H_{t+1} - H_t
-
\alpha M_t
-
\beta D_t
\]

ここで，

- \(M_t\): cycle \(t\) における切替回数
- \(D_t\): 満足度が悪化した端末数
- \(\alpha\): 切替ペナルティ係数
- \(\beta\): 悪化端末ペナルティ係数

である。

## 9. Safety filter

Online DQN 初期は未熟な行動を選ぶ可能性があるため，推定上明らかに悪い切替を適用しない safety filter を導入する。

基本ルールは以下である。

```text
if selected_bs_id == current_bs_id:
    no-op
elif estimated_h_delta_if_selected <= threshold:
    skip
else:
    apply handover
```

閾値例:

```text
学習時: threshold = -0.02 など，やや緩め
評価時: threshold = 0.0 など，悪化を許容しない
```

ただし，safety filter を強くしすぎると探索が抑制されるため，学習時と評価時で閾値を分けることを検討する。

## 10. 学習手順

推奨する学習手順は以下である。

### Step 1: 事前学習

完全なランダム初期化から online learning を始めると不安定になりやすい。まず multi_greedy や rulebase のログを使って，DQN ネットワークを事前学習する。

```text
state -> selected_bs_id
```

これは厳密には DQN というより behavior cloning であるが，Online DQN の初期方策として有効である。

### Step 2: Online learning

事前学習済み checkpoint を読み込み，ns-3 実行中に replay buffer へ transition を追加しながら DQN を更新する。

```text
(s_t, a_t, r_t, s_{t+1})
```

を保存し，ミニバッチで Q-network を更新する。

### Step 3: 複数 seed 連続学習

1 回の ns-3 実行だけではサンプル数が不足するため，同じ DQN server を起動したまま複数 seed を連続実行し，replay buffer を蓄積する。

```text
seed 1
seed 2
...
seed N
```

### Step 4: 評価

評価時は学習を止める。

```text
epsilon = 0
update = false
```

すなわち，学習済みモデルを固定してオンライン推論のみ行う。

## 11. Online inference と Online learning の区別

以下を明確に区別する。

| 種別 | 実行中の推論 | 実行中の学習 | 用途 |
|---|---:|---:|---|
| offline DQN + action CSV | なし | なし | 事前作成 action の適用 |
| offline DQN + online inference | あり | なし | 学習済みモデルの閉ループ評価 |
| online learning DQN | あり | あり | 実行中に方策を更新 |

複数 cycle の閉ループ制御では，事前作成 action CSV 方式は cycle 2 以降の状態ズレが発生するため望ましくない。

そのため，offline 学習済みモデルを使う場合でも，ns-3 実行中に現在状態を送って推論する online inference が望ましい。

## 12. 評価指標

Online DQN の評価では，調和平均だけでなく以下を必ず確認する。

```text
harmonic_mean
num_unsatisfied_users
平均 TP
平均 RTT
切替回数
切替対象外端末の満足度悪化数
reward
seed 間ばらつき
推論時間 / 実行時間
```

特に，DQN は multi_greedy に調和平均だけで勝つことを目的にするのではなく，以下のような観点でも比較する。

- rulebase より高い調和平均を達成できるか
- random より安定しているか
- multi_greedy に近い H をより少ない切替で達成できるか
- H の分散や悪化端末数を抑えられるか

## 13. 実験計画

最初に確認すべき実験は以下である。

### 13.1 K 比較

```text
K = 1, 2, 4, 8, 15
```

各 cycle における DQN 対象端末数 K を変え，H と切替回数のトレードオフを確認する。

### 13.2 特徴量 ablation

特徴量セットを段階的に増やして比較する。

| 状態表現 | 内容 |
|---|---|
| State-A | 対象 UE + 全体 QoE の最小構成 |
| State-B | State-A + 基地局負荷 + 監視 RTT |
| State-C | State-B + 候補切替時の推定満足度・推定 H 差分 |

### 13.3 baseline 比較

同一 seed・同一設定で以下と比較する。

```text
no_switch
random
rulebase
multi_greedy
online_dqn
online_dqn_safe
```

## 14. 論文上の位置づけ

Online DQN を論文で扱う場合，以下のように位置づけるのが自然である。

```text
multi_greedy は調和平均を直接探索する強い探索型 baseline である。
一方，提案する DQN 手法は，端末状態，基地局負荷，全体 QoE，候補切替効果を状態として入力し，接続先基地局を逐次選択する。
さらに切替ペナルティおよび安全制約を導入することで，QoE 改善と切替安定性の両立を目指す。
```

`estimated_h_delta_if_ap*` を用いる場合は，以下のように正直に記述する。

```text
本手法では，候補基地局へ切り替えた場合の推定 QoE 変化を状態特徴量として用いる candidate-effect-aware DQN を構成する。
```

これは，DQN が完全に観測値のみから判断しているというより，候補切替効果の推定値を補助情報として利用していることを意味する。

## 15. 今後の実装 TODO

- [ ] `rl/protocol.py` の `STATE_FEATURES` を拡張する。
- [ ] `rl/server.py` に eval-only / no-update モードを追加する。
- [ ] `online_dqn_assignment()` で K=1 から実験できるようにする。
- [ ] safety filter を追加する。
- [ ] reward ログに `H_before`, `H_after_measured`, `reward`, `switch_count`, `num_degraded_users` を出力する。
- [ ] 複数 seed 連続学習用の実行手順を整備する。
- [ ] State-A/B/C の ablation 実験を行う。

