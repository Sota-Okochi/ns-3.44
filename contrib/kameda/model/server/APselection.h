/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef AP_SELECTION_H
#define AP_SELECTION_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/traced-callback.h"

#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <random>
#include <functional>
#include <sstream>
#include <chrono>
#include <iostream>
#include <map>
#include <set>

namespace ns3 {

// === 定数定義 ===
namespace APConstants {
    // アプリケーション種別
    enum class AppType {
        BROWSER = 1,     // ブラウザ
        VIDEO = 2,       // 動画ストリーミング
        VOICE_CALL = 3,  // 通話アプリケーション
        ONLINE_GAME = 4  // オンラインゲーム
    };
    
    // アプリケーションのトラフィック要求（必要TP, RTT）
    constexpr double BROWSER_REQUIRED_TP = 3.0;      // Mbps（ブラウザ）
    constexpr double VIDEO_REQUIRED_TP = 8.0;       // Mbps（動画ストリーミング）, 720/60fps
    constexpr double VOICE_CALL_REQUIRED_RTT = 100.0; // ms（通話アプリケーション）
    constexpr double ONLINE_GAME_REQUIRED_RTT = 40.0; // ms（オンラインゲーム）
    
    // 桁合わせ
    constexpr double MIN_SATISFACTION_THRESHOLD = 1e-6;
    // TP未計測端末へのペナルティ値（要求の1%相当）。0.00 ログを防ぎ調和平均の破綻を回避する
    constexpr double SATISFACTION_FLOOR = 0.1;
    constexpr double BPS_TO_MBPS = 1e-6;
    constexpr double HARMONIC_WEIGHT_BASE = 2000.0;
    // ハンドオーバ直後の猶予満足度: TP未計測でも FLOOR を返さず「満足」扱いにする
    constexpr double GRACE_SATISFACTION = 1.0;
    // ハンドオーバ後に猶予を与えるサイクル数（デフォルト: 2サイクル）
    constexpr uint32_t HANDOVER_GRACE_CYCLES = 2;
    // 同一UEの短周期再切替を防ぐための cooldown サイクル数。
    // cycle t の切替は StartNewCycle(t+1) で記録され、t+1, t+2 では再候補から除外する。
    constexpr uint32_t HANDOVER_COOLDOWN_CYCLES = 2;
}

struct ApSelectionInput {
    int baseStations;
    int terminals;
    std::vector<int> capacities;
    std::vector<double> initialRtt;
    std::vector<int> useAppli;
    std::vector<int> initialAp;    // 各端末の初期接続先（1ベースのAP番号）
    uint32_t handoverGraceCycles = APConstants::HANDOVER_GRACE_CYCLES;
    std::string assignmentMethod = "random";
    std::string dqnActionCsvPath;
    std::string drlServerHost = "127.0.0.1";
    uint16_t drlServerPort = 50051;
    uint32_t drlTimeoutMs = 200;
    uint32_t maxSwitches = 1;
    std::string kScheduleType = "fixed";
    uint32_t kMin = 1;
    uint32_t kDecayRate = 1;
    double onlineDqnSafetyThreshold = 0.0;
    uint32_t centralizedDqnBootstrapCycles = 0;
    double rewardSwitchPenaltyAlpha = 0.001;
    double rewardDegradedPenaltyBeta = 0.001;
    double warmupBeforeCycleSec = 0.0;
    double cycleStartOffsetSec = 0.0;
    std::string outputDir = "OUTPUT/";
    uint32_t rngSeed = 1;
};

struct DqnAction
{
    uint32_t stepId = 0;
    int targetUeId = -1;    // 1-based
    int currentBsId = -1;   // 0-based
    int selectedBsId = -1;  // 0-based
    double advantage = 0.0;
    double qBs0 = 0.0;
    double qBs1 = 0.0;
    double qBs2 = 0.0;
    int candidateType = -1;
    int numUsersAp0 = 0;
    int numUsersAp1 = 0;
    int numUsersAp2 = 0;
    double monitorRttAp0 = 0.0;
    double monitorRttAp1 = 0.0;
    double monitorRttAp2 = 0.0;
    double estimatedSatisfactionIfAp0 = 0.0;
    double estimatedSatisfactionIfAp1 = 0.0;
    double estimatedSatisfactionIfAp2 = 0.0;
    double estimatedHDeltaIfAp0 = 0.0;
    double estimatedHDeltaIfAp1 = 0.0;
    double estimatedHDeltaIfAp2 = 0.0;
    double selectedEstimatedHDelta = 0.0;
    double hBeforeStepEstimated = 0.0;
    double hAfterStepEstimated = 0.0;
    double estimatedMarginalDelta = 0.0;
    double targetSatisfactionAfterEstimated = 0.0;
    double targetSatisfactionDeltaEstimated = 0.0;
    uint32_t effectiveMaxSwitches = 0;
    uint32_t appliedSwitchesInCycle = 0;
    uint32_t remainingSwitchBudget = 0;
    std::string kScheduleType = "fixed";
    uint32_t kMax = 0;
    uint32_t kMin = 1;
    uint32_t kDecayRate = 1;
    int stopActionFlag = 0;
    double qStop = 0.0;
};

class APselection : public Object{

public:
    APselection();
    virtual ~APselection();
    void init(const ApSelectionInput& input);
    void tmain();
    void setData(std::string senderIpAddress, std::string recvMessage);
    void StartNewCycle(uint32_t cycleIndex);
    void SetHandoverCallback(std::function<void(const std::vector<int>&)> cb);
    const std::vector<int>& GetLastAssignment() const { return m_lastAssignment; }
    void setTerminalTp(int termIdx, double tpBps);
    void PrintCycleHarmonicMeans();
    void SetTotalCycles(uint32_t n);

private:
    void cal_traffic_request(); // 必要TP, RTTの算出
    void cal_initial_harmonic_mean(); // 端末満足度の調和平均の計算
    double calculate_satisfaction(int terminal_idx, int ap_idx);
    void random_assignment(); //ランダム法による割り当て
    void all5g_assignment(); // 1回目の切り替えで全端末を5G(NR/AP1)へ割り当てる
    void rulebase_assignment(); // ルールベース法による割り当て
    void greedy_assignment(); // greedy法による割り当て
    void multi_greedy_assignment(); // 複数端末greedy法による割り当て
    void multi_offload_assignment(); // 混雑AP視点の複数端末offload法
    void logistic_assignment(); // ロジスティック回帰による割り当て
    void dqn_assignment(); // DQN action CSVによる割り当て
    void multi_dqn_assignment(); // Multi-DQN action CSVによる複数割り当て
    void online_dqn_assignment(); // Online DQN TCP JSON server による複数割り当て
    void centralized_dqn_assignment(); // Centralized DQN TCP JSON server による全体状態ベース割り当て
    std::string BuildDqnStateJson(int terminalIdx,
                                  int currentBsId,
                                  uint32_t stepId,
                                  double harmonicMean,
                                  int numUnsatisfiedUsers,
                                  int numUsersOnCurrentBs,
                                  const std::vector<int>& assignment,
                                  int candidateType,
                                  uint32_t effectiveMaxSwitches,
                                  uint32_t appliedSwitchesInCycle);
    bool SendStateReceiveAction(const std::string& requestJson,
                                int& selectedBsId,
                                std::vector<double>& qValues,
                                std::string& errorMessage) const;
    std::string BuildCentralizedDqnStateJson(const std::vector<int>& assignment,
                                             uint32_t stepId,
                                             double harmonicMean,
                                             uint32_t appliedSwitchesInCycle,
                                             const std::set<int>& bannedActionIds,
                                             std::vector<int>& validActionIds);
    bool SendCentralizedStateReceiveAction(const std::string& requestJson,
                                           int& actionId,
                                           int& targetUeId,
                                           int& selectedBsId,
                                           std::vector<double>& qValues,
                                           std::string& errorMessage) const;
    double calculate_harmonic_mean_for_assignment(const std::vector<int>& assignment);
    double estimate_satisfaction_for_assignment(int terminal_idx,
                                                int ap_idx,
                                                const std::vector<int>& assignment);
    double estimate_tp_mbps_for_assignment(int terminal_idx,
                                           int ap_idx,
                                           const std::vector<int>& assignment);
    double estimate_rtt_ms_for_assignment(int terminal_idx, int ap_idx) const;
    std::vector<int> count_users_per_ap(const std::vector<int>& assignment) const;
    void PrepareDecisionLogState(const std::vector<int>& assignmentBefore,
                                 const std::vector<int>& assignmentAfter,
                                 double hBefore,
                                 double hAfterEstimated);
    bool LoadLogisticModel();
    bool LoadDqnActions();
    void KeepCurrentAssignment(const std::string& reason);
    bool IsInHandoverCooldown(int terminalIdx) const;
    uint32_t GetEffectiveMaxSwitches(uint32_t cycleIndex) const;
    void ResetMonitorStats();
    void RecordHarmonicMean(double value);
    void WriteMasterLog();
    void WriteMeasuredRewardLogRow(double hAfterMeasured);
    void WriteCentralizedTeacherLogRows(const std::vector<int>& assignmentBefore,
                                        const std::vector<int>& assignmentAfter,
                                        double hBeforeCycleEstimated,
                                        double hAfterFinalEstimated);
    void WriteDecisionLogRow(const DqnAction& action,
                             int previousBsId,
                             bool applied,
                             const std::string& skipReason);
    void PrintMonitorRttReport() const;
    
    std::vector<double> m_monitor_rtt;   // 各基地局ごとの平均RTT
    std::vector<double> m_rtt_sum;       // 平均算出用の合計値
    std::vector<uint32_t> m_rtt_count;   // 平均算出用のサンプル数
    std::vector<bool> m_has_rtt;         // RTT取得済みフラグ
    std::vector<std::string> m_monitor_ip; // 各基地局の監視端末IP
    std::vector<double> m_terminal_tp;   // 端末ごとの実測TP (bit/s)
    std::vector<bool> m_has_terminal_tp; // 端末ごとのTP取得済みフラグ
    std::vector<double> m_link_rtt;                   //接続時のRTTデータ

    int aps;                        //基地局数 
    int terms;                      //端末数
    std::vector<int> initial_app;       //各端末の初期アプリ番号
    std::vector<int> initial_AP;       //各端末の初期接続先
    std::vector<int> m_lastAssignment;      // 直近の割当結果（1ベース）
    std::vector<int> m_assignmentBeforeAction; // 当該サイクルの割当実行前（1ベース）
    std::vector<int> m_assignmentAfterAction;  // 当該サイクルの割当実行後/選択結果（1ベース）
    std::vector<uint32_t> m_switchCycle;   // 端末ごとのハンドオーバ発生サイクル（0=未切り替え）
    uint32_t m_cycleIndex = 0;             // 現在のサイクル番号（1スタート）
    uint32_t m_totalCycles = 0;            // 総サイクル数（0=制限なし）
    uint32_t m_handoverGraceCycles = APConstants::HANDOVER_GRACE_CYCLES;
    uint32_t m_handoverCooldownCycles = APConstants::HANDOVER_COOLDOWN_CYCLES;
    uint32_t m_MaxSwitches = 1;       // multi_greedyで1サイクルに切り替える最大端末数
    std::string m_kScheduleType = "fixed";
    uint32_t m_kMin = 1;
    uint32_t m_kDecayRate = 1;
    std::function<void(const std::vector<int>&)> m_handoverCallback;
    std::vector<double> m_cycleHarmonicMeans; // サイクルごとの調和平均
    double m_hBeforeAction = 0.0;             // 当該サイクルの切り替え前H
    double m_hAfterEstimated = 0.0;           // 当該サイクルの推定切り替え後H
    double m_lastReward = 0.0;                // m_hAfterEstimated - m_hBeforeAction
    double m_previousMeasuredH = 0.0;          // 直前cycleで実測されたH
    bool m_hasPreviousMeasuredH = false;       // m_previousMeasuredH が有効か
    double m_lastMeasuredRewardFromPrevious = 0.0; // 現cycle実測H - 前cycle実測H
    uint32_t m_lastNumDegradedUsersMeasured = 0; // 前cycle行動に対する実測悪化端末数
    uint32_t m_lastMeasuredRewardSwitchCount = 0; // 前cycle行動の切替数
    double m_rewardSwitchPenaltyAlpha = 0.001;
    double m_rewardDegradedPenaltyBeta = 0.001;
    double m_warmupBeforeCycleSec = 0.0;
    double m_cycleStartOffsetSec = 0.0;
    bool m_pendingMeasuredReward = false;      // 次cycleで実測rewardを書ける行動があるか
    uint32_t m_pendingRewardCycleId = 0;       // reward対象の行動cycle
    double m_pendingRewardHBefore = 0.0;       // 行動前H
    double m_pendingRewardHAfterEstimated = 0.0; // 行動後推定H
    uint32_t m_pendingRewardSwitchCount = 0;   // 行動cycleでの切り替え台数
    std::vector<double> m_pendingRewardSatisfactionBefore; // 行動前の端末満足度
    
    std::vector<double> traffic_request;      //必要TP, RTT
    std::string m_assignmentMethod = "random"; // 割り当て手法名
    std::string m_dqnActionCsvPath;             // DQN action CSV
    std::string m_drlServerHost = "127.0.0.1";  // online_dqn TCP JSON server host
    uint16_t m_drlServerPort = 50051;            // online_dqn TCP JSON server port
    uint32_t m_drlTimeoutMs = 200;               // online_dqn socket timeout [ms]
    double m_onlineDqnSafetyThreshold = 0.0;     // selected action safety threshold for estimated H delta
    uint32_t m_centralizedDqnBootstrapCycles = 0; // centralized_dqn の初期 logistic bootstrap cycle 数
    std::string m_effectiveAssignmentMethod = "random"; // 実際に当該cycleで使った手法
    std::string m_pendingRewardEffectiveMethod = "random"; // 実測reward対象cycleの実効手法
    bool m_pendingRewardBootstrapCycle = false;  // 実測reward対象cycleがbootstrapか
    std::string m_outputDir = "OUTPUT/";        // master_log 出力先
    std::map<uint32_t, std::vector<DqnAction>> m_dqnActions; // cycle_id -> actions
    uint32_t m_rngSeed = 1;                    // 割り当て手法用乱数seed
    bool m_masterLogInitialized = false;       // master_log.csv ヘッダー書き込み済みフラグ
    bool m_decisionLogInitialized = false;     // decision_log.csv ヘッダー書き込み済みフラグ
    bool m_rewardLogInitialized = false;       // measured_reward_log.csv ヘッダー書き込み済みフラグ
    bool m_centralizedTeacherLogInitialized = false; // centralized teacher log ヘッダー書き込み済みフラグ
    std::string m_masterLogPath;               // 実行ごとのmaster_logファイルパス
    std::string m_decisionLogPath;             // 実行ごとのdecision_logファイルパス
    std::string m_rewardLogPath;               // 実行ごとの実測rewardログファイルパス
    std::string m_centralizedTeacherLogPath;    // centralized DQN BC 用 teacher action ログ
    std::string m_logisticModelPath;            // 学習済みロジスティック回帰モデル
    bool m_logisticModelLoaded = false;
    std::vector<int> m_logisticClasses;
    std::vector<double> m_logisticScalerMean;
    std::vector<double> m_logisticScalerScale;
    std::vector<std::vector<double>> m_logisticCoef;
    std::vector<double> m_logisticIntercept;

};

}

#endif /* AP_SELECTION_H */
