#include "NetSim.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

int main(int argc, char *argv[]){

    const auto wallClockStartSystem = std::chrono::system_clock::now();
    const auto wallClockStart = std::chrono::steady_clock::now();
    const std::time_t startTime =
        std::chrono::system_clock::to_time_t(wallClockStartSystem);

    std::cout << "============================================================" << std::endl;
    std::cout << "開始時間: "
              << std::put_time(std::localtime(&startTime), "%Y-%m-%d %H:%M:%S")
              << std::endl;

    using Profiler = ns3::ResearchWallProfiler;
    auto& profiler = Profiler::Get();
    Profiler::Clock::time_point destructionStart;
    {
        const auto constructStart = Profiler::Clock::now();
        ns3::NetSim sim;
        const double constructMs = Profiler::ElapsedMs(constructStart);
        const auto initStart = Profiler::Clock::now();
        sim.Init(argc, argv);
        profiler.Record("NetSim::constructor", constructMs);
        profiler.Record("NetSim::Init", Profiler::ElapsedMs(initStart));
        profiler.Snapshot("after_init", 0);
        sim.RunSim();
        // The enclosing scope includes member destruction, not just ~NetSim's body.
        destructionStart = Profiler::Clock::now();
    }
    profiler.Record("NetSim::destruction", Profiler::ElapsedMs(destructionStart));
    profiler.Record("main::lifetime_before_report", Profiler::ElapsedMs(wallClockStart));
    profiler.Finish();

    const auto wallClockEnd = std::chrono::steady_clock::now();
    const auto elapsedMinutes =
        std::chrono::duration_cast<std::chrono::minutes>(wallClockEnd - wallClockStart);
    const auto hours = elapsedMinutes.count() / 60;
    const auto minutes = elapsedMinutes.count() % 60;

    std::cout << "============================================================" << std::endl;
    std::cout << "総実行時間: "
              << hours << "時間" << minutes << "分"
              << std::endl;

    return 0;
}
