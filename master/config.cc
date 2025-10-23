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
    server_streaming = nullptr;
    server_browser = nullptr;
    remote_host = nullptr;
    cerNode = nullptr;
    m_remoteHostAddress = Ipv4Address::GetZero();
}

NetSim::~NetSim(){
}

void NetSim::Init(int argc, char *argv[]){
    NS_LOG_FUNCTION(this);

    CommandLine cmd;
    cmd.AddValue("nth", "1(st) is Random, 2(nd) is Greedy, 3(rd) is Hungarian", m_nth);
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

}   // namespace ns3

