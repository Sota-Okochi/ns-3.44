// Formula regression only: no network simulation or policy inference.
#include "ns3/core-module.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "ns3/application.h"
#define private public
#include "../contrib/kameda/model/server/APselection.h"
#undef private
#include <cmath>
#include <stdexcept>

static void
Check(double actual, double expected)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-12)
    {
        throw std::runtime_error("QoE floor mismatch");
    }
}

int
main()
{
    for (const std::string method : {"random", "logistic", "centralized_dqn"})
    {
        ns3::APselection policy;
        policy.m_assignmentMethod = method;
        policy.terms = 4;
        policy.aps = 3;
        policy.initial_app = {1, 2, 3, 4};
        policy.initial_AP = {1, 1, 1, 1};
        policy.traffic_request = {3., 8., 100., 40.};
        policy.m_monitor_rtt = {50., 50., 50.};
        policy.m_has_rtt = {true, true, true};
        policy.m_has_terminal_tp = {true, true, false, false};
        policy.m_terminal_tp = {0., 4e6, 0., 0.};
        policy.m_switchCycle = {1, 0, 0, 0};
        policy.m_cycleIndex = 1;
        policy.m_handoverGraceCycles = 2;
        Check(policy.calculate_satisfaction(0, 0), .01);
        Check(policy.calculate_satisfaction(1, 0), .5);
        Check(policy.calculate_satisfaction(2, 0), 2.);
        Check(policy.calculate_satisfaction(3, 0), .8);
        for (double raw : {0., .005, .01, .02})
        {
            policy.m_terminal_tp[0] = raw * 3e6;
            Check(policy.calculate_satisfaction(0, 0), std::max(raw, .01));
        }
        policy.m_terminal_tp[0] = 0.;
        policy.cal_initial_harmonic_mean();
        Check(policy.m_cycleHarmonicMeans.back(), 4. / (100. + 2. + .5 + 1.25));
        policy.m_monitor_rtt[0] = 100000.;
        Check(policy.calculate_satisfaction(2, 0), .01);
        Check(policy.estimate_satisfaction_for_assignment(2, 0, policy.initial_AP), .01);
        // Preserve missing-data grace, but not for a measured zero.
        policy.m_has_terminal_tp[0] = false;
        Check(policy.calculate_satisfaction(0, 0), 1.);
        policy.m_handoverGraceCycles = 0;
        Check(policy.calculate_satisfaction(0, 0), .01);
    }
    std::cout << "PASS: random/logistic/centralized_dqn shared QoE floor\n";
}
