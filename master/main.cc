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

    ns3::NetSim sim;
    sim.Init(argc, argv);
    sim.RunSim();

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
