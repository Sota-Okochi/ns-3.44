#include "NetSim.h"

#include <cmath>

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
            static_cast<void>(ipv4->GetAddress(ifIndex, a));
        }
    }
    Ipv4StaticRoutingHelper rh;
    Ptr<Ipv4StaticRouting> rt = rh.GetStaticRouting(ipv4);
    uint32_t nRoutes = rt->GetNRoutes();
    for (uint32_t r = 0; r < nRoutes; ++r)
    {
        static_cast<void>(rt->GetRoute(r));
    }
}

} // namespace

// ------------------------------------------------------------
// ネットワークトポロジーの作成
void NetSim::CreateNetworkTopology(){
    std::cout << "==== CreateNetworkTopology ====" << std::endl;
    NS_LOG_FUNCTION(this);

    InitializeNodeContainers(); // ノードコンテナの初期化
    CreateCerNode(); // CER
    CreateWifiApNodes(); // AP
    CreateMonitorNodes(); // 検査用端末
    CreateTerminalNodes(); // 端末
    CreateRouterNodes(); // ルータ
    CreateServerNodes(); // サーバ
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
    monitorTerminals.assign(kMaxMonitorTerminals, nullptr);
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
    if (m_nth == 5)
    {
        // nth==5: 全端末を全APのコンテナに追加 → 全RATのデバイスが付く
        for (uint32_t i = 0; i < termNum; ++i)
        {
            Ptr<Node> term = CreateObject<Node>();
            for (uint32_t ap = 0; ap < APnum; ++ap)
            {
                wifiNodes[ap].Add(term);
            }
            terms.push_back(term);
        }
    }
    else
    {
        // 既存ロジック（単一APコンテナ）
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
    server_onlineGame = CreateObject<Node>();
    server_browser = CreateObject<Node>();
    remote_host = CreateObject<Node>();

    m_serverCerNodes.clear();
    m_serverCerDevices.clear();

    if (cerNode != nullptr)
    {
        m_serverCerNodes.push_back(NodeContainer(server_rtt, cerNode));
        m_serverCerNodes.push_back(NodeContainer(server_udpVideo, cerNode));
        m_serverCerNodes.push_back(NodeContainer(server_udpVoice, cerNode));
        m_serverCerNodes.push_back(NodeContainer(server_onlineGame, cerNode));
        m_serverCerNodes.push_back(NodeContainer(server_browser, cerNode));
        m_serverCerNodes.push_back(NodeContainer(remote_host, cerNode));
    }
}
// ------------------------------------------------------------


// ------------------------------------------------------------
// データリンク層の設定
// ------------------------------------------------------------
void NetSim::ConfigureDataLinkLayer(){
    std::cout << "==== ConfigureDataLinkLayer ====" << std::endl;
    NS_LOG_FUNCTION(this);

    ConfigureMobility(); // モビリティ設定
    ConfigureMonitorPlacement(); // 検査用端末の配置
    ConfigureNrForAp0(); // AP0: NR
    ConfigureWifiForAP1(); // AP1: Wi-Fi
    ConfigureWifiForAP2(); // AP2: Wi-Fi
    ConfigureP2PDevices(); // P2Pのデバイス設定
    ConfigureRouterCerLinks(); // ルータとCERのリンク設定
    ConfigureServerCerLinks(); // サーバとCERのリンク設定
    ConfigurePgwCerLink(); // NR PgwとCERのリンク設定
}

void NetSim::ConfigureMobility(){
    std::cout << "==== ConfigureMobility ====" << std::endl;
    NS_LOG_FUNCTION(this);

    ConfigureApMobility(); // APのモビリティ設定
    ConfigureTermMobility(); // 端末のモビリティ設定
}

void NetSim::ConfigureApMobility()
{
    static const int kOffsets[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    static const int kBase = 10;

    NS_LOG_LOGIC("set mobility");
    uint32_t apCount = std::min<uint32_t>(APnum, static_cast<uint32_t>(wifiAPs.size()));
    for (uint32_t i = 0; i < apCount; ++i)
    {
        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        Ptr<ListPositionAllocator> posList = CreateObject<ListPositionAllocator>();

        if (m_nth == 5)
        {
            if (i == 0)
            {
                posList->Add(Vector(0.0, 0.0, 10.0));
            }
            else if (i == 1)
            {
                posList->Add(Vector(5.0, 0.0, 3.0));
            }
            else if (i == 2)
            {
                posList->Add(Vector(-5.0, 0.0, 3.0));
            }
            else
            {
                const int* offset = kOffsets[(i - 1) % 4];
                const int ring = static_cast<int>((i - 1) / 4) + 1;
                posList->Add(Vector(kBase * ring * offset[0], kBase * ring * offset[1], 3.0));
            }
        }
        else
        {
            if (i == 0)
            {
                posList->Add(Vector(0.0, -10.0, 10.0));
            }
            else
            {
                const int* offset = kOffsets[(i - 1) % 4];
                const int ring = static_cast<int>((i - 1) / 4) + 1;
                posList->Add(Vector(kBase * ring * offset[0], kBase * ring * offset[1], 3.0));
            }
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
    // 各APの位置を取得
    std::vector<Vector> apPositions(APnum, Vector(0.0, 0.0, 0.0));
    for (uint32_t i = 0; i < APnum && i < wifiAPs.size(); ++i)
    {
        if (wifiAPs[i] == nullptr)
        {
            continue;
        }
        Ptr<MobilityModel> mob = wifiAPs[i]->GetObject<MobilityModel>();
        if (mob != nullptr)
        {
            apPositions[i] = mob->GetPosition();
        }
    }

    // 端末をAPの近く（半径 radius メートル以内）にランダム配置
    const double radius = (m_nth == 5) ? 20.0 : 10.0;
    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    const Vector ap0Center =
        (!apPositions.empty()) ? apPositions[0] : Vector(0.0, 0.0, 10.0);

    for (uint32_t idx = 0; idx < terms.size(); ++idx)
    {
        Ptr<Node> term = terms[idx];
        if (term == nullptr)
        {
            continue;
        }

        Vector apPos;
        if (m_nth == 5)
        {
            // nth==5: AP0 を中心に半径20m以内へ配置
            apPos = ap0Center;
        }
        else
        {
            // 端末の割り当てAP (1ベース → 0ベース)
            uint32_t apIdx = 0;
            if (idx < m_termData.size() && m_termData[idx].apNo > 0)
            {
                apIdx = static_cast<uint32_t>(m_termData[idx].apNo - 1);
            }
            if (apIdx >= APnum)
            {
                apIdx = 0;
            }
            apPos = apPositions[apIdx];
        }

        // AP周囲に一様ランダム配置（円形）
        double angle = rng->GetValue(0.0, 2.0 * M_PI);
        double r = radius * std::sqrt(rng->GetValue(0.0, 1.0));
        double x = apPos.x + r * std::cos(angle);
        double y = apPos.y + r * std::sin(angle);

        Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
        pos->Add(Vector(x, y, 1.5));

        MobilityHelper mobility;
        mobility.SetPositionAllocator(pos);

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

        mobility.Install(term);
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
        }
    }
}

Vector NetSim::GetMonitorPosition(uint32_t apId) const
{
    if (m_nth == 5)
    {
        switch (apId)
        {
        case 0:
            return Vector(0.0, 0.0, 1.5);
        case 1:
            return Vector(5.0, 0.0, 1.5);
        case 2:
            return Vector(-5.0, 0.0, 1.5);
        default:
            return Vector(0.0, 0.0, 1.5);
        }
    }

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

// 5G基地局の設定
void NetSim::ConfigureNrForAp0()
{
    if (APnum == 0 || wifiNodes.empty())
    {
        return;
    }

    const double nrCenterFreqHz = 3.5e9;   // 3.5 GHz mid-band
    const double nrChannelBwHz = 100e6;
    const uint8_t nrNumComponentCarriers = 1;

    // RLC/UM バッファを拡大し、PDCP破棄を無効化（デフォルト10KBでは即オーバーフロー）
    Config::SetDefault("ns3::NrRlcUm::MaxTxBufferSize", UintegerValue(999999999));
    Config::SetDefault("ns3::NrRlcUm::EnablePdcpDiscarding", BooleanValue(false));

    m_nrHelper = CreateObject<NrHelper>();
    m_nrEpcHelper = CreateObject<NrPointToPointEpcHelper>();
    m_nrHelper->SetEpcHelper(m_nrEpcHelper);

    // Use a higher-capacity OFDMA scheduler and larger scheduling granularity
    m_nrHelper->SetSchedulerTypeId(TypeId::LookupByName("ns3::NrMacSchedulerOfdmaPF"));
    m_nrHelper->SetSchedulerAttribute("EnableSrsInUlSlots", BooleanValue(false));
    m_nrHelper->SetSchedulerAttribute("EnableSrsInFSlots", BooleanValue(false));
    m_nrHelper->SetSchedulerAttribute("DlCtrlSymbols", UintegerValue(1));
    // Use finer scheduling granularity to improve per-UE allocation under load.
    m_nrHelper->SetGnbMacAttribute("NumRbPerRbg", UintegerValue(2));

    // 100 MHz @ 3.5 GHz requires numerology 1 (30 kHz SCS).
    // Default numerology 0 (15 kHz SCS) only supports up to 50 MHz,
    // causing severe RB limitation and extremely low throughput.
    m_nrHelper->SetGnbPhyAttribute("Numerology", UintegerValue(1));

    // Align PHY capabilities with the wider resource budget
    m_nrHelper->SetGnbPhyAttribute("TxPower", DoubleValue(40.0));
    m_nrHelper->SetUePhyAttribute("TxPower", DoubleValue(26.0));
    m_nrHelper->SetGnbAntennaAttribute("NumRows", UintegerValue(8));
    m_nrHelper->SetGnbAntennaAttribute("NumColumns", UintegerValue(4));
    m_nrHelper->SetUeAntennaAttribute("NumRows", UintegerValue(2));
    m_nrHelper->SetUeAntennaAttribute("NumColumns", UintegerValue(2));

    CcBwpCreator::SimpleOperationBandConf wideBand(nrCenterFreqHz,
                                                   nrChannelBwHz,
                                                   nrNumComponentCarriers);
    std::vector<CcBwpCreator::SimpleOperationBandConf> bandConfs = {wideBand};
    auto bwpsPair = m_nrHelper->CreateBandwidthParts(bandConfs, "UMi", "LOS", "ThreeGpp");
    auto allBwps = bwpsPair.second;

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

    Ptr<Node> pgw = m_nrEpcHelper->GetPgwNode();
    if (pgw && cerNode != nullptr)
    {
        m_pgwCerNodes = NodeContainer(pgw, cerNode);
    }
}

void NetSim::ConfigureWifiForAP1(){
    std::cout << "==== ConfigureWifiForAP1 ====" << std::endl;
    NS_LOG_FUNCTION(this);

    constexpr uint32_t apIndex = 1;
    if (APnum <= apIndex || wifiNodes.size() <= apIndex || wifiDevices.size() <= apIndex ||
        wifiNodes[apIndex].GetN() == 0)
    {
        std::cout << "[Wi-Fi] AP1 is not available; skipping Wi-Fi setup" << std::endl;
        return;
    }

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
    Config::SetDefault("ns3::WifiPhy::ChannelWidth", UintegerValue(40));
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");
    Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", UintegerValue(2200));
    wifi.ConfigHeOptions("BssColor", UintegerValue((apIndex % 63) + 1));

    WifiMacHelper mac;
    std::stringstream ssidss;
    ssidss << "main-SSID-" << apIndex;
    Ssid ssid = Ssid(ssidss.str());

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid),
                "QosSupported", BooleanValue(true),
                "BeaconInterval", TimeValue(MicroSeconds(52224)));

    wifiDevices[apIndex] = wifi.Install(phy, mac, wifiNodes[apIndex].Get(0));

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(true),
                "QosSupported", BooleanValue(true));

    for(uint32_t i=1; i<wifiNodes[apIndex].GetN(); i++){
        NetDeviceContainer temp = wifi.Install(phy, mac, wifiNodes[apIndex].Get(i));
        wifiDevices[apIndex].Add(temp);
    }
    std::stringstream ss;
    ss << OUTPUT_DIR << "wifi" << apIndex;
}

void NetSim::ConfigureWifiForAP2(){
    std::cout << "==== ConfigureWifiForAP2 ====" << std::endl;
    NS_LOG_FUNCTION(this);

    constexpr uint32_t apIndex = 2;
    if (APnum <= apIndex || wifiNodes.size() <= apIndex || wifiDevices.size() <= apIndex ||
        wifiNodes[apIndex].GetN() == 0)
    {
        std::cout << "[Wi-Fi] AP2 is not available; skipping Wi-Fi setup" << std::endl;
        return;
    }

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
    Config::SetDefault("ns3::WifiPhy::ChannelWidth", UintegerValue(40));
    wifi.SetRemoteStationManager("ns3::IdealWifiManager");
    Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", UintegerValue (2200));
    wifi.ConfigHeOptions("BssColor", UintegerValue((apIndex % 63) + 1));

    WifiMacHelper mac;
    std::stringstream ssidss;
    ssidss << "main-SSID-" << apIndex;
    Ssid ssid = Ssid (ssidss.str());

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid),
                "QosSupported", BooleanValue(true),
                "BeaconInterval", TimeValue(MicroSeconds(52224)));

    wifiDevices[apIndex] = wifi.Install(phy, mac, wifiNodes[apIndex].Get(0));

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(true),
                "QosSupported", BooleanValue(true));

    for(uint32_t i=1; i<wifiNodes[apIndex].GetN(); i++){
        NetDeviceContainer temp = wifi.Install(phy, mac, wifiNodes[apIndex].Get(i));
        wifiDevices[apIndex].Add(temp);
    }
    std::stringstream ss;
    ss << OUTPUT_DIR << "wifi" << apIndex;
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
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("400Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("0.5ms"));
    pointToPoint.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", StringValue("300p"));

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
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("0.1ms"));
    pointToPoint.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", StringValue("230p"));
    m_pgwCerDevices = pointToPoint.Install(m_pgwCerNodes);
}

void NetSim::ConfigureP2P(uint32_t count){
    std::cout << "==== ConfigureP2P ====" << std::endl;
    NS_LOG_FUNCTION(this);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute  ("DataRate", StringValue ("40Mbps"));
    pointToPoint.SetChannelAttribute ("Delay", StringValue ("0.1ms"));
    pointToPoint.SetQueue ("ns3::DropTailQueue<Packet>", "MaxSize", StringValue ("150p"));
    p2pDevices[count] = pointToPoint.Install (p2pNodes[count]);
    // std::stringstream ss;
    // ss << OUTPUT_DIR << "pointToPoint" << count;
    // pointToPoint.EnablePcapAll(ss.str());
}

void NetSim::ConfigureNetworkLayer(){
    std::cout << "==== ConfigureNetworkLayer ====" << std::endl;
    NS_LOG_FUNCTION(this);

    NS_LOG_LOGIC("Install internet stack");
    InternetStackHelper stack;
    stack.InstallAll();

    Ipv4AddressHelper address;
    Ipv4StaticRoutingHelper staticRouting;
    m_wifiApAddresses.clear();
    m_wifiStationAddresses.clear();

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

    // APとCERのリンクIPの設定
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

    // サーバ群とCERのリンクIPの設定
    std::vector<Ptr<Node>> serverNodes = {server_rtt, server_udpVideo, server_udpVoice, server_onlineGame, server_browser, remote_host};
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

    // PGWとCERのリンクIPの設定
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

    // AP配下ネットワークのアドレス割り当てとルーティングの設定
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
        Ipv4Address apWifiIp = wifiInterfaces.GetAddress(0);
        m_wifiApAddresses.insert(apWifiIp.Get());
        for (uint32_t addrIndex = 1; addrIndex < wifiInterfaces.GetN(); ++addrIndex)
        {
            Ipv4Address staAddr = wifiInterfaces.GetAddress(addrIndex);
            m_wifiStationAddresses.insert(staAddr.Get());
        }

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

    // NR UEのIP設定とCERへのルーティングの設定
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

    // nth==5: マルチRAT端末のアクセス状態を初期化
    if (m_nth == 5)
    {
        // Wi-Fi APゲートウェイIPを記録
        m_wifiApGatewayIps.resize(APnum, Ipv4Address("0.0.0.0"));
        for (uint32_t i = 0; i < APnum; ++i)
        {
            if (i < wifiAPs.size() && wifiAPs[i] != nullptr)
            {
                Ptr<Ipv4> apIpv4 = wifiAPs[i]->GetObject<Ipv4>();
                if (apIpv4)
                {
                    // Wi-Fi AP側のIPを探す (10.1.{i}.x)
                    std::stringstream wifiBase;
                    wifiBase << "10.1." << i << ".";
                    std::string prefix = wifiBase.str();
                    for (uint32_t ifIdx = 0; ifIdx < apIpv4->GetNInterfaces(); ++ifIdx)
                    {
                        for (uint32_t a = 0; a < apIpv4->GetNAddresses(ifIdx); ++a)
                        {
                            std::ostringstream addrStr;
                            addrStr << apIpv4->GetAddress(ifIdx, a).GetLocal();
                            if (addrStr.str().substr(0, prefix.size()) == prefix)
                            {
                                m_wifiApGatewayIps[i] = apIpv4->GetAddress(ifIdx, a).GetLocal();
                            }
                        }
                    }
                }
            }
        }

        // NRゲートウェイIPを記録
        m_nrGateway = m_nrEpcHelper ? m_nrEpcHelper->GetUeDefaultGatewayAddress() : Ipv4Address("0.0.0.0");

        InitializeTermAccessState();
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

void NetSim::InitializeTermAccessState()
{
    Ipv4StaticRoutingHelper rh;
    m_termAccessState.resize(termNum);
    m_termAppStates.resize(termNum);

    for (uint32_t i = 0; i < termNum; ++i)
    {
        Ptr<Node> term = terms[i];
        if (term == nullptr)
        {
            continue;
        }

        TermAccessState& state = m_termAccessState[i];
        state.currentAp = m_termData[i].apNo;
        // AP1→NR, AP2以降→WIFI
        if (state.currentAp == 1)      state.currentRat = RatType::NR;
        else                           state.currentRat = RatType::WIFI;
        state.nrIpv4 = Ipv4Address("0.0.0.0");
        state.nrIfIndex = 0;
        state.wifiIpv4.resize(APnum, Ipv4Address("0.0.0.0"));
        state.wifiIfIndex.resize(APnum, 0);
        state.lastSwitchTime = Seconds(0.0);
        state.switchInProgress = false;

        // TermAppState初期化
        m_termAppStates[i].appType = m_termData[i].use_appli;
        m_termAppStates[i].primaryPort = 0;
        m_termAppStates[i].secondaryPort = 0;
        m_termAppStates[i].browserGeneration = 0;

        Ptr<Ipv4> ipv4 = term->GetObject<Ipv4>();
        if (!ipv4)
        {
            continue;
        }

        // 全インターフェースをスキャンしてNR/LTE/Wi-Fiを分類
        for (uint32_t ifIdx = 0; ifIdx < ipv4->GetNInterfaces(); ++ifIdx)
        {
            for (uint32_t a = 0; a < ipv4->GetNAddresses(ifIdx); ++a)
            {
                Ipv4Address addr = ipv4->GetAddress(ifIdx, a).GetLocal();
                if (addr == Ipv4Address("127.0.0.1"))
                {
                    continue;
                }

                std::ostringstream addrStr;
                addrStr << addr;
                std::string addrS = addrStr.str();

                // NR: 7.x.x.x
                if (addrS.substr(0, 2) == "7.")
                {
                    state.nrIpv4 = addr;
                    state.nrIfIndex = ifIdx;
                }
                else
                {
                    // Wi-Fi: 10.1.{apIdx}.x
                    for (uint32_t ap = 0; ap < APnum; ++ap)
                    {
                        std::ostringstream prefix;
                        prefix << "10.1." << ap << ".";
                        if (addrS.substr(0, prefix.str().size()) == prefix.str())
                        {
                            state.wifiIpv4[ap] = addr;
                            state.wifiIfIndex[ap] = ifIdx;
                        }
                    }
                }
            }
        }

        // デフォルトルートを全削除してアクティブなものだけ再設定
        Ptr<Ipv4StaticRouting> rt = rh.GetStaticRouting(ipv4);
        // 既存のデフォルトルートを削除（後ろから削除）
        for (int32_t r = static_cast<int32_t>(rt->GetNRoutes()) - 1; r >= 0; --r)
        {
            Ipv4RoutingTableEntry entry = rt->GetRoute(static_cast<uint32_t>(r));
            if (entry.GetDest() == Ipv4Address("0.0.0.0") &&
                entry.GetDestNetworkMask() == Ipv4Mask("0.0.0.0"))
            {
                rt->RemoveRoute(static_cast<uint32_t>(r));
            }
        }

        // 全RANのインターフェースはUpのまま維持し、default routeだけで
        // 現在利用するRANを選択する。SetDownするとNR RRC/WiFi関連付けが
        // 解放され、ハンドオーバー後に再確立遅延が発生する。
        if (state.currentRat == RatType::NR)
        {
            rt->SetDefaultRoute(m_nrGateway, state.nrIfIndex);
        }
        else
        {
            uint32_t apIdx = static_cast<uint32_t>(state.currentAp - 1);
            if (apIdx < APnum && state.wifiIfIndex[apIdx] > 0 &&
                m_wifiApGatewayIps[apIdx] != Ipv4Address("0.0.0.0"))
            {
                rt->SetDefaultRoute(m_wifiApGatewayIps[apIdx], state.wifiIfIndex[apIdx]);
            }
        }

        // 各端末のIPスタック
        // auto ratStr = [](RatType r) -> const char* {
        //     if (r == RatType::NR)  return "NR";
        //     if (r == RatType::LTE) return "LTE";
        //     return "WIFI";
        // };
        // std::cout << "[InitTermAccess] term=" << i
        //           << " ap=" << state.currentAp
        //           << " rat=" << ratStr(state.currentRat)
        //           << " nrIP=" << state.nrIpv4
        //           << " nrIF=" << state.nrIfIndex
        // for (uint32_t ap = 1; ap < APnum; ++ap)
        // {
        //     std::cout << " wifiIP[" << ap << "]=" << state.wifiIpv4[ap]
        //               << " wifiIF[" << ap << "]=" << state.wifiIfIndex[ap];
        // }
        // std::cout << std::endl;
    }
}

Ipv4Address NetSim::GetActiveIpv4(uint32_t termIdx) const
{
    if (m_nth != 5 || termIdx >= m_termAccessState.size())
    {
        // nth!=5の場合は従来のGetPrimaryIpv4相当
        if (termIdx < terms.size() && terms[termIdx] != nullptr)
        {
            Ptr<Ipv4> ipv4 = terms[termIdx]->GetObject<Ipv4>();
            if (ipv4)
            {
                for (uint32_t ifIdx = 0; ifIdx < ipv4->GetNInterfaces(); ++ifIdx)
                {
                    for (uint32_t a = 0; a < ipv4->GetNAddresses(ifIdx); ++a)
                    {
                        Ipv4Address addr = ipv4->GetAddress(ifIdx, a).GetLocal();
                        if (addr != Ipv4Address("127.0.0.1"))
                        {
                            return addr;
                        }
                    }
                }
            }
        }
        return Ipv4Address("0.0.0.0");
    }

    const auto& state = m_termAccessState[termIdx];
    if (state.currentRat == RatType::NR)
    {
        return state.nrIpv4;
    }
    uint32_t apIdx = static_cast<uint32_t>(state.currentAp - 1);
    if (apIdx < state.wifiIpv4.size())
    {
        return state.wifiIpv4[apIdx];
    }
    return Ipv4Address("0.0.0.0");
}

} // namespace ns3
