

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

NetSim::NetSim(){
    termNum = 1;
    wifiAPNum = 1;
    m_nth = 0;
    m_mob = 1;
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
    wifiAPNum = static_cast<uint32_t>(setting.baseStations);
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

    InitializeNodeContainers();
    CreateWifiApNodes();
    CreateMonitorNodes();
    CreateTerminalNodes();
    CreateRouterNodes();
    CreateServerNodes();
}

void NetSim::InitializeNodeContainers()
{
    p2pNodes.resize(wifiAPNum);
    wifiNodes.resize(wifiAPNum);
    p2pDevices.resize(wifiAPNum);
    wifiDevices.resize(wifiAPNum);
}

void NetSim::CreateWifiApNodes()
{
    wifiAPs.clear();
    for (uint32_t i = 0; i < wifiAPNum; ++i)
    {
        Ptr<Node> apNode = CreateObject<Node>();
        wifiNodes[i].Add(apNode);
        wifiAPs.push_back(apNode);
    }
}

void NetSim::CreateMonitorNodes()
{
    monitorTerminals.assign(3, nullptr);
    uint32_t monitorCount = std::min<uint32_t>(monitorTerminals.size(), wifiAPNum);

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
        wifiNodes[m_termData[i].apNo - 1].Add(term);
        terms.push_back(term);
    }
}

void NetSim::CreateRouterNodes()
{
    for (uint32_t i = 0; i < wifiAPNum; ++i)
    {
        Ptr<Node> router = CreateObject<Node>();
        p2pNodes[i].Add(wifiAPs[i]);
        p2pNodes[i].Add(router);
        routers.push_back(router);
    }
}

void NetSim::CreateServerNodes()
{
    server_udpVoice = CreateObject<Node>();
    server_udpVideo = CreateObject<Node>();
    server_rtt = CreateObject<Node>();

    csmaNodes.Add(server_rtt);
    csmaNodes.Add(server_udpVideo);
    csmaNodes.Add(server_udpVoice);

    for (const auto& router : routers)
    {
        csmaNodes.Add(router);
    }
}

Vector NetSim::GetMonitorPosition(uint32_t apId) const
{
    switch (apId)
    {
    case 0:
        return Vector(0.0, -25.0, 0.0);
    case 1:
        return Vector(25.0, 25.0, 0.0);
    case 2:
        return Vector(-25.0, 25.0, 0.0);
    default:
        return Vector(0.0, 0.0, 0.0);
    }
}

void NetSim::ConfigureDataLinkLayer(){
    std::cout << "==== ConfigureDataLinkLayer ====" << std::endl;
    NS_LOG_FUNCTION(this);

    ConfigureWifiDevices();
    ConfigureMobility();
    ConfigureMonitorPlacement();
    ConfigureP2PDevices();
    ConfigureCsmaDevices();
}

void NetSim::ConfigureWifiDevices()
{
    NS_LOG_LOGIC("set wifi devices");
    for (uint32_t i = 0; i < wifiAPNum; ++i)
    {
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
        mobility.Install(monitor);
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

void NetSim::ConfigureCsmaDevices()
{
    NS_LOG_LOGIC("set csma device");
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("1Gbps"));
    csma.SetChannelAttribute("Delay", StringValue("0.5ms"));
    csmaDevices = csma.Install(csmaNodes);
    const std::string csmast = std::string(OUTPUT_DIR) + "csma";
    csma.EnablePcapAll(csmast);
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
    phy.Set("Antennas", UintegerValue(2));
    phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(2));
    phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(2));
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");
    wifi.ConfigHeOptions("BssColor", UintegerValue((count % 63) + 1));

    WifiMacHelper mac;
    std::stringstream ssidss;
    ssidss << "main-SSID-" << count;
    Ssid ssid = Ssid (ssidss.str());
    mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
    wifiDevices[count] = wifi.Install (phy, mac, wifiNodes[count].Get(0));

    for(uint32_t i=1; i<wifiNodes[count].GetN(); i++){
        mac.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssid));
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
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                               "Exponent", DoubleValue(2.2));
    channel.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                               "m0", DoubleValue(1.0),
                               "m1", DoubleValue(1.0),
                               "m2", DoubleValue(1.0));

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("RxGain", DoubleValue(0));
    phy.Set("Antennas", UintegerValue(2));
    phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(2));
    phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(2));
    phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");
    wifi.ConfigHeOptions("BssColor", UintegerValue((count % 63) + 1));

    WifiMacHelper mac;
    std::stringstream ssidss;
    ssidss << "main-SSID-" << count;
    Ssid ssid = Ssid (ssidss.str());

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid));

    wifiDevices[count] = wifi.Install(phy, mac, wifiNodes[count].Get(0));

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid));

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
    for (uint32_t i = 0; i < wifiAPNum; ++i)
    {
        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        Ptr<ListPositionAllocator> posList = CreateObject<ListPositionAllocator>();

        if (i == 0)
        {
            posList->Add(Vector(0.0, -25.0, 0.0));
        }
        else
        {
            const int* offset = kOffsets[i - 1];
            posList->Add(Vector(kBase * offset[0], kBase * offset[1], 0.0));
        }

        mobility.SetPositionAllocator(posList);
        mobility.Install(wifiAPs[i]);
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

    for (const auto& term : terms)
    {
        mobility.Install(term);
    }
}

void NetSim::AttachMonitorApplication(uint32_t apId, Ptr<Node> monitor)
{
    if (monitor == nullptr || apId >= wifiAPs.size())
    {
        return;
    }

    Ptr<Ipv4> serverIpv4 = server_rtt->GetObject<Ipv4>();
    if (serverIpv4 == nullptr)
    {
        return;
    }

    Ipv4Address serverAddress = serverIpv4->GetAddress(1, 0).GetLocal();
    Ptr<APMonitorTerminal> monitorApp = CreateObject<APMonitorTerminal>(apId, serverAddress, serverAddress);
    monitor->AddApplication(monitorApp);
    monitorApp->SetStartTime(Seconds(1.0));
    monitorApp->SetStopTime(Seconds(6.0));
}

void NetSim::ConfigureP2P(uint32_t count){
    std::cout << "==== ConfigureP2P ====" << std::endl;
    NS_LOG_FUNCTION(this);

    /*PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute  ("DataRate", StringValue ("100Mbps"));
    pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));
    pointToPoint.SetQueue ("ns3::DropTailQueue", "MaxPackets", UintegerValue (10000));*/
    CsmaHelper csma;
    csma.SetChannelAttribute ("DataRate", StringValue ("1Gbps"));
    csma.SetChannelAttribute ("Delay"   , StringValue("0.1ms"));
    p2pDevices[count] = csma.Install (p2pNodes[count]);
    std::stringstream ss;
    ss << OUTPUT_DIR << "pointToPoint" << count;
    //pointToPoint.EnablePcapAll(ss.str());
    csma.EnablePcapAll(ss.str());
}

void NetSim::ConfigureNetworkLayer(){
    std::cout << "==== ConfigureNetworkLayer ====" << std::endl;
    NS_LOG_FUNCTION(this);

    NS_LOG_LOGIC("Install internet stack");
    InternetStackHelper stack;
	stack.InstallAll();

    NS_LOG_LOGIC("Install ipv4 addresses");
	Ipv4AddressHelper address;

	address.SetBase ("10.0.0.0", "255.255.255.0");
	Ipv4InterfaceContainer csmaInterfaces;
	csmaInterfaces = address.Assign (csmaDevices);

    for(uint32_t i=0; i<wifiAPNum; i++){
        std::stringstream ss;
        ss << "10.0." << i+1 << ".0";
        address.SetBase(Ipv4Address(ss.str().c_str()), "255.255.255.0");
        Ipv4InterfaceContainer p2pInterfaces;
        p2pInterfaces = address.Assign(p2pDevices[i]);
    }

    Ipv4StaticRoutingHelper staticRouting;
    Ptr<Ipv4StaticRouting> routerStatic;
    //routerStatic = staticRouting.GetStaticRouting(router->GetObject<Ipv4>());

    for(uint32_t i=0; i<wifiAPNum; i++){
        std::stringstream ss1;
        ss1 << "10.1." << i << ".0";
        Ipv4Address baseip = Ipv4Address(ss1.str().c_str());
        std::stringstream ss2;
        ss2 << "10.1." << i << ".1";
        // Ipv4Address gateway = Ipv4Address(ss2.str().c_str()); // 未使用変数をコメントアウト
        address.SetBase(baseip, "255.255.255.0");
        Ipv4InterfaceContainer wifiInterfaces;
        wifiInterfaces = address.Assign(wifiDevices[i]);
/*
        NS_LOG_LOGIC("set static routing");
        routerStatic->AddNetworkRouteTo(baseip, Ipv4Mask("255.255.255.0"), gateway, 1);

        NS_LOG_LOGIC("set default gateway");
        Ptr<Ipv4StaticRouting> termStatic;
        for(uint32_t termID=1; termID<wifiNodes[i].GetN(); termID++){
            termStatic = staticRouting.GetStaticRouting(wifiNodes[i].Get(termID)->GetObject<Ipv4>());
            termStatic->SetDefaultRoute(gateway, 1);
        }*/
    }

    NS_LOG_LOGIC("set ipv4 routing table");
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

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

    // サーバのインストール
    NS_LOG_LOGIC("install server app");
    Ptr<KamedaAppServer> appServer = CreateObject<KamedaAppServer>(m_apSelectionInput);
    server_rtt->AddApplication(appServer);
    appServer->SetStartTime(Seconds(1.0));
    appServer->SetStopTime(Seconds(5.0));

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
        UdpEchoClientHelper udpClient(server_udpVoice->GetObject<Ipv4>()->GetAddress(1,0).GetLocal(), port);
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
        UdpTraceClientHelper udpClient(server_udpVideo->GetObject<Ipv4>()->GetAddress(1,0).GetLocal(), multicast_port, traceFile);

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
    SetAppLayer();

    std::cout << "=====Simulator::Start()=====" << std::endl;
    Simulator::Run();
    Simulator::Destroy();
    std::cout << "=====Simulator::End()=====" << std::endl;
}

}      //namespace
