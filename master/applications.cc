#include "NetSim.h"

NS_LOG_COMPONENT_DEFINE("NetSimApplications");

namespace ns3 {
namespace {

// ポート番号の定数
constexpr uint16_t kRttForwarderListenPort = 9000;
constexpr uint16_t kRttForwarderRemotePort = 8080;
constexpr uint16_t kBrowserBasePort        = 15000;

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

void TracePacketToAscii(Ptr<OutputStreamWrapper> stream, Ptr<const Packet> packet)
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

void ScheduleBrowserDownload(Ptr<Node> server,
                             Ipv4Address clientAddress,
                             uint16_t port,
                             uint32_t requestIndex,
                             uint32_t totalRequests,
                             Time interval,
                             Time duration,
                             uint32_t maxBytes)
{
    if (server == nullptr || clientAddress == Ipv4Address("0.0.0.0") ||
        requestIndex >= totalRequests)
    {
        return;
    }
    BulkSendHelper bulk("ns3::TcpSocketFactory", InetSocketAddress(clientAddress, port));
    bulk.SetAttribute("MaxBytes", UintegerValue(maxBytes));
    ApplicationContainer apps = bulk.Install(server);
    Time now = Simulator::Now();
    apps.Start(now);
    // BulkSend has finite MaxBytes, so forcing an early Stop can close TCP with
    // unsent data and leave internal timers firing on an empty tx item.
    // Let the app/socket finish naturally when MaxBytes is sent.
    (void)duration;

    if (requestIndex + 1 < totalRequests)
    {
        Simulator::Schedule(interval,
                            &ScheduleBrowserDownload,
                            server,
                            clientAddress,
                            port,
                            requestIndex + 1,
                            totalRequests,
                            interval,
                            duration,
                            maxBytes);
    }
}

} // namespace

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
    monitorApp->SetStartTime(Seconds(1.5));
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
    const uint32_t requestBytes = 500u * 1024u;
    const Time requestDuration = Seconds(0.5);
    const Time firstRequest = Seconds(1.0);
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
        Ipv4Address clientAddress = (m_nth == 5) ? GetActiveIpv4(i) : GetPrimaryIpv4(client);
        if (clientAddress == Ipv4Address("0.0.0.0"))
        {
            NS_LOG_WARN("Browser terminal " << i << " has no IPv4 address");
            continue;
        }

        const uint16_t port = basePort + static_cast<uint16_t>(i);

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApps = sinkHelper.Install(client);
        sinkApps.Start(Seconds(0.9));
        sinkApps.Stop(sinkStop);

        for (uint32_t cycle = 0; cycle < cycles; ++cycle)
        {
            Time offset = m_cycleDuration * cycle;
            // Batch A: 1st request at t=1.0+offset
            Simulator::Schedule(firstRequest + offset,
                                &ScheduleBrowserDownload,
                                server_browser,
                                clientAddress,
                                port,
                                0,
                                1,
                                interval,
                                requestDuration,
                                requestBytes);
            // Batch B: 7 additional requests at t=2.1+offset, 0.6s interval
            Simulator::Schedule(Seconds(2.1) + offset,
                                &ScheduleBrowserDownload,
                                server_browser,
                                clientAddress,
                                port,
                                0,
                                requestCount - 1,
                                interval,
                                requestDuration,
                                requestBytes);
        }

        // アプリ追跡（nth==5用）
        if (m_nth == 5 && i < m_termAppStates.size())
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

        Ipv4Address clientAddress = (m_nth == 5) ? GetActiveIpv4(i) : GetPrimaryIpv4(client);
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

        // アプリ追跡（nth==5用）
        if (m_nth == 5 && i < m_termAppStates.size())
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

        Ipv4Address clientAddress = (m_nth == 5) ? GetActiveIpv4(i) : GetPrimaryIpv4(client);
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

        // アプリ追跡（nth==5用）
        if (m_nth == 5 && i < m_termAppStates.size())
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

        Ipv4Address clientAddress = (m_nth == 5) ? GetActiveIpv4(i) : GetPrimaryIpv4(client);
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

        // アプリ追跡（nth==5用）
        if (m_nth == 5 && i < m_termAppStates.size())
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
        if (m_nth == 5 && i < m_termAccessState.size())
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
        for (uint32_t i = 0; i < m_terminalIpAddresses.size(); ++i)
        {
            if (m_terminalIpAddresses[i] == Ipv4Address("0.0.0.0"))
            {
                continue;
            }
            if (tuple.destinationAddress != m_terminalIpAddresses[i])
            {
                continue;
            }

            perTerm[i].totalBits += bits;
            break;
        }
    }

    // 各端末のTPを計算してサーバーに通知
    for (uint32_t i = 0; i < perTerm.size(); ++i)
    {
        if (i >= m_termData.size())
        {
            continue;
        }
        // nth==5: 切替直後の端末はTP計測をスキップ
        if (m_nth == 5 && i < m_termAccessState.size())
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

    const uint32_t requestBytes = 500u * 1024u;
    const Time requestDuration = Seconds(0.5);
    const Time interval =
        m_browserRequestInterval.IsZero() ? Seconds(1.0) : m_browserRequestInterval;
    const uint32_t requestCount = std::max<uint32_t>(1, m_browserRequestCount);

    // 残りサイクル分のブラウザリクエストをスケジュール
    uint32_t remainingCycles = 0;
    Time now = Simulator::Now();
    for (uint32_t c = 0; c < m_cycleCount; ++c)
    {
        Time cycleStart = m_cycleDuration * c + Seconds(1.0);
        if (cycleStart > now)
        {
            remainingCycles++;
            Simulator::Schedule(cycleStart - now,
                                &ScheduleBrowserDownload,
                                server_browser,
                                newIp,
                                port,
                                0,
                                requestCount,
                                interval,
                                requestDuration,
                                requestBytes);
        }
    }

    // 直近のリクエストも投入
    if (remainingCycles == 0)
    {
        Simulator::Schedule(startTime,
                            &ScheduleBrowserDownload,
                            server_browser,
                            newIp,
                            port,
                            0,
                            requestCount,
                            interval,
                            requestDuration,
                            requestBytes);
    }

    if (sinkApps.GetN() > 0)
    {
        m_termAppStates[termIdx].clientApps.push_back(sinkApps.Get(0));
    }

    std::cout << "[ReinstallBrowser] term=" << termIdx << " newIP=" << newIp
              << " port=" << port
              << " startDelay=" << startTime.GetSeconds()
              << " stopDelay=" << stopTime.GetSeconds()
              << " now=" << Simulator::Now().GetSeconds()
              << std::endl;
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

    std::cout << "[ReinstallVideo] term=" << termIdx << " newIP=" << newIp
              << " port=" << port
              << " startDelay=" << startTime.GetSeconds()
              << " stopDelay=" << stopTime.GetSeconds()
              << " now=" << Simulator::Now().GetSeconds()
              << std::endl;
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

    std::cout << "[ReinstallVoice] term=" << termIdx << " newIP=" << newIp
              << " port=" << port << "/" << downlinkPort << std::endl;
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

    std::cout << "[ReinstallOnlineGame] term=" << termIdx << " newIP=" << newIp
              << " port=" << uplinkPort << "/" << downlinkPort << std::endl;
}

} // namespace ns3
