#include "APselection.h"
#include "ns3/abort.h"
#include "ns3/simulator.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cmath>
#include <iomanip>
#include <limits>

namespace ns3
{
void APselection::SetMarlTp(int ue, int ap, double bps, const std::string& status)
{
    NS_ABORT_MSG_IF(ue < 0 || ue >= terms || ap < 0 || ap >= 3, "Invalid MARL TP identity");
    m_marlActualAp.at(ue) = ap;
    m_marlTpStatus.at(ue) = status;
    m_terminal_tp.at(ue) = bps;
}

void APselection::SetMarlRtt(uint32_t cycle, uint32_t ap, const std::string& status,
                            double rtt, uint32_t sent, uint32_t replies, uint32_t failures, uint32_t late)
{
    if (cycle != m_cycleIndex || ap >= 3 || m_marlRtt.at(ap).status != "report_missing") return;
    NS_ABORT_MSG_IF(!std::isfinite(rtt) || replies > sent ||
        (status != "ok" && status != "timeout" && status != "probe_send_error" && status != "measurement_error") ||
        (status == "ok" && (rtt <= 0 || replies == 0 || failures != 0)) ||
        (status == "timeout" && (sent == 0 || replies != 0 || failures != 0)), "Invalid MARL RTT status");
    m_marlRtt.at(ap) = {status, rtt, sent, replies, failures, late};
}

void APselection::marl_ppo_assignment()
{
    NS_ABORT_MSG_IF(aps != 3 || terms < 1 || m_totalCycles != 5, "marl_ppo requires 3 APs / 5 cycles");
    const bool done = m_cycleIndex == m_totalCycles;
    std::ostringstream out;
    out << std::setprecision(17) << "{\"schema\":\"marl_v1\",\"cycle\":" << m_cycleIndex
        << ",\"seed\":" << m_rngSeed << ",\"sim_time\":" << Simulator::Now().GetSeconds()
        << ",\"done\":" << (done ? "true" : "false") << ",\"users\":[";
    for (int i=0; i<terms; ++i)
    {
        if (i) out << ',';
        const int ap = m_marlActualAp.at(i);
        NS_ABORT_MSG_IF(ap < 0 || ap != initial_AP[i]-1, "MARL actual assignment missing/mismatch");
        out << "{\"ue_id\":" << i << ",\"app\":" << initial_app[i]-1 << ",\"bs\":" << ap
            << ",\"tp_status\":\"" << m_marlTpStatus[i] << "\",\"tp_mbps\":";
        if (m_marlTpStatus[i] == "ok" && std::isfinite(m_terminal_tp[i]) && m_terminal_tp[i] >= 0)
            out << m_terminal_tp[i]*1e-6;
        else out << "null";
        out << '}';
    }
    out << "],\"aps\":[";
    for (int ap=0; ap<3; ++ap)
    {
        const auto& r = m_marlRtt.at(ap);
        if (ap) out << ',';
        out << "{\"ap_id\":" << ap << ",\"status\":\"" << r.status << "\",\"rtt_ms\":";
        if (r.status == "ok") out << r.rtt;
        else out << "null";
        out << ",\"sent\":" << r.sent << ",\"replies\":" << r.replies
            << ",\"send_failures\":" << r.failures << ",\"late\":" << r.late << '}';
    }
    out << "]}";
    // A worker has its own listening endpoint; no global files or shared episode state.
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    NS_ABORT_MSG_IF(fd < 0, "MARL socket create failed");
    timeval tv{static_cast<time_t>(m_drlTimeoutMs/1000), static_cast<suseconds_t>((m_drlTimeoutMs%1000)*1000)};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    sockaddr_in addr{};
    addr.sin_family = AF_INET; addr.sin_port = htons(m_drlServerPort);
    NS_ABORT_MSG_IF(inet_pton(AF_INET, m_drlServerHost.c_str(), &addr.sin_addr) != 1,
                    "MARL host must be an IPv4 literal");
    NS_ABORT_MSG_IF(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0,
                    "MARL learner unavailable; use scripts/run_marl.py");
    const std::string wire = out.str()+"\n";
    size_t offset = 0;
    while (offset < wire.size())
    {
        auto count = ::send(fd, wire.data()+offset, wire.size()-offset, MSG_NOSIGNAL);
        NS_ABORT_MSG_IF(count <= 0, "MARL request send failed");
        offset += count;
    }
    std::string reply;
    char buffer[4096];
    while (reply.find('\n') == std::string::npos)
    {
        auto count = ::recv(fd, buffer, sizeof(buffer), 0);
        NS_ABORT_MSG_IF(count <= 0, "MARL response missing/timeout");
        reply.append(buffer, count);
        NS_ABORT_MSG_IF(reply.size() > 65536, "MARL response too large");
    }
    ::close(fd);
    std::istringstream input(reply);
    std::string tag, extra;
    uint32_t cycle, policy;
    double h;
    NS_ABORT_MSG_IF(!(input >> tag >> cycle >> policy >> h) || tag != "MARL_PPO_V1" ||
                    cycle != m_cycleIndex || !std::isfinite(h) || h <= 0, "MARL learner rejected observation: " << reply);
    std::vector<int> assignment(terms);
    for (int i=0; i<terms; ++i)
    {
        int ap;
        NS_ABORT_MSG_IF(!(input >> ap) || ap < 0 || ap > 2, "Invalid MARL action array");
        assignment[i] = ap+1;
        NS_ABORT_MSG_IF(done && assignment[i] != initial_AP[i], "Terminal observation must not act");
    }
    NS_ABORT_MSG_IF(input >> extra, "Trailing MARL response data");
    m_lastAssignment = assignment;
    m_cycleHarmonicMeans.push_back(h); // No legacy QoE imputation or hypothetical post-switch quality.
    std::ofstream log(m_outputDir+"marl_wire.jsonl", std::ios::app);
    NS_ABORT_MSG_IF(!log, "Cannot write MARL raw log");
    log << out.str() << '\n'; log.flush();
    NS_ABORT_MSG_IF(!log, "Cannot flush MARL raw log");
    // KamedaAppServer::Ending applies this complete array once, except on the final cycle.
}
} // namespace ns3
