// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/types/color.rs.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace libopencad {

class Color {
public:
    enum class Kind { ByLayer, ByBlock, None, Index, Rgb };
    using Rgb = std::array<std::uint8_t, 3>;

    constexpr Color() = default; // ByLayer
    static constexpr Color by_layer() { return {}; }
    static constexpr Color by_block() { return {Kind::ByBlock, 0}; }
    static constexpr Color none() { return {Kind::None, 257}; }
    static Color from_index(std::int16_t index);
    static constexpr Color from_rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return {Kind::Rgb, (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b};
    }
    static constexpr Color from_true_color_value(std::uint32_t packed) {
        return {Kind::Rgb, packed & 0xffffffu};
    }

    constexpr Kind kind() const { return kind_; }
    constexpr std::optional<int> index() const {
        if (kind_ == Kind::Rgb) return std::nullopt;
        return static_cast<int>(value_);
    }
    constexpr std::optional<std::uint32_t> to_true_color_value() const {
        if (kind_ == Kind::Rgb) return value_;
        return std::nullopt;
    }
    std::optional<Rgb> rgb() const; // Resolves ordinary ACI colors through the palette.
    int approximate_index() const;
    constexpr bool operator==(Color other) const { return kind_ == other.kind_ && value_ == other.value_; }
    constexpr bool operator!=(Color other) const { return !(*this == other); }
private:
    constexpr Color(Kind kind, std::uint32_t value) : kind_(kind), value_(value) {}
    Kind kind_ = Kind::ByLayer;
    std::uint32_t value_ = 256;
};

} // namespace libopencad
