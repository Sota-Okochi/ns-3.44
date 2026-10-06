// SPDX-License-Identifier: GPL-2.0-only
#ifndef RESEARCH_WALL_PROFILER_H
#define RESEARCH_WALL_PROFILER_H

#include <chrono>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <array>
#include <vector>

namespace ns3
{

/** Optional wall-clock diagnostics for the research scenario.
 * Single simulation thread only. No simulation events or RNG draws are added.
 * All timings are inclusive; nested rows must not be summed.
 */
class ResearchWallProfiler
{
  public:
    using Clock = std::chrono::steady_clock;
    struct Stats
    {
        uint64_t calls{0};
        double totalMs{0};
        double maxMs{0};
    };

    class Scope
    {
      public:
        explicit Scope(const char* name, bool detailed = false, bool selected = true);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

      private:
        Stats* m_stats{nullptr};
        Clock::time_point m_start;
    };

    static ResearchWallProfiler& Get();
    // directory must be a new run directory, protecting previous measurements.
    void Start(const std::string& directory, bool detailed, uint64_t wifiSampleEvery = 1024);
    bool Enabled() const { return m_enabled; }
    bool Detailed() const { return m_enabled && m_detailed; }
    bool SampleWifiRx();
    enum class Metric : size_t
    {
        TX_SIGNAL, RX_SCHEDULED, RX_ARRIVAL, WIFI_START,
        WIFI_INACTIVE_PHY, WIFI_FOREIGN, WIFI_DISABLED, WIFI_WEAK,
        WIFI_CANNOT_START, WIFI_PREAMBLE, COUNT
    };
    void RegisterDevice(uint32_t node, uint32_t device, int ap, const std::string& rat,
                        const std::string& role);
    void SelectAp(uint32_t node, int ap);
    void Count(uint32_t node, uint32_t device, Metric metric);
    void Record(const char* name, double milliseconds);
    void Boundary(const char* label, uint32_t cycle, double simSeconds, uint64_t events);
    void Snapshot(const char* label, uint32_t cycle);
    void Finish();
    static double ElapsedMs(Clock::time_point start);

  private:
    bool m_enabled{false};
    bool m_detailed{false};
    bool m_hasBoundary{false};
    uint64_t m_previousEvents{0};
    double m_previousSimSeconds{0};
    Clock::time_point m_previousBoundary;
    std::map<std::string, Stats> m_stats;
    std::ofstream m_summary;
    std::ofstream m_boundaries;
    struct DeviceStats
    {
        int ap{-1};
        std::string rat{"unknown"};
        std::string role{"unknown"};
        // selected, unselected, infrastructure, unknown; classified at observation time.
        std::array<std::array<uint64_t, static_cast<size_t>(Metric::COUNT)>, 4> counts{};
    };
    std::map<std::pair<uint32_t, uint32_t>, DeviceStats> m_devices;
    std::map<uint32_t, int> m_selectedAp;
    std::ofstream m_deviceOutput;
    uint64_t m_wifiSampleEvery{1024};
    uint64_t m_wifiCalls{0};
};

} // namespace ns3
#endif
