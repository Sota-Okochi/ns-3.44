#include "NetSim.h"

NS_LOG_COMPONENT_DEFINE("researchMain");

namespace {

struct BaselineSetting
{
    int baseStations = 0;
    int terminals = 0;
    std::vector<int> capacities;
    std::vector<double> initialRtt;
};

std::string Trim(const std::string& str)
{
    const auto begin = str.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos)
    {
        return "";
    }
    const auto end = str.find_last_not_of(" \t\n\r");
    return str.substr(begin, end - begin + 1);
}

bool ExtractJsonInt(const std::string& content, const std::string& key, int& out)
{
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*(\\d+)");
    std::smatch match;
    if (std::regex_search(content, match, re))
    {
        out = std::stoi(match[1]);
        return true;
    }
    return false;
}

bool ExtractJsonArrayInt(const std::string& content, const std::string& key, std::vector<int>& out)
{
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch match;
    if (!std::regex_search(content, match, re))
    {
        return false;
    }
    std::stringstream ss(match[1].str());
    std::string item;
    out.clear();
    while (std::getline(ss, item, ','))
    {
        item = Trim(item);
        if (!item.empty())
        {
            out.push_back(std::stoi(item));
        }
    }
    return !out.empty();
}

bool ExtractJsonArrayDouble(const std::string& content, const std::string& key, std::vector<double>& out)
{
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch match;
    if (!std::regex_search(content, match, re))
    {
        return false;
    }
    std::stringstream ss(match[1].str());
    std::string item;
    out.clear();
    while (std::getline(ss, item, ','))
    {
        item = Trim(item);
        if (!item.empty())
        {
            out.push_back(std::stod(item));
        }
    }
    return !out.empty();
}

bool LoadBaselineSetting(const std::string& path, BaselineSetting& setting)
{
    std::ifstream ifs(path);
    if (ifs.fail())
    {
        std::cerr << "Failed to open setting config: " << path << std::endl;
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (!ExtractJsonInt(content, "baseStations", setting.baseStations))
    {
        std::cerr << "Failed to parse baseStations from setting config" << std::endl;
        return false;
    }
    if (!ExtractJsonInt(content, "terminals", setting.terminals))
    {
        std::cerr << "Failed to parse terminals from setting config" << std::endl;
        return false;
    }
    if (!ExtractJsonArrayInt(content, "capacities", setting.capacities))
    {
        setting.capacities.assign(setting.baseStations, 100);
    }
    if (!ExtractJsonArrayDouble(content, "initialRttHungarian", setting.initialRtt))
    {
        setting.initialRtt.assign(setting.baseStations, 50.0);
    }
    if (setting.capacities.size() < static_cast<size_t>(setting.baseStations))
    {
        setting.capacities.resize(setting.baseStations, 100);
    }
    if (setting.initialRtt.size() < static_cast<size_t>(setting.baseStations))
    {
        setting.initialRtt.resize(setting.baseStations, 50.0);
    }
    return true;
}

} // namespace

namespace ns3 {

// --- Readability helpers (no behavior change) ---
static const Ipv4Address UE_NET("7.0.0.0");
static const Ipv4Mask    UE_MASK("255.0.0.0");

static inline void EnableIpForwardIfPresent(Ptr<Ipv4> ipv4)
{
    if (ipv4)
    {
        ipv4->SetAttribute("IpForward", BooleanValue(true));
    }
}

static Ipv4Address GetPrimaryIpv4(Ptr<Node> node)
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

int G_nth = 0;

    std::vector<std::string> split2(const std::string& input, char delimiter)
    {
        std::istringstream stream(input);

        std::string field;
        std::vector<std::string> result;
        while (std::getline(stream, field, delimiter)) {
            result.push_back(field);
        }
        return result;
    }   //split

static void PingRtt (uint16_t seq, Time rtt)
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

static void DumpIpv4Info(const std::string& title, Ptr<Node> node)
{
    if (!node) return;
    std::cout << "[ROUTE] === " << title << " (Node " << node->GetId() << ") ===" << std::endl;
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (!ipv4)
    {
        std::cout << "[ROUTE] (no ipv4)" << std::endl;
        return;
    }
    // IF一覧
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
    // 経路一覧
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

NetSim::NetSim()
{
    termNum = 1;
    APnum = 1;
    m_nth = 0;
    m_mob = 1;
    server_udpVoice = nullptr;
    server_udpVideo = nullptr;
    server_rtt = nullptr;
    server_streaming = nullptr;
    server_browser = nullptr;
    remote_host = nullptr;
    cerNode = nullptr;
    m_remoteHostAddress = Ipv4Address::GetZero();
}

NetSim::~NetSim(){
}

//設定値をファイルから読み込む
void NetSim::Init(int argc, char *argv[]){
    NS_LOG_FUNCTION(this);

    CommandLine cmd;
    cmd.AddValue("nth", "1(st) is Random, 2(nd) is Greedy, 3(rd) is Hungarian", m_nth);
    cmd.AddValue("mob", "1 is constant, 2 is randomwalk", m_mob);
    cmd.Parse(argc, argv);
    G_nth = m_nth;

    //APと端末の数
    BaselineSetting setting;
    const std::string settingPath = std::string(INPUT_DIR) + "setting.json";
    if (!LoadBaselineSetting(settingPath, setting))
    {
        return;
    }
    APnum = static_cast<uint32_t>(setting.baseStations);
    termNum = static_cast<uint32_t>(setting.terminals);

    m_apSelectionInput.baseStations = setting.baseStations;
    m_apSelectionInput.terminals = setting.terminals;
    m_apSelectionInput.capacities = setting.capacities;
    m_apSelectionInput.initialRtt = setting.initialRtt;

    //端末データの読み込み
    std::string filename2;
    if(m_nth == 1){
        filename2 = std::string(INPUT_DIR) + "termData_1st.txt";
    }else if(m_nth == 2){
        filename2 = std::string(INPUT_DIR) + "termData_2nd.txt";
    }else if(m_nth == 3){
        filename2 = std::string(INPUT_DIR) + "reconnect_hungarian.txt";
    }else{
        std::cerr << "nth error" << std::endl;
    }
    std::ifstream ifs2(filename2);
    if(ifs2.fail()){
        std::cerr << "No Input File 2" << std::endl;
        return;
    }
    for(std::string line; std::getline(ifs2, line); ){
        std::vector<std::string> ret = split(line, ' ');
        std::stringstream ss1, ss2, ss3, ss4;
        TermData data;
        if(m_nth == 1 || m_nth ==2){
            ss1 << ret.at(1);
            ss1 >> data.use_appli;
            ss2 << ret.at(2);
            ss2 >> data.apNo;
            ss3 << ret.at(3);
            ss3 >> data.x;
            ss4 << ret.at(4);
            ss4 >> data.y;
        }else{
            ss1 << ret.at(1);
            ss1 >> data.use_appli;
            ss2 << ret.at(2);
            ss2 >> data.apNo;
            data.x = 0.0;
            data.y = 0.0;
        }
        m_termData.push_back(data);
        m_apSelectionInput.useAppli.push_back(data.use_appli);
    }
}

void NetSim::Configure(){
    std::cout << "==== Configure ====" << std::endl;
    NS_LOG_FUNCTION(this);

    //LogComponentEnable("KamedaAppClient", LOG_LEVEL_INFO);
    //LogComponentEnable("CountRtt", LOG_LEVEL_INFO);
    LogComponentEnable("KamedaAppServer", LOG_LEVEL_INFO);
    //LogComponentEnable("ConnectManager", LOG_LEVEL_INFO);

    //Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("0"));
    //Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));

    PacketMetadata::Enable();

    // disable fragmentation for frames below 2200 bytes
    //Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
    // turn off RTS/CTS for frames below 2200 bytes
    //Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
    // Fix non-unicast data rate to be the same as that of unicast
    //Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode",
    //                    StringValue ("DsssRate1Mbps"));
}

void NetSim::CreateNetworkTopology(){
    std::cout << "==== CreateNetworkTopology ====" << std::endl;
    NS_LOG_FUNCTION(this);

    InitializeNodeContainers(); // p2pNodes, wifiNodes, p2pDevices, wifiDevicesの初期化
    CreateCerNode(); // 共通エッジルータ
    CreateWifiApNodes(); // wifiAPsの初期化
    CreateMonitorNodes(); // monitorの初期化とアタッチ
    CreateTerminalNodes(); // termsの初期化とアタッチ
    CreateRouterNodes(); // routersの初期化とアタッチ
    CreateServerNodes(); // serverの初期化とアタッチ
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
        // AP0はWi-Fiを張らず、NRで置換するためスキップ
        if (i == 0)
        {
            continue;
        }
        ConfigureLTE(i);
    }
}

void NetSim::ConfigureMonitorPlacement()
{
    // std::cout << "[DBG] ConfigureMonitorPlacement monitorsN=" << monitorTerminals.size() << std::endl;
    for (uint32_t apId = 0; apId < monitorTerminals.size(); ++apId)
    {
        Ptr<Node> monitor = monitorTerminals[apId];
        // std::cout << "[DBG]  monitor#" << apId << " ptr=" << (monitor ? 1 : 0) << std::endl;
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

void NetSim::ConfigureWifi(uint32_t count){ //count is wifiAP number
    std::cout << "==== ConfigureWifi ====" << std::endl;
    NS_LOG_FUNCTION(this);
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RandomPropagationLossModel");

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("RxGain", DoubleValue(0));
    // 5GHz channel 36 (5180MHz), 20MHz, single stream for robustness
    phy.Set("ChannelNumber", UintegerValue(36));
    phy.Set("ChannelWidth", UintegerValue(20));
    phy.Set("Antennas", UintegerValue(1));
    phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(1));
    phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(1));
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    // 固定レート: HE MCS 7 をデータ、制御は MCS0
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
    //phy.EnablePcapAll(ss.str());
}

void NetSim::ConfigureLTE(uint32_t count){ //count is wifiAP number
    std::cout << "==== ConfigureLTE ====" << std::endl;
    NS_LOG_FUNCTION(this);
    // Spectrum WIFI PHYでHEを安定化
    SpectrumWifiPhyHelper phy;
    // スペクトラム・チャネル構成
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<ConstantSpeedPropagationDelayModel> delay = CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delay);
    Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(loss);
    phy.SetChannel(spectrumChannel);
    phy.Set("RxGain", DoubleValue(0));
    // 単一空間ストリーム
    phy.Set("Antennas", UintegerValue(1));
    phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(1));
    phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(1));
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    // 固定レート: データ HeMcs3 / 制御 HeMcs0（1SS/20MHz向け、保守的）
    wifi.SetRemoteStationManager(
        "ns3::ConstantRateWifiManager",
        "DataMode", StringValue("HeMcs3"),
        "ControlMode", StringValue("HeMcs0")
    );
    // RTS/CTS 有効化で結合性向上
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
    //phy.EnablePcapAll(ss.str());
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
            posList->Add(Vector(0.0, -25.0, 10.0)); // gNB高さ
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

    // std::cout << "[DBG] ConfigureTermMobility termsN=" << terms.size() << std::endl;
    for (uint32_t idx = 0; idx < terms.size(); ++idx)
    {
        Ptr<Node> term = terms[idx];
        // std::cout << "[DBG]  term#" << idx << " ptr=" << (term ? 1 : 0) << std::endl;
        if (term != nullptr)
    {
        mobility.Install(term);
            std::cout << "[DBG]   installed term#" << idx << std::endl;
        }
    }
}

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

void NetSim::ConfigureP2P(uint32_t count){
    std::cout << "==== ConfigureP2P ====" << std::endl;
    NS_LOG_FUNCTION(this);

    // AP-Router 間は本来の PointToPoint を使用（ARP無しで次ホップ解決が容易）
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
    // AP0 だけ 5G NR に置換。WiFi 構成は他APで維持。
    if (APnum == 0 || wifiNodes.empty())
    {
        return;
    }

    // ヘルパ生成
    m_nrHelper = CreateObject<NrHelper>();
    m_nrEpcHelper = CreateObject<NrPointToPointEpcHelper>();
    m_nrHelper->SetEpcHelper(m_nrEpcHelper);

    // 周波数・BWP（Sub-6: 3.5GHz / 40MHz）+ 3GPPチャネル割当
    std::vector<CcBwpCreator::SimpleOperationBandConf> bandConfs = {
        CcBwpCreator::SimpleOperationBandConf(3.5e9, 40e6, 1)
    };
    // 屋外マクロは高架、端末は地上として高さ前提を満たすようにAP/UE高さを上で設定済み
    // シンプル化のためUMa(都市マクロ)モデルを使用
    auto bwpsPair = m_nrHelper->CreateBandwidthParts(bandConfs, "UMa", "Default", "ThreeGpp");
    auto allBwps = bwpsPair.second;
    std::cout << "[DBG][NR] BWP count=" << allBwps.size() << std::endl;

    // gNB = AP0 ノード
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

    // UE = AP0 配下の全ノード（インデックス1以降: 監視端末＋端末群）
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

    // アタッチはIPスタック導入後(ConfigureNrIpAfterNetwork)に実施

    // PGW- CER 間のポイントツーポイントリンクを準備（アドレス付与は ConfigureNetworkLayer で実施）
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
    // まずUEノードへIPスタックを念のため導入
    InternetStackHelper internet;
    for (auto it = m_nrUeDevs.Begin(); it != m_nrUeDevs.End(); ++it)
    {
        Ptr<Node> n = (*it)->GetNode();
        if (!n->GetObject<Ipv4>())
        {
            internet.Install(n);
        }
    }

    // UEにIPv4アドレスを割当
    Ipv4InterfaceContainer ueIf = m_nrEpcHelper->AssignUeIpv4Address(m_nrUeDevs);
    (void)ueIf;

    // UEのデフォルトGW設定
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

    // IPが整ったので、ここでアタッチを実施
    if (m_nrGnbDevs.GetN() > 0 && m_nrUeDevs.GetN() > 0)
    {
        m_nrHelper->AttachToClosestGnb(m_nrUeDevs, m_nrGnbDevs);
    }
}

void NetSim::SetAppLayer(){
    NS_LOG_FUNCTION(this);

    SetGreedy();
    SetKamedaModule();
    SetVoiceApp();
    SetVideoApp();
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

    // AP選択サーバのインストール（RemoteHost上）
    NS_LOG_LOGIC("install remote host Kameda server");
    Ptr<KamedaAppServer> appServer = CreateObject<KamedaAppServer>(m_apSelectionInput);
    remote_host->AddApplication(appServer);
    appServer->SetStartTime(Seconds(1.0));
    appServer->SetStopTime(Seconds(7.0));

    // RTTフォワーダ（UDP受信→TCP送信）を server_rtt に配置
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

    // 監視端末ノードのアプリ設定（CreateNetworkTopologyで生成済みを利用）
    if (monitorTerminals.size() < 3) {
        monitorTerminals.resize(3, nullptr);
    }

    for(uint32_t apId = 0; apId < monitorTerminals.size(); apId++) {
        Ptr<Node> monitor = monitorTerminals[apId];
        AttachMonitorApplication(apId, monitor);
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
        if(m_termData[i].use_appli != 3){        //通話アプリケーションの場合以外
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

void NetSim::SetVideoApp(void){

    NS_LOG_LOGIC("install video apps");

    Ipv4Address videoServerAddress = GetPrimaryIpv4(server_udpVideo);
    if (videoServerAddress == Ipv4Address("0.0.0.0"))
    {
        NS_LOG_WARN("Video server address unavailable");
        return;
    }

    uint16_t multicast_port = 10000; // ポート番号をさらに別の値に変更
    for(uint32_t i=0; i<terms.size(); i++){
        if(m_termData[i].use_appli != 2){        //動画ストリーミング場合以外
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

void NetSim::RunSim(){

    NS_LOG_FUNCTION(this);

    Configure();
    CreateNetworkTopology();
    ConfigureDataLinkLayer();
    ConfigureNetworkLayer();
    SetAppLayer(); // 各種アプリケーションの設定

    std::cout << "=====Simulator::Start()=====" << std::endl;
    Simulator::Run();
    Simulator::Destroy();
    std::cout << "=====Simulator::End()=====" << std::endl;
}

}      //namespace
