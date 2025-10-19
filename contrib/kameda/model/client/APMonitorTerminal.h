/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef AP_MONITOR_TERMINAL_H
#define AP_MONITOR_TERMINAL_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/ping.h"

namespace ns3 {

class ConnectManager;

/**
 * \brief AP専用監視端末クラス
 * 
 * 各APに1台配置され、連続的にRTT測定を行う専用端末
 * 従来の全端末測定方式と比較して大幅な効率化を実現
 */
class APMonitorTerminal : public Application {
public:
    APMonitorTerminal(uint32_t apId, Ipv4Address targetAP, Ipv4Address serverAddress);
    virtual ~APMonitorTerminal();

    /**
     * \brief 連続監視を開始
     */
    void StartContinuousMonitoring();

    /**
     * \brief 監視を停止
     */
    void StopMonitoring();

    /**
     * \brief RTTコールバック関数
     */
    void OnRttMeasured(Time rtt);
    void HandlePingRtt(uint16_t seq, Time rtt);

protected:
    virtual void StartApplication() override;
    virtual void StopApplication() override;

private:
    /**
     * \brief 定期的なping送信
     */
    void SendPeriodicPing();

    /**
     * \brief サーバーにRTTデータを送信
     */
    void ReportRTTToServer();

    /**
     * \brief TCPソケット作成
     */
    Ptr<Socket> CreateTcpSocket();

    /**
     * \brief TCP接続成功時の処理
     */
    void OnConnectionSucceeded(Ptr<Socket> socket);

    /**
     * \brief TCP接続失敗時の処理
     */
    void OnConnectionFailed(Ptr<Socket> socket);
    
    /**
     * \brief フォールバック：RTT測定失敗時にダミーデータを送信
     */
    void SendFallbackData();

    // メンバー変数
    uint32_t m_apId;                    // 監視対象AP ID
    Ipv4Address m_targetAP;             // 監視対象APのIPアドレス
    Ipv4Address m_serverAddress;        // サーバーのIPアドレス
    uint16_t m_serverPort;              // サーバーのポート番号
    
    double m_measureInterval;           // 測定間隔（秒）
    uint32_t m_samplesPerReport;        // レポート当たりのサンプル数
    
    std::vector<double> m_rttSamples;   // RTTサンプル蓄積
    EventId m_pingEvent;                // ping送信イベント
    EventId m_reportEvent;              // レポート送信イベント
    ApplicationContainer m_currentPingApp; // 現在のpingアプリケーション
    
    Ptr<Socket> m_socket;               // TCP通信用ソケット
    bool m_isMonitoring;                // 監視状態フラグ
    
    // 統計情報
    uint32_t m_totalPings;              // 総ping送信数
    uint32_t m_successfulPings;         // 成功したping数
    double m_averageRtt;                // 平均RTT
    double m_minRtt;                    // 最小RTT
    double m_maxRtt;                    // 最大RTT

    // 診断用（最初の1バーストだけAP直叩き）
    bool m_didDiag;                     // 最初のAP疎通診断を実施済みか
};

} // namespace ns3

#endif /* AP_MONITOR_TERMINAL_H */
