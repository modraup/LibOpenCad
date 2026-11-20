// SPDX-License-Identifier: MPL-2.0
// DXF representation from acadrust src/types/line_weight.rs.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace libopencad {

class LineWeight {
public:
    constexpr LineWeight() = default;
    static constexpr LineWeight by_layer() { return {}; }
    static constexpr LineWeight by_block() { return LineWeight(-2); }
    static constexpr LineWeight default_weight() { return LineWeight(-3); }
    static constexpr LineWeight from_value(std::int16_t value) { return LineWeight(value); }

    static constexpr LineWeight from_dwg_index(std::uint8_t index) {
        if (index < indexed_values_.size()) return LineWeight(indexed_values_[index]);
        if (index == 28 || index == 29) return by_layer();
        if (index == 30) return by_block();
        return default_weight();
    }
    constexpr std::uint8_t to_dwg_index() const {
        if (*this == by_layer()) return 29;
        if (*this == by_block()) return 30;
        for (std::uint8_t i = 0; i < indexed_values_.size(); ++i)
            if (indexed_values_[i] == value_) return i;
        return 31;
    }
    constexpr std::int16_t value() const { return value_; }
    constexpr std::optional<double> millimeters() const {
        if (value_ == -1 || value_ == -2 || value_ == -3) return std::nullopt;
        return value_ / 100.0;
    }
    constexpr bool operator==(LineWeight other) const { return value_ == other.value_; }
    constexpr bool operator!=(LineWeight other) const { return !(*this == other); }
private:
    static constexpr std::array<std::int16_t, 24> indexed_values_ = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50,
                                                       53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};
    constexpr explicit LineWeight(std::int16_t value) : value_(value) {}
    std::int16_t value_ = -1;
};

} // namespace libopencad
