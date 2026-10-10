#include "APselection.h"
#include "ns3/abort.h"
#include <cmath>
#include <sstream>
#include <iomanip>

namespace ns3
{

// Single actual action per measurement window. No estimated-H safety filter.
void APselection::reactive_dqn_assignment()
{
    NS_ABORT_MSG_IF(aps != 3 || terms <= 0 || m_totalCycles < 2,
                    "reactive_dqn requires 3 APs and at least 2 measurement cycles");
    const bool done = m_cycleIndex >= m_totalCycles;
    std::vector<double> state;
    std::vector<int> valid{0};
    std::vector<int> counts(12, 0), tpCount(3, 0);
    std::vector<double> tpSum(3, 0.0);
    int unsatisfied = 0, observed = 0;
    auto logValue = [](double v) { return std::log1p(std::max(0.0, v)); };
    for (int i = 0; i < terms; ++i)
    {
        int ap = initial_AP[i] - 1, app = initial_app[i] - 1;
        NS_ABORT_MSG_IF(ap < 0 || ap >= 3 || app < 0 || app >= 4, "Invalid reactive state ID");
        for (int j = 0; j < 3; ++j) { state.push_back(ap == j); }
        for (int j = 0; j < 4; ++j) { state.push_back(app == j); }
        bool tpValid = m_has_terminal_tp[i] && std::isfinite(m_terminal_tp[i]) && m_terminal_tp[i] > 0;
        bool rttValid = m_has_rtt[ap] && std::isfinite(m_monitor_rtt[ap]) && m_monitor_rtt[ap] > 0;
        double tp = tpValid ? m_terminal_tp[i] * 1e-6 : 0;
        double sat = calculate_satisfaction(i, ap);
        NS_ABORT_MSG_IF(!std::isfinite(sat) || sat < 0, "Invalid reactive satisfaction");
        state.push_back(logValue(tp));
        state.push_back(logValue(rttValid ? m_monitor_rtt[ap] : 0));
        state.push_back(logValue(sat));
        state.push_back(app < 2);
        state.push_back(tpValid);
        state.push_back(rttValid);
        const uint32_t last = i < static_cast<int>(m_switchCycle.size()) ? m_switchCycle[i] : 0;
        state.push_back(last ? std::min(100u, m_cycleIndex-last)/100.0 : 0.0);
        state.push_back(last != 0);
        ++counts[ap*4+app];
        if (app < 2 && tpValid) { tpSum[ap] += tp; ++tpCount[ap]; }
        unsatisfied += sat < 0.5;
        observed += app < 2 ? tpValid : rttValid;
        if (!done)
        {
            for (int j = 0; j < 3; ++j)
            {
                if (j != ap) { valid.push_back(1+i*3+j); }
            }
        }
    }
    for (int ap = 0; ap < 3; ++ap)
    {
        for (int app = 0; app < 4; ++app) { state.push_back(counts[ap*4+app]/double(terms)); }
        bool ok = m_has_rtt[ap] && std::isfinite(m_monitor_rtt[ap]) && m_monitor_rtt[ap] > 0;
        state.push_back(logValue(ok ? m_monitor_rtt[ap] : 0));
        state.push_back(ok);
        state.push_back(logValue(tpCount[ap] ? tpSum[ap]/tpCount[ap] : 0));
        state.push_back(tpCount[ap]/double(terms));
        state.push_back(tpCount[ap] != 0);
    }
    double h = m_cycleHarmonicMeans.back();
    NS_ABORT_MSG_IF(!std::isfinite(h) || h < 0, "Invalid reactive H");
    state.push_back(logValue(h));
    state.push_back(unsatisfied/double(terms));
    state.push_back(observed/double(terms));
    std::ostringstream out;
    out << std::setprecision(17) << "{\"schema\":\"reactive_v1_log\",\"cycle\":" << m_cycleIndex
        << ",\"done\":" << (done ? "true" : "false") << ",\"h\":" << h
        << ",\"state\":[";
    for (size_t i = 0; i < state.size(); ++i) { if (i) out << ','; out << state[i]; }
    out << "],\"valid\":[";
    for (size_t i = 0; i < valid.size(); ++i) { if (i) out << ','; out << valid[i]; }
    out << "],\"assignment\":[";
    for (int i = 0; i < terms; ++i) { if (i) out << ','; out << initial_AP[i]-1; }
    out << "]}";
    int action = -1, ue = -1, bs = -1;
    double q = 0;
    std::vector<double> values;
    std::string error;
    NS_ABORT_MSG_IF(!SendCentralizedStateReceiveAction(out.str(), action, ue, bs, q, values, error),
                    "reactive_dqn communication failed: " << error);
    NS_ABORT_MSG_IF(std::find(valid.begin(), valid.end(), action) == valid.end(), "Invalid reactive action");
    std::vector<int> assignment = initial_AP;
    if (action > 0) { assignment[(action-1)/3] = (action-1)%3+1; }
    m_lastAssignment = assignment;
    // Estimates remain diagnostic only; neither the policy nor its reward reads them.
    PrepareDecisionLogState(initial_AP, assignment, h, calculate_harmonic_mean_for_assignment(assignment));
    if (!done && m_handoverCallback) { m_handoverCallback(assignment); }
}

} // namespace ns3
