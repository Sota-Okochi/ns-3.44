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
NetSim::StartAp1CapacityExperiment()
{
    if (!m_ap1CapacityVariation && !m_ap1CapacityTrace)
    {
        return;
    }
    NS_ABORT_MSG_IF(p2pDevices.size() <= 1 || p2pDevices[1].GetN() != 2,
                    "AP1 capacity experiment requires Wi-Fi AP1");
    NS_ABORT_MSG_IF(!std::isfinite(m_ap1SampleSec) || m_ap1SampleSec <= 0 ||
                        Seconds(m_ap1SampleSec).IsZero(), "Invalid ap1SampleSec");
    auto device = DynamicCast<PointToPointNetDevice>(p2pDevices[1].Get(0));
    DataRateValue original;
    device->GetAttribute("DataRate", original);
    const uint64_t normal = original.Get().GetBitRate();
    const uint64_t low = DataRate(m_ap1LowRate).GetBitRate();
    if (m_ap1CapacityVariation)
    {
        NS_ABORT_MSG_IF(low == 0 || low >= normal, "ap1LowRate must be positive and below normal rate");
        NS_ABORT_MSG_IF(m_ap1DropCycle < 1 || m_ap1DropCycle >= m_ap1RecoveryCycle ||
                            m_ap1RecoveryCycle > m_cycleCount, "Invalid AP1 drop/recovery cycles");
    }
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string runId = "seed" + std::to_string(m_rngSeed) + "_" +
                              std::to_string(stamp) + "_pid" + std::to_string(getpid());
    const auto directory = std::filesystem::path(m_outputDir) / "ap1_capacity" / runId;
    std::filesystem::create_directories(directory);
    std::filesystem::copy_file(m_queueSettingPath, directory / "setting.json");
    std::ofstream metadata(directory / "metadata.txt");
    metadata << "run_id=" << runId << "\nseed=" << m_rngSeed
             << "\nrng_run=" << RngSeedManager::GetRun() << "\nmethod=" << m_assignmentMethod
             << "\nns3_version=3.44\nlink=wifi_ap1_router\nbs_id_1based=2\ndirection=both"
             << "\nenabled=" << m_ap1CapacityVariation << "\nnormal_bps=" << normal
             << "\nlow_bps=" << low << "\ndrop_cycle=" << m_ap1DropCycle
             << "\nrecovery_cycle=" << m_ap1RecoveryCycle << "\nsample_sec=" << m_ap1SampleSec
             << "\ncycle_start_offset_sec=" << m_cycleStartOffset.GetSeconds()
             << "\ncycle_duration_sec=" << m_cycleDuration.GetSeconds() << '\n' << m_queueArgs;
    std::ofstream terminals(directory / "initial_terminals.csv");
    terminals << "ue_id,app_type,initial_bs_id_1based\n";
    for (uint32_t i = 0; i < m_termData.size(); ++i)
    {
        terminals << i + 1 << ',' << m_termData[i].use_appli << ',' << m_termData[i].apNo << '\n';
    }
    NS_ABORT_MSG_IF(!metadata || !terminals, "Cannot save AP1 experiment metadata");
    m_ap1Events.open(directory / "events.csv");
    m_ap1Samples.open(directory / "queue_samples.csv");
    NS_ABORT_MSG_IF(!m_ap1Events || !m_ap1Samples, "Cannot open AP1 experiment logs");
    m_ap1Events << "sim_time,cycle_id,direction,old_bps,new_bps\n";
    m_ap1Samples << "sim_time,cycle_id,direction,link_bps,queue_packets,queue_bytes,received_bytes_total,dropped_packets_total,qdisc_bytes,qdisc_dropped_packets_total\n";
    // Cycle 0 records initialization; no queues or applications are reset at changes.
    SetAp1Capacity(0, normal);
    if (m_ap1CapacityVariation)
    {
        Simulator::Schedule(m_cycleStartOffset + m_cycleDuration * (m_ap1DropCycle - 1),
                            &NetSim::SetAp1Capacity, this, m_ap1DropCycle, low);
        Simulator::Schedule(m_cycleStartOffset + m_cycleDuration * (m_ap1RecoveryCycle - 1),
                            &NetSim::SetAp1Capacity, this, m_ap1RecoveryCycle, normal);
    }
    SampleAp1Capacity();
    std::cout << "[AP1 capacity] logs=" << directory.string() << std::endl;
}

void
NetSim::SetAp1Capacity(uint32_t cycle, uint64_t rate)
{
    for (uint32_t end = 0; end < 2; ++end)
    {
        auto device = DynamicCast<PointToPointNetDevice>(p2pDevices[1].Get(end));
        DataRateValue old;
        device->GetAttribute("DataRate", old);
        device->SetDataRate(DataRate(rate));
        // p2pNodes[1] is ordered AP, router. Each device controls its own transmitter.
        m_ap1Events << Simulator::Now().GetSeconds() << ',' << cycle << ','
                    << (end == 0 ? "ap_to_router" : "router_to_ap") << ','
                    << old.Get().GetBitRate() << ',' << rate << '\n';
    }
    m_ap1Events.flush();
}

void
NetSim::SampleAp1Capacity()
{
    const Time elapsed = Simulator::Now() - m_cycleStartOffset;
    const uint32_t cycle = elapsed.IsNegative() || elapsed >= m_cycleDuration * m_cycleCount
                              ? 0 : static_cast<uint32_t>(elapsed.GetTimeStep() /
                                                         m_cycleDuration.GetTimeStep()) + 1;
    for (uint32_t end = 0; end < 2; ++end)
    {
        auto device = DynamicCast<PointToPointNetDevice>(p2pDevices[1].Get(end));
        DataRateValue rate;
        device->GetAttribute("DataRate", rate);
        auto queue = device->GetQueue();
        m_ap1Samples << Simulator::Now().GetSeconds() << ',' << cycle << ','
                     << (end == 0 ? "ap_to_router" : "router_to_ap") << ','
                     << rate.Get().GetBitRate() << ',' << queue->GetNPackets() << ','
                     << queue->GetNBytes() << ',' << queue->GetTotalReceivedBytes() << ','
                     << queue->GetTotalDroppedPackets() << ',';
        auto tc = device->GetNode()->GetObject<TrafficControlLayer>();
        auto disc = tc ? tc->GetRootQueueDiscOnDevice(device) : nullptr;
        if (disc)
        {
            m_ap1Samples << disc->GetNBytes() << ',' << disc->GetStats().nTotalDroppedPackets;
        }
        else
        {
            m_ap1Samples << ',';
        }
        m_ap1Samples << '\n';
    }
    m_ap1SampleEvent = Simulator::Schedule(Seconds(m_ap1SampleSec), &NetSim::SampleAp1Capacity, this);
}

} // namespace ns3
