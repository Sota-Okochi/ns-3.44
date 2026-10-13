#include "NetSim.h"
#include "ns3/traffic-control-layer.h"
#include "ns3/queue-disc.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <unistd.h>

namespace ns3
{

void
NetSim::StartQueueDiagnostics()
{
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_queueRunId = m_assignmentMethod + "_seed" + std::to_string(m_rngSeed) + "_" +
                   std::to_string(stamp) + "_pid" + std::to_string(getpid());
    const auto directory = std::filesystem::path(m_outputDir) / "queue_diagnostics" / m_queueRunId;
    std::filesystem::create_directories(directory);
    std::filesystem::copy_file(m_queueSettingPath, directory / "setting.json");
    std::ofstream metadata(directory / "metadata.txt");
    metadata << "schema=2\nrun_id=" << m_queueRunId << "\nseed=" << m_rngSeed
             << "\nrng_run=" << RngSeedManager::GetRun() << "\nmethod=" << m_assignmentMethod
             << "\nsample_sec=" << m_queueSampleSec
             << "\ncycle_start_offset_sec=" << m_cycleStartOffset.GetSeconds()
             << "\ncycle_duration_sec=" << m_cycleDuration.GetSeconds()
             << "\nns3_version=3.44\n" << m_queueArgs;
    NS_ABORT_MSG_IF(!metadata, "Cannot write queue diagnostic metadata");
    std::ofstream terminals(directory / "initial_terminals.csv");
    terminals << "ue_id,app_type,initial_bs_id\n";
    for (uint32_t i = 0; i < m_termData.size(); ++i)
    {
        terminals << i + 1 << ',' << m_termData[i].use_appli << ',' << m_termData[i].apNo - 1 << '\n';
    }
    NS_ABORT_MSG_IF(!terminals, "Cannot save terminal composition");
    m_linkLoadCsv.open(directory / "link_load.csv");
    NS_ABORT_MSG_IF(!m_linkLoadCsv, "Cannot open link load CSV");
    m_linkLoadCsv << "run_id,seed,sim_time,cycle_window_id,direction,queue_disc_type,link_bps,"
                    "received_packets_total,received_bytes_total,sent_packets_total,sent_bytes_total,"
                    "dropped_packets_total,dropped_bytes_total,queue_bytes\n";
    m_queueCsv.open(directory / "queues.csv");
    NS_ABORT_MSG_IF(!m_queueCsv, "Cannot open queue diagnostic CSV");
    m_queueCsv << "run_id,seed,method,sim_time,cycle_window_id,layer,direction,source,ue_id,imsi,rnti,"
                  "current_bs_id,queue_entries,queue_bytes,hol_delay_ms,dropped_packets_total,"
                  "dropped_bytes_total,capacity,link_bps,channel_delay_ms\n";
    std::cout << "[QueueDiagnostics] output=" << directory << std::endl;
    SampleQueueDiagnostics();
}

void
NetSim::SampleQueueDiagnostics()
{
    if (m_queueLastSample == Simulator::Now())
    {
        return;
    }
    m_queueLastSample = Simulator::Now();
    const double now = Simulator::Now().GetSeconds();
    // This is a time-window index, not the APselection decision callback number.
    const int cycle = (now < m_cycleStartOffset.GetSeconds() ||
                       now >= (m_cycleStartOffset + m_cycleDuration * m_cycleCount).GetSeconds()) ? 0 :
        1 + static_cast<int>(std::floor((now - m_cycleStartOffset.GetSeconds()) /
                                       m_cycleDuration.GetSeconds()));
    auto prefix = [&](const std::string& layer, const std::string& direction,
                      const std::string& source, int ueId, uint64_t imsi, uint16_t rnti, int bs) {
        m_queueCsv << m_queueRunId << ',' << m_rngSeed << ',' << m_assignmentMethod << ','
                   << std::fixed << std::setprecision(6) << now << ',' << cycle << ','
                   << layer << ',' << direction << ',' << source << ',' << ueId << ','
                   << imsi << ',' << rnti << ',' << bs << ',';
    };
    // Install order is PGW, CER. DL traffic towards the radio leaves CER.
    for (uint32_t i = 0; i < m_pgwCerDevices.GetN(); ++i)
    {
        auto device = DynamicCast<PointToPointNetDevice>(m_pgwCerDevices.Get(i));
        auto queue = device->GetQueue();
        const std::string direction = i == 0 ? "ul" : "dl";
        const std::string source = "/NodeList/" + std::to_string(device->GetNode()->GetId()) +
                                  "/DeviceList/" + std::to_string(device->GetIfIndex());
        DataRateValue rate;
        TimeValue delay;
        device->GetAttribute("DataRate", rate);
        device->GetChannel()->GetAttribute("Delay", delay);
        auto tail = [&](const QueueSize& capacity) {
            m_queueCsv << capacity << ',' << rate.Get().GetBitRate() << ','
                       << delay.Get().GetSeconds() * 1000 << '\n';
        };
        prefix("p2p_device", direction, source, -1, 0, 0, -1);
        m_queueCsv << queue->GetNPackets() << ',' << queue->GetNBytes() << ",,"
                   << queue->GetTotalDroppedPackets() << ',' << queue->GetTotalDroppedBytes() << ',';
        tail(queue->GetMaxSize());
        auto tc = device->GetNode()->GetObject<TrafficControlLayer>();
        auto disc = tc ? tc->GetRootQueueDiscOnDevice(device) : nullptr;
        if (disc)
        {
            const auto& stats = disc->GetStats();
            // Count arrivals before AQM/admission drops, not just successful enqueues.
            // QueueDisc bytes are IP-layer bytes, excluding the P2P framing header.
            m_linkLoadCsv << m_queueRunId << ',' << m_rngSeed << ','
                          << std::fixed << std::setprecision(6) << now << ',' << cycle << ','
                          << direction << ',' << disc->GetInstanceTypeId().GetName() << ','
                          << rate.Get().GetBitRate() << ',' << stats.nTotalReceivedPackets << ','
                          << stats.nTotalReceivedBytes << ',' << stats.nTotalSentPackets << ','
                          << stats.nTotalSentBytes << ',' << stats.nTotalDroppedPackets << ','
                          << stats.nTotalDroppedBytes << ',' << disc->GetNBytes() << '\n';
            prefix("p2p_qdisc", direction, source, -1, 0, 0, -1);
            m_queueCsv << disc->GetNPackets() << ',' << disc->GetNBytes() << ",,"
                       << stats.nTotalDroppedPackets << ',' << stats.nTotalDroppedBytes << ',';
            tail(disc->GetMaxSize());
        }
    }

    // Resolve every sample: data bearers are created asynchronously after attachment.
    // Read counters on the RLC itself so drops before the first sample are not lost.
    for (bool downlink : {true, false})
    {
        const std::string root = downlink ?
            "/NodeList/*/DeviceList/*/NrGnbRrc/UeMap/*/DataRadioBearerMap/*/NrRlc" :
            "/NodeList/*/DeviceList/*/NrUeRrc/DataRadioBearerMap/*/NrRlc";
        const auto matches = Config::LookupMatches(root);
        for (uint32_t i = 0; i < matches.GetN(); ++i)
        {
            const auto path = matches.GetMatchedPath(i);
            const auto rlc = DynamicCast<NrRlcUm>(matches.Get(i));
            // Do not silently report zero for an unsupported RLC mode.
            NS_ABORT_MSG_IF(!rlc, "Queue diagnostics require NrRlcUm: " << path);
            uint16_t rnti = 0;
            uint64_t imsi = 0;
            int ueId = -1;
            int bs = -1;
            std::smatch match;
            uint32_t nodeId = 0;
            if (downlink && std::regex_search(path, match, std::regex("/UeMap/([0-9]+)")))
            {
                rnti = static_cast<uint16_t>(std::stoul(match[1]));
            }
            if (!downlink && std::regex_search(path, match, std::regex("/NodeList/([0-9]+)")))
            {
                nodeId = std::stoul(match[1]);
            }
            for (uint32_t j = 0; j < m_nrUeDevs.GetN(); ++j)
            {
                auto ue = DynamicCast<NrUeNetDevice>(m_nrUeDevs.Get(j));
                if ((downlink && ue->GetRrc()->GetRnti() == rnti) ||
                    (!downlink && ue->GetNode()->GetId() == nodeId))
                {
                    imsi = ue->GetImsi();
                    rnti = ue->GetRrc()->GetRnti();
                    for (uint32_t k = 0; k < terms.size(); ++k)
                    {
                        if (terms[k] == ue->GetNode())
                        {
                            ueId = k + 1; // Match master_log's 1-based UE IDs.
                            bs = m_termAccessState[k].currentAp - 1;
                            break;
                        }
                    }
                    break;
                }
            }
            UintegerValue capacity;
            rlc->GetAttribute("MaxTxBufferSize", capacity);
            prefix("nr_rlc_um", downlink ? "dl" : "ul", path, ueId, imsi, rnti, bs);
            m_queueCsv << rlc->GetTxBufferEntries() << ',' << rlc->GetTxBufferBytes() << ','
                       << rlc->GetTxBufferHolDelay().GetSeconds() * 1000 << ','
                       << rlc->GetTxOverflowPackets() << ',' << rlc->GetTxOverflowBytes() << ','
                       << capacity.Get() << "B,,\n";
        }
    }
    m_queueCsv.flush(); // Keep completed samples readable even in interrupted runs.
    m_linkLoadCsv.flush();
    NS_ABORT_MSG_IF(!m_linkLoadCsv, "Link load diagnostic write failed");
    NS_ABORT_MSG_IF(!m_queueCsv, "Queue diagnostic write failed");
    m_queueSampleEvent = Simulator::Schedule(Seconds(m_queueSampleSec),
                                            &NetSim::SampleQueueDiagnostics, this);
}

} // namespace ns3

// Passive trace callbacks: no scheduled events, packets, RNG draws, or parameter changes.
namespace ns3
{
namespace
{
struct MarlTraceContext
{
    std::shared_ptr<std::ofstream> out;
    std::string event;
    uint32_t node;
    uint32_t device;
    int framing; // 0=IPv4, 1=LLC, 2=Wi-Fi MAC+LLC, 3=PPP
    bool operator==(const MarlTraceContext&) const = default;
};

void TraceMarlPacket(MarlTraceContext c, Ptr<const Packet> original)
{
    auto packet = original->Copy();
    bool retry = false;
    if (c.framing == 2)
    {
        WifiMacHeader mac;
        if (packet->GetSize() < 10 || !packet->RemoveHeader(mac) || !mac.IsData() ||
            (mac.IsQosData() && mac.IsQosAmsdu())) return;
        retry = mac.IsRetry();
    }
    if (c.framing == 1 || c.framing == 2)
    {
        LlcSnapHeader llc;
        if (packet->GetSize() < llc.GetSerializedSize()) return;
        packet->RemoveHeader(llc);
        if (llc.GetType() != 0x0800) return;
    }
    if (c.framing == 3)
    {
        PppHeader ppp;
        if (packet->GetSize() < ppp.GetSerializedSize()) return;
        packet->RemoveHeader(ppp);
        if (ppp.GetProtocol() != 0x0021) return;
    }
    Ipv4Header ip;
    if (packet->GetSize() < 20) return;
    packet->RemoveHeader(ip);
    if (ip.GetProtocol() != 1 || ip.GetFragmentOffset() != 0 || packet->GetSize() < 8) return;
    Icmpv4Header icmp; packet->RemoveHeader(icmp);
    if (icmp.GetType() != Icmpv4Header::ICMPV4_ECHO &&
        icmp.GetType() != Icmpv4Header::ICMPV4_ECHO_REPLY) return;
    Icmpv4Echo echo; packet->RemoveHeader(echo);
    if (echo.GetIdentifier() < 0xa000 || echo.GetIdentifier() > 0xa002) return;
    *c.out << std::setprecision(17) << Simulator::Now().GetSeconds() << ',' << c.event
           << ',' << c.node << ',' << c.device << ',' << echo.GetIdentifier()-0xa000
           << ',' << echo.GetSequenceNumber() << ',' << unsigned(icmp.GetType())
           << ',' << ip.GetSource() << ',' << ip.GetDestination() << ',' << original->GetUid()
           << ',' << retry << '\n';
}
void TraceMarlIp(MarlTraceContext c, Ptr<const Packet> packet, Ptr<Ipv4>, uint32_t interface)
{
    c.device = interface; TraceMarlPacket(c, packet);
}
void TraceMarlWifiTx(MarlTraceContext c, Ptr<const Packet> packet, double)
{
    TraceMarlPacket(c, packet);
}
void TraceMarlWifiQueue(MarlTraceContext c, Ptr<const WifiMpdu> mpdu)
{
    if (mpdu->GetHeader().IsData() &&
        !(mpdu->GetHeader().IsQosData() && mpdu->GetHeader().IsQosAmsdu()))
        TraceMarlPacket(c, mpdu->GetPacket());
}
} // namespace

void NetSim::StartMarlRttTrace()
{
    auto out = std::make_shared<std::ofstream>(std::string(m_outputDir)+"marl_rtt_path.csv");
    NS_ABORT_MSG_IF(!*out, "Cannot open MARL passive trace");
    *out << "time,event,node,device_or_ip_interface,ap,sequence,icmp_type,src,dst,uid,retry\n";
    std::ofstream nodes(std::string(m_outputDir)+"marl_trace_nodes.csv");
    nodes << "node,role,device_or_interface,address\n";
    for (auto it=NodeList::Begin(); it!=NodeList::End(); ++it)
    {
        auto node=*it;
        std::string role="router_or_other";
        if (node==server_rtt) role="server_rtt";
        for (uint32_t a=0; a<monitorTerminals.size(); ++a)
            if (node==monitorTerminals[a]) role="monitor_ap"+std::to_string(a);
        for (uint32_t a=0; a<wifiAPs.size(); ++a)
            if (node==wifiAPs[a]) role="ap"+std::to_string(a);
        for (uint32_t u=0; u<terms.size(); ++u)
            if (node==terms[u]) role="ue"+std::to_string(u);
        if (auto ip=node->GetObject<Ipv4>())
        {
            for (uint32_t i=0; i<ip->GetNInterfaces(); ++i)
                for (uint32_t j=0; j<ip->GetNAddresses(i); ++j)
                    nodes << node->GetId() << ',' << role << ',' << i << ',' << ip->GetAddress(i,j).GetLocal() << '\n';
            for (const std::string event : {"Tx", "Rx"})
                NS_ABORT_MSG_IF(!ip->TraceConnectWithoutContext(event, MakeBoundCallback(&TraceMarlIp,
                    MarlTraceContext{out,"ip_"+event,node->GetId(),0,0})), "Cannot attach IP trace");
        }
        for (uint32_t d=0; d<node->GetNDevices(); ++d)
        {
            auto device=node->GetDevice(d);
            if (auto wifi=DynamicCast<WifiNetDevice>(device))
            {
                for (const std::string event : {"MacTx", "MacRx", "MacTxDrop", "MacRxDrop"})
                    NS_ABORT_MSG_IF(!wifi->GetMac()->TraceConnectWithoutContext(event,
                        MakeBoundCallback(&TraceMarlPacket,MarlTraceContext{out,event,node->GetId(),d,1})), "Cannot attach MAC trace");
                NS_ABORT_MSG_IF(!wifi->GetPhy()->TraceConnectWithoutContext("PhyTxBegin",
                    MakeBoundCallback(&TraceMarlWifiTx,MarlTraceContext{out,"wifi_phy_tx",node->GetId(),d,2})), "Cannot attach PHY trace");
                for (auto ac : {AC_BE, AC_BK, AC_VI, AC_VO})
                {
                    auto queue=wifi->GetMac()->GetTxopQueue(ac);
                    if (!queue) continue;
                    for (const std::string event : {"Enqueue", "Dequeue", "Drop", "Expired"})
                        NS_ABORT_MSG_IF(!queue->TraceConnectWithoutContext(event,
                            MakeBoundCallback(&TraceMarlWifiQueue,MarlTraceContext{out,
                            "wifi_queue_"+std::to_string(int(ac))+"_"+event,node->GetId(),d,1})), "Cannot attach Wi-Fi queue trace");
                }
            }
            if (auto p2p=DynamicCast<PointToPointNetDevice>(device))
            {
                for (const std::string event : {"MacTx", "PhyTxBegin", "PhyRxEnd", "MacTxDrop"})
                    NS_ABORT_MSG_IF(!p2p->TraceConnectWithoutContext(event,
                        MakeBoundCallback(&TraceMarlPacket,MarlTraceContext{out,"p2p_"+event,node->GetId(),d,3})), "Cannot attach P2P trace");
            }
        }
    }
}
} // namespace ns3
