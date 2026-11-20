// SPDX-License-Identifier: MPL-2.0
// acadrust entities/{text,attribute_entity,attribute_definition}.rs and DXF I/O.
DxfRecord::Position DxfText::vertical_position() const {
    if (type() == "ATTRIB" || type() == "ATTDEF")
        return locate(type() == "ATTRIB" ? "AcDbAttribute" : "AcDbAttributeDefinition", 74);
    auto first = locate("AcDbText", 73);
    if (first.found)
        return first;
    bool active = false;
    unsigned depth = 0;
    for (std::size_t i = 1; i < pairs_.size(); ++i) {
        const auto &p = pairs_[i];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (depth && p.value == "}")
                --depth;
            continue;
        }
        if (depth)
            continue;
        if (p.code == 101 || p.code == 1001)
            break;
        if (p.code == 100)
            active = p.value == "AcDbText";
        else if (active && p.code == 73)
            return {i, true};
    }
    return first;
}
TextDefinition DxfText::definition() const {
    TextDefinition t;
    t.text = text();
    t.style = text_field("AcDbText", 7, "Standard");
    t.insertion = point_or("AcDbText", 10, {});
    t.normal = point_or("AcDbText", 210, {0, 0, 1});
    if (locate("AcDbText", 11).found && locate("AcDbText", 21).found)
        t.alignment_point = point_or("AcDbText", 11, {});
    t.height = number("AcDbText", 40, type() == "TEXT" ? 1 : 2.5);
    t.thickness = number("AcDbText", 39, 0);
    t.rotation = number("AcDbText", 50, 0) * pi / 180.;
    t.width_factor = number("AcDbText", 41, 1);
    t.oblique_angle = number("AcDbText", 51, 0) * pi / 180.;
    t.generation_flags = static_cast<std::int16_t>(short_field("AcDbText", 71, 0));
    auto horizontal = short_field("AcDbText", 72, 0);
    t.horizontal_alignment = horizontal >= 0 && horizontal <= 5 ? static_cast<std::int16_t>(horizontal) : 0;
    auto v = vertical_position();
    if (v.found) {
        auto n = binary_ ? detail::binary_short(pairs_[v.index].value) : integer(pairs_[v.index].value);
        t.vertical_alignment = n >= 0 && n <= 3 ? static_cast<std::int16_t>(n) : 0;
    }
    return t;
}
void DxfText::set_definition(const TextDefinition &t) {
    if (t.horizontal_alignment < 0 || t.horizontal_alignment > 5 || t.vertical_alignment < 0 ||
        t.vertical_alignment > 3)
        throw std::invalid_argument("Invalid text alignment");
    DxfText staged = *this;
    staged.set_text(t.text);
    staged.set_text_field("AcDbText", 7, t.style);
    staged.set_point("AcDbText", 10, t.insertion);
    staged.set_point("AcDbText", 210, t.normal);
    if (t.alignment_point)
        staged.set_point("AcDbText", 11, *t.alignment_point);
    else {
        std::vector<std::size_t> remove;
        staged.locate_if("AcDbText", [&](std::size_t i, int c) {
            if (c == 11 || c == 21 || c == 31)
                remove.push_back(i);
            return false;
        });
        for (auto it = remove.rbegin(); it != remove.rend(); ++it)
            staged.pairs_.erase(staged.pairs_.begin() + *it);
    }
    staged.set_number("AcDbText", 40, t.height);
    staged.set_number("AcDbText", 39, t.thickness);
    staged.set_number("AcDbText", 50, t.rotation * 180. / pi);
    staged.set_number("AcDbText", 41, t.width_factor);
    staged.set_number("AcDbText", 51, t.oblique_angle * 180. / pi);
    staged.set_short_field("AcDbText", 71, t.generation_flags);
    staged.set_short_field("AcDbText", 72, t.horizontal_alignment);
    auto v = staged.vertical_position();
    if (v.index == std::string_view::npos)
        throw std::invalid_argument("Missing text alignment subclass");
    const auto value =
        binary_ ? detail::binary_short(t.vertical_alignment) : std::to_string(t.vertical_alignment);
    if (v.found)
        staged.pairs_[v.index].value = value;
    else
        staged.pairs_.insert(staged.pairs_.begin() + v.index, {type() == "TEXT" ? 73 : 74, value});
    static_cast<DxfRecord &>(*this) = std::move(static_cast<DxfRecord &>(staged));
}
std::string_view DxfAttribute::attribute_subclass() const {
    return type() == "ATTDEF" ? "AcDbAttributeDefinition" : "AcDbAttribute";
}
AttributeDefinition DxfAttribute::attribute_definition() const {
    AttributeDefinition a;
    a.text = definition();
    const auto scope = attribute_subclass();
    a.tag = text_field(scope, 2);
    a.prompt = type() == "ATTDEF" ? text_field(scope, 3) : "";
    a.flags = static_cast<std::int16_t>(short_field(scope, 70, 0));
    a.field_length = static_cast<std::int16_t>(short_field(scope, 73, 0));
    auto flag = short_field(scope, 71, 0);
    const bool scoped = std::any_of(pairs_.begin(), pairs_.end(),
                                    [&](const auto &p) { return p.code == 100 && p.value == scope; });
    a.multiline = scoped && (flag == 2 || flag == 4);
    bool tag = false;
    const auto stop = locate_if(scope, [&](std::size_t i, int code) {
                          if (code == 2)
                              tag = true;
                          if (code == 280 && tag)
                              a.lock_position = (binary_ ? detail::binary_short(pairs_[i].value)
                                                         : integer(pairs_[i].value)) != 0;
                          return false;
                      }).index;
    if (stop < pairs_.size() && pairs_[stop].code == 101) {
        DxfMText embedded;
        embedded.binary_ = binary_;
        embedded.code_page_ = code_page_;
        embedded.version_ = version_;
        embedded.pairs_ = {{0, "MTEXT"}, {100, "AcDbMText"}};
        for (auto i = stop + 1; i < pairs_.size() && pairs_[i].code < 1000; ++i)
            embedded.pairs_.push_back(pairs_[i]);
        auto text = embedded.definition();
        // Embedded attribute MTEXT has no background/column fields in upstream DXF I/O.
        text.background_flags = 0;
        text.background_scale = 1.5;
        text.background_color = Color{};
        text.background_transparency = 0;
        text.columns = {};
        if (!text.text.empty())
            a.text.text = text.text;
        a.embedded_mtext = std::move(text);
        a.multiline = true;
    }
    return a;
}
void DxfAttribute::set_attribute_definition(const AttributeDefinition &a) {
    const bool multiline = a.multiline || a.embedded_mtext.has_value();
    if (multiline && version_ < DxfVersion::AC1032)
        throw std::invalid_argument("Multiline attributes require R2018 or later");
    if (a.tag.empty())
        throw std::invalid_argument("Attribute tag cannot be empty");
    DxfAttribute staged = *this;
    auto text = a.text;
    if (a.embedded_mtext)
        text.text = a.embedded_mtext->text;
    std::string value;
    for (std::size_t i = 0; i < text.text.size(); ++i) {
        auto c = text.text[i];
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.text.size() && text.text[i + 1] == '\n')
                ++i;
            value += "\\P";
        } else
            value += c;
    }
    text.text = std::move(value);
    if (!text.alignment_point)
        text.alignment_point = Point3{};
    staged.set_definition(text);
    const auto scope = attribute_subclass();
    if (type() == "ATTDEF")
        staged.set_text_field(scope, 3, a.prompt);
    staged.set_text_field(scope, 2, a.tag);
    staged.set_short_field(scope, 70, a.flags);
    staged.set_short_field(scope, 73, a.field_length);
    // Code 280 before the tag is a version byte; the trailing one is lock position.
    std::vector<std::size_t> remove;
    staged.locate_if(scope, [&](std::size_t i, int code) {
        if (code == 280)
            remove.push_back(i);
        return false;
    });
    for (auto it = remove.rbegin(); it != remove.rend(); ++it)
        staged.pairs_.erase(staged.pairs_.begin() + *it);
    if (version_ >= DxfVersion::AC1024) {
        auto tag = staged.locate(scope, 2).index;
        staged.pairs_.insert(staged.pairs_.begin() + tag, {280, binary_ ? detail::binary_short(0) : "0"});
    }
    if (version_ >= DxfVersion::AC1021) {
        auto end = staged.locate_if(scope, [](std::size_t, int) { return false; }).index;
        staged.pairs_.insert(
            staged.pairs_.begin() + end,
            {280, binary_ ? detail::binary_short(a.lock_position) : std::to_string(a.lock_position)});
    }
    // Preserve unknown embedded fields through the same scoped edit as ordinary MTEXT.
    auto stop = staged.locate_if(scope, [](std::size_t, int) { return false; }).index;
    auto last = stop;
    if (stop < staged.pairs_.size() && staged.pairs_[stop].code == 101) {
        last = stop + 1;
        while (last < staged.pairs_.size() && staged.pairs_[last].code < 1000)
            ++last;
    }
    DxfMText embedded;
    embedded.binary_ = binary_;
    embedded.code_page_ = code_page_;
    embedded.version_ = version_;
    embedded.pairs_ = {{0, "MTEXT"}, {100, "AcDbMText"}};
    if (last > stop)
        embedded.pairs_.insert(embedded.pairs_.end(), staged.pairs_.begin() + stop + 1,
                               staged.pairs_.begin() + last);
    staged.pairs_.erase(staged.pairs_.begin() + stop, staged.pairs_.begin() + last);
    if (multiline || std::any_of(staged.pairs_.begin(), staged.pairs_.end(),
                                 [&](const auto &p) { return p.code == 100 && p.value == scope; }))
        staged.set_short_field(scope, 71, multiline ? (type() == "ATTDEF" ? 4 : 2) : 0);
    if (multiline) {
        MTextDefinition m;
        if (a.embedded_mtext)
            m = *a.embedded_mtext;
        else {
            m.text = text.text;
            m.insertion = text.insertion;
            m.normal = text.normal;
            m.rotation = text.rotation;
            m.height = text.height;
            m.style = text.style;
        }
        if (m.columns.type || m.background_flags)
            throw std::invalid_argument(
                "Upstream DXF attribute MTEXT does not serialize columns or background fill");
        embedded.set_definition(m);
        // AutoCAD drops attributes carrying unnecessary default direction/normal fields.
        auto &pairs = embedded.pairs_;
        std::vector<std::size_t> defaults;
        embedded.locate_if("AcDbMText", [&](std::size_t i, int code) {
            if ((m.rotation == 0 && (code == 11 || code == 21 || code == 31)) ||
                (m.normal == Point3{0, 0, 1} && (code == 210 || code == 220 || code == 230)))
                defaults.push_back(i);
            return false;
        });
        for (auto it = defaults.rbegin(); it != defaults.rend(); ++it)
            pairs.erase(pairs.begin() + *it);
        staged.set_short_field(scope, 72, 0);
        staged.set_point(scope, 11, *text.alignment_point);
        const auto end = staged.locate_if(scope, [](std::size_t, int) { return false; }).index;
        std::vector<Pair> body{{101, "Embedded Object"}};
        body.insert(body.end(), pairs.begin() + 2, pairs.end());
        staged.pairs_.insert(staged.pairs_.begin() + end, body.begin(), body.end());
    }
    *this = std::move(staged);
}
DxfAttribute DxfInsert::attribute(std::size_t index) const {
    DxfAttribute a;
    static_cast<DxfRecord &>(a) = children_.at(index);
    a.version_ = version_;
    return a;
}
void DxfInsert::set_attribute(std::size_t index, const DxfAttribute &value) {
    const auto &old = children_.at(index);
    if (value.type() != "ATTRIB" || value.handle() != old.handle() ||
        value.owner_handle() != old.owner_handle())
        throw std::invalid_argument("Attribute identity and owner must be preserved");
    children_[index] = static_cast<const DxfRecord &>(value);
}
std::uint64_t DxfDocument::add_attribute_definition(const AttributeDefinition &definition,
                                                    const EntityOptions &options) {
    if (!get_symbol(SymbolTable::TextStyle, definition.text.style))
        throw std::invalid_argument("Unknown attribute definition text style");
    DxfAttribute entity;
    static_cast<DxfEntity &>(entity) = make_entity("ATTDEF", "AcDbText", options);
    if (version_ != DxfVersion::AC1009)
        entity.pairs_.push_back({100, "AcDbAttributeDefinition"});
    entity.set_attribute_definition(definition);
    return insert_entity(std::move(entity), options);
}
