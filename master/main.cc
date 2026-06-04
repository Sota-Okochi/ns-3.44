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

    std::cout << "=====WallClock::Start()=====" << std::endl;
    std::cout << "simulation_start_time="
              << std::put_time(std::localtime(&startTime), "%Y-%m-%d %H:%M:%S")
              << std::endl;

    ns3::NetSim sim;
    sim.Init(argc, argv);
    sim.RunSim();

    const auto wallClockEnd = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = wallClockEnd - wallClockStart;
    std::cout << "=====WallClock::Elapsed()=====" << std::endl;
    std::cout << "simulation_execution_time_sec="
              << std::fixed << std::setprecision(3) << elapsed.count()
              << std::endl;

    return 0;
}
