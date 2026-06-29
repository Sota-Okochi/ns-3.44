#include "NetSim.h"

#include <unordered_map>

NS_LOG_COMPONENT_DEFINE("NetSimApplications");

namespace ns3 {
namespace {

// ポート番号の定数
constexpr uint16_t kRttForwarderListenPort = 9000;
constexpr uint16_t kRttForwarderRemotePort = 8080;
constexpr uint16_t kBrowserBasePort        = 15000;
// master_log だけを残すため、ブラウザ/FlowMonitorの補助CSV出力は止める。
constexpr bool kEnableBrowserDiagnostics = false;
constexpr bool kEnableFlowCycleDiagnostics = false;

Ipv4Address GetPrimaryIpv4(Ptr<Node> node)
{
    if (!node)
    {
        return Ipv4Address("0.0.0.0"); 
    }
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (!ipv4)
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

[[maybe_unused]] void TracePacketToAscii(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> packet)
{
    if (stream == nullptr)
    {
        return;
    }
    std::ostream* os = stream->GetStream();
    if (os == nullptr || packet == nullptr)
    {
        return;
    }
    (*os) << Simulator::Now().GetSeconds() << " " << packet->GetSize() << std::endl;
}

const char* RatTypeToString(RatType rat)
{
    if (rat == RatType::NR)
    {
        return "NR";
    }
    return "WIFI";
}

std::string SocketErrnoToString(Socket::SocketErrno err)
{
    switch (err)
    {
    case Socket::ERROR_NOTERROR:
        return "OK";
    case Socket::ERROR_NOROUTETOHOST:
        return "NOROUTETOHOST";
    case Socket::ERROR_NODEV:
        return "NODEV";
    case Socket::ERROR_ADDRNOTAVAIL:
        return "ADDRNOTAVAIL";
    default:
        return std::to_string(static_cast<int>(err));
    }
}

} // namespace

void NetSim::LogBrowserBulkDiagnostic(uint32_t termIdx,
                                      uint16_t port,
                                      uint32_t generation,
                                      const std::string& phase,
                                      Ipv4Address dstIp,
                                      Ptr<Application> app)
{
    if (!kEnableBrowserDiagnostics)
    {
        return;
    }

    const std::string csvPath = std::string(OUTPUT_DIR) + "browser_bulk_diagnostics.csv";
    bool writeHeader = false;
    {
        std::ifstream chk(csvPath);
        writeHeader = (!chk.good() || chk.peek() == std::ifstream::traits_type::eof());
    }

    int currentAp = 0;
    const char* currentRat = "unknown";
    if (termIdx < m_termAccessState.size())
    {
        currentAp = m_termAccessState[termIdx].currentAp;
        currentRat = RatTypeToString(m_termAccessState[termIdx].currentRat);
    }

    Ipv4Address activeIp = Ipv4Address("0.0.0.0");
    if (termIdx < terms.size())
    {
        activeIp = GetActiveIpv4(termIdx);
    }

    std::string routeErr = "no_server_ipv4";
    std::string routeGateway = "0.0.0.0";
    int32_t routeIf = -1;
    if (server_browser != nullptr)
    {
        Ptr<Ipv4> serverIpv4 = server_browser->GetObject<Ipv4>();
        if (serverIpv4 != nullptr && serverIpv4->GetRoutingProtocol() != nullptr)
        {
            Ipv4Header header;
            header.SetDestination(dstIp);
            header.SetProtocol(6); // TCP
            Socket::SocketErrno err = Socket::ERROR_NOTERROR;
            Ptr<Ipv4Route> route =
                serverIpv4->GetRoutingProtocol()->RouteOutput(Create<Packet>(), header, nullptr, err);
            routeErr = SocketErrnoToString(err);
            if (route != nullptr)
            {
                routeIf = static_cast<int32_t>(serverIpv4->GetInterfaceForDevice(route->GetOutputDevice()));
                std::ostringstream gw;
                gw << route->GetGateway();
                routeGateway = gw.str();
            }
        }
    }

    uint64_t matchedTxPackets = 0;
    uint64_t matchedRxPackets = 0;
    uint64_t matchedTxBytes = 0;
    uint64_t matchedRxBytes = 0;
    uint32_t matchedFlows = 0;
    std::string matchedFlowIds;
    if (m_termFlowMonitor != nullptr && m_termFlowClassifier != nullptr)
    {
        m_termFlowMonitor->CheckForLostPackets();
        for (const auto& kv : m_termFlowMonitor->GetFlowStats())
        {
            Ipv4FlowClassifier::FiveTuple tuple = m_termFlowClassifier->FindFlow(kv.first);
            if (tuple.destinationAddress != dstIp || tuple.destinationPort != port ||
                tuple.protocol != 6)
            {
                continue;
            }

            const FlowMonitor::FlowStats& fs = kv.second;
            matchedTxPackets += fs.txPackets;
            matchedRxPackets += fs.rxPackets;
            matchedTxBytes += fs.txBytes;
            matchedRxBytes += fs.rxBytes;
            if (!matchedFlowIds.empty())
            {
                matchedFlowIds += "|";
            }
            matchedFlowIds += std::to_string(kv.first);
            matchedFlows++;
        }
    }

    if (matchedFlowIds.empty())
    {
        matchedFlowIds = "-";
    }

    std::ofstream ofs(csvPath, std::ios::app);
    if (!ofs.good())
    {
        return;
    }
    if (writeHeader)
    {
        ofs << "time_s,cycle,term_idx,generation,phase,current_ap,current_rat,"
            << "active_ip,dst_ip,port,route_error,route_if,route_gateway,"
            << "app_present,matched_flows,flow_ids,tx_packets,rx_packets,tx_bytes,rx_bytes\n";
    }

    const uint32_t cycleNum =
        static_cast<uint32_t>(Simulator::Now().GetSeconds() / m_cycleDuration.GetSeconds()) + 1;
    ofs << std::fixed << std::setprecision(2)
        << Simulator::Now().GetSeconds() << ","
        << cycleNum << ","
        << (termIdx + 1) << ","
        << generation << ","
        << phase << ","
        << currentAp << ","
        << currentRat << ","
        << activeIp << ","
        << dstIp << ","
        << port << ","
        << routeErr << ","
        << routeIf << ","
        << routeGateway << ","
        << (app != nullptr ? 1 : 0) << ","
        << matchedFlows << ","
        << matchedFlowIds << ","
        << matchedTxPackets << ","
        << matchedRxPackets << ","
        << matchedTxBytes << ","
        << matchedRxBytes << "\n";
}

void NetSim::LogBrowserTcpEvent(const std::string& context,
                                const std::string& event,
                                uint32_t packetSize,
                                const std::string& detail)
{
    if (!kEnableBrowserDiagnostics)
    {
        return;
    }

    const std::string csvPath = std::string(OUTPUT_DIR) + "browser_tcp_events.csv";
    bool writeHeader = false;
    {
        std::ifstream chk(csvPath);
        writeHeader = (!chk.good() || chk.peek() == std::ifstream::traits_type::eof());
    }

    std::ofstream ofs(csvPath, std::ios::app);
    if (!ofs.good())
    {
        return;
    }
    if (writeHeader)
    {
        ofs << "time_s,cycle,context,event,packet_size,detail\n";
    }

    const uint32_t cycleNum =
        static_cast<uint32_t>(Simulator::Now().GetSeconds() / m_cycleDuration.GetSeconds()) + 1;
    ofs << std::fixed << std::setprecision(2)
        << Simulator::Now().GetSeconds() << ","
        << cycleNum << ","
        << context << ","
        << event << ","
        << packetSize << ","
        << detail << "\n";
}

void NetSim::BrowserBulkTxTrace(std::string context, Ptr<const Packet> packet)
{
    LogBrowserTcpEvent(context, "bulk_tx", packet ? packet->GetSize() : 0, "-");
}

void NetSim::BrowserBulkSocketTrace(std::string context, Ptr<Socket> socket)
{
    std::string detail = "socket=null";
    if (socket != nullptr)
    {
        detail = "errno=" + SocketErrnoToString(socket->GetErrno());
        std::ostringstream oss;
        oss << detail << " socket=" << PeekPointer(socket);
        detail = oss.str();
    }
    LogBrowserTcpEvent(context, "bulk_socket", 0, detail);
}

void NetSim::BrowserBulkConnectReturnTrace(std::string context, Ptr<Socket> socket, int32_t result)
{
    std::ostringstream detail;
    detail << "result=" << result;
    if (socket != nullptr)
    {
        detail << " errno=" << SocketErrnoToString(socket->GetErrno())
               << " socket=" << PeekPointer(socket);
    }
    else
    {
        detail << " socket=null";
    }
    LogBrowserTcpEvent(context, "bulk_connect_return", 0, detail.str());
}

void NetSim::BrowserBulkConnectionTrace(std::string context, Ptr<Socket> socket)
{
    std::string detail = "errno=unknown";
    if (socket != nullptr)
    {
        detail = "errno=" + SocketErrnoToString(socket->GetErrno());
    }
    LogBrowserTcpEvent(context, "bulk_connection", 0, detail);
}

void NetSim::BrowserBulkRetransmissionTrace(std::string context,
                                            Ptr<const Packet> packet,
                                            const TcpHeader& header,
                                            const Address& localAddr,
                                            const Address& peerAddr,
                                            Ptr<const TcpSocketBase> socket)
{
    std::ostringstream detail;
    detail << "local=" << localAddr
           << " peer=" << peerAddr
           << " header=\"" << header << "\""
           << " socket=" << PeekPointer(socket);
    LogBrowserTcpEvent(context,
                       "bulk_tcp_retransmission",
                       packet ? packet->GetSize() : 0,
                       detail.str());
}

void NetSim::BrowserTcpSocketTxTrace(std::string context,
                                     Ptr<const Packet> packet,
                                     const TcpHeader& header,
                                     Ptr<const TcpSocketBase> socket)
{
    std::ostringstream detail;
    detail << "header=\"" << header << "\""
           << " socket=" << PeekPointer(socket);
    LogBrowserTcpEvent(context, "tcp_socket_tx", packet ? packet->GetSize() : 0, detail.str());
}

void NetSim::BrowserTcpSocketStateTrace(std::string context,
                                        TcpSocket::TcpStates_t oldState,
                                        TcpSocket::TcpStates_t newState)
{
    std::ostringstream detail;
    detail << "old=" << static_cast<int>(oldState)
           << " new=" << static_cast<int>(newState);
    LogBrowserTcpEvent(context, "tcp_socket_state", 0, detail.str());
}

void NetSim::AttachBrowserBulkTrace(uint32_t termIdx,
                                    uint16_t port,
                                    uint32_t generation,
                                    Ipv4Address dstIp,
                                    Ptr<Application> app)
{
    if (app == nullptr)
    {
        return;
    }

    std::ostringstream context;
    context << "term=" << (termIdx + 1)
            << "|gen=" << generation
            << "|dst=" << dstIp
            << "|port=" << port;
    const std::string ctx = context.str();

    app->TraceConnect("Tx", ctx, MakeCallback(&NetSim::BrowserBulkTxTrace, this));
    app->TraceConnect("Start",
                      ctx + "|phase=start",
                      MakeCallback(&NetSim::BrowserBulkSocketTrace, this));
    app->TraceConnect("SocketCreated",
                      ctx + "|phase=socket_created",
                      MakeCallback(&NetSim::BrowserBulkSocketTrace, this));
    app->TraceConnect("ConnectAttempt",
                      ctx + "|phase=connect_attempt",
                      MakeCallback(&NetSim::BrowserBulkSocketTrace, this));
    app->TraceConnect("ConnectReturn",
                      ctx,
                      MakeCallback(&NetSim::BrowserBulkConnectReturnTrace, this));
    app->TraceConnect("ConnectionSucceeded",
                      ctx + "|result=success",
                      MakeCallback(&NetSim::BrowserBulkConnectionTrace, this));
    app->TraceConnect("ConnectionFailed",
                      ctx + "|result=failed",
                      MakeCallback(&NetSim::BrowserBulkConnectionTrace, this));
    app->TraceConnect("TcpRetransmission",
                      ctx,
                      MakeCallback(&NetSim::BrowserBulkRetransmissionTrace, this));

    Simulator::Schedule(MilliSeconds(1),
                        &NetSim::ProbeBrowserBulkSocketTrace,
                        this,
                        termIdx,
                        port,
                        generation,
                        dstIp,
                        app);
}

void NetSim::ProbeBrowserBulkSocketTrace(uint32_t termIdx,
                                         uint16_t port,
                                         uint32_t generation,
                                         Ipv4Address dstIp,
                                         Ptr<Application> app)
{
    Ptr<BulkSendApplication> bulkApp = DynamicCast<BulkSendApplication>(app);
    if (bulkApp == nullptr)
    {
        return;
    }

    std::ostringstream context;
    context << "term=" << (termIdx + 1)
            << "|gen=" << generation
            << "|dst=" << dstIp
            << "|port=" << port;
    const std::string ctx = context.str();

    Ptr<Socket> socket = bulkApp->GetSocket();
    if (socket == nullptr)
    {
        LogBrowserTcpEvent(ctx, "socket_probe", 0, "socket=null");
        return;
    }

    std::ostringstream detail;
    detail << "errno=" << SocketErrnoToString(socket->GetErrno())
           << " socket=" << PeekPointer(socket);
    LogBrowserTcpEvent(ctx, "socket_probe", 0, detail.str());

    Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);
    if (tcpSocket == nullptr)
    {
        LogBrowserTcpEvent(ctx, "socket_probe", 0, "socket_not_tcp_base");
        return;
    }

    tcpSocket->TraceConnect("Tx", ctx, MakeCallback(&NetSim::BrowserTcpSocketTxTrace, this));
    tcpSocket->TraceConnect("Retransmission",
                            ctx,
                            MakeCallback(&NetSim::BrowserBulkRetransmissionTrace, this));
    tcpSocket->TraceConnect("State", ctx, MakeCallback(&NetSim::BrowserTcpSocketStateTrace, this));
}

void NetSim::ScheduleBrowserDownloadForTerminal(uint32_t termIdx,
                                                uint16_t port,
                                                uint32_t generation,
                                                uint32_t requestIndex,
                                                uint32_t totalRequests,
                                                Time interval,
                                                Time duration,
                                                uint32_t maxBytes)
{
    if (server_browser == nullptr || termIdx >= terms.size() || requestIndex >= totalRequests)
    {
        return;
    }
    if (termIdx >= m_termAppStates.size() ||
        m_termAppStates[termIdx].browserGeneration != generation)
    {
        return;
    }

    Ipv4Address clientAddress =
        GetActiveIpv4(termIdx);
    if (clientAddress == Ipv4Address("0.0.0.0"))
    {
        return;
    }

    const uint16_t requestPort = port + static_cast<uint16_t>(requestIndex);
    LogBrowserBulkDiagnostic(termIdx, requestPort, generation, "before_install", clientAddress);

    if (kEnableBrowserDiagnostics)
    {
        const uint32_t cycleNum =
            static_cast<uint32_t>(Simulator::Now().GetSeconds() / m_cycleDuration.GetSeconds()) + 1;
        const std::string csvPath = std::string(OUTPUT_DIR) + "browser_send_events.csv";
        bool writeHeader = false;
        {
            std::ifstream chk(csvPath);
            writeHeader = (!chk.good() || chk.peek() == std::ifstream::traits_type::eof());
        }

        std::ofstream ofs(csvPath, std::ios::app);
        if (ofs.good())
        {
            if (writeHeader)
            {
                ofs << "time_s,cycle,term_idx,generation,current_ap,current_rat,dst_ip,port,"
                    << "request_index,total_requests,max_bytes\n";
            }

            int currentAp = 0;
            const char* currentRat = "unknown";
            if (termIdx < m_termAccessState.size())
            {
                currentAp = m_termAccessState[termIdx].currentAp;
                currentRat = RatTypeToString(m_termAccessState[termIdx].currentRat);
            }

            ofs << std::fixed << std::setprecision(2) << Simulator::Now().GetSeconds() << ","
                << cycleNum << ","
                << (termIdx + 1) << ","
                << generation << ","
                << currentAp << ","
                << currentRat << ","
                << clientAddress << ","
                << requestPort << ","
                << (requestIndex + 1) << ","
                << totalRequests << ","
                << maxBytes << "\n";
        }
    }

    BulkSendHelper bulk("ns3::TcpSocketFactory", InetSocketAddress(clientAddress, requestPort));
    bulk.SetAttribute("MaxBytes", UintegerValue(maxBytes));
    ApplicationContainer apps = bulk.Install(server_browser);
    apps.Start(Seconds(0));
    // MaxBytesで送信完了するため、短いStop時刻を入れない。
    // TCP接続確立が遅れると0.5s Stopで後続バーストが完了前に終了してしまう。
    (void)duration;
    if (apps.GetN() > 0)
    {
        Ptr<Application> app = apps.Get(0);
        m_termAppStates[termIdx].serverApps.push_back(app);
        if (kEnableBrowserDiagnostics)
        {
            AttachBrowserBulkTrace(termIdx, requestPort, generation, clientAddress, app);
        }
        LogBrowserBulkDiagnostic(termIdx, requestPort, generation, "after_install", clientAddress, app);
        if (kEnableBrowserDiagnostics)
        {
            Simulator::Schedule(MilliSeconds(50),
                                &NetSim::LogBrowserBulkDiagnostic,
                                this,
                                termIdx,
                                requestPort,
                                generation,
                                "probe_50ms",
                                clientAddress,
                                app);
            Simulator::Schedule(MilliSeconds(500),
                                &NetSim::LogBrowserBulkDiagnostic,
                                this,
                                termIdx,
                                requestPort,
                                generation,
                                "probe_500ms",
                                clientAddress,
                                app);
            Simulator::Schedule(Seconds(1.5),
                                &NetSim::LogBrowserBulkDiagnostic,
                                this,
                                termIdx,
                                requestPort,
                                generation,
                                "probe_1500ms",
                                clientAddress,
                                app);
        }
    }
    else
    {
        LogBrowserBulkDiagnostic(termIdx, requestPort, generation, "install_failed", clientAddress);
    }

    if (requestIndex + 1 < totalRequests)
    {
        Simulator::Schedule(interval,
                            &NetSim::ScheduleBrowserDownloadForTerminal,
                            this,
                            termIdx,
                            port,
                            generation,
                            requestIndex + 1,
                            totalRequests,
                            interval,
                            duration,
                            maxBytes);
    }
}

void NetSim::ScheduleBrowserWarmupForTerminal(uint32_t termIdx, uint16_t port, uint32_t generation)
{
    if (server_browser == nullptr || termIdx >= terms.size())
    {
        return;
    }
    if (termIdx >= m_termAppStates.size() ||
        m_termAppStates[termIdx].browserGeneration != generation)
    {
        return;
    }

    Ipv4Address clientAddress =
        GetActiveIpv4(termIdx);
    if (clientAddress == Ipv4Address("0.0.0.0"))
    {
        return;
    }

    const uint16_t warmupPort =
        port + static_cast<uint16_t>(std::max<uint32_t>(1, m_browserRequestCount));
    BulkSendHelper bulk("ns3::TcpSocketFactory", InetSocketAddress(clientAddress, warmupPort));
    bulk.SetAttribute("MaxBytes", UintegerValue(1));
    ApplicationContainer apps = bulk.Install(server_browser);
    apps.Start(Seconds(0));
    if (apps.GetN() > 0)
    {
        m_termAppStates[termIdx].serverApps.push_back(apps.Get(0));
    }
}

void NetSim::ScheduleFutureBrowserBursts(uint32_t termIdx, uint16_t port, uint32_t generation,
                                         Time minStartTime)
{
    const Time interval =
        m_browserRequestInterval.IsZero() ? Seconds(1.0) : m_browserRequestInterval;
    const uint32_t requestCount = std::max<uint32_t>(1, m_browserRequestCount);
    const Time requestDuration = Seconds(0.5);
    const Time now = Simulator::Now();

    for (uint32_t cycle = 0; cycle < m_cycleCount; ++cycle)
    {
        Time cycleStart = m_cycleDuration * cycle;
        Time warmupAt = cycleStart + std::max(Seconds(0.1), m_monitorStartOffset - Seconds(0.8));
        if (warmupAt > now && warmupAt > minStartTime)
        {
            Simulator::Schedule(warmupAt - now,
                                &NetSim::ScheduleBrowserWarmupForTerminal,
                                this,
                                termIdx,
                                port,
                                generation);
        }

        Time firstRequestAt = cycleStart + m_browserFirstRequest;
        // minStartTime: ハンドオーバー直後はARPが安定するまで送信を遅らせる（案2）
        if (firstRequestAt > now && firstRequestAt > minStartTime)
        {
            Simulator::Schedule(firstRequestAt - now,
                                &NetSim::ScheduleBrowserDownloadForTerminal,
                                this,
                                termIdx,
                                port,
                                generation,
                                0,
                                1,
                                interval,
                                requestDuration,
                                m_browserRequestBytes);
        }

        Time secondBurstAt = cycleStart + m_browserBatchBStart;
        if (secondBurstAt > now && secondBurstAt > minStartTime && requestCount > 1)
        {
            Simulator::Schedule(secondBurstAt - now,
                                &NetSim::ScheduleBrowserDownloadForTerminal,
                                this,
                                termIdx,
                                port + 1,
                                generation,
                                0,
                                requestCount - 1,
                                interval,
                                requestDuration,
                                m_browserRequestBytes);
        }
    }
}

void NetSim::AttachMonitorApplication(uint32_t apId, Ptr<Node> monitor)
{
    if (apId >= m_monitorApps.size())
    {
        m_monitorApps.resize(apId + 1);
    }

    if (monitor == nullptr || apId >= wifiAPs.size())
    {
        m_monitorApps[apId] = nullptr;
        return;
    }

    Ipv4Address rttServerAddress = GetPrimaryIpv4(server_rtt);
    Ipv4Address remoteAddress = m_remoteHostAddress;
    if (rttServerAddress == Ipv4Address("0.0.0.0") || remoteAddress == Ipv4Address("0.0.0.0"))
    {
        NS_LOG_WARN("Monitor cannot determine server addresses");
        return;
    }

    Ptr<APMonitorTerminal> monitorApp = CreateObject<APMonitorTerminal>(apId, rttServerAddress, remoteAddress);
    monitor->AddApplication(monitorApp);
    monitorApp->SetStartTime(Seconds(0.0));
    monitorApp->SetStopTime(m_simulationDuration);
    m_monitorApps[apId] = monitorApp;
}

void NetSim::SetAppLayer(){
    NS_LOG_FUNCTION(this);

    SetGreedy();
    SetKamedaModule();
    SetBrowserApp(); // ブラウザ, アプリ番号1, TP
    SetVideoApp(); // 動画, アプリ番号2, TP
    SetVoiceApp(); // 通話, アプリ番号3, RTT
    SetOnlineGameApp(); // オンラインゲーム, アプリ番号4, RTT
}

void NetSim::SetGreedy(void){

    NS_LOG_INFO("Set Greedy");

#if 0
    PingHelper ping(InetSocketAddress(server_rtt->GetObject<Ipv4>()->GetAddress(1,0).GetLocal(), 0));
    ping.SetAttribute("Interval", TimeValue(MilliSeconds(500)));
    ping.SetAttribute("Size", UintegerValue(1400));
    NodeContainer pingers;

    for(uint32_t i=0; i<termNum; i++){
            pingers.Add(terms[i]);
    }

    ApplicationContainer apps = ping.Install(pingers);
    apps.Start(Seconds(1.1));
    apps.Stop(Seconds(5.1));

    Config::ConnectWithoutContext ("/NodeList/*/ApplicationList/*/$ns3::Ping/Rtt",
                    MakeCallback (&PingRtt));
#endif
}

void NetSim::SetKamedaModule(void){

    NS_LOG_INFO("Kameda module load");

    if (remote_host == nullptr || server_rtt == nullptr)
    {
        NS_LOG_WARN("Remote host or RTT server node is not available");
        return;
    }

    if (m_remoteHostAddress == Ipv4Address("0.0.0.0"))
    {
        NS_LOG_WARN("Remote host address is not configured");
    }

    NS_LOG_LOGIC("install remote host Kameda server");
    Ptr<KamedaAppServer> appServer = CreateObject<KamedaAppServer>(m_apSelectionInput);
    appServer->ConfigureCycles(m_cycleCount, m_cycleDuration);
    appServer->SetMonitorStopOffset(m_monitorStopOffset);
    appServer->SetCycleEndOffset(m_terminalTpStopOffset);
    appServer->SetCycleEndGuard(m_cycleEndGuard);
    appServer->SetHandoverCallback([this](const std::vector<int>& assignment) {
        HandoverRequest(assignment);
    });
    remote_host->AddApplication(appServer);
    appServer->SetStartTime(Seconds(1.0));
    appServer->SetStopTime(m_simulationDuration);
    m_kamedaServer = appServer;

    NS_LOG_LOGIC("install RTT forwarder on server_rtt");
    Ptr<RttForwarderApp> forwarder = CreateObject<RttForwarderApp>();
    if (m_remoteHostAddress != Ipv4Address("0.0.0.0"))
    {
        forwarder->SetRemote(m_remoteHostAddress, kRttForwarderRemotePort);
    }
    forwarder->SetListeningPort(kRttForwarderListenPort);
    server_rtt->AddApplication(forwarder);
    forwarder->SetStartTime(Seconds(0.9));
    forwarder->SetStopTime(m_simulationDuration);

    if (monitorTerminals.size() < kMaxMonitorTerminals) {
        monitorTerminals.resize(kMaxMonitorTerminals, nullptr);
    }

    for(uint32_t apId = 0; apId < monitorTerminals.size(); apId++) {
        Ptr<Node> monitor = monitorTerminals[apId];
        AttachMonitorApplication(apId, monitor);
    }
    ScheduleMonitorWindows();
}

// ブラウザアプリケーションの設定
void NetSim::SetBrowserApp()
{
    NS_LOG_LOGIC("install browser apps");

    if (server_browser == nullptr)
    {
        NS_LOG_WARN("Browser server node is not configured");
        return;
    }

    Ipv4Address serverAddress = GetPrimaryIpv4(server_browser);
    if (serverAddress == Ipv4Address("0.0.0.0"))
    {
        NS_LOG_WARN("Browser server address unavailable");
        return;
    }

    const Time interval =
        m_browserRequestInterval.IsZero() ? Seconds(1.0) : m_browserRequestInterval;
    const uint32_t requestCount = std::max<uint32_t>(1, m_browserRequestCount);
    const uint32_t requestBytes = m_browserRequestBytes;
    const Time requestDuration = Seconds(0.5);
    const Time firstRequest = m_browserFirstRequest;
    const uint32_t cycles = std::max<uint32_t>(1, m_cycleCount);
    const Time sinkStop = m_simulationDuration.IsZero()
                              ? (firstRequest + interval * requestCount + requestDuration)
                              : m_simulationDuration;
    const uint16_t basePort = kBrowserBasePort;
    bool installedAny = false;

    for (uint32_t i = 0; i < terms.size(); ++i)
    {
        if (i >= m_termData.size() || m_termData[i].use_appli != 1)
        {
            continue;
        }

        Ptr<Node> client = terms[i];
        if (client == nullptr)
        {
            continue;
        }
        Ipv4Address clientAddress = GetActiveIpv4(i);
        if (clientAddress == Ipv4Address("0.0.0.0"))
        {
            NS_LOG_WARN("Browser terminal " << i << " has no IPv4 address");
            continue;
        }

        const uint16_t port = basePort + static_cast<uint16_t>(i);

        ApplicationContainer sinkApps;
        for (uint32_t request = 0; request <= requestCount; ++request)
        {
            PacketSinkHelper sinkHelper(
                "ns3::TcpSocketFactory",
                InetSocketAddress(Ipv4Address::GetAny(),
                                  port + static_cast<uint16_t>(request)));
            ApplicationContainer oneSink = sinkHelper.Install(client);
            oneSink.Start(Seconds(0.1));
            oneSink.Stop(sinkStop);
            sinkApps.Add(oneSink);
        }

        for (uint32_t cycle = 0; cycle < cycles; ++cycle)
        {
            Time offset = m_cycleDuration * cycle;
            Time warmupAt = offset + std::max(Seconds(0.1), m_monitorStartOffset - Seconds(0.8));
            Simulator::Schedule(warmupAt,
                                &NetSim::ScheduleBrowserWarmupForTerminal,
                                this,
                                i,
                                port,
                                m_termAppStates[i].browserGeneration);

            // Batch A: 1st request at cycleStart + browserFirstBurstSec.
            Simulator::Schedule(firstRequest + offset,
                                &NetSim::ScheduleBrowserDownloadForTerminal,
                                this,
                                i,
                                port,
                                m_termAppStates[i].browserGeneration,
                                0,
                                1,
                                interval,
                                requestDuration,
                                requestBytes);
            // Batch B: remaining requests at cycleStart + browserSecondBurstSec.
            if (requestCount > 1)
            {
                Simulator::Schedule(m_browserBatchBStart + offset,
                                    &NetSim::ScheduleBrowserDownloadForTerminal,
                                    this,
                                    i,
                                    port + 1,
                                    m_termAppStates[i].browserGeneration,
                                    0,
                                    requestCount - 1,
                                    interval,
                                    requestDuration,
                                    requestBytes);
            }
        }

        // アプリ追跡
        if (i < m_termAppStates.size())
        {
            m_termAppStates[i].appType = 1;
            m_termAppStates[i].primaryPort = port;
            if (sinkApps.GetN() > 0)
            {
                m_termAppStates[i].clientApps.push_back(sinkApps.Get(0));
            }
        }

        installedAny = true;
        NS_LOG_LOGIC("browser download configured for terminal " << i << " port " << port);
    }

    if (!installedAny)
    {
        NS_LOG_INFO("No browser terminals configured for appId=1");
    }
}

// 動画アプリケーションの設定
void NetSim::SetVideoApp(void){

    NS_LOG_LOGIC("install video apps");

    if (server_udpVideo == nullptr)
    {
        NS_LOG_WARN("Video server node is not configured");
        return;
    }

    Ipv4Address videoServerAddress = GetPrimaryIpv4(server_udpVideo);
    if (videoServerAddress == Ipv4Address("0.0.0.0"))
    {
        NS_LOG_WARN("Video server address unavailable");
        return;
    }

    uint16_t streamPort = 10000;
    bool installedAny = false;
    const Time sinkStop = m_simulationDuration.IsZero() ? Seconds(7.0) : m_simulationDuration;
    const Time sinkStart = Seconds(0.9);
    const Time serverStart = Seconds(1.0);

    for (uint32_t i = 0; i < terms.size(); ++i)
    {
        if (i >= m_termData.size() || m_termData[i].use_appli != 2)
        {
            continue;
        }
        Ptr<Node> client = terms[i];
        if (client == nullptr)
        {
            continue;
        }

        Ipv4Address clientAddress = GetActiveIpv4(i);
        if (clientAddress == Ipv4Address("0.0.0.0"))
        {
            NS_LOG_WARN("Video terminal " << i << " has no IPv4 address");
            continue;
        }

        PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), streamPort));
        ApplicationContainer sinkApps = sinkHelper.Install(client);

        std::string traceFile = std::string(INPUT_DIR) + "YouTube1080p_2min.dat";
        UdpTraceClientHelper udpClient(clientAddress, streamPort, traceFile);

        ApplicationContainer serverApps = udpClient.Install(server_udpVideo);
        sinkApps.Start(sinkStart);
        sinkApps.Stop(sinkStop);
        serverApps.Start(serverStart);
        serverApps.Stop(sinkStop);

        // アプリ追跡
        if (i < m_termAppStates.size())
        {
            m_termAppStates[i].appType = 2;
            m_termAppStates[i].primaryPort = streamPort;
            if (sinkApps.GetN() > 0)
            {
                m_termAppStates[i].clientApps.push_back(sinkApps.Get(0));
            }
            if (serverApps.GetN() > 0)
            {
                m_termAppStates[i].serverApps.push_back(serverApps.Get(0));
            }
        }

        installedAny = true;
        NS_LOG_LOGIC("video download configured for terminal " << i << " port " << streamPort);
        streamPort++;
    }

    if (!installedAny)
    {
        NS_LOG_INFO("No video terminals configured for appId=2");
    }

}

// 通話アプリケーションの設定
void NetSim::SetVoiceApp(void){

    NS_LOG_LOGIC("install voice apps");

    Ipv4Address voiceServerAddress = GetPrimaryIpv4(server_udpVoice);
    if (voiceServerAddress == Ipv4Address("0.0.0.0"))
    {
        NS_LOG_WARN("Voice server address unavailable");
        return;
    }

    uint16_t port = 1000;
    uint16_t downlinkPort = 2000;
    const int phoneINTERVAL = 20;
    const int phoneMAXPACKETS = 1000000;
    const int phonePACKETSIZE = 60;
    for(uint32_t i=0; i<terms.size(); i++){
        if(m_termData[i].use_appli != 3){
            continue;
        }
        Ptr<Node> client = terms[i];
        if (client == nullptr)
        {
            continue;
        }

        Ipv4Address clientAddress = GetActiveIpv4(i);
        if (clientAddress == Ipv4Address("0.0.0.0"))
        {
            NS_LOG_WARN("Voice terminal " << i << " has no IPv4 address");
            continue;
        }

        PacketSinkHelper packetsh("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer serverApps;
        serverApps.Add(packetsh.Install(server_udpVoice));
        UdpEchoClientHelper udpClient(voiceServerAddress, port);
        udpClient.SetAttribute("Interval", TimeValue(MilliSeconds(phoneINTERVAL)));
        udpClient.SetAttribute("MaxPackets", UintegerValue(phoneMAXPACKETS));
        udpClient.SetAttribute("PacketSize", UintegerValue(phonePACKETSIZE));

        ApplicationContainer clientApps;
        clientApps.Add(udpClient.Install(client));
        serverApps.Start(Seconds(1.0));
        clientApps.Start(Seconds(1.0));

        // Downlink: server -> client voice stream (fixed 60B every ~20ms)
        PacketSinkHelper downlinkSink("ns3::UdpSocketFactory",
                                      InetSocketAddress(Ipv4Address::GetAny(), downlinkPort));
        ApplicationContainer downlinkApps = downlinkSink.Install(client);
        OnOffHelper downlinkSrc("ns3::UdpSocketFactory",
                                InetSocketAddress(clientAddress, downlinkPort));
        downlinkSrc.SetAttribute("PacketSize", UintegerValue(phonePACKETSIZE));
        downlinkSrc.SetAttribute("DataRate", DataRateValue(DataRate("24000bps")));
        downlinkSrc.SetAttribute("OnTime",
                                 StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
        downlinkSrc.SetAttribute("OffTime",
                                 StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
        downlinkSrc.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer downlinkSrcApps = downlinkSrc.Install(server_udpVoice);
        downlinkApps.Start(Seconds(1.0));
        downlinkSrcApps.Start(Seconds(1.0));

        // アプリ追跡
        if (i < m_termAppStates.size())
        {
            m_termAppStates[i].appType = 3;
            m_termAppStates[i].primaryPort = port;
            m_termAppStates[i].secondaryPort = downlinkPort;
            if (clientApps.GetN() > 0)
            {
                m_termAppStates[i].clientApps.push_back(clientApps.Get(0));
            }
            if (serverApps.GetN() > 0)
            {
                m_termAppStates[i].serverApps.push_back(serverApps.Get(0));
            }
            if (downlinkSrcApps.GetN() > 0)
            {
                m_termAppStates[i].serverApps.push_back(downlinkSrcApps.Get(0));
            }
            if (downlinkApps.GetN() > 0)
            {
                m_termAppStates[i].clientApps.push_back(downlinkApps.Get(0));
            }
        }

        port++;
        downlinkPort++;
    }
}

// オンラインゲームアプリケーションの設定
void NetSim::SetOnlineGameApp()
{
    NS_LOG_LOGIC("install online-game apps");

    if (server_onlineGame == nullptr)
    {
        NS_LOG_WARN("Online game server node is not configured");
        return;
    }

    Ipv4Address serverAddress = GetPrimaryIpv4(server_onlineGame);
    if (serverAddress == Ipv4Address("0.0.0.0"))
    {
        NS_LOG_WARN("Online game server address unavailable");
        return;
    }

    // MOBA想定: 双方向の小パケットを常時一定レートで送受信
    const uint16_t uplinkBasePort = 13000;
    const uint16_t downlinkBasePort = 13400;
    const Time startTime = Seconds(1.0);
    const Time stopTime = m_simulationDuration.IsZero() ? Seconds(7.0) : m_simulationDuration;
    const Time tickInterval = MilliSeconds(33);
    const uint32_t packetSizeBytes = 200;
    const uint64_t dataRateBps =
        static_cast<uint64_t>((static_cast<double>(packetSizeBytes) * 8.0) / tickInterval.GetSeconds());

    bool installedAny = false;

    for (uint32_t i = 0; i < terms.size(); ++i)
    {
        if (i >= m_termData.size() || m_termData[i].use_appli != 4)
        {
            continue;
        }

        Ptr<Node> client = terms[i];
        if (client == nullptr)
        {
            continue;
        }

        Ipv4Address clientAddress = GetActiveIpv4(i);
        if (clientAddress == Ipv4Address("0.0.0.0"))
        {
            NS_LOG_WARN("Online game terminal " << i << " has no IPv4 address");
            continue;
        }

        const uint16_t uplinkPort = uplinkBasePort + static_cast<uint16_t>(i);
        const uint16_t downlinkPort = downlinkBasePort + static_cast<uint16_t>(i);

        PacketSinkHelper uplinkSink("ns3::UdpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), uplinkPort));
        ApplicationContainer uplinkSinkApps = uplinkSink.Install(server_onlineGame);
        uplinkSinkApps.Start(startTime);
        uplinkSinkApps.Stop(stopTime);

        OnOffHelper uplinkSrc("ns3::UdpSocketFactory",
                              InetSocketAddress(serverAddress, uplinkPort));
        uplinkSrc.SetAttribute("PacketSize", UintegerValue(packetSizeBytes));
        uplinkSrc.SetAttribute("DataRate", DataRateValue(DataRate(dataRateBps)));
        uplinkSrc.SetAttribute("OnTime",
                               StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
        uplinkSrc.SetAttribute("OffTime",
                               StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
        uplinkSrc.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer uplinkSrcApps = uplinkSrc.Install(client);
        uplinkSrcApps.Start(startTime);
        uplinkSrcApps.Stop(stopTime);

        PacketSinkHelper downlinkSink("ns3::UdpSocketFactory",
                                      InetSocketAddress(Ipv4Address::GetAny(), downlinkPort));
        ApplicationContainer downlinkSinkApps = downlinkSink.Install(client);
        downlinkSinkApps.Start(startTime);
        downlinkSinkApps.Stop(stopTime);

        OnOffHelper downlinkSrc("ns3::UdpSocketFactory",
                                InetSocketAddress(clientAddress, downlinkPort));
        downlinkSrc.SetAttribute("PacketSize", UintegerValue(packetSizeBytes));
        downlinkSrc.SetAttribute("DataRate", DataRateValue(DataRate(dataRateBps)));
        downlinkSrc.SetAttribute("OnTime",
                                 StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
        downlinkSrc.SetAttribute("OffTime",
                                 StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
        downlinkSrc.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer downlinkSrcApps = downlinkSrc.Install(server_onlineGame);
        downlinkSrcApps.Start(startTime);
        downlinkSrcApps.Stop(stopTime);

        // アプリ追跡
        if (i < m_termAppStates.size())
        {
            m_termAppStates[i].appType = 4;
            m_termAppStates[i].primaryPort = uplinkPort;
            m_termAppStates[i].secondaryPort = downlinkPort;
            if (uplinkSrcApps.GetN() > 0)
            {
                m_termAppStates[i].clientApps.push_back(uplinkSrcApps.Get(0));
            }
            if (uplinkSinkApps.GetN() > 0)
            {
                m_termAppStates[i].serverApps.push_back(uplinkSinkApps.Get(0));
            }
            if (downlinkSrcApps.GetN() > 0)
            {
                m_termAppStates[i].serverApps.push_back(downlinkSrcApps.Get(0));
            }
            if (downlinkSinkApps.GetN() > 0)
            {
                m_termAppStates[i].clientApps.push_back(downlinkSinkApps.Get(0));
            }
        }

        installedAny = true;
        NS_LOG_LOGIC("online-game configured for terminal " << i);
    }

    if (!installedAny)
    {
        NS_LOG_INFO("No online-game terminals configured for appId=4");
    }
}


void NetSim::BuildTerminalIpMap()
{
    m_terminalIpAddresses.resize(terms.size(), Ipv4Address("0.0.0.0"));
    for (uint32_t i = 0; i < terms.size(); ++i)
    {
        if (i < m_termAccessState.size())
        {
            m_terminalIpAddresses[i] = GetActiveIpv4(i);
            continue;
        }
        Ptr<Node> node = terms[i];
        if (node == nullptr)
        {
            continue;
        }
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (ipv4 == nullptr)
        {
            continue;
        }
        for (uint32_t ifIndex = 0; ifIndex < ipv4->GetNInterfaces(); ++ifIndex)
        {
            for (uint32_t a = 0; a < ipv4->GetNAddresses(ifIndex); ++a)
            {
                Ipv4InterfaceAddress ifaddr = ipv4->GetAddress(ifIndex, a);
                if (ifaddr.GetLocal() != Ipv4Address("127.0.0.1"))
                {
                    m_terminalIpAddresses[i] = ifaddr.GetLocal();
                    goto next_term;
                }
            }
        }
        next_term:;
    }
}

void NetSim::ResetTerminalFlowStats()
{
    // TPは「サイクル固定窓」平均で評価するため、窓開始時刻を保持する。
    m_terminalTpWindowStart = Simulator::Now();
    if (m_termFlowMonitor != nullptr)
    {
        m_termFlowMonitor->ResetAllStats();
    }
}

void NetSim::CollectTerminalThroughput()
{
    if (m_kamedaServer == nullptr || m_termFlowMonitor == nullptr ||
        m_termFlowClassifier == nullptr)
    {
        return;
    }

    m_termFlowMonitor->CheckForLostPackets();
    auto stats = m_termFlowMonitor->GetFlowStats();

    // 各端末宛の受信ビット数を集計
    struct TermStats
    {
        double totalBits = 0.0;
    };
    std::vector<TermStats> perTerm(terms.size());

    const double fixedWindowSec = (Simulator::Now() - m_terminalTpWindowStart).GetSeconds();
    if (fixedWindowSec <= 0.0)
    {
        return;
    }

    // このサイクル時点の端末IPを一度だけ逆引き表にまとめる。
    std::unordered_map<uint32_t, uint32_t> ipToTermIndex;
    auto addTerminalAddress = [&ipToTermIndex](uint32_t termIdx, const Ipv4Address& addr) {
        if (addr == Ipv4Address("0.0.0.0") || addr == Ipv4Address("127.0.0.1"))
        {
            return;
        }

        // 既存実装は端末番号の小さい順に最初に一致した端末を採用していたため、
        // 重複IPが万一あっても先に登録された端末を優先する。
        ipToTermIndex.emplace(addr.Get(), termIdx);
    };

    for (uint32_t i = 0; i < terms.size(); ++i)
    {
        if (i < m_terminalIpAddresses.size())
        {
            addTerminalAddress(i, m_terminalIpAddresses[i]);
        }

        if (i < m_termAccessState.size())
        {
            const TermAccessState& state = m_termAccessState[i];
            addTerminalAddress(i, state.nrIpv4);
            for (const auto& wifiAddr : state.wifiIpv4)
            {
                addTerminalAddress(i, wifiAddr);
            }
        }

        if (terms[i] != nullptr)
        {
            Ptr<Ipv4> ipv4 = terms[i]->GetObject<Ipv4>();
            if (ipv4 != nullptr)
            {
                for (uint32_t ifIndex = 0; ifIndex < ipv4->GetNInterfaces(); ++ifIndex)
                {
                    for (uint32_t a = 0; a < ipv4->GetNAddresses(ifIndex); ++a)
                    {
                        addTerminalAddress(i, ipv4->GetAddress(ifIndex, a).GetLocal());
                    }
                }
            }
        }
    }

    for (const auto& kv : stats)
    {
        const FlowId flowId = kv.first;
        const FlowMonitor::FlowStats& fs = kv.second;
        if (fs.rxPackets == 0)
        {
            continue;
        }

        Ipv4FlowClassifier::FiveTuple tuple =
            m_termFlowClassifier->FindFlow(flowId);

        double bits = static_cast<double>(fs.rxBytes) * 8.0;

        // フローの宛先が端末であればその端末のTPに加算
        auto termIt = ipToTermIndex.find(tuple.destinationAddress.Get());
        if (termIt != ipToTermIndex.end() && termIt->second < perTerm.size())
        {
            perTerm[termIt->second].totalBits += bits;
        }
    }

    // サイクルごとのフロー詳細をCSVに書き出す
    if (kEnableFlowCycleDiagnostics)
    {
        const uint32_t cycleNum =
            static_cast<uint32_t>(Simulator::Now().GetSeconds() / m_cycleDuration.GetSeconds()) + 1;
        const std::string flowCsvPath = std::string(OUTPUT_DIR) + "flow_per_cycle.csv";
        bool writeHeader = false;
        {
            std::ifstream chk(flowCsvPath);
            writeHeader = (!chk.good() || chk.peek() == std::ifstream::traits_type::eof());
        }
        std::ofstream ofs(flowCsvPath, std::ios::app);
        if (ofs.good())
        {
            if (writeHeader)
            {
                ofs << "cycle,time_s,term_idx,term_ip,flow_id,src,dst,"
                    << "tx_bytes,tx_packets,rx_bytes,rx_packets,lost_packets,"
                    << "time_first_tx_s,time_first_rx_s,time_last_tx_s,time_last_rx_s,"
                    << "tp_mbps,diagnosis\n";
            }
            for (const auto& kv : stats)
            {
                const FlowId flowId = kv.first;
                const FlowMonitor::FlowStats& fs = kv.second;
                Ipv4FlowClassifier::FiveTuple tuple = m_termFlowClassifier->FindFlow(flowId);
                auto termIt = ipToTermIndex.find(tuple.destinationAddress.Get());
                if (termIt == ipToTermIndex.end() || termIt->second >= perTerm.size())
                {
                    continue;
                }
                uint32_t i = termIt->second;
                    double tpMbps = (fs.rxBytes > 0 && fixedWindowSec > 0.0)
                                        ? (static_cast<double>(fs.rxBytes) * 8.0 / fixedWindowSec / 1e6)
                                        : 0.0;
                    const char* diagnosis = "received";
                    if (fs.txPackets == 0)
                    {
                        diagnosis = "no_tx";
                    }
                    else if (fs.rxPackets == 0)
                    {
                        diagnosis = (fs.lostPackets > 0) ? "lost_or_unrouted" : "tx_no_rx_yet";
                    }
                    else if (fs.timeFirstRxPacket < m_terminalTpWindowStart)
                    {
                        diagnosis = "rx_before_window";
                    }
                    ofs << cycleNum << ","
                        << std::fixed << std::setprecision(2) << Simulator::Now().GetSeconds() << ","
                        << (i + 1) << ","
                        << tuple.destinationAddress << ","
                        << flowId << ","
                        << tuple.sourceAddress << ","
                        << tuple.destinationAddress << ","
                        << fs.txBytes << ","
                        << fs.txPackets << ","
                        << fs.rxBytes << ","
                        << fs.rxPackets << ","
                        << fs.lostPackets << ","
                        << fs.timeFirstTxPacket.GetSeconds() << ","
                        << fs.timeFirstRxPacket.GetSeconds() << ","
                        << fs.timeLastTxPacket.GetSeconds() << ","
                        << fs.timeLastRxPacket.GetSeconds() << ","
                        << tpMbps << ","
                        << diagnosis << "\n";
            }
        }
    }

    // 各端末のTPを計算してサーバーに通知
    for (uint32_t i = 0; i < perTerm.size(); ++i)
    {
        if (i >= m_termData.size())
        {
            continue;
        }
        // 切替直後の端末はTP計測をスキップ
        if (i < m_termAccessState.size())
        {
            Time elapsed = Simulator::Now() - m_termAccessState[i].lastSwitchTime;
            if (m_termAccessState[i].lastSwitchTime.IsPositive() && elapsed < Seconds(0.5))
            {
                continue;
            }
        }

        double tpBps = (perTerm[i].totalBits > 0.0)
                           ? (perTerm[i].totalBits / fixedWindowSec)
                           : 0.0;
        m_kamedaServer->SetTerminalTp(static_cast<int>(i), tpBps);
    }
}

void NetSim::RebindTerminalApps(uint32_t termIdx, Ipv4Address newIp)
{
    if (termIdx >= m_termAppStates.size() || termIdx >= terms.size())
    {
        return;
    }

    TermAppState& appState = m_termAppStates[termIdx];
    if (appState.appType == 1)
    {
        for (auto& app : appState.serverApps)
        {
            if (app)
            {
                app->SetStopTime(Seconds(0));
            }
        }
        appState.serverApps.clear();
        appState.browserGeneration++;
        // ハンドオーバー直後はARPが安定するまでバースト送信を遅らせる（案2）
        Time postHandoverStart = Simulator::Now() + m_browserPostHandoverDelay;
        ScheduleFutureBrowserBursts(termIdx, appState.primaryPort, appState.browserGeneration,
                                    postHandoverStart);

        return;
    }

    // 旧アプリを停止（実行中アプリに効かせるため SetStopTime を直接使用）
    const Time stopDelay = MilliSeconds(1);
    for (auto& app : appState.clientApps)
    {
        if (app)
        {
            app->SetStopTime(stopDelay);
        }
    }
    for (auto& app : appState.serverApps)
    {
        if (app)
        {
            app->SetStopTime(stopDelay);
        }
    }
    appState.clientApps.clear();
    appState.serverApps.clear();

    // 新アプリを再インストール（Start/Stop は再インストール時点からの相対時刻）
    Time now = Simulator::Now();
    Time startDelay = MilliSeconds(50);
    Time remaining = m_simulationDuration - now;
    if (remaining <= startDelay)
    {
        return;
    }
    Time runDuration = remaining;

    switch (appState.appType)
    {
    case 1:
        ReinstallBrowserApp(termIdx, newIp, startDelay, runDuration);
        break;
    case 2:
        ReinstallVideoApp(termIdx, newIp, startDelay, runDuration);
        break;
    case 3:
        ReinstallVoiceApp(termIdx, newIp, startDelay, runDuration);
        break;
    case 4:
        ReinstallOnlineGameApp(termIdx, newIp, startDelay, runDuration);
        break;
    default:
        break;
    }
}

void NetSim::ReinstallBrowserApp(uint32_t termIdx, Ipv4Address newIp, Time startTime, Time stopTime)
{
    if (termIdx >= terms.size() || server_browser == nullptr)
    {
        return;
    }

    Ptr<Node> client = terms[termIdx];
    if (client == nullptr || newIp == Ipv4Address("0.0.0.0"))
    {
        return;
    }

    uint16_t port = m_termAppStates[termIdx].primaryPort;

    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sinkHelper.Install(client);
    sinkApps.Start(startTime);
    sinkApps.Stop(stopTime);

    // Browser request events are installed once in SetBrowserApp().  Each event
    // resolves the terminal's active IP at execution time, so handover only
    // needs a fresh sink and must not add duplicate future source events.

    if (sinkApps.GetN() > 0)
    {
        m_termAppStates[termIdx].clientApps.push_back(sinkApps.Get(0));
    }

    // Reinstall details are intentionally not printed to keep console logs concise.
}

void NetSim::ReinstallVideoApp(uint32_t termIdx, Ipv4Address newIp, Time startTime, Time stopTime)
{
    if (termIdx >= terms.size() || server_udpVideo == nullptr)
    {
        return;
    }

    Ptr<Node> client = terms[termIdx];
    if (client == nullptr || newIp == Ipv4Address("0.0.0.0"))
    {
        return;
    }

    uint16_t port = m_termAppStates[termIdx].primaryPort;

    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApps = sinkHelper.Install(client);
    sinkApps.Start(startTime);
    sinkApps.Stop(stopTime);

    std::string traceFile = std::string(INPUT_DIR) + "YouTube1080p_2min.dat";
    UdpTraceClientHelper udpClient(newIp, port, traceFile);
    ApplicationContainer serverApps = udpClient.Install(server_udpVideo);
    serverApps.Start(startTime);
    serverApps.Stop(stopTime);

    if (sinkApps.GetN() > 0)
    {
        m_termAppStates[termIdx].clientApps.push_back(sinkApps.Get(0));
    }
    if (serverApps.GetN() > 0)
    {
        m_termAppStates[termIdx].serverApps.push_back(serverApps.Get(0));
    }

    // Reinstall details are intentionally not printed to keep console logs concise.
}

void NetSim::ReinstallVoiceApp(uint32_t termIdx, Ipv4Address newIp, Time startTime, Time stopTime)
{
    if (termIdx >= terms.size() || server_udpVoice == nullptr)
    {
        return;
    }

    Ptr<Node> client = terms[termIdx];
    if (client == nullptr || newIp == Ipv4Address("0.0.0.0"))
    {
        return;
    }

    uint16_t port = m_termAppStates[termIdx].primaryPort;
    uint16_t downlinkPort = m_termAppStates[termIdx].secondaryPort;

    Ipv4Address voiceServerAddress = GetPrimaryIpv4(server_udpVoice);
    if (voiceServerAddress == Ipv4Address("0.0.0.0"))
    {
        return;
    }

    const int phoneINTERVAL = 20;
    const int phoneMAXPACKETS = 1000000;
    const int phonePACKETSIZE = 60;

    // Uplink: Echo client -> server
    PacketSinkHelper packetsh("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer serverApps = packetsh.Install(server_udpVoice);
    UdpEchoClientHelper udpClient(voiceServerAddress, port);
    udpClient.SetAttribute("Interval", TimeValue(MilliSeconds(phoneINTERVAL)));
    udpClient.SetAttribute("MaxPackets", UintegerValue(phoneMAXPACKETS));
    udpClient.SetAttribute("PacketSize", UintegerValue(phonePACKETSIZE));
    ApplicationContainer clientApps = udpClient.Install(client);
    serverApps.Start(startTime);
    clientApps.Start(startTime);

    // Downlink: server -> client
    PacketSinkHelper downlinkSink("ns3::UdpSocketFactory",
                                  InetSocketAddress(Ipv4Address::GetAny(), downlinkPort));
    ApplicationContainer downlinkApps = downlinkSink.Install(client);
    OnOffHelper downlinkSrc("ns3::UdpSocketFactory",
                            InetSocketAddress(newIp, downlinkPort));
    downlinkSrc.SetAttribute("PacketSize", UintegerValue(phonePACKETSIZE));
    downlinkSrc.SetAttribute("DataRate", DataRateValue(DataRate("24000bps")));
    downlinkSrc.SetAttribute("OnTime",
                             StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
    downlinkSrc.SetAttribute("OffTime",
                             StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
    downlinkSrc.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer downlinkSrcApps = downlinkSrc.Install(server_udpVoice);
    downlinkApps.Start(startTime);
    downlinkSrcApps.Start(startTime);

    if (clientApps.GetN() > 0)
    {
        m_termAppStates[termIdx].clientApps.push_back(clientApps.Get(0));
    }
    if (serverApps.GetN() > 0)
    {
        m_termAppStates[termIdx].serverApps.push_back(serverApps.Get(0));
    }
    if (downlinkSrcApps.GetN() > 0)
    {
        m_termAppStates[termIdx].serverApps.push_back(downlinkSrcApps.Get(0));
    }
    if (downlinkApps.GetN() > 0)
    {
        m_termAppStates[termIdx].clientApps.push_back(downlinkApps.Get(0));
    }

    // Reinstall details are intentionally not printed to keep console logs concise.
}

void NetSim::ReinstallOnlineGameApp(uint32_t termIdx, Ipv4Address newIp, Time startTime, Time stopTime)
{
    if (termIdx >= terms.size() || server_onlineGame == nullptr)
    {
        return;
    }

    Ptr<Node> client = terms[termIdx];
    if (client == nullptr || newIp == Ipv4Address("0.0.0.0"))
    {
        return;
    }

    uint16_t uplinkPort = m_termAppStates[termIdx].primaryPort;
    uint16_t downlinkPort = m_termAppStates[termIdx].secondaryPort;

    Ipv4Address serverAddress = GetPrimaryIpv4(server_onlineGame);
    if (serverAddress == Ipv4Address("0.0.0.0"))
    {
        return;
    }

    const uint32_t packetSizeBytes = 200;
    const Time tickInterval = MilliSeconds(33);
    const uint64_t dataRateBps =
        static_cast<uint64_t>((static_cast<double>(packetSizeBytes) * 8.0) / tickInterval.GetSeconds());

    // Uplink: client -> server
    PacketSinkHelper uplinkSink("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), uplinkPort));
    ApplicationContainer uplinkSinkApps = uplinkSink.Install(server_onlineGame);
    uplinkSinkApps.Start(startTime);
    uplinkSinkApps.Stop(stopTime);

    OnOffHelper uplinkSrc("ns3::UdpSocketFactory",
                          InetSocketAddress(serverAddress, uplinkPort));
    uplinkSrc.SetAttribute("PacketSize", UintegerValue(packetSizeBytes));
    uplinkSrc.SetAttribute("DataRate", DataRateValue(DataRate(dataRateBps)));
    uplinkSrc.SetAttribute("OnTime",
                           StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
    uplinkSrc.SetAttribute("OffTime",
                           StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
    uplinkSrc.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer uplinkSrcApps = uplinkSrc.Install(client);
    uplinkSrcApps.Start(startTime);
    uplinkSrcApps.Stop(stopTime);

    // Downlink: server -> client
    PacketSinkHelper downlinkSink("ns3::UdpSocketFactory",
                                  InetSocketAddress(Ipv4Address::GetAny(), downlinkPort));
    ApplicationContainer downlinkSinkApps = downlinkSink.Install(client);
    downlinkSinkApps.Start(startTime);
    downlinkSinkApps.Stop(stopTime);

    OnOffHelper downlinkSrc("ns3::UdpSocketFactory",
                            InetSocketAddress(newIp, downlinkPort));
    downlinkSrc.SetAttribute("PacketSize", UintegerValue(packetSizeBytes));
    downlinkSrc.SetAttribute("DataRate", DataRateValue(DataRate(dataRateBps)));
    downlinkSrc.SetAttribute("OnTime",
                             StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
    downlinkSrc.SetAttribute("OffTime",
                             StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
    downlinkSrc.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer downlinkSrcApps = downlinkSrc.Install(server_onlineGame);
    downlinkSrcApps.Start(startTime);
    downlinkSrcApps.Stop(stopTime);

    if (uplinkSrcApps.GetN() > 0)
    {
        m_termAppStates[termIdx].clientApps.push_back(uplinkSrcApps.Get(0));
    }
    if (uplinkSinkApps.GetN() > 0)
    {
        m_termAppStates[termIdx].serverApps.push_back(uplinkSinkApps.Get(0));
    }
    if (downlinkSrcApps.GetN() > 0)
    {
        m_termAppStates[termIdx].serverApps.push_back(downlinkSrcApps.Get(0));
    }
    if (downlinkSinkApps.GetN() > 0)
    {
        m_termAppStates[termIdx].clientApps.push_back(downlinkSinkApps.Get(0));
    }

    // Reinstall details are intentionally not printed to keep console logs concise.
}

} // namespace ns3
