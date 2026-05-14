# DRL設計仕様書 — 端末満足度調和平均最大化 AP 選択

**バージョン**: 0.1  
**作成日**: 2026-05-06  
**対象ブランチ**: main

---

## 1. 背景と目的

### 目的

端末ごとの QoE（Quality of Experience）を最大化する AP 選択を深層強化学習（DRL）で実現する。  
最適化指標は **端末満足度の調和平均 H** とする。

```
H = N / Σ(1 / S_i)
```

- `N` : 端末総数
- `S_i` : 端末 `i` の満足度（TP重視，RTT重視）

### 現状の課題

| 手法 | 概要 | 課題 |
|---|---|---|
| `random_assignment` | 全端末をランダムに再割当 | H が悪化することも多い |
| `policy_assignment1` | 不満足端末 (S<0.8) のみランダムに再割当 | 局所改善に留まる |

DRL により「全体的な H を向上させる割当」を学習することが本仕様の目標である。

---

## 2. 現行システム構成

### クラス・ファイル対応

| ファイル | クラス | 役割 |
|---|---|---|
| `master/NetSim.h / *.cc` | `NetSim` | シミュレーション全体管理、トポロジ、ハンドオーバ実行 |
| `contrib/kameda/.../KamedaAppServer.cc` | `KamedaAppServer` | サイクル制御、RTT 受信、APselection 呼び出し |
| `contrib/kameda/.../APselection.cc` | `APselection` | 端末満足度計算、割当アルゴリズム、master_log 出力 |
| `data/setting.json` | — | 実験パラメータ（端末数、サイクル数、タイミング） |

### 1 サイクルの現行フロー

```
[サイクル開始]
  │
  ├─ モニター端末が各 AP に ping → RTT 収集
  │   APselection::setData() でサンプル蓄積
  │
  ├─ FlowMonitor による端末別 TP 計測ウィンドウ
  │   NetSim::CollectTerminalThroughput()
  │   → KamedaAppServer::SetTerminalTp() → APselection::setTerminalTp()
  │
  ├─ [サイクル終了時] KamedaAppServer::ScheduleCycleEnd()
  │   → APselection::tmain()
  │       cal_traffic_request()           // アプリ別要求値を設定
  │       cal_initial_harmonic_mean()     // 割当前 H を計算
  │       WriteMasterLog()                // CSV 出力
  │       random_assignment() / policy_assignment1()  // 割当決定
  │
  └─ HandoverCallback → NetSim::HandoverRequest()
      → 物理的な AP 切り替え（ルーティング変更など）
```

### master_log.csv 現在のカラム

```
サイクル, 端末, AP, アプリ, ネットワーク指標, 通信品質, 端末満足度, 計測有効
```

---

## 3. DRL 導入後の全体アーキテクチャ

### 概要図

```
┌─────────────────────────────────────────────────────┐
│  ns-3 プロセス (C++)                                  │
│                                                       │
│  [APselection::tmain()]                               │
│    │  state (JSON)                                    │
│    ▼                                                  │
│  IPC クライアント ──────────── TCP ──────────────────┼──► DRL Python プロセス
│    ▲                                                  │     │  DRLAgent.act(state)
│    │  action (JSON)                                   │     │  → action
│    └──────────────────────────── TCP ◄───────────────┼─────┘
│                                                       │
│  HandoverCallback → NetSim::HandoverRequest()         │
└─────────────────────────────────────────────────────┘
        │ master_log.csv (拡張版)
        ▼
   Python 学習・評価スクリプト (offline / online)
```

### IPC 方式: TCP ソケット（同一ホスト）

- ns-3 側が **クライアント**、Python 側が **サーバー**
- ポート: `50051`（変更可、setting.json で管理）
- プロトコル: 1リクエスト＋1レスポンスの同期通信（1サイクル1往復）
- メッセージ形式: JSON（改行区切り）
- **推論モード**: Python サーバーが稼働していれば DRL 推論
- **フォールバック**: Python サーバー未起動 or タイムアウト (100ms) → `policy_assignment1` にフォールバック

> `G_nth` 値で切り替える既存パターンに合わせ、`G_nth == 6` を DRL モードとする。

---

## 4. 状態空間 state

### 設計方針

- サイクルごとに 1 つの state を構成する
- 各端末の個別特徴量 + 基地局側の集計量 + 全体量を含める
- 正規化は Python 側で行う（C++ はそのまま数値を送る）

### state の JSON 構造

```json
{
  "cycle": 3,
  "num_terms": 10,
  "num_aps": 3,
  "terms": [
    {
      "term_id": 1,
      "app_type": 2,
      "current_ap": 1,
      "satisfaction": 0.85,
      "tp_mbps": 6.8,
      "rtt_ms": 0.0,
      "data_valid": 1
    }
  ],
  "aps": [
    {
      "ap_id": 0,
      "monitor_rtt_ms": 14.2,
      "num_connected": 4,
      "avg_satisfaction": 0.91
    }
  ],
  "global": {
    "harmonic_mean": 0.78,
    "num_unsatisfied": 3,
    "prev_harmonic_mean": 0.72
  }
}
```

### フラット化ベクトル（DQN / PPO 入力用）

端末数 N、AP 数 A のとき、状態ベクトルの次元数:

```
dim(s) = N × 6  +  A × 3  +  3
       = 10×6   +  3×3    +  3   (デフォルト設定で = 72)
```

| グループ | 要素 | 説明 |
|---|---|---|
| 端末 (×N) | app_type | 1-hot または整数値 |
| | current_ap | 0-based 整数 |
| | satisfaction | 実数 [0, ∞) |
| | tp_mbps | 実数（TP 系のみ有効）|
| | rtt_ms | 実数（RTT 系のみ有効）|
| | data_valid | 0 or 1 |
| 基地局 (×A) | monitor_rtt_ms | 実数 |
| | num_connected | 整数 |
| | avg_satisfaction | 実数 |
| 全体 | harmonic_mean | 実数 |
| | num_unsatisfied | 整数 |
| | prev_harmonic_mean | 実数 |

---

## 5. 行動空間 action

### 設計方針

DRL の action は「各端末の割当先 AP（1-based）」を決める。  
出力が大きくなりすぎる場合は端末単位分割も検討するが、初期実装は全端末一括とする。

### action の JSON 構造（C++ → Python への応答）

```json
{
  "assignment": [1, 2, 1, 3, 2, 1, 1, 3, 2, 1]
}
```

- 配列長 = 端末数 N
- 値域 = [1, A]（1-based、APselection の既存規則に合わせる）

### 行動空間の大きさ

| 設計 | 大きさ | 備考 |
|---|---|---|
| 全端末一括（離散） | A^N = 3^10 ≒ 59,000 | DQN で扱うには大きすぎる |
| 1 端末ずつ逐次決定 | N × A = 30 | PPO の連続・離散混合で実装可能 |
| 不満足端末のみ再割当 | 可変 | policy_assignment1 を踏襲、小さい |

**初期実装推奨**: 「不満足端末のみ対象」として行動空間を A^K（K = 不満足端末数）に制限する。K が大きい場合は PPO + Multi-Discrete を使用する。

---

## 6. 報酬関数 reward

### 基本報酬

```
reward = H_after - H_before
```

- `H_before` : tmain() 冒頭で計算した割当前調和平均
- `H_after` : HandoverRequest 後の次サイクル冒頭 H（1サイクル遅延あり）

> H_after は次サイクルの `cal_initial_harmonic_mean()` 結果を用いる。  
> C++ 側では H_before / H_after をともに master_log に記録する。

### ペナルティ項（調整用）

```
reward -= α × switch_count          // 切り替え過多ペナルティ
reward -= β × num_degraded          // 満足度が悪化した端末数
reward -= γ × large_drop_penalty    // 大幅悪化ペナルティ（例: ΔS < -0.3 の端末数）
```

デフォルト係数: `α = 0.01, β = 0.02, γ = 0.05`（実験で調整）

### 報酬の計算タイミング

```
Cycle t 終了 → action_t を実行 → Cycle t+1 開始 → H_{t+1} 計算 → reward_t = H_{t+1} - H_t
```

最終サイクルの reward は当サイクルの H のみで代替する（H_after = H_before とする）。

---

## 7. remote_host 連携方式（IPC 詳細）

### シーケンス（1サイクル）

```
ns-3 (C++ IPC クライアント)           Python DRL サーバー
          │                                    │
          │ ── connect (初回のみ) ──────────► │
          │                                    │
          │ ── state JSON + "\n" ────────────► │
          │                                    │  DRLAgent.act(state)
          │ ◄─── action JSON + "\n" ─────────  │
          │                                    │
          │  HandoverRequest(assignment)        │
          │  reward 計算（次サイクル冒頭）      │
          │ ── reward JSON + "\n" ───────────► │  experience 蓄積
          │                                    │  （online の場合）学習ステップ
```

### C++ 側実装箇所

`APselection.cc` に以下を追加する：

```cpp
// G_nth == 6 の場合のみ呼ばれる
std::vector<int> APselection::drl_assignment() {
    std::string stateJson = BuildStateJson();     // state を構築
    std::string actionJson = SendAndReceive(stateJson); // IPC 送受信
    return ParseAssignment(actionJson);           // action を変換
}
```

`KamedaAppServer.cc` に reward 送信処理を追加する：

```cpp
// 次サイクル冒頭の cal_initial_harmonic_mean() 後に呼ぶ
void KamedaAppServer::SendReward(double hBefore, double hAfter, int switchCount) {
    // reward JSON を組み立てて IPC で送信
}
```

### Python 側実装箇所（新規）

```
rl/
├── server.py          # TCP サーバー（state 受信、action 応答、reward 受信）
├── agent.py           # DRLAgent クラス（DQN/PPO）
├── env_wrapper.py     # ns-3 通信を Gymnasium Env に見せるラッパー
├── train.py           # オフライン / オンライン学習エントリーポイント
└── evaluate.py        # 評価スクリプト
```

### setting.json 追加項目

```json
{
  "drlMode": false,
  "drlServerHost": "127.0.0.1",
  "drlServerPort": 50051,
  "drlTimeoutMs": 100
}
```

---

## 8. サイクルごとの処理シーケンス（DRL モード）

```
[Cycle t 開始]
    │
    ├─ モニター端末 RTT 収集（setData）
    ├─ FlowMonitor TP 収集（setTerminalTp）
    │
[Cycle t 終了: tmain() 呼び出し]
    │
    ├─ cal_traffic_request()
    ├─ cal_initial_harmonic_mean()   → H_before
    ├─ WriteMasterLog()
    │
    ├─ [G_nth == 6: DRL モード]
    │     BuildStateJson()
    │     IPC 送信 → Python サーバー
    │     IPC 受信 ← action (assignment)
    │     fallback: policy_assignment1() (タイムアウト時)
    │
    ├─ HandoverCallback(assignment) → NetSim::HandoverRequest()
    │
[Cycle t+1 開始]
    │
    ├─ cal_initial_harmonic_mean()   → H_after
    ├─ reward = H_after - H_before - penalties
    ├─ IPC 送信 → reward JSON
    │
    └─ 繰り返し
```

---

## 9. ログ・データセット仕様

### master_log.csv（拡張版）

既存カラムを維持しつつ DRL 用カラムを追加する。

```
サイクル, 端末, AP, アプリ, ネットワーク指標, 通信品質, 端末満足度, 計測有効,
prev_ap, h_before, h_after, reward, switch_flag, action_source
```

| 追加カラム | 型 | 説明 |
|---|---|---|
| `prev_ap` | int | 切り替え前の AP（1-based） |
| `h_before` | float | サイクル開始時調和平均 |
| `h_after` | float | サイクル終了後調和平均（次サイクル冒頭） |
| `reward` | float | 当サイクルの報酬値 |
| `switch_flag` | 0/1 | このサイクルで切り替え発生 |
| `action_source` | str | `"drl"` / `"policy"` / `"random"` / `"fallback"` |

### episode_log.csv（新規・サイクル集計）

学習・評価の集計用。1行=1サイクル。

```
run_id, seed, nth, cycle, sim_time,
h_before, h_after, reward, switch_count,
num_unsatisfied_before, num_unsatisfied_after,
num_degraded, action_source
```

### ファイル命名規則

```
OUTPUT/master_log_{N}_{YYYYMMDD_HHMMSS}.csv
OUTPUT/episode_log_{N}_{YYYYMMDD_HHMMSS}.csv
```

### DRL 学習データ（オフライン用）

episode_log から Parquet 形式に変換して保存する。

```
data/processed/drl_dataset_{YYYYMMDD}.parquet
```

---

## 10. 学習方式と推論方式

### フェーズ 1: オフライン学習

1. `random_assignment` / `policy_assignment1` で多数のエピソードを収集  
2. master_log / episode_log を `rl/train.py` に入力  
3. DQN または PPO で行動クローニング or 報酬ベースの offline RL  
4. 学習済みモデルを `rl/models/` に保存

### フェーズ 2: オンライン推論（本番モード）

1. `python rl/server.py --model rl/models/best.pt` で Python サーバー起動  
2. `./ns3 run master/main -- --nth=6` で ns-3 を DRL モードで実行  
3. 各サイクルの state/action/reward を IPC 経由でやりとり  
4. オプション: replay buffer でオンライン学習（`--online` フラグ）

### アルゴリズム選定方針

| 段階 | アルゴリズム | 理由 |
|---|---|---|
| 初期実装 | DQN（不満足端末対象・行動圧縮） | 実装シンプル、デバッグ容易 |
| 比較 | PPO + Multi-Discrete | 端末数が多い場合に対応 |
| 発展 | Double DQN / Dueling DQN | Q値過大評価の抑制 |
| 将来 | Multi-Agent RL | 基地局単位の分散制御 |

---

## 11. 評価指標

### 必須指標

| 指標 | 集計単位 | 備考 |
|---|---|---|
| 端末満足度の調和平均 H | サイクル / エピソード | 主指標 |
| 不満足端末数 (S < 0.8) | サイクル | H との乖離ケースを確認 |
| 平均 TP | サイクル | TP 系アプリ対象 |
| 平均 RTT | サイクル | RTT 系アプリ対象 |
| 切り替え回数 | サイクル | ペナルティ設計の参考 |
| 満足度悪化端末数 | サイクル | 切り替え対象外端末の影響確認 |
| 累積 reward | エピソード | 学習進捗 |

### 比較対象（baseline）

1. 切り替えなし（初期割当維持）
2. `random_assignment`
3. `policy_assignment1`（不満足端末のみランダム）
4. DRL（本手法）

### 評価時の注意

- 最低 5 seed で平均・標準偏差を報告する
- 不満足端末数が減っても H が悪化するケースを必ず確認する
- 切り替え回数の増加と H の改善のトレードオフを分析する

---

## 12. 実装ステップ

### Step 1: ログ拡張（C++ side）
- `APselection::WriteMasterLog()` に `prev_ap, h_before, h_after, reward, switch_flag, action_source` を追加
- `episode_log.csv` 出力処理を追加

### Step 2: IPC スタブ（C++ side）
- `APselection::drl_assignment()` を実装（まず固定 action を返すダミー）
- `KamedaAppServer` に reward 送信処理を追加
- `setting.json` に `drlMode, drlServerPort` を追加

### Step 3: Python サーバー基盤
- `rl/server.py`: TCP サーバー、state 受信、action 応答
- `rl/agent.py`: ランダム行動エージェント（動作確認用）

### Step 4: 動作確認
- ns-3 + Python サーバーで 1 エピソード通信確認
- master_log / episode_log のカラム検証

### Step 5: DQN 実装
- `rl/agent.py` に DQN を実装（行動空間: 不満足端末 × AP 数）
- `rl/train.py` にオフライン学習フロー実装

### Step 6: 評価・比較
- baseline 3 手法と DRL を同一 seed で比較
- `rl/evaluate.py` で指標集計・グラフ生成

---

## 13. リスクと制約

| リスク | 内容 | 対策 |
|---|---|---|
| IPC レイテンシ | TCP 往復がサイクル時間 (7s) に影響しない | タイムアウト (100ms) + fallback で保護 |
| 行動空間爆発 | 端末数増加で A^N が扱えない | 不満足端末のみ対象に制限、PPO に切替 |
| H_after の遅延 | reward は 1 サイクル遅延して確定 | Markovian ではないが許容範囲内 |
| 再現性 | Python 側の seed 管理が必要 | server.py 起動時に --seed を必須引数とする |
| ns-3 バージョン固定 | 外部ライブラリ追加は禁止 | IPC は標準ソケットのみ使用 |
| 状態次元の変化 | 端末数変更で state ベクトルが変わる | JSON 形式で送受信し Python 側でパースする |

## 付録 A: state JSON 送受信プロトコル詳細

```
<state_json>\n    ← ns-3 → Python (改行終端)
<action_json>\n   ← Python → ns-3 (改行終端)
<reward_json>\n   ← ns-3 → Python (次サイクル冒頭)
```

reward JSON 構造:
```json
{
  "cycle": 3,
  "h_before": 0.78,
  "h_after": 0.83,
  "reward": 0.05,
  "switch_count": 2,
  "num_degraded": 1,
  "done": false
}
```
