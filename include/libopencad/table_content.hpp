// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "table.hpp"
#include "table_geometry.hpp"

namespace libopencad {
// Numeric flags follow acadrust::entities::table, including unknown flag bits.
struct TableContentFormat {
    std::int32_t overrides = 0, properties = 0, data_type = 0, unit_type = 0, alignment = 0;
    // Document APIs resolve names. Low-level setters require a nonzero style handle for named styles.
    std::string value_format, text_style_name;
    std::uint64_t text_style = 0;
    double rotation = 0, scale = 1, text_height = .18;
    Color color = Color::by_block();
    std::vector<DxfRawField> extra_fields;
};
struct TableGridFormat {
    // DXF GridLineType (1 top, 2 inside horizontal, 4 bottom, 8 left, 16 inside vertical, 32 right).
    std::uint32_t edge = 0, overrides = 0;
    std::int32_t type = 1;
    Color color = Color::by_block();
    std::int16_t line_weight = -1;
    std::uint64_t line_type = 0;
    bool invisible = false;
    double double_spacing = 0;
    std::vector<DxfRawField> extra_fields;
};
struct TableFormat {
    std::int32_t type = 1, overrides = 0;
    std::uint32_t properties = 0, layout = 1;
    Color background = Color::by_block();
    TableContentFormat content;
    std::int16_t margin_flags = 0;
    // Top, left, bottom, right, horizontal spacing, vertical spacing.
    std::array<double, 6> margins{.06, .06, .06, .06, 0, 0};
    std::vector<TableGridFormat> borders;
    std::vector<DxfRawField> extra_fields, margin_extra;
};
struct TableCustomData {
    std::string name;
    TableValue value;
};
struct TableContentAttribute {
    std::uint64_t definition = 0;
    std::string text;
    std::int32_t index = 0;
};
struct TableContentItem {
    // 0 unknown, 1 value, 2 FIELD, 4 block.
    std::int32_t type = 0;
    TableValue value;
    std::uint64_t reference = 0;
    std::vector<TableContentAttribute> attributes;
    TableContentFormat format;
    std::vector<DxfRawField> extra_fields, formatted_extra;
};
struct ModernTableCell {
    std::int32_t state = 0, custom_data = 0, style_id = 0;
    std::string tooltip;
    std::vector<TableCustomData> custom;
    std::vector<DxfRawField> custom_extra;
    bool linked = false;
    std::uint64_t data_link = 0;
    std::int32_t link_rows = 0, link_columns = 0, link_unknown = 0;
    std::vector<TableContentItem> contents;
    std::optional<TableFormat> format;
    // Document-level queries restore this cache from TABLEGEOMETRY.
    // A missing value preserves an existing cache; an empty cell explicitly clears it.
    std::optional<TableGeometryCell> geometry;
    // Imported embedded geometry fields are retained as raw data.
    std::vector<DxfRawField> linked_extra, formatted_extra, extra_fields;
};
struct ModernTableColumn {
    std::string name;
    std::int32_t custom_data = 0, style_id = 0;
    double width = 2.5;
    std::vector<TableCustomData> custom;
    std::vector<DxfRawField> custom_extra;
    std::optional<TableFormat> format;
    std::vector<DxfRawField> linked_extra, formatted_extra, extra_fields;
};
struct ModernTableRow {
    std::int32_t custom_data = 0, style_id = 0;
    double height = .25;
    std::vector<ModernTableCell> cells;
    std::vector<TableCustomData> custom;
    std::vector<DxfRawField> custom_extra;
    std::optional<TableFormat> format;
    std::vector<DxfRawField> linked_extra, formatted_extra, extra_fields;
};
struct TableContentDefinition {
    std::string name, description;
    std::vector<ModernTableColumn> columns;
    std::vector<ModernTableRow> rows;
    std::optional<TableFormat> format;
    std::vector<TableRange> merged_ranges;
    std::vector<std::uint64_t> fields;
    std::uint64_t style = 0;
    std::vector<DxfRawField> data_extra, linked_extra, formatted_extra, extra_fields;
};
} // namespace libopencad
