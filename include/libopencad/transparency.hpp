// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
namespace libopencad {
class Transparency {
  public:
    enum class Kind { ByLayer, ByBlock, Explicit };
    constexpr Transparency() = default;
    static constexpr Transparency by_layer() { return {}; }
    static constexpr Transparency by_block() { return {Kind::ByBlock, 0}; }
    static constexpr Transparency explicit_alpha(std::uint8_t alpha) { return {Kind::Explicit, alpha}; }
    static Transparency from_percent(double value) {
        return explicit_alpha(
            std::isnan(value) ? 0 : static_cast<std::uint8_t>(std::ceil(std::clamp(value, 0., 1.) * 255)));
    }
    static constexpr Transparency from_alpha_value(std::uint32_t value) {
        return value >> 24 == 1                       ? by_block()
               : value >> 24 == 2 || value >> 24 == 3 ? explicit_alpha(255 - (value & 255))
                                                      : by_layer();
    }
    static std::optional<Transparency> from_layer_alpha_value(std::uint32_t value) {
        if (value == 0)
            return explicit_alpha(0);
        if (value >> 24 >= 1 && value >> 24 <= 3)
            return from_alpha_value(value);
        return std::nullopt;
    }
    constexpr Kind kind() const { return kind_; }
    constexpr std::uint8_t alpha() const { return alpha_; }
    constexpr std::optional<std::uint8_t> explicit_alpha() const {
        return kind_ == Kind::Explicit ? std::optional<std::uint8_t>{alpha_} : std::nullopt;
    }
    double as_percent() const { return alpha_ / 255.; }
    constexpr std::uint32_t to_alpha_value() const {
        return kind_ == Kind::ByLayer ? 0 : kind_ == Kind::ByBlock ? 0x1000000 : 0x2000000 | (255 - alpha_);
    }
    constexpr std::uint32_t to_dxf_value() const { return to_alpha_value(); }
    constexpr bool operator==(Transparency other) const {
        return kind_ == other.kind_ && alpha_ == other.alpha_;
    }
    constexpr bool operator!=(Transparency other) const { return !(*this == other); }

  private:
    constexpr Transparency(Kind kind, std::uint8_t alpha) : kind_(kind), alpha_(alpha) {}
    Kind kind_ = Kind::ByLayer;
    std::uint8_t alpha_ = 0;
};
} // namespace libopencad
