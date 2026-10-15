/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "APMonitorTerminal.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/inet-socket-address.h"
#include "ns3/config.h"
#include "ns3/icmpv4.h"
#include "ns3/ipv4-header.h"
#include "ns3/node.h"
#include "ns3/uinteger.h"

#include <iostream>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <limits>
#include <vector>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("APMonitorTerminal");

APMonitorTerminal::APMonitorTerminal(uint32_t apId, Ipv4Address targetAP, Ipv4Address serverAddress)
    : m_apId(apId),
      m_targetAP(targetAP),
      m_serverAddress(serverAddress),
      m_serverPort(8080),
      m_measureInterval(100.0),
      m_samplesPerReport(10),
      m_pingInterval(0.2),
      m_pingPayloadSize(1200),        // Pingペイロードサイズ（バイト）
      m_socket(nullptr),
      m_pingSocket(nullptr),
      m_isMonitoring(false),
      m_appStartTime(Seconds(0.0)),
      m_pingSeq(0),
      m_sentPingsInWindow(0),
      m_totalPings(0),
      m_successfulPings(0),
      m_averageRtt(0.0),
      m_minRtt(std::numeric_limits<double>::max()),
      m_maxRtt(0.0)
{
    NS_LOG_FUNCTION(this);
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
    // RTT監視の開始・停止は NetSim::ScheduleMonitorWindows() で
    // サイクルごとに制御する。StartApplication() では
    // Application の初期化のみ行い、監視は開始しない。
    EnsurePingSocket();
}

void APMonitorTerminal::StopApplication()
{
    NS_LOG_FUNCTION(this);
    StopMonitoring();
    if (m_pingSocket)
    {
        m_pingSocket->Close();
        m_pingSocket = nullptr;
    }
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

    m_isMonitoring = true;
    if (m_marl)
    {
        ++m_marlCycle;
        m_marlAttempts = m_marlFailures = m_marlExpired = m_marlLate = 0;
        m_marlFinished = m_marlMeasurementError = false;
        m_marlReport.clear();
        m_marlExpiredTimes.clear();
        m_marlReportOffset = 0;
        if (m_socket) { m_socket->Close(); m_socket = nullptr; }
    }
    std::cout << "=== Monitoring started for AP" << m_apId << " ===" << std::endl;

    m_totalPings = 0;
    m_successfulPings = 0;
    m_sentPingsInWindow = 0;
    m_pingSendTimes.clear();
    m_rttSamples.clear();
    m_minRtt = std::numeric_limits<double>::max();
    m_maxRtt = 0.0;
    
    // サイクル監視窓ごとに1回だけPing測定を開始する。
    // 次回の監視開始は NetSim::ScheduleMonitorWindows() が次サイクルで行う。
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
    if (m_marl && m_socket)
    {
        std::cout << "[MARL report stop] cycle=" << m_marlCycle << " ap=" << m_apId
                  << " time=" << Simulator::Now().GetSeconds()
                  << " queued=" << m_marlReportOffset << '/' << m_marlReport.size() << std::endl;
        m_socket->Close(); m_socket = nullptr;
    }
    
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
    m_pingSendTimes.clear();
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
    if (m_marl) { SendMarlPing(); return; }

    if (m_sentPingsInWindow >= m_samplesPerReport)
    {
        return;
    }

    if (!EnsurePingSocket())
    {
        std::cout << "MONITOR_AP" << m_apId
                  << " failed to create ICMP socket" << std::endl;
        return;
    }

    Ipv4Address dst = m_targetAP;

    // PingHelper/Applicationを毎サイクル生成せず、APMonitorTerminal自身の
    // Raw ICMP socketからEcho Requestを送る。
    double intervalSeconds = (m_pingInterval > 0.0) ? m_pingInterval : 0.02;
    const uint32_t payloadSize = std::max<uint32_t>(m_pingPayloadSize, 16);
    std::vector<uint8_t> payload(payloadSize, 0);
    Ptr<Packet> dataPacket = Create<Packet>(payload.data(), payload.size());

    const uint16_t seq = m_pingSeq++;
    Icmpv4Echo echo;
    echo.SetIdentifier(GetPingIdentifier());
    echo.SetSequenceNumber(seq);
    echo.SetData(dataPacket);

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(echo);

    Icmpv4Header icmpHeader;
    icmpHeader.SetType(Icmpv4Header::ICMPV4_ECHO);
    icmpHeader.SetCode(0);
    if (Node::ChecksumEnabled())
    {
        icmpHeader.EnableChecksum();
    }
    packet->AddHeader(icmpHeader);

    int sentBytes = m_pingSocket->SendTo(packet, 0, InetSocketAddress(dst, 0));
    if (sentBytes > 0)
    {
        m_pingSendTimes[seq] = Simulator::Now();
        m_totalPings++;
        m_sentPingsInWindow++;

        // 送信ログ
        // std::cout << "MONITOR_AP" << m_apId << " ping sent to " << dst
        //           << " at time " << Simulator::Now().GetSeconds()
        //           << "s (Sample: " << m_sentPingsInWindow << "/"
        //           << m_samplesPerReport << ")" << std::endl;
    }
    else
    {
        std::cout << "MONITOR_AP" << m_apId << " ping send failed to "
                  << dst << " at time " << Simulator::Now().GetSeconds()
                  << "s" << std::endl;
    }

    double durationSeconds = intervalSeconds * static_cast<double>(m_samplesPerReport);
    if (durationSeconds <= 0.0)
    {
        durationSeconds = intervalSeconds;
    }
    Time measurementDuration = Seconds(durationSeconds);

    if (!m_measurementTimeoutEvent.IsPending())
    {
        m_measurementTimeoutEvent = Simulator::Schedule(measurementDuration + MilliSeconds(50),
                                                       &APMonitorTerminal::HandleMeasurementTimeout,
                                                       this);
    }

    if (m_sentPingsInWindow < m_samplesPerReport)
    {
        m_pingEvent = Simulator::Schedule(Seconds(intervalSeconds),
                                          &APMonitorTerminal::SendPeriodicPing,
                                          this);
    }
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
    if (m_pingEvent.IsPending())
    {
        Simulator::Cancel(m_pingEvent);
    }
    m_pingSendTimes.clear();

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

    // TCP接続を作成してサーバーに送信
    m_socket = CreateTcpSocket();
    if (m_socket) {
        InetSocketAddress serverAddr(m_serverAddress, m_serverPort);
        m_socket->Connect(serverAddr);
        
        m_socket->SetConnectCallback(
            MakeCallback(&APMonitorTerminal::OnConnectionSucceeded, this),
            MakeCallback(&APMonitorTerminal::OnConnectionFailed, this));
    }

    // 以前はここで次回測定を予約していたが、現在はサイクル窓ごとに
    // 1回だけRTT測定する。次の測定開始は次サイクルの
    // StartContinuousMonitoring() に任せる。
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
    if (m_pingEvent.IsPending())
    {
        Simulator::Cancel(m_pingEvent);
    }
    if (!m_isMonitoring)
    {
        return;
    }

    if (!m_rttSamples.empty())
    {
        ReportRTTToServer();
        return;
    }

    m_pingSendTimes.clear();

    // サイクル窓ごとに1測定だけ行うため、RTTサンプルが得られない場合も
    // この窓内では再試行しない。次の測定は次サイクルで開始される。
    std::cout << "AP" << m_apId
              << " measurement window expired with no RTT samples" << std::endl;
}

Ptr<Socket> APMonitorTerminal::CreateTcpSocket()
{
    return Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
}

bool APMonitorTerminal::EnsurePingSocket()
{
    if (m_pingSocket)
    {
        return true;
    }

    Ptr<Node> node = GetNode();
    if (node == nullptr)
    {
        return false;
    }

    m_pingSocket =
        Socket::CreateSocket(node, TypeId::LookupByName("ns3::Ipv4RawSocketFactory"));
    if (m_pingSocket == nullptr)
    {
        return false;
    }

    m_pingSocket->SetAttribute("Protocol", UintegerValue(1)); // ICMP
    m_pingSocket->SetRecvCallback(MakeCallback(&APMonitorTerminal::HandlePingReply, this));
    return true;
}

uint16_t APMonitorTerminal::GetPingIdentifier() const
{
    // APMonitorTerminalが生成したICMP Echo Replyだけを識別するためのID。
    return static_cast<uint16_t>(0xA000u + (m_apId & 0x0FFFu));
}

void APMonitorTerminal::HandlePingReply(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    if (socket == nullptr)
    {
        return;
    }

    while (socket->GetRxAvailable() > 0)
    {
        Address from;
        Ptr<Packet> packet = socket->RecvFrom(from);
        if (packet == nullptr)
        {
            continue;
        }

        if (!InetSocketAddress::IsMatchingType(from))
        {
            continue;
        }

        Ipv4Header ipv4Header;
        packet->RemoveHeader(ipv4Header);

        Icmpv4Header icmpHeader;
        packet->RemoveHeader(icmpHeader);

        if (icmpHeader.GetType() != Icmpv4Header::ICMPV4_ECHO_REPLY)
        {
            continue;
        }

        Icmpv4Echo echo;
        packet->RemoveHeader(echo);

        if (echo.GetIdentifier() != GetPingIdentifier())
        {
            continue;
        }

        const uint16_t seq = echo.GetSequenceNumber();
        auto sentIt = m_pingSendTimes.find(seq);
        if (sentIt == m_pingSendTimes.end())
        {
            if (m_marl && m_marlExpiredTimes.erase(seq))
            {
                ++m_marlLate;
                std::cout << "[MARL late] cycle=" << m_marlCycle << " ap=" << m_apId
                          << " seq=" << seq << " time=" << Simulator::Now().GetSeconds() << std::endl;
            }
            continue;
        }

        const Time rtt = Simulator::Now() - sentIt->second;
        if (m_marl)
        {
            if (!m_isMonitoring || m_marlFinished) continue;
            if (rtt >= Seconds(m_marlTimeout))
            {
                ++m_marlLate; ++m_marlExpired;
                m_pingSendTimes.erase(sentIt);
                std::cout << "[MARL late] cycle=" << m_marlCycle << " ap=" << m_apId
                          << " seq=" << seq << " time=" << Simulator::Now().GetSeconds() << std::endl;
                FinishMarlReport();
                continue;
            }
            m_pingSendTimes.erase(sentIt);
            if (rtt.IsStrictlyPositive()) m_rttSamples.push_back(rtt.GetSeconds()*1000.0);
            else { m_marlMeasurementError = true; ++m_marlExpired; }
            FinishMarlReport();
        }
        else
        {
            m_pingSendTimes.erase(sentIt);
            OnRttMeasured(rtt);
        }
    }
}

void APMonitorTerminal::OnConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this);
    
    if (m_marl)
    {
        std::cout << "[MARL report connected] cycle=" << m_marlCycle << " ap=" << m_apId
                  << " time=" << Simulator::Now().GetSeconds() << std::endl;
        SendMarlReportBytes(socket, socket->GetTxAvailable()); return;
    }
    std::stringstream message;
    message << "MONITOR_AP" << m_apId << "," << m_averageRtt;
    
    std::string msg = message.str();
    socket->Send(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.length(), 0);
    socket->ShutdownSend();
    socket->Close();
    m_socket = nullptr;
    
    // サンプルをクリア
    m_rttSamples.clear();
}

void APMonitorTerminal::OnConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this);
    std::cout << "=== Monitor AP" << m_apId << ": Connection to server failed ===" << std::endl;
    
    if (m_marl)
    {
        socket->Close();
        m_socket = nullptr;
        return; // TCP transport retries only; no application reconnect.
    }
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

    // cancel pending RTT/report events
    if (m_pingEvent.IsPending())
    {
        Simulator::Cancel(m_pingEvent);
    }
    if (m_reportEvent.IsPending())
    {
        Simulator::Cancel(m_reportEvent);
    }
    if (m_measurementTimeoutEvent.IsPending())
    {
        Simulator::Cancel(m_measurementTimeoutEvent);
    }
    m_pingSendTimes.clear();

    std::cout << "=== Monitoring stopped for AP" << m_apId << " ===\n";
}

Time APMonitorTerminal::GetApplicationStartTime() const
{
    return m_appStartTime;
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

void APMonitorTerminal::ForceReportToServer()
{
    if (m_marl) { FinishMarlReport(); return; }
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


void APMonitorTerminal::EnableMarl(uint32_t count, double interval, double timeout, double reportConnTimeout)
{
    NS_ABORT_MSG_IF(!std::isfinite(reportConnTimeout) || reportConnTimeout <= 0,
                    "Invalid MARL report connection timeout");
    m_marlReportConnTimeout = reportConnTimeout;
    NS_ABORT_MSG_IF(count == 0 || count > 10000 || !std::isfinite(interval) || interval <= 0 ||
                    !std::isfinite(timeout) || timeout <= 0, "Invalid MARL probe configuration");
    m_marl = true;
    m_samplesPerReport = count; m_pingInterval = interval; m_marlTimeout = timeout;
}

void APMonitorTerminal::SendMarlPing()
{
    if (!m_isMonitoring || m_marlFinished || m_marlAttempts >= m_samplesPerReport) return;
    ++m_marlAttempts;
    if (EnsurePingSocket())
    {
        const uint16_t seq = m_pingSeq++;
        std::vector<uint8_t> payload(std::max<uint32_t>(m_pingPayloadSize, 16), 0);
        Icmpv4Echo echo;
        echo.SetIdentifier(GetPingIdentifier()); echo.SetSequenceNumber(seq);
        echo.SetData(Create<Packet>(payload.data(), payload.size()));
        auto packet = Create<Packet>(); packet->AddHeader(echo);
        Icmpv4Header icmp;
        icmp.SetType(Icmpv4Header::ICMPV4_ECHO); icmp.SetCode(0);
        if (Node::ChecksumEnabled()) icmp.EnableChecksum();
        packet->AddHeader(icmp);
        if (m_pingSocket->SendTo(packet, 0, InetSocketAddress(m_targetAP, 0)) > 0)
        {
            m_pingSendTimes[seq] = Simulator::Now(); ++m_sentPingsInWindow;
            Simulator::Schedule(Seconds(m_marlTimeout), &APMonitorTerminal::MarlDeadline, this, m_marlCycle, seq);
        }
        else ++m_marlFailures;
    }
    else ++m_marlFailures;
    if (m_marlAttempts < m_samplesPerReport)
        m_pingEvent = Simulator::Schedule(Seconds(m_pingInterval), &APMonitorTerminal::SendMarlPing, this);
    FinishMarlReport();
}

void APMonitorTerminal::MarlDeadline(uint32_t cycle, uint16_t seq)
{
    if (cycle != m_marlCycle || !m_isMonitoring || m_marlFinished) return;
    auto it = m_pingSendTimes.find(seq);
    if (it != m_pingSendTimes.end())
    {
        m_marlExpiredTimes[seq] = it->second;
        m_pingSendTimes.erase(it);
        ++m_marlExpired;
    }
    FinishMarlReport();
}

void APMonitorTerminal::SetMarlReportCallback(std::function<void(const std::string&)> callback)
{
    m_marlReportCallback = callback;
}

void APMonitorTerminal::FinishMarlReport()
{
    if (m_marlFinished || !m_isMonitoring || m_marlAttempts != m_samplesPerReport ||
        m_rttSamples.size()+m_marlExpired != m_sentPingsInWindow) return;
    m_marlFinished = true;
    const std::string status = m_marlFailures ? "probe_send_error" :
        m_marlMeasurementError ? "measurement_error" : m_rttSamples.empty() ? "timeout" : "ok";
    const double mean = m_rttSamples.empty() ? 0.0 :
        std::accumulate(m_rttSamples.begin(), m_rttSamples.end(), 0.0)/m_rttSamples.size();
    std::ostringstream report;
    report << std::setprecision(17) << "MARL_RTT_V1 " << m_marlCycle << ' ' << m_apId << ' ' << status
           << ' ' << mean << ' ' << m_sentPingsInWindow << ' ' << m_rttSamples.size()
           << ' ' << m_marlFailures << ' ' << m_marlLate << '\n';
    m_marlReport = report.str();
    std::cout << "[MARL monitor] " << m_marlReport;
    // Research assumption: completed measurements reach the controller reliably,
    // at the current simulation time. No TCP report traffic or fallback values.
    NS_ABORT_MSG_IF(!m_marlReportCallback, "MARL internal report callback is not configured");
    m_marlReportCallback(m_marlReport);
}

void APMonitorTerminal::SendMarlReportBytes(Ptr<Socket> socket, uint32_t available)
{
    if (!m_isMonitoring || socket != m_socket) return;
    while (m_marlReportOffset < m_marlReport.size() && available > 0)
    {
        const size_t size = std::min<size_t>(available, m_marlReport.size()-m_marlReportOffset);
        int sent = socket->Send(reinterpret_cast<const uint8_t*>(m_marlReport.data()+m_marlReportOffset), size, 0);
        if (sent <= 0) return;
        m_marlReportOffset += sent;
        std::cout << "[MARL report queued] cycle=" << m_marlCycle << " ap=" << m_apId
                  << " time=" << Simulator::Now().GetSeconds()
                  << " bytes=" << m_marlReportOffset << '/' << m_marlReport.size() << std::endl;
        available = socket->GetTxAvailable();
    }
    if (m_marlReportOffset == m_marlReport.size()) socket->ShutdownSend();
}

} // namespace ns3
