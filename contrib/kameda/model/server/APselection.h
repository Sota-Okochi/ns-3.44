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
    constexpr double BROWSER_REQUIRED_TP = 3.3;      // Mbps（ブラウザ）
    constexpr double VIDEO_REQUIRED_TP = 8.0;       // Mbps（動画ストリーミング）, 720/60fps
    constexpr double VOICE_CALL_REQUIRED_RTT = 60.0; // ms（通話アプリケーション）
    constexpr double ONLINE_GAME_REQUIRED_RTT = 20.0; // ms（オンラインゲーム）
    
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
}

struct ApSelectionInput {
    int baseStations;
    int terminals;
    std::vector<int> capacities;
    std::vector<double> initialRtt;
    std::vector<int> useAppli;
    std::vector<int> initialAp;    // 各端末の初期接続先（1ベースのAP番号）
    uint32_t handoverGraceCycles = APConstants::HANDOVER_GRACE_CYCLES;
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
    void policy_assignment1(); //方策による割り当て
    void ResetMonitorStats();
    void RecordHarmonicMean(double value);
    void WriteMasterLog();
    
    std::vector<double> m_monitor_rtt;   // 各基地局ごとの平均RTT
    std::vector<double> m_rtt_sum;       // 平均算出用の合計値
    std::vector<uint32_t> m_rtt_count;   // 平均算出用のサンプル数
    std::vector<bool> m_has_rtt;         // RTT取得済みフラグ
    std::vector<double> m_terminal_tp;   // 端末ごとの実測TP (bit/s)
    std::vector<bool> m_has_terminal_tp; // 端末ごとのTP取得済みフラグ
    std::vector<double> m_link_rtt;                   //接続時のRTTデータ

    int aps;                        //基地局数 
    int terms;                      //端末数
    std::vector<int> initial_app;       //各端末の初期アプリ番号
    std::vector<int> initial_AP;       //各端末の初期接続先
    std::vector<int> m_lastAssignment;      // 直近の割当結果（1ベース）
    std::vector<uint32_t> m_switchCycle;   // 端末ごとのハンドオーバ発生サイクル（0=未切り替え）
    uint32_t m_cycleIndex = 0;             // 現在のサイクル番号（1スタート）
    uint32_t m_totalCycles = 0;            // 総サイクル数（0=制限なし）
    uint32_t m_handoverGraceCycles = APConstants::HANDOVER_GRACE_CYCLES;
    std::function<void(const std::vector<int>&)> m_handoverCallback;
    std::vector<double> m_cycleHarmonicMeans; // サイクルごとの調和平均
    
    std::vector<double> traffic_request;      //必要TP, RTT
    bool m_masterLogInitialized = false;       // master_log.csv ヘッダー書き込み済みフラグ
    std::string m_masterLogPath;               // 実行ごとのmaster_logファイルパス

};

}

#endif /* AP_SELECTION_H */
