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
        m_cycleCount = 5;
    }
    m_simulationDuration = m_cycleStartOffset + m_cycleDuration * m_cycleCount + Seconds(5.0);
}

void NetSim::ScheduleMonitorWindows()
{
    if (m_monitorApps.empty() || m_cycleCount == 0)
    {
        return;
    }

    for (uint32_t cycle = 0; cycle < m_cycleCount; ++cycle)
    {
        Time cycleStart = m_cycleStartOffset + m_cycleDuration * cycle;
        Time rttStart = cycleStart + m_monitorStartOffset;
        Time rttStop  = cycleStart + m_monitorStopOffset;
        Time tpStart  = cycleStart + m_monitorStartOffset;
        Time tpStop   = cycleStart + m_terminalTpStopOffset;
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

void NetSim::HandoverRequest(const std::vector<int>& assignment)
{
    if (assignment.empty())
    {
        std::cout << "[Handover] Received empty assignment; skipping" << std::endl;
        return;
    }

    if (!m_termAccessState.empty())
    {
        // 差分抽出
        std::vector<std::pair<uint32_t, int>> switchList;
        size_t termCount = std::min<size_t>(assignment.size(), m_termAccessState.size());
        for (size_t i = 0; i < termCount; ++i)
        {
            if (assignment[i] != m_termAccessState[i].currentAp)
            {
                switchList.push_back({static_cast<uint32_t>(i), assignment[i]});
            }
        }

        if (!switchList.empty())
        {
            ApplyHandoverBatch(switchList);
        }
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

    // Detailed handover status is recorded in master_log; suppress console noise.
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

    // Console output intentionally disabled.
}

void NetSim::ApplyHandoverBatch(const std::vector<std::pair<uint32_t, int>>& switchList)
{
    for (const auto& entry : switchList)
    {
        uint32_t termIdx = entry.first;
        int newAp = entry.second;

        if (termIdx >= m_termAccessState.size())
        {
            continue;
        }

        int oldAp = m_termAccessState[termIdx].currentAp;
        RatType oldRat = m_termAccessState[termIdx].currentRat;
        RatType newRat = GetRatTypeForAp(newAp);

        Ipv4Address oldIp = GetActiveIpv4(termIdx);

        if (oldRat == RatType::NR && newRat == RatType::NR)
        {
            // NR→NR: gNBが1台のためno-op
            continue;
        }
        else if (oldRat == RatType::WIFI && newRat == RatType::WIFI)
        {
            WifiToWifiHandover(termIdx, oldAp, newAp);
        }
        else if (oldRat == RatType::NR && newRat == RatType::WIFI)
        {
            NrToWifiHandover(termIdx, newAp);
        }
        else if (oldRat == RatType::WIFI && newRat == RatType::NR)
        {
            WifiToNrHandover(termIdx);
        }

        Ipv4Address newIp = GetActiveIpv4(termIdx);
        LogHandoverEvent(Simulator::Now().GetSeconds(), termIdx, oldAp, newAp,
                         oldRat, newRat, oldIp, newIp, "OK");
    }

    // ハンドオーバ後にIPマップを再構築
    BuildTerminalIpMap();

    // FlowMonitor統計リセット
    if (m_termFlowMonitor != nullptr)
    {
        m_termFlowMonitor->ResetAllStats();
    }
}

void NetSim::SwitchDefaultRoute(Ptr<Node> termNode, uint32_t newIfIndex, Ipv4Address newGateway)
{
    if (termNode == nullptr)
    {
        return;
    }

    Ptr<Ipv4> ipv4 = termNode->GetObject<Ipv4>();
    if (!ipv4)
    {
        return;
    }

    Ipv4StaticRoutingHelper rh;
    Ptr<Ipv4StaticRouting> rt = rh.GetStaticRouting(ipv4);

    // 既存のデフォルトルートを全削除（後ろから）
    for (int32_t r = static_cast<int32_t>(rt->GetNRoutes()) - 1; r >= 0; --r)
    {
        Ipv4RoutingTableEntry entry = rt->GetRoute(static_cast<uint32_t>(r));
        if (entry.GetDest() == Ipv4Address("0.0.0.0") &&
            entry.GetDestNetworkMask() == Ipv4Mask("0.0.0.0"))
        {
            rt->RemoveRoute(static_cast<uint32_t>(r));
        }
    }

    // 新インターフェースをUpにしてデフォルトルートを設定
    if (!ipv4->IsUp(newIfIndex))
    {
        ipv4->SetUp(newIfIndex);
    }
    rt->SetDefaultRoute(newGateway, newIfIndex);
}

void NetSim::ApplyHandoverState(uint32_t termIdx, int newAp, RatType newRat, Ipv4Address newIp)
{
    TermAccessState& state = m_termAccessState[termIdx];
    RebindTerminalApps(termIdx, newIp);
    state.currentAp = newAp;
    state.currentRat = newRat;
    state.lastSwitchTime = Simulator::Now();
}

void NetSim::WifiToWifiHandover(uint32_t termIdx, int oldAp, int newAp)
{
    Ptr<Node> term = terms[termIdx];
    TermAccessState& state = m_termAccessState[termIdx];

    uint32_t newApIdx = static_cast<uint32_t>(newAp - 1);

    // 新Wi-Fiインターフェースを有効化してルート設定
    if (newApIdx < state.wifiIfIndex.size() && newApIdx < m_wifiApGatewayIps.size())
    {
        SwitchDefaultRoute(term, state.wifiIfIndex[newApIdx], m_wifiApGatewayIps[newApIdx]);
    }

    Ipv4Address newIp = state.wifiIpv4[newApIdx];
    ApplyHandoverState(termIdx, newAp, RatType::WIFI, newIp);

    (void)oldAp;
}

void NetSim::NrToWifiHandover(uint32_t termIdx, int newAp)
{
    Ptr<Node> term = terms[termIdx];
    TermAccessState& state = m_termAccessState[termIdx];

    uint32_t newApIdx = static_cast<uint32_t>(newAp - 1);

    // Wi-Fiインターフェースを有効化してルート設定
    if (newApIdx < state.wifiIfIndex.size() && newApIdx < m_wifiApGatewayIps.size())
    {
        SwitchDefaultRoute(term, state.wifiIfIndex[newApIdx], m_wifiApGatewayIps[newApIdx]);
    }

    Ipv4Address newIp = state.wifiIpv4[newApIdx];
    ApplyHandoverState(termIdx, newAp, RatType::WIFI, newIp);

    (void)newIp;
}

void NetSim::WifiToNrHandover(uint32_t termIdx)
{
    Ptr<Node> term = terms[termIdx];
    TermAccessState& state = m_termAccessState[termIdx];

    // NRインターフェースを有効化してルート設定
    SwitchDefaultRoute(term, state.nrIfIndex, m_nrGateway);

    int oldAp = state.currentAp;
    Ipv4Address newIp = state.nrIpv4;
    ApplyHandoverState(termIdx, 1, RatType::NR, newIp);

    (void)oldAp;
}

RatType NetSim::GetRatTypeForAp(int apNo) const
{
    if (apNo == 1) return RatType::NR;
    return RatType::WIFI;
}

void NetSim::LogHandoverEvent(double timeSec, uint32_t termId, int oldAp, int newAp,
                               RatType oldRat, RatType newRat,
                               Ipv4Address oldIp, Ipv4Address newIp,
                               const std::string& result)
{
    // master_log だけを残すため、handover-events.csv は出力しない。
    return;

    static bool headerWritten = false;
    const std::string filePath = std::string(m_outputDir) + "handover-events.csv";

    std::ofstream ofs(filePath, std::ios::app);
    if (!ofs.good())
    {
        return;
    }

    if (!headerWritten)
    {
        ofs << "time_s,term_id,old_ap,new_ap,old_rat,new_rat,old_ip,new_ip,result\n";
        headerWritten = true;
    }

    auto ratStr = [](RatType r) -> std::string {
        if (r == RatType::NR)  return "NR";
        return "WIFI";
    };

    ofs << timeSec << ","
        << termId << ","
        << oldAp << ","
        << newAp << ","
        << ratStr(oldRat) << ","
        << ratStr(newRat) << ","
        << oldIp << ","
        << newIp << ","
        << result << "\n";
}

} // namespace ns3
