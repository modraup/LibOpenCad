// SPDX-License-Identifier: MPL-2.0
// Ported from acadrust tables/{ucs,view,vport}.rs and DXF section reader/writer.
std::optional<UcsDefinition> DxfDocument::get_ucs(std::string_view name) const {
    const auto locations = symbol_locations(SymbolTable::Ucs, name);
    if (locations.empty())
        return std::nullopt;
    const auto &r = record_at({locations.back(), std::nullopt, false});
    const auto *scope = symbol_subclass(SymbolTable::Ucs);
    UcsDefinition v;
    v.flags = static_cast<std::int16_t>(r.short_field(scope, 70, v.flags));
    v.origin = r.point_or(scope, 10, v.origin);
    v.x_axis = r.point_or(scope, 11, v.x_axis);
    v.y_axis = r.point_or(scope, 12, v.y_axis);
    v.elevation = r.number(scope, 146, v.elevation);
    v.ortho_type = static_cast<std::int16_t>(r.short_field(scope, 71, v.ortho_type));
    v.ortho_view_type = static_cast<std::int16_t>(r.short_field(scope, 79, v.ortho_view_type));
    v.named_ucs_handle = handle_value(r.field(scope, 345));
    v.base_ucs_handle = handle_value(r.field(scope, 346));
    return v;
}
void DxfDocument::set_ucs(std::string_view name, const UcsDefinition &v) {
    if (version_ < DxfVersion::AC1015)
        throw std::invalid_argument("Full Ucs definition requires AC1015 or later");
    const auto locations = symbol_locations(SymbolTable::Ucs, name);
    if (locations.size() != 1)
        throw std::invalid_argument("Ucs must resolve uniquely");
    DxfRecord r = record_at({locations.front(), std::nullopt, false});
    const auto *scope = symbol_subclass(SymbolTable::Ucs);
    r.set_short_field(scope, 70, v.flags);
    r.set_point(scope, 10, v.origin);
    r.set_point(scope, 11, v.x_axis);
    r.set_point(scope, 12, v.y_axis);
    r.set_number(scope, 146, v.elevation);
    r.set_short_field(scope, 71, v.ortho_type);
    r.set_short_field(scope, 79, v.ortho_view_type);
    if (v.named_ucs_handle && records_with_handle(hex_handle(v.named_ucs_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous Ucs reference");
    r.set_field(scope, 345, hex_handle(v.named_ucs_handle));
    if (v.base_ucs_handle && records_with_handle(hex_handle(v.base_ucs_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous Ucs reference");
    r.set_field(scope, 346, hex_handle(v.base_ucs_handle));
    const_cast<DxfRecord &>(record_at({locations.front(), std::nullopt, false})) = std::move(r);
    rebuild_indexes();
}
std::uint64_t DxfDocument::add_ucs(std::string name, const UcsDefinition &value) {
    DxfDocument staged = *this;
    const auto id = staged.add_symbol(SymbolTable::Ucs, name);
    staged.set_ucs(name, value);
    *this = std::move(staged);
    return id;
}
std::optional<ViewDefinition> DxfDocument::get_view(std::string_view name) const {
    const auto locations = symbol_locations(SymbolTable::View, name);
    if (locations.empty())
        return std::nullopt;
    const auto &r = record_at({locations.back(), std::nullopt, false});
    const auto *scope = symbol_subclass(SymbolTable::View);
    ViewDefinition v;
    v.flags = static_cast<std::int16_t>(r.short_field(scope, 70, v.flags));
    v.view_mode = static_cast<std::int16_t>(r.short_field(scope, 71, v.view_mode));
    v.center = r.point_or(scope, 10, v.center);
    v.direction = r.point_or(scope, 11, v.direction);
    v.target = r.point_or(scope, 12, v.target);
    v.height = r.number(scope, 40, v.height);
    v.width = r.number(scope, 41, v.width);
    v.lens_length = r.number(scope, 42, v.lens_length);
    v.front_clip = r.number(scope, 43, v.front_clip);
    v.back_clip = r.number(scope, 44, v.back_clip);
    v.twist_angle = r.number(scope, 50, v.twist_angle) * radians_per_degree;
    v.ucs_associated = r.short_field(scope, 72, v.ucs_associated) != 0;
    v.camera_plottable = r.short_field(scope, 73, v.camera_plottable) != 0;
    v.render_mode = static_cast<std::int16_t>(r.short_field(scope, 281, v.render_mode));
    v.ucs_origin = r.point_or(scope, 110, v.ucs_origin);
    v.ucs_x_axis = r.point_or(scope, 111, v.ucs_x_axis);
    v.ucs_y_axis = r.point_or(scope, 112, v.ucs_y_axis);
    v.ucs_elevation = r.number(scope, 146, v.ucs_elevation);
    v.ucs_ortho_type = static_cast<std::int16_t>(r.short_field(scope, 79, v.ucs_ortho_type));
    v.named_ucs_handle = handle_value(r.field(scope, 345));
    v.base_ucs_handle = handle_value(r.field(scope, 346));
    v.background_handle = handle_value(r.field(scope, 332));
    v.visual_style_handle = handle_value(r.field(scope, 348));
    v.sun_handle = handle_value(r.field(scope, 361));
    v.use_default_lights = r.short_field(scope, 292, v.use_default_lights) != 0;
    v.default_lighting_type = static_cast<std::int16_t>(r.short_field(scope, 282, v.default_lighting_type));
    v.brightness = r.number(scope, 141, v.brightness);
    v.contrast = r.number(scope, 142, v.contrast);
    r.locate_if(scope, [&](std::size_t at, int code) {
        if (code == 63)
            v.ambient_color = Color::from_index(r.short_field(scope, 63, 250));
        if (code == 421)
            if (auto rgb = detail::as_i32_bits(r.pairs_[at].value, r.binary_))
                v.ambient_color = Color::from_true_color_value(*rgb);
        return false;
    });
    v.live_section_handle = handle_value(r.field(scope, 334));
    return v;
}
void DxfDocument::set_view(std::string_view name, const ViewDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Full View definition requires AC1021 or later");
    const auto locations = symbol_locations(SymbolTable::View, name);
    if (locations.size() != 1)
        throw std::invalid_argument("View must resolve uniquely");
    DxfRecord r = record_at({locations.front(), std::nullopt, false});
    const auto *scope = symbol_subclass(SymbolTable::View);
    r.set_short_field(scope, 70, v.flags);
    r.set_short_field(scope, 71, v.view_mode);
    r.set_point(scope, 10, v.center);
    r.set_point(scope, 11, v.direction);
    r.set_point(scope, 12, v.target);
    r.set_number(scope, 40, v.height);
    r.set_number(scope, 41, v.width);
    r.set_number(scope, 42, v.lens_length);
    r.set_number(scope, 43, v.front_clip);
    r.set_number(scope, 44, v.back_clip);
    r.set_number(scope, 50, v.twist_angle / radians_per_degree);
    r.set_short_field(scope, 72, v.ucs_associated ? 1 : 0);
    r.set_short_field(scope, 73, v.camera_plottable ? 1 : 0);
    r.set_short_field(scope, 281, v.render_mode);
    r.set_point(scope, 110, v.ucs_origin);
    r.set_point(scope, 111, v.ucs_x_axis);
    r.set_point(scope, 112, v.ucs_y_axis);
    r.set_number(scope, 146, v.ucs_elevation);
    r.set_short_field(scope, 79, v.ucs_ortho_type);
    if (v.named_ucs_handle && records_with_handle(hex_handle(v.named_ucs_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous View reference");
    r.set_field(scope, 345, hex_handle(v.named_ucs_handle));
    if (v.base_ucs_handle && records_with_handle(hex_handle(v.base_ucs_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous View reference");
    r.set_field(scope, 346, hex_handle(v.base_ucs_handle));
    if (v.background_handle && records_with_handle(hex_handle(v.background_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous View reference");
    r.set_field(scope, 332, hex_handle(v.background_handle));
    if (v.visual_style_handle && records_with_handle(hex_handle(v.visual_style_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous View reference");
    r.set_field(scope, 348, hex_handle(v.visual_style_handle));
    if (v.sun_handle && records_with_handle(hex_handle(v.sun_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous View reference");
    r.set_field(scope, 361, hex_handle(v.sun_handle));
    r.set_field(scope, 292,
                r.binary_ ? std::string(1, static_cast<char>(v.use_default_lights))
                          : (v.use_default_lights ? "1" : "0"));
    r.set_short_field(scope, 282, v.default_lighting_type);
    r.set_number(scope, 141, v.brightness);
    r.set_number(scope, 142, v.contrast);
    r.set_short_field(scope, 63, v.ambient_color.index().value_or(v.ambient_color.approximate_index()));
    const auto position = r.locate(scope, 421);
    if (position.found)
        r.pairs_.erase(r.pairs_.begin() + position.index);
    if (const auto rgb = v.ambient_color.to_true_color_value())
        r.set_field(scope, 421, r.binary_ ? detail::binary_int32_bits(*rgb) : std::to_string(*rgb));
    if (v.live_section_handle && records_with_handle(hex_handle(v.live_section_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous View reference");
    r.set_field(scope, 334, hex_handle(v.live_section_handle));
    const_cast<DxfRecord &>(record_at({locations.front(), std::nullopt, false})) = std::move(r);
    rebuild_indexes();
}
std::uint64_t DxfDocument::add_view(std::string name, const ViewDefinition &value) {
    DxfDocument staged = *this;
    const auto id = staged.add_symbol(SymbolTable::View, name);
    staged.set_view(name, value);
    *this = std::move(staged);
    return id;
}
std::optional<VPortDefinition> DxfDocument::get_vport(std::string_view name) const {
    const auto locations = symbol_locations(SymbolTable::VPort, name);
    if (locations.empty())
        return std::nullopt;
    const auto &r = record_at({locations.back(), std::nullopt, false});
    const auto *scope = symbol_subclass(SymbolTable::VPort);
    VPortDefinition v;
    v.flags = static_cast<std::int16_t>(r.short_field(scope, 70, v.flags));
    v.view_mode = static_cast<std::int16_t>(r.short_field(scope, 71, v.view_mode));
    v.lower_left = {r.number(scope, 10, v.lower_left.x), r.number(scope, 20, v.lower_left.y)};
    v.upper_right = {r.number(scope, 11, v.upper_right.x), r.number(scope, 21, v.upper_right.y)};
    v.view_center = {r.number(scope, 12, v.view_center.x), r.number(scope, 22, v.view_center.y)};
    v.snap_base = {r.number(scope, 13, v.snap_base.x), r.number(scope, 23, v.snap_base.y)};
    v.snap_spacing = {r.number(scope, 14, v.snap_spacing.x), r.number(scope, 24, v.snap_spacing.y)};
    v.grid_spacing = {r.number(scope, 15, v.grid_spacing.x), r.number(scope, 25, v.grid_spacing.y)};
    v.view_direction = {r.number(scope, 16, v.view_direction.x), r.number(scope, 26, v.view_direction.y),
                        r.number(scope, 36, v.view_direction.z)};
    v.view_target = {r.number(scope, 17, v.view_target.x), r.number(scope, 27, v.view_target.y),
                     r.number(scope, 37, v.view_target.z)};
    v.view_height = r.number(scope, 40, v.view_height);
    v.aspect_ratio = r.number(scope, 41, v.aspect_ratio);
    v.lens_length = r.number(scope, 42, v.lens_length);
    v.front_clip = r.number(scope, 43, v.front_clip);
    v.back_clip = r.number(scope, 44, v.back_clip);
    v.snap_rotation = r.number(scope, 50, v.snap_rotation) * radians_per_degree;
    v.view_twist = r.number(scope, 51, v.view_twist) * radians_per_degree;
    v.circle_zoom = static_cast<std::int16_t>(r.short_field(scope, 72, v.circle_zoom));
    v.fast_zoom = r.short_field(scope, 73, v.fast_zoom) != 0;
    v.ucs_icon_flags = static_cast<std::int16_t>(r.short_field(scope, 74, v.ucs_icon_flags));
    v.snap_on = r.short_field(scope, 75, v.snap_on) != 0;
    v.grid_on = r.short_field(scope, 76, v.grid_on) != 0;
    v.snap_style = r.short_field(scope, 77, v.snap_style) != 0;
    v.snap_isopair = static_cast<std::int16_t>(r.short_field(scope, 78, v.snap_isopair));
    v.grid_flags = static_cast<std::int16_t>(r.short_field(scope, 60, v.grid_flags));
    v.grid_major = static_cast<std::int16_t>(r.short_field(scope, 61, v.grid_major));
    v.ucs_per_viewport = r.short_field(scope, 65, v.ucs_per_viewport) != 0;
    v.render_mode = static_cast<std::int16_t>(r.short_field(scope, 281, v.render_mode));
    v.ucs_origin = {r.number(scope, 110, v.ucs_origin.x), r.number(scope, 120, v.ucs_origin.y),
                    r.number(scope, 130, v.ucs_origin.z)};
    v.ucs_x_axis = {r.number(scope, 111, v.ucs_x_axis.x), r.number(scope, 121, v.ucs_x_axis.y),
                    r.number(scope, 131, v.ucs_x_axis.z)};
    v.ucs_y_axis = {r.number(scope, 112, v.ucs_y_axis.x), r.number(scope, 122, v.ucs_y_axis.y),
                    r.number(scope, 132, v.ucs_y_axis.z)};
    v.ucs_elevation = r.number(scope, 146, v.ucs_elevation);
    v.ucs_ortho_type = static_cast<std::int16_t>(r.short_field(scope, 79, v.ucs_ortho_type));
    v.named_ucs_handle = handle_value(r.field(scope, 345));
    v.base_ucs_handle = handle_value(r.field(scope, 346));
    v.background_handle = handle_value(r.field(scope, 332));
    v.visual_style_handle = handle_value(r.field(scope, 348));
    v.sun_handle = handle_value(r.field(scope, 361));
    v.use_default_lights = r.short_field(scope, 292, v.use_default_lights) != 0;
    v.default_lighting_type = static_cast<std::int16_t>(r.short_field(scope, 282, v.default_lighting_type));
    v.brightness = r.number(scope, 141, v.brightness);
    v.contrast = r.number(scope, 142, v.contrast);
    r.locate_if(scope, [&](std::size_t at, int code) {
        if (code == 63)
            v.ambient_color = Color::from_index(r.short_field(scope, 63, 250));
        if (code == 421)
            if (auto rgb = detail::as_i32_bits(r.pairs_[at].value, r.binary_))
                v.ambient_color = Color::from_true_color_value(*rgb);
        return false;
    });
    return v;
}
void DxfDocument::set_vport(std::string_view name, const VPortDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Full VPort definition requires AC1021 or later");
    const auto locations = symbol_locations(SymbolTable::VPort, name);
    if (locations.size() != 1)
        throw std::invalid_argument("VPort must resolve uniquely");
    DxfRecord r = record_at({locations.front(), std::nullopt, false});
    const auto *scope = symbol_subclass(SymbolTable::VPort);
    r.set_short_field(scope, 70, v.flags);
    r.set_short_field(scope, 71, v.view_mode);
    r.set_number(scope, 10, v.lower_left.x);
    r.set_number(scope, 20, v.lower_left.y);
    r.set_number(scope, 11, v.upper_right.x);
    r.set_number(scope, 21, v.upper_right.y);
    r.set_number(scope, 12, v.view_center.x);
    r.set_number(scope, 22, v.view_center.y);
    r.set_number(scope, 13, v.snap_base.x);
    r.set_number(scope, 23, v.snap_base.y);
    r.set_number(scope, 14, v.snap_spacing.x);
    r.set_number(scope, 24, v.snap_spacing.y);
    r.set_number(scope, 15, v.grid_spacing.x);
    r.set_number(scope, 25, v.grid_spacing.y);
    r.set_point(scope, 16, v.view_direction);
    r.set_point(scope, 17, v.view_target);
    r.set_number(scope, 40, v.view_height);
    r.set_number(scope, 41, v.aspect_ratio);
    r.set_number(scope, 42, v.lens_length);
    r.set_number(scope, 43, v.front_clip);
    r.set_number(scope, 44, v.back_clip);
    r.set_number(scope, 50, v.snap_rotation / radians_per_degree);
    r.set_number(scope, 51, v.view_twist / radians_per_degree);
    r.set_short_field(scope, 72, v.circle_zoom);
    r.set_short_field(scope, 73, v.fast_zoom ? 1 : 0);
    r.set_short_field(scope, 74, v.ucs_icon_flags);
    r.set_short_field(scope, 75, v.snap_on ? 1 : 0);
    r.set_short_field(scope, 76, v.grid_on ? 1 : 0);
    r.set_short_field(scope, 77, v.snap_style ? 1 : 0);
    r.set_short_field(scope, 78, v.snap_isopair);
    r.set_short_field(scope, 60, v.grid_flags);
    r.set_short_field(scope, 61, v.grid_major);
    r.set_short_field(scope, 65, v.ucs_per_viewport ? 1 : 0);
    r.set_short_field(scope, 281, v.render_mode);
    r.set_point(scope, 110, v.ucs_origin);
    r.set_point(scope, 111, v.ucs_x_axis);
    r.set_point(scope, 112, v.ucs_y_axis);
    r.set_number(scope, 146, v.ucs_elevation);
    r.set_short_field(scope, 79, v.ucs_ortho_type);
    if (v.named_ucs_handle && records_with_handle(hex_handle(v.named_ucs_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous VPort reference");
    r.set_field(scope, 345, hex_handle(v.named_ucs_handle));
    if (v.base_ucs_handle && records_with_handle(hex_handle(v.base_ucs_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous VPort reference");
    r.set_field(scope, 346, hex_handle(v.base_ucs_handle));
    if (v.background_handle && records_with_handle(hex_handle(v.background_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous VPort reference");
    r.set_field(scope, 332, hex_handle(v.background_handle));
    if (v.visual_style_handle && records_with_handle(hex_handle(v.visual_style_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous VPort reference");
    r.set_field(scope, 348, hex_handle(v.visual_style_handle));
    if (v.sun_handle && records_with_handle(hex_handle(v.sun_handle)).size() != 1)
        throw std::invalid_argument("Missing or ambiguous VPort reference");
    r.set_field(scope, 361, hex_handle(v.sun_handle));
    r.set_field(scope, 292,
                r.binary_ ? std::string(1, static_cast<char>(v.use_default_lights))
                          : (v.use_default_lights ? "1" : "0"));
    r.set_short_field(scope, 282, v.default_lighting_type);
    r.set_number(scope, 141, v.brightness);
    r.set_number(scope, 142, v.contrast);
    r.set_short_field(scope, 63, v.ambient_color.index().value_or(v.ambient_color.approximate_index()));
    const auto position = r.locate(scope, 421);
    if (position.found)
        r.pairs_.erase(r.pairs_.begin() + position.index);
    if (const auto rgb = v.ambient_color.to_true_color_value())
        r.set_field(scope, 421, r.binary_ ? detail::binary_int32_bits(*rgb) : std::to_string(*rgb));
    const_cast<DxfRecord &>(record_at({locations.front(), std::nullopt, false})) = std::move(r);
    rebuild_indexes();
}
std::uint64_t DxfDocument::add_vport(std::string name, const VPortDefinition &value) {
    DxfDocument staged = *this;
    const auto id = staged.add_symbol(SymbolTable::VPort, name);
    staged.set_vport(name, value);
    *this = std::move(staged);
    return id;
}

namespace {
// Matches ExtendedData::merge_modeled_record: retain unmodeled app values.
ExtendedDataRecord merge_modeled_xdata(ExtendedDataRecord source, const ExtendedDataRecord &replacement) {
    const auto &values = replacement.values;
    const bool labeled =
        values.size() > 1 && values[1].code == 1002 && std::get<std::string>(values[1].value) == "{";
    if (labeled) {
        auto start = std::find_if(source.values.begin(), source.values.end(), [&](const auto &v) {
            return v.code == values.front().code && v.value == values.front().value;
        });
        if (start != source.values.end()) {
            int depth = 0;
            for (auto end = start + 1; end != source.values.end(); ++end) {
                if (end->code != 1002)
                    continue;
                const auto &control = std::get<std::string>(end->value);
                if (control == "{")
                    ++depth;
                if (control == "}" && --depth == 0) {
                    const auto offset = start - source.values.begin();
                    source.values.erase(start, end + 1);
                    source.values.insert(source.values.begin() + offset, values.begin(), values.end());
                    return source;
                }
            }
        }
        source.values.insert(source.values.end(), values.begin(), values.end());
    } else {
        std::size_t cursor = 0;
        for (const auto &v : values) {
            const auto match = std::find_if(source.values.begin() + cursor, source.values.end(),
                                            [&](const auto &old) { return old.code == v.code; });
            if (match == source.values.end()) {
                source.values.push_back(v);
                cursor = source.values.size();
            } else {
                *match = v;
                cursor = static_cast<std::size_t>(match - source.values.begin()) + 1;
            }
        }
    }
    return source;
}
} // namespace
std::optional<TextStyleDefinition> DxfDocument::get_text_style(std::string_view name) const {
    const auto locations = symbol_locations(SymbolTable::TextStyle, name);
    if (locations.empty())
        return std::nullopt;
    return get_text_style_by_handle(record_at({locations.back(), std::nullopt, false}).handle());
}
std::optional<TextStyleDefinition> DxfDocument::get_text_style_by_handle(std::string_view handle) const {
    const auto *found = get_record(handle);
    if (!found)
        return std::nullopt;
    if (records_with_handle(handle).size() != 1 || found->type() != "STYLE")
        throw std::invalid_argument("Expected unique STYLE");
    const auto &r = *found;
    const auto *scope = symbol_subclass(SymbolTable::TextStyle);
    TextStyleDefinition v;
    v.flags = static_cast<std::int16_t>(r.short_field(scope, 70, 0));
    v.generation_flags = static_cast<std::int16_t>(r.short_field(scope, 71, 0));
    v.height = r.number(scope, 40, 0);
    v.width_factor = r.number(scope, 41, 1);
    v.oblique_angle = r.number(scope, 50, 0) * radians_per_degree;
    v.last_height = r.number(scope, 42, 2.5);
    v.font_file = r.text_field(scope, 3, "txt");
    v.big_font_file = r.text_field(scope, 4);
    bool acad = false, annotative = false;
    for (const auto &data : r.extended_data()) {
        if (data.application_name == "ACAD" && !acad) {
            acad = true;
            bool font = false, flags = false;
            for (const auto &item : data.values) {
                if (item.code == 1000 && !font) {
                    v.true_type_font = std::get<std::string>(item.value);
                    font = true;
                }
                if (item.code == 1071 && !flags) {
                    v.true_type_font_flags = std::get<std::int32_t>(item.value);
                    flags = true;
                }
            }
        }
        if (data.application_name == "AcadAnnotative" && !annotative) {
            annotative = true;
            for (const auto &item : data.values)
                if (item.code == 1070)
                    v.annotative = std::get<std::int16_t>(item.value) != 0;
        }
    }
    return v;
}
void DxfDocument::set_text_style(std::string_view name, const TextStyleDefinition &v) {
    const auto locations = symbol_locations(SymbolTable::TextStyle, name);
    if (locations.size() != 1)
        throw std::invalid_argument("Text style must resolve uniquely");
    set_text_style_by_handle(record_at({locations.front(), std::nullopt, false}).handle(), v);
}
void DxfDocument::set_text_style_by_handle(std::string_view handle, const TextStyleDefinition &v) {
    if (!get_text_style_by_handle(handle))
        throw std::invalid_argument("Missing STYLE");
    DxfDocument staged = *this;
    auto &r = const_cast<DxfRecord &>(*staged.get_record(handle));
    const auto *scope = symbol_subclass(SymbolTable::TextStyle);
    // Switching between named text and anonymous shape styles changes lookup identity.
    if ((r.short_field(scope, 70, 0) ^ v.flags) & 1)
        throw std::invalid_argument("Cannot change a named text style into a shape-file style");
    r.set_short_field(scope, 70, v.flags);
    r.set_short_field(scope, 71, v.generation_flags);
    r.set_number(scope, 40, v.height);
    r.set_number(scope, 41, v.width_factor);
    r.set_number(scope, 50, v.oblique_angle / radians_per_degree);
    finite(v.last_height);
    r.set_number(scope, 42, v.last_height > 0 ? v.last_height : 2.5);
    r.set_text_field(scope, 3, v.font_file);
    r.set_text_field(scope, 4, v.big_font_file);
    auto source = r.extended_data();
    std::vector<std::string> apps;
    auto apply = [&](ExtendedDataRecord replacement, bool requested) {
        auto found = std::find_if(source.begin(), source.end(), [&](const auto &item) {
            return item.application_name == replacement.application_name;
        });
        if (!requested && found == source.end())
            return;
        auto merged = found == source.end() ? replacement : merge_modeled_xdata(*found, replacement);
        apps.push_back(merged.application_name);
        r.upsert_extended_data(merged);
    };
    apply({"ACAD", {{1000, std::string(trim(v.true_type_font))}, {1071, v.true_type_font_flags}}},
          !trim(v.true_type_font).empty());
    apply({"AcadAnnotative",
           {{1000, std::string("AnnotativeData")},
            {1002, std::string("{")},
            {1070, std::int16_t{1}},
            {1070, static_cast<std::int16_t>(v.annotative)},
            {1002, std::string("}")}}},
          v.annotative);
    for (const auto &app : apps)
        if (!staged.get_symbol(SymbolTable::AppId, app))
            staged.add_symbol(SymbolTable::AppId, app);
    staged.rebuild_indexes();
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_text_style(std::string name, const TextStyleDefinition &value) {
    if (value.flags & 1)
        throw std::invalid_argument("Use a named text style, not a shape-file STYLE");
    DxfDocument staged = *this;
    const auto id = staged.add_symbol(SymbolTable::TextStyle, name);
    staged.set_text_style(name, value);
    *this = std::move(staged);
    return id;
}

// Dimension style defaults and field codes follow tables/dimstyle.rs.
std::optional<DimStyleDefinition> DxfDocument::get_dim_style(std::string_view name) const {
    const auto locations = symbol_locations(SymbolTable::DimStyle, name);
    if (locations.empty())
        return std::nullopt;
    const auto &r = record_at({locations.back(), std::nullopt, false});
    const auto *scope = symbol_subclass(SymbolTable::DimStyle);
    DimStyleDefinition v;
    v.dimclrd = static_cast<std::int16_t>(r.short_field(scope, 176, v.dimclrd));
    v.dimdle = r.number(scope, 46, v.dimdle);
    v.dimdli = r.number(scope, 43, 0.38);
    v.dimgap = r.number(scope, 147, 0.09);
    v.dimlwd = static_cast<std::int16_t>(r.short_field(scope, 371, v.dimlwd));
    v.dimsd1 = r.short_field(scope, 281, v.dimsd1) != 0;
    v.dimsd2 = r.short_field(scope, 282, v.dimsd2) != 0;
    v.dimclre = static_cast<std::int16_t>(r.short_field(scope, 177, v.dimclre));
    v.dimexe = r.number(scope, 44, 0.18);
    v.dimexo = r.number(scope, 42, 0.0625);
    v.dimlwe = static_cast<std::int16_t>(r.short_field(scope, 372, v.dimlwe));
    v.dimse1 = r.short_field(scope, 75, v.dimse1) != 0;
    v.dimse2 = r.short_field(scope, 76, v.dimse2) != 0;
    v.dimfxl = r.number(scope, 49, v.dimfxl);
    v.dimfxlon = r.short_field(scope, 290, v.dimfxlon) != 0;
    v.dimasz = r.number(scope, 41, v.dimasz);
    v.dimblk = handle_value(r.field(scope, 342));
    v.dimblk1 = handle_value(r.field(scope, 343));
    v.dimblk2 = handle_value(r.field(scope, 344));
    v.dimldrblk = handle_value(r.field(scope, 341));
    v.dimsah = r.short_field(scope, 173, false) != 0;
    v.dimcen = r.number(scope, 141, v.dimcen);
    v.dimtsz = r.number(scope, 142, v.dimtsz);
    v.dimclrt = static_cast<std::int16_t>(r.short_field(scope, 178, v.dimclrt));
    v.dimtxt = r.number(scope, 140, v.dimtxt);
    v.dimjust = static_cast<std::int16_t>(r.short_field(scope, 280, v.dimjust));
    v.dimtad = static_cast<std::int16_t>(r.short_field(scope, 77, 0));
    v.dimtvp = r.number(scope, 145, v.dimtvp);
    v.dimtih = r.short_field(scope, 73, true) != 0;
    v.dimtoh = r.short_field(scope, 74, true) != 0;
    v.dimtix = r.short_field(scope, 174, v.dimtix) != 0;
    v.dimsoxd = r.short_field(scope, 175, v.dimsoxd) != 0;
    v.dimtfill = static_cast<std::int16_t>(r.short_field(scope, 69, v.dimtfill));
    v.dimtmove = static_cast<std::int16_t>(r.short_field(scope, 279, v.dimtmove));
    v.dimtxsty_handle = handle_value(r.field(scope, 340));
    if (!v.dimtxsty_handle)
        if (const auto standard = get_symbol(SymbolTable::TextStyle, "Standard"))
            v.dimtxsty_handle = handle_value(standard->handle);
    v.dimscale = r.number(scope, 40, v.dimscale);
    v.dimlfac = r.number(scope, 144, v.dimlfac);
    v.dimlunit = static_cast<std::int16_t>(r.short_field(scope, 277, v.dimlunit));
    v.dimdec = static_cast<std::int16_t>(r.short_field(scope, 271, 4));
    v.dimrnd = r.number(scope, 45, v.dimrnd);
    v.dimdsep = static_cast<std::int16_t>(r.short_field(scope, 278, v.dimdsep));
    v.dimzin = static_cast<std::int16_t>(r.short_field(scope, 78, 0));
    v.dimaunit = static_cast<std::int16_t>(r.short_field(scope, 275, v.dimaunit));
    v.dimadec = static_cast<std::int16_t>(r.short_field(scope, 179, v.dimadec));
    v.dimazin = static_cast<std::int16_t>(r.short_field(scope, 79, v.dimazin));
    v.dimalt = r.short_field(scope, 170, v.dimalt) != 0;
    v.dimaltf = r.number(scope, 143, v.dimaltf);
    v.dimaltd = static_cast<std::int16_t>(r.short_field(scope, 171, 2));
    v.dimaltu = static_cast<std::int16_t>(r.short_field(scope, 273, v.dimaltu));
    v.dimalttd = static_cast<std::int16_t>(r.short_field(scope, 274, 2));
    v.dimaltrnd = r.number(scope, 148, v.dimaltrnd);
    v.dimapost = r.text_field(scope, 4, v.dimapost);
    v.dimaltz = static_cast<std::int16_t>(r.short_field(scope, 285, v.dimaltz));
    v.dimalttz = static_cast<std::int16_t>(r.short_field(scope, 286, v.dimalttz));
    v.dimtol = r.short_field(scope, 71, v.dimtol) != 0;
    v.dimlim = r.short_field(scope, 72, v.dimlim) != 0;
    v.dimtp = r.number(scope, 47, v.dimtp);
    v.dimtm = r.number(scope, 48, v.dimtm);
    v.dimtdec = static_cast<std::int16_t>(r.short_field(scope, 272, 4));
    v.dimtfac = r.number(scope, 146, v.dimtfac);
    v.dimtolj = static_cast<std::int16_t>(r.short_field(scope, 283, 1));
    v.dimtzin = static_cast<std::int16_t>(r.short_field(scope, 284, 0));
    v.dimatfit = static_cast<std::int16_t>(r.short_field(scope, 289, v.dimatfit));
    v.dimtofl = r.short_field(scope, 172, v.dimtofl) != 0;
    v.dimupt = r.short_field(scope, 288, v.dimupt) != 0;
    v.dimfit = static_cast<std::int16_t>(r.short_field(scope, 287, v.dimfit));
    v.dimpost = r.text_field(scope, 3, v.dimpost);
    v.dimfrac = static_cast<std::int16_t>(r.short_field(scope, 276, v.dimfrac));
    v.dimarcsym = r.locate(scope, 90).found
                      ? detail::as_i16(90, r.field(scope, 90), r.binary_).value_or(v.dimarcsym)
                      : v.dimarcsym;
    v.dimjogang = r.number(scope, 50, v.dimjogang / radians_per_degree) * radians_per_degree;
    v.dimltex_handle = handle_value(r.field(scope, 345));
    v.dimltex1_handle = handle_value(r.field(scope, 346));
    v.dimltex2_handle = handle_value(r.field(scope, 347));
    v.dimunit = static_cast<std::int16_t>(r.short_field(scope, 270, v.dimunit));
    bool seen_flags = false;
    r.locate_if(scope, [&](std::size_t at, int code) {
        if (code == 70) {
            const auto value = detail::as_i16(code, r.pairs_[at].value, r.binary_);
            if (value) {
                if (seen_flags)
                    v.dimtfillclr = *value;
                else
                    v.flags = *value;
                seen_flags = true;
            }
        }
        if (code == 294 || code == 295)
            if (auto value = detail::as_i16(code, r.pairs_[at].value, r.binary_))
                v.dimtxtdirection = *value != 0;
        return false;
    });
    for (const auto &app : r.extended_data())
        if (app.application_name == "AcadAnnotative") {
            for (const auto &item : app.values)
                if (item.code == 1070)
                    v.annotative = std::get<std::int16_t>(item.value) != 0;
            break;
        }
    return v;
}
void DxfDocument::set_dim_style(std::string_view name, const DimStyleDefinition &v) {
    if (version_ < DxfVersion::AC1024)
        throw std::invalid_argument("Full DimStyle definition requires R2010 or later");
    const auto locations = symbol_locations(SymbolTable::DimStyle, name);
    if (locations.size() != 1)
        throw std::invalid_argument("Dimension style must resolve uniquely");
    DxfDocument staged = *this;
    auto &r = const_cast<DxfRecord &>(staged.record_at({locations.front(), std::nullopt, false}));
    const auto *scope = symbol_subclass(SymbolTable::DimStyle);
    r.set_short_field(scope, 176, v.dimclrd);
    r.set_number(scope, 46, v.dimdle);
    r.set_number(scope, 43, v.dimdli);
    r.set_number(scope, 147, v.dimgap);
    r.set_short_field(scope, 371, v.dimlwd);
    r.set_short_field(scope, 281, v.dimsd1);
    r.set_short_field(scope, 282, v.dimsd2);
    r.set_short_field(scope, 177, v.dimclre);
    r.set_number(scope, 44, v.dimexe);
    r.set_number(scope, 42, v.dimexo);
    r.set_short_field(scope, 372, v.dimlwe);
    r.set_short_field(scope, 75, v.dimse1);
    r.set_short_field(scope, 76, v.dimse2);
    r.set_number(scope, 49, v.dimfxl);
    r.set_field(scope, 290,
                r.binary_ ? std::string(1, static_cast<char>(v.dimfxlon)) : (v.dimfxlon ? "1" : "0"));
    r.set_number(scope, 41, v.dimasz);
    if (v.dimblk && records_with_handle(hex_handle(v.dimblk)).size() != 1)
        throw std::invalid_argument("Missing dimension style reference");
    r.set_field(scope, 342, hex_handle(v.dimblk));
    if (v.dimblk1 && records_with_handle(hex_handle(v.dimblk1)).size() != 1)
        throw std::invalid_argument("Missing dimension style reference");
    r.set_field(scope, 343, hex_handle(v.dimblk1));
    if (v.dimblk2 && records_with_handle(hex_handle(v.dimblk2)).size() != 1)
        throw std::invalid_argument("Missing dimension style reference");
    r.set_field(scope, 344, hex_handle(v.dimblk2));
    if (v.dimldrblk && records_with_handle(hex_handle(v.dimldrblk)).size() != 1)
        throw std::invalid_argument("Missing dimension style reference");
    r.set_field(scope, 341, hex_handle(v.dimldrblk));
    r.set_short_field(scope, 173, v.dimsah);
    r.set_number(scope, 141, v.dimcen);
    r.set_number(scope, 142, v.dimtsz);
    r.set_short_field(scope, 178, v.dimclrt);
    r.set_number(scope, 140, v.dimtxt);
    r.set_short_field(scope, 280, v.dimjust);
    r.set_short_field(scope, 77, v.dimtad);
    r.set_number(scope, 145, v.dimtvp);
    r.set_short_field(scope, 73, v.dimtih);
    r.set_short_field(scope, 74, v.dimtoh);
    r.set_short_field(scope, 174, v.dimtix);
    r.set_short_field(scope, 175, v.dimsoxd);
    r.set_short_field(scope, 69, v.dimtfill);
    r.set_short_field(scope, 279, v.dimtmove);
    r.set_field(scope, 295,
                r.binary_ ? std::string(1, static_cast<char>(v.dimtxtdirection))
                          : (v.dimtxtdirection ? "1" : "0"));
    if (v.dimtxsty_handle && records_with_handle(hex_handle(v.dimtxsty_handle)).size() != 1)
        throw std::invalid_argument("Missing dimension style reference");
    r.set_field(scope, 340, hex_handle(v.dimtxsty_handle));
    r.set_number(scope, 40, v.dimscale);
    r.set_number(scope, 144, v.dimlfac);
    r.set_short_field(scope, 277, v.dimlunit);
    r.set_short_field(scope, 271, v.dimdec);
    r.set_number(scope, 45, v.dimrnd);
    r.set_short_field(scope, 278, v.dimdsep);
    r.set_short_field(scope, 78, v.dimzin);
    r.set_short_field(scope, 275, v.dimaunit);
    r.set_short_field(scope, 179, v.dimadec);
    r.set_short_field(scope, 79, v.dimazin);
    r.set_short_field(scope, 170, v.dimalt);
    r.set_number(scope, 143, v.dimaltf);
    r.set_short_field(scope, 171, v.dimaltd);
    r.set_short_field(scope, 273, v.dimaltu);
    r.set_short_field(scope, 274, v.dimalttd);
    r.set_number(scope, 148, v.dimaltrnd);
    r.set_text_field(scope, 4, v.dimapost);
    r.set_short_field(scope, 285, v.dimaltz);
    r.set_short_field(scope, 286, v.dimalttz);
    r.set_short_field(scope, 71, v.dimtol);
    r.set_short_field(scope, 72, v.dimlim);
    r.set_number(scope, 47, v.dimtp);
    r.set_number(scope, 48, v.dimtm);
    r.set_short_field(scope, 272, v.dimtdec);
    r.set_number(scope, 146, v.dimtfac);
    r.set_short_field(scope, 283, v.dimtolj);
    r.set_short_field(scope, 284, v.dimtzin);
    r.set_short_field(scope, 289, v.dimatfit);
    r.set_short_field(scope, 172, v.dimtofl);
    r.set_short_field(scope, 288, v.dimupt);
    r.set_short_field(scope, 287, v.dimfit);
    r.set_text_field(scope, 3, v.dimpost);
    r.set_short_field(scope, 276, v.dimfrac);
    r.set_field(scope, 90,
                r.binary_ ? detail::binary_int32_bits(static_cast<std::uint32_t>(v.dimarcsym))
                          : std::to_string(v.dimarcsym));
    finite(v.dimjogang);
    r.set_number(scope, 50, std::clamp(v.dimjogang / radians_per_degree, 5.0, 90.0));
    if (v.dimltex_handle && records_with_handle(hex_handle(v.dimltex_handle)).size() != 1)
        throw std::invalid_argument("Missing dimension style reference");
    r.set_field(scope, 345, hex_handle(v.dimltex_handle));
    if (v.dimltex1_handle && records_with_handle(hex_handle(v.dimltex1_handle)).size() != 1)
        throw std::invalid_argument("Missing dimension style reference");
    r.set_field(scope, 346, hex_handle(v.dimltex1_handle));
    if (v.dimltex2_handle && records_with_handle(hex_handle(v.dimltex2_handle)).size() != 1)
        throw std::invalid_argument("Missing dimension style reference");
    r.set_field(scope, 347, hex_handle(v.dimltex2_handle));
    r.set_short_field(scope, 270, v.dimunit);
    // Group 70 first means table flags; subsequent occurrences mean DIMTFILLCLR.
    std::vector<std::size_t> repeated;
    r.locate_if(scope, [&](std::size_t at, int code) {
        if (code == 70 || code == 294)
            repeated.push_back(at);
        return false;
    });
    for (auto it = repeated.rbegin(); it != repeated.rend(); ++it)
        r.pairs_.erase(r.pairs_.begin() + *it);
    r.set_short_field(scope, 70, v.flags);
    const auto position = r.locate(scope, 70).index;
    r.pairs_.insert(r.pairs_.begin() + position + 1,
                    {70, r.binary_ ? detail::binary_short(v.dimtfillclr) : std::to_string(v.dimtfillclr)});
    auto source = r.extended_data();
    const auto old = std::find_if(source.begin(), source.end(),
                                  [](const auto &app) { return app.application_name == "AcadAnnotative"; });
    if (v.annotative || old != source.end()) {
        ExtendedDataRecord replacement{"AcadAnnotative",
                                       {{1000, std::string("AnnotativeData")},
                                        {1002, std::string("{")},
                                        {1070, std::int16_t{1}},
                                        {1070, static_cast<std::int16_t>(v.annotative)},
                                        {1002, std::string("}")}}};
        r.upsert_extended_data(old == source.end() ? replacement : merge_modeled_xdata(*old, replacement));
        if (!staged.get_symbol(SymbolTable::AppId, "AcadAnnotative"))
            staged.add_symbol(SymbolTable::AppId, "AcadAnnotative");
    }
    staged.rebuild_indexes();
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_dim_style(std::string name, const DimStyleDefinition &value) {
    DxfDocument staged = *this;
    const auto id = staged.add_symbol(SymbolTable::DimStyle, name);
    staged.set_dim_style(name, value);
    *this = std::move(staged);
    return id;
}
