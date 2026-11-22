// SPDX-License-Identifier: MPL-2.0
// Native DWG class-object decode. Port of acadrust object_reader/class_object.rs
// for render/section-manager, render-scene, path/TV, layer filter, partial
// viewing index, section settings, sun study, data table, geomap image,
// model-doc view styles, data links, object data and ViewRep families.
Color read_cm_color(DynamicStream &s) {
    return read_packed_cm_color(s.main, [&] { return s.str(); });
}
RenderSettings read_render_settings(DynamicStream &s, bool rapid_rt, bool read_predefined_bit) {
    auto stored = s.main.bit_long();
    RenderSettings value;
    value.class_version = !rapid_rt && read_predefined_bit ? stored - 1 : stored;
    value.name = s.str();
    value.fog_enabled = s.main.bits(1);
    value.fog_background_enabled = s.main.bits(1);
    value.backfaces_enabled = s.main.bits(1);
    value.environment_image_enabled = s.main.bits(1);
    value.environment_image_filename = s.str();
    value.description = s.str();
    value.display_index = s.main.bit_long();
    value.has_predefined = read_predefined_bit && s.main.bits(1);
    return value;
}
ModelDocViewStyle read_model_doc_view_style(DynamicStream &s, unsigned version) {
    ModelDocViewStyle value;
    value.class_version = s.main.bit_short();
    value.description = s.str();
    value.modified_for_recompute = s.main.bits(1);
    if (version >= 1032) {
        value.display_name = s.str();
        value.flags = s.main.bit_long();
    }
    return value;
}
std::string read_cad_bytes(DynamicStream &s, std::int32_t count) {
    require(count >= 0 && count <= 100000, "Invalid data link payload");
    std::string bytes;
    bytes.resize(static_cast<std::size_t>(count));
    for (auto &byte : bytes)
        byte = static_cast<char>(s.main.bits(8));
    return bytes;
}
std::string utf16le_to_utf8(std::string_view bytes) {
    std::u32string scalars;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        auto unit = static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[i]) |
                                               (static_cast<unsigned char>(bytes[i + 1]) << 8));
        if (unit >= 0xd800 && unit <= 0xdbff && i + 3 < bytes.size()) {
            auto next = static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[i + 2]) |
                                                   (static_cast<unsigned char>(bytes[i + 3]) << 8));
            if (next >= 0xdc00 && next <= 0xdfff) {
                scalars.push_back(0x10000 + ((unit - 0xd800) << 10) + (next - 0xdc00));
                i += 2;
                continue;
            }
        }
        scalars.push_back(unit);
    }
    return detail::utf8_string(scalars);
}
std::string read_cad_string(DynamicStream &s) {
    auto stored = s.main.bit_long();
    auto bytes = read_cad_bytes(s, stored);
    if (s.v >= 1021) {
        auto n = bytes.size() >= 2 ? bytes.size() - 2 : 0;
        return utf16le_to_utf8(std::string_view(bytes).substr(0, n));
    }
    auto end = bytes.size();
    if (end && bytes[end - 1] == 0)
        --end;
    return detail::from_legacy(std::string_view(bytes).substr(0, end), s.page);
}
std::vector<std::uint8_t> read_object_data_payload(DynamicStream &s) {
    (void)s.main.bit_short();
    (void)s.main.bit_long();
    (void)s.main.bit_long();
    auto stored = s.main.bit_long();
    require(stored >= 2 && stored - 2 <= 10000000, "Invalid object data payload");
    auto length = stored - 2;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    for (auto &byte : bytes)
        byte = static_cast<std::uint8_t>(s.main.bits(8));
    return bytes;
}
TableValue read_cad_value(DynamicStream &s) {
    TableValue value;
    if (s.v >= 1021)
        value.flags = s.main.bit_long();
    value.type = s.main.bit_long();
    if (s.v < 1021 || (value.flags & 1) == 0) {
        switch (value.type) {
        case 0:
        case 1:
            value.number = s.main.bit_long();
            break;
        case 2:
            value.number = s.main.bit_double();
            break;
        case 4:
        case 0x200:
            value.text = read_cad_string(s);
            break;
        case 8: {
            value.data_size = s.main.bit_long();
            auto bytes = read_cad_bytes(s, value.data_size);
            value.binary.assign(bytes.begin(), bytes.end());
            break;
        }
        case 0x10:
            value.data_size = s.main.bit_long();
            if (value.data_size > 0) {
                value.point.x = s.main.raw_double();
                value.point.y = s.main.raw_double();
            }
            break;
        case 0x20:
            value.data_size = s.main.bit_long();
            if (value.data_size > 0) {
                value.point.x = s.main.raw_double();
                value.point.y = s.main.raw_double();
                value.point.z = s.main.raw_double();
            }
            break;
        case 0x40:
            value.handle = s.href();
            break;
        default:
            break;
        }
    }
    if (s.v >= 1021) {
        value.unit = s.main.bit_long();
        value.format = s.str();
        if (value.unit != 12)
            value.formatted = s.str();
    }
    return value;
}
void require_view_rep_count(std::int32_t count) {
    require(count >= 0 && count <= 100000, "Invalid view representation count");
}
void require_view_rep_path(std::int32_t count) {
    require(count >= 0 && count <= 10000, "Invalid view representation path count");
}
Vector3 read_view_rep_vec(DynamicStream &s) {
    return {s.main.bit_double(), s.main.bit_double(), s.main.bit_double()};
}
ViewRepGuid read_view_rep_guid(DynamicStream &s) {
    ViewRepGuid guid;
    guid.data1 = s.main.bit_long();
    guid.data2 = s.main.bit_short();
    guid.data3 = s.main.bit_short();
    for (auto &item : guid.data4)
        item = static_cast<std::uint8_t>(s.main.bits(8));
    return guid;
}
ViewRepSketchGeometry read_view_rep_geometry(DynamicStream &s, std::int32_t type_code) {
    ViewRepSketchGeometry geometry;
    geometry.type_code = type_code;
    if (type_code == 19 || type_code == 23) {
        geometry.kind = ViewRepSketchGeometry::Kind::Line;
        geometry.first = read_view_rep_vec(s);
        geometry.second = read_view_rep_vec(s);
        return geometry;
    }
    if (type_code == 11) {
        geometry.kind = ViewRepSketchGeometry::Kind::Circle;
        geometry.center = read_view_rep_vec(s);
        geometry.normal = read_view_rep_vec(s);
        geometry.direction = read_view_rep_vec(s);
        geometry.radius = s.main.bit_double();
        geometry.start_parameter = s.main.bit_double();
        geometry.end_parameter = s.main.bit_double();
        geometry.reserved = s.main.bit_double();
        return geometry;
    }
    if (type_code != 42)
        return geometry;
    geometry.kind = ViewRepSketchGeometry::Kind::Nurb;
    geometry.flags = {s.main.bits(1) != 0, s.main.bits(1) != 0};
    geometry.degree = s.main.bit_short();
    geometry.tolerance = s.main.bit_double();
    geometry.knot_header = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long()};
    require_view_rep_count(geometry.knot_header[0]);
    geometry.knots.reserve(static_cast<std::size_t>(geometry.knot_header[0]));
    for (std::int32_t i = 0; i < geometry.knot_header[0]; ++i)
        geometry.knots.push_back(s.main.bit_double());
    geometry.weight_header = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long()};
    require_view_rep_count(geometry.weight_header[0]);
    geometry.weights.reserve(static_cast<std::size_t>(geometry.weight_header[0]));
    for (std::int32_t i = 0; i < geometry.weight_header[0]; ++i)
        geometry.weights.push_back(s.main.bit_double());
    geometry.point_header = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long()};
    require_view_rep_count(geometry.point_header[0]);
    geometry.control_points.reserve(static_cast<std::size_t>(geometry.point_header[0]));
    for (std::int32_t i = 0; i < geometry.point_header[0]; ++i)
        geometry.control_points.push_back(read_view_rep_vec(s));
    return geometry;
}
ViewRep read_view_rep(DynamicStream &s) {
    ViewRep value;
    value.header_values = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long(), s.main.bit_long(),
                           s.main.bit_long()};
    value.name = s.str();
    value.scale = s.main.bit_long();
    value.header_status = s.main.bit_long();
    value.description = s.str();
    value.source_id = s.main.bit_long_long();
    value.source_enabled = s.main.bits(1);
    value.source_version = s.main.bit_long();
    value.model_id = s.main.bit_long_long();
    value.guid = read_view_rep_guid(s);
    value.marker = static_cast<std::uint8_t>(s.main.bits(8));
    for (auto &item : value.transform)
        item = s.main.bit_double();
    value.transform_version = s.main.bit_long();
    value.database_id = s.main.bit_long_long();
    value.geometry_version = s.main.bit_long();
    value.geometry_marker = s.main.bit_long();
    auto sketch_count = s.main.bit_long();
    require_view_rep_count(sketch_count);
    value.sketches.reserve(static_cast<std::size_t>(sketch_count));
    for (std::int32_t i = 0; i < sketch_count; ++i) {
        ViewRepSketch sketch;
        sketch.id = s.main.bit_long();
        sketch.version = s.main.bit_long();
        auto reference_count = s.main.bit_long();
        require_view_rep_count(reference_count);
        sketch.references.reserve(static_cast<std::size_t>(reference_count));
        for (std::int32_t j = 0; j < reference_count; ++j)
            sketch.references.push_back({s.href(), s.main.bits(1) != 0});
        sketch.reserved = s.main.bit_long();
        sketch.enabled = s.main.bits(1);
        auto type_code = s.main.bit_long();
        sketch.geometry = read_view_rep_geometry(s, type_code);
        sketch.final_flag = s.main.bits(1);
        value.sketches.push_back(std::move(sketch));
    }
    value.related_objects = {s.href(), s.href()};
    value.source_manager = s.href();
    value.owned_objects = {s.href(), s.href()};
    value.optional_objects = {s.href(), s.href()};
    value.position = s.raw2();
    value.rotation = s.main.bit_double();
    value.orientation = s.href();
    value.is_active = s.main.bits(1);
    value.projection = s.main.bit_short();
    value.linked_views = {s.href(), s.href()};
    auto path_count = s.main.bit_long();
    require_view_rep_count(path_count);
    value.section_sketches.reserve(static_cast<std::size_t>(path_count));
    for (std::int32_t i = 0; i < path_count; ++i) {
        ViewRepObjectPath path;
        path.class_name = s.str();
        auto extra = s.main.bit_short();
        require_view_rep_path(extra);
        path.objects.reserve(static_cast<std::size_t>(extra) + 1);
        for (std::int32_t j = 0; j <= extra; ++j)
            path.objects.push_back(s.href());
        value.section_sketches.push_back(std::move(path));
    }
    value.action_mode = s.main.bit_long();
    if (value.action_mode != 0)
        value.action = s.href();
    value.has_parent = s.main.bits(1);
    value.parent = s.href();
    value.tail_version = s.main.bit_long();
    value.tail_state = s.main.bit_long();
    value.tail_id = s.main.bit_long_long();
    value.path_count = s.main.bit_long();
    value.path_version = s.main.bit_long();
    value.path_id = s.main.bit_long_long();
    value.has_block_path = s.main.bits(1);
    if (value.has_block_path) {
        ViewRepBlockPath path;
        path.class_name = s.str();
        path.version = s.main.bit_long();
        auto entry_count = s.main.bit_long();
        require_view_rep_count(entry_count);
        path.entries.reserve(static_cast<std::size_t>(entry_count));
        for (std::int32_t i = 0; i < entry_count; ++i)
            path.entries.push_back({static_cast<std::uint8_t>(s.main.bits(8)),
                                    static_cast<std::uint8_t>(s.main.bits(8)), s.href()});
        value.block_path = std::move(path);
    }
    value.style = s.href();
    return value;
}
ClassObjectData read_class_object_data(DynamicStream &s, std::string_view dxf_name, unsigned version) {
    auto name = class_object_ascii_upper(dxf_name);
    if (name == "RENDERSETTINGS")
        return read_render_settings(s, false, version >= 1027);
    if (name == "MENTALRAYRENDERSETTINGS") {
        MentalRayRenderSettings value;
        value.base = read_render_settings(s, false, false);
        value.version = s.main.bit_long();
        value.sampling_min = s.main.bit_long();
        value.sampling_max = s.main.bit_long();
        value.sampling_filter = s.main.bit_short();
        value.sampling_filter_width = s.main.bit_double();
        value.sampling_filter_height = s.main.bit_double();
        value.sampling_contrast = {s.main.bit_double(), s.main.bit_double(), s.main.bit_double(),
                                   s.main.bit_double()};
        value.shadow_mode = s.main.bit_short();
        value.shadow_maps_enabled = s.main.bits(1);
        value.ray_tracing_enabled = s.main.bits(1);
        value.ray_trace_depth = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long()};
        value.global_illumination_enabled = s.main.bits(1);
        value.global_illumination_sample_count = s.main.bit_long();
        value.global_illumination_sample_radius_enabled = s.main.bits(1);
        value.global_illumination_sample_radius = s.main.bit_double();
        value.photons_per_light = s.main.bit_long();
        value.photon_trace_depth = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long()};
        value.final_gathering_enabled = s.main.bits(1);
        value.final_gathering_ray_count = s.main.bit_long();
        value.final_gathering_sample_radius_state = {bool(s.main.bits(1)), bool(s.main.bits(1)),
                                                    bool(s.main.bits(1))};
        value.final_gathering_sample_radius = {s.main.bit_double(), s.main.bit_double()};
        value.light_luminance_scale = s.main.bit_double();
        value.diagnostics_mode = s.main.bit_short();
        value.diagnostics_grid_mode = s.main.bit_short();
        value.diagnostics_grid_size = s.main.bit_double();
        value.diagnostics_photon_mode = s.main.bit_short();
        value.diagnostics_bsp_mode = s.main.bit_short();
        value.export_mi_enabled = s.main.bits(1);
        value.description = s.str();
        value.tile_size = s.main.bit_long();
        value.tile_order = s.main.bit_short();
        value.memory_limit = s.main.bit_long();
        value.diagnostics_samples_mode = s.main.bits(1);
        value.energy_multiplier = s.main.bit_double();
        return value;
    }
    if (name == "RAPIDRTRENDERSETTINGS") {
        RapidRtRenderSettings value;
        value.base = read_render_settings(s, true, false);
        value.version = s.main.bit_long();
        value.render_target = s.main.bit_long();
        value.render_level = s.main.bit_long();
        value.render_time = s.main.bit_long();
        value.lighting_model = s.main.bit_long();
        value.filter_type = s.main.bit_long();
        value.filter_width = s.main.bit_double();
        value.filter_height = s.main.bit_double();
        value.base.has_predefined = s.main.bits(1);
        return value;
    }
    if (name == "SECTION_MANAGER") {
        SectionManager value;
        value.is_live = s.main.bits(1);
        auto count = s.main.bit_short();
        require(count >= 0, "Invalid section manager count");
        value.sections.reserve(static_cast<std::size_t>(count));
        for (std::int16_t i = 0; i < count; ++i)
            value.sections.push_back(s.href());
        return value;
    }
    if (name == "GRADIENT_BACKGROUND") {
        GradientBackground value;
        value.class_version = s.main.bit_long();
        value.color_top = static_cast<std::uint32_t>(s.main.bit_long());
        value.color_middle = static_cast<std::uint32_t>(s.main.bit_long());
        value.color_bottom = static_cast<std::uint32_t>(s.main.bit_long());
        value.horizon = s.main.bit_double();
        value.height = s.main.bit_double();
        value.rotation = s.main.bit_double();
        return value;
    }
    if (name == "GROUND_PLANE_BACKGROUND") {
        GroundPlaneBackground value;
        value.class_version = s.main.bit_long();
        value.color_sky_zenith = static_cast<std::uint32_t>(s.main.bit_long());
        value.color_sky_horizon = static_cast<std::uint32_t>(s.main.bit_long());
        value.color_underground_horizon = static_cast<std::uint32_t>(s.main.bit_long());
        value.color_underground_azimuth = static_cast<std::uint32_t>(s.main.bit_long());
        value.color_near = static_cast<std::uint32_t>(s.main.bit_long());
        value.color_far = static_cast<std::uint32_t>(s.main.bit_long());
        return value;
    }
    if (name == "RAPIDRTRENDERENVIRONMENT" || name == "IBL_BACKGROUND") {
        IblBackground value;
        value.class_version = s.main.bit_long();
        value.enabled = s.main.bits(1);
        value.name = s.str();
        value.rotation = s.main.bit_double();
        value.display_image = s.main.bits(1);
        value.secondary_background = s.href();
        return value;
    }
    if (name == "IMAGE_BACKGROUND") {
        ImageBackground value;
        value.class_version = s.main.bit_long();
        value.filename = s.str();
        value.fit_to_screen = s.main.bits(1);
        value.maintain_aspect_ratio = s.main.bits(1);
        value.use_tiling = s.main.bits(1);
        value.offset = {s.main.bit_double(), s.main.bit_double()};
        value.scale = {s.main.bit_double(), s.main.bit_double()};
        return value;
    }
    if (name == "SKYLIGHT_BACKGROUND") {
        SkyLightBackground value;
        value.class_version = s.main.bit_long();
        value.sun = s.href();
        return value;
    }
    if (name == "SOLID_BACKGROUND") {
        SolidBackground value;
        value.class_version = s.main.bit_long();
        value.color = static_cast<std::uint32_t>(s.main.bit_long());
        return value;
    }
    if (name == "RENDERENTRY") {
        RenderEntry value;
        value.class_version = s.main.bit_long();
        value.image_filename = s.str();
        value.preset_name = s.str();
        value.view_name = s.str();
        value.width = s.main.bit_long();
        value.height = s.main.bit_long();
        value.start_year = s.main.bit_short();
        value.start_month = s.main.bit_short();
        value.start_day = s.main.bit_short();
        value.start_hour = s.main.bit_short();
        value.start_minute = s.main.bit_short();
        value.start_second = s.main.bit_short();
        value.start_millisecond = s.main.bit_short();
        value.end_year = s.main.bit_short();
        value.end_month = s.main.bit_short();
        value.end_day = s.main.bit_short();
        value.end_hour = s.main.bit_short();
        value.end_minute = s.main.bit_short();
        value.end_second = s.main.bit_short();
        value.end_millisecond = s.main.bit_short();
        value.render_time = s.main.bit_double();
        value.memory_amount = s.main.bit_long();
        value.material_count = s.main.bit_long();
        value.light_count = s.main.bit_long();
        value.triangle_count = s.main.bit_long();
        value.display_index = s.main.bit_long();
        return value;
    }
    if (name == "RENDERENVIRONMENT") {
        RenderEnvironment value;
        value.class_version = s.main.bit_long();
        value.fog_enabled = s.main.bits(1);
        value.fog_background_enabled = s.main.bits(1);
        value.fog_color = {static_cast<std::uint8_t>(s.main.bits(8)),
                           static_cast<std::uint8_t>(s.main.bits(8)),
                           static_cast<std::uint8_t>(s.main.bits(8))};
        value.fog_density_near = s.main.bit_double();
        value.fog_density_far = s.main.bit_double();
        value.fog_distance_near = s.main.bit_double();
        value.fog_distance_far = s.main.bit_double();
        value.environment_image_enabled = s.main.bits(1);
        value.environment_image_filename = s.str();
        return value;
    }
    if (name == "RENDERGLOBAL") {
        RenderGlobal value;
        value.class_version = s.main.bit_long();
        value.procedure = s.main.bit_long();
        value.destination = s.main.bit_long();
        value.save_enabled = s.main.bits(1);
        value.save_filename = s.str();
        value.image_width = s.main.bit_long();
        value.image_height = s.main.bit_long();
        value.predefined_presets_first = s.main.bits(1);
        value.high_level_info = s.main.bits(1);
        return value;
    }
    if (name == "LIGHTLIST") {
        LightList value;
        value.class_version = s.main.bit_long();
        auto count = s.main.bit_long();
        require(count >= 0 && count <= 100000, "Invalid light list count");
        value.lights.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
            value.lights.push_back({s.href(), s.str()});
        return value;
    }
    if (name == "SUN") {
        Sun value;
        value.class_version = s.main.bit_long();
        value.is_on = s.main.bits(1);
        value.color = read_cm_color(s);
        value.intensity = s.main.bit_double();
        value.has_shadow = s.main.bits(1);
        value.julian_day = s.main.bit_long();
        value.milliseconds = s.main.bit_long();
        value.is_daylight_savings_on = s.main.bits(1);
        value.shadow_type = s.main.bit_long();
        value.shadow_map_size = s.main.bit_short();
        value.shadow_softness = static_cast<std::uint8_t>(s.main.bits(8));
        return value;
    }
    if (name == "ACDBMOTIONPATH" || name == "MOTIONPATH") {
        MotionPath value;
        value.class_version = s.main.bit_long();
        value.camera_path = s.href();
        value.target_path = s.href();
        value.view = s.href();
        value.frames = s.main.bit_short();
        value.frame_rate = s.main.bit_short();
        value.corner_deceleration = s.main.bits(1);
        return value;
    }
    if (name == "ACDBCURVEPATH" || name == "CURVEPATH") {
        CurvePath value;
        value.class_version = s.main.bit_long();
        value.entity = s.href();
        return value;
    }
    if (name == "ACDBPOINTPATH" || name == "POINTPATH") {
        PointPath value;
        value.class_version = s.main.bit_short();
        value.point = {s.main.bit_double(), s.main.bit_double(), s.main.bit_double()};
        return value;
    }
    if (name == "TVDEVICEPROPERTIES") {
        TvDeviceProperties value;
        value.flags = static_cast<std::uint32_t>(s.main.bit_long());
        value.max_regen_threads = s.main.bit_short();
        value.use_lut_palette = s.main.bit_long();
        value.alternate_highlight = s.main.bit_long_long();
        value.alternate_highlight_color = s.main.bit_long_long();
        value.geometry_shader_usage = s.main.bit_long_long();
        value.blending_mode = s.main.bit_long();
        value.antialiasing_level = s.main.bit_double();
        value.reserved_double = s.main.bit_double();
        return value;
    }
    if (name == "LAYERFILTER") {
        LayerFilter value;
        auto count = s.main.bit_long();
        require(count >= 0 && count <= 100000, "Invalid layer filter count");
        value.names.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
            value.names.push_back(s.str());
        return value;
    }
    if (name == "PARTIAL_VIEWING_INDEX") {
        PartialViewingIndex value;
        auto count = s.main.bit_long();
        require(count >= 0 && count <= 100000, "Invalid partial viewing index count");
        value.has_entries = count != 0 && s.main.bits(1);
        value.entries.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            PartialViewingIndexEntry entry;
            entry.extents_min = {s.main.bit_double(), s.main.bit_double(), s.main.bit_double()};
            entry.extents_max = {s.main.bit_double(), s.main.bit_double(), s.main.bit_double()};
            entry.object = s.href();
            value.entries.push_back(entry);
        }
        return value;
    }
    if (name == "SUNSTUDY") {
        SunStudy value;
        value.class_version = s.main.bit_long();
        value.setup_name = s.str();
        value.description = s.str();
        value.output_type = s.main.bit_long();
        if (value.output_type == 0) {
            value.use_subset = s.main.bits(1);
            value.sheet_set_name = s.str();
            value.sheet_subset_name = s.str();
        }
        value.select_dates_from_calendar = s.main.bits(1);
        auto date_count = s.main.bit_long();
        require(date_count >= 0 && date_count <= 10000, "Invalid sun study date count");
        value.dates.reserve(static_cast<std::size_t>(date_count));
        for (std::int32_t i = 0; i < date_count; ++i)
            value.dates.push_back({s.main.bit_long(), s.main.bit_long()});
        value.select_range_of_dates = s.main.bits(1);
        if (value.select_range_of_dates) {
            value.start_time = s.main.bit_long();
            value.end_time = s.main.bit_long();
            value.interval = s.main.bit_long();
        }
        auto hour_count = s.main.bit_long();
        require(hour_count >= 0 && hour_count <= 10000, "Invalid sun study hour count");
        value.hours.reserve(static_cast<std::size_t>(hour_count));
        for (std::int32_t i = 0; i < hour_count; ++i)
            value.hours.push_back(s.main.bits(1));
        value.shade_plot_type = s.main.bit_long();
        value.viewport_count = s.main.bit_long();
        value.rows = s.main.bit_long();
        value.columns = s.main.bit_long();
        value.spacing = s.main.bit_double();
        value.lock_viewports = s.main.bits(1);
        value.label_viewports = s.main.bits(1);
        value.page_setup_wizard = s.href();
        value.view = s.href();
        value.visual_style = s.href();
        value.text_style = s.href();
        return value;
    }
    if (name == "DATATABLE" || name == "ACDBDATATABLE") {
        DataTable value;
        value.flags = s.main.bit_short();
        auto column_count = s.main.bit_long();
        value.row_count = s.main.bit_long();
        require(column_count >= 0 && column_count <= 100000 && value.row_count >= 0 &&
                    value.row_count <= 100000,
                "Invalid data table count");
        value.name = s.str();
        value.columns.reserve(static_cast<std::size_t>(column_count));
        for (std::int32_t i = 0; i < column_count; ++i) {
            DataTableColumn column;
            column.value_type = s.main.bit_long();
            require(data_table_has_native_codec(column.value_type), "Invalid data table column");
            column.name = s.str();
            column.rows.reserve(static_cast<std::size_t>(value.row_count));
            for (std::int32_t j = 0; j < value.row_count; ++j) {
                DataTableValue cell;
                switch (static_cast<DataTableCellType>(column.value_type)) {
                case DataTableCellType::Integer:
                    cell.integer = s.main.bit_long();
                    break;
                case DataTableCellType::Double:
                    cell.real = s.main.bit_double();
                    break;
                case DataTableCellType::Text:
                    cell.text = s.str();
                    break;
                case DataTableCellType::Point:
                    cell.point = {s.main.bit_double(), s.main.bit_double(), s.main.bit_double()};
                    break;
                case DataTableCellType::ObjectId:
                    cell.handle = s.href();
                    break;
                }
                column.rows.push_back(std::move(cell));
            }
            value.columns.push_back(std::move(column));
        }
        return value;
    }
    if (name == "GEOMAPIMAGE") {
        GeoMapImage value;
        value.class_version = s.main.bit_long();
        value.origin = {s.main.raw_double(), s.main.raw_double(), s.main.raw_double()};
        value.image_size = {s.main.raw_double(), s.main.raw_double()};
        value.display_properties = s.main.bit_short();
        value.clipping_enabled = s.main.bits(1);
        value.brightness = static_cast<std::uint8_t>(s.main.bits(8));
        value.contrast = static_cast<std::uint8_t>(s.main.bits(8));
        value.fade = static_cast<std::uint8_t>(s.main.bits(8));
        return value;
    }
    if (name == "ACDBDETAILVIEWSTYLE" || name == "DETAILVIEWSTYLE") {
        DetailViewStyle value;
        value.base = read_model_doc_view_style(s, version);
        value.class_version = s.main.bit_short();
        value.flags = s.main.bit_long();
        value.identifier_style = s.href();
        value.identifier_color = read_cm_color(s);
        value.identifier_height = s.main.bit_double();
        value.identifier_excluded_characters = s.str();
        value.identifier_offset = s.main.bit_double();
        value.identifier_placement = static_cast<std::uint8_t>(s.main.bits(8));
        value.arrow_symbol = s.href();
        value.arrow_symbol_color = read_cm_color(s);
        value.arrow_symbol_size = s.main.bit_double();
        value.boundary_linetype = s.href();
        value.boundary_lineweight = s.main.bit_long();
        value.boundary_color = read_cm_color(s);
        value.view_label_text_style = s.href();
        value.view_label_text_color = read_cm_color(s);
        value.view_label_text_height = s.main.bit_double();
        value.view_label_attachment = s.main.bit_long();
        value.view_label_offset = s.main.bit_double();
        value.view_label_alignment = s.main.bit_long();
        value.view_label_pattern = s.str();
        value.connection_linetype = s.href();
        value.connection_lineweight = s.main.bit_long();
        value.connection_color = read_cm_color(s);
        value.border_linetype = s.href();
        value.border_lineweight = s.main.bit_long();
        value.border_color = read_cm_color(s);
        value.model_edge = static_cast<std::uint8_t>(s.main.bits(8));
        return value;
    }
    if (name == "ACDBSECTIONVIEWSTYLE" || name == "SECTIONVIEWSTYLE") {
        SectionViewStyle value;
        value.base = read_model_doc_view_style(s, version);
        value.class_version = s.main.bit_short();
        value.flags = s.main.bit_long();
        value.identifier_style = s.href();
        value.identifier_color = read_cm_color(s);
        value.identifier_height = s.main.bit_double();
        value.arrow_start_symbol = s.href();
        value.arrow_end_symbol = s.href();
        value.arrow_symbol_color = read_cm_color(s);
        value.arrow_symbol_size = s.main.bit_double();
        value.identifier_excluded_characters = s.str();
        value.arrow_symbol_extension_length = s.main.bit_double();
        value.plane_linetype = s.href();
        value.plane_lineweight = s.main.bit_long();
        value.plane_color = read_cm_color(s);
        value.bend_linetype = s.href();
        value.bend_lineweight = s.main.bit_long();
        value.bend_color = read_cm_color(s);
        value.bend_line_length = s.main.bit_double();
        value.end_line_length = s.main.bit_double();
        value.view_label_text_style = s.href();
        value.view_label_text_color = read_cm_color(s);
        value.view_label_text_height = s.main.bit_double();
        value.view_label_attachment = s.main.bit_long();
        value.view_label_offset = s.main.bit_double();
        value.view_label_alignment = s.main.bit_long();
        value.view_label_pattern = s.str();
        value.hatch_color = read_cm_color(s);
        value.hatch_background_color = read_cm_color(s);
        value.hatch_pattern = s.str();
        value.hatch_scale = s.main.bit_double();
        value.hatch_transparency = s.main.bit_long();
        value.reserved_flags[0] = s.main.bits(1);
        value.reserved_flags[1] = s.main.bits(1);
        value.identifier_position = s.main.bit_long();
        value.identifier_offset = s.main.bit_double();
        value.arrow_position = s.main.bit_long();
        value.end_line_overshoot = s.main.bit_double();
        auto angle_count = s.main.bit_long();
        require(angle_count >= 0 && angle_count <= 100000, "Invalid hatch angle count");
        value.hatch_angles.reserve(static_cast<std::size_t>(angle_count));
        for (std::int32_t i = 0; i < angle_count; ++i)
            value.hatch_angles.push_back(s.main.bit_double());
        return value;
    }
    if (name == "IRD_DSC_DICT") {
        ObjectDataDictionary value;
        auto count = s.main.bit_long();
        require(count >= 0 && count <= 100000, "Invalid object data count");
        value.duplicate_cloning = s.main.bit_short();
        value.hard_owner = s.main.bits(8) != 0;
        value.entries.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
            value.entries.push_back({s.str(), s.href()});
        (void)s.main.bit_double();
        return value;
    }
    if (name == "IRD_DSC_RECORD") {
        ObjectDataDescriptor value;
        value.payload = read_object_data_payload(s);
        value.kind = s.main.bit_long();
        value.name = s.str();
        (void)s.main.bit_long();
        return value;
    }
    if (name == "IRD_OBJ_RECORD") {
        ObjectDataRecord value;
        value.payload = read_object_data_payload(s);
        value.table = s.href();
        return value;
    }
    if (name == "ACDBVIEWREPSOURCEMGR") {
        ViewRepSourceManager value;
        value.has_source = s.main.bits(1);
        value.source = s.href();
        value.status = s.main.bit_long();
        return value;
    }
    if (name == "ACDBVIEWREPSTANDARD") {
        ViewRepStandard value;
        value.values = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long(),
                        s.main.bit_long(), s.main.bit_long(), s.main.bit_long()};
        return value;
    }
    if (name == "ACDBVIEWREPORIENTATIONDEF")
        return ViewRepOrientationDefinition{};
    if (name == "ACDBVIEWREPORIENTATION") {
        ViewRepOrientation value;
        value.camera = read_view_rep_vec(s);
        value.target = read_view_rep_vec(s);
        value.normal = read_view_rep_vec(s);
        return value;
    }
    if (name == "ACDBVIEWREPSECTIONDEFINITION") {
        ViewRepSectionDefinition value;
        value.version = s.main.bit_long();
        value.section_depth = s.main.bit_double();
        value.flags = {s.main.bit_long(), s.main.bit_long()};
        return value;
    }
    if (name == "ACDBSYMODELSPACEVIEWSELSET") {
        ViewRepModelSpaceViewSelectionSet value;
        value.version = s.main.bit_long();
        auto count = s.main.bit_long();
        require_view_rep_count(count);
        value.entities.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
            value.entities.push_back(s.href());
        return value;
    }
    if (name == "ACDBVIEWREPMODELSPACESOURCE") {
        ViewRepModelSpaceSource value;
        value.enabled = s.main.bits(1);
        value.header_values = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long(),
                               s.main.bit_long(), s.main.bit_long(), s.main.bit_long()};
        for (auto &item : value.transform)
            item = s.main.bit_double();
        value.source_version = s.main.bit_long();
        value.source_status = s.main.bit_long();
        value.model = s.main.handle(s.self).value;
        value.guid = read_view_rep_guid(s);
        value.references = {s.href(), s.href()};
        value.tail_values = {s.main.bit_long(), s.main.bit_long()};
        value.orientation = s.href();
        return value;
    }
    if (name == "ACDBVIEWREP")
        return read_view_rep(s);
    if (name == "DATALINK") {
        DataLink value;
        value.data_adapter = s.str();
        value.description = s.str();
        value.tooltip = s.str();
        value.connection_string = s.str();
        auto option = s.main.bit_long();
        value.update_option = s.main.bit_long();
        value.flags = s.main.bit_long();
        value.year = s.main.bit_short();
        value.month = s.main.bit_short();
        value.day = s.main.bit_short();
        value.hour = s.main.bit_short();
        value.minute = s.main.bit_short();
        value.second = s.main.bit_short();
        value.millisecond = s.main.bit_short();
        value.path_option = s.main.bit_short();
        value.status_flags = s.main.bit_long();
        value.update_status = s.str();
        auto custom_count = s.main.bit_long();
        require(custom_count >= 0 && custom_count <= 100000, "Invalid data link count");
        value.custom_data.reserve(static_cast<std::size_t>(custom_count));
        for (std::int32_t i = 0; i < custom_count; ++i)
            value.custom_data.push_back({s.href(), {}});
        if (option & DataLink::has_data_map) {
            auto map_count = s.main.bit_long();
            require(map_count >= 0 && map_count <= 100000, "Invalid data link count");
            value.data_map.reserve(static_cast<std::size_t>(map_count));
            for (std::int32_t i = 0; i < map_count; ++i) {
                TableCustomData item;
                item.name = s.str();
                item.value = read_cad_value(s);
                value.data_map.push_back(std::move(item));
            }
        }
        value.option = option & ~DataLink::has_data_map;
        value.hard_owner = s.href();
        return value;
    }
    if (name != "SECTION_SETTINGS")
        throw std::invalid_argument("Unsupported class object");
    SectionSettings value;
    value.current_type = s.main.bit_long();
    auto type_count = s.main.bit_long();
    require(type_count >= 0 && type_count <= 4, "Invalid section settings count");
    value.types.reserve(static_cast<std::size_t>(type_count));
    for (std::int32_t i = 0; i < type_count; ++i) {
        SectionTypeSettings type;
        type.section_type = s.main.bit_long();
        type.generation = s.main.bit_long();
        auto source_count = s.main.bit_long();
        require(source_count >= 0 && source_count <= 100000, "Invalid section settings source count");
        type.sources.reserve(static_cast<std::size_t>(source_count));
        for (std::int32_t j = 0; j < source_count; ++j)
            type.sources.push_back(s.href());
        type.destination_block = s.href();
        type.destination_file = s.str();
        auto geometry_count = s.main.bit_long();
        require(geometry_count >= 0 && geometry_count <= 100000, "Invalid section geometry count");
        type.geometry.reserve(static_cast<std::size_t>(geometry_count));
        for (std::int32_t j = 0; j < geometry_count; ++j) {
            SectionGeometrySettings geometry;
            geometry.geometry_count = s.main.bit_long();
            geometry.index = s.main.bit_long();
            geometry.flags = s.main.bit_long();
            geometry.color = read_cm_color(s);
            geometry.layer = s.str();
            geometry.linetype = s.str();
            geometry.linetype_scale = s.main.bit_double();
            geometry.plot_style = s.str();
            geometry.lineweight = s.main.bit_long();
            geometry.face_transparency = s.main.bit_short();
            geometry.edge_transparency = s.main.bit_short();
            geometry.hatch_type = s.main.bit_short();
            geometry.hatch_pattern = s.str();
            geometry.hatch_angle = s.main.bit_double();
            geometry.hatch_spacing = s.main.bit_double();
            geometry.hatch_scale = s.main.bit_double();
            type.geometry.push_back(std::move(geometry));
        }
        value.types.push_back(std::move(type));
    }
    return value;
}
ClassObject read_class_object_record(std::string_view body, DwgR2000Record &record, std::uint64_t h, unsigned v,
                                     const char *page, const std::string &dxf_name) {
    require(v >= 1018, "Native DWG class objects require R2004+");
    auto s = open_dynamic(body, record, h, v, page);
    ClassObject out;
    out.handle = h;
    out.dxf_name = dxf_name;
    out.cpp_class_name = class_object_cpp_name(dxf_name);
    DwgDynamicRecord common;
    read_dynamic_common(s, common, false, record);
    out.owner = common.owner;
    out.reactors = common.reactors;
    if (common.extension_dictionary)
        out.xdictionary_handle = common.extension_dictionary;
    out.data = read_class_object_data(s, dxf_name, v);
    out.source_version = static_cast<DxfVersion>(v);
    return out;
}
