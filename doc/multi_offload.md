# multi_offload 設計書

作成日: 2026-07-18  
対象 method: `multi_offload`

## 1. 目的

`multi_offload` は、**混雑している AP を先に特定し、その AP から UE を逃がす**負荷分散方策である。

既存の `greedy` / `multi_greedy` は、主に UE 視点で「不満足端末」を候補にして、全体の端末満足度の調和平均 `H` を改善する移動を探す。  
一方、`multi_offload` は AP 視点で、混雑 AP に所属する UE のうち、移動しても満足度が下がらない、または上がる UE を候補にする。

狙いは以下である。

- 混雑 AP の負荷を直接下げる。
- 不満足 UE だけでなく、移動しても QoE を維持できる UE も退避候補にする。
- 切り替え対象 UE だけでなく、AP 配下の他 UE への負荷改善効果を `H` で評価する。
- 最終判断は端末満足度の調和平均 `H` 最大化で行う。

## 2. 既存実装との関係

関連ファイル:

- `contrib/kameda/model/server/APselection.h`
- `contrib/kameda/model/server/APselection.cc`
- method 分岐がある `APselection::tmain()`

既存手法:

- `greedy_assignment()`
  - 1 サイクル最大 1 UE
  - 不満足 UE のみ候補
  - `H_after - H_before` が最大の移動を採用
- `multi_greedy_assignment()`
  - 1 サイクル最大 `m_MaxSwitches` UE
  - 不満足 UE のみ候補
  - 逐次 greedy により `H` を改善

`multi_offload` は、既存 baseline を壊さないため新規 method として追加する。

推奨する関数名は、既存命名規則に合わせて以下とする。

```cpp
void multi_offload_assignment();
```

コマンドライン method 名は以下とする。

```bash
./ns3 run "master --method=multi_offload"
```

## 3. 基本方針

処理手順は以下を基本とする。

1. AP ごとの接続数、RTT、所属 UE の平均満足度（診断・タイブレーク用）を計算する。
2. 接続数と監視 RTT を中心に、混雑 AP を選ぶ。低満足 UE の比率は混雑 AP スコアに入れない。
3. その AP 上の **全 UE** を候補にする。不満足 UE かどうかでは絞り込まない。
4. 各候補 UE について、他 AP へ移した後も自身の満足度低下が小さい、または満足状態を維持できるかを確認する。
5. その UE を別 AP に移したとき、全体 `H` が最大になる移動を採用する。

重要な点は、**候補生成は混雑 AP 視点、採用判断は全体 `H` 視点**に分けることである。

## 4. AP ごとの指標

### 4.1 接続数

現在割当 `assignment` から AP ごとの接続数を計算する。

既存の補助関数を利用できる。

```cpp
std::vector<int> count_users_per_ap(const std::vector<int>& assignment) const;
```

### 4.2 RTT

監視端末から得られる AP ごとの RTT を使う。

関連メンバ:

```cpp
std::vector<double> m_monitor_rtt;
std::vector<bool> m_has_rtt;
```

RTT が未取得の AP は、混雑度評価で過大評価・過小評価しないように扱う。

初期実装では以下を推奨する。

```text
m_has_rtt[ap] == true なら m_monitor_rtt[ap] を使用
m_has_rtt[ap] == false なら estimate_rtt_ms_for_assignment() のフォールバック値、または 0 として RTT 項を無効化
```

研究上は、未取得 RTT を極端に悪い値として扱うと、測定欠損が混雑判定を歪めるため避ける。

### 4.3 所属 UE の満足度

各 AP に所属する UE について、現在割当における満足度を推定する。

推奨:

```cpp
estimate_satisfaction_for_assignment(termIdx, currentAp - 1, assignment)
```

理由:

- `calculate_satisfaction()` は現在の実測 TP/RTT に基づく。
- `estimate_satisfaction_for_assignment()` は仮割当 `assignment` の AP 接続数を反映できる。
- `multi_offload` は移動後の負荷変化を見るため、推定関数を使う方が方策の意図に合う。

## 5. 混雑 AP の選び方

### 5.1 推奨スコア

単純に接続数だけで混雑 AP を選ぶと、AP の RTT や配下 UE の QoE 悪化を見落とす。  
そのため、初期実装では以下のような正規化スコアを推奨する。

```text
load_score(ap) = w_users * normalized_user_count(ap)
               + w_rtt   * normalized_rtt(ap)
```

各項目:

```text
normalized_user_count(ap) = users_on_ap / max_users_on_any_ap
normalized_rtt(ap)        = rtt_ap / max_rtt_among_measured_ap
```

初期重み:

```text
w_users = 0.8
w_rtt   = 0.2
```

`multi_offload` は「不満足 UE を直接救う」方策ではないため、
混雑 AP スコアには不満足 UE 比率を入れない。平均満足度はログ確認と、
スコア同点時のタイブレークにのみ使う。

### 5.2 実装を簡単に始める場合

まず最小実装にする場合は、以下の優先順位で混雑 AP を選んでもよい。

```text
1. 接続数が最大
2. 同数なら RTT が大きい
3. さらに同じなら平均満足度が低い
```

ただし、研究比較ではスコア式の方が説明しやすく、ログにも残しやすいため推奨する。

### 5.3 AP 選択時の注意

- 接続 UE 数が 0 の AP は混雑 AP 候補から除外する。
- AP 数 `aps` が 1 以下の場合は切り替え先がないため何もしない。
- `assignment` の AP 番号は 1 ベース、内部 index は 0 ベースである点に注意する。

## 6. UE 候補の作り方

混雑 AP `congestedAp` に所属する UE だけを走査する。

候補条件:

```text
1. 現在 AP が congestedAp である
2. 移動先 AP が現在 AP と異なる
3. 移動前の対象 UE 満足度が offload 可能しきい値以上である
4. 移動後の対象 UE 満足度が、移動前満足度から許容低下幅以内、または満足維持しきい値以上である
5. 全体 H が改善する
```

対象 UE の満足度維持条件:

```text
s_after >= s_before - tolerance
```

初期実装の推奨値:

```text
tolerance = 0.1
```

理由:

- TP 推定は、移動先 AP の既存 UE の実測 TP 平均や接続台数補正に基づくため、完全には正確でない。
- 切り替え後の満足度低下が 0.1 程度なら推定誤差・測定揺らぎとして許容し、候補から除外しすぎない。
- ただし、対象 UE の満足度低下を許容する場合でも、最終採用は全体 `H` の改善を原則とする。

実装上は、浮動小数誤差用の `1e-6` ではなく、研究パラメータとして意味を持つ許容幅を明示する。

```cpp
constexpr double kSatisfactionDropTolerance = 0.1;
```

offload 可能しきい値は、移動元で既に QoE に余裕がある UE を選ぶための入口条件である。

```cpp
constexpr double kOffloadableSourceThreshold = 0.8;
```

満足度維持しきい値は、移動後も大きく壊れていないことを確認するために使う。

```cpp
constexpr double kMaintainSatisfactionThreshold = 0.8;
```

候補条件は「不満足 UE であること」ではなく、以下とする。

```text
s_before >= kOffloadableSourceThreshold
かつ
(
  s_after >= s_before - kSatisfactionDropTolerance
  または
  s_after >= kMaintainSatisfactionThreshold
)
```

## 6.1 TP 重視アプリの切り替え後 TP 推定

TP 重視アプリ、つまりブラウザ閲覧・動画視聴の UE については、移動先 AP で期待される TP を推定し、以下で満足度を計算する。

```text
S_i = estimated_tp_mbps / required_tp_mbps
```

`multi_offload` では、切り替え後 TP の推定において、**移動先 AP にいる同じアプリ種別の端末の実測 TP 平均を最優先**に使う。

推奨する優先順位:

```text
1. 移動先 AP に接続中の同じアプリ種別 UE の実測 TP 平均
2. 移動先 AP に接続中の TP 重視アプリ UE 全体の実測 TP 平均
3. 対象 UE 自身の現在実測 TP
4. TP 未計測の場合は satisfaction floor 相当の TP
```

既存の `estimate_tp_mbps_for_assignment()` はこの方針に近く、以下の考え方で使う。

```cpp
const double estimatedTpMbps = estimate_tp_mbps_for_assignment(
    termIdx, targetAp - 1, candidate);
const double sAfter = estimatedTpMbps / requiredTpMbps;
```

さらに、候補割当前後の AP 接続台数比で簡易補正する。

```text
移動先 AP の接続台数が増える場合: 推定 TP を下げる方向に補正
移動元 AP の接続台数が減る場合: 残る UE の推定 TP を上げる方向に補正
```

この推定は完全な物理層・MAC 層再計算ではないため、満足度 0.1 程度の低下は候補生成段階では許容する。ただし、全体 `H` が改善しない移動は採用しない。

## 7. 移動先 AP の評価

候補 UE ごとに、現在 AP 以外の AP をすべて試す。

擬似コード:

```cpp
std::vector<int> candidate = assignment;
candidate[termIdx] = targetAp; // 1-based

const double sBefore = estimate_satisfaction_for_assignment(
    termIdx, currentAp - 1, assignment);
if (sBefore < kOffloadableSourceThreshold)
{
    continue;
}

const double sAfter = estimate_satisfaction_for_assignment(
    termIdx, targetAp - 1, candidate);

const bool toleratedDrop = (sAfter + kSatisfactionDropTolerance >= sBefore);
const bool keepsSatisfaction = (sAfter >= kMaintainSatisfactionThreshold);
if (!toleratedDrop && !keepsSatisfaction)
{
    continue;
}

const double hAfter = calculate_harmonic_mean_for_assignment(candidate);
const double deltaH = hAfter - hCurrent;
```

採用候補は以下で比較する。

```text
第1優先: H_after が最大
第2優先: deltaH が大きい
第3優先: 混雑 AP の接続数削減効果
```

初期実装では、`deltaH > 1e-6` の改善がある移動だけを採用することを推奨する。

```text
bestDeltaH > 1e-6 なら採用
それ以外は no switch
```

ただし、「H はほぼ維持しつつ混雑 AP を下げる」挙動も評価したい場合は、将来的に以下の条件を検討する。

```text
bestDeltaH >= -small_tolerance かつ congested_ap_user_count が減る
```

この緩和条件を入れる場合は、`reward` やログで `H` が悪化したことを必ず確認できるようにする。

## 8. 1 サイクル内で複数 UE を逃がすか

method 名が `multi_offload` なので、1 サイクル内で複数 UE の退避を許す設計が自然である。

推奨は、`multi_greedy` と同様に逐次 greedy とする。

```text
1. 混雑 AP を選ぶ
2. その AP から最良の 1 UE 移動を選ぶ
3. 採用したら assignment と hCurrent を更新する
4. 再度 AP 指標を計算し、混雑 AP を選び直す
5. MaxSwitches に達するか、改善候補がなくなれば終了
```

既存の `m_MaxSwitches` を流用できる。

```cpp
uint32_t m_MaxSwitches = 8;
```

ただし、比較実験では以下に注意する。

- `multi_greedy` と同じ `m_MaxSwitches` で比較する。
- 切り替え回数が QoE 悪化や不安定化を招く可能性があるため、`switch_count` を必ず評価する。
- `m_MaxSwitches = 1` の結果も取り、単一 offload と複数 offload の差を確認する。

## 9. 詳細アルゴリズム

### Step 0: 初期化

```cpp
std::vector<int> assignment = initial_AP;
double hBefore = m_cycleHarmonicMeans.empty()
    ? calculate_harmonic_mean_for_assignment(assignment)
    : m_cycleHarmonicMeans.back();
double hCurrent = hBefore;
uint32_t switchCount = 0;
```

### Step 1: AP 指標の計算

各 AP について以下を計算する。

```text
users_on_ap
monitor_rtt_ms
avg_satisfaction_on_ap
load_score
```

実装しやすくするため、AP 指標用のローカル構造体を `APselection.cc` 内に置くことを推奨する。

```cpp
struct ApLoadInfo
{
    int ap; // 1-based
    int users = 0;
    double rttMs = 0.0;
    bool hasRtt = false;
    double avgSatisfaction = 0.0;
    double score = 0.0;
};
```

外部公開が不要なら `APselection.h` には置かず、`.cc` の無名 namespace または関数ローカルで十分である。

### Step 2: 混雑 AP の選択

```cpp
int congestedAp = SelectCongestedAp(loadInfos); // 1-based
```

初期実装では helper 関数化せず、`multi_offload_assignment()` 内に書いてもよい。  
ただし、後でログ出力や重み変更を行う可能性が高いため、以下の private helper への分割を推奨する。

```cpp
std::vector<ApLoadInfo> BuildApLoadInfo(const std::vector<int>& assignment) const;
int SelectCongestedAp(const std::vector<ApLoadInfo>& infos) const;
```

`ApLoadInfo` を header に出したくない場合は、helper を `.cc` 内の無名 namespace 関数として実装する。

### Step 3: 混雑 AP 配下 UE の移動評価

```cpp
int bestTerm = -1;
int bestAp = -1;
double bestHAfter = hCurrent;
double bestDeltaH = 0.0;

for (int termIdx = 0; termIdx < terms; ++termIdx)
{
    if (assignment[termIdx] != congestedAp)
    {
        continue;
    }

    const double sBefore = estimate_satisfaction_for_assignment(
        termIdx, congestedAp - 1, assignment);

    for (int targetAp = 1; targetAp <= aps; ++targetAp)
    {
        if (targetAp == congestedAp)
        {
            continue;
        }

        std::vector<int> candidate = assignment;
        candidate[termIdx] = targetAp;

        const double sAfter = estimate_satisfaction_for_assignment(
            termIdx, targetAp - 1, candidate);
        if (sAfter + kSatisfactionDropTolerance < sBefore)
        {
            continue;
        }

        const double hAfter = calculate_harmonic_mean_for_assignment(candidate);
        const double deltaH = hAfter - hCurrent;
        if (deltaH > bestDeltaH)
        {
            bestDeltaH = deltaH;
            bestHAfter = hAfter;
            bestTerm = termIdx;
            bestAp = targetAp;
        }
    }
}
```

### Step 4: 採用と繰り返し

```cpp
if (bestTerm < 0 || bestAp < 1 || bestDeltaH <= kMinImprovement)
{
    break;
}

assignment[bestTerm] = bestAp;
hCurrent = bestHAfter;
switchCount++;
```

`switchCount >= m_MaxSwitches` まで繰り返す。  
各ステップで混雑 AP を選び直すことを推奨する。

## 10. APselection.h / APselection.cc への追加案

### 10.1 APselection.h

private メソッドに追加する。

```cpp
void multi_offload_assignment(); // 混雑AP視点の複数端末offload法
```

必要に応じて、将来的に以下を追加する。

```cpp
// AP負荷指標をheaderに公開しないなら不要
// double CalculateApLoadScore(...);
```

### 10.2 APselection.cc の method 分岐

`APselection::tmain()` に追加する。

```cpp
else if (m_assignmentMethod == "multi_offload")
{
    multi_offload_assignment();
}
```

### 10.3 init() の表示

`multi_greedy` と同様に、`m_MaxSwitches` を表示すると実験ログ確認がしやすい。

```cpp
if (m_assignmentMethod == "multi_offload")
{
    std::cout << "[MultiOffload] 1サイクルあたりの最大offload端末数: "
              << m_MaxSwitches << " 台" << std::endl;
}
```

### 10.4 config 側

method 許可リストがある場合は、`multi_offload` を追加する。  
検索例:

```bash
grep -R "multi_greedy\|assignmentMethod\|method" -n master contrib/kameda | head
```

## 11. ログ設計

既存の `master_log.csv` には以下が出ている想定である。

- `h_before`
- `h_after`
- `reward`
- `switch_flag`
- `current_bs_id`
- `previous_bs_id`
- `num_users_on_bs`
- `harmonic_mean`
- `num_unsatisfied_users`

`multi_offload` の検証では、追加ログがあると分析しやすい。

推奨追加ログ:

```text
cycle_id
step_id
congested_ap
congested_ap_score
congested_ap_users
congested_ap_rtt_ms
selected_ue_id
from_ap
to_ap
ue_s_before
ue_s_after
h_before_step
h_after_step
delta_h
```

初期実装では標準出力だけでもよいが、研究比較に使う場合は CSV で保存することを推奨する。

## 12. 評価時に見るべき指標

`multi_offload` は「AP 負荷分散」を狙うため、通常の QoE 指標に加えて AP 側指標も確認する。

必須:

- 端末満足度の調和平均 `H`
- 不満足端末数
- 切り替え回数
- 平均 TP
- 平均 RTT
- 切り替え対象外端末の満足度悪化数

追加推奨:

- AP ごとの接続 UE 数の分散
- 最大接続 UE 数
- AP ごとの平均満足度
- 混雑 AP として選ばれた回数
- offload された UE の移動前後満足度
- `H` は改善したが offload UE 自身は悪化していないか

## 13. 実装上の注意

### 13.1 1 ベース / 0 ベース

`assignment` と `initial_AP` は 1 ベース AP 番号である。

```text
assignment[termIdx] = 1 なら AP1
estimate_satisfaction_for_assignment() の ap_idx は 0 ベース
```

そのため、満足度推定では必ず `ap - 1` に変換する。

### 13.2 RTT 未計測

RTT 未計測 AP を無条件に混雑 AP にしない。  
未計測は `hasRtt=false` としてログに出し、スコア計算では RTT 項を 0 または平均値で補完する。

### 13.3 候補 UE の満足度低下禁止

ユーザ指定手順では「切り替えても満足度が下がらないまたは上がる UE」を候補にする。  
したがって、対象 UE の `s_after >= s_before - 0.1` 条件を入れる。

ただし、対象 UE の満足度が維持されても、他 UE の満足度低下により `H` が下がる可能性がある。  
そのため、最終採用は `H` 改善条件で制御する。

### 13.4 baseline との公平性

- `multi_greedy` と同じ `m_MaxSwitches` で比較する。
- 同一 seed・同一端末配置・同一アプリ分布で比較する。
- `H` だけでなく、AP 接続数の偏りも確認する。
- 未実行の結果を改善として断定しない。

## 14. 推奨する初期実装順序

1. `APselection.h` に `multi_offload_assignment()` を追加する。
2. `APselection::tmain()` に `method == "multi_offload"` 分岐を追加する。
3. `multi_offload_assignment()` を `multi_greedy_assignment()` の直後に追加する。
4. まずは 1 サイクル内で最大 `m_MaxSwitches` 回の逐次 offload を実装する。
5. AP 負荷スコアは最初はローカル構造体で実装する。
6. 標準出力に以下を出す。

```text
[MultiOffload] congested_ap=...
[MultiOffload] step=... switch term=... APx -> APy s_before=... s_after=... h_before_step=... h_after_step=... delta=...
[MultiOffload] selected_switches=... h_before=... h_after_estimated=... reward=...
```

7. 動作確認後、必要なら offload 専用 CSV ログを追加する。

## 15. 最小疑似コード

```cpp
void APselection::multi_offload_assignment()
{
    constexpr double kMinImprovement = 1e-6;
    constexpr double kSatisfactionDropTolerance = 0.1;
    constexpr double kOffloadableSourceThreshold = 0.8;
    constexpr double kMaintainSatisfactionThreshold = 0.8;

    std::vector<int> assignment = initial_AP;
    const double hBefore = m_cycleHarmonicMeans.empty()
        ? calculate_harmonic_mean_for_assignment(assignment)
        : m_cycleHarmonicMeans.back();
    double hCurrent = hBefore;
    uint32_t switchCount = 0;

    while (switchCount < m_MaxSwitches)
    {
        // 1. APごとの users/rtt/satisfaction/score を計算
        // 2. score が最大の AP を congestedAp として選ぶ
        int congestedAp = SelectCongestedApForCurrentAssignment(assignment);
        if (congestedAp < 1)
        {
            break;
        }

        int bestTerm = -1;
        int bestAp = -1;
        double bestHAfter = hCurrent;
        double bestDeltaH = 0.0;
        double bestSBefore = 0.0;
        double bestSAfter = 0.0;

        for (int termIdx = 0; termIdx < terms; ++termIdx)
        {
            if (termIdx >= static_cast<int>(assignment.size()) ||
                assignment[termIdx] != congestedAp)
            {
                continue;
            }

            const double sBefore = estimate_satisfaction_for_assignment(
                termIdx, congestedAp - 1, assignment);
            if (sBefore < kOffloadableSourceThreshold)
            {
                continue;
            }

            for (int targetAp = 1; targetAp <= aps; ++targetAp)
            {
                if (targetAp == congestedAp)
                {
                    continue;
                }

                std::vector<int> candidate = assignment;
                candidate[termIdx] = targetAp;

                const double sAfter = estimate_satisfaction_for_assignment(
                    termIdx, targetAp - 1, candidate);
                const bool toleratedDrop =
                    (sAfter + kSatisfactionDropTolerance >= sBefore);
                const bool keepsSatisfaction =
                    (sAfter >= kMaintainSatisfactionThreshold);
                if (!toleratedDrop && !keepsSatisfaction)
                {
                    continue;
                }

                const double hAfter = calculate_harmonic_mean_for_assignment(candidate);
                const double deltaH = hAfter - hCurrent;
                if (deltaH > bestDeltaH)
                {
                    bestDeltaH = deltaH;
                    bestHAfter = hAfter;
                    bestTerm = termIdx;
                    bestAp = targetAp;
                    bestSBefore = sBefore;
                    bestSAfter = sAfter;
                }
            }
        }

        if (bestTerm < 0 || bestAp < 1 || bestDeltaH <= kMinImprovement)
        {
            break;
        }

        std::cout << "[MultiOffload] step=" << (switchCount + 1)
                  << " switch term=" << (bestTerm + 1)
                  << " AP" << assignment[bestTerm]
                  << " -> AP" << bestAp
                  << " s_before=" << bestSBefore
                  << " s_after=" << bestSAfter
                  << " h_before_step=" << hCurrent
                  << " h_after_step=" << bestHAfter
                  << " delta=" << bestDeltaH << std::endl;

        assignment[bestTerm] = bestAp;
        hCurrent = bestHAfter;
        switchCount++;
    }

    m_lastAssignment = assignment;
    PrepareDecisionLogState(initial_AP, assignment, hBefore, hCurrent);
    if (m_handoverCallback)
    {
        m_handoverCallback(assignment);
    }
}
```

`SelectCongestedApForCurrentAssignment()` は説明用の仮名である。  
実装時は、`.cc` 内のローカル処理にするか、private helper として追加する。

## 16. 研究メモ

`multi_offload` は、不満足 UE だけを直接救う方策ではなく、混雑 AP の負荷を下げることで AP 配下全体の QoE 改善を狙う方策である。  
そのため、次のようなケース分析が重要である。

- 不満足端末数は減らないが `H` が改善するケース
- offload UE 自身の満足度は維持され、他 UE の満足度が改善するケース
- AP 接続数の偏りは改善したが `H` が改善しないケース
- `multi_greedy` より切り替え回数が多くなり、QoE が不安定化するケース

結論を出す際は、平均 TP や不満足端末数だけでなく、必ず調和平均 `H`、満足度分布、切り替え対象外端末への影響を合わせて確認する。
