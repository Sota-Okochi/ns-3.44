#include "NetSim.h"

#include <algorithm>
#include <iostream>

namespace ns3 {

void NetSim::ConfigureCycleParameters()
{
    if (!m_cycleDuration.IsPositive())
    {
        m_cycleDuration = Seconds(7.0);
    }
    if (m_cycleCount == 0)
    {
        m_cycleCount = 2;
    }
    m_simulationDuration = m_cycleDuration * m_cycleCount;
}

void NetSim::ScheduleMonitorWindows()
{
    if (m_monitorApps.empty() || m_cycleCount == 0)
    {
        return;
    }

    const Time rttStartOffset = Seconds(1.5);
    const Time rttStopOffset = Seconds(5.4);
    const Time tpStartOffset = Seconds(1.5);
    const Time tpStopOffset = Seconds(6.2);
    for (uint32_t cycle = 0; cycle < m_cycleCount; ++cycle)
    {
        Time rttStart = m_cycleDuration * cycle + rttStartOffset;
        Time rttStop = m_cycleDuration * cycle + rttStopOffset;
        Time tpStart = m_cycleDuration * cycle + tpStartOffset;
        Time tpStop = m_cycleDuration * cycle + tpStopOffset;
        for (const auto& monitor : m_monitorApps)
        {
            if (monitor == nullptr)
            {
                continue;
            }
            Simulator::Schedule(rttStart, &APMonitorTerminal::StartContinuousMonitoring, monitor);
            Simulator::Schedule(rttStop, &APMonitorTerminal::StopMonitoring, monitor);
        }
        // 端末別TP計測: ウィンドウ開始時にFlowMonitor統計リセット、終了時に収集
        Simulator::Schedule(tpStart, &NetSim::ResetTerminalFlowStats, this);
        Simulator::Schedule(tpStop, &NetSim::CollectTerminalThroughput, this);
    }
}

void NetSim::HandleHandoverRequest(const std::vector<int>& assignment)
{
    if (assignment.empty())
    {
        std::cout << "[Handover] Received empty assignment; skipping" << std::endl;
        return;
    }

    m_activeAssignment = assignment;

    size_t termCount = std::min<size_t>(assignment.size(), m_termData.size());
    for (size_t i = 0; i < termCount; ++i)
    {
        m_termData[i].apNo = assignment[i];
    }

    if (m_apSelectionInput.initialAp.size() < assignment.size())
    {
        m_apSelectionInput.initialAp.resize(assignment.size(), 1);
    }
    std::copy_n(assignment.begin(),
                std::min(assignment.size(), m_apSelectionInput.initialAp.size()),
                m_apSelectionInput.initialAp.begin());

    PrintAssignmentSummary(assignment);
}

void NetSim::PrintAssignmentSummary(const std::vector<int>& assignment) const
{
    if (assignment.empty() || APnum == 0)
    {
        return;
    }
    std::vector<uint32_t> counts(APnum, 0);
    for (size_t i = 0; i < assignment.size(); ++i)
    {
        int apNo = assignment[i];
        if (apNo > 0 && static_cast<size_t>(apNo) <= counts.size())
        {
            counts[static_cast<size_t>(apNo - 1)] += 1;
        }
    }

    std::cout << "[Handover] New terminal assignment summary:" << std::endl;
    for (size_t i = 0; i < counts.size(); ++i)
    {
        std::cout << "  AP" << (i + 1) << ": " << counts[i] << " terminals" << std::endl;
    }
}

} // namespace ns3
