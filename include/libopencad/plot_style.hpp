// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/plot_style.rs (CTB/STB TTBVER1 zlib codec).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace libopencad {

inline const std::array<float, 27> kPlotStyleLineweights = {
    0.00f, 0.05f, 0.09f, 0.10f, 0.13f, 0.15f, 0.18f, 0.20f, 0.25f, 0.30f,
    0.35f, 0.40f, 0.45f, 0.50f, 0.53f, 0.60f, 0.65f, 0.70f, 0.80f, 0.90f,
    1.00f, 1.06f, 1.20f, 1.40f, 1.58f, 2.00f, 2.11f};

struct PlotStyleEntry {
    std::string name;
    std::string localized_name;
    std::string description;
    std::optional<std::array<std::uint8_t, 3>> color;
    std::uint8_t lineweight = 0;
    std::uint8_t screening = 100;
    std::uint8_t color_policy = 1;
    std::uint16_t physical_pen_number = 0;
    std::uint16_t virtual_pen_number = 0;
    float linepattern_size = 0.5f;
    std::uint8_t linetype = 31;
    bool adaptive_linetype = true;
    std::uint8_t fill_style = 73;
    std::uint8_t end_style = 4;
    std::uint8_t join_style = 5;
};

struct PlotStyleTable {
    std::string name;
    bool is_stb = false;
    std::string description;
    float scale_factor = 1.0f;
    bool apply_factor = false;
    std::uint8_t custom_lineweight_display_units = 0;
    std::vector<float> lineweights;
    std::vector<PlotStyleEntry> aci_entries;
    std::unordered_map<std::string, PlotStyleEntry> named_entries;
    std::vector<std::string> load_warnings;

    static PlotStyleTable identity(std::string name);
    static PlotStyleTable from_bytes(std::string name, const std::uint8_t *data, std::size_t size);
    static PlotStyleTable from_bytes(std::string name, const std::vector<std::uint8_t> &raw) {
        return from_bytes(std::move(name), raw.data(), raw.size());
    }

    std::vector<std::uint8_t> to_bytes() const;
    std::string to_text() const;
    std::optional<std::array<float, 3>> resolve_color(std::uint8_t aci) const;
    std::optional<float> resolve_lineweight(std::uint8_t aci) const;
    float resolve_screening(std::uint8_t aci) const;
};

std::pair<std::string, std::optional<std::string>> decode_plot_style_text(std::vector<std::uint8_t> bytes);

} // namespace libopencad
