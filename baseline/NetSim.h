#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/animation-interface.h"
#include "ns3/netanim-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/lte-module.h"
#include "ns3/KamedaAppClient.h"
#include "ns3/KamedaAppServer.h"
#include "ns3/APselection.h"
#include "ns3/waypoint-mobility-model.h"
#include "ns3/APMonitorTerminal.h"

#include <sstream>
#include <iomanip>
#include <vector>
#include <fstream>
#include <random>
#include <algorithm>

namespace ns3 {

extern int G_nth;
const std::string DATA_DIR = "/home/sota/ns-3.44/OUTPUT/";

enum APID{
    LTE = 0,
    wifi1,
    wifi2,
    wifi3,
    wifi4,
};

struct TermData
{
    int use_appli;
    int apNo;
    double x;
    double y;
};

class NetSim{
public:
    NetSim();
    ~NetSim();

    void RunSim();
    void Init(int, char **);

private:
    void Configure();
    void CreateNetworkTopology();
    void ConfigureDataLinkLayer();
    void ConfigureWifi(uint32_t count);
    void ConfigureLTE(uint32_t count);
    void ConfigureP2P(uint32_t count);
    void ConfigureMobility();
    void ConfigureNetworkLayer();
    //void ConfigureAnimation();
    void SetAppLayer();
    void SetKamedaModule();
    void SetKamedaServerOnly();  // 監視端末方式用：サーバーのみ起動
    void SetVoiceApp();
    void SetVideoApp();
    void SetAutoLoadGeneration();  // ns-3標準機能による自動負荷生成
    void SetGreedy();
    
    bool LoadTermDataFromSqlite(const std::string& dbPath);

    // 監視端末関連の関数
    void SetupMonitorTerminals();
    void ConfigureMonitorTerminal(uint32_t apId, Ptr<Node> monitorNode, Ipv4Address targetAP);

    int m_nth;
    int m_mob;

    NodeContainer csmaNodes;
    std::vector<NodeContainer> p2pNodes;
    std::vector<NodeContainer> wifiNodes;

    NetDeviceContainer csmaDevices;
    std::vector<NetDeviceContainer> p2pDevices;
    std::vector<NetDeviceContainer> wifiDevices;

    uint32_t termNum;
    uint32_t wifiAPNum;
    std::vector<Ptr<Node> > terms;
    std::vector<Ptr<Node> > wifiAPs;
    std::vector<Ptr<Node> > routers;
    Ptr<Node> server_udpVoice;
    Ptr<Node> server_udpVideo;
    Ptr<Node> server_ping;
    Ptr<Node> server_rtt;
    
    // 監視端末用
    std::vector<Ptr<Node> > monitorTerminals;

    std::vector<TermData> m_termData;
    ApSelectionInput m_apSelectionInput;

    std::vector<std::string> split(const std::string& input, char delimiter)
{
    std::istringstream stream(input);

    std::string field;
    std::vector<std::string> result;
    while (std::getline(stream, field, delimiter)) {
        result.push_back(field);
    }
    return result;
}   //split


};  //class

}   //namespace
