// SPDX-License-Identifier: MPL-2.0
// acadrust entities/dimension.rs and DXF dimension section I/O.
namespace {
const char *dimension_subclass(std::size_t kind) {
    static constexpr const char *names[] = {
        "AcDbAlignedDimension",   "AcDbAlignedDimension", "AcDb2LineAngularDimension",
        "AcDbDiametricDimension", "AcDbRadialDimension",  "AcDb3PointAngularDimension",
        "AcDbOrdinateDimension",  "AcDbArcDimension",     "AcDbRadialDimensionLarge"};
    return names[kind];
}
double dimension_default_measurement(const DimensionDefinition &v) {
    return std::visit(
        [&](const auto &g) -> double {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, LinearDimension> || std::is_same_v<T, AlignedDimension>)
                return (g.second_point - g.first_point).length();
            else if constexpr (std::is_same_v<T, RadiusDimension>)
                return (g.chord_point - v.definition_point).length();
            else if constexpr (std::is_same_v<T, DiameterDimension>)
                return (g.angle_vertex - v.definition_point).length();
            else if constexpr (std::is_same_v<T, OrdinateDimension>) {
                const auto delta =
                    Matrix3::arbitrary_axis(v.normal).transpose() * (g.feature_location - v.definition_point);
                const auto s = std::sin(-v.horizontal_direction), c = std::cos(-v.horizontal_direction);
                return std::abs(g.is_x ? delta.x * c + delta.y * s : -delta.x * s + delta.y * c);
            } else
                return 0;
        },
        v.geometry);
}
} // namespace
DimensionDefinition DxfDimension::definition() const {
    constexpr auto base = "AcDbDimension";
    const auto flags = short_field(base, 70, 0);
    std::size_t kind = static_cast<unsigned>(flags) & 15;
    if (type() == "ARC_DIMENSION")
        kind = 7;
    else if (type() == "LARGE_RADIAL_DIMENSION")
        kind = 8;
    else if (kind > 6)
        kind = 0;
    const auto *scope = dimension_subclass(kind);
    DimensionDefinition v;
    v.insertion_rotation = insertion_rotation_;
    v.definition_point = point_or(base, 10, {});
    v.text_middle_point = point_or(base, 11, {});
    v.insertion_point = point_or(base, 12, {});
    v.normal = kind >= 7 ? Point3{number(base, 210, 0), number(base, 220, 0), number(base, 230, 1)}
                         : point_or(base, 210, {0, 0, 1});
    v.style_name = text_field(base, 3, "Standard");
    v.block_name = text_field(base, 2);
    if (locate(base, 1).found)
        v.text_override = text_field(base, 1);
    v.text_rotation = number(base, 53, 0) * radians_per_degree;
    v.horizontal_direction = number(base, 51, 0) * radians_per_degree;
    v.line_spacing_factor = number(base, 41, 1);
    if (kind >= 7)
        locate_if(base, [&](std::size_t at, int code) {
            if (code == 41 || code == 44)
                v.line_spacing_factor = number_at(at);
            return false;
        });
    const auto attachment = short_field(base, 71, 5);
    v.attachment_point = attachment >= 1 && attachment <= 9 ? static_cast<std::int16_t>(attachment) : 5;
    v.line_spacing_style = static_cast<std::int16_t>(short_field(base, 72, 1));
    v.text_user_positioned = (flags & 128) != 0;
    v.flip_arrow1 = short_field(base, 74, 0) != 0;
    v.flip_arrow2 = short_field(base, 75, 0) != 0;
    auto p = [&](int code) { return point_or(scope, code, {}); };
    switch (kind) {
    case 0:
        v.geometry = LinearDimension{p(13), p(14), number(scope, 50, 0) * radians_per_degree,
                                     number(scope, 52, 0) * radians_per_degree};
        break;
    case 1:
        v.geometry = AlignedDimension{p(13), p(14), number(scope, 52, 0) * radians_per_degree};
        break;
    case 2:
        v.geometry = Angular2LineDimension{p(13), p(14), p(15), p(16)};
        break;
    case 3:
        v.geometry = DiameterDimension{p(15), number(scope, 40, 0)};
        break;
    case 4:
        v.geometry = RadiusDimension{p(15), number(scope, 40, 0)};
        break;
    case 5:
        v.geometry = Angular3PointDimension{p(13), p(14), p(15)};
        break;
    case 6:
        v.geometry = OrdinateDimension{p(13), p(14), (flags & 64) != 0};
        break;
    case 7:
        v.geometry = ArcDimension{p(13),
                                  p(14),
                                  p(15),
                                  short_field(scope, 70, 0) != 0,
                                  short_field(scope, 71, 0) != 0,
                                  number(scope, 40, 0),
                                  number(scope, 41, 0),
                                  p(16),
                                  p(17)};
        break;
    case 8:
        v.geometry = LargeRadialDimension{p(13), p(14), p(15), number(scope, 40, 0)};
        break;
    }
    v.actual_measurement =
        kind == 6 ? dimension_default_measurement(v) : number(base, 42, dimension_default_measurement(v));
    return v;
}
void DxfDimension::set_definition(const DimensionDefinition &v) {
    constexpr auto base = "AcDbDimension";
    const auto kind = v.geometry.index();
    const auto *scope = dimension_subclass(kind);
    if (definition().geometry.index() != kind)
        throw std::invalid_argument("Cannot change an existing dimension's type");
    valid_symbol_name(v.style_name);
    if (v.attachment_point < 1 || v.attachment_point > 9)
        throw std::invalid_argument("Invalid dimension attachment point");
    if (!std::isfinite(v.insertion_rotation))
        throw std::invalid_argument("Nonfinite dimension insertion rotation");
    DxfDimension staged = *this;
    staged.insertion_rotation_ = v.insertion_rotation;
    auto remove = [&](const char *sub, int code) {
        const auto at = staged.locate(sub, code);
        if (at.found)
            staged.pairs_.erase(staged.pairs_.begin() + at.index);
    };
    staged.set_point(base, 10, v.definition_point);
    staged.set_point(base, 11, v.text_middle_point);
    staged.set_point(base, 12, v.insertion_point);
    staged.set_point(base, 210, v.normal);
    staged.set_text_field(base, 3, v.style_name);
    if (v.block_name.empty())
        remove(base, 2);
    else
        staged.set_text_field(base, 2, v.block_name);
    if (v.text_override)
        staged.set_text_field(base, 1, *v.text_override);
    else
        remove(base, 1);
    staged.set_number(base, 53, v.text_rotation / radians_per_degree);
    staged.set_number(base, 51, v.horizontal_direction / radians_per_degree);
    staged.set_number(base, 41, v.line_spacing_factor);
    remove(base, 44);
    staged.set_short_field(base, 71, v.attachment_point);
    staged.set_short_field(base, 72, v.line_spacing_style);
    staged.set_short_field(base, 74, v.flip_arrow1);
    staged.set_short_field(base, 75, v.flip_arrow2);
    int flags = (kind == 7   ? 8
                 : kind == 8 ? 4
                             : static_cast<int>(kind)) |
                32 | (v.text_user_positioned ? 128 : 0);
    if (kind == 6 && std::get<OrdinateDimension>(v.geometry).is_x)
        flags |= 64;
    // Preserve unknown high flags, while keeping ordinate and text-position bits independent.
    flags |= short_field(base, 70, 0) & ~255;
    staged.set_short_field(base, 70, flags);
    staged.set_number(base, 42,
                      kind == 6 ? dimension_default_measurement(v)
                                : v.actual_measurement.value_or(dimension_default_measurement(v)));
    auto p = [&](int code, Point3 point) { staged.set_point(scope, code, point); };
    auto n = [&](int code, double number) { staged.set_number(scope, code, number); };
    std::visit(
        [&](const auto &g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, LinearDimension> || std::is_same_v<T, AlignedDimension>) {
                p(13, g.first_point);
                p(14, g.second_point);
                n(52, g.ext_line_rotation / radians_per_degree);
                if constexpr (std::is_same_v<T, LinearDimension>)
                    n(50, g.rotation / radians_per_degree);
            } else if constexpr (std::is_same_v<T, Angular2LineDimension>) {
                p(13, g.first_point);
                p(14, g.second_point);
                p(15, g.angle_vertex);
                p(16, g.dimension_arc);
            } else if constexpr (std::is_same_v<T, Angular3PointDimension>) {
                p(13, g.first_point);
                p(14, g.second_point);
                p(15, g.angle_vertex);
            } else if constexpr (std::is_same_v<T, DiameterDimension>) {
                p(15, g.angle_vertex);
                n(40, g.leader_length);
            } else if constexpr (std::is_same_v<T, RadiusDimension>) {
                p(15, g.chord_point);
                n(40, g.leader_length);
            } else if constexpr (std::is_same_v<T, OrdinateDimension>) {
                p(13, g.feature_location);
                p(14, g.leader_endpoint);
            } else if constexpr (std::is_same_v<T, ArcDimension>) {
                p(13, g.first_extension_point);
                p(14, g.second_extension_point);
                p(15, g.center_point);
                staged.set_short_field(scope, 70, g.partial);
                staged.set_short_field(scope, 71, g.has_leader);
                n(40, g.start_parameter);
                n(41, g.end_parameter);
                p(16, g.first_leader_point);
                p(17, g.second_leader_point);
            } else if constexpr (std::is_same_v<T, LargeRadialDimension>) {
                p(13, g.jog_point);
                p(14, g.override_center);
                p(15, g.chord_point);
                n(40, g.jog_angle);
            }
        },
        v.geometry);
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_dimension(const DimensionDefinition &v, const EntityOptions &options) {
    if (version_ < DxfVersion::AC1024)
        throw std::invalid_argument("Full dimension construction requires R2010 or later");
    if (!get_symbol(SymbolTable::DimStyle, v.style_name))
        throw std::invalid_argument("Missing dimension style");
    if (!v.block_name.empty() && !get_symbol(SymbolTable::BlockRecord, v.block_name))
        throw std::invalid_argument("Missing dimension geometry block");
    const auto kind = v.geometry.index();
    DxfDimension entity;
    static_cast<DxfEntity &>(entity) = make_entity(kind == 7   ? "ARC_DIMENSION"
                                                   : kind == 8 ? "LARGE_RADIAL_DIMENSION"
                                                               : "DIMENSION",
                                                   "AcDbDimension", options);
    entity.set_short_field("AcDbDimension", 70, kind == 7 ? 8 : kind == 8 ? 4 : static_cast<int>(kind));
    entity.set_short_field("AcDbDimension", 280, 0);
    entity.pairs_.push_back({100, dimension_subclass(kind)});
    if (kind == 0)
        entity.pairs_.push_back({100, "AcDbRotatedDimension"});
    entity.set_definition(v);
    return insert_entity(std::move(entity), options);
}
