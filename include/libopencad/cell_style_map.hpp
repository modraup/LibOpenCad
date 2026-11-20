// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "table_content.hpp"
namespace libopencad {
struct NamedTableCellStyle {
    std::int32_t id = 0, style_type = 0;
    std::string name;
    std::int16_t data_flags = 0;
    // TABLESTYLE flags differ from Table cell overrides. Grid visibility follows
    // TableCellBorder::is_invisible, which is inverted when projected to legacy rows.
    TableFormat format;
    std::vector<DxfRawField> extra_fields;
    NamedTableCellStyle() {
        format.type = 0;
        format.layout = 0;
        format.background = Color::by_layer();
        format.margins.fill(0);
        format.content.data_type = 512;
        format.content.alignment = 5;
    }
};
struct CellStyleMapDefinition {
    std::vector<NamedTableCellStyle> styles;
    std::vector<DxfRawField> extra_fields;
};
} // namespace libopencad
