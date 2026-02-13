#include "NetSim.h"
#include "ns3/system-path.h"

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

namespace {

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

} // namespace

NetSim::NetSim()
{
    termNum = 1;
    APnum = 1;
    m_nth = 0;
    m_mob = 1;
    server_udpVoice = nullptr;
    server_udpVideo = nullptr;
    server_rtt = nullptr;
    server_onlineGame = nullptr;
    server_browser = nullptr;
    remote_host = nullptr;
    cerNode = nullptr;
    m_remoteHostAddress = Ipv4Address::GetZero();
    m_cycleCount = 2;
    m_cycleDuration = Seconds(6.7);
    m_simulationDuration = m_cycleDuration * m_cycleCount;
    m_browserRequestInterval = Seconds(1.5);
    m_browserRequestCount = 4;
    m_enableOnlineGameTracing = true;
    m_goodputReportScheduled = false;
    m_currentCycle = 0;

    // Ensure OUTPUT directory exists before writing trace or log files
    SystemPath::MakeDirectories(OUTPUT_DIR);
}

NetSim::~NetSim(){
}

void NetSim::Init(int argc, char *argv[]){
    NS_LOG_FUNCTION(this);

    CommandLine cmd;
    cmd.AddValue("nth", " 3(rd) is static app, 4(th) is random app", m_nth);
    cmd.AddValue("mob", "1 is constant, 2 is randomwalk", m_mob);
    cmd.Parse(argc, argv);
    G_nth = m_nth;

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

    m_termData.clear();
    m_apSelectionInput.useAppli.clear();
    m_apSelectionInput.initialAp.clear();

    if (m_nth == 4)
    {
        Ptr<UniformRandomVariable> apRand = CreateObject<UniformRandomVariable>();
        Ptr<UniformRandomVariable> appRand = CreateObject<UniformRandomVariable>();
        uint32_t apCount = std::max<uint32_t>(APnum, 1);

        for (uint32_t i = 0; i < termNum; ++i)
        {
            TermData data;
            const double appDraw = appRand->GetValue(0.0, 1.0);
            if (appDraw < 0.30)
            {
                data.use_appli = 1;
            }
            else if (appDraw < 0.60)
            {
                data.use_appli = 2;
            }
            else if (appDraw < 0.70)
            {
                data.use_appli = 3;
            }
            else
            {
                data.use_appli = 4;
            }
            data.apNo = static_cast<int>(apRand->GetInteger(1, apCount));
            data.x = 0.0;
            data.y = 0.0;
            m_termData.push_back(data);
            m_apSelectionInput.useAppli.push_back(data.use_appli);
            m_apSelectionInput.initialAp.push_back(data.apNo);
        }
        std::cout << "初期AP番号: [";
        for (size_t i = 0; i < m_apSelectionInput.initialAp.size(); ++i)
        {
            std::cout << m_apSelectionInput.initialAp[i];
            if (i + 1 != m_apSelectionInput.initialAp.size())
            {
                std::cout << ", ";
            }
        }
        std::cout << "]" << std::endl;
        std::cout << "初期アプリ番号: [";
        for (size_t i = 0; i < m_apSelectionInput.useAppli.size(); ++i)
        {
            std::cout << m_apSelectionInput.useAppli[i];
            if (i + 1 != m_apSelectionInput.useAppli.size())
            {
                std::cout << ", ";
            }
        }
        std::cout << "]" << std::endl;
    }
    else
    {
        std::string filename2;
        if(m_nth == 1){
            filename2 = std::string(INPUT_DIR) + "termData_1st.txt";
        }else if(m_nth == 2){
            filename2 = std::string(INPUT_DIR) + "termData_2nd.txt";
        }else if(m_nth == 3){
            filename2 = std::string(INPUT_DIR) + "reconnect_hungarian.txt";
        }else{
            std::cerr << "nth error" << std::endl;
            return;
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
            m_apSelectionInput.initialAp.push_back(data.apNo);
        }
    }
    m_activeAssignment = m_apSelectionInput.initialAp;

    // 各基地局の接続数を表示
    uint32_t printLimit = std::min<uint32_t>(termNum, static_cast<uint32_t>(m_termData.size()));
    if (APnum > 0 && printLimit > 0)
    {
        std::vector<uint32_t> apCounts(APnum, 0);
        for (uint32_t i = 0; i < printLimit; ++i)
        {
            const auto& data = m_termData[i];
            uint32_t apIndex = (data.apNo > 0) ? static_cast<uint32_t>(data.apNo - 1) : APnum;
            if (apIndex < apCounts.size())
            {
                ++apCounts[apIndex];
            }
        }
        std::cout << "=== Baseline terminal counts per AP (first " << printLimit << ") ===" << std::endl;
        for (uint32_t i = 0; i < apCounts.size(); ++i)
        {
            std::cout << "AP" << (i + 1) << ": " << apCounts[i] << " terminals" << std::endl;
        }
    }

    // 監視端末と同じAPで確実に通信が発生するよう、各APに最低限の動画端末を割り当てる
    EnsureMonitorVideoTerminals(3);
}

void NetSim::EnsureMonitorVideoTerminals(uint32_t minVideoPerAp)
{
    if (minVideoPerAp == 0 || APnum == 0)
    {
        return;
    }

    bool modified = false;
    for (uint32_t apIndex = 0; apIndex < APnum; ++apIndex)
    {
        uint32_t apNo = apIndex + 1;
        uint32_t current = 0;
        for (const auto& data : m_termData)
        {
            if (data.apNo == static_cast<int>(apNo) && data.use_appli == 2)
            {
                ++current;
            }
        }
        if (current >= minVideoPerAp)
        {
            continue;
        }

        for (auto& data : m_termData)
        {
            if (data.apNo == static_cast<int>(apNo) && data.use_appli != 2)
            {
                data.use_appli = 2;
                ++current;
                modified = true;
                if (current >= minVideoPerAp)
                {
                    break;
                }
            }
        }
        if (current >= minVideoPerAp)
        {
            continue;
        }

        for (auto& data : m_termData)
        {
            if (data.apNo != static_cast<int>(apNo) && data.use_appli == 2)
            {
                data.apNo = static_cast<int>(apNo);
                ++current;
                modified = true;
                if (current >= minVideoPerAp)
                {
                    break;
                }
            }
        }
        if (current >= minVideoPerAp)
        {
            continue;
        }

        while (current < minVideoPerAp)
        {
            TermData extra;
            extra.use_appli = 2;
            extra.apNo = static_cast<int>(apNo);
            extra.x = 0.0;
            extra.y = 0.0;
            m_termData.push_back(extra);
            ++current;
            modified = true;
        }
    }

    if (modified)
    {
        termNum = static_cast<uint32_t>(m_termData.size());
        m_apSelectionInput.terminals = static_cast<int>(termNum);
        m_apSelectionInput.useAppli.clear();
        m_apSelectionInput.initialAp.clear();
        for (const auto& data : m_termData)
        {
            m_apSelectionInput.useAppli.push_back(data.use_appli);
            m_apSelectionInput.initialAp.push_back(data.apNo);
        }
        m_activeAssignment = m_apSelectionInput.initialAp;
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

void NetSim::RunSim(){

    NS_LOG_FUNCTION(this);

    ConfigureCycleParameters();
    Configure();
    CreateNetworkTopology(); // ノードの生成
    ConfigureDataLinkLayer();
    ConfigureNetworkLayer();

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowMonitor = flowmonHelper.InstallAll();
    Ptr<Ipv4FlowClassifier> flowClassifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());

    // 端末別TP計測用にFlowMonitor/Classifierを保持
    m_termFlowMonitor = flowMonitor;
    m_termFlowClassifier = flowClassifier;
    BuildTerminalIpMap();

    SetAppLayer(); // 各種アプリケーションの設定

    if (m_simulationDuration.IsPositive())
    {
        Simulator::Stop(m_simulationDuration);
    }

    Time checkStop = m_simulationDuration.IsZero() ? Seconds(7.0) : m_simulationDuration;
    if (flowMonitor && checkStop.IsPositive())
    {
        Time checkTime = checkStop - MilliSeconds(1);
        if (checkTime.IsNegative())
        {
            checkTime = checkStop;
        }
        Simulator::Schedule(checkTime, &NetSim::CheckFlowMonitor, this, flowMonitor, flowClassifier);
    }

    std::cout << "=====Simulator::Start()=====" << std::endl;
    Simulator::Run();
    Simulator::Destroy();
    std::cout << "=====Simulator::End()=====" << std::endl;
}

void NetSim::CheckFlowMonitor(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier)
{
    NS_LOG_FUNCTION(this);

    if (monitor == nullptr)
    {
        NS_LOG_WARN("FlowMonitor instance is null");
        return;
    }

    monitor->CheckForLostPackets();

    const std::string throughputFile = std::string(OUTPUT_DIR) + "monitor-flow-throughput.csv";
    bool writeHeader = false;
    {
        std::ifstream check(throughputFile);
        if (!check.good() || check.peek() == std::ifstream::traits_type::eof())
        {
            writeHeader = true;
        }
    }
    std::ofstream throughputStream(throughputFile, std::ios::app);
    if (throughputStream.good())
    {
        if (writeHeader)
        {
            throughputStream << "time_s,flow_id,src,dst,protocol,src_port,dst_port,rx_bytes,tx_packets,rx_packets,throughput_bps\n";
        }

        if (classifier)
        {
            const auto stats = monitor->GetFlowStats();
            auto isApWifiAddress = [this](const Ipv4Address& addr) {
                return m_wifiApAddresses.find(addr.Get()) != m_wifiApAddresses.end();
            };
            auto isStationWifiAddress = [this](const Ipv4Address& addr) {
                return m_wifiStationAddresses.find(addr.Get()) != m_wifiStationAddresses.end();
            };
            for (const auto& entry : stats)
            {
                FlowId flowId = entry.first;
                const FlowMonitor::FlowStats& stat = entry.second;
                if (stat.rxPackets == 0 || stat.timeLastRxPacket <= stat.timeFirstTxPacket)
                {
                    continue;
                }

                double duration = (stat.timeLastRxPacket - stat.timeFirstTxPacket).GetSeconds();
                if (duration <= 0.0)
                {
                    continue;
                }

                double throughputBps = static_cast<double>(stat.rxBytes) * 8.0 / duration;
                Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
                bool srcIsSta = isStationWifiAddress(tuple.sourceAddress);
                bool dstIsSta = isStationWifiAddress(tuple.destinationAddress);
                if (!(srcIsSta || dstIsSta))
                {
                    continue;
                }

                throughputStream << Simulator::Now().GetSeconds() << ","
                                 << flowId << ","
                                 << tuple.sourceAddress << ","
                                 << tuple.destinationAddress << ","
                                 << static_cast<uint32_t>(tuple.protocol) << ","
                                 << tuple.sourcePort << ","
                                 << tuple.destinationPort << ","
                                 << stat.rxBytes << ","
                                 << stat.txPackets << ","
                                 << stat.rxPackets << ","
                                 << throughputBps << "\n";
            }
        }
        else
        {
            NS_LOG_WARN("Flow classifier is null; throughput CSV not written.");
        }
    }
    else
    {
        NS_LOG_WARN("Failed to open throughput file: " << throughputFile);
    }

    std::ostringstream filename;
    filename << OUTPUT_DIR << "monitor-flow";
    if (G_nth > 0)
    {
        filename << "_G" << G_nth;
    }
    filename << ".xml";

    monitor->SerializeToXmlFile(filename.str(), true, true);
}

}   // namespace ns3
