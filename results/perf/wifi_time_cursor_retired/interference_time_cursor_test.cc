// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/interference-helper.h"
#include <cassert>
#include <iostream>
#include <random>
using namespace ns3;
class Access : public InterferenceHelper
{
  public:
    using State = BandInterferenceState;
    using History = NiChanges;
    using Change = NiChange;
};
int main()
{
    Access::State state;
    Access::History reference;
    std::mt19937 rng(1001);
    int64_t now = 0;
    auto check = [&](Time query) {
        auto actual = state.UpperBound(query, NanoSeconds(now));
        auto expected = reference.upper_bound(query);
        assert((actual == state.changes.end()) == (expected == reference.end()));
        if (expected != reference.end())
        {
            assert(actual->first == expected->first);
            assert(actual->second.GetPower() == expected->second.GetPower());
        }
    };
    for (unsigned i = 0; i < 30000; ++i)
    {
        now += rng() % 3;
        switch (rng() % 6)
        {
        case 0:
        case 1:
        case 2: {
            // Includes equal-time, future, and backdated insertions; unique values
            // distinguish ordering of nodes with equal timestamps.
            auto time = NanoSeconds(now + int(rng() % 81) - 40);
            Access::Change change(Watt_u{double(i)}, nullptr);
            state.Insert(time, change, NanoSeconds(now));
            reference.insert(reference.upper_bound(time), {time, change});
            break;
        }
        case 3: {
            auto cutoff = NanoSeconds(now + int(rng() % 31) - 15);
            auto end = state.changes.upper_bound(cutoff);
            state.ErasePast(state.changes.begin(), end, cutoff);
            reference.erase(reference.begin(), reference.upper_bound(cutoff));
            break;
        }
        case 4:
            if (rng() % 20 == 0) { state.ClearHistory(); reference.clear(); }
            break;
        case 5: {
            Access::State copy = state;
            assert(!copy.cursorValid);
            state = copy;
            assert(!state.cursorValid);
            break;
        }
        }
        check(NanoSeconds(now));
        check(NanoSeconds(now)); // repeated same-time query
        check(NanoSeconds(now - 100));
        check(NanoSeconds(now + 100));
        assert(state.changes.size() == reference.size());
        auto b = reference.begin();
        for (const auto& a : state.changes)
        {
            assert(a.first == b->first && a.second.GetPower() == b->second.GetPower());
            ++b;
        }
    }
    // Force a forward walk exceeding the fast-path bound and its tree fallback.
    state.ClearHistory(); reference.clear(); now = 0;
    for (int i = 0; i < 40; ++i)
    {
        auto t = NanoSeconds(i);
        Access::Change change(Watt_u{double(i)}, nullptr);
        state.Insert(t, change, NanoSeconds(now));
        reference.insert(reference.upper_bound(t), {t, change});
    }
    now = 30; check(NanoSeconds(now));
    now = 5; check(NanoSeconds(now)); // decreasing query uses original tree
    std::cout << "PASS: 30000 operations, exact history/upper_bound, equal-time order, erase, clear, copy, fallback\n";
}
