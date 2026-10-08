// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/rx-power-band-map.h"
#include "ns3/wifi-spectrum-phy-interface.h"
#include <cassert>
#include <iostream>
using namespace ns3;

int main()
{
    WifiSpectrumBandInfo a{{{1, 2}}, {{Hz_u{100}, Hz_u{200}}}};
    WifiSpectrumBandInfo b{{{3, 4}}, {{Hz_u{300}, Hz_u{400}}}};
    auto equivalent = a;
    equivalent.indices = {{10, 20}};
    WifiSpectrumBands inputs{b, a, equivalent, b};
    auto layout = RxPowerBandLayout::Build(inputs);
    RxPowerWattPerChannelBand compact(layout);
    std::map<WifiSpectrumBandInfo, Watt_u> reference;
    for (std::size_t i = 0; i < inputs.size(); ++i)
    {
        compact.SetInputPower(i, Watt_u{double(i + 1)});
        reference.insert({inputs[i], Watt_u{double(i + 1)}});
    }
    assert(compact.size() == reference.size());
    auto expected = reference.begin();
    for (const auto& [band, power] : compact)
    {
        assert(band.indices == expected->first.indices);
        assert(band.frequencies == expected->first.frequencies);
        assert(power == expected->second);
        ++expected;
    }
    auto copied = compact;
    assert(&copied.begin()->first == &compact.begin()->first);
    copied.begin()->second += Watt_u{10.0};
    assert(copied.begin()->second != compact.begin()->second);
    RxPowerWattPerChannelBand assigned;
    assigned = copied;
    assigned = assigned;
    assigned = std::move(copied);
    assert(assigned.size() == 2);
    compact.clear();
    layout.reset();
    inputs.clear();
    assert(assigned.find(a)->second == Watt_u{12.0});
    // Generic insert detaches the layout without invalidating copies/events.
    auto survivor = assigned;
    WifiSpectrumBandInfo c{{{5, 6}, {7, 8}},
                           {{Hz_u{500}, Hz_u{600}}, {Hz_u{700}, Hz_u{800}}}};
    assigned.try_emplace(c, Watt_u{3.0});
    assert(assigned.size() == 3 && survivor.size() == 2);
    assert(survivor.find(a)->second == Watt_u{12.0});
    assert(std::as_const(assigned).find(c) != assigned.cend());
    assert(survivor.find(c) == survivor.end());

    auto interface = CreateObject<WifiSpectrumPhyInterface>(FrequencyRange{});
    interface->SetBands(WifiSpectrumBands{a});
    auto firstLayout = interface->GetRxPowerLayout(false);
    assert(firstLayout == interface->GetRxPowerLayout(false));
    RxPowerWattPerChannelBand oldEvent(firstLayout);
    oldEvent.SetInputPower(0, Watt_u{7.0});
    interface->SetBands(WifiSpectrumBands{b});
    auto nextLayout = interface->GetRxPowerLayout(false);
    assert(firstLayout != nextLayout);
    assert(nextLayout->bands.front().indices == b.indices);
    interface->SetHeRuBands({});
    assert(nextLayout != interface->GetRxPowerLayout(false));
    interface->Dispose();
    interface = nullptr;
    firstLayout.reset();
    assert(oldEvent.find(a)->second == Watt_u{7.0});
    std::cout << "PASS: ordering, duplicates, shared keys, independent powers, copy/move, layout invalidation and lifetime\n";
}
