// SPDX-License-Identifier: GPL-2.0-only
// Small ns-3 event/RNG equivalence smoke test, not a network-performance benchmark.
#include "ns3/core-module.h"
#include "ns3/research-wall-profiler.h"
#include <iostream>
using namespace ns3;
int main(int argc, char** argv)
{
    RngSeedManager::SetSeed(1001);
    auto rng = CreateObject<UniformRandomVariable>();
    rng->SetStream(3);
    auto& p = ResearchWallProfiler::Get();
    if (argc > 1) p.Start(argv[1], true);
    uint32_t total = 0;
    for (unsigned i = 1; i <= 5; ++i)
    {
        Simulator::Schedule(MilliSeconds(i), [&, i] {
            ResearchWallProfiler::Scope scope("smoke_event", true);
            total += rng->GetInteger(0, 100);
            if (i == 3) p.Boundary("cycle_end", 1, Simulator::Now().GetSeconds(), Simulator::GetEventCount());
        });
    }
    p.Boundary("run_start", 0, Simulator::Now().GetSeconds(), Simulator::GetEventCount());
    {
        ResearchWallProfiler::Scope scope("Simulator::Run");
        Simulator::Run();
    }
    p.Boundary("run_end", 0, Simulator::Now().GetSeconds(), Simulator::GetEventCount());
    std::cout << "rng_total=" << total << " events=" << Simulator::GetEventCount() << '\n';
    {
        ResearchWallProfiler::Scope scope("Simulator::Destroy");
        Simulator::Destroy();
    }
    p.Finish();
}
