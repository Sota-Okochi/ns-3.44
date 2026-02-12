/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "APMonitorTerminal.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/inet-socket-address.h"
#include "ns3/config.h"
#include "ns3/ping-helper.h"
#include "ns3/flow-monitor-helper.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <limits>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("APMonitorTerminal");

APMonitorTerminal::APMonitorTerminal(uint32_t apId, Ipv4Address targetAP, Ipv4Address serverAddress)
    : m_apId(apId),
      m_targetAP(targetAP),
      m_serverAddress(serverAddress),
      m_serverPort(8080),
      m_measureInterval(0.5),
      m_samplesPerReport(10),
      m_pingInterval(0.2),
      m_pingPayloadSize(1200),        // Pingペイロードサイズ（バイト）
      m_socket(nullptr),
      m_isMonitoring(false),
      m_appStartTime(Seconds(0.0)),
      m_totalPings(0),
      m_successfulPings(0),
      m_averageRtt(0.0),
      m_minRtt(std::numeric_limits<double>::max()),
      m_maxRtt(0.0),
      m_totalRxBytes(0),
      m_measurementStartTime(Seconds(0.0)),
      m_lastRxTime(Seconds(0.0)),
      m_hasVideoTraffic(false),
      m_lastGoodputBps(0.0),
      m_hasGoodput(false),
      m_lastThroughputBps(0.0),
      m_hasThroughput(false)
{
    NS_LOG_FUNCTION(this);
    std::cout << "=== APMonitorTerminal created for AP" << m_apId << " ===" << std::endl;
}

APMonitorTerminal::~APMonitorTerminal()
{
    NS_LOG_FUNCTION(this);
}

void APMonitorTerminal::StartApplication()
{
    NS_LOG_FUNCTION(this);
    std::cout << "=== APMonitorTerminal::StartApplication() - AP" << m_apId << " ===" << std::endl;
    m_appStartTime = Simulator::Now();
    ResetGoodputStats();

    // FlowMonitor をインストール（初回のみ）
    if (m_flowMonitor == nullptr)
    {
        m_flowMonitor = m_flowHelper.Install(GetNode());
        Ptr<FlowClassifier> classifier = m_flowHelper.GetClassifier();
        m_flowClassifier = DynamicCast<Ipv4FlowClassifier>(classifier);
    }
    else
    {
        m_flowMonitor->ResetAllStats();
    }
    
    Simulator::Schedule(Seconds(0.0), &APMonitorTerminal::StartContinuousMonitoring, this);
}

void APMonitorTerminal::StopApplication()
{
    NS_LOG_FUNCTION(this);
    StopMonitoring();
}

void APMonitorTerminal::StartContinuousMonitoring()
{
    NS_LOG_FUNCTION(this);
    
    if (m_isMonitoring) {
        return; // 既に監視中
    }
    
    if (m_closeEvent.IsPending())
    {
        Simulator::Cancel(m_closeEvent);
    }

    ResetGoodputStats();

    m_isMonitoring = true;
    std::cout << "=== Starting continuous monitoring for AP" << m_apId << " ===" << std::endl;
    
    // 統計情報をリセット
    m_totalPings = 0;
    m_successfulPings = 0;
    m_rttSamples.clear();
    m_minRtt = std::numeric_limits<double>::max();
    m_maxRtt = 0.0;
    
    // 単回測定を即時開始
    ScheduleNextMeasurement(Seconds(0.0));
}

void APMonitorTerminal::StopMonitoring()
{
    NS_LOG_FUNCTION(this);
    
    if (!m_isMonitoring)
    {
        return;
    }

    m_isMonitoring = false;
    
    // イベントをキャンセル
    if (m_pingEvent.IsPending()) {
        Simulator::Cancel(m_pingEvent);
    }
    if (m_reportEvent.IsPending()) {
        Simulator::Cancel(m_reportEvent);
    }
    if (m_measurementTimeoutEvent.IsPending()) {
        Simulator::Cancel(m_measurementTimeoutEvent);
    }
    if (!m_closeEvent.IsPending())
    {
        m_closeEvent = Simulator::Schedule(Seconds(0.1),
                                           &APMonitorTerminal::FinalizeTransmission,
                                           this);
    }
}

void APMonitorTerminal::SendPeriodicPing()
{
    NS_LOG_FUNCTION(this);
    
    if (!m_isMonitoring) {
        return;
    }
    m_pingEvent = EventId();
    
    // 前のpingアプリがあれば停止
    if (m_currentPingApp.GetN() > 0) {
        m_currentPingApp.Stop(Seconds(Simulator::Now().GetSeconds()));
    }
    
    Ipv4Address dst = m_targetAP;

    // V4PingHelperを使用してping送信
    PingHelper ping(dst);
    double intervalSeconds = (m_pingInterval > 0.0) ? m_pingInterval : 0.02;
    Time pingInterval = Seconds(intervalSeconds);
    ping.SetAttribute("Interval", TimeValue(pingInterval));
    ping.SetAttribute("Size", UintegerValue(m_pingPayloadSize));
    ping.SetAttribute("Count", UintegerValue(m_samplesPerReport));

    double durationSeconds = intervalSeconds * static_cast<double>(m_samplesPerReport);
    if (durationSeconds <= 0.0)
    {
        durationSeconds = intervalSeconds;
    }
    Time measurementDuration = Seconds(durationSeconds);
    ping.SetAttribute("StopTime", TimeValue(measurementDuration));
    
    m_currentPingApp = ping.Install(GetNode());
    
    // ping実行時間を設定（十分な時間を確保）
    m_currentPingApp.Start(Seconds(0.0));
    
    // RTTコールバックを設定（PingアプリケーションのRttトレースに接続）
    if (m_currentPingApp.GetN() > 0)
    {
        Ptr<Application> app = m_currentPingApp.Get(0);
        Ptr<Ping> pingApp = DynamicCast<Ping>(app);
        if (pingApp)
        {
            pingApp->TraceConnectWithoutContext("Rtt",
                                               MakeCallback(&APMonitorTerminal::HandlePingRtt, this));
        }
    }
    
    m_totalPings++;
    
    std::cout << "MONITOR_AP" << m_apId << " ping sent to " << dst 
              << " at time " << Simulator::Now().GetSeconds() << "s (Total: " << m_totalPings << ")" << std::endl;

    if (m_measurementTimeoutEvent.IsPending())
    {
        Simulator::Cancel(m_measurementTimeoutEvent);
    }
    m_measurementTimeoutEvent = Simulator::Schedule(measurementDuration + MilliSeconds(50),
                                                   &APMonitorTerminal::HandleMeasurementTimeout,
                                                   this);
}

void APMonitorTerminal::HandlePingRtt(uint16_t /*seq*/, Time rtt)
{
    OnRttMeasured(rtt);
}

void APMonitorTerminal::NotifyVideoRx(uint32_t rxBytes, Time rxTime)
{
    if (rxTime < Seconds(1.0))
    {
        return;
    }

    if (!m_hasVideoTraffic)
    {
        m_hasVideoTraffic = true;
        m_measurementStartTime = rxTime;
    }

    m_totalRxBytes += rxBytes;
    m_lastRxTime = rxTime;
}

void APMonitorTerminal::HandleVideoSinkRx(Ptr<const Packet> packet, const Address& /*from*/)
{
    if (packet == nullptr)
    {
        return;
    }
    NotifyVideoRx(packet->GetSize(), Simulator::Now());
}

void APMonitorTerminal::OnRttMeasured(Time rtt)
{
    NS_LOG_FUNCTION(this);
    
    double rttMs = rtt.GetMilliSeconds();
    
    // RTT=0msの場合の処理（オプション：フィルタリング）
    if (rttMs <= 0.0) {
        std::cout << "AP" << m_apId << " RTT: " << rttMs << "ms (Sample " 
                  << m_rttSamples.size() + 1 << "/" << m_samplesPerReport 
                  << ") - Zero RTT detected" << std::endl;
        // 0ms値も統計に含める（現実的な測定結果として）
    } else {
        std::cout << "AP" << m_apId << " RTT: " << rttMs << "ms (Sample " 
                  << m_rttSamples.size() + 1 << "/" << m_samplesPerReport << ")" << std::endl;
    }
    
    m_rttSamples.push_back(rttMs);
    m_successfulPings++;
    
    // 統計情報を更新
    m_minRtt = std::min(m_minRtt, rttMs);
    m_maxRtt = std::max(m_maxRtt, rttMs);
    
    // 指定サンプル数に達したらサーバーに報告
    if (m_rttSamples.size() >= m_samplesPerReport) {
        ReportRTTToServer();
    }
}

void APMonitorTerminal::ReportRTTToServer()
{
    NS_LOG_FUNCTION(this);
    if (m_measurementTimeoutEvent.IsPending())
    {
        Simulator::Cancel(m_measurementTimeoutEvent);
    }
    m_measurementTimeoutEvent = EventId();

    if (m_rttSamples.empty()) {
        return;
    }
    
    // 平均RTTを計算
    double sum = std::accumulate(m_rttSamples.begin(), m_rttSamples.end(), 0.0);
    double baseAverage = sum / m_rttSamples.size();

    const uint32_t windowSize = 6;
    double movingAverage = baseAverage;
    if (m_rttSamples.size() >= windowSize)
    {
        double windowSum = 0.0;
        for (size_t i = m_rttSamples.size() - windowSize; i < m_rttSamples.size(); ++i)
        {
            windowSum += m_rttSamples[i];
        }
        movingAverage = windowSum / static_cast<double>(windowSize);
    }
    m_averageRtt = movingAverage;
    
    std::cout << "=== AP" << m_apId << " Reporting to Server ===" << std::endl;
    std::cout << "Samples: " << m_rttSamples.size()
              << ", Average RTT (base): " << baseAverage
              << "ms, Moving Average (last " << windowSize << "): "
              << m_averageRtt << "ms" << std::endl;

    // FlowMonitor で計測したスループット（IP 層バイト）
    double throughputBps = ComputeThroughputBps();
    m_lastThroughputBps = throughputBps;
    m_hasThroughput = (throughputBps > 0.0);
    std::cout << "FlowMonitor throughput (bps): " << throughputBps << std::endl;

    // 参考用に従来の Goodput も計算（アプリ層バイト）
    double goodputBps = 0.0;
    if (m_hasVideoTraffic && m_lastRxTime > m_measurementStartTime)
    {
        double duration = (m_lastRxTime - m_measurementStartTime).GetSeconds();
        if (duration > 0.0)
        {
            goodputBps = static_cast<double>(m_totalRxBytes) * 8.0 / duration;
            SetLastGoodputBps(goodputBps);
        }
    }
    std::cout << "Video goodput (bps): " << goodputBps << std::endl;
    
    // TCP接続を作成してサーバーに送信
    m_socket = CreateTcpSocket();
    if (m_socket) {
        InetSocketAddress serverAddr(m_serverAddress, m_serverPort);
        m_socket->Connect(serverAddr);
        
        m_socket->SetConnectCallback(
            MakeCallback(&APMonitorTerminal::OnConnectionSucceeded, this),
            MakeCallback(&APMonitorTerminal::OnConnectionFailed, this));
    }

    Time nextDelay = Seconds(m_measureInterval > 0.0 ? m_measureInterval : 0.5);
    ScheduleNextMeasurement(nextDelay);
}

void APMonitorTerminal::ScheduleNextMeasurement(Time delay)
{
    if (!m_isMonitoring)
    {
        return;
    }
    if (m_pingEvent.IsPending())
    {
        Simulator::Cancel(m_pingEvent);
    }
    m_pingEvent = Simulator::Schedule(delay, &APMonitorTerminal::SendPeriodicPing, this);
}

void APMonitorTerminal::HandleMeasurementTimeout()
{
    m_measurementTimeoutEvent = EventId();
    if (!m_isMonitoring)
    {
        return;
    }

    if (!m_rttSamples.empty())
    {
        ReportRTTToServer();
        return;
    }

    std::cout << "AP" << m_apId << " measurement window expired with no RTT samples - retrying" << std::endl;
    Time retryDelay = Seconds(m_measureInterval > 0.0 ? m_measureInterval : 0.5);
    ScheduleNextMeasurement(retryDelay);
}

Ptr<Socket> APMonitorTerminal::CreateTcpSocket()
{
    return Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
}

void APMonitorTerminal::OnConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this);
    
    // メッセージを作成（監視端末専用フォーマット）
    std::stringstream message;
    double reportBps = 0.0;
    if (m_hasThroughput)
    {
        reportBps = m_lastThroughputBps;
    }
    else if (m_hasGoodput)
    {
        reportBps = m_lastGoodputBps;
    }

    message << "MONITOR_AP" << m_apId << "," << m_averageRtt << "," << reportBps;
    
    std::string msg = message.str();
    socket->Send(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.length(), 0);
    socket->ShutdownSend();
    socket->Close();
    m_socket = nullptr;
    
    std::cout << "=== Monitor data sent to server: " << msg << " ===" << std::endl;
    
    // サンプルをクリア
    m_rttSamples.clear();
}

void APMonitorTerminal::OnConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this);
    std::cout << "=== Monitor AP" << m_apId << ": Connection to server failed ===" << std::endl;
    
    // リトライのため、少し待ってから再送信
    if (m_isMonitoring) {
        m_reportEvent = Simulator::Schedule(Seconds(2.0), &APMonitorTerminal::ReportRTTToServer, this);
    }
}

void APMonitorTerminal::SendFallbackData()
{
    NS_LOG_FUNCTION(this);
    
    // 単回測定に合わせたフォールバックは無効化
}

void APMonitorTerminal::FinalizeTransmission()
{
    NS_LOG_FUNCTION(this);

    m_closeEvent = EventId();

    if (m_socket)
    {
        m_socket = nullptr;
    }

    // cancel events, ping app stop
    if (m_pingEvent.IsPending())
    {
        Simulator::Cancel(m_pingEvent);
    }
    if (m_reportEvent.IsPending())
    {
        Simulator::Cancel(m_reportEvent);
    }

    if (m_currentPingApp.GetN() > 0)
    {
        m_currentPingApp.Stop(Seconds(Simulator::Now().GetSeconds()));
    }

    std::cout << "=== Monitoring stopped for AP" << m_apId << " ===" << std::endl;
}

void APMonitorTerminal::ResetGoodputStats()
{
    m_totalRxBytes = 0;
    m_measurementStartTime = Seconds(0.0);
    m_lastRxTime = Seconds(0.0);
    m_hasVideoTraffic = false;
    m_lastGoodputBps = 0.0;
    m_hasGoodput = false;
    m_lastThroughputBps = 0.0;
    m_hasThroughput = false;
    if (m_flowMonitor)
    {
        m_flowMonitor->ResetAllStats();
    }
}

double APMonitorTerminal::GetLastGoodputBps() const
{
    return m_lastGoodputBps;
}

bool APMonitorTerminal::HasGoodput() const
{
    return m_hasGoodput;
}

Time APMonitorTerminal::GetMeasurementStartTime() const
{
    return m_measurementStartTime;
}

Time APMonitorTerminal::GetLastRxTime() const
{
    return m_lastRxTime;
}

uint64_t APMonitorTerminal::GetTotalRxBytes() const
{
    return m_totalRxBytes;
}

Time APMonitorTerminal::GetApplicationStartTime() const
{
    return m_appStartTime;
}

void APMonitorTerminal::SetLastGoodputBps(double goodputBps)
{
    m_lastGoodputBps = goodputBps;
    m_hasGoodput = true;
}

bool APMonitorTerminal::HasVideoTraffic() const
{
    return m_hasVideoTraffic;
}

Ipv4Address APMonitorTerminal::GetPrimaryIpv4(Ptr<Node> node) const
{
    if (node == nullptr)
    {
        return Ipv4Address("0.0.0.0");
    }
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (ipv4 == nullptr)
    {
        return Ipv4Address("0.0.0.0");
    }
    for (uint32_t ifIndex = 0; ifIndex < ipv4->GetNInterfaces(); ++ifIndex)
    {
        for (uint32_t addrIndex = 0; addrIndex < ipv4->GetNAddresses(ifIndex); ++addrIndex)
        {
            Ipv4InterfaceAddress ifaddr = ipv4->GetAddress(ifIndex, addrIndex);
            if (ifaddr.GetLocal() != Ipv4Address("127.0.0.1"))
            {
                return ifaddr.GetLocal();
            }
        }
    }
    return Ipv4Address("0.0.0.0");
}

double APMonitorTerminal::ComputeThroughputBps()
{
    if (m_flowMonitor == nullptr || m_flowClassifier == nullptr)
    {
        return 0.0;
    }

    m_flowMonitor->CheckForLostPackets();
    auto stats = m_flowMonitor->GetFlowStats();

    Ipv4Address myAddress = GetPrimaryIpv4(GetNode());
    double totalBits = 0.0;
    double firstRx = -1.0;
    double lastRx = -1.0;

    for (const auto& kv : stats)
    {
        const FlowId flowId = kv.first;
        const FlowMonitor::FlowStats& fs = kv.second;
        if (fs.rxPackets == 0)
        {
            continue;
        }

        Ipv4FlowClassifier::FiveTuple tuple = m_flowClassifier->FindFlow(flowId);
        if (tuple.sourceAddress != myAddress && tuple.destinationAddress != myAddress)
        {
            continue;
        }

        double start = fs.timeFirstRxPacket.GetSeconds();
        double end = fs.timeLastRxPacket.GetSeconds();
        if (end <= start)
        {
            continue;
        }

        totalBits += static_cast<double>(fs.rxBytes) * 8.0;
        if (firstRx < 0.0 || start < firstRx)
        {
            firstRx = start;
        }
        if (lastRx < 0.0 || end > lastRx)
        {
            lastRx = end;
        }
    }

    double duration = (lastRx > firstRx && firstRx >= 0.0) ? (lastRx - firstRx) : 0.0;
    double throughput = (duration > 0.0 && totalBits > 0.0) ? (totalBits / duration) : 0.0;

    // 次の計測を新しいウィンドウで始めるため統計をリセット
    m_flowMonitor->ResetAllStats();

    return throughput;
}

void APMonitorTerminal::ForceReportToServer()
{
    if (m_rttSamples.empty())
    {
        if (m_averageRtt > 0.0)
        {
            m_rttSamples.push_back(m_averageRtt);
        }
        else
        {
            m_rttSamples.push_back(0.0);
        }
    }
    ReportRTTToServer();
}

} // namespace ns3
