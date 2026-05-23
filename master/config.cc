#include "NetSim.h"
#include "ns3/system-path.h"
#include <algorithm>

NS_LOG_COMPONENT_DEFINE("researchMain");

namespace {

// master_log だけを残すため、FlowMonitorの補助出力は止める。
constexpr bool kEnableFlowOutputLogs = false;

struct BaselineSetting
{
    int baseStations = 0;
    int terminals = 0;
    std::vector<int> capacities;
    std::vector<double> initialRtt;
    int numCycles = 10;
    double cycleTimeSec = 3.5;
    double monitorStartSec = 1.1;
    double monitorStopSec = 4.0;
    double browserFirstBurstSec = 1.2;
    double browserSecondBurstSec = 2.1;
    double browserBurstIntervalSec = 1.0;
    int browserNumRequests = 5;
    int browserRequestSize = 512000;
    double cycleEndMarginSec = 0.5;
    int handoverGraceCycles = 2;
    double browserPostHandoverDelaySec = 0.0;
    double terminalTpStopSec = 4.0;
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

bool ExtractJsonDouble(const std::string& content, const std::string& key, double& out)
{
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*([0-9]*\\.?[0-9]+)");
    std::smatch match;
    if (std::regex_search(content, match, re))
    {
        out = std::stod(match[1]);
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
    // オプションパラメータの読み込み（JSONになければ構造体のデフォルト値を維持）
    {
        int tmp = 0;
        double dtmp = 0.0;
        if (ExtractJsonInt(content, "numCycles", tmp)) { setting.numCycles = tmp; }
        if (ExtractJsonDouble(content, "cycleTimeSec", dtmp)) { setting.cycleTimeSec = dtmp; }
        if (ExtractJsonDouble(content, "monitorStartSec", dtmp)) { setting.monitorStartSec = dtmp; }
        if (ExtractJsonDouble(content, "monitorStopSec", dtmp)) { setting.monitorStopSec = dtmp; }
        if (ExtractJsonDouble(content, "browserFirstBurstSec", dtmp)) { setting.browserFirstBurstSec = dtmp; }
        if (ExtractJsonDouble(content, "browserSecondBurstSec", dtmp)) { setting.browserSecondBurstSec = dtmp; }
        if (ExtractJsonDouble(content, "browserBurstIntervalSec", dtmp)) { setting.browserBurstIntervalSec = dtmp; }
        if (ExtractJsonInt(content, "browserNumRequests", tmp)) { setting.browserNumRequests = tmp; }
        if (ExtractJsonInt(content, "browserRequestSize", tmp)) { setting.browserRequestSize = tmp; }
        if (ExtractJsonDouble(content, "cycleEndMarginSec", dtmp)) { setting.cycleEndMarginSec = dtmp; }
        if (ExtractJsonInt(content, "handoverGraceCycles", tmp)) { setting.handoverGraceCycles = tmp; }
        if (ExtractJsonDouble(content, "browserPostHandoverDelaySec", dtmp)) { setting.browserPostHandoverDelaySec = dtmp; }
    }
    // 制約バリデーション
    if (setting.monitorStartSec <= 1.0)
    {
        std::cerr << "[WARN] monitorStartSec=" << setting.monitorStartSec
                  << " must be > 1.0. Resetting to 1.1." << std::endl;
        setting.monitorStartSec = 1.1;
    }
    if (setting.monitorStopSec <= setting.monitorStartSec)
    {
        std::cerr << "[WARN] monitorStopSec=" << setting.monitorStopSec
                  << " must be > monitorStartSec. Resetting to 4.0." << std::endl;
        setting.monitorStopSec = 4.0;
    }
    const double maxStop = setting.cycleTimeSec + setting.monitorStartSec;
    if (setting.monitorStopSec >= maxStop)
    {
        std::cerr << "[WARN] monitorStopSec=" << setting.monitorStopSec
                  << " must be < cycleDuration+monitorStart (" << maxStop << "). Resetting to 4.0." << std::endl;
        setting.monitorStopSec = 4.0;
    }
    if (setting.browserFirstBurstSec <= setting.monitorStartSec)
    {
        std::cerr << "[WARN] browserFirstBurstSec=" << setting.browserFirstBurstSec
                  << " must be > monitorStartSec. Resetting to monitorStart+0.1." << std::endl;
        setting.browserFirstBurstSec = setting.monitorStartSec + 0.1;
    }

    const int browserSecondBurstRequests = std::max(0, setting.browserNumRequests - 1);
    const double lastBrowserBurstSec =
        browserSecondBurstRequests > 0
            ? setting.browserSecondBurstSec +
                  setting.browserBurstIntervalSec * static_cast<double>(browserSecondBurstRequests - 1)
            : setting.browserFirstBurstSec;
    const double browserCompletionGuardSec =
        std::max(1.0, setting.browserBurstIntervalSec * 4.0 + 0.7);
    const double terminalTpStopSec =
        std::max(setting.monitorStopSec, lastBrowserBurstSec + browserCompletionGuardSec);
    setting.terminalTpStopSec = terminalTpStopSec;

    // Ending() が次サイクルに食い込まないことを保証
    if (terminalTpStopSec + setting.cycleEndMarginSec >= setting.cycleTimeSec)
    {
        std::cerr << "[ERROR] terminalTpStopSec(" << terminalTpStopSec
                  << ") + cycleEndMarginSec(" << setting.cycleEndMarginSec
                  << ") must be < cycleTimeSec(" << setting.cycleTimeSec
                  << "). Increase cycleTimeSec so browser bursts can complete before the next cycle."
                  << std::endl;
        NS_FATAL_ERROR("Invalid timing: cycleEndMargin overflow");
    }
    // TP計測ウィンドウ内にブラウザトラフィックが発生することを保証
    if (setting.browserFirstBurstSec >= setting.monitorStopSec)
    {
        std::cerr << "[ERROR] browserFirstBurstSec(" << setting.browserFirstBurstSec
                  << ") must be < monitorStopSec(" << setting.monitorStopSec
                  << "). No browser traffic would be captured in the TP window." << std::endl;
        NS_FATAL_ERROR("Invalid timing: browserFirstBurst outside TP window");
    }
    if (setting.browserSecondBurstSec >= setting.monitorStopSec)
    {
        std::cerr << "[ERROR] browserSecondBurstSec(" << setting.browserSecondBurstSec
                  << ") must be < monitorStopSec(" << setting.monitorStopSec
                  << "). Second browser burst would not be captured in the TP window." << std::endl;
        NS_FATAL_ERROR("Invalid timing: browserSecondBurst outside TP window");
    }
    return true;
}

} // namespace

namespace ns3 {

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
    m_cycleCount = 10;
    m_cycleDuration = Seconds(3.5);
    m_simulationDuration = m_cycleDuration * m_cycleCount;
    m_browserRequestInterval = Seconds(1.0);
    m_browserRequestCount = 5;
    m_monitorStartOffset  = Seconds(1.1);
    m_monitorStopOffset   = Seconds(4.0);
    m_terminalTpStopOffset = Seconds(4.0);
    m_browserFirstRequest = Seconds(1.2);
    m_browserBatchBStart  = Seconds(2.1);
    m_browserRequestBytes = 500u * 1024u;
    m_cycleEndGuard              = Seconds(0.5);
    m_browserPostHandoverDelay   = Seconds(0.0);
    m_terminalTpWindowStart = Seconds(0.0);

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

    BaselineSetting setting;
    const std::string settingPath = std::string(INPUT_DIR) + "setting.json";
    if (!LoadBaselineSetting(settingPath, setting))
    {
        return;
    }
    APnum = static_cast<uint32_t>(setting.baseStations);
    termNum = static_cast<uint32_t>(setting.terminals);
    m_cycleCount             = static_cast<uint32_t>(setting.numCycles);
    m_cycleDuration          = Seconds(setting.cycleTimeSec);
    m_monitorStartOffset     = Seconds(setting.monitorStartSec);
    m_monitorStopOffset      = Seconds(setting.monitorStopSec);
    m_terminalTpStopOffset   = Seconds(setting.terminalTpStopSec);
    m_browserFirstRequest    = Seconds(setting.browserFirstBurstSec);
    m_browserBatchBStart     = Seconds(setting.browserSecondBurstSec);
    m_browserRequestInterval = Seconds(setting.browserBurstIntervalSec);
    m_browserRequestCount    = static_cast<uint32_t>(setting.browserNumRequests);
    m_browserRequestBytes    = static_cast<uint32_t>(setting.browserRequestSize);
    m_cycleEndGuard              = Seconds(setting.cycleEndMarginSec);
    m_browserPostHandoverDelay   = Seconds(setting.browserPostHandoverDelaySec);

    m_apSelectionInput.baseStations = setting.baseStations;
    m_apSelectionInput.terminals = setting.terminals;
    m_apSelectionInput.capacities = setting.capacities;
    m_apSelectionInput.initialRtt = setting.initialRtt;
    m_apSelectionInput.handoverGraceCycles = static_cast<uint32_t>(setting.handoverGraceCycles);
    m_apSelectionInput.nth = m_nth;

    m_termData.clear();
    m_apSelectionInput.useAppli.clear();
    m_apSelectionInput.initialAp.clear();

    if (m_nth == 5)
    {
        Ptr<UniformRandomVariable> apRand = CreateObject<UniformRandomVariable>();
        Ptr<UniformRandomVariable> appRand = CreateObject<UniformRandomVariable>();
        uint32_t apCount = std::max<uint32_t>(APnum, 1);
        
        // アプリの出現率の設定    
        for (uint32_t i = 0; i < termNum; ++i)
        {
            TermData data;
            const double appDraw = appRand->GetValue(0.0, 1.0);
            if (appDraw < 0.40)
            {
                data.use_appli = 1;
            }
            else if (appDraw < 0.80)
            {
                data.use_appli = 2;
            }
            else if (appDraw < 0.86)
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

    // ARPキャッシュタイムアウトを短縮してハンドオーバー後の再解決を速める（案1）
    Config::SetDefault("ns3::ArpCache::AliveTimeout", TimeValue(Seconds(10)));
    Config::SetDefault("ns3::ArpCache::DeadTimeout", TimeValue(MilliSeconds(50)));
    Config::SetDefault("ns3::ArpCache::WaitReplyTimeout", TimeValue(MilliSeconds(200)));

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

    // Keep per-cycle FlowMonitor diagnostics scoped to this simulation run.
    if (kEnableFlowOutputLogs)
    {
        const std::string flowCsvPath = std::string(OUTPUT_DIR) + "flow_per_cycle.csv";
        std::ofstream resetFlowCsv(flowCsvPath, std::ios::trunc);
        if (!resetFlowCsv.good())
        {
            NS_LOG_WARN("Failed to reset flow diagnostics CSV: " << flowCsvPath);
        }

        const std::string browserCsvPath = std::string(OUTPUT_DIR) + "browser_send_events.csv";
        std::ofstream resetBrowserCsv(browserCsvPath, std::ios::trunc);
        if (!resetBrowserCsv.good())
        {
            NS_LOG_WARN("Failed to reset browser send diagnostics CSV: " << browserCsvPath);
        }
    }

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
    if (kEnableFlowOutputLogs && flowMonitor && checkStop.IsPositive())
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

    if (!kEnableFlowOutputLogs)
    {
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
    if (m_nth > 0)
    {
        filename << "_G" << m_nth;
    }
    filename << ".xml";

    monitor->SerializeToXmlFile(filename.str(), true, true);
}

}   // namespace ns3
