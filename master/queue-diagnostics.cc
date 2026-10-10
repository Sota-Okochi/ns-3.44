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
