// SPDX-License-Identifier: MPL-2.0
// acadrust entities/{block,insert}.rs and DXF block-reference I/O.
std::string DxfBlock::name() const { return text_field("AcDbBlockBegin", 2); }
Point3 DxfBlock::base_point() const { return point_or("AcDbBlockBegin", 10, {}); }
void DxfBlock::set_base_point(Point3 value) { set_point("AcDbBlockBegin", 10, value); }
std::string DxfBlock::description() const { return text_field("AcDbBlockBegin", 4); }
void DxfBlock::set_description(std::string_view value) {
    set_text_field("AcDbBlockBegin", 4, std::string(value));
}
std::string DxfBlock::xref_path() const { return text_field("AcDbBlockBegin", 1); }
void DxfBlock::set_xref_path(std::string_view value) {
    set_text_field("AcDbBlockBegin", 1, std::string(value));
}
std::int16_t DxfBlock::flags() const {
    return static_cast<std::int16_t>(short_field("AcDbBlockBegin", 70, 0));
}
void DxfBlock::set_flags(std::int16_t value) { set_short_field("AcDbBlockBegin", 70, value); }
std::string_view DxfInsert::subclass() const {
    return std::any_of(pairs_.begin(), pairs_.end(),
                       [](const auto &p) { return p.code == 100 && p.value == "AcDbMInsertBlock"; })
               ? "AcDbMInsertBlock"
               : "AcDbBlockReference";
}
InsertDefinition DxfInsert::definition() const {
    const auto scope = subclass();
    InsertDefinition d;
    d.block_name = text_field(scope, 2);
    d.insertion = point_or(scope, 10, {});
    d.normal = point_or(scope, 210, {0, 0, 1});
    auto scale = [&](int code) {
        auto value = number(scope, code, 1);
        return std::abs(value) < 1e-12 ? 1e-12 : value;
    };
    d.scale = {scale(41), scale(42), scale(43)};
    d.rotation = number(scope, 50, 0) * pi / 180.;
    d.columns = static_cast<std::uint16_t>(std::max(short_field(scope, 70, 1), 1));
    d.rows = static_cast<std::uint16_t>(std::max(short_field(scope, 71, 1), 1));
    d.column_spacing = number(scope, 44, 0);
    d.row_spacing = number(scope, 45, 0);
    return d;
}
void DxfInsert::set_definition(const InsertDefinition &d) {
    finite(d.scale.x);
    finite(d.scale.y);
    finite(d.scale.z);
    finite(d.rotation);
    if (d.columns < 1 || d.rows < 1 || d.columns > 32767 || d.rows > 32767)
        throw std::invalid_argument("INSERT dimensions must fit positive DXF short counts");
    if (d.block_name.empty())
        throw std::invalid_argument("INSERT requires a block name");
    DxfInsert staged = *this;
    auto old = subclass();
    const auto scope = d.columns > 1 || d.rows > 1 ? "AcDbMInsertBlock" : "AcDbBlockReference";
    for (auto &p : staged.pairs_)
        if (p.code == 100 && p.value == old)
            p.value = scope;
    staged.set_text_field(scope, 2, d.block_name);
    staged.set_point(scope, 10, d.insertion);
    staged.set_point(scope, 210, d.normal);
    auto scale = [](double value) { return std::abs(value) < 1e-12 ? 1e-12 : value; };
    staged.set_number(scope, 41, scale(d.scale.x));
    staged.set_number(scope, 42, scale(d.scale.y));
    staged.set_number(scope, 43, scale(d.scale.z));
    staged.set_number(scope, 50, d.rotation * 180. / pi);
    staged.set_short_field(scope, 70, d.columns);
    staged.set_short_field(scope, 71, d.rows);
    staged.set_number(scope, 44, d.column_spacing);
    staged.set_number(scope, 45, d.row_spacing);
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_insert(const InsertDefinition &definition, const EntityOptions &options) {
    return add_insert(definition, {}, options);
}
std::uint64_t DxfDocument::add_insert(const InsertDefinition &definition,
                                      const std::vector<AttributeDefinition> &attributes,
                                      const EntityOptions &options) {
    const auto matches = symbol_locations(SymbolTable::BlockRecord, definition.block_name);
    if (matches.size() != 1)
        throw std::invalid_argument("INSERT block name must resolve uniquely");
    const auto &block = std::get<DxfBlockRecord>(records_[matches.front()]);
    if (block.is_model_space() || block.is_paper_space())
        throw std::invalid_argument("Cannot insert a space block");
    DxfInsert entity;
    static_cast<DxfEntity &>(entity) = make_entity("INSERT", "AcDbBlockReference", options);
    auto canonical = definition;
    canonical.block_name = block.name();
    entity.set_definition(canonical);
    for (const auto &definition : attributes) {
        if (!get_symbol(SymbolTable::TextStyle, definition.text.style))
            throw std::invalid_argument("Unknown attribute text style");
        DxfAttribute child;
        static_cast<DxfEntity &>(child) = make_entity("ATTRIB", "AcDbText", options);
        if (version_ != DxfVersion::AC1009)
            child.pairs_.push_back({100, "AcDbAttribute"});
        child.set_attribute_definition(definition);
        entity.children_.push_back(std::move(child));
    }
    if (!attributes.empty()) {
        entity.set_short_field(entity.subclass(), 66, 1);
        auto end = make_record("SEQEND");
        if (version_ != DxfVersion::AC1009)
            end.pairs_.push_back({100, "AcDbEntity"});
        end.set_text_field("AcDbEntity", 8, entity.layer());
        entity.seqend_ = std::move(end);
    }
    return insert_entity(std::move(entity), options);
}
