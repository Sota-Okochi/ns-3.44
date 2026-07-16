# multi_greedy 設計書

作成日: 2026-07-16  
対象 method: `multi_greedy`

## 1. 目的

現状の `greedy` は 1 サイクルにつき最大 1 端末のみを切り替える。  
本設計では、既存の `greedy` は変更せず、新しい手法として `multi_greedy` を追加する。

`multi_greedy` の目的は、**不満足端末だけを切り替え候補**にして、1 サイクル内で最大 `MaxSwitches` 台まで逐次的に切り替え、端末満足度の調和平均 `H` を改善することである。

将来的に「混雑 AP から余裕端末を逃がす手法」とハイブリッド化できるよう、候補生成部分と候補評価部分を分けて設計する。ただし、本設計で実装対象にする候補は **不満足端末のみ** とする。

## 2. 現状実装の確認

### 2.1 既存 greedy

既存 greedy は以下に実装されている。

- `contrib/kameda/model/server/APselection.cc`
  - `APselection::greedy_assignment()`
- `contrib/kameda/model/server/APselection.h`
  - `void greedy_assignment();`

現在の `greedy_assignment()` は、以下の処理を行う。

1. 現在割当 `initial_AP` を `assignment` にコピーする。
2. 現在の調和平均 `hBefore` を取得する。
3. 全端末を走査する。
4. `satisfaction < 0.8` の不満足端末だけを候補にする。
5. 各候補端末について、現在 AP 以外の AP へ 1 台だけ移した場合の `H_after` を計算する。
6. `H_after - H_before` が最大の 1 件だけを採用する。
7. `m_handoverCallback(assignment)` に最終割当を渡す。

つまり、既存 `greedy` は **単一端末切り替え greedy** である。

### 2.2 複数端末切り替え実行側

切り替え実行側は以下にある。

- `master/handover.cc`
  - `NetSim::HandoverRequest()`
  - `NetSim::ApplyHandoverBatch()`

`NetSim::HandoverRequest()` は、受け取った `assignment` と現在接続先 `m_termAccessState[i].currentAp` を比較し、差分がある端末を `switchList` に追加する。差分が複数あれば、`ApplyHandoverBatch(switchList)` にまとめて渡せる。

したがって、`multi_greedy` 側で複数端末を変更した `assignment` を作れば、切り替え実行側は基本的に流用できる。

## 3. multi_greedy の基本方針

### 3.1 既存 greedy は変更しない

既存の `greedy_assignment()` は比較用 baseline として残す。

新規に以下を追加する。

```cpp
void multi_greedy_assignment();
```

コマンドライン指定は以下とする。

```bash
./ns3 run "master --method=multi_greedy"
```

`config.cc` 側の method 許可リストにも `multi_greedy` を追加する。

### 3.2 候補端末

`multi_greedy` では、切り替え候補を **不満足端末のみ** に限定する。

不満足判定は以下を基本とする。

```text
satisfaction < 0.8
```

この閾値は既存 greedy と揃える。将来的にはパラメータ化してもよいが、初期実装では固定値でよい。

### 3.3 切り替え上限 `MaxSwitches`

1 サイクル内で切り替え可能な最大端末数を `MaxSwitches` で制御する。

```cpp
uint32_t MaxSwitches;
```

意味:

```text
MaxSwitches = 1 なら、現状 greedy と同様に最大 1 端末のみ切り替え
MaxSwitches = 2 なら、最大 2 端末まで切り替え
MaxSwitches = 3 なら、最大 3 端末まで切り替え
```

初期値は既存 greedy と比較しやすいように `1` とする。

```cpp
uint32_t m_MaxSwitches = 1;
```

`MaxSwitches` は **設定ファイルからのみ** 調整できるようにする。コマンドライン引数には追加しない。

例:

```json
{
  "MaxSwitches": 3
}
```

## 4. multi_greedy のアルゴリズム

### 4.1 概要

`multi_greedy` は、1 回で複数端末を同時決定するのではなく、**1 端末ずつ最良候補を選び、仮割当を更新しながら繰り返す**。

理由は、1 端末を切り替えると AP ごとの接続台数が変わり、次の端末の推定 TP・満足度・調和平均が変わるためである。

処理概要:

1. 現在割当 `initial_AP` から開始する。
2. 現在の調和平均を `hCurrent` とする。
3. 不満足端末だけを候補にする。
4. 各候補端末について、現在 AP 以外の AP へ移した場合の `H_after` を評価する。
5. `H_after - hCurrent` が最大の 1 件を選ぶ。
6. 改善があれば、その切り替えを仮割当へ反映する。
7. 更新後の仮割当に対して、再度 greedy 探索を行う。
8. `MaxSwitches` 件に達するか、改善候補がなくなったら終了する。

### 4.2 詳細手順

#### Step 0: 初期化

```cpp
std::vector<int> assignment = initial_AP;
double hBefore = current harmonic mean;
double hCurrent = hBefore;
uint32_t switchCount = 0;
std::vector<int> selectedTerms;
```

`assignment` は探索中に更新する仮割当である。  
`hBefore` はサイクル開始時点の調和平均である。  
`hCurrent` は直近の仮割当に対する推定調和平均である。

#### Step 1: 候補探索

各ステップで、全端末を走査する。

候補にする条件:

```text
1. 端末 index が assignment の範囲内である
2. 同一サイクルですでに選択されていない
3. 現在の仮割当における満足度が 0.8 未満
```

満足度判定は、更新後の仮割当を反映するため、可能なら以下を使う。

```cpp
estimate_satisfaction_for_assignment(termIdx, currentAp - 1, assignment)
```

ただし、既存 greedy と完全に揃えたい場合は、初期実装では `calculate_satisfaction(termIdx, currentAp - 1)` を使ってもよい。研究上は、複数切り替え後の負荷影響を考慮できる `estimate_satisfaction_for_assignment()` を推奨する。

#### Step 2: 移動先 AP の評価

候補端末ごとに、現在 AP 以外の全 AP を試す。

```cpp
for (int ap = 1; ap <= aps; ++ap)
{
    if (ap == currentAp)
    {
        continue;
    }

    std::vector<int> candidate = assignment;
    candidate[termIdx] = ap;

    double hAfter = calculate_harmonic_mean_for_assignment(candidate);
    double delta = hAfter - hCurrent;
}
```

ここで重要なのは、比較対象を最初の `hBefore` ではなく、直近の `hCurrent` にすることである。

```text
1 回目: delta = hAfter - hBefore
2 回目以降: delta = hAfter - hCurrent
```

#### Step 3: 最良候補の採用

全候補の中で `delta` が最大のものを選ぶ。

採用条件:

```text
bestDelta > 1e-6
```

改善がない場合は、そのサイクルの探索を終了する。

採用する場合:

```cpp
assignment[bestTerm] = bestAp;
hCurrent = bestHAfter;
selectedTerms.push_back(bestTerm);
switchCount++;
```

#### Step 4: 繰り返し

以下のどちらかを満たすまで Step 1 から Step 3 を繰り返す。

```text
switchCount >= MaxSwitches
改善候補が存在しない
```

#### Step 5: 最終割当の反映

探索終了後、最終的な `assignment` を採用する。

```cpp
m_lastAssignment = assignment;
PrepareDecisionLogState(initial_AP, assignment, hBefore, hCurrent);
if (m_handoverCallback)
{
    m_handoverCallback(assignment);
}
```

## 5. 擬似コード

```cpp
void APselection::multi_greedy_assignment()
{
    constexpr double kUnsatisfiedThreshold = 0.8;
    constexpr double kMinImprovement = 1e-6;

    std::vector<int> assignment = initial_AP;

    const double hBefore =
        m_cycleHarmonicMeans.empty()
            ? calculate_harmonic_mean_for_assignment(assignment)
            : m_cycleHarmonicMeans.back();

    double hCurrent = hBefore;
    uint32_t switchCount = 0;
    std::vector<int> selectedTerms;

    while (switchCount < m_MaxSwitches)
    {
        int bestTerm = -1;
        int bestAp = -1;
        double bestHAfter = hCurrent;
        double bestDelta = 0.0;

        for (int termIdx = 0; termIdx < terms; ++termIdx)
        {
            if (termIdx >= static_cast<int>(assignment.size()))
            {
                continue;
            }

            if (std::find(selectedTerms.begin(), selectedTerms.end(), termIdx)
                != selectedTerms.end())
            {
                continue;
            }

            const int currentAp = assignment[termIdx];
            const double currentSatisfaction =
                estimate_satisfaction_for_assignment(termIdx, currentAp - 1, assignment);

            if (currentSatisfaction >= kUnsatisfiedThreshold)
            {
                continue;
            }

            for (int ap = 1; ap <= aps; ++ap)
            {
                if (ap == currentAp)
                {
                    continue;
                }

                std::vector<int> candidate = assignment;
                candidate[termIdx] = ap;

                const double hAfter = calculate_harmonic_mean_for_assignment(candidate);
                const double delta = hAfter - hCurrent;

                if (delta > bestDelta)
                {
                    bestDelta = delta;
                    bestHAfter = hAfter;
                    bestTerm = termIdx;
                    bestAp = ap;
                }
            }
        }

        if (bestTerm < 0 || bestAp < 1 || bestDelta <= kMinImprovement)
        {
            break;
        }

        assignment[bestTerm] = bestAp;
        hCurrent = bestHAfter;
        selectedTerms.push_back(bestTerm);
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

## 6. 実装変更箇所

### 6.1 `APselection.h`

追加:

```cpp
void multi_greedy_assignment();
uint32_t m_MaxSwitches = 1;
```

`MaxSwitches` を `ApSelectionInput` 経由で受け取るため、以下も追加する。

```cpp
struct ApSelectionInput {
    ...
    uint32_t MaxSwitches = 1;
};
```

### 6.2 `APselection.cc`

`APselection::tmain()` の method 分岐へ追加する。

```cpp
else if (m_assignmentMethod == "multi_greedy")
{
    multi_greedy_assignment();
}
```

`APselection::init()` で `MaxSwitches` を受け取る。

```cpp
m_MaxSwitches = input.MaxSwitches;
if (m_MaxSwitches == 0)
{
    m_MaxSwitches = 1;
}
```

`multi_greedy_assignment()` を新規実装する。

### 6.3 `master/config.cc`

method 許可リストへ追加する。

```text
multi_greedy
```

設定ファイル読み込み用の構造体へ `MaxSwitches` を追加する。

```cpp
struct BaselineSetting
{
    ...
    int MaxSwitches = 1;
};
```

`LoadBaselineSetting()` で `setting.json` から読み込む。

```cpp
if (ExtractJsonInt(content, "MaxSwitches", tmp))
{
    setting.MaxSwitches = tmp;
}
```

`m_apSelectionInput.MaxSwitches` へ渡す。`MaxSwitches` はコマンドライン引数では受け取らない。

```cpp
m_apSelectionInput.MaxSwitches =
    static_cast<uint32_t>(std::max(1, setting.MaxSwitches));
```

## 7. ログ設計

### 7.1 既存 `master_log` の利用

既存 `master_log` は基本的にそのまま利用する。

複数切り替え時は、切り替え対象端末が複数行で以下のようになる。

```text
switch_flag = 1
action_selected_bs_id = 切り替え先 BS
```

`h_after_estimated` と `reward` は、サイクル全体の最終結果を各端末行へ同じ値で出力する。

```text
h_after_estimated = multi_greedy 探索後の最終推定 H
reward = h_after_estimated - h_before
```

### 7.2 注意点

`target_ue_flag` は現状 1 端末だけを示す設計に近いため、`multi_greedy` の複数対象端末の判定には使わない。  
複数切り替え対象の判定には `switch_flag` を使う。

## 8. 将来のハイブリッド化を見据えた設計

将来的に「混雑 AP から余裕端末を逃がす手法」と統合する場合、以下のように分離する。

### 8.1 候補生成

今回の `multi_greedy`:

```text
GenerateDirectRescueCandidates()
  -> 不満足端末のみを返す
```

将来の offload 手法:

```text
GenerateOffloadCandidates()
  -> 混雑 AP 上の余裕端末を返す
```

ハイブリッド:

```text
candidates = directRescueCandidates ∪ offloadCandidates
```

### 8.2 候補評価

候補評価は共通化する。

```text
候補端末 i を AP j へ移す
candidate assignment を作る
H_after を計算する
delta = H_after - hCurrent を計算する
```

この構造にしておけば、将来候補生成だけを増やしても、評価処理とログ処理を再利用できる。

## 9. 実験計画

まずは以下を比較する。

```text
method=greedy
setting.json で MaxSwitches=1 に設定し、method=multi_greedy で実行
setting.json で MaxSwitches=2 に設定し、method=multi_greedy で実行
setting.json で MaxSwitches=3 に設定し、method=multi_greedy で実行
setting.json で MaxSwitches=5 に設定し、method=multi_greedy で実行
method=no_switch
method=rulebase
```

確認する指標:

- 端末満足度の調和平均
- 不満足端末数
- 切り替え回数
- 平均 TP
- 平均 RTT
- 切り替え対象外端末の満足度悪化数
- seed 間のばらつき

特に確認する点:

```text
MaxSwitches を増やすと H が上がるか
MaxSwitches を増やしすぎると切り替え過多で悪化しないか
不満足端末数が減っても H が悪化するケースがないか
H が上がっても一部端末が大きく悪化していないか
```

## 10. テスト観点

- `method=greedy` の挙動が変わらないこと。
- `setting.json` で `MaxSwitches=1` に設定し、`method=multi_greedy` で実行した場合、最大 1 端末のみ切り替わること。
- `setting.json` で `MaxSwitches=3` に設定し、`method=multi_greedy` で実行した場合、1 サイクル最大 3 端末までしか切り替わらないこと。
- 候補端末が `satisfaction < 0.8` の端末だけであること。
- 同一端末が同一サイクル内で複数回選ばれないこと。
- 改善候補がなければ 0 件切り替えで終了すること。
- `switch_flag=1` の端末数が、実際の assignment 差分数と一致すること。
- `reward = h_after_estimated - h_before` になっていること。

## 11. 未実施事項

本書は設計書であり、コード実装はまだ行っていない。  
実装時は、既存 `greedy` を変更せず、必ず新規 method `multi_greedy` として追加する。
