#include "NetSim.h"

NS_LOG_COMPONENT_DEFINE("NetSimApplications");

namespace ns3 {
namespace {

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

void PingRtt (uint16_t seq, Time rtt)
{
    std::string filename;
    if(G_nth == 1){
        filename = std::string(OUTPUT_DIR) + "outputData_1st.txt";
    }else if(G_nth == 2){
        filename = std::string(OUTPUT_DIR) + "outputData_2nd.txt";
    }else if(G_nth == 3){
        filename = std::string(OUTPUT_DIR) + "outputData_hungarian.txt";
    }else{
        std::cerr << "nth error in PingRtt" << std::endl;
    }

    std::ofstream ofs(filename, std::ios::app);
    ofs << seq << " " << rtt.GetMilliSeconds() << std::endl;
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

void WebMeetingRttTrace(const Time& rtt)
{
    NS_LOG_INFO("Webmeeting RTT " << rtt.GetMilliSeconds() << " ms");
}

void ScheduleBrowserRequest(Ptr<Node> client,
                            Ipv4Address serverAddress,
                            uint16_t port,
                            uint32_t requestIndex,
                            uint32_t totalRequests,
                            Time interval,
                            Time duration,
                            uint32_t maxBytes)
{
    if (client == nullptr || requestIndex >= totalRequests)
    {
        return;
    }
    BulkSendHelper bulk("ns3::TcpSocketFactory", InetSocketAddress(serverAddress, port));
    bulk.SetAttribute("MaxBytes", UintegerValue(maxBytes));
    ApplicationContainer apps = bulk.Install(client);
    Time now = Simulator::Now();
    apps.Start(now);
    apps.Stop(now + duration);

    if (requestIndex + 1 < totalRequests)
    {
        Simulator::Schedule(interval,
                            &ScheduleBrowserRequest,
                            client,
                            serverAddress,
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
    if (monitor == nullptr || apId >= wifiAPs.size())
    {
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
    monitorApp->SetStartTime(Seconds(1.0));
    monitorApp->SetStopTime(Seconds(6.0));
}

void NetSim::SetAppLayer(){
    NS_LOG_FUNCTION(this);

    SetGreedy();
    SetKamedaModule();
    SetBrowserApp(); // ブラウザ, アプリ番号1, TP
    SetVideoApp(); // 動画, アプリ番号2, TP
    SetVoiceApp(); // 通話, アプリ番号3, RTT
    SetWebmeetingApp(); //Web会議, アプリ番号4, RTT
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
    const uint32_t requestBytes = 750u * 1024u;
    const Time requestDuration = Seconds(0.5);
    const Time firstRequest = Seconds(1.0);
    const Time sinkStop = firstRequest + interval * requestCount + requestDuration;
    const uint16_t basePort = 15000;
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

        const uint16_t port = basePort + static_cast<uint16_t>(i);

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApps = sinkHelper.Install(server_browser);
        sinkApps.Start(Seconds(0.9));
        sinkApps.Stop(sinkStop);

        Simulator::Schedule(firstRequest,
                            &ScheduleBrowserRequest,
                            client,
                            serverAddress,
                            port,
                            0,
                            requestCount,
                            interval,
                            requestDuration,
                            requestBytes);
        installedAny = true;
        NS_LOG_LOGIC("browser app configured for terminal " << i << " port " << port);
    }

    if (!installedAny)
    {
        NS_LOG_INFO("No browser terminals configured for appId=1");
    }
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
    remote_host->AddApplication(appServer);
    appServer->SetStartTime(Seconds(1.0));
    appServer->SetStopTime(Seconds(7.0));

    NS_LOG_LOGIC("install RTT forwarder on server_rtt");
    Ptr<RttForwarderApp> forwarder = CreateObject<RttForwarderApp>();
    if (m_remoteHostAddress != Ipv4Address("0.0.0.0"))
    {
        forwarder->SetRemote(m_remoteHostAddress, 8080);
    }
    forwarder->SetListeningPort(9000);
    server_rtt->AddApplication(forwarder);
    forwarder->SetStartTime(Seconds(0.9));
    forwarder->SetStopTime(Seconds(7.0));

    if (monitorTerminals.size() < 3) {
        monitorTerminals.resize(3, nullptr);
    }

    for(uint32_t apId = 0; apId < monitorTerminals.size(); apId++) {
        Ptr<Node> monitor = monitorTerminals[apId];
        AttachMonitorApplication(apId, monitor);
    }
}

void NetSim::SetVideoApp(void){

    NS_LOG_LOGIC("install video apps");

    Ipv4Address videoServerAddress = GetPrimaryIpv4(server_udpVideo);
    if (videoServerAddress == Ipv4Address("0.0.0.0"))
    {
        NS_LOG_WARN("Video server address unavailable");
        return;
    }

    uint16_t multicast_port = 10000;
    for(uint32_t i=0; i<terms.size(); i++){
        if(m_termData[i].use_appli != 2){
            continue;
        }
        UdpServerHelper udpServer;
        ApplicationContainer serverApps;
        udpServer = UdpServerHelper(multicast_port);
        serverApps = udpServer.Install(server_udpVideo);

        std::string traceFile = std::string(INPUT_DIR) + "Verbose_Jurassic.dat";
        UdpTraceClientHelper udpClient(videoServerAddress, multicast_port, traceFile);

        ApplicationContainer videoClientApps;
        videoClientApps.Add(udpClient.Install(terms[i]));
        serverApps.Start(Seconds(1.0));
        videoClientApps.Start(Seconds(1.0));
        multicast_port++;
    }
}

void NetSim::SetVoiceApp(void){

    NS_LOG_LOGIC("install voice apps");

    Ipv4Address voiceServerAddress = GetPrimaryIpv4(server_udpVoice);
    if (voiceServerAddress == Ipv4Address("0.0.0.0"))
    {
        NS_LOG_WARN("Voice server address unavailable");
        return;
    }

    uint16_t port = 1000;
    const int phoneINTERVAL = 20;
    const int phoneMAXPACKETS = 1000000;
    const int phonePACKETSIZE = 60;
    for(uint32_t i=0; i<terms.size(); i++){
        if(m_termData[i].use_appli != 3){
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
        clientApps.Add(udpClient.Install(terms[i]));
        serverApps.Start(Seconds(1.0));
        clientApps.Start(Seconds(1.0));

        port++;
    }
}

void NetSim::SetWebmeetingApp()
{
    NS_LOG_LOGIC("install webmeeting apps");

    if (server_live == nullptr)
    {
        NS_LOG_WARN("Live streaming server node is not configured");
        return;
    }

    Ipv4Address serverAddress = GetPrimaryIpv4(server_live);
    if (serverAddress == Ipv4Address("0.0.0.0"))
    {
        NS_LOG_WARN("Live streaming server address unavailable");
        return;
    }

    const uint16_t videoUpBasePort = 13000;
    const uint16_t videoDownBasePort = 13400;
    const uint16_t audioBasePort = 14000;
    const Time startTime = Seconds(1.0);
    const Time downlinkStart = Seconds(1.05);
    Time stopTime = m_simulationDuration.IsZero() ? Seconds(7.0) : m_simulationDuration;

    Ptr<OutputStreamWrapper> uplinkStream;
    Ptr<OutputStreamWrapper> downlinkStream;
    AsciiTraceHelper asciiHelper;
    if (m_enableWebmeetingTracing)
    {
        uplinkStream = asciiHelper.CreateFileStream(OUTPUT_DIR + "webmeeting_video-uplink.tr");
        downlinkStream = asciiHelper.CreateFileStream(OUTPUT_DIR + "webmeeting_video-downlink.tr");
    }

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

        Ipv4Address clientAddress = GetPrimaryIpv4(client);
        if (clientAddress == Ipv4Address("0.0.0.0"))
        {
            NS_LOG_WARN("Webmeeting terminal " << i << " has no IPv4 address");
            continue;
        }

        const uint16_t videoUpPort = videoUpBasePort + static_cast<uint16_t>(i);
        const uint16_t videoDownPort = videoDownBasePort + static_cast<uint16_t>(i);
        const uint16_t audioPort = audioBasePort + static_cast<uint16_t>(i);

        PacketSinkHelper videoUpSink("ns3::UdpSocketFactory",
                                     InetSocketAddress(Ipv4Address::GetAny(), videoUpPort));
        ApplicationContainer videoUpSinkApps = videoUpSink.Install(server_live);
        videoUpSinkApps.Start(startTime);
        videoUpSinkApps.Stop(stopTime);

        OnOffHelper videoUpClient("ns3::UdpSocketFactory",
                                  InetSocketAddress(serverAddress, videoUpPort));
        videoUpClient.SetAttribute("PacketSize", UintegerValue(1200));
        videoUpClient.SetAttribute("DataRate", DataRateValue(DataRate("1.2Mbps")));
        videoUpClient.SetAttribute("OnTime",
                                   StringValue("ns3::ConstantRandomVariable[Constant=0.95]"));
        videoUpClient.SetAttribute("OffTime",
                                   StringValue("ns3::ConstantRandomVariable[Constant=0.05]"));
        videoUpClient.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer videoUpClientApps = videoUpClient.Install(client);
        videoUpClientApps.Start(startTime);
        videoUpClientApps.Stop(stopTime);
        if (m_enableWebmeetingTracing && uplinkStream != nullptr && videoUpClientApps.GetN() > 0)
        {
            videoUpClientApps.Get(0)->TraceConnectWithoutContext(
                "Tx", MakeBoundCallback(&TracePacketToAscii, uplinkStream));
        }

        PacketSinkHelper videoDownSink("ns3::UdpSocketFactory",
                                       InetSocketAddress(Ipv4Address::GetAny(), videoDownPort));
        ApplicationContainer videoDownSinkApps = videoDownSink.Install(client);
        videoDownSinkApps.Start(startTime);
        videoDownSinkApps.Stop(stopTime);

        OnOffHelper videoDownServer("ns3::UdpSocketFactory",
                                    InetSocketAddress(clientAddress, videoDownPort));
        videoDownServer.SetAttribute("PacketSize", UintegerValue(1200));
        videoDownServer.SetAttribute("DataRate", DataRateValue(DataRate("1.5Mbps")));
        videoDownServer.SetAttribute("OnTime",
                                     StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
        videoDownServer.SetAttribute("OffTime",
                                     StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
        videoDownServer.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer videoDownServerApps = videoDownServer.Install(server_live);
        videoDownServerApps.Start(downlinkStart);
        videoDownServerApps.Stop(stopTime);
        if (m_enableWebmeetingTracing && downlinkStream != nullptr &&
            videoDownServerApps.GetN() > 0)
        {
            videoDownServerApps.Get(0)->TraceConnectWithoutContext(
                "Tx", MakeBoundCallback(&TracePacketToAscii, downlinkStream));
        }

        UdpEchoServerHelper audioServer(audioPort);
        ApplicationContainer audioServerApps = audioServer.Install(server_live);
        audioServerApps.Start(startTime);
        audioServerApps.Stop(stopTime);

        UdpEchoClientHelper audioClient(serverAddress, audioPort);
        audioClient.SetAttribute("MaxPackets", UintegerValue(0));
        audioClient.SetAttribute("Interval", TimeValue(MilliSeconds(20)));
        audioClient.SetAttribute("PacketSize", UintegerValue(160));
        ApplicationContainer audioClientApps = audioClient.Install(client);
        audioClientApps.Start(startTime);
        audioClientApps.Stop(stopTime);

        installedAny = true;
        NS_LOG_LOGIC("webmeeting configured for terminal " << i);
    }

    if (installedAny)
    {
        if (!Config::ConnectWithoutContextFailSafe(
                "/NodeList/*/ApplicationList/*/$ns3::UdpEchoClient/Rtt",
                MakeCallback(&WebMeetingRttTrace)))
        {
            NS_LOG_WARN("Failed to connect Webmeeting RTT trace source");
        }
    }
    else
    {
        NS_LOG_INFO("No webmeeting terminals configured for appId=4");
    }
}




} // namespace ns3
