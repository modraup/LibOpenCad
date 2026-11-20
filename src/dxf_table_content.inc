// SPDX-License-Identifier: MPL-2.0
// Port of acadrust table_content.rs and modern table DXF writers.
namespace {
using MTFields = std::vector<DxfRawField>;
struct MTReader {
    MLFields input;
    std::size_t at = 0;
    explicit MTReader(MTFields values) : input(std::move(values)) {}
    bool done() const { return at == input.values.size(); }
    bool is(int code, std::string_view value) const {
        return !done() && !input.hidden[at] && input.values[at].code == code &&
               input.values[at].value == value;
    }
    MTFields body(std::string_view end, int code = 309) {
        const auto start = at;
        while (!done() && !is(code, end))
            ++at;
        if (done())
            throw std::invalid_argument("Unterminated modern table scope: " + std::string(end));
        MTFields result(input.values.begin() + start, input.values.begin() + at);
        ++at;
        return result;
    }
    template <class Handler> MLFields scan(Handler handle) {
        MTFields plain;
        while (!done()) {
            if (!input.hidden[at] && handle(*this))
                continue;
            plain.push_back(input.values[at++]);
        }
        return MLFields(std::move(plain));
    }
};
void mt_color(MLFields &f, Color color) { f.put_i(62, color.approximate_index()); }
void mt_begin(MLFields &f, const char *name) { f.put_s(1, std::string(name) + "_BEGIN"); }
void mt_end(MLFields &f, const char *name) { f.put_s(309, std::string(name) + "_END"); }
void mt_append(MLFields &f, const MTFields &values) { table_extras(f, values); }
std::int32_t mt_count(std::size_t n) {
    if (n > INT32_MAX)
        throw std::length_error("Table collection too large");
    return static_cast<std::int32_t>(n);
}
TableValue mt_value(MTFields fields) {
    // Modern value readers accept the body even when the legacy suppression bit is set.
    MLFields f(std::move(fields));
    auto flags = f.i(93, 0);
    for (std::size_t i = 0; i < f.values.size(); ++i)
        if (!f.hidden[i] && f.values[i].code == 93)
            f.values[i].value = "0";
    auto v = table_value(f);
    v.flags = flags;
    v.binary.clear();
    for (std::size_t n = 0; n < f.values.size(); ++n)
        if (!f.hidden[n] && f.values[n].code >= 310 && f.values[n].code <= 319) {
            auto bytes = table_bytes(f.values[n].value);
            v.binary.insert(v.binary.end(), bytes.begin(), bytes.end());
            f.used[n] = true;
        }
    v.extra_fields = f.extras();
    return v;
}
std::vector<TableCustomData> mt_custom(MTFields fields, MTFields &extra) {
    std::vector<TableCustomData> result;
    std::string name;
    MTReader r(std::move(fields));
    auto f = r.scan([&](auto &r) {
        if (r.is(301, "DATAMAP_VALUE")) {
            ++r.at;
            result.push_back({name, mt_value(r.body("ACVALUE_END", 304))});
            name.clear();
            return true;
        }
        if (r.input.values[r.at].code == 300) {
            name = r.input.values[r.at++].value;
            return true;
        }
        return false;
    });
    f.take(1);
    f.take(90);
    extra = f.extras();
    return result;
}
void mt_custom(MLFields &f, const std::vector<TableCustomData> &values, const MTFields &extra) {
    f.put_s(301, "CUSTOMDATA");
    mt_begin(f, "DATAMAP");
    f.put_i(90, mt_count(values.size()));
    for (const auto &value : values) {
        f.put_s(300, value.name);
        f.put_s(301, "DATAMAP_VALUE");
        table_value(f, value.value);
        f.put_s(304, "ACVALUE_END");
    }
    mt_append(f, extra);
    mt_end(f, "DATAMAP");
}
TableContentFormat mt_content_format(MTFields fields) {
    MLFields f(std::move(fields));
    TableContentFormat v;
    f.take(1);
    v.overrides = f.i(90, 0);
    v.properties = f.i(91, 0);
    v.data_type = f.i(92, 0);
    v.unit_type = f.i(93, 0);
    v.value_format = f.s(300, {});
    v.rotation = f.r(40, 0);
    v.scale = f.r(140, 1);
    v.alignment = f.i(94, 0);
    v.color = read_table_color(f, 62, 420, Color::by_block());
    v.text_style = f.h(340, 0);
    v.text_style_name = f.s(7, {});
    v.text_height = f.r(144, .18);
    v.extra_fields = f.extras();
    return v;
}
void mt_content_format(MLFields &f, const TableContentFormat &v) {
    if (!v.text_style_name.empty() && !v.text_style)
        throw std::invalid_argument("Resolve table text style names through a document API before writing");
    f.put_s(300, "CONTENTFORMAT");
    mt_begin(f, "CONTENTFORMAT");
    f.put_i(90, v.overrides);
    f.put_i(91, v.properties);
    f.put_i(92, v.data_type);
    f.put_i(93, v.unit_type);
    f.put_s(300, v.value_format);
    f.put_r(40, v.rotation);
    f.put_r(140, v.scale);
    f.put_i(94, v.alignment);
    mt_color(f, v.color);
    f.put_s(340, hex_handle(v.text_style));
    f.put_r(144, v.text_height);
    mt_append(f, v.extra_fields);
    mt_end(f, "CONTENTFORMAT");
}
TableGridFormat mt_grid(MTFields fields, std::uint32_t edge) {
    MLFields f(std::move(fields));
    TableGridFormat v;
    v.edge = edge;
    f.take(1);
    v.overrides = f.i(90, 0);
    v.type = f.e(91, 1, 1, 2);
    v.color = read_table_color(f, 62, 420, Color::by_block());
    v.line_weight = f.i(92, -1);
    v.line_type = f.h(340, 0);
    v.invisible = f.b(93, false);
    v.double_spacing = f.r(40, 0);
    v.extra_fields = f.extras();
    return v;
}
void mt_grid(MLFields &f, const TableGridFormat &v) {
    f.put_i(95, v.edge);
    f.put_s(302, "GRIDFORMAT");
    mt_begin(f, "GRIDFORMAT");
    f.put_i(90, v.overrides);
    f.put_i(91, v.type);
    mt_color(f, v.color);
    f.put_i(92, v.line_weight);
    f.put_s(340, hex_handle(v.line_type));
    f.put_i(93, v.invisible);
    f.put_r(40, v.double_spacing);
    mt_append(f, v.extra_fields);
    mt_end(f, "GRIDFORMAT");
}
std::optional<TableFormat> mt_format(MTFields fields) {
    MTReader r(std::move(fields));
    TableFormat v;
    std::uint32_t edge = 0;
    auto f = r.scan([&](auto &r) {
        if (r.is(300, "CONTENTFORMAT")) {
            ++r.at;
            v.content = mt_content_format(r.body("CONTENTFORMAT_END"));
            return true;
        }
        if (r.is(301, "MARGIN")) {
            ++r.at;
            MLFields m(r.body("CELLMARGIN_END"));
            m.take(1);
            auto values = m.take(40);
            for (std::size_t i = 0; i < std::min(values.size(), v.margins.size()); ++i) {
                MLFields one({m.values[values[i]]});
                v.margins[i] = one.r(40, 0);
            }
            v.margin_extra = m.extras();
            return true;
        }
        if (r.is(302, "GRIDFORMAT")) {
            ++r.at;
            v.borders.push_back(mt_grid(r.body("GRIDFORMAT_END"), edge));
            return true;
        }
        if (r.input.values[r.at].code == 95) {
            MLFields one({r.input.values[r.at++]});
            edge = one.i(95, 0);
            return true;
        }
        return false;
    });
    f.take(1);
    v.type = f.e(90, 1, 1, 5);
    bool present = f.b(170, false);
    v.overrides = f.i(91, 0);
    v.properties = f.i(92, 0);
    v.background = read_table_color(f, 62, 420, Color::by_block());
    v.layout = f.i(93, 1);
    v.margin_flags = f.i(171, 0);
    f.take(94);
    v.extra_fields = f.extras();
    return present ? std::optional<TableFormat>{v} : std::nullopt;
}
void mt_format(MLFields &f, const std::optional<TableFormat> &value, int fallback) {
    mt_begin(f, "TABLEFORMAT");
    f.put_i(90, value ? value->type : fallback);
    f.put_i(170, value.has_value());
    if (value) {
        const auto &v = *value;
        f.put_i(91, v.overrides);
        f.put_i(92, v.properties);
        mt_color(f, v.background);
        f.put_i(93, v.layout);
        mt_content_format(f, v.content);
        const auto flags = v.margin_flags | ((v.properties & 0x63c00) ? 1 : 0);
        f.put_i(171, flags);
        if (flags) {
            f.put_s(301, "MARGIN");
            mt_begin(f, "CELLMARGIN");
            for (double x : v.margins)
                f.put_r(40, x);
            mt_append(f, v.margin_extra);
            mt_end(f, "CELLMARGIN");
        }
        f.put_i(94, mt_count(v.borders.size()));
        for (const auto &border : v.borders)
            mt_grid(f, border);
        mt_append(f, v.extra_fields);
    }
    mt_end(f, "TABLEFORMAT");
}
std::optional<TableFormat> mt_format_section(MTFields fields, MTFields &extra) {
    MTReader r(std::move(fields));
    std::optional<TableFormat> result;
    auto f = r.scan([&](auto &r) {
        if (r.is(1, "TABLEFORMAT_BEGIN")) {
            ++r.at;
            result = mt_format(r.body("TABLEFORMAT_END"));
            return true;
        }
        if (r.input.values[r.at].code == 300) {
            ++r.at;
            return true;
        }
        return false;
    });
    extra = f.extras();
    return result;
}
TableContentItem mt_item(MTReader &r) {
    TableContentItem v;
    if (!r.is(1, "CELLCONTENT_BEGIN"))
        throw std::invalid_argument("Missing CELLCONTENT_BEGIN");
    ++r.at;
    MTReader body(r.body("CELLCONTENT_END"));
    std::optional<std::size_t> attribute;
    auto f = body.scan([&](auto &r) {
        if (r.is(300, "VALUE")) {
            ++r.at;
            v.value = mt_value(r.body("ACVALUE_END", 304));
            return true;
        }
        auto p = r.input.values[r.at];
        if (p.code == 330) {
            v.attributes.push_back({handle_value(p.value), {}, mt_count(v.attributes.size())});
            attribute = v.attributes.size() - 1;
            ++r.at;
            return true;
        }
        if (attribute && p.code == 301) {
            v.attributes[*attribute].text = p.value;
            ++r.at;
            return true;
        }
        if (attribute && p.code == 92) {
            MLFields one({p});
            v.attributes[*attribute].index = one.i(92, *attribute);
            attribute.reset();
            ++r.at;
            return true;
        }
        return false;
    });
    v.type = f.i(90, 0);
    v.reference = f.h(340, 0);
    f.take(91);
    v.extra_fields = f.extras();
    if (!r.is(1, "FORMATTEDCELLCONTENT_BEGIN"))
        throw std::invalid_argument("Missing formatted content scope");
    ++r.at;
    MTReader formatting(r.body("FORMATTEDCELLCONTENT_END"));
    auto format = formatting.scan([&](auto &r) {
        if (r.is(300, "CONTENTFORMAT")) {
            ++r.at;
            v.format = mt_content_format(r.body("CONTENTFORMAT_END"));
            return true;
        }
        return false;
    });
    format.take(170);
    v.formatted_extra = format.extras();
    return v;
}
bool mt_has_format(const TableContentFormat &v) {
    return v.overrides || v.properties || v.data_type || v.unit_type || !v.value_format.empty() ||
           v.rotation || v.scale != 1 || v.alignment || v.color != Color::by_block() || v.text_style ||
           !v.text_style_name.empty() || v.text_height != .18 || !v.extra_fields.empty();
}
void mt_item(MLFields &f, const TableContentItem &v) {
    if (v.type != 0 && v.type != 1 && v.type != 2 && v.type != 4)
        throw std::invalid_argument("Invalid table content type");
    f.put_s(302, "CONTENT");
    mt_begin(f, "CELLCONTENT");
    f.put_i(90, v.type);
    if (v.type == 1) {
        f.put_s(300, "VALUE");
        table_value(f, v.value);
        f.put_s(304, "ACVALUE_END");
    } else if (v.type == 2 || v.type == 4)
        f.put_s(340, hex_handle(v.reference));
    f.put_i(91, mt_count(v.attributes.size()));
    for (const auto &a : v.attributes) {
        f.put_s(330, hex_handle(a.definition));
        f.put_s(301, a.text);
        f.put_i(92, a.index);
    }
    mt_append(f, v.extra_fields);
    mt_end(f, "CELLCONTENT");
    mt_begin(f, "FORMATTEDCELLCONTENT");
    f.put_i(170, mt_has_format(v.format));
    if (mt_has_format(v.format))
        mt_content_format(f, v.format);
    mt_append(f, v.formatted_extra);
    mt_end(f, "FORMATTEDCELLCONTENT");
}
ModernTableCell mt_cell(MTFields fields) {
    MTReader r(std::move(fields));
    ModernTableCell v;
    auto meta = r.scan([&](auto &r) {
        if (r.is(1, "LINKEDTABLEDATACELL_BEGIN")) {
            ++r.at;
            MTReader core(r.body("LINKEDTABLEDATACELL_END"));
            auto f = core.scan([&](auto &r) {
                if (r.is(301, "CUSTOMDATA")) {
                    ++r.at;
                    v.custom = mt_custom(r.body("DATAMAP_END"), v.custom_extra);
                    return true;
                }
                if (r.is(302, "CONTENT")) {
                    ++r.at;
                    v.contents.push_back(mt_item(r));
                    return true;
                }
                return false;
            });
            v.state = f.i(90, 0);
            v.tooltip = f.s(300, {});
            v.custom_data = f.i(91, 0);
            v.linked = f.b(92, false);
            v.data_link = f.h(340, 0);
            v.link_rows = f.i(93, 0);
            v.link_columns = f.i(94, 0);
            v.link_unknown = f.i(96, 0);
            f.take(95);
            v.linked_extra = f.extras();
            return true;
        }
        if (r.is(1, "FORMATTEDTABLEDATACELL_BEGIN")) {
            ++r.at;
            v.format = mt_format_section(r.body("FORMATTEDTABLEDATACELL_END"), v.formatted_extra);
            return true;
        }
        return false;
    });
    meta.take(1);
    v.style_id = meta.i(90, 0);
    auto geometry = meta.take(91);
    // The first 91 is the embedded-geometry marker. Keep unsupported geometry fields.
    for (std::size_t i = 1; i < geometry.size(); ++i)
        meta.used[geometry[i]] = false;
    v.extra_fields = meta.extras();
    return v;
}
void mt_cell(MLFields &f, const ModernTableCell &v) {
    f.put_s(300, "CELL");
    mt_begin(f, "LINKEDTABLEDATACELL");
    f.put_i(90, v.state);
    f.put_s(300, v.tooltip);
    f.put_i(91, v.custom_data);
    mt_custom(f, v.custom, v.custom_extra);
    f.put_i(92, v.linked);
    if (v.linked) {
        f.put_s(340, hex_handle(v.data_link));
        f.put_i(93, v.link_rows);
        f.put_i(94, v.link_columns);
        f.put_i(96, v.link_unknown);
    }
    f.put_i(95, mt_count(v.contents.size()));
    for (const auto &c : v.contents)
        mt_item(f, c);
    mt_append(f, v.linked_extra);
    mt_end(f, "LINKEDTABLEDATACELL");
    mt_begin(f, "FORMATTEDTABLEDATACELL");
    f.put_s(300, "CELLTABLEFORMAT");
    mt_format(f, v.format, 1);
    mt_append(f, v.formatted_extra);
    mt_end(f, "FORMATTEDTABLEDATACELL");
    mt_begin(f, "TABLECELL");
    f.put_i(90, v.style_id);
    f.put_i(91, v.extra_fields.empty() ? 0 : 1);
    mt_append(f, v.extra_fields);
    mt_end(f, "TABLECELL");
}
ModernTableColumn mt_column(MTFields fields) {
    ModernTableColumn v;
    MTReader r(std::move(fields));
    auto meta = r.scan([&](auto &r) {
        if (r.is(1, "LINKEDTABLEDATACOLUMN_BEGIN")) {
            ++r.at;
            MTReader core(r.body("LINKEDTABLEDATACOLUMN_END"));
            auto f = core.scan([&](auto &r) {
                if (r.is(301, "CUSTOMDATA")) {
                    ++r.at;
                    v.custom = mt_custom(r.body("DATAMAP_END"), v.custom_extra);
                    return true;
                }
                return false;
            });
            v.name = f.s(300, {});
            v.custom_data = f.i(91, 0);
            v.linked_extra = f.extras();
            return true;
        }
        if (r.is(1, "FORMATTEDTABLEDATACOLUMN_BEGIN")) {
            ++r.at;
            v.format = mt_format_section(r.body("FORMATTEDTABLEDATACOLUMN_END"), v.formatted_extra);
            return true;
        }
        return false;
    });
    meta.take(1);
    v.style_id = meta.i(90, 0);
    v.width = meta.r(40, 2.5);
    v.extra_fields = meta.extras();
    return v;
}
void mt_column(MLFields &f, const ModernTableColumn &v) {
    f.put_s(300, "COLUMN");
    mt_begin(f, "LINKEDTABLEDATACOLUMN");
    f.put_s(300, v.name);
    f.put_i(91, v.custom_data);
    mt_custom(f, v.custom, v.custom_extra);
    mt_append(f, v.linked_extra);
    mt_end(f, "LINKEDTABLEDATACOLUMN");
    mt_begin(f, "FORMATTEDTABLEDATACOLUMN");
    f.put_s(300, "COLUMNTABLEFORMAT");
    mt_format(f, v.format, 3);
    mt_append(f, v.formatted_extra);
    mt_end(f, "FORMATTEDTABLEDATACOLUMN");
    mt_begin(f, "TABLECOLUMN");
    f.put_i(90, v.style_id);
    f.put_r(40, v.width);
    mt_append(f, v.extra_fields);
    mt_end(f, "TABLECOLUMN");
}
ModernTableRow mt_row(MTFields fields) {
    ModernTableRow v;
    MTReader r(std::move(fields));
    auto meta = r.scan([&](auto &r) {
        if (r.is(1, "LINKEDTABLEDATAROW_BEGIN")) {
            ++r.at;
            MTReader core(r.body("LINKEDTABLEDATAROW_END"));
            auto f = core.scan([&](auto &r) {
                if (r.is(300, "CELL")) {
                    ++r.at;
                    v.cells.push_back(mt_cell(r.body("TABLECELL_END")));
                    return true;
                }
                if (r.is(301, "CUSTOMDATA")) {
                    ++r.at;
                    v.custom = mt_custom(r.body("DATAMAP_END"), v.custom_extra);
                    return true;
                }
                return false;
            });
            f.take(90);
            v.custom_data = f.i(91, 0);
            v.linked_extra = f.extras();
            return true;
        }
        if (r.is(1, "FORMATTEDTABLEDATAROW_BEGIN")) {
            ++r.at;
            v.format = mt_format_section(r.body("FORMATTEDTABLEDATAROW_END"), v.formatted_extra);
            return true;
        }
        return false;
    });
    meta.take(1);
    v.style_id = meta.i(90, 0);
    v.height = meta.r(40, .25);
    v.extra_fields = meta.extras();
    return v;
}
void mt_row(MLFields &f, const ModernTableRow &v) {
    f.put_s(301, "ROW");
    mt_begin(f, "LINKEDTABLEDATAROW");
    f.put_i(90, mt_count(v.cells.size()));
    for (const auto &c : v.cells)
        mt_cell(f, c);
    f.put_i(91, v.custom_data);
    mt_custom(f, v.custom, v.custom_extra);
    mt_append(f, v.linked_extra);
    mt_end(f, "LINKEDTABLEDATAROW");
    mt_begin(f, "FORMATTEDTABLEDATAROW");
    f.put_s(300, "ROWTABLEFORMAT");
    mt_format(f, v.format, 2);
    mt_append(f, v.formatted_extra);
    mt_end(f, "FORMATTEDTABLEDATAROW");
    mt_begin(f, "TABLEROW");
    f.put_i(90, v.style_id);
    f.put_r(40, v.height);
    mt_append(f, v.extra_fields);
    mt_end(f, "TABLEROW");
}
void mt_dimensions(const TableContentDefinition &v) {
    if (v.rows.size() > 1000000 || v.columns.size() > 32767)
        throw std::invalid_argument("Modern table dimensions exceed supported limits");
    for (const auto &row : v.rows)
        if (row.cells.size() != v.columns.size())
            throw std::invalid_argument("Modern table must be rectangular");
    for (const auto &r : v.merged_ranges)
        if (r.top > r.bottom || r.left > r.right || r.bottom >= v.rows.size() || r.right >= v.columns.size())
            throw std::invalid_argument("Invalid modern table merge range");
}
} // namespace
TableContentDefinition DxfTableContent::definition() const {
    TableContentDefinition v;
    MLFields data(subclass_fields("AcDbLinkedData"));
    v.name = data.s(1, {});
    v.description = data.s(300, {});
    v.data_extra = data.extras();
    MTReader linked(subclass_fields("AcDbLinkedTableData"));
    auto f = linked.scan([&](auto &r) {
        if (r.is(300, "COLUMN")) {
            ++r.at;
            v.columns.push_back(mt_column(r.body("TABLECOLUMN_END")));
            return true;
        }
        if (r.is(301, "ROW")) {
            ++r.at;
            v.rows.push_back(mt_row(r.body("TABLEROW_END")));
            return true;
        }
        return false;
    });
    f.take(90);
    f.take(91);
    auto count = std::max(0, f.i(92, 0));
    for (std::size_t n = 0; n < f.values.size() && count; ++n)
        if (!f.hidden[n] && (f.values[n].code == 330 || f.values[n].code == 340 || f.values[n].code == 360)) {
            auto h = handle_value(f.values[n].value);
            if (h)
                v.fields.push_back(h);
            f.used[n] = true;
            --count;
        }
    v.linked_extra = f.extras();
    MTReader formatted(subclass_fields("AcDbFormattedTableData"));
    auto ranges = formatted.scan([&](auto &r) {
        if (r.is(300, "TABLEFORMAT")) {
            ++r.at;
            return true;
        }
        if (r.is(1, "TABLEFORMAT_BEGIN")) {
            ++r.at;
            v.format = mt_format(r.body("TABLEFORMAT_END"));
            return true;
        }
        return false;
    });
    auto range_count = std::max(0, ranges.i(90, 0));
    TableRange range;
    bool open = false;
    for (std::size_t n = 0; n < ranges.values.size(); ++n)
        if (!ranges.hidden[n] && range_count) {
            auto p = ranges.values[n];
            MLFields one({p});
            auto x = std::size_t(std::max(0, one.i(p.code, 0)));
            if (p.code == 91) {
                range = {};
                range.top = x;
                open = true;
            } else if (open && p.code == 92)
                range.left = x;
            else if (open && p.code == 93)
                range.bottom = x;
            else if (open && p.code == 94) {
                range.right = x;
                v.merged_ranges.push_back(range);
                open = false;
                --range_count;
            } else
                continue;
            ranges.used[n] = true;
        }
    v.formatted_extra = ranges.extras();
    MLFields content(subclass_fields("AcDbTableContent"));
    v.style = content.h(340, 0);
    v.extra_fields = content.extras();
    return v;
}
void DxfTableContent::set_definition(const TableContentDefinition &v) {
    mt_dimensions(v);
    MLFields data, linked, formatted, content;
    data.put_s(1, v.name);
    data.put_s(300, v.description);
    mt_append(data, v.data_extra);
    linked.put_i(90, mt_count(v.columns.size()));
    for (const auto &col : v.columns)
        mt_column(linked, col);
    linked.put_i(91, mt_count(v.rows.size()));
    for (const auto &row : v.rows)
        mt_row(linked, row);
    linked.put_i(92, mt_count(v.fields.size()));
    for (auto h : v.fields)
        linked.put_s(360, hex_handle(h));
    mt_append(linked, v.linked_extra);
    formatted.put_s(300, "TABLEFORMAT");
    mt_format(formatted, v.format, 4);
    formatted.put_i(90, mt_count(v.merged_ranges.size()));
    for (const auto &r : v.merged_ranges) {
        formatted.put_i(91, r.top);
        formatted.put_i(92, r.left);
        formatted.put_i(93, r.bottom);
        formatted.put_i(94, r.right);
    }
    mt_append(formatted, v.formatted_extra);
    content.put_s(340, hex_handle(v.style));
    mt_append(content, v.extra_fields);
    auto staged = *this;
    staged.set_subclass_fields("AcDbLinkedData", data.values);
    staged.set_subclass_fields("AcDbLinkedTableData", linked.values);
    staged.set_subclass_fields("AcDbFormattedTableData", formatted.values);
    staged.set_subclass_fields("AcDbTableContent", content.values);
    *this = std::move(staged);
}
namespace {
constexpr std::array<unsigned, 4> outer_grid_edges{1, 32, 4, 8};
TableFormat mt_from_legacy(const TableCellStyle &s) {
    TableFormat f;
    f.overrides = s.overrides;
    f.properties = s.property_flags;
    f.background = s.background_color;
    f.content.text_style_name = s.text_style;
    f.content.text_height = s.text_height;
    f.content.alignment = s.alignment;
    f.content.color = s.content_color;
    for (int i = 0; i < 4; ++i) {
        TableGridFormat b;
        b.edge = outer_grid_edges[i];
        b.color = s.borders[i].color;
        b.line_weight = s.borders[i].line_weight;
        b.invisible = s.borders[i].invisible;
        f.borders.push_back(b);
    }
    return f;
}
TableCellStyle mt_to_legacy(const TableFormat &f) {
    TableCellStyle s;
    s.overrides = f.overrides;
    s.property_flags = f.properties;
    s.background_color = f.background;
    s.fill_enabled = f.background != Color::by_block() && f.background != Color::none();
    s.text_style = f.content.text_style_name;
    s.text_height = f.content.text_height;
    s.alignment = f.content.alignment;
    s.content_color = f.content.color;
    for (const auto &b : f.borders)
        for (int i = 0; i < 4; ++i)
            if (b.edge == (outer_grid_edges[i])) {
                s.borders[i].color = b.color;
                s.borders[i].line_weight = b.line_weight;
                s.borders[i].invisible = b.invisible;
            }
    return s;
}
} // namespace
// Project the resolved DXF cell into the pre-R2010 layout (acadrust Table::legacy_layout_cell).
void DxfDocument::project_legacy_table_cells(TableDefinition &table, const TableContentDefinition &data) const {
    for (std::size_t r = 0; r < data.rows.size(); ++r) {
        const auto &row = data.rows[r];
        for (std::size_t c = 0; c < row.cells.size(); ++c) {
            auto &out = table.cells.at(r).at(c);
            const auto &cell = row.cells[c];
            const auto block = std::find_if(cell.contents.begin(), cell.contents.end(),
                                            [](const auto &item) { return item.type == 4; });
            if (block != cell.contents.end()) {
                out.type = 2;
                // The stored value handle follows the first block in the content.
                out.block = block->reference;
                out.field = 0;
            } else if (!cell.contents.empty()) {
                out.type = 1;
                out.block = 0;
                out.field = cell.contents.front().type == 2 ? cell.contents.front().reference : 0;
            }
            // A style decoded from legacy cell codes already has the authoritative word.
            if (out.style && out.style->overrides) continue;
            const unsigned top = r == 0 ? 1 : 2;
            const unsigned bottom = r + std::max<int>(1, out.merge_height) >= data.rows.size() ? 4 : 2;
            const unsigned left = c == 0 ? 8 : 16;
            const unsigned right = c + std::max<int>(1, out.merge_width) >= row.cells.size() ? 32 : 16;
            struct Source { const std::optional<TableFormat> &style; std::array<unsigned, 4> edges; };
            const Source sources[] = {{cell.format, outer_grid_edges}, {row.format, {1,right,4,left}},
                {data.columns.at(c).format, {top,32,bottom,8}}, {data.format, {top,right,bottom,left}}};
            const auto *item = cell.contents.empty() ? nullptr : &cell.contents.front().format;
            TableCellStyle style;
            for (int property : {16, 32, 64, 128, 512}) {
                if (out.type == 2 && (property == 64 || property == 128)) continue;
                const TableContentFormat *format = nullptr;
                if (property != 512 && item && ((item->properties | item->overrides) & property))
                    format = item;
                else {
                    for (const auto &src : sources) {
                        if (!src.style || !((src.style->properties | src.style->overrides) & property))
                            continue;
                        format = &src.style->content;
                        if (property == 512) {
                            style.background_color = src.style->background;
                            style.fill_enabled = style.background_color != Color::by_block() &&
                                                 style.background_color != Color::none();
                        }
                        break;
                    }
                }
                if (!format) continue;
                style.property_flags |= property;
                switch (property) {
                case 16: style.alignment = format->alignment; style.overrides |= 1; break;
                case 32: style.content_color = format->color; style.overrides |= 8; break;
                case 64: style.text_style = format->text_style_name; style.overrides |= 16; break;
                case 128: style.text_height = format->text_height; style.overrides |= 32; break;
                case 512: style.overrides |= 6; break;
                }
            }
            for (int e = 0; e < 4; ++e) {
                auto &border = style.borders[e];
                unsigned used = 0;
                for (const auto &src : sources) {
                    if (!src.style) continue;
                    for (const auto &b : src.style->borders) {
                        if (b.edge != src.edges[e]) continue;
                        auto bits = b.overrides & ~used;
                        if (bits & 8) { border.color = b.color; style.overrides |= 0x40 << e; }
                        if (bits & 2) { border.line_weight = b.line_weight; style.overrides |= 0x400 << e; }
                        if (bits & 16) { border.invisible = b.invisible; style.overrides |= 0x4000 << e; }
                        used |= bits;
                    }
                }
            }
            if (style.overrides || cell.format) out.style = std::move(style);
        }
    }
}
void DxfDocument::validate_table_content(TableContentDefinition &v) const {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Modern table construction requires R2007 or later");
    mt_dimensions(v);
    mleader_check_handle(*this, v.style, "TABLESTYLE");
    auto format = [&](TableContentFormat &f) {
        if (!f.text_style_name.empty() && !f.text_style) {
            auto s = get_symbol(SymbolTable::TextStyle, f.text_style_name);
            if (!s)
                throw std::invalid_argument("Missing table text style");
            f.text_style = handle_value(s->handle);
        }
        mleader_check_handle(*this, f.text_style, "STYLE");
        if (f.text_style) {
            auto *s = get_record(hex_handle(f.text_style));
            f.text_style_name = s->text_field("AcDbTextStyleTableRecord", 2);
        }
    };
    auto style = [&](std::optional<TableFormat> &s) {
        if (!s)
            return;
        format(s->content);
        for (const auto &b : s->borders)
            mleader_check_handle(*this, b.line_type, "LTYPE");
    };
    auto value = [&](const TableValue &x) {
        if (x.type == 64 && x.handle && records_with_handle(hex_handle(x.handle)).size() != 1)
            throw std::invalid_argument("Missing table value handle");
    };
    auto custom = [&](const std::vector<TableCustomData> &data) {
        for (const auto &x : data)
            value(x.value);
    };
    style(v.format);
    for (auto h : v.fields)
        mleader_check_handle(*this, h, "FIELD");
    for (auto &c : v.columns) {
        style(c.format);
        custom(c.custom);
    }
    for (auto &row : v.rows) {
        style(row.format);
        custom(row.custom);
        for (auto &c : row.cells) {
            style(c.format);
            custom(c.custom);
            mleader_check_handle(*this, c.data_link, "DATALINK");
            for (auto &item : c.contents) {
                format(item.format);
                value(item.value);
                if (item.type == 2)
                    mleader_check_handle(*this, item.reference, "FIELD");
                if (item.type == 4)
                    mleader_check_handle(*this, item.reference, "BLOCK_RECORD");
                for (const auto &a : item.attributes)
                    mleader_check_handle(*this, a.definition, "ATTDEF");
            }
        }
    }
}
std::uint64_t DxfDocument::add_table_content(std::string_view dictionary, std::string name,
                                             const TableContentDefinition &definition, bool as_template) {
    auto v = definition;
    validate_table_content(v);
    // A separately owned FIELD cannot be adopted implicitly by a new content object.
    if (!v.fields.empty())
        throw std::invalid_argument("New table content cannot adopt existing owned FIELD objects");
    DxfDocument staged = *this;
    DxfTableContent object;
    static_cast<DxfRecord &>(object) = staged.make_record(as_template ? "TABLETEMPLATE" : "TABLECONTENT");
    for (const auto *scope :
         {"AcDbLinkedData", "AcDbLinkedTableData", "AcDbFormattedTableData", "AcDbTableContent"})
        object.pairs_.push_back({100, scope});
    if (as_template)
        object.pairs_.push_back({100, "AcDbTableTemplate"});
    object.set_definition(v);
    const auto id = staged.add_dictionary_object(dictionary, std::move(name), object);
    *this = std::move(staged);
    return id;
}
std::pair<std::string, std::string> DxfDocument::table_roundtrip_links(std::string_view entity) const {
    const auto *source = get_record(entity);
    if (records_with_handle(entity).size() != 1 || !source || source->type() != "ACAD_TABLE")
        throw std::invalid_argument("Expected unique ACAD_TABLE entity");
    bool typed = false;
    for_each<DxfTable>([&](const auto &table) {
        if (handle_value(table.handle()) == handle_value(entity))
            typed = true;
    });
    if (!typed)
        throw std::invalid_argument("Expected typed ACAD_TABLE in an entity section");
    std::string dictionary, record;
    unsigned depth = 0;
    for (std::size_t i = 1; i < source->pairs_.size(); ++i) {
        const auto &p = source->pairs_[i];
        if (!depth && (p.code == 100 || p.code >= 1000 || p.code == 101))
            break;
        if (p.code != 102)
            continue;
        if (!depth && p.value == "{ACAD_XDICTIONARY") {
            if (!dictionary.empty() || i + 2 >= source->pairs_.size() || source->pairs_[i + 1].code != 360 ||
                source->pairs_[i + 2].value != "}")
                throw std::invalid_argument("Malformed table extension dictionary");
            dictionary = source->pairs_[i + 1].value;
            i += 2;
            continue;
        }
        if (!p.value.empty() && p.value.front() == '{')
            ++depth;
        else if (depth && p.value == "}")
            --depth;
    }
    if (dictionary.empty())
        return {};
    const auto *dict = get_record(dictionary);
    if (records_with_handle(dictionary).size() != 1 || !dict || dict->type() != "DICTIONARY" ||
        handle_value(dict->owner_handle()) != handle_value(entity))
        throw std::invalid_argument("Invalid table dictionary ownership");
    bool matched = false;
    dict->locate_if("AcDbDictionary", [&](std::size_t i, int code) {
        const auto &p = dict->pairs_[i];
        if (code == 3)
            matched = dict->decode_string(p.value) == "ACAD_XREC_ROUNDTRIP";
        else if (matched && (code == 350 || code == 360)) {
            if (!record.empty())
                throw std::invalid_argument("Duplicate table roundtrip record");
            record = p.value;
            matched = false;
        }
        return false;
    });
    if (!record.empty()) {
        const auto *x = get_record(record);
        if (records_with_handle(record).size() != 1 || !x || x->type() != "XRECORD" ||
            handle_value(x->owner_handle()) != handle_value(dictionary))
            throw std::invalid_argument("Invalid table roundtrip record ownership");
    }
    return {dictionary, record};
}
std::string DxfDocument::table_content_handle(std::string_view entity) const {
    const auto links = table_roundtrip_links(entity);
    if (links.second.empty())
        return {};
    const auto *record = get_record(links.second);
    bool active = false;
    std::string content;
    int sections = 0;
    for (const auto &p : record->pairs_) {
        if (p.code == 102) {
            active = p.value == "ACAD_ROUNDTRIP_2008_TABLE_ENTITY";
            if (active && ++sections > 1)
                throw std::invalid_argument("Duplicate table roundtrip section");
        } else if (active && p.code == 360) {
            if (!content.empty())
                throw std::invalid_argument("Multiple table content links");
            content = p.value;
        }
    }
    if (content.empty())
        return {};
    const auto *object = get_record(content);
    if (records_with_handle(content).size() != 1 || !object || object->type() != "TABLECONTENT" ||
        handle_value(object->owner_handle()) != handle_value(links.second))
        throw std::invalid_argument("Invalid table content ownership");
    return object->handle();
}
TableContentDefinition DxfDocument::table_content_definition(std::string_view entity) const {
    return resolved_table_content(entity, false);
}
TableContentDefinition DxfDocument::resolved_table_content(std::string_view entity, bool synchronize) const {
    std::string content;
    try {
        content = table_content_handle(entity);
    } catch (const std::invalid_argument &) {
        if (synchronize || !get_record(entity) || get_record(entity)->type() != "ACAD_TABLE") throw;
    }
    TableContentDefinition v;
    if (!content.empty())
        for_each<DxfTableContent>([&](const auto &t) {
            if (handle_value(t.handle()) == handle_value(content))
                v = t.definition();
        });
    TableDefinition legacy;
    for_each<DxfTable>([&](const auto &t) {
        if (handle_value(t.handle()) == handle_value(entity))
            legacy = t.definition();
    });
    if (!content.empty() && (v.columns.size() != legacy.column_widths.size() ||
        v.rows.size() != legacy.cells.size() || !std::equal(v.rows.begin(), v.rows.end(), legacy.cells.begin(),
            [](const auto &row, const auto &cells) { return row.cells.size() == cells.size(); })))
        v = {}; // Ignore an inconsistent adjunct as a whole.
    v.style = legacy.style;
    v.merged_ranges = legacy.merged_ranges;
    v.columns.resize(legacy.column_widths.size());
    v.rows.resize(legacy.cells.size());
    for (std::size_t c = 0; c < v.columns.size(); ++c)
        v.columns[c].width = legacy.column_widths[c];
    for (std::size_t r = 0; r < v.rows.size(); ++r) {
        auto &row = v.rows[r];
        row.height = legacy.row_heights[r];
        row.cells.resize(legacy.cells[r].size());
        for (std::size_t c = 0; c < row.cells.size(); ++c) {
            auto &cell = row.cells[c];
            const auto &old = legacy.cells[r][c];
            cell.state |= old.state;
            const bool authored = !synchronize && (cell.contents.size() > 1 || (!cell.contents.empty() &&
                (cell.contents.front().type == 2 || cell.contents.front().type == 4)));
            if (!authored && (old.field || old.block || (old.value && (!cell.contents.empty() ||
                old.value->type != 4 || !old.value->text.empty())))) {
                if (cell.contents.empty())
                    cell.contents.emplace_back();
                auto &item = cell.contents.front();
                item.type = old.field ? 2 : old.block ? 4 : 1;
                item.reference = old.field ? old.field : old.block;
                if (old.value)
                    item.value = *old.value;
                // Legacy attributes omit modern indices; retain those indices when possible.
                auto attributes = item.attributes;
                item.attributes.clear();
                for (std::size_t i = 0; i < old.attributes.size(); ++i) {
                    const auto &a = old.attributes[i];
                    item.attributes.push_back(
                        {a.definition, a.text, i < attributes.size() ? attributes[i].index : mt_count(i)});
                }
            } else if (!authored && !old.value)
                cell.contents.clear();
            if (old.style) {
                if (!cell.format)
                    cell.format = mt_from_legacy(*old.style);
                else {
                    // Match restore_table_entity_roundtrip: retain data type/format metadata.
                    auto updated = mt_from_legacy(*old.style);
                    auto &f = *cell.format;
                    const auto mask = old.style->overrides;
                    if (mask & 4)
                        f.background = updated.background;
                    if (mask & 8)
                        f.content.color = updated.content.color;
                    if (mask & 1)
                        f.content.alignment = updated.content.alignment;
                    if (mask & 32)
                        f.content.text_height = updated.content.text_height;
                    if ((mask & 16) && !old.style->text_style.empty()) {
                        f.content.text_style_name = old.style->text_style;
                        f.content.text_style = 0;
                    }
                    for (int i = 0; i < 4; ++i)
                        for (auto &b : f.borders)
                            if (b.edge == (outer_grid_edges[i])) {
                                if (mask & (0x40 << i))
                                    b.color = old.style->borders[i].color;
                                if (mask & (0x400 << i))
                                    b.line_weight = old.style->borders[i].line_weight;
                                if (mask & (0x4000 << i))
                                    b.invisible = old.style->borders[i].invisible;
                            }
                }
            }
        }
    }
    TableGeometryDefinition geometry;
    try {
        geometry = table_geometry_definition(entity);
    } catch (const std::invalid_argument &) {
        if (synchronize) throw;
    }
    if (geometry.rows >= 0 && std::size_t(geometry.rows) == v.rows.size() && geometry.columns >= 0 &&
        std::size_t(geometry.columns) == v.columns.size() && geometry.cells.size() == v.rows.size() * v.columns.size()) {
        std::size_t i = 0;
        for (auto &row : v.rows)
            for (auto &cell : row.cells) {
                if (i < geometry.cells.size())
                    cell.geometry = geometry.cells[i];
                ++i;
            }
    }
    return v;
}
void DxfDocument::set_table_content(std::string_view entity, const TableContentDefinition &definition) {
    const std::string id(entity);
    auto v = definition;
    validate_table_content(v);
    auto links = table_roundtrip_links(id);
    auto content = table_content_handle(id);
    const auto *source = get_record(id);
    extension_tree({source});
    TableDefinition legacy;
    for_each<DxfTable>([&](const auto &t) {
        if (handle_value(t.handle()) == handle_value(id))
            legacy = t.definition();
    });
    if (!content.empty())
        for_each<DxfTable>([&](const auto &t) {
            if (handle_value(t.handle()) != handle_value(id) && table_content_handle(t.handle()) == content)
                throw std::invalid_argument("Shared table content");
        });
    for (auto h : v.fields) {
        const auto *field = get_record(hex_handle(h));
        if (content.empty() || handle_value(field->owner_handle()) != handle_value(content))
            throw std::invalid_argument("FIELD is not owned by table content");
    }
    legacy.style = v.style;
    legacy.merged_ranges = v.merged_ranges;
    legacy.row_heights.resize(v.rows.size());
    legacy.column_widths.resize(v.columns.size());
    legacy.cells.resize(v.rows.size());
    for (std::size_t c = 0; c < v.columns.size(); ++c)
        legacy.column_widths[c] = v.columns[c].width;
    for (std::size_t r = 0; r < v.rows.size(); ++r) {
        legacy.row_heights[r] = v.rows[r].height;
        legacy.cells[r].resize(v.columns.size());
        for (std::size_t c = 0; c < v.columns.size(); ++c) {
            auto &cell = legacy.cells[r][c];
            const auto &modern = v.rows[r].cells[c];
            cell.state = modern.state;
            cell.value.reset();
            cell.field = cell.block = 0;
            cell.attributes.clear();
            cell.merged = 0;
            cell.merge_height = cell.merge_width = 1;
            cell.type = 1;
            if (!modern.contents.empty()) {
                const auto &item = modern.contents.front();
                if (item.type == 1)
                    cell.value = item.value;
                else if (item.type == 2)
                    cell.field = item.reference;
                else if (item.type == 4) {
                    cell.block = item.reference;
                    cell.type = 2;
                }
                for (const auto &a : item.attributes)
                    cell.attributes.push_back({a.definition, a.text});
            }
            cell.style =
                modern.format ? std::optional<TableCellStyle>{mt_to_legacy(*modern.format)} : std::nullopt;
        }
    }
    DxfDocument staged = *this;
    if (version_ < DxfVersion::AC1024) {
        // These styles came from the modern object, not from a stored legacy override word.
        for (auto &row : legacy.cells)
            for (auto &cell : row) cell.style.reset();
        project_legacy_table_cells(legacy, v);
    }
    staged.for_each<DxfTable>([&](auto &t) {
        if (handle_value(t.handle()) == handle_value(id))
            t.set_definition(legacy);
    });
    if (links.first.empty())
        links.first = hex_handle(staged.add_extension_dictionary(id));
    if (links.second.empty())
        links.second = hex_handle(staged.add_xrecord(links.first, "ACAD_XREC_ROUNDTRIP", {}));
    if (content.empty()) {
        DxfTableContent object;
        static_cast<DxfRecord &>(object) = staged.make_record("TABLECONTENT");
        object.set_field("", 330, links.second);
        object.pairs_.insert(object.pairs_.end(), {{102, "{ACAD_REACTORS"}, {330, links.second}, {102, "}"}});
        for (const auto *scope :
             {"AcDbLinkedData", "AcDbLinkedTableData", "AcDbFormattedTableData", "AcDbTableContent"})
            object.pairs_.push_back({100, scope});
        object.set_definition(v);
        content = hex_handle(staged.insert_object(object));
    } else
        staged.for_each<DxfTableContent>([&](auto &t) {
            if (handle_value(t.handle()) == handle_value(content))
                t.set_definition(v);
        });
    auto &record = const_cast<DxfRecord &>(*staged.get_record(links.second));
    std::size_t begin = record.pairs_.size(), end = begin;
    for (std::size_t i = 0; i < record.pairs_.size(); ++i)
        if (record.pairs_[i].code == 102 && record.pairs_[i].value == "ACAD_ROUNDTRIP_2008_TABLE_ENTITY") {
            begin = i;
            end = i + 1;
            while (end < record.pairs_.size() && record.pairs_[end].code != 102)
                ++end;
            break;
        }
    if (begin == record.pairs_.size()) {
        MLFields payload;
        payload.put_s(102, "ACAD_ROUNDTRIP_2008_TABLE_ENTITY");
        payload.put_s(360, content);
        payload.put_i(70, 2);
        payload.put_i(90, 1);
        payload.put_p(10, {});
        payload.put_i(90, 0);
        payload.put_i(90, v.rows.empty() ? 0 : mt_count(v.rows.size() - 1));
        for (const auto &p : payload.values)
            record.pairs_.push_back(
                {p.code, detail::is_string_value(p.code)
                             ? record.encode_string(p.value)
                             : (binary_ ? detail::binary_value_from_text(p.code, p.value) : p.value)});
    } else {
        bool linked = false;
        int index = 0;
        for (auto i = begin + 1; i < end; ++i) {
            auto &p = record.pairs_[i];
            if (p.code == 360) {
                p.value = content;
                linked = true;
            }
            if (p.code == 90 && ++index == 3) {
                auto value = std::to_string(v.rows.empty() ? 0 : v.rows.size() - 1);
                p.value = binary_ ? detail::binary_value_from_text(90, value) : value;
            }
        }
        if (!linked)
            record.pairs_.insert(record.pairs_.begin() + end, {360, content});
    }
    staged.rebuild_indexes();
    staged.update_table_geometry(id, v);
    *this = std::move(staged);
}
void DxfDocument::synchronize_table_content(std::string_view entity) {
    set_table_content(entity, resolved_table_content(entity, true));
}

void DxfDocument::synchronize_table_dimensions(std::string_view entity) {
    // Unlike full content synchronization, this preserves authored cell values,
    // multi-content formatting and cached geometry byte for byte.
    const auto content = table_content_handle(entity);
    const auto *record = get_record(entity);
    if (!record || record->type() != "ACAD_TABLE")
        throw std::invalid_argument("Table dimension synchronization requires ACAD_TABLE");
    if (content.empty())
        return;
    TableDefinition legacy;
    for_each<DxfTable>([&](const auto &table) {
        if (handle_value(table.handle()) == handle_value(entity))
            legacy = table.definition();
    });
    auto staged = *this;
    staged.for_each<DxfTableContent>([&](auto &table) {
        if (handle_value(table.handle()) != handle_value(content))
            return;
        auto definition = table.definition();
        if (definition.rows.size() != legacy.row_heights.size() ||
            definition.columns.size() != legacy.column_widths.size())
            throw std::invalid_argument("TABLECONTENT dimension mismatch");
        std::size_t rows = 0, columns = 0;
        int active = 0;
        bool written = false;
        table.locate_if("AcDbLinkedTableData", [&](std::size_t at, int code) {
            const auto value = (code == 1 || code == 309) ? table.decode_string(table.pairs_[at].value) : "";
            if (code == 1 && (value == "TABLECOLUMN_BEGIN" || value == "TABLEROW_BEGIN")) {
                if (active)
                    throw std::invalid_argument("Nested table dimension metadata");
                active = value == "TABLECOLUMN_BEGIN" ? 1 : 2;
                written = false;
            } else if (active && code == 40) {
                if (written)
                    throw std::invalid_argument("Duplicate table dimension metadata");
                const auto &sizes = active == 1 ? legacy.column_widths : legacy.row_heights;
                const auto index = active == 1 ? columns : rows;
                if (index >= sizes.size())
                    throw std::invalid_argument("Excess table dimension metadata");
                if (table.number_at(at) != sizes[index])
                    table.pairs_[at].value = table.encode_number(sizes[index]);
                written = true;
            } else if (code == 309 && (value == "TABLECOLUMN_END" || value == "TABLEROW_END")) {
                if (!active || !written || (active == 1) != (value == "TABLECOLUMN_END"))
                    throw std::invalid_argument("Incomplete table dimension metadata");
                if (active == 1)
                    ++columns;
                else
                    ++rows;
                active = 0;
            }
            return false;
        });
        if (active || rows != legacy.row_heights.size() || columns != legacy.column_widths.size())
            throw std::invalid_argument("Incomplete table dimension metadata");
    });
    *this = std::move(staged);
}
