// SPDX-License-Identifier: GPL-2.0-only
// Algorithm-level oracle: compare call-local hints to the original algorithm.
// Real library coverage is provided by interference_band_state_regression.cc.
#include "ns3/nstime.h"
#include <cassert>
#include <iostream>
#include <map>
#include <random>
using namespace ns3;
struct Change { double power; unsigned event; };
struct State
{
    std::multimap<Time, Change> history{{NanoSeconds(0), {0.0, 0}}};
    double firstPower{0.0};
};
void Append(State& state, Time start, Time end, double power, unsigned id,
            bool rxing, bool he, bool reuseHints)
{
    auto& history = state.history;
    auto startNext = history.upper_bound(start);
    auto endNext = history.upper_bound(end);
    const auto beforeStart = std::prev(startNext)->second.power;
    const auto beforeEnd = std::prev(endNext)->second.power;
    if (!rxing)
    {
        state.firstPower = beforeStart;
        history.erase(std::next(history.begin()), startNext);
    }
    else if (he)
    {
        state.firstPower = beforeStart;
    }
    // end >= start: deletion affects neither next iterator. Inserting at start
    // leaves upper_bound(end) unchanged, including the zero-duration case.
    auto first = history.insert(reuseHints ? startNext : history.upper_bound(start),
                                {start, {beforeStart, id}});
    auto last = history.insert(reuseHints ? endNext : history.upper_bound(end),
                               {end, {beforeEnd, id}});
    for (auto it = first; it != last; ++it) { it->second.power += power; }
}
int main()
{
    for (unsigned seed : {1001, 1002})
    {
        std::mt19937 rng(seed);
        State baseline, candidate;
        int64_t now = 0;
        for (unsigned i = 1; i <= 10000; ++i)
        {
            now += rng() % 3;
            auto start = NanoSeconds(now);
            auto end = NanoSeconds(now + rng() % 20);
            double power = double(rng() % 10) / 8.0;
            bool rxing = rng() % 2;
            bool he = rng() % 2;
            Append(baseline, start, end, power, i, rxing, he, false);
            Append(candidate, start, end, power, i, rxing, he, true);
            assert(baseline.firstPower == candidate.firstPower);
            assert(baseline.history.size() == candidate.history.size());
            auto b = candidate.history.begin();
            for (const auto& a : baseline.history)
            {
                assert(a.first == b->first);
                assert(a.second.power == b->second.power);
                assert(a.second.event == b->second.event);
                ++b;
            }
            if (i % 97 == 0) { baseline = State{}; candidate = State{}; }
        }
        std::cout << "PASS: seed=" << seed << " 10000 appends; equal-time/zero-duration/prefix erasure/reset\n";
    }
}
