// Synthetic observed data only: no network simulation and no research result.
#include "APselection.h"
#include <chrono>
#include <thread>
int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    ns3::ApSelectionInput input;
    input.baseStations = 3;
    input.terminals = 80;
    input.assignmentMethod = argv[1];
    input.outputDir = argv[2];
    input.drlServerPort = std::stoi(argv[3]);
    input.drlTimeoutMs = 10000;
    input.maxSwitches = 2;
    input.capacities = {80, 40, 20};
    input.initialRtt = {50, 50, 50};
    input.rngSeed = 7;
    for (int i = 0; i < 80; ++i)
    {
        input.initialAp.push_back(i % 3 + 1);
        input.useAppli.push_back(i % 4 + 1);
    }
    ns3::APselection selection;
    selection.init(input);
    selection.SetTotalCycles(5);
    selection.SetHandoverCallback([](const std::vector<int>&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    });
    for (int cycle = 1; cycle <= 5; ++cycle)
    {
        selection.StartNewCycle(cycle);
        for (int ap = 0; ap < 3; ++ap)
        {
            selection.setData("127.0.0.1", "MONITOR_AP" + std::to_string(ap) + ",50");
            if (input.assignmentMethod == "marl_ppo")
                selection.SetMarlRtt(cycle, ap, "ok", 50, 10, 10, 0, 0);
        }
        for (int i = 0; i < 80; ++i)
        {
            selection.setTerminalTp(i, 4e6);
            if (input.assignmentMethod == "marl_ppo")
                selection.SetMarlTp(i, selection.GetLastAssignment()[i]-1, 4e6, "ok");
        }
        selection.tmain();
    }
}
