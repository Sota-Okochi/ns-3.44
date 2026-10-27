#ifndef ASSIGNMENT_COMPUTE_TIMER_H
#define ASSIGNMENT_COMPUTE_TIMER_H
#include <chrono>

namespace ns3
{
// Accumulate only local computation segments. Pauses never subtract noisy wall times.
class AssignmentComputeTimer
{
  public:
    explicit AssignmentComputeTimer(double& milliseconds, bool enabled = true) : m_ms(milliseconds)
    { if (enabled) Resume(); }
    ~AssignmentComputeTimer() { Pause(); }
    AssignmentComputeTimer(const AssignmentComputeTimer&) = delete;
    AssignmentComputeTimer& operator=(const AssignmentComputeTimer&) = delete;
    void Pause()
    {
        if (m_running)
        {
            m_ms += std::chrono::duration<double, std::milli>(Clock::now() - m_start).count();
            m_running = false;
        }
    }
    void Resume()
    {
        if (!m_running) { m_start = Clock::now(); m_running = true; }
    }
  private:
    using Clock = std::chrono::steady_clock;
    double& m_ms;
    Clock::time_point m_start;
    bool m_running = false;
};
}
#endif
