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
NetSim::StartAp0CapacityExperiment()
{
    if (!m_ap0CapacityVariation && !m_ap0CapacityTrace)
    {
        return;
    }
    NS_ABORT_MSG_IF(m_pgwCerDevices.GetN() != 2,
                    "AP0 capacity experiment requires PGW-CER link");
    NS_ABORT_MSG_IF(!std::isfinite(m_ap0SampleSec) || m_ap0SampleSec <= 0 ||
                        Seconds(m_ap0SampleSec).IsZero(), "Invalid ap0SampleSec");
    auto device = DynamicCast<PointToPointNetDevice>(m_pgwCerDevices.Get(0));
    DataRateValue original;
    device->GetAttribute("DataRate", original);
    const uint64_t normal = original.Get().GetBitRate();
    const uint64_t low = DataRate(m_ap0LowRate).GetBitRate();
    if (m_ap0CapacityVariation)
    {
        NS_ABORT_MSG_IF(low == 0 || low >= normal, "ap0LowRate must be positive and below normal rate");
        NS_ABORT_MSG_IF(m_ap0DropCycle < 1 || m_ap0DropCycle >= m_ap0RecoveryCycle ||
                            m_ap0RecoveryCycle > m_cycleCount, "Invalid AP0 drop/recovery cycles");
    }
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string runId = "seed" + std::to_string(m_rngSeed) + "_" +
                              std::to_string(stamp) + "_pid" + std::to_string(getpid());
    const auto directory = std::filesystem::path(m_outputDir) / "ap0_capacity" / runId;
    m_ap0LogDirectory = directory.string();
    std::filesystem::create_directories(directory);
    std::filesystem::copy_file(m_queueSettingPath, directory / "setting.json");
    std::ofstream metadata(directory / "metadata.txt");
    metadata << "run_id=" << runId << "\nseed=" << m_rngSeed
             << "\nrng_run=" << RngSeedManager::GetRun() << "\nmethod=" << m_assignmentMethod
             << "\nns3_version=3.44\nlink=pgw_cer\nbs_id_1based=1\ndirection=both"
             << "\nenabled=" << m_ap0CapacityVariation << "\nnormal_bps=" << normal
             << "\nlow_bps=" << low << "\ndrop_cycle=" << m_ap0DropCycle
             << "\nrecovery_cycle=" << m_ap0RecoveryCycle << "\nsample_sec=" << m_ap0SampleSec
             << "\ncycle_start_offset_sec=" << m_cycleStartOffset.GetSeconds()
             << "\ncycle_duration_sec=" << m_cycleDuration.GetSeconds() << '\n' << m_queueArgs;
    std::ofstream terminals(directory / "initial_terminals.csv");
    terminals << "ue_id,app_type,initial_bs_id_1based\n";
    for (uint32_t i = 0; i < m_termData.size(); ++i)
    {
        terminals << i + 1 << ',' << m_termData[i].use_appli << ',' << m_termData[i].apNo << '\n';
    }
    NS_ABORT_MSG_IF(!metadata || !terminals, "Cannot save AP0 experiment metadata");
    m_ap0Events.open(directory / "events.csv");
    m_ap0Samples.open(directory / "queue_samples.csv");
    NS_ABORT_MSG_IF(!m_ap0Events || !m_ap0Samples, "Cannot open AP0 experiment logs");
    m_ap0Events << "sim_time,cycle_id,direction,old_bps,new_bps\n";
    m_ap0Samples << "sim_time,cycle_id,direction,link_bps,queue_packets,queue_bytes,received_bytes_total,dropped_packets_total,qdisc_bytes,qdisc_dropped_packets_total\n";
    // Cycle 0 records initialization; no queues or applications are reset at changes.
    metadata.flush();
    terminals.flush();
    SetAp0Capacity(0, normal);
    if (m_ap0CapacityVariation)
    {
        Simulator::Schedule(m_cycleStartOffset + m_cycleDuration * (m_ap0DropCycle - 1),
                            &NetSim::SetAp0Capacity, this, m_ap0DropCycle, low);
        Simulator::Schedule(m_cycleStartOffset + m_cycleDuration * (m_ap0RecoveryCycle - 1),
                            &NetSim::SetAp0Capacity, this, m_ap0RecoveryCycle, normal);
    }
    SampleAp0Capacity();
    std::cout << "[AP0 capacity] logs=" << directory.string() << std::endl;
}

void
NetSim::SetAp0Capacity(uint32_t cycle, uint64_t rate)
{
    for (uint32_t end = 0; end < 2; ++end)
    {
        auto device = DynamicCast<PointToPointNetDevice>(m_pgwCerDevices.Get(end));
        DataRateValue old;
        device->GetAttribute("DataRate", old);
        device->SetDataRate(DataRate(rate));
        // m_pgwCerNodes is ordered PGW, CER. Each device controls its own transmitter.
        m_ap0Events << Simulator::Now().GetSeconds() << ',' << cycle << ','
                    << (end == 0 ? "pgw_to_cer" : "cer_to_pgw") << ','
                    << old.Get().GetBitRate() << ',' << rate << '\n';
    }
    m_ap0Events.flush();
}

void
NetSim::SampleAp0Capacity()
{
    const Time elapsed = Simulator::Now() - m_cycleStartOffset;
    const uint32_t cycle = elapsed.IsNegative() || elapsed >= m_cycleDuration * m_cycleCount
                              ? 0 : static_cast<uint32_t>(elapsed.GetTimeStep() /
                                                         m_cycleDuration.GetTimeStep()) + 1;
    for (uint32_t end = 0; end < 2; ++end)
    {
        auto device = DynamicCast<PointToPointNetDevice>(m_pgwCerDevices.Get(end));
        DataRateValue rate;
        device->GetAttribute("DataRate", rate);
        auto queue = device->GetQueue();
        m_ap0Samples << Simulator::Now().GetSeconds() << ',' << cycle << ','
                     << (end == 0 ? "pgw_to_cer" : "cer_to_pgw") << ','
                     << rate.Get().GetBitRate() << ',' << queue->GetNPackets() << ','
                     << queue->GetNBytes() << ',' << queue->GetTotalReceivedBytes() << ','
                     << queue->GetTotalDroppedPackets() << ',';
        auto tc = device->GetNode()->GetObject<TrafficControlLayer>();
        auto disc = tc ? tc->GetRootQueueDiscOnDevice(device) : nullptr;
        if (disc)
        {
            m_ap0Samples << disc->GetNBytes() << ',' << disc->GetStats().nTotalDroppedPackets;
        }
        else
        {
            m_ap0Samples << ',';
        }
        m_ap0Samples << '\n';
    }
    m_ap0Samples.flush();
    NS_ABORT_MSG_IF(!m_ap0Samples, "Cannot write AP0 queue samples");
    m_ap0SampleEvent = Simulator::Schedule(Seconds(m_ap0SampleSec), &NetSim::SampleAp0Capacity, this);
}

void
NetSim::FinishAp0CapacityExperiment()
{
    m_ap0SampleEvent.Cancel();
    if (!m_ap0Samples.is_open())
    {
        return;
    }
    SampleAp0Capacity();
    m_ap0SampleEvent.Cancel();
    m_ap0Samples.close();
    m_ap0Events.close();
    NS_ABORT_MSG_IF(m_ap0Samples.fail() || m_ap0Events.fail(), "Cannot close AP0 logs");
    std::ofstream status(std::filesystem::path(m_ap0LogDirectory) / "simulation_completed.txt");
    status << "sim_time=" << Simulator::Now().GetSeconds() << '\n';
    status.close();
    NS_ABORT_MSG_IF(status.fail(), "Cannot save AP0 completion status");
}

} // namespace ns3
