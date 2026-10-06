// SPDX-License-Identifier: GPL-2.0-only
// Check the actual band key, including comparator-equivalent nonidentical keys.
#include "ns3/phy-entity.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace ns3;
    RxPowerWattPerChannelBand before;
    RxPowerWattPerChannelBand after;
    WifiSpectrumBandInfo band;
    band.indices.emplace_back(1, 2);
    band.frequencies.emplace_back(Hz_u{100}, Hz_u{200});
    auto insert = [&](const WifiSpectrumBandInfo& key, Watt_u power) {
        before.insert({key, power});
        after.try_emplace(key, power);
    };
    insert(band, Watt_u{1.0});
    insert(band, Watt_u{2.0}); // must not replace the first value
    band.indices.front() = {3, 4};
    insert(band, Watt_u{3.0}); // equivalent key must preserve original indices too
    band.frequencies.emplace_back(Hz_u{300}, Hz_u{400});
    band.indices.emplace_back(5, 6);
    insert(band, Watt_u{4.0}); // distinct multisegment band
    assert(before.size() == 2 && before.size() == after.size());
    auto right = after.begin();
    for (const auto& [key, power] : before)
    {
        assert(key.indices == right->first.indices);
        assert(key.frequencies == right->first.frequencies);
        assert(power == right->second);
        ++right;
    }
    std::cout << "PASS: unique/duplicate/equivalent/multisegment band keys\n";
}
