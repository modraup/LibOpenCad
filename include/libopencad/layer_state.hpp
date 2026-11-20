// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/layer_state.rs (native ACAD_LAYERSTATES XRecords).
#pragma once

#include "color.hpp"
#include "line_weight.hpp"
#include "transparency.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace libopencad {

struct LayerStateMask {
    static constexpr std::uint32_t ON = 0x0001;
    static constexpr std::uint32_t FROZEN = 0x0002;
    static constexpr std::uint32_t LOCKED = 0x0004;
    static constexpr std::uint32_t PLOT = 0x0008;
    static constexpr std::uint32_t NEW_VIEWPORT = 0x0010;
    static constexpr std::uint32_t COLOR = 0x0020;
    static constexpr std::uint32_t LINE_TYPE = 0x0040;
    static constexpr std::uint32_t LINE_WEIGHT = 0x0080;
    static constexpr std::uint32_t PLOT_STYLE = 0x0100;
    static constexpr std::uint32_t CURRENT_VIEWPORT = 0x0200;
    static constexpr std::uint32_t TRANSPARENCY = 0x0400;
    static constexpr std::uint32_t LAST_RESTORED = 0x10000;
    static constexpr std::uint32_t SUPPORTED =
        ON | FROZEN | LOCKED | PLOT | NEW_VIEWPORT | COLOR | LINE_TYPE | LINE_WEIGHT | PLOT_STYLE |
        TRANSPARENCY;
    static constexpr std::uint32_t ALL = 0x07ff;

    std::uint32_t bits = 0;

    static constexpr LayerStateMask from_bits(std::uint32_t value) noexcept { return {value}; }
    constexpr bool contains(std::uint32_t flag) const noexcept { return (bits & flag) == flag; }
    constexpr bool operator==(LayerStateMask other) const noexcept { return bits == other.bits; }
    constexpr bool operator!=(LayerStateMask other) const noexcept { return bits != other.bits; }
};

struct LayerStateLayer {
    std::uint64_t layer_handle = 0;
    std::string layer_name;
    bool off = false;
    bool frozen = false;
    bool locked = false;
    bool plottable = true;
    bool new_viewport_frozen = false;
    Color color = Color::from_index(7);
    std::optional<std::string> color_name;
    std::optional<std::string> book_name;
    std::string line_type;
    LineWeight line_weight = LineWeight::default_weight();
    std::string plot_style;
    std::optional<Transparency> transparency;
    bool operator==(const LayerStateLayer &other) const {
        return layer_handle == other.layer_handle && layer_name == other.layer_name &&
               off == other.off && frozen == other.frozen && locked == other.locked &&
               plottable == other.plottable && new_viewport_frozen == other.new_viewport_frozen &&
               color == other.color && color_name == other.color_name && book_name == other.book_name &&
               line_type == other.line_type && line_weight == other.line_weight &&
               plot_style == other.plot_style && transparency == other.transparency;
    }
    bool operator!=(const LayerStateLayer &other) const { return !(*this == other); }
};

struct LayerState {
    std::string name;
    std::string description;
    LayerStateMask mask = LayerStateMask::from_bits(LayerStateMask::SUPPORTED);
    bool hidden = false;
    std::string current_layer;
    std::vector<LayerStateLayer> layers;
    bool operator==(const LayerState &other) const {
        return name == other.name && description == other.description && mask == other.mask &&
               hidden == other.hidden && current_layer == other.current_layer && layers == other.layers;
    }
    bool operator!=(const LayerState &other) const { return !(*this == other); }
};

} // namespace libopencad
