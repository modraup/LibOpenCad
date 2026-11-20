// SPDX-License-Identifier: MPL-2.0
// DXF field semantics from acadrust entities/{tolerance,leader}.rs and section I/O.
ToleranceDefinition DxfTolerance::definition() const {
    ToleranceDefinition v;
    v.text_height = text_height_;
    v.dimension_gap = dimension_gap_;
    v.text = text_field("AcDbFcf", 1);
    v.dimension_style = text_field("AcDbFcf", 3, "Standard");
    v.insertion = point_or("AcDbFcf", 10, {});
    v.direction = point_or("AcDbFcf", 11, {1, 0, 0});
    v.normal = point_or("AcDbFcf", 210, {0, 0, 1});
    return v;
}
void DxfTolerance::set_definition(const ToleranceDefinition &v) {
    valid_symbol_name(v.dimension_style);
    if (!std::isfinite(v.text_height) || !std::isfinite(v.dimension_gap))
        throw std::invalid_argument("Nonfinite tolerance size");
    DxfTolerance staged = *this;
    staged.text_height_ = v.text_height;
    staged.dimension_gap_ = v.dimension_gap;
    staged.set_text_field("AcDbFcf", 1, v.text);
    staged.set_text_field("AcDbFcf", 3, v.dimension_style);
    staged.set_point("AcDbFcf", 10, v.insertion);
    staged.set_point("AcDbFcf", 11, v.direction);
    staged.set_point("AcDbFcf", 210, v.normal);
    *this = std::move(staged);
}
LeaderDefinition DxfLeader::definition() const {
    constexpr auto scope = "AcDbLeader";
    LeaderDefinition v;
    v.origin = origin_.value_or(Point3{});
    v.dimension_style = text_field(scope, 3, "Standard");
    v.arrow_enabled = short_field(scope, 71, 1) != 0;
    v.path_type = short_field(scope, 72, 0) == 1 ? 1 : 0;
    const auto creation = short_field(scope, 73, 3);
    v.creation_type = creation >= 0 && creation <= 2 ? static_cast<std::int16_t>(creation) : 3;
    v.hookline_direction = short_field(scope, 74, 1) == 1 ? 1 : 0;
    v.hookline_enabled = short_field(scope, 75, 0) != 0;
    v.text_height = number(scope, 40, 2.5);
    v.text_width = number(scope, 41, 0);
    v.override_color = Color::from_index(static_cast<std::int16_t>(short_field(scope, 77, 256)));
    v.annotation_handle = handle_value(field(scope, 340));
    v.normal = point_or(scope, 210, {0, 0, 1});
    v.horizontal_direction = point_or(scope, 211, {1, 0, 0});
    v.block_offset = point_or(scope, 212, {});
    v.annotation_offset = point_or(scope, 213, {});
    Point3 current{};
    bool x = false, y = false;
    auto flush = [&] {
        if (x && y)
            v.vertices.push_back(current);
    };
    locate_if(scope, [&](std::size_t at, int code) {
        if (code == 10) {
            flush();
            current = {};
            x = true;
            y = false;
            current.x = number_at(at);
        }
        if (code == 20) {
            current.y = number_at(at);
            y = true;
        }
        if (code == 30)
            current.z = number_at(at);
        return false;
    });
    flush();
    if (!origin_ && !v.vertices.empty())
        v.origin = v.vertices.front();
    return v;
}
void DxfLeader::set_definition(const LeaderDefinition &v) {
    constexpr auto scope = "AcDbLeader";
    valid_symbol_name(v.dimension_style);
    if (v.path_type < 0 || v.path_type > 1 || v.creation_type < 0 || v.creation_type > 3 ||
        v.hookline_direction < 0 || v.hookline_direction > 1 || v.vertices.size() > 32767)
        throw std::invalid_argument("Invalid leader type or vertex count");
    if (!v.override_color.index())
        throw std::invalid_argument("DXF LEADER override color is indexed, not RGB");
    if (!std::isfinite(v.origin.x) || !std::isfinite(v.origin.y) || !std::isfinite(v.origin.z))
        throw std::invalid_argument("Nonfinite leader origin");
    DxfLeader staged = *this;
    staged.origin_ = v.origin;
    staged.set_text_field(scope, 3, v.dimension_style);
    staged.set_short_field(scope, 71, v.arrow_enabled);
    staged.set_short_field(scope, 72, v.path_type);
    staged.set_short_field(scope, 73, v.creation_type);
    staged.set_short_field(scope, 74, v.hookline_direction);
    staged.set_short_field(scope, 75, v.hookline_enabled);
    staged.set_number(scope, 40, v.text_height);
    staged.set_number(scope, 41, v.text_width);
    staged.set_short_field(scope, 77, *v.override_color.index());
    staged.set_field(scope, 340, hex_handle(v.annotation_handle));
    staged.set_point(scope, 210, v.normal);
    staged.set_point(scope, 211, v.horizontal_direction);
    staged.set_point(scope, 212, v.block_offset);
    staged.set_point(scope, 213, v.annotation_offset);
    std::vector<Pair> replacement{{76, binary_ ? detail::binary_short(static_cast<int>(v.vertices.size()))
                                               : std::to_string(v.vertices.size())}};
    for (const auto &point : v.vertices) {
        replacement.push_back({10, encode_number(point.x)});
        replacement.push_back({20, encode_number(point.y)});
        replacement.push_back({30, encode_number(point.z)});
    }
    std::vector<std::size_t> positions;
    auto end = staged
                   .locate_if(scope,
                              [&](std::size_t at, int code) {
                                  if (code == 10 || code == 20 || code == 30 || code == 76)
                                      positions.push_back(at);
                                  return false;
                              })
                   .index;
    if (end == std::string_view::npos)
        throw std::invalid_argument("Missing LEADER subclass");
    const auto insertion = positions.empty() ? end : positions.front();
    for (auto at = positions.rbegin(); at != positions.rend(); ++at)
        staged.pairs_.erase(staged.pairs_.begin() + *at);
    staged.pairs_.insert(staged.pairs_.begin() + insertion, replacement.begin(), replacement.end());
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_tolerance(const ToleranceDefinition &v, const EntityOptions &options) {
    if (version_ < DxfVersion::AC1012)
        throw std::invalid_argument("TOLERANCE requires R13 or later");
    if (!get_symbol(SymbolTable::DimStyle, v.dimension_style))
        throw std::invalid_argument("Missing dimension style");
    DxfTolerance entity;
    static_cast<DxfEntity &>(entity) = make_entity("TOLERANCE", "AcDbFcf", options);
    entity.set_definition(v);
    return insert_entity(std::move(entity), options);
}
std::uint64_t DxfDocument::add_leader(const LeaderDefinition &v, const EntityOptions &options) {
    if (version_ < DxfVersion::AC1012)
        throw std::invalid_argument("LEADER requires R13 or later");
    if (!get_symbol(SymbolTable::DimStyle, v.dimension_style))
        throw std::invalid_argument("Missing dimension style");
    if (v.annotation_handle && records_with_handle(hex_handle(v.annotation_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous leader annotation");
    DxfLeader entity;
    static_cast<DxfEntity &>(entity) = make_entity("LEADER", "AcDbLeader", options);
    entity.set_definition(v);
    return insert_entity(std::move(entity), options);
}
