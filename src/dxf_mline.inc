// SPDX-License-Identifier: MPL-2.0
// acadrust entities/mline.rs, objects/mlinestyle.rs and DXF section I/O.
MLineDefinition DxfMLine::definition() const {
    constexpr auto scope = "AcDbMline";
    MLineDefinition v;
    v.style_name = text_field(scope, 2, "Standard");
    v.style_handle = handle_value(field(scope, 340));
    v.scale_factor = number(scope, 40, 1);
    const auto justification = short_field(scope, 70, 1);
    v.justification = justification >= 0 && justification <= 2 ? justification : 1;
    v.flags = static_cast<std::uint16_t>(short_field(scope, 71, 1));
    v.style_element_count = static_cast<std::uint16_t>(short_field(scope, 73, 0));
    v.start_point = point_or(scope, 10, {});
    v.normal = point_or(scope, 210, {0, 0, 1});
    struct PointReader {
        Point3 value{};
        bool x = false, y = false;
        void add(int component, double n) {
            if (component == 0) {
                value.x = n;
                x = true;
            } else if (component == 1) {
                value.y = n;
                y = true;
            } else
                value.z = n;
        }
        Point3 get(Point3 fallback) const { return x && y ? value : fallback; }
    } position, direction, miter;
    MLineVertex vertex;
    MLineSegment segment;
    bool reading_vertex = false, reading_parameters = false, reading_fill = false;
    auto flush_segment = [&] {
        if (reading_parameters)
            vertex.segments.push_back(std::move(segment));
        segment = {};
        reading_parameters = false;
        reading_fill = false;
    };
    auto flush_vertex = [&] {
        if (!reading_vertex)
            return;
        flush_segment();
        vertex.position = position.get({});
        vertex.direction = direction.get({1, 0, 0});
        vertex.miter = miter.get({0, 1, 0});
        v.vertices.push_back(std::move(vertex));
        vertex = {};
        position = {};
        direction = {};
        miter = {};
    };
    locate_if(scope, [&](std::size_t at, int code) {
        if (code == 11) {
            flush_vertex();
            reading_vertex = true;
        }
        if (code == 11 || code == 21 || code == 31)
            position.add((code - 11) / 10, number_at(at));
        if (code == 12 || code == 22 || code == 32)
            direction.add((code - 12) / 10, number_at(at));
        if (code == 13 || code == 23 || code == 33)
            miter.add((code - 13) / 10, number_at(at));
        if (code == 74) {
            flush_segment();
            reading_parameters = true;
        }
        if (code == 75)
            reading_fill = true;
        if (code == 41 && reading_parameters && !reading_fill)
            segment.parameters.push_back(number_at(at));
        if (code == 42 || (code == 41 && reading_fill))
            segment.area_fill_parameters.push_back(number_at(at));
        return false;
    });
    flush_vertex();
    return v;
}
void DxfMLine::set_definition(const MLineDefinition &v) {
    constexpr auto scope = "AcDbMline";
    valid_symbol_name(v.style_name);
    if (v.justification < 0 || v.justification > 2 || v.vertices.size() > 32767 ||
        v.style_element_count > 32767)
        throw std::invalid_argument("Invalid MLINE justification or count");
    std::vector<Pair> values;
    auto small = [&](int code, int value) {
        values.push_back({code, binary_ ? detail::binary_short(value) : std::to_string(value)});
    };
    auto real = [&](int code, double value) { values.push_back({code, encode_number(value)}); };
    auto point = [&](int code, Point3 value) {
        real(code, value.x);
        real(code + 10, value.y);
        real(code + 20, value.z);
    };
    values.push_back({2, encode_string(v.style_name)});
    values.push_back({340, hex_handle(v.style_handle)});
    real(40, v.scale_factor);
    small(70, v.justification);
    small(71, static_cast<std::int16_t>((v.flags & ~1u) | (v.vertices.empty() ? 0 : 1)));
    small(72, static_cast<int>(v.vertices.size()));
    small(73, static_cast<int>(v.style_element_count));
    point(10, v.start_point);
    point(210, v.normal);
    for (const auto &vertex : v.vertices) {
        if (vertex.segments.size() != v.style_element_count)
            throw std::invalid_argument("MLINE segment count differs from style element count");
        point(11, vertex.position);
        point(12, vertex.direction);
        point(13, vertex.miter);
        for (const auto &segment : vertex.segments) {
            if (segment.parameters.size() > 32767 || segment.area_fill_parameters.size() > 32767)
                throw std::invalid_argument("MLINE parameter count exceeds DXF capacity");
            small(74, static_cast<int>(segment.parameters.size()));
            for (auto value : segment.parameters)
                real(41, value);
            small(75, static_cast<int>(segment.area_fill_parameters.size()));
            for (auto value : segment.area_fill_parameters)
                real(42, value);
        }
    }
    const std::unordered_set<int> modeled{2,  340, 40, 70, 71, 72, 73, 10, 20, 30, 210, 220, 230,
                                          11, 21,  31, 12, 22, 32, 13, 23, 33, 74, 75,  41,  42};
    DxfMLine staged = *this;
    std::vector<std::size_t> positions;
    const auto end = locate_if(scope, [&](std::size_t at, int code) {
                         if (modeled.count(code))
                             positions.push_back(at);
                         return false;
                     }).index;
    if (end == std::string_view::npos)
        throw std::invalid_argument("Missing MLINE subclass");
    const auto insertion = positions.empty() ? end : positions.front();
    for (auto it = positions.rbegin(); it != positions.rend(); ++it)
        staged.pairs_.erase(staged.pairs_.begin() + *it);
    staged.pairs_.insert(staged.pairs_.begin() + insertion, values.begin(), values.end());
    *this = std::move(staged);
}
std::string DxfMLineStyle::name() const { return text_field("AcDbMlineStyle", 2); }
MLineStyleDefinition DxfMLineStyle::definition() const {
    constexpr auto scope = "AcDbMlineStyle";
    MLineStyleDefinition v;
    v.description = text_field(scope, 3);
    v.flags = static_cast<std::uint16_t>(short_field(scope, 70, 0));
    v.start_angle = number(scope, 51, 90) * pi / 180;
    v.end_angle = number(scope, 52, 90) * pi / 180;
    locate_if(scope, [&](std::size_t at, int code) {
        const auto &p = pairs_[at];
        if (code == 49)
            v.elements.push_back({number_at(at), Color{}, "BYLAYER"});
        else if (code == 62 || code == 420) {
            auto &color = v.elements.empty() ? v.fill_color : v.elements.back().color;
            if (code == 62) {
                if (auto n = detail::as_i16(code, p.value, binary_))
                    color = Color::from_index(*n);
            } else if (auto n = detail::as_i32_bits(p.value, binary_))
                color = Color::from_true_color_value(*n);
        } else if (code == 6 && !v.elements.empty())
            v.elements.back().linetype = decode_string(p.value);
        return false;
    });
    return v;
}
void DxfMLineStyle::set_definition(const MLineStyleDefinition &v) {
    constexpr auto scope = "AcDbMlineStyle";
    if (v.elements.size() > 32767)
        throw std::invalid_argument("MLINESTYLE element count exceeds DXF capacity");
    std::vector<Pair> values;
    auto small = [&](int code, int value) {
        values.push_back({code, binary_ ? detail::binary_short(value) : std::to_string(value)});
    };
    auto real = [&](int code, double value) { values.push_back({code, encode_number(value)}); };
    auto color = [&](Color value) {
        small(62, value.index().value_or(value.approximate_index()));
        if (auto rgb = value.to_true_color_value())
            values.push_back({420, binary_ ? detail::binary_value_from_text(420, std::to_string(*rgb))
                                           : std::to_string(*rgb)});
    };
    small(70, static_cast<std::int16_t>(v.flags));
    values.push_back({3, encode_string(v.description)});
    color(v.fill_color);
    real(51, v.start_angle * 180 / pi);
    real(52, v.end_angle * 180 / pi);
    small(71, static_cast<int>(v.elements.size()));
    for (const auto &element : v.elements) {
        valid_symbol_name(element.linetype);
        real(49, element.offset);
        color(element.color);
        values.push_back({6, encode_string(element.linetype)});
    }
    DxfMLineStyle staged = *this;
    std::vector<std::size_t> positions;
    const std::unordered_set<int> modeled{70, 3, 62, 420, 51, 52, 71, 49, 6};
    const auto end = locate_if(scope, [&](std::size_t at, int code) {
                         if (modeled.count(code))
                             positions.push_back(at);
                         return false;
                     }).index;
    if (end == std::string_view::npos)
        throw std::invalid_argument("Missing MLINESTYLE subclass");
    const auto insertion = positions.empty() ? end : positions.front();
    for (auto it = positions.rbegin(); it != positions.rend(); ++it)
        staged.pairs_.erase(staged.pairs_.begin() + *it);
    staged.pairs_.insert(staged.pairs_.begin() + insertion, values.begin(), values.end());
    *this = std::move(staged);
}
std::string DxfDocument::named_object_dictionary(std::string_view name) const {
    const auto key = detail::symbol_name_key(name);
    std::string result;
    for (const auto &entry : records_) {
        const auto &r = std::visit([](const DxfRecord &r) -> const DxfRecord & { return r; }, entry);
        if (r.type() != "DICTIONARY" || handle_value(r.owner_handle()))
            continue;
        bool match = false;
        r.locate_if("AcDbDictionary", [&](std::size_t at, int code) {
            if (code == 3)
                match = detail::symbol_name_key(r.decode_string(r.pairs_[at].value)) == key;
            else if (match && (code == 350 || code == 360)) {
                if (!result.empty())
                    throw std::invalid_argument("Ambiguous named object dictionary");
                result = r.pairs_[at].value;
                match = false;
            }
            return false;
        });
    }
    if (!result.empty()) {
        const auto found = records_with_handle(result);
        if (found.size() != 1 || found.front()->type() != "DICTIONARY")
            throw std::invalid_argument("Invalid named object dictionary reference");
    }
    return result;
}
const DxfMLineStyle *DxfDocument::get_mline_style(std::string_view name) const {
    const DxfMLineStyle *found = nullptr;
    const auto key = detail::symbol_name_key(name);
    for_each<DxfMLineStyle>([&](const auto &style) {
        if (detail::symbol_name_key(style.name()) == key) {
            if (found)
                throw std::invalid_argument("Ambiguous MLINESTYLE name");
            found = &style;
        }
    });
    return found;
}
std::uint64_t DxfDocument::add_mline_style(std::string name, const MLineStyleDefinition &v) {
    if (version_ < DxfVersion::AC1018)
        throw std::invalid_argument("Full MLINESTYLE construction requires R2004 or later");
    valid_symbol_name(name);
    if (get_mline_style(name))
        throw std::invalid_argument("MLINESTYLE already exists");
    for (const auto &element : v.elements)
        if (!get_linetype(element.linetype))
            throw std::invalid_argument("Missing MLINESTYLE linetype");
    const auto dictionary = named_object_dictionary("ACAD_MLINESTYLE");
    if (dictionary.empty())
        throw std::invalid_argument("Missing ACAD_MLINESTYLE dictionary");
    DxfDocument staged = *this;
    DxfMLineStyle style;
    static_cast<DxfRecord &>(style) = staged.make_record("MLINESTYLE", "AcDbMlineStyle");
    style.set_text_field("AcDbMlineStyle", 2, name);
    style.set_definition(v);
    const auto id = staged.add_dictionary_object(dictionary, std::move(name), std::move(style));
    *this = std::move(staged);
    return id;
}
std::uint64_t DxfDocument::add_mline(const MLineDefinition &definition, const EntityOptions &options) {
    if (version_ < DxfVersion::AC1012)
        throw std::invalid_argument("MLINE requires R13 or later");
    auto v = definition;
    const auto *style = get_mline_style(v.style_name);
    if (!style || (v.style_handle && v.style_handle != handle_value(style->handle())))
        throw std::invalid_argument("Missing or inconsistent MLINE style");
    if (v.style_element_count != style->definition().elements.size())
        throw std::invalid_argument("MLINE element count differs from style");
    v.style_handle = handle_value(style->handle());
    v.style_name = style->name();
    DxfMLine entity;
    static_cast<DxfEntity &>(entity) = make_entity("MLINE", "AcDbMline", options);
    entity.set_definition(v);
    return insert_entity(std::move(entity), options);
}
