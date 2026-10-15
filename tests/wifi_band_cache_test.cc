// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/interference-helper.h"
#include "ns3/simulator.h"
#include <cassert>
#include <iostream>
using namespace ns3;
class InspectBandCache : public InterferenceHelper
{
  public:
    void Check(const RxPowerWattPerChannelBand& powers)
    {
        const auto& cached = GetCachedBandStates(powers);
        size_t i = 0;
        for (const auto& [band, power] : powers)
        {
            assert(cached.at(i++) == m_niChanges.find(band));
        }
        assert(cached.size() == powers.size());
        assert(m_cachedBandLayout == powers.GetLayout());
    }
    void EmptyCache() { assert(!m_cachedBandLayout && m_cachedBandStates.empty()); }
};
int main()
{
    auto h = CreateObject<InspectBandCache>();
    WifiSpectrumBandInfo a{{{1,2}},{{Hz_u{5000000000.0},Hz_u{5020000000.0}}}};
    WifiSpectrumBandInfo b{{{3,4}},{{Hz_u{5020000000.0},Hz_u{5040000000.0}}}};
    FrequencyRange range{MHz_u{4900},MHz_u{5900}};
    auto layout = RxPowerBandLayout::Build({b,a,a});
    RxPowerWattPerChannelBand powers(layout);
    powers.SetInputPower(0,Watt_u{2}); powers.SetInputPower(1,Watt_u{1});
    h->AddBand(a); h->AddBand(b); h->Check(powers); h->Check(powers);
    // Separate helpers must not share iterators even for the same layout.
    auto other = CreateObject<InspectBandCache>();
    other->AddBand(a); other->AddBand(b); other->Check(powers);
    h->RemoveBand(a); h->EmptyCache(); h->AddBand(a); h->EmptyCache(); h->Check(powers);
    // Different layout identity with equal keys and a subset alternating.
    RxPowerWattPerChannelBand equal(RxPowerBandLayout::Build({a,b}));
    RxPowerWattPerChannelBand subset(RxPowerBandLayout::Build({a}));
    h->Check(equal); h->Check(subset); h->Check(powers);
    h->UpdateBands({b},range); h->EmptyCache();
    h->UpdateBands({a,b},range); h->Check(powers);
    // Same shared layout across real appends and zero-duration duplicate times.
    for (int i=0;i<3;++i)
    {
        auto copy=powers;
        h->Add(nullptr,Seconds(0),copy,range);
        h->Check(powers);
    }
    h->UpdateBands({},range); h->EmptyCache();
    RxPowerWattPerChannelBand empty; h->Check(empty);
    h->AddBand(a); h->Check(subset);
    h->Dispose(); h->EmptyCache(); other->Dispose(); Simulator::Destroy();
    std::cout << "cache lifecycle PASS\n";
}
