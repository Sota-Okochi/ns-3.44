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
#include "ns3/waypoint-mobility-model.h"

#include <sstream>
#include <iomanip>
#include <vector>
#include <fstream>
#include <random>
#include <algorithm>

namespace ns3 {

extern int G_nth;
const std::string DATA_DIR = "/home/sota/ns-3.44/TextData/";

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
    void SetVoiceApp();
    void SetVideoApp();
    void SetGreedy();

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

    std::vector<TermData> m_termData;

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
