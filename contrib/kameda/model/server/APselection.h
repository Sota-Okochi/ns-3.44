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
    constexpr double BROWSER_REQUIRED_TP = 8.0;      // Mbps（ブラウザ）
    constexpr double VIDEO_REQUIRED_TP = 5.0;       // Mbps（動画ストリーミング）
    constexpr double VOICE_CALL_REQUIRED_RTT = 100.0; // ms（通話アプリケーション）
    constexpr double ONLINE_GAME_REQUIRED_RTT = 50.0; // ms（オンラインゲーム）
    
    // 桁合わせ
    constexpr double MIN_SATISFACTION_THRESHOLD = 1e-6;
    constexpr double BPS_TO_MBPS = 1e-6;
    constexpr double HARMONIC_WEIGHT_BASE = 2000.0;
}

struct ApSelectionInput {
    int baseStations;
    int terminals;
    std::vector<int> capacities;
    std::vector<double> initialRtt;
    std::vector<int> useAppli;
    std::vector<int> initialAp;    // 各端末の初期接続先（1ベースのAP番号）
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
    
private:
    void cal_traffic_request(); // 必要TP, RTTの算出
    void cal_initial_harmonic_mean(); // 端末満足度の調和平均の計算
    double calculate_satisfaction(int terminal_idx, int ap_idx);
    void random_assignment(); //ランダム法による割り当て
    void ResetMonitorStats();
    void RecordHarmonicMean(double value);
    void PrintCycleHarmonicMeans();
    
    std::vector<double> m_monitor_rtt;   // 各基地局ごとの平均RTT
    std::vector<double> m_rtt_sum;       // 平均算出用の合計値
    std::vector<uint32_t> m_rtt_count;   // 平均算出用のサンプル数
    std::vector<bool> m_has_rtt;         // RTT取得済みフラグ
    std::vector<double> m_monitor_tp;    // 各基地局ごとの実測TP（bit/s）
    std::vector<bool> m_has_tp;          // TP取得済みフラグ
    std::vector<double> m_terminal_tp;   // 端末ごとの実測TP (bit/s)
    std::vector<bool> m_has_terminal_tp; // 端末ごとのTP取得済みフラグ
    std::vector<double> m_link_rtt;                   //接続時のRTTデータ

    int aps;                        //基地局数 
    int terms;                      //端末数
    std::vector<int> initial_app;       //各端末の初期アプリ番号
    std::vector<int> initial_AP;       //各端末の初期接続先
    std::vector<int> m_lastAssignment;  // 直近の割当結果（1ベース）
    uint32_t m_cycleIndex = 0;          // 現在のサイクル番号（1スタート）
    std::function<void(const std::vector<int>&)> m_handoverCallback;
    std::vector<double> m_cycleHarmonicMeans; // サイクルごとの調和平均
    
    std::vector<double> traffic_request;      //必要TP, RTT
    
};

}

#endif /* AP_SELECTION_H */
