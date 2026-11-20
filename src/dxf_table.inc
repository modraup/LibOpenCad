// SPDX-License-Identifier: MPL-2.0
// ACAD_TABLE/TABLESTYLE DXF maps from acadrust section_reader/section_writer.
std::vector<DxfRawField> DxfRecord::subclass_fields(std::string_view scope) const {
    unsigned depth = 0;
    std::size_t begin = std::string_view::npos;
    for (std::size_t n = 1; n < pairs_.size(); ++n) {
        const auto &p = pairs_[n];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (depth && p.value == "}")
                --depth;
            continue;
        }
        if (!depth && p.code == 100 && p.value == scope) {
            begin = n + 1;
            break;
        }
    }
    if (begin == std::string_view::npos)
        throw std::invalid_argument("Missing DXF subclass " + std::string(scope));
    auto end = locate_if(scope, [](std::size_t, int) { return false; }).index;
    std::vector<DxfRawField> values;
    for (auto n = begin; n < end; ++n) {
        const auto &p = pairs_[n];
        values.push_back(
            {p.code, detail::is_string_value(p.code)
                         ? decode_string(p.value)
                         : (binary_ ? detail::text_value_from_binary(p.code, p.value) : p.value)});
    }
    return values;
}
void DxfRecord::set_subclass_fields(std::string_view scope, const std::vector<DxfRawField> &values,
                                    bool literal_binary_strings, bool allow_extended_codes) {
    // Find the original contiguous span, including private application groups.
    auto original = subclass_fields(scope);
    auto end = locate_if(scope, [](std::size_t, int) { return false; }).index;
    auto begin = end - original.size();
    std::vector<Pair> replacement;
    unsigned depth = 0;
    for (const auto &p : values) {
        if (p.code <= 0 || p.code == 100 || p.code == 101 || (p.code >= 1000 && !allow_extended_codes))
            throw std::invalid_argument("Invalid subclass field code");
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (p.value == "}") {
                if (!depth)
                    throw std::invalid_argument("Unbalanced subclass group");
                --depth;
            }
        }
        if (literal_binary_strings && p.value.find('\0') != std::string::npos)
            throw std::invalid_argument("NUL in literal DXF string");
        auto binary = detail::binary_value_from_text(p.code, p.value);
        replacement.push_back(
            {p.code, detail::is_string_value(p.code)
                         ? (literal_binary_strings && binary_ ? detail::to_legacy(p.value, code_page_)
                                                              : encode_string(p.value))
                         : (binary_ ? std::move(binary) : p.value)});
    }
    if (depth)
        throw std::invalid_argument("Unclosed subclass group");
    auto staged = pairs_;
    staged.erase(staged.begin() + begin, staged.begin() + end);
    staged.insert(staged.begin() + begin, replacement.begin(), replacement.end());
    pairs_ = std::move(staged);
}
namespace {
// MLFields is the shared decoded-field cursor; table colors use ACI/RGB pairs,
// rather than the method-word representation used by multi-leaders.
Color read_table_color(MLFields &f, int code, int rgb, Color fallback, bool row = false) {
    auto a = f.take(code), b = f.take(rgb);
    for (std::size_t n = 0; n < f.values.size(); ++n)
        if (!f.hidden[n]) {
            const auto &p = f.values[n];
            if (p.code == code) {
                MLFields one({p});
                int v = one.i(code, 256);
                if (row && v > 256)
                    v &= 255;
                fallback = Color::from_index(static_cast<std::int16_t>(v));
            } else if (p.code == rgb) {
                if (auto v = detail::as_i32_bits(p.value, false))
                    fallback = Color::from_true_color_value(*v);
            }
        }
    return fallback;
}
void table_color(MLFields &f, int code, int rgb, Color value) {
    f.put_i(code, value.index().value_or(7));
    if (auto v = value.to_true_color_value())
        f.put_i(rgb, static_cast<std::int32_t>(*v));
}
void table_extras(MLFields &f, const std::vector<DxfRawField> &extra) {
    MLFields valid(extra);
    for (const auto &p : extra) {
        if (p.code <= 0 || p.code == 100 || p.code == 101 || p.code >= 1000)
            throw std::invalid_argument("Invalid table extra field");
        f.values.push_back(p);
    }
}
std::vector<std::uint8_t> table_bytes(std::string_view text) {
    std::vector<std::uint8_t> out;
    auto digit = [](char c) {
        return c >= '0' && c <= '9'   ? c - '0'
               : c >= 'a' && c <= 'f' ? c - 'a' + 10
               : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                      : -1;
    };
    for (std::size_t n = 0; n + 1 < text.size(); n += 2) {
        int a = digit(text[n]), b = digit(text[n + 1]);
        if (a >= 0 && b >= 0)
            out.push_back(static_cast<std::uint8_t>(a * 16 + b));
    }
    return out;
}
TableValue table_value(MLFields &f) {
    TableValue v;
    v.type = f.i(90, 0);
    v.flags = f.i(93, 0);
    v.unit = f.i(94, 0);
    v.data_size = f.i(92, 0);
    v.format = f.s(300, {});
    v.formatted = f.s(302, {});
    if (v.unit == 12)
        v.formatted.clear();
    v.handle = f.h(330, 0);
    auto point = Point3{f.r(11, 0), f.r(21, 0), f.r(31, 0)};
    if (!(v.flags & 1))
        v.point = point;
    for (std::size_t n = 0; n < f.values.size(); ++n) {
        if (f.hidden[n])
            continue;
        const auto &p = f.values[n];
        MLFields one({p});
        if (p.code == 91 || p.code == 140) {
            if (!(v.flags & 1))
                v.number = p.code == 91 ? one.i(91, 0) : one.r(140, 0);
            f.used[n] = true;
        }
        if (p.code == 1 || p.code == 2) {
            if (!(v.flags & 1))
                v.text += p.value;
            f.used[n] = true;
        }
        if (p.code == 310) {
            if (!(v.flags & 1)) {
                auto bytes = table_bytes(p.value);
                v.binary.insert(v.binary.end(), bytes.begin(), bytes.end());
            }
            f.used[n] = true;
        }
    }
    v.extra_fields = f.extras();
    return v;
}
void table_value(MLFields &f, const TableValue &v) {
    f.put_i(93, v.flags);
    f.put_i(90, v.type);
    if (!(v.flags & 1))
        switch (v.type) {
        case 0:
        case 1: {
            if (!std::isfinite(v.number))
                throw std::invalid_argument("Nonfinite table integer");
            auto value = std::max(double(INT32_MIN), std::min(double(INT32_MAX), v.number));
            f.put_i(91, static_cast<std::int32_t>(value));
            break;
        }
        case 2:
            f.put_r(140, v.number);
            break;
        case 4:
        case 512:
            f.put_s(1, v.text);
            break;
        case 8: {
            if (v.binary.size() > INT32_MAX)
                throw std::length_error("Table binary value too large");
            f.put_i(92, static_cast<std::int32_t>(v.binary.size()));
            constexpr char digits[] = "0123456789ABCDEF";
            for (std::size_t n = 0; n < v.binary.size(); n += 127) {
                std::string hex;
                for (auto j = n; j < std::min(n + 127, v.binary.size()); ++j) {
                    hex += digits[v.binary[j] >> 4];
                    hex += digits[v.binary[j] & 15];
                }
                f.put_s(310, hex);
            }
            break;
        }
        case 16:
        case 32:
            f.put_i(92, v.data_size ? v.data_size : (v.type == 16 ? 16 : 24));
            f.put_r(11, v.point.x);
            f.put_r(21, v.point.y);
            if (v.type == 32)
                f.put_r(31, v.point.z);
            break;
        case 64:
            f.put_s(330, hex_handle(v.handle));
            break;
        default:
            break;
        }
    f.put_i(94, v.unit);
    f.put_s(300, v.format);
    if (v.unit != 12)
        f.put_s(302, v.formatted);
    table_extras(f, v.extra_fields);
}
constexpr int table_style_codes[] = {170, 283, 63,  64,  7,   140, 69,  65,  66,
                                     68,  279, 275, 276, 278, 289, 285, 286, 288};
TableCell table_cell(const std::vector<DxfRawField> &raw) {
    MLFields all(raw);
    std::vector<DxfRawField> scalar, value;
    bool in_value = false, has_value = false;
    for (std::size_t n = 0; n < raw.size(); ++n) {
        const auto &p = raw[n];
        if (!all.hidden[n] && p.code == 301) {
            in_value = true;
            has_value = true;
            value.clear();
            continue;
        }
        if (!all.hidden[n] && p.code == 304) {
            in_value = false;
            continue;
        }
        (in_value ? value : scalar).push_back(p);
    }
    MLFields f(scalar);
    TableCell c;
    c.type = f.i(171, 1) == 2 ? 2 : 1;
    c.edge_flags = static_cast<std::uint8_t>(f.i(172, 0));
    c.merged = f.i(173, 0);
    c.auto_fit = f.b(174, false);
    c.merge_width = f.i(175, 1);
    c.merge_height = f.i(176, 1);
    c.virtual_edge = f.i(178, 0);
    c.rotation = f.r(145, 0);
    c.block_scale = f.r(144, 1);
    c.flags = f.i(91, f.i(177, 0));
    c.state = f.i(92, 0);
    c.block = f.h(340, 0);
    c.field = f.h(344, 0);
    f.take(179);
    if (has_value) {
        MLFields body(value);
        c.value = table_value(body);
    } else
        for (std::size_t n = 0; n < scalar.size(); ++n)
            if (!f.hidden[n] && (scalar[n].code == 1 || scalar[n].code == 2)) {
                if (!c.value)
                    c.value = TableValue{};
                c.value->type = 4;
                c.value->text += scalar[n].value;
                f.used[n] = true;
            }
    std::optional<std::size_t> pending;
    for (std::size_t n = 0; n < scalar.size(); ++n)
        if (!f.hidden[n]) {
            const auto &p = scalar[n];
            if (p.code == 331) {
                c.attributes.push_back({handle_value(p.value), {}});
                pending = c.attributes.size() - 1;
                f.used[n] = true;
            } else if (p.code == 300 && pending) {
                c.attributes[*pending].text = p.value;
                pending.reset();
                f.used[n] = true;
            }
        }
    std::int32_t overrides = 0;
    for (int bit = 0; bit < 18; ++bit)
        for (auto n : f.take(table_style_codes[bit])) {
            static_cast<void>(n);
            overrides |= 1 << bit;
        }
    for (int code : {421, 422, 423, 424, 426, 427})
        f.take(code);
    bool color_only = false;
    for (std::size_t n = 0; n < scalar.size(); ++n)
        if (!f.hidden[n] && scalar[n].code >= 421 && scalar[n].code <= 427)
            color_only = true;
    if (overrides || color_only) {
        TableCellStyle s;
        s.overrides = overrides;
        s.property_flags = ((overrides & 1) ? 16 : 0) | ((overrides & 6) ? 512 : 0) |
                           ((overrides & 8) ? 32 : 0) | ((overrides & 16) ? 64 : 0) |
                           ((overrides & 32) ? 128 : 0);
        s.alignment = f.i(170, 0);
        s.fill_enabled = !f.b(283, true);
        s.text_style = f.s(7, {});
        s.text_height = f.r(140, .18);
        s.background_color = read_table_color(f, 63, 421, Color::by_block());
        s.content_color = read_table_color(f, 64, 422, Color::by_block());
        for (int i = 0; i < 4; ++i) {
            auto &b = s.borders[i];
            b.color = read_table_color(f, table_style_codes[6 + i], table_style_codes[6 + i] + 358,
                                       Color::by_block());
            b.line_weight = f.i(table_style_codes[10 + i], -1);
            b.invisible = f.b(table_style_codes[14 + i], false);
        }
        c.style = s;
    }
    c.extra_fields = f.extras();
    return c;
}
void table_cell(MLFields &f, const TableCell &c, bool legacy) {
    if (c.type != 1 && c.type != 2)
        throw std::invalid_argument("Invalid table cell type");
    f.put_i(171, c.type);
    f.put_i(172, c.edge_flags);
    f.put_i(173, c.merged);
    f.put_i(174, c.auto_fit);
    f.put_i(175, c.merge_width);
    f.put_i(176, c.merge_height);
    const auto properties = c.style ? c.style->property_flags : 0;
    const int promoted = ((properties & 16) ? 1 : 0) | ((properties & 512) ? 6 : 0) |
                         ((properties & 32) ? 8 : 0) | ((properties & 64) ? 16 : 0) |
                         ((properties & 128) ? 32 : 0);
    const int mask = c.style ? ((legacy || c.style->overrides) ? (c.style->overrides | promoted) : 0x3ffff) : 0;
    f.put_i(91, c.flags | (legacy ? mask : promoted));
    f.put_i(92, c.state);
    f.put_i(178, c.virtual_edge);
    f.put_r(145, c.rotation);
    if (c.type == 2)
        f.put_r(144, c.block_scale);
    if (c.type == 1)
        f.put_h(344, c.field);
    else
        f.put_h(340, c.block);
    if (c.value) {
        f.put_s(301, "CELL_VALUE");
        table_value(f, *c.value);
        f.put_s(304, "ACVALUE_END");
    }
    if (!c.attributes.empty()) {
        if (c.attributes.size() > 32767)
            throw std::length_error("Too many table attributes");
        f.put_i(179, static_cast<int>(c.attributes.size()));
        for (const auto &a : c.attributes) {
            f.put_s(331, hex_handle(a.definition));
            f.put_s(300, a.text);
        }
    }
    if (c.style) {
        const auto &s = *c.style;
        if (mask & 1)
            f.put_i(170, s.alignment);
        if (mask & 2)
            f.put_i(283, !s.fill_enabled);
        if (mask & 4)
            table_color(f, 63, 421, s.background_color);
        if (mask & 8)
            table_color(f, 64, 422, s.content_color);
        if ((mask & 16) && !s.text_style.empty())
            f.put_s(7, s.text_style);
        if (mask & 32)
            f.put_r(140, s.text_height);
        for (int i = 0; i < 4; ++i) {
            const auto &b = s.borders[i];
            if (mask & (0x40 << i))
                table_color(f, table_style_codes[6 + i], table_style_codes[6 + i] + 358, b.color);
            if (mask & (0x400 << i))
                f.put_i(table_style_codes[10 + i], b.line_weight);
            if (mask & (0x4000 << i))
                f.put_i(table_style_codes[14 + i], b.invisible);
        }
    }
    table_extras(f, c.extra_fields);
}
std::size_t table_bit_count(std::int32_t flags, int start, int count) {
    std::size_t n = 0;
    for (int i = start; i < start + count; ++i)
        if (std::uint32_t(flags) & (1u << i))
            ++n;
    return n;
}
TableOverrides table_overrides(MLFields &f) {
    TableOverrides v;
    v.flags = f.i(93, 0);
    v.border_colors = f.i(94, 0);
    v.border_weights = f.i(95, 0);
    v.border_visibility = f.i(96, 0);
    v.title_suppressed = f.b(280, false);
    v.header_suppressed = f.b(281, false);
    v.flow_direction = f.i(70, 0);
    v.horizontal_margin = f.r(40, 0);
    v.vertical_margin = f.r(41, 0);
    auto colors = table_bit_count(v.flags, 5, 3), fills = table_bit_count(v.flags, 11, 3);
    for (std::size_t n = 0; n < f.values.size(); ++n) {
        if (f.hidden[n])
            continue;
        const auto &p = f.values[n];
        MLFields one({p});
        if (p.code == 63 || p.code == 64 || p.code == 65 || p.code == 66 || p.code == 68 || p.code == 69) {
            auto color = Color::from_index(one.i(p.code, 256));
            if (p.code == 64 && v.row_colors.size() < colors)
                v.row_colors.push_back(color);
            else if (p.code == 63 && v.row_fill_colors.size() < fills)
                v.row_fill_colors.push_back(color);
            else
                v.colors.push_back(color);
            f.used[n] = true;
        } else if (p.code >= 274 && p.code <= 279) {
            v.weights.push_back(one.i(p.code, -1));
            f.used[n] = true;
        } else if (p.code >= 284 && p.code <= 289) {
            v.visible.push_back(one.b(p.code, true));
            f.used[n] = true;
        } else if (p.code == 283) {
            v.row_fill_none.push_back(one.b(283, false));
            f.used[n] = true;
        } else if (p.code == 170) {
            v.row_alignments.push_back(one.i(170, 0));
            f.used[n] = true;
        } else if (p.code == 7) {
            v.text_styles.push_back(p.value);
            f.used[n] = true;
        } else if (p.code == 140) {
            v.row_text_heights.push_back(one.r(140, 0));
            f.used[n] = true;
        }
    }
    return v;
}
void table_overrides(MLFields &f, const TableOverrides &v) {
    f.put_i(93, v.flags);
    f.put_i(94, v.border_colors);
    f.put_i(95, v.border_weights);
    f.put_i(96, v.border_visibility);
    if (v.flags & 1)
        f.put_i(280, v.title_suppressed);
    if (v.flags & 2)
        f.put_i(281, v.header_suppressed);
    if (v.flags & 4)
        f.put_i(70, v.flow_direction);
    if (v.flags & 8)
        f.put_r(40, v.horizontal_margin);
    if (v.flags & 16)
        f.put_r(41, v.vertical_margin);
    auto group = [&](int start, auto emit) {
        std::size_t at = 0;
        for (int i = start; i < start + 3; ++i)
            if (v.flags & (1 << i))
                emit(at++);
    };
    group(5, [&](auto n) {
        auto c = n < v.row_colors.size() ? v.row_colors[n] : Color::by_block();
        f.put_i(64, c.index().value_or(7));
    });
    group(8, [&](auto n) { f.put_i(283, n < v.row_fill_none.size() ? v.row_fill_none[n] : false); });
    group(11, [&](auto n) {
        auto c = n < v.row_fill_colors.size() ? v.row_fill_colors[n] : Color::by_block();
        f.put_i(63, c.index().value_or(7));
    });
    group(14, [&](auto n) { f.put_i(170, n < v.row_alignments.size() ? v.row_alignments[n] : 0); });
    group(17, [&](auto n) { f.put_s(7, n < v.text_styles.size() ? v.text_styles[n] : "Standard"); });
    group(20, [&](auto n) { f.put_r(140, n < v.row_text_heights.size() ? v.row_text_heights[n] : 0); });
    const int codes[] = {64, 65, 66, 63, 68, 69};
    std::size_t a = 0, b = 0, c = 0;
    for (int i = 0; i < 18; ++i) {
        if (v.border_colors & (1 << i)) {
            auto color = a < v.colors.size() ? v.colors[a] : Color::by_block();
            f.put_i(codes[i % 6], color.index().value_or(7));
            ++a;
        }
        if (v.border_weights & (1 << i)) {
            f.put_i(274 + i % 6, b < v.weights.size() ? v.weights[b] : -1);
            ++b;
        }
        if (v.border_visibility & (1 << i)) {
            f.put_i(284 + i % 6, c < v.visible.size() ? v.visible[c] : true);
            ++c;
        }
    }
}
TableRowStyle table_row_style(const std::vector<DxfRawField> &raw) {
    MLFields f(raw);
    TableRowStyle v;
    v.text_style = f.s(7, {});
    v.text_height = f.r(140, .18);
    v.alignment = f.e(170, 5, 1, 9);
    v.fill_enabled = f.b(283, false);
    v.text_color = read_table_color(f, 62, 420, Color::by_block(), true);
    v.fill_color = read_table_color(f, 63, 421, Color::from_index(7), true);
    v.data_type = f.i(90, 512);
    v.unit_type = f.i(91, 0);
    v.format = f.s(1, {});
    for (int i = 0; i < 6; ++i) {
        v.borders[i].color = read_table_color(f, 64 + i, 422 + i, Color::by_block());
        v.borders[i].line_weight = f.i(274 + i, -2);
        v.borders[i].invisible = !f.b(284 + i, true);
    }
    v.extra_fields = f.extras();
    return v;
}
void table_row_style(MLFields &f, const TableRowStyle &v) {
    if (!v.text_style.empty())
        f.put_s(7, v.text_style);
    f.put_r(140, v.text_height);
    f.put_i(170, v.alignment);
    table_color(f, 62, 420, v.text_color);
    table_color(f, 63, 421, v.fill_color);
    f.put_i(283, v.fill_enabled);
    f.put_i(90, v.data_type);
    f.put_i(91, v.unit_type);
    f.put_s(1, v.format);
    for (int i = 0; i < 6; ++i) {
        f.put_i(274 + i, v.borders[i].line_weight);
        f.put_i(284 + i, !v.borders[i].invisible);
        table_color(f, 64 + i, 422 + i, v.borders[i].color);
    }
    table_extras(f, v.extra_fields);
}
} // namespace
TableDefinition DxfTable::definition() const {
    TableDefinition v;
    MLFields base(subclass_fields("AcDbBlockReference"));
    v.insertion = base.p(10, {});
    v.normal = base.p(210, {0, 0, 1});
    v.block_name = base.s(2, {});
    auto raw = subclass_fields("AcDbTable");
    MLFields all(raw);
    std::vector<DxfRawField> header;
    std::vector<std::vector<DxfRawField>> cells;
    bool overrides = false;
    for (std::size_t n = 0; n < raw.size(); ++n) {
        const auto &p = raw[n];
        if (!all.hidden[n] && p.code == 171)
            cells.emplace_back();
        if (cells.empty() && !all.hidden[n]) {
            if (p.code == 93)
                overrides = true;
            if (p.code == 280 && !overrides) {
                MLFields one({p});
                v.data_version = one.i(280, 0);
                continue;
            }
        }
        (cells.empty() ? header : cells.back()).push_back(p);
    }
    MLFields f(header);
    v.style = f.h(342, 0);
    v.block_record = f.h(343, 0);
    v.horizontal_direction = f.p(11, {1, 0, 0});
    v.value_flags = f.i(90, 0);
    auto rows = std::max(0, f.i(91, 0)), columns = std::max(0, f.i(92, 0));
    if (rows > 1000000 || columns > 1000000)
        throw std::length_error("TABLE dimensions exceed supported allocation limit");
    for (auto n : f.take(141)) {
        MLFields one({f.values[n]});
        v.row_heights.push_back(one.r(141, 0));
    }
    for (auto n : f.take(142)) {
        MLFields one({f.values[n]});
        v.column_widths.push_back(one.r(142, 0));
    }
    v.row_heights.resize(std::max(v.row_heights.size(), std::size_t(rows)), 0);
    v.cells.resize(v.row_heights.size());
    if (columns == 0)
        columns = static_cast<int>(v.column_widths.size());
    for (std::size_t n = 0; n < cells.size(); ++n) {
        const auto row = columns ? n / std::size_t(columns) : 0;
        if (row < v.cells.size())
            v.cells[row].push_back(table_cell(cells[n]));
    }
    for (std::size_t r = 0; r < v.cells.size(); ++r)
        for (std::size_t c = 0; c < v.cells[r].size() && c < v.column_widths.size(); ++c) {
            const auto &cell = v.cells[r][c];
            if (cell.merge_width > 1 || cell.merge_height > 1)
                v.merged_ranges.push_back(
                    {r, c,
                     std::min(v.cells.size() - 1, r + std::size_t(std::max<int>(1, cell.merge_height)) - 1),
                     std::min(v.column_widths.size() - 1,
                              c + std::size_t(std::max<int>(1, cell.merge_width)) - 1)});
        }
    v.overrides = table_overrides(f);
    v.extra_fields = f.extras();
    return v;
}
void DxfTable::set_definition(const TableDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Full ACAD_TABLE editing requires R2007 or later");
    if (v.cells.size() != v.row_heights.size() || v.row_heights.size() > 1000000 ||
        v.column_widths.size() > 32767)
        throw std::invalid_argument("Invalid TABLE dimensions");
    for (const auto &row : v.cells)
        if (row.size() != v.column_widths.size())
            throw std::invalid_argument("TABLE cells must form a rectangle");
    for (const auto &r : v.merged_ranges)
        if (r.top > r.bottom || r.left > r.right || r.bottom >= v.cells.size() ||
            r.right >= v.column_widths.size() || r.bottom - r.top >= 32767)
            throw std::invalid_argument("Invalid TABLE merge range");
    MLFields f;
    f.put_h(342, v.style);
    f.put_h(343, v.block_record);
    f.put_i(280, v.data_version);
    f.put_p(11, v.horizontal_direction);
    f.put_i(90, v.value_flags);
    f.put_i(91, static_cast<int>(v.row_heights.size()));
    f.put_i(92, static_cast<int>(v.column_widths.size()));
    table_overrides(f, v.overrides);
    for (auto h : v.row_heights)
        f.put_r(141, h);
    for (auto w : v.column_widths)
        f.put_r(142, w);
    table_extras(f, v.extra_fields);
    for (std::size_t r = 0; r < v.cells.size(); ++r)
        for (std::size_t c = 0; c < v.cells[r].size(); ++c) {
            auto cell = v.cells[r][c];
            for (const auto &range : v.merged_ranges)
                if (r >= range.top && r <= range.bottom && c >= range.left && c <= range.right) {
                    const bool anchor = r == range.top && c == range.left;
                    cell.merged = !anchor;
                    if (anchor) {
                        cell.merge_height = static_cast<std::int16_t>(range.bottom - range.top + 1);
                        cell.merge_width = static_cast<std::int16_t>(range.right - range.left + 1);
                    }
                    break;
                }
            table_cell(f, cell, version_ < DxfVersion::AC1024);
        }
    auto staged = *this;
    staged.set_subclass_fields("AcDbTable", f.values);
    staged.set_point("AcDbBlockReference", 10, v.insertion);
    staged.set_point("AcDbBlockReference", 210, v.normal);
    staged.set_text_field("AcDbBlockReference", 2, v.block_name);
    *this = std::move(staged);
}
void DxfTable::set_merged_ranges(const std::vector<TableRange> &ranges) {
    auto v = definition();
    for (auto &row : v.cells)
        for (auto &cell : row) {
            cell.merged = 0;
            cell.merge_width = cell.merge_height = 1;
        }
    v.merged_ranges = ranges;
    set_definition(v);
}
TableStyleDefinition DxfTableStyle::definition() const {
    auto raw = subclass_fields("AcDbTableStyle");
    MLFields all(raw);
    std::vector<DxfRawField> head;
    std::vector<std::vector<DxfRawField>> rows;
    bool named = false;
    int last = 0;
    TableStyleDefinition v;
    for (std::size_t n = 0; n < raw.size(); ++n) {
        const auto &p = raw[n];
        if (!all.hidden[n]) {
            if ((p.code == 7 || (p.code == 140 && last != 7)) && rows.size() < 3)
                rows.emplace_back();
            last = p.code;
            if (p.code == 3)
                named = true;
            if (p.code == 280 && !named) {
                MLFields one({p});
                v.version = one.i(280, 0);
                continue;
            }
        }
        (rows.empty() ? head : rows.back()).push_back(p);
    }
    MLFields f(head);
    v.description = f.s(3, {});
    v.flow_direction = f.e(70, 0, 0, 1);
    v.flags = static_cast<std::int16_t>(f.i(71, 0));
    v.horizontal_margin = f.r(40, .06);
    v.vertical_margin = f.r(41, .06);
    v.title_suppressed = f.b(280, false) || (v.flags & 1);
    v.header_suppressed = f.b(281, false) || (v.flags & 2);
    v.flags |= (v.title_suppressed ? 1 : 0) | (v.header_suppressed ? 2 : 0);
    if (!rows.empty())
        v.data = table_row_style(rows[0]);
    if (rows.size() > 1)
        v.title = table_row_style(rows[1]);
    if (rows.size() > 2)
        v.header = table_row_style(rows[2]);
    v.extra_fields = f.extras();
    return v;
}
void DxfTableStyle::set_definition(const TableStyleDefinition &v) {
    MLFields f;
    f.put_i(280, v.version);
    f.put_s(3, v.description);
    f.put_i(70, v.flow_direction);
    f.put_i(71, v.flags | (v.title_suppressed ? 1 : 0) | (v.header_suppressed ? 2 : 0));
    f.put_r(40, v.horizontal_margin);
    f.put_r(41, v.vertical_margin);
    f.put_i(280, v.title_suppressed || (v.flags & 1));
    f.put_i(281, v.header_suppressed || (v.flags & 2));
    table_extras(f, v.extra_fields);
    table_row_style(f, v.data);
    table_row_style(f, v.title);
    table_row_style(f, v.header);
    set_subclass_fields("AcDbTableStyle", f.values);
}
const DxfTableStyle *DxfDocument::get_table_style(std::string_view name) const {
    const auto dictionary = named_object_dictionary("ACAD_TABLESTYLE");
    if (dictionary.empty())
        return nullptr;
    const auto *dict = get_record(dictionary);
    std::string handle;
    bool match = false;
    dict->locate_if("AcDbDictionary", [&](std::size_t at, int code) {
        const auto &p = dict->pairs_[at];
        if (code == 3)
            match = detail::symbol_name_key(dict->decode_string(p.value)) == detail::symbol_name_key(name);
        else if (match && (code == 350 || code == 360)) {
            if (!handle.empty())
                throw std::invalid_argument("Ambiguous TABLESTYLE name");
            handle = p.value;
            match = false;
        }
        return false;
    });
    if (handle.empty())
        return nullptr;
    const auto matches = records_with_handle(handle);
    if (matches.size() != 1)
        throw std::invalid_argument("Invalid TABLESTYLE reference");
    const DxfTableStyle *result = nullptr;
    for_each<DxfTableStyle>([&](const auto &r) {
        if (handle_value(r.handle()) == handle_value(handle))
            result = &r;
    });
    if (!result)
        throw std::invalid_argument("Wrong TABLESTYLE entry type");
    return result;
}
std::uint64_t DxfDocument::add_table_style(std::string name, const TableStyleDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Full TABLESTYLE construction requires R2007 or later");
    valid_symbol_name(name);
    if (get_table_style(name))
        throw std::invalid_argument("TABLESTYLE already exists");
    for (const auto *row : {&v.data, &v.title, &v.header})
        if (!row->text_style.empty() && !get_symbol(SymbolTable::TextStyle, row->text_style))
            throw std::invalid_argument("Missing table text style");
    auto dictionary = named_object_dictionary("ACAD_TABLESTYLE");
    if (dictionary.empty())
        throw std::invalid_argument("Missing ACAD_TABLESTYLE dictionary");
    DxfDocument staged = *this;
    DxfTableStyle style;
    static_cast<DxfRecord &>(style) = staged.make_record("TABLESTYLE", "AcDbTableStyle");
    style.set_definition(v);
    auto id = staged.add_dictionary_object(dictionary, std::move(name), std::move(style));
    *this = std::move(staged);
    return id;
}
std::uint64_t DxfDocument::add_table(const TableDefinition &definition, const EntityOptions &options) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Full ACAD_TABLE construction requires R2007 or later");
    auto v = definition;
    if (!v.style)
        if (const auto *s = get_table_style("Standard"))
            v.style = handle_value(s->handle());
    mleader_check_handle(*this, v.style, "TABLESTYLE");
    mleader_check_handle(*this, v.block_record, "BLOCK_RECORD");
    if (!v.block_name.empty() && !get_symbol(SymbolTable::BlockRecord, v.block_name))
        throw std::invalid_argument("Missing table graphics block");
    if (v.block_record) {
        const auto *block = block_record(hex_handle(v.block_record));
        if (!block)
            throw std::invalid_argument("Missing table block record");
        if (!v.block_name.empty() &&
            detail::symbol_name_key(block->name()) != detail::symbol_name_key(v.block_name))
            throw std::invalid_argument("Conflicting table graphics block references");
        v.block_name = block->name();
    }
    for (const auto &row : v.cells)
        for (const auto &c : row) {
            mleader_check_handle(*this, c.block, "BLOCK_RECORD");
            mleader_check_handle(*this, c.field, "FIELD");
            for (const auto &a : c.attributes)
                mleader_check_handle(*this, a.definition, "ATTDEF");
            if (c.style && !c.style->text_style.empty() &&
                !get_symbol(SymbolTable::TextStyle, c.style->text_style))
                throw std::invalid_argument("Missing table cell text style");
        }
    DxfDocument staged = *this;
    if (!v.block_record) {
        if (v.block_name.empty()) {
            std::size_t n = 1;
            do {
                v.block_name = "*T" + std::to_string(n++);
            } while (staged.get_symbol(SymbolTable::BlockRecord, v.block_name));
            v.block_record = staged.add_symbol_impl(SymbolTable::BlockRecord, v.block_name, {}, true);
            staged.for_each<DxfBlock>([&](auto &b) {
                if (b.name() == v.block_name)
                    b.set_flags(b.flags() | 1);
            });
        } else
            v.block_record = handle_value(staged.get_symbol(SymbolTable::BlockRecord, v.block_name)->handle);
    }
    DxfTable entity;
    static_cast<DxfEntity &>(entity) = staged.make_entity("ACAD_TABLE", "AcDbBlockReference", options);
    entity.pairs_.push_back({100, "AcDbTable"});
    entity.set_definition(v);
    for (const auto &p : entity.pairs_)
        if (pointer_code(p.code) && handle_value(p.value) && staged.records_with_handle(p.value).size() != 1)
            throw std::invalid_argument("Missing table reference");
    const auto id = staged.insert_entity(std::move(entity), options);
    *this = std::move(staged);
    return id;
}

void DxfTable::transform_table_fields(Point3 direction, double scale) {
    finite(scale);
    // Missing header components must be inserted before the first cell; a
    // same-code value inside a cell is not the table's horizontal direction.
    const double components[] = {direction.x, direction.y, direction.z};
    for (int axis = 0; axis < 3; ++axis) {
        const int code = 11 + 10 * axis;
        auto value = encode_number(components[axis]);
        auto at = locate_if("AcDbTable", [&](std::size_t, int candidate) {
                      return candidate == code || candidate == 171;
                  }).index;
        if (at < pairs_.size() && pairs_[at].code == code)
            pairs_[at].value = std::move(value);
        else
            pairs_.insert(pairs_.begin() + at, {code, std::move(value)});
    }
    // Row/column dimensions precede the first cell. Identical codes inside
    // cells and nested application data must retain their authored values.
    locate_if("AcDbTable", [&](std::size_t at, int code) {
        if (code == 171)
            return true;
        if (code == 141 || code == 142)
            pairs_[at].value = encode_number(number_at(at) * scale);
        return false;
    });
}
