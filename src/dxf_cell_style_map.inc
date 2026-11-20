// SPDX-License-Identifier: MPL-2.0
namespace {
constexpr const char *cell_map_key = "ACAD_ROUNDTRIP_2008_TABLESTYLE_CELLSTYLEMAP";
constexpr const char *table_template_key = "ACAD_ROUNDTRIP_2008_TABLESTYLE_TEMPLATE";
void cs_color(MLFields &f, Color c) {
    f.put_i(62, c.approximate_index());
    if (auto rgb = c.to_true_color_value())
        f.put_i(420, *rgb);
}
NamedTableCellStyle cs_read(MTFields raw) {
    NamedTableCellStyle v;
    MTReader r(std::move(raw));
    auto tail = r.scan([&](auto &r) {
        if (!r.is(1, "TABLEFORMAT_BEGIN"))
            return false;
        ++r.at;
        MTReader body(r.body("TABLEFORMAT_END"));
        std::uint32_t edge = 0;
        auto f = body.scan([&](auto &r) {
            if (r.is(300, "CONTENTFORMAT")) {
                ++r.at;
                auto raw = r.body("CONTENTFORMAT_END");
                MLFields fields(raw);
                v.format.content = mt_content_format(raw);
                if (fields.take(92).empty())
                    v.format.content.data_type = 512;
                if (fields.take(94).empty())
                    v.format.content.alignment = 5;
                return true;
            }
            if (r.is(301, "MARGIN")) {
                ++r.at;
                MLFields m(r.body("CELLMARGIN_END"));
                m.take(1);
                auto indexes = m.take(40);
                for (std::size_t i = 0; i < std::min(std::size_t(6), indexes.size()); ++i) {
                    MLFields one({m.values[indexes[i]]});
                    v.format.margins[i] = one.r(40, 0);
                }
                v.format.margin_extra = m.extras();
                return true;
            }
            if (r.input.values[r.at].code == 95) {
                MLFields one({r.input.values[r.at++]});
                edge = one.i(95, 0);
                return true;
            }
            if (r.is(302, "GRIDFORMAT")) {
                ++r.at;
                auto raw = r.body("GRIDFORMAT_END");
                MLFields fields(raw);
                auto border = mt_grid(raw, edge);
                border.invisible = fields.i(93, 1) == 0;
                if (fields.take(92).empty())
                    border.line_weight = -2;
                v.format.borders.push_back(border);
                edge = 0;
                return true;
            }
            if (r.is(309, "GRIDFORMAT_END")) {
                TableGridFormat border;
                border.line_weight = -2;
                v.format.borders.push_back(border);
                ++r.at;
                return true;
            }
            return false;
        });
        v.format.type = f.i(90, 0);
        v.data_flags = f.i(170, 0);
        v.format.overrides = f.i(91, 0);
        v.format.properties = f.i(92, 0);
        v.format.background = read_table_color(f, 62, 420, Color::by_layer());
        v.format.layout = f.i(93, 0);
        v.format.margin_flags = f.i(171, 0);
        f.take(94);
        v.format.extra_fields = f.extras();
        return true;
    });
    tail.take(1);
    v.id = tail.i(90, 0);
    v.style_type = tail.i(91, 0);
    v.name = tail.s(300, {});
    v.extra_fields = tail.extras();
    return v;
}
void cs_write(MLFields &f, const NamedTableCellStyle &v) {
    f.put_s(300, "CELLSTYLE");
    mt_begin(f, "TABLEFORMAT");
    const auto &s = v.format;
    f.put_i(90, s.type);
    f.put_i(170, v.data_flags);
    if (v.data_flags) {
        f.put_i(91, s.overrides);
        f.put_i(92, s.properties);
        cs_color(f, s.background);
        f.put_i(93, s.layout);
        const auto &c = s.content;
        f.put_s(300, "CONTENTFORMAT");
        mt_begin(f, "CONTENTFORMAT");
        f.put_i(90, c.overrides);
        f.put_i(91, c.properties);
        f.put_i(92, c.data_type);
        f.put_i(93, c.unit_type);
        f.put_s(300, c.value_format);
        f.put_r(40, c.rotation);
        f.put_r(140, c.scale);
        f.put_i(94, c.alignment);
        cs_color(f, c.color);
        f.put_s(340, hex_handle(c.text_style));
        f.put_r(144, c.text_height);
        mt_append(f, c.extra_fields);
        mt_end(f, "CONTENTFORMAT");
        f.put_i(171, s.margin_flags);
        if (s.margin_flags) {
            f.put_s(301, "MARGIN");
            mt_begin(f, "CELLMARGIN");
            for (auto x : s.margins)
                f.put_r(40, x);
            mt_append(f, s.margin_extra);
            mt_end(f, "CELLMARGIN");
        }
        f.put_i(94, std::min(std::size_t(6), s.borders.size()));
        for (std::size_t i = 0; i < std::min(std::size_t(6), s.borders.size()); ++i) {
            const auto &b = s.borders[i];
            if (b.edge) {
                f.put_i(95, b.edge);
                f.put_s(302, "GRIDFORMAT");
                mt_begin(f, "GRIDFORMAT");
                f.put_i(90, b.overrides);
                f.put_i(91, b.type);
                cs_color(f, b.color);
                f.put_i(92, b.line_weight);
                f.put_s(340, hex_handle(b.line_type));
                f.put_i(93, !b.invisible);
                f.put_r(40, b.double_spacing);
                mt_append(f, b.extra_fields);
            }
            mt_end(f, "GRIDFORMAT");
        }
    }
    mt_append(f, s.extra_fields);
    mt_end(f, "TABLEFORMAT");
    mt_begin(f, "CELLSTYLE");
    f.put_i(90, v.id);
    f.put_i(91, v.style_type);
    f.put_s(300, v.name);
    mt_append(f, v.extra_fields);
    mt_end(f, "CELLSTYLE");
}
void cs_unique(const CellStyleMapDefinition &v) {
    std::unordered_set<std::int32_t> ids;
    std::unordered_set<std::u32string> names;
    for (const auto &s : v.styles)
        if (s.name.empty() || !ids.insert(s.id).second ||
            !names.insert(detail::symbol_name_key(s.name)).second)
            throw std::invalid_argument("Duplicate/empty cell style name or ID");
}
} // namespace
CellStyleMapDefinition DxfCellStyleMap::definition() const {
    MTReader r(subclass_fields("AcDbCellStyleMap"));
    CellStyleMapDefinition v;
    auto f = r.scan([&](auto &r) {
        if (!r.is(300, "CELLSTYLE"))
            return false;
        ++r.at;
        v.styles.push_back(cs_read(r.body("CELLSTYLE_END")));
        return true;
    });
    f.take(90);
    v.extra_fields = f.extras();
    return v;
}
void DxfCellStyleMap::set_definition(const CellStyleMapDefinition &v) {
    cs_unique(v);
    MLFields f;
    f.put_i(90, mt_count(v.styles.size()));
    for (const auto &s : v.styles)
        cs_write(f, s);
    mt_append(f, v.extra_fields);
    set_subclass_fields("AcDbCellStyleMap", f.values);
}
std::string DxfDocument::extension_dictionary_handle(std::string_view owner) const {
    if (records_with_handle(owner).size() != 1)
        throw std::invalid_argument("Owner must resolve uniquely");
    const auto *object = get_record(owner);
    std::string handle;
    unsigned depth = 0;
    for (std::size_t i = 1; i < object->pairs_.size(); ++i) {
        const auto &p = object->pairs_[i];
        if (!depth && (p.code == 100 || p.code == 101 || p.code >= 1000))
            break;
        if (p.code != 102)
            continue;
        if (!depth && p.value == "{ACAD_XDICTIONARY") {
            if (!handle.empty() || i + 2 >= object->pairs_.size() || object->pairs_[i + 1].code != 360 ||
                object->pairs_[i + 2].code != 102 || object->pairs_[i + 2].value != "}")
                throw std::invalid_argument("Malformed extension dictionary");
            handle = object->pairs_[i + 1].value;
            i += 2;
            continue;
        }
        if (!p.value.empty() && p.value.front() == '{')
            ++depth;
        else if (depth && p.value == "}")
            --depth;
    }
    if (!handle.empty()) {
        const auto *dict = get_record(handle);
        if (records_with_handle(handle).size() != 1 || !dict || dict->type() != "DICTIONARY" ||
            handle_value(dict->owner_handle()) != handle_value(owner))
            throw std::invalid_argument("Invalid extension dictionary owner");
    }
    return handle;
}
std::string DxfDocument::dictionary_entry(std::string_view dictionary, std::string_view name) const {
    if (records_with_handle(dictionary).size() != 1 || get_record(dictionary)->type() != "DICTIONARY")
        throw std::invalid_argument("Expected unique dictionary");
    const auto *dict = get_record(dictionary);
    std::string result;
    bool active = false;
    dict->locate_if("AcDbDictionary", [&](std::size_t i, int code) {
        const auto &p = dict->pairs_[i];
        if (code == 3)
            active = detail::symbol_name_key(dict->decode_string(p.value)) == detail::symbol_name_key(name);
        else if (active && (code == 350 || code == 360)) {
            if (!result.empty())
                throw std::invalid_argument("Duplicate dictionary key");
            result = p.value;
            active = false;
        }
        return false;
    });
    if (!result.empty() && records_with_handle(result).size() != 1)
        throw std::invalid_argument("Invalid dictionary target");
    return result;
}
std::uint64_t DxfDocument::add_cell_style_map(std::string_view dictionary, std::string name,
                                              const CellStyleMapDefinition &value) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("CELLSTYLEMAP construction requires R2007+");
    auto v = value;
    cs_unique(v);
    for (auto &s : v.styles) {
        TableContentDefinition check;
        check.format = s.format;
        validate_table_content(check);
        s.format = *check.format;
    }
    DxfDocument staged = *this;
    DxfCellStyleMap object;
    static_cast<DxfRecord &>(object) = staged.make_record("CELLSTYLEMAP");
    object.pairs_.insert(object.pairs_.end(),{{102,"{ACAD_REACTORS"},{330,std::string(dictionary)},{102,"}"},{100,"AcDbCellStyleMap"}});
    object.set_definition(v);
    const auto id = staged.add_dictionary_object(dictionary, std::move(name), object);
    *this = std::move(staged);
    return id;
}
CellStyleMapDefinition DxfDocument::table_cell_styles(std::string_view style) const {
    mleader_check_handle(*this, handle_value(style), "TABLESTYLE");
    const auto dict = extension_dictionary_handle(style);
    if (dict.empty())
        return {};
    const auto map = dictionary_entry(dict, cell_map_key);
    if (map.empty())
        return {};
    if (get_record(map)->type() != "CELLSTYLEMAP" ||
        handle_value(get_record(map)->owner_handle()) != handle_value(dict))
        throw std::invalid_argument("Invalid style map ownership");
    CellStyleMapDefinition v;
    for_each<DxfCellStyleMap>([&](const auto &m) {
        if (handle_value(m.handle()) == handle_value(map))
            v = m.definition();
    });
    return v;
}
void DxfDocument::set_table_cell_styles(std::string_view style, const CellStyleMapDefinition &value) {
    mleader_check_handle(*this, handle_value(style), "TABLESTYLE");
    auto existing = table_cell_styles(style);
    static_cast<void>(existing);
    auto v = value;
    cs_unique(v);
    for (auto &s : v.styles) {
        TableContentDefinition check;
        check.format = s.format;
        validate_table_content(check);
        s.format = *check.format;
    }
    DxfDocument staged = *this;
    auto dict = staged.extension_dictionary_handle(style);
    if (dict.empty())
        dict = hex_handle(staged.add_extension_dictionary(style));
    auto map = staged.dictionary_entry(dict, cell_map_key);
    if (map.empty())
        staged.add_cell_style_map(dict, cell_map_key, v);
    else
        staged.for_each<DxfCellStyleMap>([&](auto &m) {
            if (handle_value(m.handle()) == handle_value(map))
                m.set_definition(v);
        });
    // Project the reserved data/title/header cell styles to legacy row groups.
    staged.for_each<DxfTableStyle>([&](auto &record) {
        if (handle_value(record.handle()) != handle_value(style))
            return;
        auto legacy = record.definition();
        for (const auto &s : v.styles) {
            const auto name = detail::symbol_name_key(s.name);
            TableRowStyle *row = nullptr;
            if (name == U"_DATA")
                row = &legacy.data;
            else if (name == U"_TITLE")
                row = &legacy.title;
            else if (name == U"_HEADER")
                row = &legacy.header;
            if (!row)
                continue;
            const auto &f = s.format;
            if (name == U"_DATA" && f.margin_flags) {
                legacy.horizontal_margin = f.margins[1];
                legacy.vertical_margin = f.margins[0];
            }
            row->data_type = f.content.data_type;
            row->unit_type = f.content.unit_type;
            row->format = f.content.value_format;
            row->alignment = f.content.alignment;
            row->text_height = f.content.text_height;
            row->text_color = f.content.color;
            row->text_style = f.content.text_style_name;
            row->fill_enabled = f.background != Color::none();
            if (row->fill_enabled)
                row->fill_color = f.background;
            for (int i = 0; i < 6; ++i)
                for (const auto &b : f.borders)
                    if (b.edge == (1u << i)) {
                        row->borders[i].color = b.color;
                        row->borders[i].line_weight = b.line_weight;
                        row->borders[i].invisible = !b.invisible;
                    }
        }
        record.set_definition(legacy);
    });
    *this = std::move(staged);
}
std::string DxfDocument::table_style_template_handle(std::string_view style) const {
    mleader_check_handle(*this, handle_value(style), "TABLESTYLE");
    auto dict = extension_dictionary_handle(style);
    if (dict.empty())
        return {};
    auto h = dictionary_entry(dict, table_template_key);
    if (!h.empty() && (get_record(h)->type() != "TABLETEMPLATE" ||
                       handle_value(get_record(h)->owner_handle()) != handle_value(dict)))
        throw std::invalid_argument("Invalid table template ownership");
    return h;
}
void DxfDocument::set_table_style_template(std::string_view style, const TableContentDefinition &definition) {
    auto h = table_style_template_handle(style);
    auto v = definition;
    validate_table_content(v);
    DxfDocument staged = *this;
    if (h.empty()) {
        auto dict = staged.extension_dictionary_handle(style);
        if (dict.empty())
            dict = hex_handle(staged.add_extension_dictionary(style));
        staged.add_table_content(dict, table_template_key, v, true);
    } else
        staged.for_each<DxfTableContent>([&](auto &t) {
            if (handle_value(t.handle()) == handle_value(h))
                t.set_definition(v);
        });
    *this = std::move(staged);
}
