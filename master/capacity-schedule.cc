#include "NetSim.h"

#include <chrono>
#include <filesystem>
#include <set>
#include <tuple>
#include <unistd.h>

namespace ns3
{

void
NetSim::StartCapacitySchedule()
{
    if (m_capacityEventsPath.empty())
    {
        return; // Existing commands retain their original behavior.
    }
    NS_ABORT_MSG_IF(m_ap0CapacityVariation || m_ap1CapacityVariation || m_ap2CapacityVariation ||
                        m_ap0CapacityTrace || m_ap1CapacityTrace || m_ap2CapacityTrace,
                    "capacityEventsPath cannot be combined with legacy capacity variation/trace");
    std::ifstream input(m_capacityEventsPath);
    NS_ABORT_MSG_IF(!input, "Cannot open capacity schedule: " << m_capacityEventsPath);
    std::string line;
    std::getline(input, line);
    NS_ABORT_MSG_IF(line != "cycle_id,ap_id,rate_bps,direction", "Invalid capacity CSV header");
    std::vector<std::tuple<uint32_t, uint32_t, uint64_t>> events;
    std::set<std::pair<uint32_t, uint32_t>> keys;
    const std::regex row("([0-9]+),([0-9]+),([0-9]+),both");
    while (std::getline(input, line))
    {
        std::smatch match;
        NS_ABORT_MSG_IF(!std::regex_match(line, match, row), "Invalid capacity row: " << line);
        uint64_t cycle = 0, ap = 0, rate = 0;
        try
        {
            cycle = std::stoull(match[1].str());
            ap = std::stoull(match[2].str());
            rate = std::stoull(match[3].str());
        }
        catch (const std::exception&)
        {
            NS_ABORT_MSG("Capacity value outside uint64 range: " << line);
        }
        NS_ABORT_MSG_IF(cycle < 1 || cycle > m_cycleCount || ap > 2 || rate == 0,
                        "Invalid capacity cycle/AP/rate: " << line);
        NS_ABORT_MSG_IF(!keys.emplace(cycle, ap).second, "Duplicate cycle/AP capacity event");
        events.emplace_back(cycle, ap, rate);
    }
    NS_ABORT_MSG_IF(input.bad(), "Failed reading capacity schedule");
    std::sort(events.begin(), events.end());
    // Validate all links before modifying any transmitter.
    for (uint32_t ap = 0; ap < 3; ++ap)
    {
        NS_ABORT_MSG_IF(ap > 0 && p2pDevices.size() <= ap, "Missing Wi-Fi backhaul");
        const auto& devices = ap == 0 ? m_pgwCerDevices : p2pDevices[ap];
        NS_ABORT_MSG_IF(devices.GetN() != 2, "Capacity schedule requires two link endpoints");
        for (uint32_t end = 0; end < 2; ++end)
        {
            NS_ABORT_MSG_IF(!DynamicCast<PointToPointNetDevice>(devices.Get(end)),
                            "Capacity endpoint is not PointToPointNetDevice");
        }
    }
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto directory = std::filesystem::path(m_outputDir) / "capacity_schedule" /
        ("seed" + std::to_string(m_rngSeed) + "_" + std::to_string(stamp) +
         "_pid" + std::to_string(getpid()));
    std::filesystem::create_directories(directory);
    m_capacityScheduleDirectory = directory.string();
    std::filesystem::copy_file(m_capacityEventsPath, directory / "planned_events.csv");
    std::filesystem::copy_file(m_queueSettingPath, directory / "setting.json");
    std::ofstream meta(directory / "metadata.txt");
    meta << "ns3_version=3.44\nseed=" << m_rngSeed << "\nrng_run=" << RngSeedManager::GetRun()
         << "\nmethod=" << m_assignmentMethod << "\ncycle_start_offset_sec="
         << m_cycleStartOffset.GetSeconds() << "\ncycle_duration_sec="
         << m_cycleDuration.GetSeconds() << "\ndirection=both\n" << m_queueArgs;
    meta.close();
    NS_ABORT_MSG_IF(meta.fail(), "Cannot save capacity schedule metadata");
    std::ofstream terminals(directory / "initial_terminals.csv");
    terminals << "ue_id,app_type,initial_bs_id_1based\n";
    for (uint32_t i = 0; i < m_termData.size(); ++i)
    {
        terminals << i + 1 << ',' << m_termData[i].use_appli << ',' << m_termData[i].apNo << '\n';
    }
    terminals.close();
    NS_ABORT_MSG_IF(terminals.fail(), "Cannot save initial capacity experiment assignment");
    m_capacityScheduleEvents.open(directory / "events.csv");
    m_capacityScheduleEvents << "sim_time,cycle_id,ap_id,direction,old_bps,new_bps\n";
    // Log initial configured rates independently for each endpoint.
    for (uint32_t ap = 0; ap < 3; ++ap)
    {
        const auto& devices = ap == 0 ? m_pgwCerDevices : p2pDevices[ap];
        DataRateValue rate;
        devices.Get(0)->GetAttribute("DataRate", rate);
        ApplyCapacityEvent(0, ap, rate.Get().GetBitRate());
    }
    // Registered before application/cycle events: a boundary change precedes measurement.
    for (const auto& [cycle, ap, rate] : events)
    {
        Simulator::Schedule(m_cycleStartOffset + m_cycleDuration * (cycle - 1),
                            &NetSim::ApplyCapacityEvent, this, cycle, ap, rate);
    }
    std::cout << "[capacity schedule] " << directory.string() << std::endl;
}

void
NetSim::ApplyCapacityEvent(uint32_t cycle, uint32_t ap, uint64_t bps)
{
    const auto& devices = ap == 0 ? m_pgwCerDevices : p2pDevices[ap];
    for (uint32_t end = 0; end < 2; ++end)
    {
        auto device = DynamicCast<PointToPointNetDevice>(devices.Get(end));
        DataRateValue old;
        device->GetAttribute("DataRate", old);
        device->SetDataRate(DataRate(bps)); // Preserve queued and in-flight packets.
        const char* direction = ap == 0 ? (end == 0 ? "pgw_to_cer" : "cer_to_pgw")
                                       : (end == 0 ? "ap_to_router" : "router_to_ap");
        m_capacityScheduleEvents << Simulator::Now().GetSeconds() << ',' << cycle << ','
                                 << ap << ',' << direction << ',' << old.Get().GetBitRate()
                                 << ',' << bps << '\n';
    }
    m_capacityScheduleEvents.flush();
    NS_ABORT_MSG_IF(!m_capacityScheduleEvents, "Cannot write capacity schedule events");
}

void
NetSim::FinishCapacitySchedule()
{
    if (!m_capacityScheduleEvents.is_open())
    {
        return;
    }
    m_capacityScheduleEvents.close();
    NS_ABORT_MSG_IF(m_capacityScheduleEvents.fail(), "Cannot close capacity events");
    std::ofstream status(std::filesystem::path(m_capacityScheduleDirectory) /
                         "simulation_completed.txt");
    status << "sim_time=" << Simulator::Now().GetSeconds() << '\n';
    status.close();
    NS_ABORT_MSG_IF(status.fail(), "Cannot write capacity completion marker");
}
} // namespace ns3
