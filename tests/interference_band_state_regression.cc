// SPDX-License-Identifier: GPL-2.0-only
// Compile against both snapshots; preserve equal-time ordering and band lifecycle.
#include "ns3/interference-helper.h"
#include "ns3/simulator.h"
#include <cassert>
#include <iostream>
using namespace ns3;
class InspectHelper : public InterferenceHelper
{
  public:
    void Dump() const
    {
        for (const auto& [band, state] : m_niChanges)
        {
#ifdef NS3_UNIFIED_BAND_STATE
            const auto& changes = state.changes;
#else
            const auto& changes = state;
#endif
            std::cout << "band=" << band << '\n';
            for (const auto& [time, change] : changes)
            {
                std::cout << time.GetNanoSeconds() << ':' << change.GetPower();
                if (auto event = change.GetEvent())
                {
                    std::cout << ':' << event->GetStartTime().GetNanoSeconds()
                              << ':' << event->GetRxPower(band);
                }
                std::cout << '\n';
            }
        }
    }
    void CheckFirst(const WifiSpectrumBandInfo& band, Watt_u expected) const
    {
#ifdef NS3_UNIFIED_BAND_STATE
        assert(m_niChanges.at(band).firstPower == expected);
#endif
    }
};
int main()
{
    auto helper = CreateObject<InspectHelper>();
    WifiSpectrumBandInfo band{{{1, 2}}, {{Hz_u{5000000000.0}, Hz_u{5020000000.0}}}};
    FrequencyRange range{MHz_u{4900}, MHz_u{5900}};
    helper->AddBand(band);
    helper->CheckFirst(band, Watt_u{0});
    auto add = [&](double power, bool he) {
        RxPowerWattPerChannelBand powers;
        powers.insert({band, Watt_u{power}});
        helper->Add(nullptr, Seconds(6) - Simulator::Now(), powers, range, he);
        helper->Dump();
        std::cout << "energy=" << helper->GetEnergyDuration(Watt_u{0.5}, band).GetNanoSeconds() << '\n';
    };
    Simulator::Schedule(Seconds(1), [&] { add(1, false); helper->CheckFirst(band, Watt_u{0}); });
    Simulator::Schedule(Seconds(2), [&] {
        add(2, false);
        helper->CheckFirst(band, Watt_u{1});
        helper->NotifyRxStart(range);
    });
    Simulator::Schedule(Seconds(3), [&] { add(4, true); helper->CheckFirst(band, Watt_u{3}); });
    Simulator::Schedule(Seconds(3), [&] { add(8, false); helper->CheckFirst(band, Watt_u{3}); });
    Simulator::Schedule(Seconds(4), [&] {
        helper->NotifyRxEnd(Simulator::Now(), range);
        helper->CheckFirst(band, Watt_u{7});
        helper->Dump();
    });
    Simulator::Schedule(Seconds(5), [&] {
        helper->UpdateBands({}, range);
        assert(!helper->HasBands());
        helper->UpdateBands({band}, range);
        helper->CheckFirst(band, Watt_u{0});
        helper->Dump();
        // Zero-duration signals exercise coincident start/end insertion hints.
        // Repeat at the same time, both with prefix deletion and while receiving.
        for (unsigned i = 0; i < 3; ++i)
        {
            if (i == 2) { helper->NotifyRxStart(range); }
            RxPowerWattPerChannelBand zeroPower;
            zeroPower.insert({band, Watt_u{16}});
            helper->Add(nullptr, Seconds(0), zeroPower, range);
            helper->CheckFirst(band, Watt_u{0});
            assert(helper->GetEnergyDuration(Watt_u{0.5}, band).IsZero());
            helper->Dump();
        }
        helper->NotifyRxEnd(Simulator::Now(), range);
        // Preserve the original frame-capture lookup: it steps back from the
        // final end marker to the preceding equal-time start marker.
        helper->CheckFirst(band, Watt_u{16});
    });
    Simulator::Run();
    std::cout << "events=" << Simulator::GetEventCount() << '\n';
    helper->Dispose();
    assert(!helper->HasBands());
    Simulator::Destroy();
}
