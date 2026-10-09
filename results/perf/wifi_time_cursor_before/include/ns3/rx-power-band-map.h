// SPDX-License-Identifier: GPL-2.0-only
#ifndef RX_POWER_BAND_MAP_H
#define RX_POWER_BAND_MAP_H

#include "wifi-phy-common.h"
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace ns3
{
/** Immutable, sorted band identities shared by simultaneously live receptions.
 * Input slots preserve original insertion order and first-insertion-wins semantics.
 */
struct RxPowerBandLayout
{
    WifiSpectrumBands bands;
    std::vector<std::pair<std::size_t, bool>> inputSlots;

    static std::shared_ptr<const RxPowerBandLayout> Build(const WifiSpectrumBands& input)
    {
        auto layout = std::make_shared<RxPowerBandLayout>();
        std::map<WifiSpectrumBandInfo, std::size_t> unique;
        for (const auto& band : input)
        {
            unique.try_emplace(band, 0);
        }
        for (auto& [band, index] : unique)
        {
            index = layout->bands.size();
            layout->bands.push_back(band);
        }
        std::vector<bool> seen(unique.size(), false);
        for (const auto& band : input)
        {
            const auto index = unique.find(band)->second;
            layout->inputSlots.emplace_back(index, !seen[index]);
            seen[index] = true;
        }
        return layout;
    }
};

/** Compact received powers with shared immutable keys.
 * Iterators expose a const band reference and an event-local mutable power.
 * This is the map-like subset used by Wi-Fi, not a full std::map replacement.
 * Generic insertions rebuild a private layout; the Spectrum PHY hot path uses
 * a cached layout and SetInputPower, allocating only the contiguous entries.
 */
class RxPowerWattPerChannelBand
{
  public:
    using value_type = std::pair<const WifiSpectrumBandInfo&, Watt_u>;
    using Storage = std::vector<value_type>;
    using iterator = Storage::iterator;
    using const_iterator = Storage::const_iterator;

    RxPowerWattPerChannelBand() = default;
    explicit RxPowerWattPerChannelBand(std::shared_ptr<const RxPowerBandLayout> layout)
        : m_layout(std::move(layout))
    {
        m_values.reserve(m_layout->bands.size());
        for (const auto& band : m_layout->bands)
        {
            m_values.emplace_back(band, Watt_u{0.0});
        }
    }
    RxPowerWattPerChannelBand(std::initializer_list<std::pair<WifiSpectrumBandInfo, Watt_u>> values)
    {
        for (const auto& [band, power] : values) { try_emplace(band, power); }
    }
    RxPowerWattPerChannelBand(const RxPowerWattPerChannelBand&) = default;
    RxPowerWattPerChannelBand(RxPowerWattPerChannelBand&&) noexcept = default;
    RxPowerWattPerChannelBand& operator=(RxPowerWattPerChannelBand other) noexcept
    {
        m_layout.swap(other.m_layout);
        m_values.swap(other.m_values);
        return *this;
    }
    auto begin() { return m_values.begin(); }
    auto end() { return m_values.end(); }
    auto begin() const { return m_values.begin(); }
    auto end() const { return m_values.end(); }
    auto cbegin() const { return m_values.cbegin(); }
    auto cend() const { return m_values.cend(); }
    auto size() const { return m_values.size(); }
    bool empty() const { return m_values.empty(); }
    void clear() { m_values.clear(); m_layout.reset(); }
    iterator find(const WifiSpectrumBandInfo& band)
    {
        auto it = std::lower_bound(begin(), end(), band,
            [](const auto& entry, const auto& key) { return entry.first < key; });
        return it != end() && !(band < it->first) ? it : end();
    }
    const_iterator find(const WifiSpectrumBandInfo& band) const
    {
        auto it = std::lower_bound(begin(), end(), band,
            [](const auto& entry, const auto& key) { return entry.first < key; });
        return it != end() && !(band < it->first) ? it : end();
    }
    std::pair<iterator, bool> try_emplace(const WifiSpectrumBandInfo& band, Watt_u power)
    {
        auto found = find(band);
        if (found != end()) { return {found, false}; }
        WifiSpectrumBands bands;
        for (const auto& entry : m_values) { bands.push_back(entry.first); }
        bands.push_back(band);
        RxPowerWattPerChannelBand replacement(RxPowerBandLayout::Build(bands));
        for (const auto& entry : m_values) { replacement.find(entry.first)->second = entry.second; }
        replacement.find(band)->second = power;
        *this = std::move(replacement);
        return {find(band), true};
    }
    std::pair<iterator, bool> insert(const std::pair<WifiSpectrumBandInfo, Watt_u>& value)
    {
        return try_emplace(value.first, value.second);
    }
    Watt_u& operator[](const WifiSpectrumBandInfo& band)
    {
        return try_emplace(band, Watt_u{0.0}).first->second;
    }
    /** Fill in the original regular/RU traversal order, without searching/copying keys. */
    void SetInputPower(std::size_t inputIndex, Watt_u power)
    {
        const auto [slot, first] = m_layout->inputSlots.at(inputIndex);
        if (first) { m_values[slot].second = power; }
    }

  private:
    std::shared_ptr<const RxPowerBandLayout> m_layout;
    Storage m_values;
};
} // namespace ns3
#endif
