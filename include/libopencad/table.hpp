// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "color.hpp"
#include "dxf_raw_field.hpp"
#include "vector.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
namespace libopencad {
struct TableValue {
    // Raw ACVALUE type codes: 1 integer, 2 real, 4/512 string, 8 binary/date,
    // 16/32 point, 64 handle. Unknown codes are retained.
    std::int32_t type = 0, unit = 0, flags = 0, data_size = 0;
    std::string text, format, formatted;
    double number = 0;
    Point3 point{};
    std::uint64_t handle = 0;
    std::vector<std::uint8_t> binary;
    std::vector<DxfRawField> extra_fields;
};
struct TableBorder {
    Color color = Color::by_block();
    std::int16_t line_weight = -2;
    bool invisible = false;
};
struct TableCellStyle {
    // Legacy property mask: alignment/fill/background/content/style/height,
    // followed by four edge color, weight and visibility bits (top/right/bottom/left).
    std::int32_t overrides = 0;
    std::uint32_t property_flags = 0; // Upstream CellStylePropertyFlags; translated to legacy bits.
    std::string text_style;
    double text_height = .18;
    std::int16_t alignment = 0;
    bool fill_enabled = false;
    Color content_color = Color::by_block(), background_color = Color::by_block();
    std::array<TableBorder, 4> borders;
    TableCellStyle() {
        for (auto &border : borders)
            border.line_weight = -1;
    }
};
struct TableAttribute {
    std::uint64_t definition = 0;
    std::string text;
};
struct TableCell {
    std::int16_t type = 1, edge_flags = 0, merged = 0, merge_width = 1, merge_height = 1, virtual_edge = 0;
    std::int32_t flags = 0, state = 0;
    bool auto_fit = false;
    double rotation = 0, block_scale = 1;
    std::optional<TableValue> value;
    std::uint64_t field = 0, block = 0;
    std::vector<TableAttribute> attributes;
    std::optional<TableCellStyle> style;
    std::vector<DxfRawField> extra_fields;
};
struct TableRange {
    std::size_t top = 0, left = 0, bottom = 0, right = 0;
};
struct TableOverrides {
    std::int32_t flags = 0, border_colors = 0, border_weights = 0, border_visibility = 0;
    bool title_suppressed = false, header_suppressed = false;
    std::int16_t flow_direction = 0;
    double horizontal_margin = 0, vertical_margin = 0;
    // Compact values in set-bit order, matching the upstream legacy definition.
    std::vector<Color> row_colors, row_fill_colors, colors;
    std::vector<bool> row_fill_none, visible;
    std::vector<std::int16_t> row_alignments, weights;
    std::vector<std::string> text_styles;
    std::vector<double> row_text_heights;
};
struct TableDefinition {
    Point3 insertion{}, normal{0, 0, 1}, horizontal_direction{1, 0, 0};
    std::string block_name;
    std::uint64_t style = 0, block_record = 0;
    std::int16_t data_version = 0;
    std::int32_t value_flags = 0;
    std::vector<double> row_heights, column_widths;
    std::vector<std::vector<TableCell>> cells;
    std::vector<TableRange> merged_ranges;
    TableOverrides overrides;
    std::vector<DxfRawField> extra_fields;
};
struct TableRowStyle {
    std::string text_style = "Standard", format;
    double text_height = .18;
    std::int16_t alignment = 5;
    Color text_color = Color::by_block(), fill_color = Color::from_index(7);
    bool fill_enabled = false;
    std::int32_t data_type = 512, unit_type = 0;
    // Top, horizontal-inside, bottom, left, vertical-inside, right.
    std::array<TableBorder, 6> borders;
    std::vector<DxfRawField> extra_fields;
};
struct TableStyleDefinition {
    std::string description;
    std::int16_t version = 0, flow_direction = 0, flags = 0;
    double horizontal_margin = .06, vertical_margin = .06;
    bool title_suppressed = false, header_suppressed = false;
    TableRowStyle data, title, header;
    std::vector<DxfRawField> extra_fields;
    TableStyleDefinition() {
        data.alignment = 2;
        title.text_height = header.text_height = .25;
    }
};
} // namespace libopencad
