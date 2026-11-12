// SPDX-License-Identifier: MPL-2.0
// DXF representation from acadrust src/types/line_weight.rs.
#pragma once

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

    constexpr std::int16_t value() const { return value_; }
    constexpr std::optional<double> millimeters() const {
        if (value_ == -1 || value_ == -2 || value_ == -3) return std::nullopt;
        return value_ / 100.0;
    }
    constexpr bool operator==(LineWeight other) const { return value_ == other.value_; }
    constexpr bool operator!=(LineWeight other) const { return !(*this == other); }
private:
    constexpr explicit LineWeight(std::int16_t value) : value_(value) {}
    std::int16_t value_ = -1;
};

} // namespace libopencad
