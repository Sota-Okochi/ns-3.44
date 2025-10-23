#include "NetSim.h"

NS_LOG_COMPONENT_DEFINE("NetSimTopology");

namespace ns3 {
namespace {

static const Ipv4Address UE_NET("7.0.0.0");
static const Ipv4Mask    UE_MASK("255.0.0.0");

inline void EnableIpForwardIfPresent(Ptr<Ipv4> ipv4)
{
    if (ipv4)
    {
        ipv4->SetAttribute("IpForward", BooleanValue(true));
    }
}

void DumpIpv4Info(const std::string& title, Ptr<Node> node)
{
    if (!node) return;
    std::cout << "[ROUTE] === " << title << " (Node " << node->GetId() << ") ===" << std::endl;
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (!ipv4)
    {
        std::cout << "[ROUTE] (no ipv4)" << std::endl;
        return;
    }
    for (uint32_t ifIndex = 0; ifIndex < ipv4->GetNInterfaces(); ++ifIndex)
    {
        for (uint32_t a = 0; a < ipv4->GetNAddresses(ifIndex); ++a)
        {
            Ipv4InterfaceAddress ifaddr = ipv4->GetAddress(ifIndex, a);
            std::cout << "[ROUTE] IF=" << ifIndex
                      << " addr=" << ifaddr.GetLocal()
                      << "/" << ifaddr.GetMask()
                      << " bcast=" << ifaddr.GetBroadcast()
                      << std::endl;
        }
    }
    Ipv4StaticRoutingHelper rh;
    Ptr<Ipv4StaticRouting> rt = rh.GetStaticRouting(ipv4);
    uint32_t nRoutes = rt->GetNRoutes();
    for (uint32_t r = 0; r < nRoutes; ++r)
    {
        Ipv4RoutingTableEntry e = rt->GetRoute(r);
        std::cout << "[ROUTE] route#" << r
                  << " dst=" << e.GetDest() << "/" << e.GetDestNetworkMask()
                  << " gw=" << e.GetGateway()
                  << " if=" << e.GetInterface()
                  << std::endl;
    }
}

} // namespace

void NetSim::CreateNetworkTopology(){
    std::cout << "==== CreateNetworkTopology ====" << std::endl;
    NS_LOG_FUNCTION(this);

    InitializeNodeContainers();
    CreateCerNode();
    CreateWifiApNodes();
    CreateMonitorNodes();
    CreateTerminalNodes();
    CreateRouterNodes();
    CreateServerNodes();
}

void NetSim::InitializeNodeContainers()
{
    p2pNodes.resize(APnum);
    wifiNodes.resize(APnum);
    p2pDevices.resize(APnum);
    wifiDevices.resize(APnum);
    m_routerCerNodes.resize(APnum);
    m_routerCerDevices.resize(APnum);
}

void NetSim::CreateCerNode()
{
    cerNode = CreateObject<Node>();
}

void NetSim::CreateWifiApNodes()
{
    wifiAPs.clear();
    for (uint32_t i = 0; i < APnum; ++i)
    {
        Ptr<Node> apNode = CreateObject<Node>();
        wifiNodes[i].Add(apNode);
        wifiAPs.push_back(apNode);
    }
}

void NetSim::CreateMonitorNodes()
{
    monitorTerminals.assign(3, nullptr);
    uint32_t monitorCount = std::min<uint32_t>(monitorTerminals.size(), APnum);

    for (uint32_t apId = 0; apId < monitorCount; ++apId)
    {
        Ptr<Node> monitor = CreateObject<Node>();
        monitorTerminals[apId] = monitor;
        wifiNodes[apId].Add(monitor);
    }

    for (uint32_t apId = monitorCount; apId < monitorTerminals.size(); ++apId)
    {
        monitorTerminals[apId] = CreateObject<Node>();
    }
}

void NetSim::CreateTerminalNodes()
{
    for (uint32_t i = 0; i < termNum; ++i)
    {
        Ptr<Node> term = CreateObject<Node>();
        uint32_t apIndex = static_cast<uint32_t>(std::max(0, m_termData[i].apNo - 1));
        if (apIndex >= wifiNodes.size())
        {
            apIndex = 0;
        }
        wifiNodes[apIndex].Add(term);
        terms.push_back(term);
    }
}

void NetSim::CreateRouterNodes()
{
    for (uint32_t i = 0; i < APnum; ++i)
    {
        Ptr<Node> router = CreateObject<Node>();
        p2pNodes[i].Add(wifiAPs[i]);
        p2pNodes[i].Add(router);
        routers.push_back(router);
        if (cerNode != nullptr)
        {
            NodeContainer link(router, cerNode);
            m_routerCerNodes[i] = link;
        }
    }
}

void NetSim::CreateServerNodes()
{
    server_udpVoice = CreateObject<Node>();
    server_udpVideo = CreateObject<Node>();
    server_rtt = CreateObject<Node>();
    server_streaming = CreateObject<Node>();
    server_browser = CreateObject<Node>();
    remote_host = CreateObject<Node>();

    m_serverCerNodes.clear();
    m_serverCerDevices.clear();

    if (cerNode != nullptr)
    {
        m_serverCerNodes.push_back(NodeContainer(server_rtt, cerNode));
        m_serverCerNodes.push_back(NodeContainer(server_udpVideo, cerNode));
        m_serverCerNodes.push_back(NodeContainer(server_udpVoice, cerNode));
        m_serverCerNodes.push_back(NodeContainer(server_streaming, cerNode));
        m_serverCerNodes.push_back(NodeContainer(server_browser, cerNode));
        m_serverCerNodes.push_back(NodeContainer(remote_host, cerNode));
    }
}

Vector NetSim::GetMonitorPosition(uint32_t apId) const
{
    switch (apId)
    {
    case 0:
        return Vector(0.0, -25.0, 1.5);
    case 1:
        return Vector(25.0, 25.0, 1.5);
    case 2:
        return Vector(-25.0, 25.0, 1.5);
    default:
        return Vector(0.0, 0.0, 1.5);
    }
}

void NetSim::ConfigureDataLinkLayer(){
    std::cout << "==== ConfigureDataLinkLayer ====" << std::endl;
    NS_LOG_FUNCTION(this);

    ConfigureWifiDevices();
    ConfigureMobility();
    ConfigureMonitorPlacement();
    ConfigureNrForAp0();
    ConfigureP2PDevices();
    ConfigureRouterCerLinks();
    ConfigureServerCerLinks();
    ConfigurePgwCerLink();
}

void NetSim::ConfigureWifiDevices()
{
    NS_LOG_LOGIC("set wifi devices");
    for (uint32_t i = 0; i < APnum; ++i)
    {
        if (i == 0)
        {
            continue;
        }
        ConfigureLTE(i);
    }
}

void NetSim::ConfigureMonitorPlacement()
{
    for (uint32_t apId = 0; apId < monitorTerminals.size(); ++apId)
    {
        Ptr<Node> monitor = monitorTerminals[apId];
        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        Ptr<ListPositionAllocator> posList = CreateObject<ListPositionAllocator>();
        posList->Add(GetMonitorPosition(apId));
        mobility.SetPositionAllocator(posList);
        if (monitor)
        {
            mobility.Install(monitor);
            std::cout << "[DBG]   installed monitor#" << apId << std::endl;
        }
    }
}

void NetSim::ConfigureP2PDevices()
{
    NS_LOG_LOGIC("set p2p devices");
    for (uint32_t i = 0; i < p2pNodes.size(); ++i)
    {
        ConfigureP2P(i);
    }
}

void NetSim::ConfigureRouterCerLinks()
{
    NS_LOG_LOGIC("set router<->cer links");
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("0.5ms"));
    pointToPoint.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", StringValue("10000p"));

    for (uint32_t i = 0; i < m_routerCerNodes.size(); ++i)
    {
        if (m_routerCerNodes[i].GetN() != 2)
        {
            continue;
        }
        m_routerCerDevices[i] = pointToPoint.Install(m_routerCerNodes[i]);
    }
}

void NetSim::ConfigureServerCerLinks()
{
    NS_LOG_LOGIC("set server<->cer links");
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("0.5ms"));
    pointToPoint.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", StringValue("10000p"));

    m_serverCerDevices.resize(m_serverCerNodes.size());
    for (uint32_t i = 0; i < m_serverCerNodes.size(); ++i)
    {
        if (m_serverCerNodes[i].GetN() != 2)
        {
            continue;
        }
        m_serverCerDevices[i] = pointToPoint.Install(m_serverCerNodes[i]);
    }
}

void NetSim::ConfigurePgwCerLink()
{
    if (m_pgwCerNodes.GetN() != 2)
    {
        return;
    }
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("1ms"));
    pointToPoint.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", StringValue("10000p"));
    m_pgwCerDevices = pointToPoint.Install(m_pgwCerNodes);
}

void NetSim::ConfigureWifi(uint32_t count){
    std::cout << "==== ConfigureWifi ====" << std::endl;
    NS_LOG_FUNCTION(this);
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RandomPropagationLossModel");

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("RxGain", DoubleValue(0));
    phy.Set("ChannelNumber", UintegerValue(36));
    phy.Set("ChannelWidth", UintegerValue(20));
    phy.Set("Antennas", UintegerValue(1));
    phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(1));
    phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(1));
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager(
        "ns3::ConstantRateWifiManager",
        "DataMode", StringValue("HeMcs7"),
        "ControlMode", StringValue("HeMcs0")
    );
    wifi.ConfigHeOptions("BssColor", UintegerValue((count % 63) + 1));

    WifiMacHelper mac;
    std::stringstream ssidss;
    ssidss << "main-SSID-" << count;
    Ssid ssid = Ssid (ssidss.str());
    mac.SetType ("ns3::ApWifiMac",
                 "Ssid", SsidValue (ssid),
                 "BeaconInterval", TimeValue(MicroSeconds(52224)));
    wifiDevices[count] = wifi.Install (phy, mac, wifiNodes[count].Get(0));

    for(uint32_t i=1; i<wifiNodes[count].GetN(); i++){
        mac.SetType ("ns3::StaWifiMac",
                     "Ssid", SsidValue (ssid),
                     "ActiveProbing", BooleanValue(true));
        NetDeviceContainer temp = wifi.Install (phy, mac, wifiNodes[count].Get(i));
        wifiDevices[count].Add(temp);
    }
    std::stringstream ss;
    ss << OUTPUT_DIR << "wifi" << count;
}

void NetSim::ConfigureLTE(uint32_t count){
    std::cout << "==== ConfigureLTE ====" << std::endl;
    NS_LOG_FUNCTION(this);
    SpectrumWifiPhyHelper phy;
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<ConstantSpeedPropagationDelayModel> delay = CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delay);
    Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(loss);
    phy.SetChannel(spectrumChannel);
    phy.Set("RxGain", DoubleValue(0));
    phy.Set("Antennas", UintegerValue(1));
    phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(1));
    phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(1));
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager(
        "ns3::ConstantRateWifiManager",
        "DataMode", StringValue("HeMcs3"),
        "ControlMode", StringValue("HeMcs0")
    );
    Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", UintegerValue (0));
    wifi.ConfigHeOptions("BssColor", UintegerValue((count % 63) + 1));

    WifiMacHelper mac;
    std::stringstream ssidss;
    ssidss << "main-SSID-" << count;
    Ssid ssid = Ssid (ssidss.str());

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid),
                "QosSupported", BooleanValue(true),
                "BeaconInterval", TimeValue(MicroSeconds(52224)));

    wifiDevices[count] = wifi.Install(phy, mac, wifiNodes[count].Get(0));

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(true),
                "QosSupported", BooleanValue(true));

    for(uint32_t i=1; i<wifiNodes[count].GetN(); i++){
        NetDeviceContainer temp = wifi.Install(phy, mac, wifiNodes[count].Get(i));
        wifiDevices[count].Add(temp);
    }
    std::stringstream ss;
    ss << OUTPUT_DIR << "wifi" << count;
}

void NetSim::ConfigureMobility(){
    std::cout << "==== ConfigureMobility ====" << std::endl;
    NS_LOG_FUNCTION(this);

    ConfigureApMobility();
    ConfigureTermMobility();
}

void NetSim::ConfigureApMobility()
{
    static const int kOffsets[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    static const int kBase = 25;

    NS_LOG_LOGIC("set mobility");
    uint32_t apCount = std::min<uint32_t>(APnum, static_cast<uint32_t>(wifiAPs.size()));
    for (uint32_t i = 0; i < apCount; ++i)
    {
        std::cout << "[DBG]  AP#" << i << " nodePtr=" << (wifiAPs[i] ? 1 : 0) << std::endl;
        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        Ptr<ListPositionAllocator> posList = CreateObject<ListPositionAllocator>();

        if (i == 0)
        {
            posList->Add(Vector(0.0, -25.0, 10.0));
        }
        else
        {
            const int* offset = kOffsets[i - 1];
            posList->Add(Vector(kBase * offset[0], kBase * offset[1], 3.0));
        }

        mobility.SetPositionAllocator(posList);
        if (i < wifiAPs.size() && wifiAPs[i] != nullptr)
        {
            mobility.Install(wifiAPs[i]);
        }
    }
}

void NetSim::ConfigureTermMobility()
{
    double distance = 8.0;
    double minPoint = -25.0;
    uint32_t gridWidth = 8;

    if (termNum == 64)
    {
        distance = 50.0 / 8.0;
        gridWidth = 8;
    }
    else if (termNum == 81)
    {
        distance = 50.0 / 9.0;
        gridWidth = 9;
    }
    else if (termNum == 100)
    {
        distance = 50.0 / 10.0;
        gridWidth = 10;
    }

    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue(minPoint),
                                  "MinY", DoubleValue(minPoint),
                                  "DeltaX", DoubleValue(distance),
                                  "DeltaY", DoubleValue(distance),
                                  "GridWidth", UintegerValue(gridWidth),
                                  "LayoutType", StringValue("RowFirst"));

    if (m_mob == 2)
    {
        mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                                   "Bounds", RectangleValue(Rectangle(-50, 50, -50, 50)),
                                   "Distance", StringValue("10"),
                                   "Speed", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
                                   "Mode", StringValue("Time"),
                                   "Time", StringValue("1s"));
    }
    else
    {
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    }

    for (uint32_t idx = 0; idx < terms.size(); ++idx)
    {
        Ptr<Node> term = terms[idx];
        if (term != nullptr)
        {
            mobility.Install(term);
            std::cout << "[DBG]   installed term#" << idx << std::endl;
        }
    }
}

void NetSim::ConfigureP2P(uint32_t count){
    std::cout << "==== ConfigureP2P ====" << std::endl;
    NS_LOG_FUNCTION(this);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute  ("DataRate", StringValue ("1Gbps"));
    pointToPoint.SetChannelAttribute ("Delay", StringValue ("0.1ms"));
    pointToPoint.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", StringValue ("10000p"));
    p2pDevices[count] = pointToPoint.Install (p2pNodes[count]);
    std::stringstream ss;
    ss << OUTPUT_DIR << "pointToPoint" << count;
    pointToPoint.EnablePcapAll(ss.str());
}

void NetSim::ConfigureNetworkLayer(){
    std::cout << "==== ConfigureNetworkLayer ====" << std::endl;
    NS_LOG_FUNCTION(this);

    NS_LOG_LOGIC("Install internet stack");
    InternetStackHelper stack;
    stack.InstallAll();

    Ipv4AddressHelper address;
    Ipv4StaticRoutingHelper staticRouting;

    std::vector<Ipv4Address> routerCerIps(APnum, Ipv4Address("0.0.0.0"));
    std::vector<Ipv4Address> cerRouterIps(APnum, Ipv4Address("0.0.0.0"));
    std::vector<uint32_t> routerCerIfIndex(APnum, 0);
    std::vector<uint32_t> cerRouterIfIndex(APnum, 0);

    Ptr<Ipv4> cerIpv4 = cerNode ? cerNode->GetObject<Ipv4>() : nullptr;
    Ptr<Ipv4StaticRouting> cerStatic = cerIpv4 ? staticRouting.GetStaticRouting(cerIpv4) : nullptr;
    if (cerIpv4)
    {
        EnableIpForwardIfPresent(cerIpv4);
    }

    for (uint32_t i = 0; i < APnum; ++i)
    {
        if (i >= m_routerCerDevices.size() || m_routerCerDevices[i].GetN() == 0)
        {
            continue;
        }
        std::stringstream base;
        base << "10.200." << (i + 1) << ".0";
        address.SetBase(Ipv4Address(base.str().c_str()), "255.255.255.252");
        Ipv4InterfaceContainer ifaces = address.Assign(m_routerCerDevices[i]);
        routerCerIps[i] = ifaces.GetAddress(0);
        cerRouterIps[i] = ifaces.GetAddress(1);

        Ptr<Ipv4> routerIpv4 = routers[i]->GetObject<Ipv4>();
        if (routerIpv4)
        {
            EnableIpForwardIfPresent(routerIpv4);
            int32_t routerIf = routerIpv4->GetInterfaceForDevice(m_routerCerDevices[i].Get(0));
            if (routerIf < 0)
            {
                routerIf = routerIpv4->GetInterfaceForAddress(routerCerIps[i]);
            }
            if (routerIf >= 0)
            {
                routerCerIfIndex[i] = static_cast<uint32_t>(routerIf);
                Ptr<Ipv4StaticRouting> rStatic = staticRouting.GetStaticRouting(routerIpv4);
                rStatic->SetDefaultRoute(cerRouterIps[i], routerCerIfIndex[i]);
            }
        }

        if (cerIpv4)
        {
            int32_t cerIf = cerIpv4->GetInterfaceForDevice(m_routerCerDevices[i].Get(1));
            if (cerIf < 0)
            {
                cerIf = cerIpv4->GetInterfaceForAddress(cerRouterIps[i]);
            }
            if (cerIf >= 0)
            {
                cerRouterIfIndex[i] = static_cast<uint32_t>(cerIf);
            }
        }
    }

    std::vector<Ptr<Node>> serverNodes = {server_rtt, server_udpVideo, server_udpVoice, server_streaming, server_browser, remote_host};
    m_remoteHostAddress = Ipv4Address::GetZero();
    for (size_t i = 0; i < serverNodes.size(); ++i)
    {
        if (i >= m_serverCerDevices.size() || m_serverCerDevices[i].GetN() == 0)
        {
            continue;
        }
        std::stringstream base;
        base << "10.201." << (i + 1) << ".0";
        address.SetBase(Ipv4Address(base.str().c_str()), "255.255.255.252");
        Ipv4InterfaceContainer ifaces = address.Assign(m_serverCerDevices[i]);
        Ipv4Address serverAddr = ifaces.GetAddress(0);
        Ipv4Address cerAddr = ifaces.GetAddress(1);

        Ptr<Node> serverNode = serverNodes[i];
        Ptr<Ipv4> srvIpv4 = serverNode ? serverNode->GetObject<Ipv4>() : nullptr;
        if (srvIpv4)
        {
            int32_t srvIf = srvIpv4->GetInterfaceForDevice(m_serverCerDevices[i].Get(0));
            if (srvIf < 0)
            {
                srvIf = srvIpv4->GetInterfaceForAddress(serverAddr);
            }
            if (srvIf >= 0)
            {
                Ptr<Ipv4StaticRouting> sStatic = staticRouting.GetStaticRouting(srvIpv4);
                sStatic->SetDefaultRoute(cerAddr, static_cast<uint32_t>(srvIf));
            }
        }

        if (serverNode == remote_host)
        {
            m_remoteHostAddress = serverAddr;
        }
    }

    Ipv4Address pgwAddr("0.0.0.0");
    uint32_t cerPgwIfIndex = 0;
    bool pgwLinkActive = false;
    if (m_pgwCerDevices.GetN() == 2)
    {
        address.SetBase("10.202.0.0", "255.255.255.252");
        Ipv4InterfaceContainer ifaces = address.Assign(m_pgwCerDevices);
        pgwAddr = ifaces.GetAddress(0);
        Ipv4Address cerSideAddr = ifaces.GetAddress(1);

        Ptr<Node> pgwNode = m_nrEpcHelper ? m_nrEpcHelper->GetPgwNode() : nullptr;
        Ptr<Ipv4> pgwIpv4 = pgwNode ? pgwNode->GetObject<Ipv4>() : nullptr;
        if (pgwIpv4)
        {
            EnableIpForwardIfPresent(pgwIpv4);
            int32_t pgwIf = pgwIpv4->GetInterfaceForDevice(m_pgwCerDevices.Get(0));
            if (pgwIf < 0)
            {
                pgwIf = pgwIpv4->GetInterfaceForAddress(pgwAddr);
            }
            if (pgwIf >= 0)
            {
                Ptr<Ipv4StaticRouting> pgwStatic = staticRouting.GetStaticRouting(pgwIpv4);
                pgwStatic->SetDefaultRoute(cerSideAddr, static_cast<uint32_t>(pgwIf));
                pgwStatic->AddNetworkRouteTo(Ipv4Address("10.201.0.0"), Ipv4Mask("255.255.240.0"), cerSideAddr, static_cast<uint32_t>(pgwIf));
            }
        }

        if (cerIpv4)
        {
            int32_t cerIf = cerIpv4->GetInterfaceForDevice(m_pgwCerDevices.Get(1));
            if (cerIf < 0)
            {
                cerIf = cerIpv4->GetInterfaceForAddress(cerSideAddr);
            }
            if (cerIf >= 0)
            {
                cerPgwIfIndex = static_cast<uint32_t>(cerIf);
                pgwLinkActive = true;
            }
        }
    }

    std::vector<Ipv4Address> apP2PIps(APnum, Ipv4Address("0.0.0.0"));
    std::vector<Ipv4Address> routerP2PIps(APnum, Ipv4Address("0.0.0.0"));
    std::vector<uint32_t> routerP2PIfIndex(APnum, 0);
    std::vector<Ipv4Address> wifiNetworkBases(APnum, Ipv4Address("0.0.0.0"));

    for (uint32_t i = 0; i < APnum; ++i)
    {
        std::stringstream p2pBase;
        p2pBase << "10.0." << (i + 1) << ".0";
        address.SetBase(Ipv4Address(p2pBase.str().c_str()), "255.255.255.0");
        Ipv4InterfaceContainer p2pInterfaces = address.Assign(p2pDevices[i]);
        apP2PIps[i] = p2pInterfaces.GetAddress(0);
        routerP2PIps[i] = p2pInterfaces.GetAddress(1);

        Ptr<Ipv4> routerIpv4 = routers[i]->GetObject<Ipv4>();
        if (routerIpv4)
        {
            int32_t outIf = routerIpv4->GetInterfaceForDevice(p2pDevices[i].Get(1));
            if (outIf < 0)
            {
                outIf = routerIpv4->GetInterfaceForAddress(routerP2PIps[i]);
            }
            if (outIf >= 0)
            {
                routerP2PIfIndex[i] = static_cast<uint32_t>(outIf);
            }
        }

        if (wifiDevices[i].GetN() == 0)
        {
            continue;
        }

        std::stringstream wifiBase;
        wifiBase << "10.1." << i << ".0";
        Ipv4Address baseip(wifiBase.str().c_str());
        wifiNetworkBases[i] = baseip;

        address.SetBase(baseip, "255.255.255.0");
        Ipv4InterfaceContainer wifiInterfaces = address.Assign(wifiDevices[i]);

        Ptr<Ipv4> apIpv4 = wifiAPs[i]->GetObject<Ipv4>();
        if (apIpv4)
        {
            EnableIpForwardIfPresent(apIpv4);
            int32_t apIf = apIpv4->GetInterfaceForDevice(p2pDevices[i].Get(0));
            if (apIf < 0)
            {
                apIf = apIpv4->GetInterfaceForAddress(apP2PIps[i]);
            }
            if (apIf >= 0)
            {
                Ptr<Ipv4StaticRouting> apStatic = staticRouting.GetStaticRouting(apIpv4);
                apStatic->SetDefaultRoute(routerP2PIps[i], static_cast<uint32_t>(apIf));
            }
        }

        Ptr<Ipv4> rIpv4 = routers[i]->GetObject<Ipv4>();
        if (rIpv4)
        {
            Ptr<Ipv4StaticRouting> rStatic = staticRouting.GetStaticRouting(rIpv4);
            if (apP2PIps[i] != Ipv4Address("0.0.0.0"))
            {
                rStatic->AddNetworkRouteTo(baseip, Ipv4Mask("255.255.255.0"), apP2PIps[i], routerP2PIfIndex[i]);
            }
        }

        Ipv4Address apWifiIp = wifiInterfaces.GetAddress(0);
        for (uint32_t termID = 1; termID < wifiNodes[i].GetN(); ++termID)
        {
            Ptr<Node> termNode = wifiNodes[i].Get(termID);
            Ptr<Ipv4> tIpv4 = termNode ? termNode->GetObject<Ipv4>() : nullptr;
            if (!tIpv4)
            {
                continue;
            }
            int32_t wifiIf = -1;
            for (uint32_t ifIndex = 0; ifIndex < tIpv4->GetNInterfaces(); ++ifIndex)
            {
                uint32_t nAddresses = tIpv4->GetNAddresses(ifIndex);
                for (uint32_t a = 0; a < nAddresses; ++a)
                {
                    Ipv4InterfaceAddress ifaddr = tIpv4->GetAddress(ifIndex, a);
                    if (ifaddr.GetLocal().CombineMask(Ipv4Mask("255.255.255.0")) == baseip)
                    {
                        wifiIf = static_cast<int32_t>(ifIndex);
                    }
                }
            }
            uint32_t outIf = wifiIf >= 0 ? static_cast<uint32_t>(wifiIf) : 1u;
            Ptr<Ipv4StaticRouting> termStatic = staticRouting.GetStaticRouting(tIpv4);
            termStatic->SetDefaultRoute(apWifiIp, outIf);
        }
    }

    ConfigureNrIpAfterNetwork();

    if (cerStatic)
    {
        for (uint32_t i = 0; i < APnum; ++i)
        {
            if (wifiNetworkBases[i] == Ipv4Address("0.0.0.0"))
            {
                continue;
            }
            if (routerCerIps[i] == Ipv4Address("0.0.0.0"))
            {
                continue;
            }
            cerStatic->AddNetworkRouteTo(wifiNetworkBases[i], Ipv4Mask("255.255.255.0"), routerCerIps[i], cerRouterIfIndex[i]);
        }

        if (pgwLinkActive)
        {
            cerStatic->AddNetworkRouteTo(UE_NET, UE_MASK, pgwAddr, cerPgwIfIndex);
        }
    }

    if (APnum >= 3)
    {
        if (wifiAPs.size() > 1) { DumpIpv4Info("AP1", wifiAPs[1]); }
        if (wifiAPs.size() > 2) { DumpIpv4Info("AP2", wifiAPs[2]); }
        if (routers.size() > 0) { DumpIpv4Info("ROUTER1", routers[0]); }
        if (routers.size() > 1) { DumpIpv4Info("ROUTER2", routers[1]); }
        if (!monitorTerminals.empty() && monitorTerminals[0]) { DumpIpv4Info("MONITOR0", monitorTerminals[0]); }
        if (monitorTerminals.size() > 1 && monitorTerminals[1]) { DumpIpv4Info("MONITOR1", monitorTerminals[1]); }
        if (monitorTerminals.size() > 2 && monitorTerminals[2]) { DumpIpv4Info("MONITOR2", monitorTerminals[2]); }
        DumpIpv4Info("REMOTE_HOST", remote_host);
        DumpIpv4Info("SERVER_RTT", server_rtt);
        DumpIpv4Info("CER", cerNode);
        if (m_nrEpcHelper) {
            DumpIpv4Info("PGW", m_nrEpcHelper->GetPgwNode());
        }
    }
}

void NetSim::ConfigureNrForAp0()
{
    if (APnum == 0 || wifiNodes.empty())
    {
        return;
    }

    m_nrHelper = CreateObject<NrHelper>();
    m_nrEpcHelper = CreateObject<NrPointToPointEpcHelper>();
    m_nrHelper->SetEpcHelper(m_nrEpcHelper);

    std::vector<CcBwpCreator::SimpleOperationBandConf> bandConfs = {
        CcBwpCreator::SimpleOperationBandConf(3.5e9, 40e6, 1)
    };
    auto bwpsPair = m_nrHelper->CreateBandwidthParts(bandConfs, "UMa", "Default", "ThreeGpp");
    auto allBwps = bwpsPair.second;
    std::cout << "[DBG][NR] BWP count=" << allBwps.size() << std::endl;

    NodeContainer gnb;
    if (wifiAPs.size() > 0 && wifiAPs[0] != nullptr)
    {
        gnb.Add(wifiAPs[0]);
    }
    if (gnb.GetN() == 0)
    {
        return;
    }
    m_nrGnbDevs = m_nrHelper->InstallGnbDevice(gnb, allBwps);
    std::cout << "[DBG][NR] gNB devices=" << m_nrGnbDevs.GetN() << std::endl;

    NodeContainer ue;
    for (uint32_t i = 1; i < wifiNodes[0].GetN(); ++i)
    {
        Ptr<Node> n = wifiNodes[0].Get(i);
        if (n)
        {
            ue.Add(n);
        }
    }
    if (ue.GetN() == 0)
    {
        return;
    }
    m_nrUeDevs = m_nrHelper->InstallUeDevice(ue, allBwps);
    std::cout << "[DBG][NR] UE devices=" << m_nrUeDevs.GetN() << std::endl;

    Ptr<Node> pgw = m_nrEpcHelper->GetPgwNode();
    if (pgw && cerNode != nullptr)
    {
        m_pgwCerNodes = NodeContainer(pgw, cerNode);
    }
}

void NetSim::ConfigureNrIpAfterNetwork()
{
    if (!m_nrEpcHelper || m_nrUeDevs.GetN() == 0)
    {
        return;
    }

    InternetStackHelper internet;
    for (auto it = m_nrUeDevs.Begin(); it != m_nrUeDevs.End(); ++it)
    {
        Ptr<Node> n = (*it)->GetNode();
        if (!n->GetObject<Ipv4>())
        {
            internet.Install(n);
        }
    }

    Ipv4InterfaceContainer ueIf = m_nrEpcHelper->AssignUeIpv4Address(m_nrUeDevs);
    (void)ueIf;

    Ipv4StaticRoutingHelper rh;
    Ipv4Address gw = m_nrEpcHelper->GetUeDefaultGatewayAddress();
    for (auto it = m_nrUeDevs.Begin(); it != m_nrUeDevs.End(); ++it)
    {
        Ptr<NetDevice> dev = *it;
        Ptr<Node> n = dev->GetNode();
        Ptr<Ipv4> ipv4 = n->GetObject<Ipv4>();
        if (!ipv4)
        {
            continue;
        }
        Ptr<Ipv4StaticRouting> rt = rh.GetStaticRouting(ipv4);
        rt->SetDefaultRoute(gw, 1);
    }

    if (m_nrGnbDevs.GetN() > 0 && m_nrUeDevs.GetN() > 0)
    {
        m_nrHelper->AttachToClosestGnb(m_nrUeDevs, m_nrGnbDevs);
    }
}

} // namespace ns3
