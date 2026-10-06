// SPDX-License-Identifier: GPL-2.0-only
#include "research-wall-profiler.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <stdexcept>

namespace ns3
{

ResearchWallProfiler&
ResearchWallProfiler::Get()
{
    static ResearchWallProfiler profiler;
    return profiler;
}

double
ResearchWallProfiler::ElapsedMs(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void
ResearchWallProfiler::Start(const std::string& directory, bool detailed, uint64_t wifiSampleEvery)
{
    if (wifiSampleEvery == 0)
    {
        throw std::invalid_argument("wifiSampleEvery must be positive");
    }
    if (m_enabled)
    {
        throw std::logic_error("Wall profiler already active");
    }
    if (!std::filesystem::create_directories(directory))
    {
        throw std::runtime_error("Profiler directory already exists: " + directory);
    }
    m_summary.open(directory + "/functions.csv");
    m_boundaries.open(directory + "/boundaries.csv");
    m_deviceOutput.open(directory + "/devices.csv");
    if (!m_summary || !m_boundaries || !m_deviceOutput)
    {
        throw std::runtime_error("Cannot open profiler output: " + directory);
    }
    m_summary << "snapshot,cycle_id,function,call_count,total_wall_ms,mean_wall_ms,max_wall_ms\n";
    m_deviceOutput << "snapshot,cycle_id,node_id,net_device_index,ap_id_1based,rat,role,selection,metric,count\n";
    m_boundaries << "label,cycle_id,sim_time_s,interval_sim_s,interval_wall_ms,event_count,interval_event_count\n";
    m_summary << std::fixed << std::setprecision(6);
    m_boundaries << std::fixed << std::setprecision(6);
    m_stats.clear();
    m_devices.clear();
    m_selectedAp.clear();
    m_wifiSampleEvery = wifiSampleEvery;
    m_wifiCalls = 0;
    m_hasBoundary = false;
    m_detailed = detailed;
    m_enabled = true;
}

ResearchWallProfiler::Scope::Scope(const char* name, bool detailed, bool selected)
{
    if (!selected)
    {
        return;
    }
    auto& profiler = Get();
    if (profiler.m_enabled && (!detailed || profiler.m_detailed))
    {
        m_stats = &profiler.m_stats[name];
        m_start = Clock::now();
    }
}

ResearchWallProfiler::Scope::~Scope()
{
    if (m_stats)
    {
        const double elapsed = ElapsedMs(m_start);
        ++m_stats->calls;
        m_stats->totalMs += elapsed;
        m_stats->maxMs = std::max(m_stats->maxMs, elapsed);
    }
}

void
ResearchWallProfiler::Record(const char* name, double milliseconds)
{
    if (!m_enabled)
    {
        return;
    }
    auto& stats = m_stats[name];
    ++stats.calls;
    stats.totalMs += milliseconds;
    stats.maxMs = std::max(stats.maxMs, milliseconds);
}

void
ResearchWallProfiler::Snapshot(const char* label, uint32_t cycle)
{
    if (!m_enabled)
    {
        return;
    }
    for (const auto& [name, stats] : m_stats)
    {
        if (stats.calls == 0)
        {
            continue; // A still-active outer scope is not a completed measurement.
        }
        m_summary << label << ',' << cycle << ',' << name << ',' << stats.calls << ','
                  << stats.totalMs << ',' << stats.totalMs / stats.calls << ',' << stats.maxMs
                  << '\n';
    }
    m_summary.flush();
    static constexpr const char* metrics[] = {
        "tx_signal", "rx_scheduled", "rx_arrival", "wifi_start_rx", "wifi_inactive_phy",
        "wifi_foreign", "wifi_disabled", "wifi_weak", "wifi_cannot_start", "wifi_preamble"};
    static constexpr const char* selections[] = {"selected", "unselected", "infrastructure", "unknown"};
    for (const auto& [key, device] : m_devices)
    {
        for (size_t selection = 0; selection < device.counts.size(); ++selection)
        {
            for (size_t metric = 0; metric < static_cast<size_t>(Metric::COUNT); ++metric)
            {
                const auto count = device.counts[selection][metric];
                if (count != 0)
                {
                    m_deviceOutput << label << ',' << cycle << ',' << key.first << ',' << key.second
                                   << ',' << device.ap << ',' << device.rat << ',' << device.role
                                   << ',' << selections[selection] << ',' << metrics[metric] << ',' << count << '\n';
                }
            }
        }
    }
    m_deviceOutput.flush();
}

bool
ResearchWallProfiler::SampleWifiRx()
{
    return Detailed() && (m_wifiCalls++ % m_wifiSampleEvery == 0);
}

void
ResearchWallProfiler::RegisterDevice(uint32_t node, uint32_t device, int ap,
                                     const std::string& rat, const std::string& role)
{
    if (Detailed())
    {
        auto& stats = m_devices[{node, device}];
        stats.ap = ap;
        stats.rat = rat;
        stats.role = role;
    }
}

void
ResearchWallProfiler::SelectAp(uint32_t node, int ap)
{
    if (Detailed())
    {
        m_selectedAp[node] = ap;
    }
}

void
ResearchWallProfiler::Count(uint32_t node, uint32_t device, Metric metric)
{
    if (!Detailed())
    {
        return;
    }
    auto& stats = m_devices[{node, device}];
    size_t selection = 3;
    if (stats.role == "terminal")
    {
        const auto it = m_selectedAp.find(node);
        if (it != m_selectedAp.end() && stats.ap > 0)
        {
            selection = (it->second == stats.ap) ? 0 : 1;
        }
    }
    else if (stats.role != "unknown")
    {
        selection = 2;
    }
    ++stats.counts[selection][static_cast<size_t>(metric)];
}

void
ResearchWallProfiler::Boundary(const char* label, uint32_t cycle, double simSeconds, uint64_t events)
{
    if (!m_enabled)
    {
        return;
    }
    const auto now = Clock::now();
    const double wallMs = m_hasBoundary
                              ? std::chrono::duration<double, std::milli>(now - m_previousBoundary).count()
                              : 0.0;
    m_boundaries << label << ',' << cycle << ',' << simSeconds << ','
                 << (m_hasBoundary ? simSeconds - m_previousSimSeconds : 0.0) << ',' << wallMs
                 << ',' << events << ',' << (m_hasBoundary ? events - m_previousEvents : 0)
                 << '\n';
    m_boundaries.flush();
    m_previousBoundary = now;
    m_previousSimSeconds = simSeconds;
    m_previousEvents = events;
    m_hasBoundary = true;
    Snapshot(label, cycle);
}

void
ResearchWallProfiler::Finish()
{
    if (m_enabled)
    {
        Snapshot("final", 0);
        m_summary.close();
        m_boundaries.close();
        m_deviceOutput.close();
        m_enabled = false;
    }
}

} // namespace ns3
