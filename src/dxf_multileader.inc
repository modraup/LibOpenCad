// SPDX-License-Identifier: MPL-2.0
// Port of acadrust MULTILEADER nested DXF reader/writer.
namespace {
using MLRaw = MultiLeaderRawField;
struct MLFields {
    std::vector<MLRaw> values;
    std::vector<bool> used, hidden;
    explicit MLFields(std::vector<MLRaw> raw = {})
        : values(std::move(raw)), used(values.size()), hidden(values.size()) {
        unsigned depth = 0;
        for (std::size_t n = 0; n < values.size(); ++n) {
            const auto &p = values[n];
            if (p.code == 102) {
                hidden[n] = true;
                if (!p.value.empty() && p.value.front() == '{')
                    ++depth;
                else if (p.value == "}") {
                    if (!depth)
                        throw std::invalid_argument("Unbalanced MULTILEADER application group");
                    --depth;
                }
            } else
                hidden[n] = depth != 0;
        }
        if (depth)
            throw std::invalid_argument("Unterminated MULTILEADER application group");
    }
    std::vector<std::size_t> take(int code) {
        std::vector<std::size_t> found;
        for (std::size_t n = 0; n < values.size(); ++n)
            if (!hidden[n] && values[n].code == code) {
                used[n] = true;
                found.push_back(n);
            }
        return found;
    }
    double r(int code, double fallback) {
        for (auto n : take(code)) {
            try {
                std::size_t end = 0;
                auto text = std::string(trim(values[n].value));
                auto value = std::stod(text, &end);
                if (end == text.size())
                    fallback = value;
            } catch (const std::exception &) {
            }
        }
        return fallback;
    }
    std::int32_t i(int code, std::int32_t fallback) {
        for (auto n : take(code))
            if (auto value = detail::as_i32_bits(values[n].value, false))
                fallback = static_cast<std::int32_t>(*value);
        return fallback;
    }
    std::int16_t e(int code, std::int16_t fallback, int low, int high) {
        auto value = i(code, fallback);
        return value >= low && value <= high ? static_cast<std::int16_t>(value) : fallback;
    }
    bool b(int code, bool fallback) { return i(code, fallback) != 0; }
    std::uint64_t h(int code, std::uint64_t fallback) {
        for (auto n : take(code))
            if (auto h = parse_handle(values[n].value))
                fallback = *h;
        return fallback;
    }
    std::string s(int code, std::string fallback) {
        for (auto n : take(code))
            fallback = values[n].value;
        return fallback;
    }
    Point3 p(int code, Point3 fallback) {
        auto x = take(code), y = take(code + 10);
        take(code + 20);
        if (x.empty() || y.empty())
            return fallback;
        return {r(code, 0), r(code + 10, 0), r(code + 20, 0)};
    }
    Color c(int code, Color fallback) {
        for (auto at : take(code))
            if (auto n = detail::as_i32_bits(values[at].value, false)) {
                switch (*n >> 24) {
                case 0xc0:
                    fallback = Color::by_layer();
                    break;
                case 0xc1:
                    fallback = Color::by_block();
                    break;
                case 0xc2:
                    fallback = Color::from_true_color_value(*n);
                    break;
                case 0xc3:
                    fallback = Color::from_index(*n & 255);
                    break;
                case 0xc8:
                    fallback = Color::none();
                    break;
                default:
                    fallback = *n <= 256 ? Color::from_index(static_cast<std::int16_t>(*n))
                                         : Color::from_true_color_value(*n);
                    break;
                }
            }
        return fallback;
    }
    void put_i(int code, std::int32_t value) { values.push_back({code, std::to_string(value)}); }
    void put_b(int code, bool value) { put_i(code, value); }
    void put_h(int code, std::uint64_t value) {
        if (value)
            values.push_back({code, hex_handle(value)});
    }
    void put_s(int code, const std::string &value) { values.push_back({code, value}); }
    void put_r(int code, double value) {
        if (!std::isfinite(value))
            throw std::invalid_argument("Nonfinite MULTILEADER field");
        std::ostringstream out;
        out.precision(17);
        out << value;
        values.push_back({code, out.str()});
    }
    void put_p(int code, Point3 value) {
        put_r(code, value.x);
        put_r(code + 10, value.y);
        put_r(code + 20, value.z);
    }
    void put_c(int code, Color value) {
        std::uint32_t n = 0;
        switch (value.kind()) {
        case Color::Kind::ByLayer:
            n = 0xc0000000;
            break;
        case Color::Kind::ByBlock:
            n = 0xc1000000;
            break;
        case Color::Kind::None:
            n = 0xc8000000;
            break;
        case Color::Kind::Index:
            n = 0xc3000000 | *value.index();
            break;
        case Color::Kind::Rgb:
            n = 0xc2000000 | *value.to_true_color_value();
            break;
        }
        put_i(code, static_cast<std::int32_t>(n));
    }
    std::vector<MLRaw> extras() const {
        std::vector<MLRaw> out;
        for (std::size_t n = 0; n < values.size(); ++n)
            if (!used[n])
                out.push_back(values[n]);
        return out;
    }
    void append(const std::vector<MLRaw> &raw) {
        MLFields check(raw);
        for (std::size_t n = 0; n < raw.size(); ++n) {
            const auto &p = raw[n];
            if (p.code <= 0 || p.code == 100 || p.code == 101 || p.code >= 1000 ||
                (!check.hidden[n] && p.code >= 300 && p.code <= 305))
                throw std::invalid_argument("Invalid MULTILEADER extra field");
            values.push_back(p);
        }
    }
};
#include "dxf_multileader_fields.inc"
struct MLNode {
    std::vector<MLRaw> fields;
    std::vector<MLNode> children;
};
MLNode mleader_node(const std::vector<MLRaw> &raw, std::size_t &cursor, int level) {
    MLNode result;
    unsigned depth = 0;
    const int end = level == 1 ? 301 : level == 2 ? 303 : 305;
    while (cursor < raw.size()) {
        const auto &p = raw[cursor++];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (p.value == "}" && depth)
                --depth;
            result.fields.push_back(p);
            continue;
        }
        if (depth) {
            result.fields.push_back(p);
            continue;
        }
        if (level && p.code == end)
            return result;
        const bool child = (level == 0 && p.code == 300 && p.value.rfind("CONTEXT_DATA", 0) == 0) ||
                           (level == 1 && p.code == 302 && p.value.rfind("LEADER", 0) == 0) ||
                           (level == 2 && p.code == 304 && p.value.rfind("LEADER_LINE", 0) == 0);
        if (child)
            result.children.push_back(mleader_node(raw, cursor, level + 1));
        else
            result.fields.push_back(p);
    }
    // Match the upstream tolerant reader for truncated nesting; setters emit complete markers.
    return result;
}
std::vector<MultiLeaderBreakPair> mleader_break_pairs(MLFields &fields, int code) {
    std::vector<MultiLeaderBreakPair> out;
    std::vector<MLRaw> current;
    auto flush = [&] {
        if (current.empty())
            return;
        MLFields p(current);
        const auto a = p.take(code), b = p.take(code + 10), c = p.take(code + 1), d = p.take(code + 11);
        if (!a.empty() && !b.empty() && !c.empty() && !d.empty())
            out.push_back({p.p(code, {}), p.p(code + 1, {})});
        current.clear();
    };
    for (std::size_t n = 0; n < fields.values.size(); ++n) {
        const auto &p = fields.values[n];
        if (fields.hidden[n])
            continue;
        if (p.code == code)
            flush();
        if (p.code == code || p.code == code + 10 || p.code == code + 20 || p.code == code + 1 ||
            p.code == code + 11 || p.code == code + 21) {
            current.push_back(p);
            fields.used[n] = true;
        }
    }
    flush();
    return out;
}
MultiLeaderLine mleader_line(const MLNode &node, std::int32_t index) {
    MultiLeaderLine v;
    v.index = index;
    MLFields f(node.fields);
    mleader_read_fields(f, v);
    std::optional<std::int32_t> segment;
    std::vector<MLRaw> segment_fields;
    auto flush = [&] {
        if (!segment)
            return;
        MLFields part(segment_fields);
        v.breaks.push_back({*segment, mleader_break_pairs(part, 11)});
        segment_fields.clear();
    };
    for (std::size_t n = 0; n < f.values.size(); ++n) {
        if (f.hidden[n])
            continue;
        const auto &p = f.values[n];
        if (p.code == 10) {
            MLFields point({p});
            v.points.push_back({point.r(10, 0), 0, 0});
            f.used[n] = true;
        } else if (p.code == 20 || p.code == 30) {
            if (!v.points.empty()) {
                MLFields point({p});
                (p.code == 20 ? v.points.back().y : v.points.back().z) = point.r(p.code, 0);
            }
            f.used[n] = true;
        } else if (p.code == 90) {
            flush();
            MLFields value({p});
            segment = value.i(90, 0);
            f.used[n] = true;
        } else if (p.code == 11 || p.code == 21 || p.code == 31 || p.code == 12 || p.code == 22 ||
                   p.code == 32) {
            if (p.code == 11 && !segment)
                segment = 0;
            segment_fields.push_back(p);
            f.used[n] = true;
        }
    }
    flush();
    v.extra_fields = f.extras();
    return v;
}
MultiLeaderContext mleader_context(const MLNode &node, MultiLeaderContext v = {}) {
    MLFields f(node.fields);
    mleader_read_fields(f, v);
    if (v.text_flow_direction != 1 && v.text_flow_direction != 3 && v.text_flow_direction != 5)
        v.text_flow_direction = 1;
    for (auto n : f.take(144)) {
        MLFields value({f.values[n]});
        v.column_sizes.push_back(value.r(144, 0));
    }
    std::size_t i = 0;
    for (auto n : f.take(47)) {
        if (i == 16)
            break;
        MLFields value({f.values[n]});
        v.transform_matrix[i++] = value.r(47, 0);
    }
    for (const auto &node : node.children) {
        MultiLeaderRoot root;
        MLFields fields(node.fields);
        mleader_read_fields(fields, root);
        root.break_points = mleader_break_pairs(fields, 12);
        root.extra_fields = fields.extras();
        for (const auto &line : node.children)
            root.lines.push_back(mleader_line(line, static_cast<std::int32_t>(root.lines.size())));
        v.leader_roots.push_back(std::move(root));
    }
    v.extra_fields = f.extras();
    return v;
}
void mleader_write_context(MLFields &out, const MultiLeaderContext &v) {
    out.put_s(300, "CONTEXT_DATA{");
    mleader_write_fields(out, v);
    for (const auto &root : v.leader_roots) {
        out.put_s(302, "LEADER{");
        mleader_write_fields(out, root);
        for (const auto &p : root.break_points) {
            out.put_p(12, p.start);
            out.put_p(13, p.end);
        }
        for (const auto &line : root.lines) {
            out.put_s(304, "LEADER_LINE{");
            for (const auto &p : line.points)
                out.put_p(10, p);
            for (const auto &segment : line.breaks) {
                out.put_i(90, segment.segment_index);
                for (const auto &p : segment.points) {
                    out.put_p(11, p.start);
                    out.put_p(12, p.end);
                }
            }
            mleader_write_fields(out, line);
            out.append(line.extra_fields);
            out.put_s(305, "}");
        }
        out.append(root.extra_fields);
        out.put_s(303, "}");
    }
    out.append(v.extra_fields);
    out.put_s(301, "}");
}
} // namespace
std::pair<std::size_t, std::size_t> DxfMultiLeader::scope_range() const {
    unsigned depth = 0;
    for (std::size_t n = 1; n < pairs_.size(); ++n) {
        const auto &p = pairs_[n];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (depth && p.value == "}")
                --depth;
            continue;
        }
        if (depth)
            continue;
        if (p.code == 100 && p.value == "AcDbMLeader")
            return {n + 1, locate_if("AcDbMLeader", [](std::size_t, int) { return false; }).index};
    }
    throw std::invalid_argument("Missing MULTILEADER subclass");
}
MultiLeaderDefinition DxfMultiLeader::definition() const {
    const auto range = scope_range();
    std::vector<MLRaw> raw;
    for (auto n = range.first; n < range.second; ++n) {
        const auto &p = pairs_[n];
        raw.push_back({p.code, detail::is_string_value(p.code)
                                   ? decode_string(p.value)
                                   : (binary_ ? detail::text_value_from_binary(p.code, p.value) : p.value)});
    }
    std::size_t cursor = 0;
    auto node = mleader_node(raw, cursor, 0);
    MLFields f(node.fields);
    MultiLeaderDefinition v;
    mleader_read_fields(f, v);
    for (const auto &context : node.children)
        v.context = mleader_context(context, std::move(v.context));
    std::optional<MultiLeaderBlockAttribute> attr;
    for (std::size_t n = 0; n < f.values.size(); ++n) {
        if (f.hidden[n])
            continue;
        const auto &p = f.values[n];
        MLFields one({p});
        if (p.code == 94) {
            v.arrowhead_overrides.push_back({one.i(94, 0), 0});
            f.used[n] = true;
        } else if (p.code == 345) {
            if (!v.arrowhead_overrides.empty())
                v.arrowhead_overrides.back().arrowhead_handle = one.h(345, 0);
            f.used[n] = true;
        } else if (p.code == 330) {
            if (attr)
                v.block_attributes.push_back(*attr);
            attr = MultiLeaderBlockAttribute{};
            attr->definition_handle = one.h(330, 0);
            f.used[n] = true;
        } else if (attr && p.code == 177) {
            attr->index = static_cast<std::int16_t>(one.i(177, 0));
            f.used[n] = true;
        } else if (attr && p.code == 44) {
            attr->width = one.r(44, 0);
            f.used[n] = true;
        } else if (attr && p.code == 302) {
            attr->text = p.value;
            v.block_attributes.push_back(*attr);
            attr.reset();
            f.used[n] = true;
        } else if (p.code == 44) {
            // Lenient entity-level height, distinct from a block attribute width.
            v.text_height = one.r(44, .18);
            f.used[n] = true;
        }
    }
    if (attr)
        v.block_attributes.push_back(*attr);
    v.extra_fields = f.extras();
    v.text_height = text_height_.value_or(v.text_height);
    if (inactive_geometry_) {
        const auto &g = *inactive_geometry_;
        auto &c = v.context;
        if (!c.has_text_contents) {
            c.text_normal = g.text_normal;
            c.text_location = g.text_location;
            c.text_direction = g.text_direction;
            c.text_rotation = g.text_rotation;
            c.text_width = g.text_width;
            c.text_boundary_height = g.text_boundary_height;
        }
        if (!c.has_block_contents) {
            c.block_content_location = g.block_location;
            c.block_content_scale = g.block_scale;
            c.block_rotation = g.block_rotation;
        }
    }
    return v;
}
void DxfMultiLeader::set_definition(const MultiLeaderDefinition &v) {
    MLFields out;
    mleader_write_context(out, v.context);
    mleader_write_fields(out, v);
    for (const auto &arrow : v.arrowhead_overrides) {
        out.put_i(94, arrow.index);
        out.put_s(345, hex_handle(arrow.arrowhead_handle));
    }
    for (const auto &attr : v.block_attributes) {
        out.put_s(330, hex_handle(attr.definition_handle));
        out.put_i(177, attr.index);
        out.put_r(44, attr.width);
        out.put_s(302, attr.text);
    }
    out.append(v.extra_fields);
    const auto range = scope_range();
    std::vector<Pair> values;
    for (const auto &p : out.values) {
        auto binary = detail::binary_value_from_text(p.code, p.value);
        values.push_back({p.code, detail::is_string_value(p.code) ? encode_string(p.value)
                                                                  : (binary_ ? std::move(binary) : p.value)});
    }
    const auto &c = v.context;
    for (auto p :
         {c.text_normal, c.text_location, c.text_direction, c.block_content_location, c.block_content_scale})
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            throw std::invalid_argument("Nonfinite multileader content geometry");
    for (double n : {v.text_height, c.text_rotation, c.text_width, c.text_boundary_height, c.block_rotation})
        if (!std::isfinite(n))
            throw std::invalid_argument("Nonfinite multileader content size or angle");
    auto staged = *this;
    staged.text_height_ = v.text_height;
    staged.inactive_geometry_ =
        InactiveGeometry{c.text_normal,         c.text_location, c.text_direction, c.block_content_location,
                         c.block_content_scale, c.text_rotation, c.text_width,     c.text_boundary_height,
                         c.block_rotation};
    staged.pairs_.erase(staged.pairs_.begin() + range.first, staged.pairs_.begin() + range.second);
    staged.pairs_.insert(staged.pairs_.begin() + range.first, values.begin(), values.end());
    *this = std::move(staged);
}
namespace {
void mleader_check_handle(const DxfDocument &doc, std::uint64_t handle, std::string_view type) {
    if (!handle)
        return;
    const auto matches = doc.records_with_handle(hex_handle(handle));
    if (matches.size() != 1 || matches.front()->type() != type)
        throw std::invalid_argument("MULTILEADER reference must resolve to " + std::string(type));
}
} // namespace
std::uint64_t DxfDocument::add_multileader(const MultiLeaderDefinition &definition,
                                           const EntityOptions &options) {
    if (version_ < DxfVersion::AC1027)
        throw std::invalid_argument("Full MULTILEADER construction requires R2013 or later");
    auto v = definition;
    if (!v.style_handle)
        if (const auto *style = get_multileader_style("Standard"))
            v.style_handle = handle_value(style->handle());
    mleader_check_handle(*this, v.style_handle, "MLEADERSTYLE");
    mleader_check_handle(*this, v.line_type_handle, "LTYPE");
    mleader_check_handle(*this, v.text_style_handle, "STYLE");
    mleader_check_handle(*this, v.block_content_handle, "BLOCK_RECORD");
    mleader_check_handle(*this, v.arrowhead_handle, "BLOCK_RECORD");
    mleader_check_handle(*this, v.context.text_style_handle, "STYLE");
    mleader_check_handle(*this, v.context.block_content_handle, "BLOCK_RECORD");
    for (const auto &root : v.context.leader_roots)
        for (const auto &line : root.lines) {
            mleader_check_handle(*this, line.line_type_handle, "LTYPE");
            mleader_check_handle(*this, line.arrowhead_handle, "BLOCK_RECORD");
        }
    for (const auto &arrow : v.arrowhead_overrides)
        mleader_check_handle(*this, arrow.arrowhead_handle, "BLOCK_RECORD");
    for (const auto &attr : v.block_attributes)
        mleader_check_handle(*this, attr.definition_handle, "ATTDEF");
    if (!v.line_type_handle) {
        const auto *type = get_linetype("ByBlock");
        if (type)
            v.line_type_handle = handle_value(type->handle());
    }
    DxfMultiLeader entity;
    static_cast<DxfEntity &>(entity) = make_entity("MULTILEADER", "AcDbMLeader", options);
    entity.set_definition(v);
    // All non-null handles, including nested overrides/attributes, must resolve.
    for (const auto &p : entity.pairs_)
        if (pointer_code(p.code) && handle_value(p.value) && records_with_handle(p.value).size() != 1)
            throw std::invalid_argument("Missing MULTILEADER reference");
    return insert_entity(std::move(entity), options);
}
MultiLeaderStyleDefinition DxfMultiLeaderStyle::definition() const {
    std::vector<MLRaw> raw;
    locate_if("AcDbMLeaderStyle", [&](std::size_t at, int code) {
        const auto &p = pairs_[at];
        raw.push_back({code, detail::is_string_value(code)
                                 ? decode_string(p.value)
                                 : (binary_ ? detail::text_value_from_binary(code, p.value) : p.value)});
        return false;
    });
    MLFields f(raw);
    MultiLeaderStyleDefinition v;
    mleader_read_fields(f, v);
    // Legacy exporters used group 304 for default text; the last spelling wins.
    for (auto at : f.take(304))
        static_cast<void>(at);
    for (const auto &p : raw)
        if (p.code == 300 || p.code == 304)
            v.default_text = p.value;
    v.extra_fields = f.extras();
    return v;
}
void DxfMultiLeaderStyle::set_definition(const MultiLeaderStyleDefinition &v) {
    MLFields out;
    mleader_write_fields(out, v);
    out.append(v.extra_fields);
    std::vector<Pair> replacement;
    for (const auto &p : out.values) {
        auto binary = detail::binary_value_from_text(p.code, p.value);
        replacement.push_back({p.code, detail::is_string_value(p.code)
                                           ? encode_string(p.value)
                                           : (binary_ ? std::move(binary) : p.value)});
    }
    auto staged = *this;
    std::vector<std::size_t> positions;
    const auto end = locate_if("AcDbMLeaderStyle", [&](std::size_t at, int) {
                         positions.push_back(at);
                         return false;
                     }).index;
    if (end == std::string_view::npos)
        throw std::invalid_argument("Missing MLEADERSTYLE subclass");
    const auto start = positions.empty() ? end : positions.front();
    for (auto at = positions.rbegin(); at != positions.rend(); ++at)
        staged.pairs_.erase(staged.pairs_.begin() + *at);
    staged.pairs_.insert(staged.pairs_.begin() + start, replacement.begin(), replacement.end());
    *this = std::move(staged);
}
const DxfMultiLeaderStyle *DxfDocument::get_multileader_style(std::string_view name) const {
    const auto dictionary = named_object_dictionary("ACAD_MLEADERSTYLE");
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
                throw std::invalid_argument("Ambiguous MLEADERSTYLE name");
            handle = p.value;
            match = false;
        }
        return false;
    });
    if (handle.empty())
        return nullptr;
    const auto locations = handle_index_.find(handle_value(handle));
    if (locations == handle_index_.end() || locations->second.size() != 1)
        throw std::invalid_argument("Invalid MLEADERSTYLE dictionary entry");
    const DxfMultiLeaderStyle *result = nullptr;
    for_each<DxfMultiLeaderStyle>([&](const auto &r) {
        if (handle_value(r.handle()) == handle_value(handle))
            result = &r;
    });
    if (!result)
        throw std::invalid_argument("Wrong MLEADERSTYLE dictionary entry type");
    return result;
}
std::uint64_t DxfDocument::add_multileader_style(std::string name,
                                                 const MultiLeaderStyleDefinition &definition) {
    if (version_ < DxfVersion::AC1027)
        throw std::invalid_argument("Full MLEADERSTYLE construction requires R2013 or later");
    valid_symbol_name(name);
    if (get_multileader_style(name))
        throw std::invalid_argument("MLEADERSTYLE already exists");
    const auto dictionary = named_object_dictionary("ACAD_MLEADERSTYLE");
    if (dictionary.empty())
        throw std::invalid_argument("Missing ACAD_MLEADERSTYLE dictionary");
    auto v = definition;
    mleader_check_handle(*this, v.line_type_handle, "LTYPE");
    mleader_check_handle(*this, v.text_style_handle, "STYLE");
    mleader_check_handle(*this, v.block_content_handle, "BLOCK_RECORD");
    mleader_check_handle(*this, v.arrowhead_handle, "BLOCK_RECORD");
    if (!v.line_type_handle) {
        const auto *type = get_linetype("ByLayer");
        if (type)
            v.line_type_handle = handle_value(type->handle());
    }
    DxfDocument staged = *this;
    DxfMultiLeaderStyle style;
    static_cast<DxfRecord &>(style) = staged.make_record("MLEADERSTYLE", "AcDbMLeaderStyle");
    style.set_definition(v);
    for (const auto &p : style.pairs_)
        if (pointer_code(p.code) && handle_value(p.value) && records_with_handle(p.value).size() != 1)
            throw std::invalid_argument("Missing MLEADERSTYLE reference");
    const auto id = staged.add_dictionary_object(dictionary, std::move(name), std::move(style));
    *this = std::move(staged);
    return id;
}
