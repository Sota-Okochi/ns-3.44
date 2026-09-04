#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
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
#include "ns3/KamedaAppClient.h"
#include "ns3/KamedaAppServer.h"
#include "ns3/APselection.h"
#include "ns3/waypoint-mobility-model.h"
#include "ns3/APMonitorTerminal.h"
#include "ns3/wifi-helper.h"
#include "ns3/nr-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/tcp-socket-state.h"
#include "RttForwarderApp.h"

#include <sstream>
#include <iomanip>
#include <vector>
#include <unordered_set>
#include <fstream>
#include <random>
#include <algorithm>
#include <regex>

namespace ns3 {

const std::string INPUT_DIR = std::string(PROJECT_SOURCE_PATH) + "/data/";
const std::string OUTPUT_ROOT_DIR = std::string(PROJECT_SOURCE_PATH) + "/OUTPUT/";

// 監視端末の最大数（Wi-Fi AP 数と合わせて使用）
constexpr uint32_t kMaxMonitorTerminals = 3;

enum APID{
    LTE = 0,
    wifi1,
    wifi2,
    wifi3,
    wifi4,
};

enum class RatType { NR, WIFI };

// 各端末の接続状態（どのAP、どのRAT・IP）
struct TermAccessState {
    int currentAp;              // 1-based
    RatType currentRat;         // NR, WIFI, or LTE
    Ipv4Address nrIpv4;         // 7.x.x.x
    std::vector<Ipv4Address> wifiIpv4; // index=apIdx (0-based), Wi-Fi AP毎のIP
    uint32_t nrIfIndex;         // NRデバイスのIpv4インターフェース番号
    std::vector<uint32_t> wifiIfIndex; // Wi-Fi AP毎のインターフェース番号
    Time lastSwitchTime;
    bool switchInProgress;
};

// 各端末のアプリケーション情報
struct TermAppState {
    int appType;                // 1=browser, 2=video, 3=voice, 4=game
    uint16_t primaryPort;
    uint16_t secondaryPort;     // voice/game downlink用
    uint32_t browserGeneration;
    std::vector<Ptr<Application>> clientApps;
    std::vector<Ptr<Application>> serverApps;
};

// 各端末の初期設定
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
    void ConfigureWifiForAP2();
    void ConfigureNrForAp0();
    void ConfigureNrIpAfterNetwork();
    void ConfigureWifiForAP1();
    void ConfigureLteIpAfterNetwork();
    void ConfigureLtePgwCerLink();
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
    void SetBrowserApp();
    void SetOnlineGameApp();
    void CheckFlowMonitor(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier);
    
    bool LoadTermDataFromSqlite(const std::string& dbPath);

    // 監視端末関連の関数
    void ConfigureMonitorTerminal(uint32_t apId, Ptr<Node> monitorNode, Ipv4Address targetAP);

    void InitializeNodeContainers();
    void CreateWifiApNodes();
    void CreateMonitorNodes();
    void CreateTerminalNodes();
    void CreateRouterNodes();
    void CreateServerNodes();
    void CreateCerNode();
    void ConfigureApMobility();
    void ConfigureTermMobility();
    Vector GetMonitorPosition(uint32_t apId) const;
    void AttachMonitorApplication(uint32_t apId, Ptr<Node> monitor);
    void ConfigureMonitorPlacement();
    void ConfigureP2PDevices();
    void ConfigureRouterCerLinks();
    void ConfigureServerCerLinks();
    void ConfigurePgwCerLink();
    void InstallMonitorCompetitionTraffic();

    int m_mob;
    std::string m_assignmentMethod;
    std::string m_dqnActionCsvPath;
    std::string m_drlServerHost;
    uint16_t m_drlServerPort;
    uint32_t m_drlTimeoutMs;
    uint32_t m_maxSwitches;
    std::string m_kScheduleType;
    uint32_t m_kMin;
    uint32_t m_kDecayRate;
    double m_rewardSwitchPenaltyAlpha;
    double m_rewardDegradedPenaltyBeta;
    double m_onlineDqnSafetyThreshold;
    uint32_t m_centralizedDqnBootstrapCycles;
    uint32_t m_rngSeed;
    std::string m_outputDir;  // OUTPUT/<端末数>/<手法>/（実行ログ・補助ログの出力先）

    std::vector<NodeContainer> p2pNodes;
    std::vector<NodeContainer> wifiNodes;

    std::vector<NetDeviceContainer> p2pDevices;
    std::vector<NetDeviceContainer> wifiDevices;
    // NR devices (AP0専用)
    NetDeviceContainer m_nrGnbDevs;
    NetDeviceContainer m_nrUeDevs;
    Ptr<NrHelper> m_nrHelper;
    Ptr<NrPointToPointEpcHelper> m_nrEpcHelper;

    uint32_t termNum;
    uint32_t APnum;
    std::vector<Ptr<Node> > terms;
    std::vector<Ptr<Node> > wifiAPs;
    std::vector<Ptr<Node> > routers;
    Ptr<Node> server_udpVoice;
    Ptr<Node> server_udpVideo;
    Ptr<Node> server_rtt;
    Ptr<Node> server_onlineGame;
    Ptr<Node> server_browser;
    Ptr<Node> remote_host;
    Ptr<Node> cerNode;
    
    // 監視端末用
    std::vector<Ptr<Node> > monitorTerminals;
    std::vector<Ptr<APMonitorTerminal> > m_monitorApps;

    std::vector<TermData> m_termData;
    std::vector<TermAccessState> m_termAccessState;
    std::vector<TermAppState> m_termAppStates;
    std::vector<Ipv4Address> m_wifiApGatewayIps;  // 各Wi-Fi APのゲートウェイIP
    Ipv4Address m_nrGateway;                       // NR EPCゲートウェイIP
    ApSelectionInput m_apSelectionInput;
    std::vector<NodeContainer> m_routerCerNodes;
    std::vector<NetDeviceContainer> m_routerCerDevices;
    std::vector<NodeContainer> m_serverCerNodes;
    std::vector<NetDeviceContainer> m_serverCerDevices;
    NodeContainer m_pgwCerNodes;
    NetDeviceContainer m_pgwCerDevices;
    Ipv4Address m_remoteHostAddress;
    Time m_simulationDuration;
    Time m_browserRequestInterval;
    uint32_t m_browserRequestCount;
    bool m_enableOnlineGameTracing;
    std::unordered_set<uint32_t> m_wifiApAddresses;
    std::unordered_set<uint32_t> m_wifiStationAddresses;
    uint32_t m_cycleCount;
    uint32_t m_currentCycle;
    Time m_cycleDuration;
    Time m_warmupBeforeCycle;
    Time m_cycleStartOffset;
    Time m_monitorStartOffset;
    Time m_monitorStopOffset;
    Time m_terminalTpStopOffset;
    Time m_cycleEndGuard;
    Time m_browserFirstRequest;
    Time m_browserBatchBStart;
    uint32_t m_browserRequestBytes;
    Time m_browserPostHandoverDelay;
    std::vector<int> m_activeAssignment;

    // 端末別TP計測用（FlowMonitor方式）
    Ptr<FlowMonitor> m_termFlowMonitor;
    Ptr<Ipv4FlowClassifier> m_termFlowClassifier;
    std::vector<Ipv4Address> m_terminalIpAddresses;
    Time m_terminalTpWindowStart;
    Ptr<KamedaAppServer> m_kamedaServer;
    void BuildTerminalIpMap();
    void ResetTerminalFlowStats();
    void CollectTerminalThroughput();
    void ScheduleBrowserDownloadForTerminal(uint32_t termIdx,
                                            uint16_t port,
                                            uint32_t generation,
                                            uint32_t requestIndex,
                                            uint32_t totalRequests,
                                            Time interval,
                                            Time duration,
                                            uint32_t maxBytes);
    void ScheduleBrowserWarmupForTerminal(uint32_t termIdx, uint16_t port, uint32_t generation);
    void ScheduleFutureBrowserBursts(uint32_t termIdx, uint16_t port, uint32_t generation,
                                     Time minStartTime = Time(0));
    void LogBrowserBulkDiagnostic(uint32_t termIdx,
                                  uint16_t port,
                                  uint32_t generation,
                                  const std::string& phase,
                                  Ipv4Address dstIp,
                                  Ptr<Application> app = nullptr);
    void AttachBrowserBulkTrace(uint32_t termIdx,
                                uint16_t port,
                                uint32_t generation,
                                Ipv4Address dstIp,
                                Ptr<Application> app);
    void ProbeBrowserBulkSocketTrace(uint32_t termIdx,
                                     uint16_t port,
                                     uint32_t generation,
                                     Ipv4Address dstIp,
                                     Ptr<Application> app);
    void LogBrowserTcpEvent(const std::string& context,
                            const std::string& event,
                            uint32_t packetSize,
                            const std::string& detail);
    void BrowserBulkTxTrace(std::string context, Ptr<const Packet> packet);
    void BrowserBulkSocketTrace(std::string context, Ptr<Socket> socket);
    void BrowserBulkConnectReturnTrace(std::string context, Ptr<Socket> socket, int32_t result);
    void BrowserBulkConnectionTrace(std::string context, Ptr<Socket> socket);
    void BrowserBulkRetransmissionTrace(std::string context,
                                        Ptr<const Packet> packet,
                                        const TcpHeader& header,
                                        const Address& localAddr,
                                        const Address& peerAddr,
                                        Ptr<const TcpSocketBase> socket);
    void BrowserTcpSocketTxTrace(std::string context,
                                 Ptr<const Packet> packet,
                                 const TcpHeader& header,
                                 Ptr<const TcpSocketBase> socket);
    void BrowserTcpSocketStateTrace(std::string context,
                                    TcpSocket::TcpStates_t oldState,
                                    TcpSocket::TcpStates_t newState);

    void ScheduleMonitorWindows();
    void HandoverRequest(const std::vector<int>& assignment);
    void ConfigureCycleParameters();
    void PrintAssignmentSummary(const std::vector<int>& assignment) const;

    // 擬似ハンドオーバ関連
    void ApplyHandoverBatch(const std::vector<std::pair<uint32_t, int>>& switchList);
    void WifiToWifiHandover(uint32_t termIdx, int oldAp, int newAp);
    void NrToWifiHandover(uint32_t termIdx, int newAp);
    void WifiToNrHandover(uint32_t termIdx);
    RatType GetRatTypeForAp(int apNo) const;
    void SwitchDefaultRoute(Ptr<Node> termNode, uint32_t newIfIndex, Ipv4Address newGateway);
    void RebindTerminalApps(uint32_t termIdx, Ipv4Address newIp);
    void ReinstallBrowserApp(uint32_t termIdx, Ipv4Address newIp, Time startTime, Time stopTime);
    void ReinstallVideoApp(uint32_t termIdx, Ipv4Address newIp, Time startTime, Time stopTime);
    void ReinstallVoiceApp(uint32_t termIdx, Ipv4Address newIp, Time startTime, Time stopTime);
    void ReinstallOnlineGameApp(uint32_t termIdx, Ipv4Address newIp, Time startTime, Time stopTime);
    void InitializeTermAccessState();
    Ipv4Address GetActiveIpv4(uint32_t termIdx) const;
    void ApplyHandoverState(uint32_t termIdx, int newAp, RatType newRat, Ipv4Address newIp);
    void LogHandoverEvent(double timeSec, uint32_t termId, int oldAp, int newAp,
                          RatType oldRat, RatType newRat,
                          Ipv4Address oldIp, Ipv4Address newIp,
                          const std::string& result);

};  //class

}   //namespace
