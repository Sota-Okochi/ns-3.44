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
NetSim::StartAp2CapacityExperiment()
{
    if (!m_ap2CapacityVariation && !m_ap2CapacityTrace)
    {
        return;
    }
    NS_ABORT_MSG_IF(p2pDevices.size() <= 2 || p2pDevices[2].GetN() != 2,
                    "AP2 capacity experiment requires Wi-Fi AP2");
    NS_ABORT_MSG_IF(!std::isfinite(m_ap2SampleSec) || m_ap2SampleSec <= 0 ||
                        Seconds(m_ap2SampleSec).IsZero(), "Invalid ap2SampleSec");
    auto device = DynamicCast<PointToPointNetDevice>(p2pDevices[2].Get(0));
    DataRateValue original;
    device->GetAttribute("DataRate", original);
    const uint64_t normal = original.Get().GetBitRate();
    const uint64_t low = DataRate(m_ap2LowRate).GetBitRate();
    if (m_ap2CapacityVariation)
    {
        NS_ABORT_MSG_IF(low == 0 || low >= normal, "ap2LowRate must be positive and below normal rate");
        NS_ABORT_MSG_IF(m_ap2DropCycle < 1 || m_ap2DropCycle >= m_ap2RecoveryCycle ||
                            m_ap2RecoveryCycle > m_cycleCount, "Invalid AP2 drop/recovery cycles");
    }
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string runId = "seed" + std::to_string(m_rngSeed) + "_" +
                              std::to_string(stamp) + "_pid" + std::to_string(getpid());
    const auto directory = std::filesystem::path(m_outputDir) / "ap2_capacity" / runId;
    m_ap2LogDirectory = directory.string();
    std::filesystem::create_directories(directory);
    std::filesystem::copy_file(m_queueSettingPath, directory / "setting.json");
    std::ofstream metadata(directory / "metadata.txt");
    metadata << "run_id=" << runId << "\nseed=" << m_rngSeed
             << "\nrng_run=" << RngSeedManager::GetRun() << "\nmethod=" << m_assignmentMethod
             << "\nns3_version=3.44\nlink=wifi_ap2_router\nbs_id_1based=3\ndirection=both"
             << "\nenabled=" << m_ap2CapacityVariation << "\nnormal_bps=" << normal
             << "\nlow_bps=" << low << "\ndrop_cycle=" << m_ap2DropCycle
             << "\nrecovery_cycle=" << m_ap2RecoveryCycle << "\nsample_sec=" << m_ap2SampleSec
             << "\ncycle_start_offset_sec=" << m_cycleStartOffset.GetSeconds()
             << "\ncycle_duration_sec=" << m_cycleDuration.GetSeconds() << '\n' << m_queueArgs;
    std::ofstream terminals(directory / "initial_terminals.csv");
    terminals << "ue_id,app_type,initial_bs_id_1based\n";
    for (uint32_t i = 0; i < m_termData.size(); ++i)
    {
        terminals << i + 1 << ',' << m_termData[i].use_appli << ',' << m_termData[i].apNo << '\n';
    }
    NS_ABORT_MSG_IF(!metadata || !terminals, "Cannot save AP2 experiment metadata");
    m_ap2Events.open(directory / "events.csv");
    m_ap2Samples.open(directory / "queue_samples.csv");
    NS_ABORT_MSG_IF(!m_ap2Events || !m_ap2Samples, "Cannot open AP2 experiment logs");
    m_ap2Events << "sim_time,cycle_id,direction,old_bps,new_bps\n";
    m_ap2Samples << "sim_time,cycle_id,direction,link_bps,queue_packets,queue_bytes,received_bytes_total,dropped_packets_total,qdisc_bytes,qdisc_dropped_packets_total\n";
    // Cycle 0 records initialization; no queues or applications are reset at changes.
    metadata.flush();
    terminals.flush();
    SetAp2Capacity(0, normal);
    if (m_ap2CapacityVariation)
    {
        Simulator::Schedule(m_cycleStartOffset + m_cycleDuration * (m_ap2DropCycle - 1),
                            &NetSim::SetAp2Capacity, this, m_ap2DropCycle, low);
        Simulator::Schedule(m_cycleStartOffset + m_cycleDuration * (m_ap2RecoveryCycle - 1),
                            &NetSim::SetAp2Capacity, this, m_ap2RecoveryCycle, normal);
    }
    SampleAp2Capacity();
    std::cout << "[AP2 capacity] logs=" << directory.string() << std::endl;
}

void
NetSim::SetAp2Capacity(uint32_t cycle, uint64_t rate)
{
    for (uint32_t end = 0; end < 2; ++end)
    {
        auto device = DynamicCast<PointToPointNetDevice>(p2pDevices[2].Get(end));
        DataRateValue old;
        device->GetAttribute("DataRate", old);
        device->SetDataRate(DataRate(rate));
        // p2pNodes[2] is ordered AP2, router. Each device controls its own transmitter.
        m_ap2Events << Simulator::Now().GetSeconds() << ',' << cycle << ','
                    << (end == 0 ? "ap_to_router" : "router_to_ap") << ','
                    << old.Get().GetBitRate() << ',' << rate << '\n';
    }
    m_ap2Events.flush();
}

void
NetSim::SampleAp2Capacity()
{
    const Time elapsed = Simulator::Now() - m_cycleStartOffset;
    const uint32_t cycle = elapsed.IsNegative() || elapsed >= m_cycleDuration * m_cycleCount
                              ? 0 : static_cast<uint32_t>(elapsed.GetTimeStep() /
                                                         m_cycleDuration.GetTimeStep()) + 1;
    for (uint32_t end = 0; end < 2; ++end)
    {
        auto device = DynamicCast<PointToPointNetDevice>(p2pDevices[2].Get(end));
        DataRateValue rate;
        device->GetAttribute("DataRate", rate);
        auto queue = device->GetQueue();
        m_ap2Samples << Simulator::Now().GetSeconds() << ',' << cycle << ','
                     << (end == 0 ? "ap_to_router" : "router_to_ap") << ','
                     << rate.Get().GetBitRate() << ',' << queue->GetNPackets() << ','
                     << queue->GetNBytes() << ',' << queue->GetTotalReceivedBytes() << ','
                     << queue->GetTotalDroppedPackets() << ',';
        auto tc = device->GetNode()->GetObject<TrafficControlLayer>();
        auto disc = tc ? tc->GetRootQueueDiscOnDevice(device) : nullptr;
        if (disc)
        {
            m_ap2Samples << disc->GetNBytes() << ',' << disc->GetStats().nTotalDroppedPackets;
        }
        else
        {
            m_ap2Samples << ',';
        }
        m_ap2Samples << '\n';
    }
    m_ap2Samples.flush();
    NS_ABORT_MSG_IF(!m_ap2Samples, "Cannot write AP2 queue samples");
    m_ap2SampleEvent = Simulator::Schedule(Seconds(m_ap2SampleSec), &NetSim::SampleAp2Capacity, this);
}

void
NetSim::FinishAp2CapacityExperiment()
{
    m_ap2SampleEvent.Cancel();
    if (!m_ap2Samples.is_open())
    {
        return;
    }
    SampleAp2Capacity();
    m_ap2SampleEvent.Cancel();
    m_ap2Samples.close();
    m_ap2Events.close();
    NS_ABORT_MSG_IF(m_ap2Samples.fail() || m_ap2Events.fail(), "Cannot close AP2 logs");
    std::ofstream status(std::filesystem::path(m_ap2LogDirectory) / "simulation_completed.txt");
    status << "sim_time=" << Simulator::Now().GetSeconds() << '\n';
    status.close();
    NS_ABORT_MSG_IF(status.fail(), "Cannot save AP2 completion status");
}

} // namespace ns3
