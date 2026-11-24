// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include <libopencad/dxf_document.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static bool near(double a, double b) { return std::abs(a - b) < 1e-12; }
static std::string dxf_bytes(const DxfDocument &d, bool binary) {
    std::ostringstream o;
    d.write(o, binary);
    return o.str();
}
static std::string dwg_bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static DxfDocument parse_dxf(const std::string &s) {
    std::istringstream i(s);
    return DxfDocument::read(i);
}
static DwgDocument parse_dwg(const std::string &s) {
    std::istringstream i(s);
    return DwgDocument::read(i);
}
static void write_file(const std::string &path, const std::string &bytes) {
    std::ofstream(path, std::ios::binary) << bytes;
}
static std::string trim_line(std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ')
        ++i;
    return line.substr(i);
}
static std::string first_group_after(const std::string &text, std::string_view marker, int code) {
    auto at = text.find(marker);
    check(at != std::string::npos, "Missing subclass marker");
    auto start = text.find('\n', at);
    check(start != std::string::npos, "Missing subclass marker");
    std::size_t i = start + 1;
    while (i < text.size()) {
        auto end = text.find('\n', i);
        auto line = trim_line(text.substr(i, end == std::string::npos ? std::string::npos : end - i));
        i = end == std::string::npos ? text.size() : end + 1;
        auto value_end = text.find('\n', i);
        auto value = trim_line(text.substr(i, value_end == std::string::npos ? std::string::npos : value_end - i));
        i = value_end == std::string::npos ? text.size() : value_end + 1;
        if (line == std::to_string(code))
            return value;
    }
    throw std::runtime_error("Missing group after marker");
}
static RenderSettings sample_render() {
    RenderSettings value;
    value.class_version = 2;
    value.name = "High";
    value.fog_enabled = true;
    value.fog_background_enabled = false;
    value.backfaces_enabled = true;
    value.environment_image_enabled = false;
    value.environment_image_filename = "env.hdr";
    value.description = "render";
    value.display_index = 9;
    value.has_predefined = true;
    return value;
}
static RapidRtRenderSettings sample_rapid() {
    RapidRtRenderSettings value;
    value.base.class_version = 2;
    value.base.name = "RapidRT";
    value.base.backfaces_enabled = true;
    value.base.description = "Render settings";
    value.base.display_index = 3;
    value.base.has_predefined = true;
    value.version = 1;
    value.render_target = 1;
    value.render_level = 5;
    value.render_time = 1;
    value.lighting_model = 1;
    value.filter_type = 1;
    value.filter_width = 2;
    value.filter_height = 2;
    return value;
}
static MentalRayRenderSettings sample_mental() {
    MentalRayRenderSettings value;
    value.base.class_version = 4;
    value.base.name = "RaySettings";
    value.base.fog_enabled = true;
    value.base.backfaces_enabled = true;
    value.base.environment_image_enabled = true;
    value.base.environment_image_filename = "environment.hdr";
    value.base.description = "base";
    value.base.display_index = 7;
    value.version = 11;
    value.sampling_min = 2;
    value.sampling_max = 9;
    value.sampling_filter = 3;
    value.sampling_filter_width = 1.25;
    value.sampling_filter_height = 2.5;
    value.sampling_contrast = {0.1, 0.2, 0.3, 0.4};
    value.shadow_mode = 5;
    value.shadow_maps_enabled = true;
    value.ray_trace_depth = {1, 2, 3};
    value.global_illumination_enabled = true;
    value.global_illumination_sample_count = 17;
    value.global_illumination_sample_radius_enabled = true;
    value.global_illumination_sample_radius = 42;
    value.photons_per_light = 23;
    value.photon_trace_depth = {4, 5, 6};
    value.final_gathering_enabled = true;
    value.final_gathering_ray_count = 31;
    value.final_gathering_sample_radius_state = {true, false, true};
    value.final_gathering_sample_radius = {3.5, 4.5};
    value.light_luminance_scale = 1.75;
    value.diagnostics_mode = 2;
    value.diagnostics_grid_mode = 3;
    value.diagnostics_grid_size = 5.5;
    value.diagnostics_photon_mode = 4;
    value.diagnostics_bsp_mode = 5;
    value.export_mi_enabled = true;
    value.description = "mental-ray";
    value.tile_size = 64;
    value.tile_order = 2;
    value.memory_limit = 1024;
    value.diagnostics_samples_mode = true;
    value.energy_multiplier = 0.875;
    return value;
}
static void check_render(const RenderSettings &value, const RenderSettings &expected, bool expect_predefined) {
    check(value.class_version == expected.class_version, "Render class version");
    check(value.name == expected.name, "Render name");
    check(value.fog_enabled == expected.fog_enabled, "Render fog");
    check(value.backfaces_enabled == expected.backfaces_enabled, "Render backfaces");
    check(value.display_index == expected.display_index, "Render display index");
    check(value.has_predefined == expect_predefined, "Render predefined");
}
static void check_rapid(const RapidRtRenderSettings &value) {
    auto expected = sample_rapid();
    check(value.base.class_version == expected.base.class_version, "RapidRT class version");
    check(value.base.name == expected.base.name, "RapidRT name");
    check(value.render_level == expected.render_level, "RapidRT level");
    check(value.base.has_predefined == expected.base.has_predefined, "RapidRT predefined");
    check(near(value.filter_width, expected.filter_width), "RapidRT filter");
}
static void check_mental(const MentalRayRenderSettings &value) {
    auto expected = sample_mental();
    check(value.base.class_version == expected.base.class_version, "MentalRay class version");
    check(value.base.name == expected.base.name, "MentalRay name");
    check(value.tile_size == expected.tile_size, "MentalRay tile");
    check(near(value.energy_multiplier, expected.energy_multiplier), "MentalRay energy");
    check(near(value.sampling_contrast[3], expected.sampling_contrast[3]), "MentalRay contrast");
    check(value.final_gathering_sample_radius_state[1] == expected.final_gathering_sample_radius_state[1],
          "MentalRay gather flags");
}
static GradientBackground sample_gradient() {
    GradientBackground value;
    value.class_version = 1;
    value.color_top = 0x00ff0000;
    value.color_middle = 0x0000ff00;
    value.color_bottom = 0x000000ff;
    value.horizon = 0.25;
    value.height = 0.5;
    value.rotation = 1.25;
    return value;
}
static GroundPlaneBackground sample_ground() {
    GroundPlaneBackground value;
    value.class_version = 1;
    value.color_sky_zenith = 0x00111111;
    value.color_sky_horizon = 0x00222222;
    value.color_underground_horizon = 0x00333333;
    value.color_underground_azimuth = 0x00444444;
    value.color_near = 0x00555555;
    value.color_far = 0x00666666;
    return value;
}
static IblBackground sample_ibl() {
    IblBackground value;
    value.class_version = 2;
    value.enabled = true;
    value.name = "studio.hdr";
    value.rotation = 0.75;
    value.display_image = true;
    value.secondary_background = 0x3456;
    return value;
}
static ImageBackground sample_image() {
    ImageBackground value;
    value.class_version = 1;
    value.filename = "bg.png";
    value.fit_to_screen = true;
    value.maintain_aspect_ratio = false;
    value.use_tiling = true;
    value.offset = {1.5, 2.5};
    value.scale = {3.5, 4.5};
    return value;
}
static SkyLightBackground sample_sky() {
    SkyLightBackground value;
    value.class_version = 1;
    value.sun = 0x4567;
    return value;
}
static SolidBackground sample_solid() {
    SolidBackground value;
    value.class_version = 1;
    value.color = 0x00112233;
    return value;
}
static RenderEntry sample_entry() {
    RenderEntry value;
    value.class_version = 1;
    value.image_filename = "shot.png";
    value.preset_name = "High";
    value.view_name = "Front";
    value.width = 1920;
    value.height = 1080;
    value.start_year = 2024;
    value.start_month = 3;
    value.start_day = 14;
    value.start_hour = 9;
    value.start_minute = 30;
    value.start_second = 5;
    value.start_millisecond = 12;
    value.end_year = 2024;
    value.end_month = 3;
    value.end_day = 14;
    value.end_hour = 9;
    value.end_minute = 31;
    value.end_second = 6;
    value.end_millisecond = 13;
    value.render_time = 12.5;
    value.memory_amount = 2048;
    value.material_count = 7;
    value.light_count = 3;
    value.triangle_count = 9001;
    value.display_index = 4;
    return value;
}
static RenderEnvironment sample_environment() {
    RenderEnvironment value;
    value.class_version = 1;
    value.fog_enabled = true;
    value.fog_background_enabled = false;
    value.fog_color = {10, 20, 30};
    value.fog_density_near = 0.1;
    value.fog_density_far = 0.9;
    value.fog_distance_near = 2;
    value.fog_distance_far = 40;
    value.environment_image_enabled = true;
    value.environment_image_filename = "fog.hdr";
    return value;
}
static RenderGlobal sample_global() {
    RenderGlobal value;
    value.class_version = 1;
    value.procedure = 2;
    value.destination = 1;
    value.save_enabled = true;
    value.save_filename = "out.png";
    value.image_width = 800;
    value.image_height = 600;
    value.predefined_presets_first = true;
    value.high_level_info = false;
    return value;
}
static LightList sample_lights() {
    LightList value;
    value.class_version = 2;
    value.lights = {{0x2345, "spot"}};
    return value;
}
static Sun sample_sun() {
    Sun value;
    value.class_version = 1;
    value.is_on = true;
    value.color = Color::from_index(1);
    value.intensity = 1.5;
    value.has_shadow = true;
    value.julian_day = 2451545;
    value.milliseconds = 250;
    value.is_daylight_savings_on = true;
    value.shadow_type = 1;
    value.shadow_map_size = 256;
    value.shadow_softness = 3;
    return value;
}
static MotionPath sample_motion() {
    MotionPath value;
    value.class_version = 1;
    value.camera_path = 0x5678;
    value.target_path = 0x6789;
    value.view = 0x789A;
    value.frames = 30;
    value.frame_rate = 24;
    value.corner_deceleration = true;
    return value;
}
static CurvePath sample_curve() {
    CurvePath value;
    value.class_version = 1;
    value.entity = 0x89AB;
    return value;
}
static PointPath sample_point() {
    PointPath value;
    value.class_version = 1;
    value.point = {1.25, 2.5, 3.75};
    return value;
}
static TvDeviceProperties sample_tv() {
    TvDeviceProperties value;
    value.flags = 7;
    value.max_regen_threads = 4;
    value.use_lut_palette = 1;
    value.alternate_highlight = 11;
    value.alternate_highlight_color = 22;
    value.geometry_shader_usage = 33;
    value.blending_mode = 2;
    value.antialiasing_level = 4.5;
    value.reserved_double = 0.25;
    return value;
}
static LayerFilter sample_filter() {
    LayerFilter value;
    value.names = {"Walls", "Doors"};
    return value;
}
static PartialViewingIndex sample_partial() {
    PartialViewingIndex value;
    value.has_entries = true;
    value.entries.push_back({{0, 0, 0}, {10, 10, 1}, 0xBCDE});
    return value;
}
static SectionSettings sample_section_settings() {
    SectionGeometrySettings geometry;
    geometry.geometry_count = 1;
    geometry.index = 0;
    geometry.flags = 3;
    geometry.color = Color::from_index(1);
    geometry.layer = "0";
    geometry.linetype = "Continuous";
    geometry.linetype_scale = 1;
    geometry.plot_style = "ByLayer";
    geometry.lineweight = 25;
    geometry.face_transparency = 10;
    geometry.edge_transparency = 20;
    geometry.hatch_type = 1;
    geometry.hatch_pattern = "ANSI31";
    geometry.hatch_angle = 0.5;
    geometry.hatch_spacing = 2;
    geometry.hatch_scale = 1.25;
    SectionTypeSettings type;
    type.section_type = 1;
    type.generation = 2;
    type.sources = {0x9ABC};
    type.destination_block = 0xABCD;
    type.destination_file = "out.dwg";
    type.geometry.push_back(std::move(geometry));
    SectionSettings value;
    value.current_type = 1;
    value.types.push_back(std::move(type));
    return value;
}
static SunStudy sample_study() {
    SunStudy value;
    value.class_version = 1;
    value.setup_name = "Summer";
    value.description = "Study";
    value.output_type = 0;
    value.use_subset = true;
    value.sheet_set_name = "Set";
    value.sheet_subset_name = "Sub";
    value.select_dates_from_calendar = true;
    value.dates.push_back({2451545, 0});
    value.select_range_of_dates = true;
    value.start_time = 8;
    value.end_time = 18;
    value.interval = 1;
    value.hours = {true, false, true};
    value.shade_plot_type = 1;
    value.viewport_count = 2;
    value.rows = 1;
    value.columns = 2;
    value.spacing = 0.5;
    value.lock_viewports = true;
    value.page_setup_wizard = 0xCDEF;
    value.view = 0xD012;
    value.visual_style = 0xD123;
    value.text_style = 0xD234;
    return value;
}
static DataTable sample_table() {
    DataTableColumn count;
    count.value_type = static_cast<std::int32_t>(DataTableCellType::Integer);
    count.name = "Count";
    count.rows.push_back({7});
    DataTableColumn block;
    block.value_type = static_cast<std::int32_t>(DataTableCellType::ObjectId);
    block.name = "Block";
    DataTableValue cell;
    cell.handle = 0xD345;
    block.rows.push_back(std::move(cell));
    DataTable value;
    value.flags = 1;
    value.name = "Schedule";
    value.row_count = 1;
    value.columns.push_back(std::move(count));
    value.columns.push_back(std::move(block));
    return value;
}
static GeoMapImage sample_geo() {
    GeoMapImage value;
    value.class_version = 1;
    value.origin = {1, 2, 3};
    value.image_size = {64, 32};
    value.display_properties = 4;
    value.clipping_enabled = true;
    value.brightness = 10;
    value.contrast = 20;
    value.fade = 30;
    return value;
}
static DetailViewStyle sample_detail() {
    DetailViewStyle value;
    value.base.class_version = 1;
    value.base.description = "Detail style";
    value.base.modified_for_recompute = true;
    value.base.display_name = "Detail";
    value.base.flags = 2;
    value.class_version = 1;
    value.flags = 3;
    value.identifier_style = 0xE101;
    value.identifier_color = Color::from_index(1);
    value.identifier_height = 2.5;
    value.identifier_excluded_characters = "IOS";
    value.identifier_offset = 0.25;
    value.identifier_placement = 1;
    value.arrow_symbol = 0xE102;
    value.arrow_symbol_color = Color::from_index(2);
    value.arrow_symbol_size = 1.5;
    value.boundary_linetype = 0xE103;
    value.boundary_lineweight = 25;
    value.boundary_color = Color::from_index(3);
    value.view_label_text_style = 0xE104;
    value.view_label_text_color = Color::from_index(4);
    value.view_label_text_height = 3;
    value.view_label_attachment = 1;
    value.view_label_offset = 0.5;
    value.view_label_alignment = 2;
    value.view_label_pattern = "%<VIEW>";
    value.connection_linetype = 0xE105;
    value.connection_lineweight = 18;
    value.connection_color = Color::from_index(5);
    value.border_linetype = 0xE106;
    value.border_lineweight = 25;
    value.border_color = Color::from_index(6);
    value.model_edge = 2;
    return value;
}
static SectionViewStyle sample_section_style() {
    SectionViewStyle value;
    value.base.class_version = 1;
    value.base.description = "Section style";
    value.base.modified_for_recompute = true;
    value.base.display_name = "Section";
    value.base.flags = 2;
    value.class_version = 1;
    value.flags = 3;
    value.identifier_style = 0xE201;
    value.identifier_color = Color::from_index(1);
    value.identifier_height = 2.5;
    value.arrow_start_symbol = 0xE202;
    value.arrow_end_symbol = 0xE203;
    value.arrow_symbol_color = Color::from_index(2);
    value.arrow_symbol_size = 1.5;
    value.identifier_excluded_characters = "IOS";
    value.arrow_symbol_extension_length = 0.75;
    value.identifier_position = 1;
    value.identifier_offset = 0.25;
    value.arrow_position = 2;
    value.plane_linetype = 0xE204;
    value.plane_lineweight = 25;
    value.plane_color = Color::from_index(3);
    value.bend_linetype = 0xE205;
    value.bend_lineweight = 18;
    value.bend_color = Color::from_index(4);
    value.bend_line_length = 1.25;
    value.end_line_overshoot = 0.5;
    value.end_line_length = 2;
    value.view_label_text_style = 0xE206;
    value.view_label_text_color = Color::from_index(5);
    value.view_label_text_height = 3;
    value.view_label_attachment = 1;
    value.view_label_offset = 0.5;
    value.view_label_alignment = 2;
    value.view_label_pattern = "%<SECTION>";
    value.hatch_color = Color::from_index(6);
    value.hatch_background_color = Color::from_index(7);
    value.hatch_pattern = "ANSI31";
    value.hatch_scale = 1.25;
    value.hatch_transparency = 30;
    value.reserved_flags = {true, false};
    value.hatch_angles = {45};
    return value;
}
static DataLink sample_link() {
    DataLink value;
    value.data_adapter = "AcExcel";
    value.description = "Schedule";
    value.tooltip = "Excel";
    value.connection_string = "file.xlsx";
    value.option = 1;
    value.update_option = 2;
    value.flags = 4;
    value.year = 2024;
    value.month = 11;
    value.day = 16;
    value.hour = 7;
    value.path_option = 1;
    value.status_flags = 8;
    value.update_status = "ok";
    value.custom_data.push_back({0xE301, "ignored"});
    TableCustomData text;
    text.name = "x";
    text.value.type = 4;
    text.value.text = "hello";
    text.value.formatted = "hello";
    TableCustomData number;
    number.name = "y";
    number.value.type = 2;
    number.value.number = 12.5;
    number.value.formatted = "12.5";
    value.data_map.push_back(std::move(text));
    value.data_map.push_back(std::move(number));
    value.hard_owner = 0xE302;
    return value;
}
static std::vector<std::uint8_t> hex_bytes(std::string_view text) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        auto digit = [](char c) {
            return c >= '0' && c <= '9'   ? c - '0'
                   : c >= 'a' && c <= 'f' ? c - 'a' + 10
                   : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                          : -1;
        };
        int a = digit(text[i]), b = digit(text[i + 1]);
        check(a >= 0 && b >= 0, "Invalid hex");
        out.push_back(static_cast<std::uint8_t>(a * 16 + b));
    }
    return out;
}
static ObjectDataTable sample_ird_table() {
    ObjectDataTable table;
    table.name = "TEST";
    table.fields = {{"VOLTAGE", ObjectDataType::real(), "the voltage"},
                    {"CIRCUITS", ObjectDataType::integer(), "the circuit"},
                    {"OWNER", ObjectDataType::character(), "the owner"},
                    {"ENERGIZED", ObjectDataType::integer(), "is it energized"}};
    return table;
}
static ObjectDataDescriptor sample_ird_desc() { return ObjectDataDescriptor::table(sample_ird_table()); }
static ObjectDataRecord sample_ird_record() {
    return {0xE401,
            sample_ird_table().fields,
            {ObjectDataValue::from_real(230), ObjectDataValue::from_integer(1),
             ObjectDataValue::from_character("hello"), ObjectDataValue::from_integer(1)}};
}
static ObjectDataDictionary sample_ird_dict() {
    ObjectDataDictionary value;
    value.entries = {{"*A1", 0xE401}, {"*A2", 0xE402}};
    return value;
}
static ViewRepGuid sample_view_rep_guid() {
    return {0x12345678, 0x1111, 0x2222, {1, 2, 3, 4, 5, 6, 7, 8}};
}
static std::array<double, 16> sample_view_rep_transform() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1};
}
static ViewRepSourceManager sample_view_rep_mgr() { return {true, 0xE501, 1}; }
static ViewRepStandard sample_view_rep_standard() { return {{1, 2, 3, 4, 5, 6}}; }
static ViewRepOrientation sample_view_rep_orientation() {
    return {{0, 0, 100}, {0, 0, 0}, {0, 1, 0}};
}
static ViewRepSectionDefinition sample_view_rep_section() { return {1, 25.5, {2, 3}}; }
static ViewRepModelSpaceViewSelectionSet sample_view_rep_selset() { return {1, {0xE502}}; }
static ViewRepModelSpaceSource sample_view_rep_source() {
    ViewRepModelSpaceSource value;
    value.enabled = true;
    value.header_values = {1, 2, 3, 4, 5, 6};
    value.transform = sample_view_rep_transform();
    value.source_version = 1;
    value.source_status = 2;
    value.model = 0xE503;
    value.guid = sample_view_rep_guid();
    value.references = {0xE504, 0xE505};
    value.tail_values = {3, 4};
    value.orientation = 0xE506;
    return value;
}
static ViewRep sample_view_rep() {
    ViewRep value;
    value.header_values = {1, 2, 3, 4, 5};
    value.name = "Front";
    value.scale = 2;
    value.header_status = 1;
    value.description = "view";
    value.source_id = 11;
    value.source_enabled = true;
    value.source_version = 1;
    value.model_id = 22;
    value.guid = sample_view_rep_guid();
    value.marker = 9;
    value.transform = sample_view_rep_transform();
    value.transform_version = 1;
    value.database_id = 33;
    value.geometry_version = 1;
    ViewRepSketch line;
    line.id = 7;
    line.version = 1;
    line.references = {{0xE507, true}};
    line.enabled = true;
    line.geometry.kind = ViewRepSketchGeometry::Kind::Line;
    line.geometry.type_code = 19;
    line.geometry.first = {0, 0, 0};
    line.geometry.second = {10, 0, 0};
    ViewRepSketch nurb;
    nurb.id = 8;
    nurb.version = 1;
    nurb.enabled = true;
    nurb.final_flag = true;
    nurb.geometry.kind = ViewRepSketchGeometry::Kind::Nurb;
    nurb.geometry.type_code = 42;
    nurb.geometry.flags = {true, false};
    nurb.geometry.degree = 3;
    nurb.geometry.tolerance = 0.01;
    nurb.geometry.knot_header = {2, 0, 0};
    nurb.geometry.knots = {0, 1};
    nurb.geometry.weight_header = {2, 0, 0};
    nurb.geometry.weights = {1, 1};
    nurb.geometry.point_header = {2, 0, 0};
    nurb.geometry.control_points = {{0, 0, 0}, {1, 1, 0}};
    value.sketches = {std::move(line), std::move(nurb)};
    value.related_objects = {0xE508, 0xE509};
    value.source_manager = 0xE50A;
    value.owned_objects = {0xE50B, 0xE50C};
    value.optional_objects = {0xE50D, 0xE50E};
    value.position = {100, 200};
    value.rotation = 90;
    value.orientation = 0xE50F;
    value.is_active = true;
    value.projection = 1;
    value.linked_views = {0xE510, 0xE511};
    value.section_sketches = {{"AcDbViewRepSketch", {0xE512, 0xE513}}};
    value.action_mode = 1;
    value.action = 0xE514;
    value.has_parent = true;
    value.parent = 0xE515;
    value.tail_version = 1;
    value.tail_state = 2;
    value.tail_id = 44;
    value.path_count = 1;
    value.path_version = 1;
    value.path_id = 55;
    value.has_block_path = true;
    value.block_path = ViewRepBlockPath{"AcDbBlockReferencePath", 1, {{1, 2, 0xE516}}};
    value.style = 0xE517;
    return value;
}
static std::uint64_t viewrep_h(bool remap, std::uint64_t dxf, std::uint64_t dwg) {
    return remap ? dwg : dxf;
}
static void check_gradient(const GradientBackground &value) {
    auto expected = sample_gradient();
    check(value.class_version == expected.class_version, "Gradient version");
    check(value.color_top == expected.color_top, "Gradient top");
    check(near(value.horizon, expected.horizon), "Gradient horizon");
    check(near(value.rotation, expected.rotation), "Gradient rotation");
}
static void check_ground(const GroundPlaneBackground &value) {
    auto expected = sample_ground();
    check(value.color_sky_zenith == expected.color_sky_zenith, "Ground zenith");
    check(value.color_far == expected.color_far, "Ground far");
}
static void check_ibl(const IblBackground &value, std::uint64_t secondary) {
    auto expected = sample_ibl();
    check(value.enabled == expected.enabled, "IBL enabled");
    check(value.name == expected.name, "IBL name");
    check(near(value.rotation, expected.rotation), "IBL rotation");
    check(value.secondary_background == secondary, "IBL secondary");
}
static void check_image(const ImageBackground &value, bool full_xy) {
    auto expected = sample_image();
    check(value.filename == expected.filename, "Image filename");
    check(value.fit_to_screen == expected.fit_to_screen, "Image fit");
    check(value.use_tiling == expected.use_tiling, "Image tiling");
    check(near(value.offset.x, expected.offset.x), "Image offset x");
    check(near(value.scale.x, expected.scale.x), "Image scale x");
    if (full_xy) {
        check(near(value.offset.y, expected.offset.y), "Image offset y");
        check(near(value.scale.y, expected.scale.y), "Image scale y");
    }
}
static void check_sky(const SkyLightBackground &value, std::uint64_t sun) {
    check(value.class_version == 1, "Sky version");
    check(value.sun == sun, "Sky sun");
}
static void check_solid(const SolidBackground &value) {
    check(value.color == 0x00112233, "Solid color");
}
static void check_entry(const RenderEntry &value) {
    auto expected = sample_entry();
    check(value.image_filename == expected.image_filename, "RenderEntry image");
    check(value.width == expected.width, "RenderEntry width");
    check(value.end_millisecond == expected.end_millisecond, "RenderEntry end ms");
    check(near(value.render_time, expected.render_time), "RenderEntry time");
}
static void check_environment(const RenderEnvironment &value) {
    auto expected = sample_environment();
    check(value.fog_enabled == expected.fog_enabled, "RenderEnvironment fog");
    check(value.fog_color == expected.fog_color, "RenderEnvironment color");
    check(near(value.fog_density_far, expected.fog_density_far), "RenderEnvironment density");
    check(value.environment_image_filename == expected.environment_image_filename,
          "RenderEnvironment file");
}
static void check_global(const RenderGlobal &value) {
    auto expected = sample_global();
    check(value.procedure == expected.procedure, "RenderGlobal procedure");
    check(value.save_filename == expected.save_filename, "RenderGlobal file");
    check(value.image_height == expected.image_height, "RenderGlobal height");
}
static void check_lights(const LightList &value, std::uint64_t handle) {
    check(value.class_version == 2, "LightList version");
    check(value.lights.size() == 1 && value.lights[0].name == "spot", "LightList name");
    check(value.lights[0].handle == handle, "LightList handle");
}
static void check_sun(const Sun &value) {
    auto expected = sample_sun();
    check(value.is_on == expected.is_on, "Sun on");
    check(value.color == expected.color, "Sun color");
    check(near(value.intensity, expected.intensity), "Sun intensity");
    check(value.julian_day == expected.julian_day, "Sun julian");
    check(value.shadow_map_size == expected.shadow_map_size, "Sun map");
    check(value.shadow_softness == expected.shadow_softness, "Sun softness");
}
static void check_motion(const MotionPath &value, std::uint64_t camera, std::uint64_t target,
                         std::uint64_t view) {
    check(value.class_version == 1, "Motion version");
    check(value.camera_path == camera, "Motion camera");
    check(value.target_path == target, "Motion target");
    check(value.view == view, "Motion view");
    check(value.frames == 30 && value.frame_rate == 24, "Motion frames");
    check(value.corner_deceleration, "Motion corner");
}
static void check_curve(const CurvePath &value, std::uint64_t entity) {
    check(value.class_version == 1, "Curve version");
    check(value.entity == entity, "Curve entity");
}
static void check_point(const PointPath &value) {
    auto expected = sample_point();
    check(value.class_version == expected.class_version, "PointPath version");
    check(near(value.point.x, expected.point.x) && near(value.point.y, expected.point.y) &&
              near(value.point.z, expected.point.z),
          "PointPath point");
}
static void check_tv(const TvDeviceProperties &value) {
    auto expected = sample_tv();
    check(value.flags == expected.flags, "TV flags");
    check(value.max_regen_threads == expected.max_regen_threads, "TV threads");
    check(value.alternate_highlight == expected.alternate_highlight, "TV highlight");
    check(near(value.antialiasing_level, expected.antialiasing_level), "TV AA");
}
static void check_filter(const LayerFilter &value) {
    check(value.names.size() == 2 && value.names[0] == "Walls" && value.names[1] == "Doors",
          "LayerFilter names");
}
static void check_partial(const PartialViewingIndex &value, std::uint64_t object) {
    check(value.has_entries && value.entries.size() == 1, "Partial viewing entries");
    check(near(value.entries[0].extents_max.x, 10) && near(value.entries[0].extents_max.z, 1),
          "Partial viewing extents");
    check(value.entries[0].object == object, "Partial viewing object");
}
static void check_section_settings(const SectionSettings &value, std::uint64_t source,
                                   std::uint64_t destination) {
    check(value.current_type == 1 && value.types.size() == 1, "Section settings type");
    check(value.types[0].generation == 2, "Section settings generation");
    check(value.types[0].sources.size() == 1 && value.types[0].sources[0] == source,
          "Section settings source");
    check(value.types[0].destination_block == destination, "Section settings destination");
    check(value.types[0].destination_file == "out.dwg", "Section settings file");
    check(value.types[0].geometry.size() == 1, "Section settings geometry");
    check(value.types[0].geometry[0].color == Color::from_index(1), "Section settings color");
    check(value.types[0].geometry[0].hatch_pattern == "ANSI31", "Section settings hatch");
    check(near(value.types[0].geometry[0].hatch_scale, 1.25), "Section settings scale");
}
static void check_study(const SunStudy &value, std::uint64_t wizard, std::uint64_t view,
                        std::uint64_t visual, std::uint64_t text) {
    check(value.class_version == 1 && value.setup_name == "Summer", "Sun study name");
    check(value.output_type == 0 && value.use_subset, "Sun study subset");
    check(value.sheet_set_name == "Set" && value.sheet_subset_name == "Sub", "Sun study sheets");
    check(value.dates.size() == 1 && value.dates[0].julian_day == 2451545, "Sun study dates");
    check(value.select_range_of_dates && value.start_time == 8 && value.end_time == 18, "Sun study range");
    check(value.hours.size() == 3 && value.hours[0] && !value.hours[1] && value.hours[2], "Sun study hours");
    check(near(value.spacing, 0.5) && value.lock_viewports, "Sun study layout");
    check(value.page_setup_wizard == wizard && value.view == view, "Sun study view handles");
    check(value.visual_style == visual && value.text_style == text, "Sun study style handles");
}
static void check_table(const DataTable &value, std::uint64_t handle) {
    check(value.flags == 1 && value.name == "Schedule" && value.row_count == 1, "Data table header");
    check(value.columns.size() == 2 && value.columns[0].name == "Count", "Data table columns");
    check(value.columns[0].rows[0].integer == 7, "Data table integer");
    check(value.columns[1].value_type == static_cast<std::int32_t>(DataTableCellType::ObjectId),
          "Data table object column");
    check(value.columns[1].rows[0].handle == handle, "Data table object");
}
static void check_geo(const GeoMapImage &value) {
    auto expected = sample_geo();
    check(value.class_version == expected.class_version, "GeoMap version");
    check(near(value.origin.x, expected.origin.x) && near(value.origin.z, expected.origin.z),
          "GeoMap origin");
    check(near(value.image_size.x, expected.image_size.x) &&
              near(value.image_size.y, expected.image_size.y),
          "GeoMap size");
    check(value.clipping_enabled && value.brightness == expected.brightness &&
              value.fade == expected.fade,
          "GeoMap display");
}
static void check_base(const ModelDocViewStyle &value, const ModelDocViewStyle &expected, bool full_base) {
    check(value.class_version == expected.class_version && value.description == expected.description,
          "View style description");
    check(value.modified_for_recompute == expected.modified_for_recompute, "View style recompute");
    if (full_base) {
        check(value.display_name == expected.display_name && value.flags == expected.flags,
              "View style display");
    } else {
        check(value.display_name.empty() && value.flags == 0, "View style display omitted");
    }
}
static void check_detail(const DetailViewStyle &value, std::uint64_t identifier, std::uint64_t arrow,
                         std::uint64_t boundary, std::uint64_t label, std::uint64_t connection,
                         std::uint64_t border, bool full_base) {
    auto expected = sample_detail();
    check_base(value.base, expected.base, full_base);
    check(value.class_version == expected.class_version && value.flags == expected.flags,
          "Detail flags");
    check(value.identifier_style == identifier && value.arrow_symbol == arrow, "Detail identifier");
    check(value.identifier_color == expected.identifier_color &&
              near(value.identifier_height, expected.identifier_height),
          "Detail identifier style");
    check(value.identifier_excluded_characters == expected.identifier_excluded_characters &&
              value.identifier_placement == expected.identifier_placement &&
              near(value.identifier_offset, expected.identifier_offset),
          "Detail identifier extras");
    check(near(value.arrow_symbol_size, expected.arrow_symbol_size), "Detail arrow size");
    check(value.boundary_linetype == boundary && value.view_label_text_style == label, "Detail styles");
    check(value.connection_linetype == connection && value.border_linetype == border, "Detail lines");
    check(value.boundary_lineweight == expected.boundary_lineweight &&
              value.model_edge == expected.model_edge,
          "Detail edge");
    check(value.view_label_pattern == expected.view_label_pattern, "Detail pattern");
}
static void check_section_style(const SectionViewStyle &value, std::uint64_t identifier,
                               std::uint64_t start, std::uint64_t end, std::uint64_t plane,
                               std::uint64_t bend, std::uint64_t label, bool full_base) {
    auto expected = sample_section_style();
    check_base(value.base, expected.base, full_base);
    check(value.class_version == expected.class_version && value.flags == expected.flags,
          "Section style flags");
    check(value.identifier_style == identifier && value.arrow_start_symbol == start &&
              value.arrow_end_symbol == end,
          "Section style identifier");
    check(value.plane_linetype == plane && value.bend_linetype == bend &&
              value.view_label_text_style == label,
          "Section style lines");
    check(near(value.end_line_overshoot, expected.end_line_overshoot) &&
              near(value.end_line_length, expected.end_line_length),
          "Section style ends");
    check(value.identifier_position == expected.identifier_position &&
              value.arrow_position == expected.arrow_position &&
              near(value.identifier_offset, expected.identifier_offset),
          "Section style positions");
    check(value.hatch_color == expected.hatch_color &&
              value.hatch_background_color == expected.hatch_background_color &&
              value.hatch_pattern == expected.hatch_pattern,
          "Section style hatch");
    check(value.reserved_flags[0] && !value.reserved_flags[1], "Section style reserved");
    check(value.hatch_angles.size() == 1 && near(value.hatch_angles[0], 45), "Section style angles");
}
static void check_link(const DataLink &value, std::uint64_t target, std::uint64_t owner, bool frames,
                      bool stolen_end) {
    check(value.data_adapter == "AcExcel" && value.description == "Schedule", "Data link header");
    check(value.option == 1 && value.update_option == 2 && value.year == 2024, "Data link options");
    check(value.custom_data.size() == 1 && value.custom_data[0].target == target, "Data link custom");
    check(value.custom_data[0].value == (stolen_end ? "ACVALUE_END" : ""), "Data link custom value");
    check(value.hard_owner == owner, "Data link owner");
    check(value.data_map.size() == 2 && value.data_map[0].name == "x" && value.data_map[1].name == "y",
          "Data link map names");
    check(value.data_map[0].value.type == 4 && value.data_map[0].value.text == "hello",
          "Data link text");
    check(value.data_map[1].value.type == 2 && near(value.data_map[1].value.number, 12.5),
          "Data link number");
    if (frames) {
        check(value.data_map[0].value.formatted == "hello", "Data link text format");
        check(value.data_map[1].value.formatted == "12.5", "Data link number format");
    }
}
static void check_ird_dict(const ObjectDataDictionary &value, std::uint64_t table, std::uint64_t extra) {
    check(value.duplicate_cloning == 1 && !value.hard_owner, "Object data dictionary flags");
    check(value.entries.size() == 2 && value.entries[0].first == "*A1" && value.entries[1].first == "*A2",
          "Object data dictionary keys");
    check(value.entries[0].second == table && value.entries[1].second == extra, "Object data dictionary handles");
}
static void check_ird_desc(const ObjectDataDescriptor &value) {
    auto expected = sample_ird_desc();
    check(value.kind == ObjectDataDescriptor::kind_table && value.name == "TEST-_-ade", "Object data descriptor");
    check(value.payload == expected.payload, "Object data descriptor payload");
    check(value.as_table() == sample_ird_table(), "Object data table");
}
static void check_ird_record(const ObjectDataRecord &value, std::uint64_t table) {
    auto expected = sample_ird_record();
    check(value.table == table, "Object data record table");
    check(value.payload == expected.payload, "Object data record payload");
    check(value.values(sample_ird_table().fields) == expected.values(sample_ird_table().fields),
          "Object data record values");
}
static void check_view_rep_guid(const ViewRepGuid &value) {
    auto expected = sample_view_rep_guid();
    check(value.data1 == expected.data1 && value.data2 == expected.data2 && value.data3 == expected.data3 &&
              value.data4 == expected.data4,
          "ViewRep guid");
}
static void check_view_rep_transform(const std::array<double, 16> &value) {
    auto expected = sample_view_rep_transform();
    for (std::size_t i = 0; i < expected.size(); ++i)
        check(near(value[i], expected[i]), "ViewRep transform");
}
static void check_view_rep_mgr(const ViewRepSourceManager &value, bool remap) {
    check(value.has_source && value.status == 1, "ViewRep source manager flags");
    check(value.source == viewrep_h(remap, 0xE501, 0xF19), "ViewRep source manager handle");
}
static void check_view_rep_standard(const ViewRepStandard &value) {
    check(value.values == sample_view_rep_standard().values, "ViewRep standard");
}
static void check_view_rep_orientation(const ViewRepOrientation &value) {
    auto expected = sample_view_rep_orientation();
    check(value.camera == expected.camera && value.target == expected.target && value.normal == expected.normal,
          "ViewRep orientation");
}
static void check_view_rep_section(const ViewRepSectionDefinition &value) {
    auto expected = sample_view_rep_section();
    check(value.version == expected.version && near(value.section_depth, expected.section_depth) &&
              value.flags == expected.flags,
          "ViewRep section definition");
}
static void check_view_rep_selset(const ViewRepModelSpaceViewSelectionSet &value, bool remap) {
    check(value.version == 1 && value.entities.size() == 1, "ViewRep selection set");
    check(value.entities[0] == viewrep_h(remap, 0xE502, 0xF1A), "ViewRep selection handle");
}
static void check_view_rep_source(const ViewRepModelSpaceSource &value, bool remap) {
    check(value.enabled && value.header_values == sample_view_rep_source().header_values, "ViewRep source header");
    check_view_rep_transform(value.transform);
    check(value.source_version == 1 && value.source_status == 2, "ViewRep source status");
    check(value.model == viewrep_h(remap, 0xE503, 0xF1B), "ViewRep source model");
    check_view_rep_guid(value.guid);
    check(value.references[0] == viewrep_h(remap, 0xE504, 0xF1C) &&
              value.references[1] == viewrep_h(remap, 0xE505, 0xF1D),
          "ViewRep source references");
    check(value.tail_values[0] == 3 && value.tail_values[1] == 4, "ViewRep source tail");
    check(value.orientation == viewrep_h(remap, 0xE506, 0xF1E), "ViewRep source orientation");
}
static void check_view_rep(const ViewRep &value, bool remap) {
    auto expected = sample_view_rep();
    check(value.header_values == expected.header_values && value.name == "Front" && value.scale == 2, "ViewRep header");
    check(value.header_status == 1 && value.description == "view" && value.source_id == 11, "ViewRep source");
    check(value.source_enabled && value.source_version == 1 && value.model_id == 22, "ViewRep model");
    check_view_rep_guid(value.guid);
    check(value.marker == 9, "ViewRep marker");
    check_view_rep_transform(value.transform);
    check(value.transform_version == 1 && value.database_id == 33 && value.geometry_version == 1, "ViewRep geometry");
    check(value.sketches.size() == 2, "ViewRep sketch count");
    check(value.sketches[0].id == 7 && value.sketches[0].enabled &&
              value.sketches[0].geometry.kind == ViewRepSketchGeometry::Kind::Line &&
              value.sketches[0].geometry.type_code == 19 && value.sketches[0].geometry.second.x == 10,
          "ViewRep line sketch");
    check(value.sketches[0].references.size() == 1 &&
              value.sketches[0].references[0].object == viewrep_h(remap, 0xE507, 0xF1F) &&
              value.sketches[0].references[0].flag,
          "ViewRep sketch reference");
    check(value.sketches[1].geometry.kind == ViewRepSketchGeometry::Kind::Nurb &&
              value.sketches[1].geometry.type_code == 42 && value.sketches[1].geometry.degree == 3 &&
              value.sketches[1].geometry.flags[0] && !value.sketches[1].geometry.flags[1] &&
              near(value.sketches[1].geometry.tolerance, 0.01) && value.sketches[1].geometry.knots.size() == 2 &&
              value.sketches[1].geometry.control_points.size() == 2,
          "ViewRep nurb sketch");
    check(value.related_objects[0] == viewrep_h(remap, 0xE508, 0xF20) &&
              value.related_objects[1] == viewrep_h(remap, 0xE509, 0xF21),
          "ViewRep related");
    check(value.source_manager == viewrep_h(remap, 0xE50A, 0xF22), "ViewRep source manager");
    check(value.owned_objects[0] == viewrep_h(remap, 0xE50B, 0xF23) &&
              value.owned_objects[1] == viewrep_h(remap, 0xE50C, 0xF24),
          "ViewRep owned");
    check(value.optional_objects[0] == viewrep_h(remap, 0xE50D, 0xF25) &&
              value.optional_objects[1] == viewrep_h(remap, 0xE50E, 0xF26),
          "ViewRep optional");
    check(near(value.position.x, 100) && near(value.position.y, 200) && near(value.rotation, 90), "ViewRep placement");
    check(value.orientation == viewrep_h(remap, 0xE50F, 0xF27) && value.is_active && value.projection == 1,
          "ViewRep orientation handle");
    check(value.linked_views[0] == viewrep_h(remap, 0xE510, 0xF28) &&
              value.linked_views[1] == viewrep_h(remap, 0xE511, 0xF29),
          "ViewRep linked views");
    check(value.section_sketches.size() == 1 && value.section_sketches[0].class_name == "AcDbViewRepSketch" &&
              value.section_sketches[0].objects.size() == 2 &&
              value.section_sketches[0].objects[0] == viewrep_h(remap, 0xE512, 0xF2A) &&
              value.section_sketches[0].objects[1] == viewrep_h(remap, 0xE513, 0xF2B),
          "ViewRep section path");
    check(value.action_mode == 1 && value.action && *value.action == viewrep_h(remap, 0xE514, 0xF2C),
          "ViewRep action");
    check(value.has_parent && value.parent == viewrep_h(remap, 0xE515, 0xF2D), "ViewRep parent");
    check(value.tail_version == 1 && value.tail_state == 2 && value.tail_id == 44, "ViewRep tail");
    check(value.path_count == 1 && value.path_version == 1 && value.path_id == 55 && value.has_block_path &&
              value.block_path && value.block_path->class_name == "AcDbBlockReferencePath" &&
              value.block_path->entries.size() == 1 &&
              value.block_path->entries[0].object == viewrep_h(remap, 0xE516, 0xF2E),
          "ViewRep block path");
    check(value.style == viewrep_h(remap, 0xE517, 0xF2F), "ViewRep style");
}
static DxfClassObject *typed(DxfDocument &d, std::string_view type) {
    DxfClassObject *found = nullptr;
    d.for_each<DxfClassObject>([&](DxfClassObject &object) {
        if (object.type() == type)
            found = &object;
    });
    check(found, "Missing class object");
    return found;
}
static void check_viewrep_dxf(DxfDocument &d) {
    check_view_rep_mgr(std::get<ViewRepSourceManager>(typed(d, "ACDBVIEWREPSOURCEMGR")->definition()), false);
    check_view_rep_standard(std::get<ViewRepStandard>(typed(d, "ACDBVIEWREPSTANDARD")->definition()));
    (void)std::get<ViewRepOrientationDefinition>(typed(d, "ACDBVIEWREPORIENTATIONDEF")->definition());
    check_view_rep_orientation(std::get<ViewRepOrientation>(typed(d, "ACDBVIEWREPORIENTATION")->definition()));
    check_view_rep_section(std::get<ViewRepSectionDefinition>(typed(d, "ACDBVIEWREPSECTIONDEFINITION")->definition()));
    check_view_rep_selset(std::get<ViewRepModelSpaceViewSelectionSet>(
                              typed(d, "ACDBSYMODELSPACEVIEWSELSET")->definition()),
                          false);
    check_view_rep_source(std::get<ViewRepModelSpaceSource>(typed(d, "ACDBVIEWREPMODELSPACESOURCE")->definition()),
                          false);
    check_view_rep(std::get<ViewRep>(typed(d, "ACDBVIEWREP")->definition()), false);
}
static void check_scene(DxfDocument &d, bool full_image_xy, bool full_style_base) {
    check_gradient(std::get<GradientBackground>(typed(d, "GRADIENT_BACKGROUND")->definition()));
    check_ground(std::get<GroundPlaneBackground>(typed(d, "GROUND_PLANE_BACKGROUND")->definition()));
    check_ibl(std::get<IblBackground>(typed(d, "RAPIDRTRENDERENVIRONMENT")->definition()), 0x3456);
    check_image(std::get<ImageBackground>(typed(d, "IMAGE_BACKGROUND")->definition()), full_image_xy);
    check_sky(std::get<SkyLightBackground>(typed(d, "SKYLIGHT_BACKGROUND")->definition()), 0x4567);
    check_solid(std::get<SolidBackground>(typed(d, "SOLID_BACKGROUND")->definition()));
    check_entry(std::get<RenderEntry>(typed(d, "RENDERENTRY")->definition()));
    check_environment(std::get<RenderEnvironment>(typed(d, "RENDERENVIRONMENT")->definition()));
    check_global(std::get<RenderGlobal>(typed(d, "RENDERGLOBAL")->definition()));
    check_lights(std::get<LightList>(typed(d, "LIGHTLIST")->definition()), 0x2345);
    check_sun(std::get<Sun>(typed(d, "SUN")->definition()));
    check_motion(std::get<MotionPath>(typed(d, "ACDBMOTIONPATH")->definition()), 0x5678, 0x6789, 0x789A);
    check_curve(std::get<CurvePath>(typed(d, "ACDBCURVEPATH")->definition()), 0x89AB);
    check_point(std::get<PointPath>(typed(d, "ACDBPOINTPATH")->definition()));
    check_tv(std::get<TvDeviceProperties>(typed(d, "TVDEVICEPROPERTIES")->definition()));
    check_filter(std::get<LayerFilter>(typed(d, "LAYERFILTER")->definition()));
    check_partial(std::get<PartialViewingIndex>(typed(d, "PARTIAL_VIEWING_INDEX")->definition()), 0xBCDE);
    check_section_settings(std::get<SectionSettings>(typed(d, "SECTION_SETTINGS")->definition()), 0x9ABC,
                           0xABCD);
    check_study(std::get<SunStudy>(typed(d, "SUNSTUDY")->definition()), 0xCDEF, 0xD012, 0xD123, 0xD234);
    check_table(std::get<DataTable>(typed(d, "DATATABLE")->definition()), 0xD345);
    check_geo(std::get<GeoMapImage>(typed(d, "GEOMAPIMAGE")->definition()));
    check_detail(std::get<DetailViewStyle>(typed(d, "ACDBDETAILVIEWSTYLE")->definition()), 0xE101, 0xE102,
                 0xE103, 0xE104, 0xE105, 0xE106, full_style_base);
    check_section_style(std::get<SectionViewStyle>(typed(d, "ACDBSECTIONVIEWSTYLE")->definition()),
                        0xE201, 0xE202, 0xE203, 0xE204, 0xE205, 0xE206, full_style_base);
    check_link(std::get<DataLink>(typed(d, "DATALINK")->definition()), 0xE301, 0xE302, true, true);
    check_ird_dict(std::get<ObjectDataDictionary>(typed(d, "IRD_DSC_DICT")->definition()), 0xE401, 0xE402);
    check_ird_desc(std::get<ObjectDataDescriptor>(typed(d, "IRD_DSC_RECORD")->definition()));
    check_ird_record(std::get<ObjectDataRecord>(typed(d, "IRD_OBJ_RECORD")->definition()), 0xE401);
    check_viewrep_dxf(d);
}
static void check_class_count(const DxfDocument &d, std::string_view name) {
    bool found = false;
    for (auto &c : d.classes())
        if (c.name == name) {
            found = true;
            check(c.instance_count >= 1 && !c.is_entity, "Class instance count");
        }
    check(found, "Missing CLASSES entry");
}
static void check_legacy_view_rep_standard(const char *path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Missing legacy R2010 ViewRep fixture");
    auto drawing = DwgDocument::read(input);
    constexpr std::uint64_t legacy_id = 0x10000000;
    constexpr std::uint64_t manager_id = 0x10000001;
    auto values = [](const DwgDocument &d, std::uint64_t id) {
        return std::get<ViewRepStandard>(d.class_object(id).value().data).values;
    };
    check(values(drawing, legacy_id) == sample_view_rep_standard().values, "Read legacy six-value ViewRep");

    auto legacy = drawing.class_object(legacy_id).value();
    auto &standard = std::get<ViewRepStandard>(legacy.data);
    standard.values.front() = 99;
    legacy.reactors = {manager_id};
    drawing.update_class_object(legacy);
    drawing = parse_dwg(dwg_bytes(drawing));
    check(values(drawing, legacy_id) == standard.values, "Direct edit preserves sixth ViewRep value");

    ClassObject fresh;
    fresh.data = sample_view_rep_standard();
    const auto fresh_id = drawing.insert_class_object(fresh);
    auto expected_fresh = sample_view_rep_standard().values;
    expected_fresh.back() = 0;
    check(values(drawing, fresh_id) == expected_fresh, "New R2010 ViewRep stores five values in a mixed drawing");
    auto edited_fresh = drawing.class_object(fresh_id).value();
    std::get<ViewRepStandard>(edited_fresh.data).values.back() = 42;
    drawing.update_class_object(edited_fresh);
    check(values(drawing, fresh_id) == expected_fresh, "Editing a five-value record retains its layout");

    drawing.remap_class_object_handles({{manager_id, fresh_id}});
    drawing = parse_dwg(dwg_bytes(drawing));
    check(values(drawing, legacy_id) == standard.values, "Remapping preserves legacy sixth ViewRep value");
    check(drawing.class_object(legacy_id)->reactors == std::vector<std::uint64_t>{fresh_id},
          "Legacy ViewRep reactor was remapped");
    check(values(drawing, fresh_id) == expected_fresh, "Remapping preserves the five-value layout");

    legacy = drawing.class_object(legacy_id).value();
    auto &remapped_standard = std::get<ViewRepStandard>(legacy.data);
    remapped_standard.values.back() = 0;
    drawing.update_class_object(legacy);
    drawing = parse_dwg(dwg_bytes(drawing));
    legacy = drawing.class_object(legacy_id).value();
    auto &reopened_standard = std::get<ViewRepStandard>(legacy.data);
    check(reopened_standard.values.back() == 0, "Legacy sixth ViewRep value can be set to zero");
    reopened_standard.values.back() = 17;
    drawing.update_class_object(legacy);
    drawing = parse_dwg(dwg_bytes(drawing));
    check(values(drawing, legacy_id) == reopened_standard.values, "Zero does not erase the legacy sixth field");
}

int main(int argc, char **argv) try {
    if (argc == 3 && std::string_view(argv[1]) == "--legacy-viewrep") {
        check_legacy_view_rep_standard(argv[2]);
        return 0;
    }
    if (argc != 2)
        return 2;
    const std::string prefix = argv[1];
    auto render = sample_render();
    auto rapid = sample_rapid();
    auto mental = sample_mental();
    SectionManager manager;
    manager.is_live = true;
    manager.sections = {0x1234};
    auto dxf = DxfDocument::create(false);
    auto root = dxf.root_dictionary_handle();
    dxf.add_class_object(root, "Render", render);
    dxf.add_class_object(root, "RapidRT", rapid);
    dxf.add_class_object(root, "MentalRay", mental);
    dxf.add_class_object(root, "Sections", manager);
    dxf.add_class_object(root, "Gradient", sample_gradient());
    dxf.add_class_object(root, "Ground", sample_ground());
    dxf.add_class_object(root, "Ibl", sample_ibl());
    dxf.add_class_object(root, "ImageBg", sample_image());
    dxf.add_class_object(root, "Sky", sample_sky());
    dxf.add_class_object(root, "SolidBg", sample_solid());
    dxf.add_class_object(root, "Entry", sample_entry());
    dxf.add_class_object(root, "Environment", sample_environment());
    dxf.add_class_object(root, "Global", sample_global());
    dxf.add_class_object(root, "Lights", sample_lights());
    dxf.add_class_object(root, "Motion", sample_motion());
    dxf.add_class_object(root, "Curve", sample_curve());
    dxf.add_class_object(root, "PointPath", sample_point());
    dxf.add_class_object(root, "TvDevice", sample_tv());
    dxf.add_class_object(root, "Filter", sample_filter());
    dxf.add_class_object(root, "Partial", sample_partial());
    dxf.add_class_object(root, "SectionSettings", sample_section_settings());
    dxf.add_class_object(root, "SunStudy", sample_study());
    dxf.add_class_object(root, "Table", sample_table());
    dxf.add_class_object(root, "GeoMap", sample_geo());
    dxf.add_class_object(root, "DetailStyle", sample_detail());
    dxf.add_class_object(root, "SectionStyle", sample_section_style());
    dxf.add_class_object(root, "Link", sample_link());
    dxf.add_class_object(root, "IrdDict", sample_ird_dict());
    dxf.add_class_object(root, "IrdDesc", sample_ird_desc());
    dxf.add_class_object(root, "IrdRec", sample_ird_record());
    dxf.add_class_object(root, "ViewMgr", sample_view_rep_mgr());
    dxf.add_class_object(root, "ViewStd", sample_view_rep_standard());
    dxf.add_class_object(root, "ViewOriDef", ViewRepOrientationDefinition{});
    dxf.add_class_object(root, "ViewOri", sample_view_rep_orientation());
    dxf.add_class_object(root, "ViewSec", sample_view_rep_section());
    dxf.add_class_object(root, "ViewSel", sample_view_rep_selset());
    dxf.add_class_object(root, "ViewSrc", sample_view_rep_source());
    dxf.add_class_object(root, "ViewRep", sample_view_rep());
    dxf.add_class_object(root, "Sun", sample_sun());
    check_render(std::get<RenderSettings>(typed(dxf, "RENDERSETTINGS")->definition()), render, true);
    check_rapid(std::get<RapidRtRenderSettings>(typed(dxf, "RAPIDRTRENDERSETTINGS")->definition()));
    check_mental(std::get<MentalRayRenderSettings>(typed(dxf, "MENTALRAYRENDERSETTINGS")->definition()));
    auto sections = std::get<SectionManager>(typed(dxf, "SECTION_MANAGER")->definition());
    check(sections.is_live && sections.sections == std::vector<std::uint64_t>{0x1234}, "Section manager");
    check_scene(dxf, true, true);
    bool mismatch = false;
    try {
        typed(dxf, "RENDERSETTINGS")->set_definition(mental);
    } catch (const std::invalid_argument &) {
        mismatch = true;
    }
    check(mismatch, "Type mismatch accepted");
    bool registered = false;
    try {
        dxf.add_registered_object(root, "Nope", "RENDERSETTINGS", {});
    } catch (const std::invalid_argument &e) {
        registered = std::string(e.what()).find("built-in") != std::string::npos;
    }
    check(registered, "Registered class accepted typed name");
    for (auto name : {"MENTALRAYRENDERSETTINGS", "RAPIDRTRENDERSETTINGS", "SECTION_MANAGER",
                      "GRADIENT_BACKGROUND", "GROUND_PLANE_BACKGROUND", "RAPIDRTRENDERENVIRONMENT",
                      "IBL_BACKGROUND", "IMAGE_BACKGROUND", "SKYLIGHT_BACKGROUND", "SOLID_BACKGROUND",
                      "RENDERENTRY", "RENDERENVIRONMENT", "RENDERGLOBAL", "LIGHTLIST", "SUN",
                      "ACDBMOTIONPATH", "MOTIONPATH", "ACDBCURVEPATH", "CURVEPATH", "ACDBPOINTPATH",
                      "POINTPATH", "TVDEVICEPROPERTIES", "LAYERFILTER", "PARTIAL_VIEWING_INDEX",
                      "SECTION_SETTINGS", "SUNSTUDY", "DATATABLE", "ACDBDATATABLE", "GEOMAPIMAGE",
                      "ACDBDETAILVIEWSTYLE", "DETAILVIEWSTYLE", "ACDBSECTIONVIEWSTYLE",
                      "SECTIONVIEWSTYLE", "DATALINK", "IRD_DSC_DICT", "IRD_DSC_RECORD",
                      "IRD_OBJ_RECORD", "ACDBVIEWREPSOURCEMGR", "ACDBVIEWREPSTANDARD",
                      "ACDBVIEWREPORIENTATIONDEF", "ACDBVIEWREPORIENTATION",
                      "ACDBVIEWREPSECTIONDEFINITION", "ACDBSYMODELSPACEVIEWSELSET",
                      "ACDBVIEWREPMODELSPACESOURCE", "ACDBVIEWREP"}) {
        bool refused = false;
        try {
            dxf.add_registered_object(root, std::string("Nope-") + name, name, {});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Registered class accepted typed name");
    }
    bool overflow = false;
    try {
        SectionManager huge;
        huge.sections.resize(100001);
        dxf.add_class_object(root, "Huge", huge);
    } catch (const std::invalid_argument &) {
        overflow = true;
    }
    check(overflow, "Invalid section count accepted");
    bool lights_overflow = false;
    try {
        LightList huge;
        huge.lights.resize(100001);
        dxf.add_class_object(root, "HugeLights", huge);
    } catch (const std::invalid_argument &) {
        lights_overflow = true;
    }
    check(lights_overflow, "Invalid light list count accepted");
    bool filter_overflow = false;
    try {
        LayerFilter huge;
        huge.names.resize(100001);
        dxf.add_class_object(root, "HugeFilter", huge);
    } catch (const std::invalid_argument &) {
        filter_overflow = true;
    }
    check(filter_overflow, "Invalid layer filter count accepted");
    bool settings_overflow = false;
    try {
        SectionSettings huge;
        huge.types.resize(5);
        dxf.add_class_object(root, "HugeSettings", huge);
    } catch (const std::invalid_argument &) {
        settings_overflow = true;
    }
    check(settings_overflow, "Invalid section settings count accepted");
    bool study_overflow = false;
    try {
        SunStudy huge = sample_study();
        huge.dates.resize(10001);
        dxf.add_class_object(root, "HugeStudy", huge);
    } catch (const std::invalid_argument &) {
        study_overflow = true;
    }
    check(study_overflow, "Invalid sun study date count accepted");
    bool table_overflow = false;
    try {
        DataTable huge;
        huge.row_count = 0;
        huge.columns.resize(100001);
        dxf.add_class_object(root, "HugeTable", huge);
    } catch (const std::invalid_argument &) {
        table_overflow = true;
    }
    check(table_overflow, "Invalid data table count accepted");
    bool table_type = false;
    try {
        DataTable huge = sample_table();
        huge.columns[0].value_type = 6;
        dxf.add_class_object(root, "BadTable", huge);
    } catch (const std::invalid_argument &) {
        table_type = true;
    }
    check(table_type, "Unsupported data table cell type accepted");
    bool hatch_overflow = false;
    try {
        SectionViewStyle huge = sample_section_style();
        huge.hatch_angles.resize(100001);
        dxf.add_class_object(root, "HugeHatch", huge);
    } catch (const std::invalid_argument &) {
        hatch_overflow = true;
    }
    check(hatch_overflow, "Invalid hatch angle count accepted");
    bool link_overflow = false;
    try {
        DataLink huge = sample_link();
        huge.custom_data.resize(100001);
        dxf.add_class_object(root, "HugeLink", huge);
    } catch (const std::invalid_argument &) {
        link_overflow = true;
    }
    check(link_overflow, "Invalid data link count accepted");
    bool map_overflow = false;
    try {
        DataLink huge = sample_link();
        huge.data_map.resize(100001);
        dxf.add_class_object(root, "HugeMap", huge);
    } catch (const std::invalid_argument &) {
        map_overflow = true;
    }
    check(map_overflow, "Invalid data map count accepted");
    bool ird_overflow = false;
    try {
        ObjectDataDictionary huge = sample_ird_dict();
        huge.entries.resize(100001);
        dxf.add_class_object(root, "HugeIrd", huge);
    } catch (const std::invalid_argument &) {
        ird_overflow = true;
    }
    check(ird_overflow, "Invalid object data count accepted");
    bool view_sel_overflow = false;
    try {
        ViewRepModelSpaceViewSelectionSet huge;
        huge.entities.resize(100001);
        dxf.add_class_object(root, "HugeViewSel", huge);
    } catch (const std::invalid_argument &) {
        view_sel_overflow = true;
    }
    check(view_sel_overflow, "Invalid view representation count accepted");
    bool view_sketch_overflow = false;
    try {
        ViewRep huge = sample_view_rep();
        huge.sketches.resize(100001);
        dxf.add_class_object(root, "HugeViewSketches", huge);
    } catch (const std::invalid_argument &) {
        view_sketch_overflow = true;
    }
    check(view_sketch_overflow, "Invalid view representation sketch count accepted");
    bool view_nurb_overflow = false;
    try {
        ViewRep huge = sample_view_rep();
        huge.sketches[1].geometry.knot_header[0] = 100001;
        dxf.add_class_object(root, "HugeViewNurb", huge);
    } catch (const std::invalid_argument &) {
        view_nurb_overflow = true;
    }
    check(view_nurb_overflow, "Invalid view representation spline count accepted");
    bool view_path_overflow = false;
    try {
        ViewRep huge = sample_view_rep();
        huge.section_sketches[0].objects.resize(10002);
        dxf.add_class_object(root, "HugeViewPath", huge);
    } catch (const std::invalid_argument &) {
        view_path_overflow = true;
    }
    check(view_path_overflow, "Invalid view representation path count accepted");
    auto ascii = dxf_bytes(dxf, false);
    auto objects = ascii.find("OBJECTS");
    check(objects != std::string::npos, "OBJECTS section");
    check(first_group_after(ascii.substr(objects), "AcDbRenderSettings", 90) == "3",
          "RENDERSETTINGS stores class_version+1");
    auto rapid_marker = ascii.find("RAPIDRTRENDERSETTINGS", objects);
    check(rapid_marker != std::string::npos, "RapidRT record");
    check(first_group_after(ascii.substr(rapid_marker), "AcDbRenderSettings", 90) == "2",
          "RapidRT stores class_version as-is");
    check(first_group_after(ascii.substr(objects), "AcDbModelDocViewStyle", 70) == "1",
          "View style base class version");
    check(first_group_after(ascii.substr(objects), "AcDbModelDocViewStyle", 300) == "Detail",
          "View style display name");
    write_file(prefix + "-ascii.dxf", ascii);
    write_file(prefix + "-binary.dxf", dxf_bytes(dxf, true));
    auto restored = parse_dxf(ascii);
    check_render(std::get<RenderSettings>(typed(restored, "RENDERSETTINGS")->definition()), render, true);
    check_rapid(std::get<RapidRtRenderSettings>(typed(restored, "RAPIDRTRENDERSETTINGS")->definition()));
    check_mental(std::get<MentalRayRenderSettings>(typed(restored, "MENTALRAYRENDERSETTINGS")->definition()));
    check_scene(restored, true, true);
    check_class_count(parse_dxf(ascii), "RENDERSETTINGS");
    check_class_count(parse_dxf(ascii), "LIGHTLIST");
    check_class_count(parse_dxf(ascii), "RAPIDRTRENDERENVIRONMENT");
    check_class_count(parse_dxf(ascii), "ACDBMOTIONPATH");
    check_class_count(parse_dxf(ascii), "TVDEVICEPROPERTIES");
    check_class_count(parse_dxf(ascii), "LAYERFILTER");
    check_class_count(parse_dxf(ascii), "PARTIAL_VIEWING_INDEX");
    check_class_count(parse_dxf(ascii), "SECTION_SETTINGS");
    check_class_count(parse_dxf(ascii), "SUNSTUDY");
    check_class_count(parse_dxf(ascii), "DATATABLE");
    check_class_count(parse_dxf(ascii), "GEOMAPIMAGE");
    check_class_count(parse_dxf(ascii), "ACDBDETAILVIEWSTYLE");
    check_class_count(parse_dxf(ascii), "ACDBSECTIONVIEWSTYLE");
    check_class_count(parse_dxf(ascii), "DATALINK");
    check_class_count(parse_dxf(ascii), "IRD_DSC_DICT");
    check_class_count(parse_dxf(ascii), "IRD_DSC_RECORD");
    check_class_count(parse_dxf(ascii), "IRD_OBJ_RECORD");
    check(first_group_after(ascii.substr(objects), "CIrdBaseRecord", 1070) == "516",
          "Object data payload version");
    check(first_group_after(ascii.substr(objects), "AcDbViewRepSourceMgr", 290) == "1",
          "ViewRep source manager flag");
    check(first_group_after(ascii.substr(objects), "AcDbViewRepStandard", 90) == "1", "ViewRep standard");
    check(first_group_after(ascii.substr(objects), "AcDbViewRepSectionDefinition", 90) == "1",
          "ViewRep section version");
    check(first_group_after(ascii.substr(objects), "AcDbViewRepModelSpaceSource", 5) == "E503",
          "ViewRep model handle");
    check_class_count(parse_dxf(ascii), "ACDBVIEWREPSOURCEMGR");
    check_class_count(parse_dxf(ascii), "ACDBVIEWREPSTANDARD");
    check_class_count(parse_dxf(ascii), "ACDBVIEWREPORIENTATIONDEF");
    check_class_count(parse_dxf(ascii), "ACDBVIEWREPORIENTATION");
    check_class_count(parse_dxf(ascii), "ACDBVIEWREPSECTIONDEFINITION");
    check_class_count(parse_dxf(ascii), "ACDBSYMODELSPACEVIEWSELSET");
    check_class_count(parse_dxf(ascii), "ACDBVIEWREPMODELSPACESOURCE");
    check_class_count(parse_dxf(ascii), "ACDBVIEWREP");
    check(sample_ird_desc().payload ==
              hex_bytes("0030000054004500530054000000610064006500000043006C00610073007300000049007200640000000000"
                        "3100000004000080000056004F004C005400410047004500000043006C006100730073005200650061006C00"
                        "00004900720064000000740068006500200076006F006C007400610067006500000000000000008000004300"
                        "4900520043005500490054005300000043006C006100730073004C006F006E00670000004900720064000000"
                        "74006800650020006300690072006300750069007400000000000000008000004F0057004E00450052000000"
                        "43006C0061007300730043006800610072000000490072006400000074006800650020006F0077006E006500"
                        "72000000000000000080000045004E0045005200470049005A0045004400000043006C006100730073004C00"
                        "6F006E0067000000490072006400000069007300200069007400200065006E0065007200670069007A006500"
                        "64000000000000000000"),
          "Object data descriptor bytes");
    check(sample_ird_record().payload == hex_bytes("00000000000000C06C4001000000680065006C006C006F00000001000000"),
          "Object data record bytes");
    auto binary = parse_dxf(dxf_bytes(dxf, true));
    check_render(std::get<RenderSettings>(typed(binary, "RENDERSETTINGS")->definition()), render, true);
    check_mental(std::get<MentalRayRenderSettings>(typed(binary, "MENTALRAYRENDERSETTINGS")->definition()));
    check_scene(binary, true, true);
    DxfConversionOptions allow;
    allow.allow_loss = true;
    for (auto version : {DxfVersion::AC1018, DxfVersion::AC1024, DxfVersion::AC1027, DxfVersion::AC1032}) {
        for (bool bin : {false, true}) {
            auto exported = dxf.export_version(version, bin, allow);
            check(exported.report.can_write, "Class object conversion");
            auto loaded = parse_dxf(exported.bytes);
            auto *settings = typed(loaded, "RENDERSETTINGS");
            auto value = std::get<RenderSettings>(settings->definition());
            check(value.class_version == 2, "Converted RENDERSETTINGS class version");
            check(value.display_index == 9, "Converted display index");
            check(value.has_predefined == (version >= DxfVersion::AC1027), "Converted predefined flag");
            auto *rapid_object = typed(loaded, "RAPIDRTRENDERSETTINGS");
            check(std::get<RapidRtRenderSettings>(rapid_object->definition()).base.class_version == 2,
                  "Converted RapidRT class version");
            check_gradient(std::get<GradientBackground>(typed(loaded, "GRADIENT_BACKGROUND")->definition()));
            check_lights(std::get<LightList>(typed(loaded, "LIGHTLIST")->definition()), 0x2345);
            check_sun(std::get<Sun>(typed(loaded, "SUN")->definition()));
            check_image(std::get<ImageBackground>(typed(loaded, "IMAGE_BACKGROUND")->definition()), true);
            check_motion(std::get<MotionPath>(typed(loaded, "ACDBMOTIONPATH")->definition()), 0x5678, 0x6789,
                         0x789A);
            check_point(std::get<PointPath>(typed(loaded, "ACDBPOINTPATH")->definition()));
            check_tv(std::get<TvDeviceProperties>(typed(loaded, "TVDEVICEPROPERTIES")->definition()));
            check_filter(std::get<LayerFilter>(typed(loaded, "LAYERFILTER")->definition()));
            check_partial(std::get<PartialViewingIndex>(typed(loaded, "PARTIAL_VIEWING_INDEX")->definition()),
                          0xBCDE);
            check_section_settings(std::get<SectionSettings>(typed(loaded, "SECTION_SETTINGS")->definition()),
                                   0x9ABC, 0xABCD);
            check_study(std::get<SunStudy>(typed(loaded, "SUNSTUDY")->definition()), 0xCDEF, 0xD012,
                        0xD123, 0xD234);
            check_table(std::get<DataTable>(typed(loaded, "DATATABLE")->definition()), 0xD345);
            check_geo(std::get<GeoMapImage>(typed(loaded, "GEOMAPIMAGE")->definition()));
            auto style_base = version >= DxfVersion::AC1032;
            check_detail(std::get<DetailViewStyle>(typed(loaded, "ACDBDETAILVIEWSTYLE")->definition()),
                         0xE101, 0xE102, 0xE103, 0xE104, 0xE105, 0xE106, style_base);
            check_section_style(
                std::get<SectionViewStyle>(typed(loaded, "ACDBSECTIONVIEWSTYLE")->definition()), 0xE201,
                0xE202, 0xE203, 0xE204, 0xE205, 0xE206, style_base);
            auto frames = version >= DxfVersion::AC1021;
            check_link(std::get<DataLink>(typed(loaded, "DATALINK")->definition()), 0xE301, 0xE302,
                       frames, frames);
            check_ird_dict(std::get<ObjectDataDictionary>(typed(loaded, "IRD_DSC_DICT")->definition()), 0xE401,
                           0xE402);
            check_ird_desc(std::get<ObjectDataDescriptor>(typed(loaded, "IRD_DSC_RECORD")->definition()));
            check_ird_record(std::get<ObjectDataRecord>(typed(loaded, "IRD_OBJ_RECORD")->definition()), 0xE401);
            check_viewrep_dxf(loaded);
            auto tag = std::string("-") + std::string(dxf_version_string(version)) +
                       (bin ? "-binary.dxf" : "-ascii.dxf");
            write_file(prefix + tag, exported.bytes);
            if (!bin && version == DxfVersion::AC1024) {
                auto section = exported.bytes.find("OBJECTS");
                check(first_group_after(exported.bytes.substr(section), "AcDbRenderSettings", 90) == "2",
                      "AC1024 RENDERSETTINGS stores class_version as-is");
            }
            if (!bin && version == DxfVersion::AC1027) {
                auto section = exported.bytes.find("OBJECTS");
                auto at = exported.bytes.find("RAPIDRTRENDERSETTINGS", section);
                check(first_group_after(exported.bytes.substr(at), "AcDbRenderSettings", 90) == "2",
                      "AC1027 RapidRT stores class_version as-is");
            }
        }
    }
    std::string source = __FILE__;
    auto slash = source.find_last_of('/');
    std::ifstream legacy_in(source.substr(0, slash) + "/data/modeler/AC1015-ascii.dxf", std::ios::binary);
    auto old = DxfDocument::read(legacy_in);
    check(old.version() == DxfVersion::AC1015, "AC1015 fixture");
    bool refused = false;
    try {
        old.add_class_object(old.root_dictionary_handle(), "Render", render);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "AC1015 construction accepted");
    R12ExportOptions r12;
    r12.allow_loss = true;
    auto dropped = dxf.export_r12(r12);
    check(dropped.report.can_write && dropped.report.removed_objects > 0, "R12 drops OBJECTS class records");
    auto strict = dxf.export_r12();
    check(!strict.report.can_write, "R12 strict accepted class objects");
    std::istringstream dwg_in(create_dwg(DxfVersion::AC1032, {}));
    auto dwg = DwgDocument::read(dwg_in);
    ClassObject render_object;
    render_object.data = render;
    auto render_id = dwg.insert_class_object(render_object);
    ClassObject rapid_object;
    rapid_object.data = rapid;
    auto rapid_id = dwg.insert_class_object(rapid_object);
    ClassObject mental_object;
    mental_object.data = mental;
    auto mental_id = dwg.insert_class_object(mental_object);
    ClassObject section_object;
    section_object.data = manager;
    auto section_id = dwg.insert_class_object(section_object);
    auto insert_data = [&](const ClassObjectData &data) {
        ClassObject rec;
        rec.data = data;
        return dwg.insert_class_object(rec);
    };
    auto gradient_id = insert_data(sample_gradient());
    auto ground_id = insert_data(sample_ground());
    auto ibl_id = insert_data(sample_ibl());
    auto image_id = insert_data(sample_image());
    auto sky_id = insert_data(sample_sky());
    auto solid_id = insert_data(sample_solid());
    auto entry_id = insert_data(sample_entry());
    auto environment_id = insert_data(sample_environment());
    auto global_id = insert_data(sample_global());
    auto lights_id = insert_data(sample_lights());
    auto sun_id = insert_data(sample_sun());
    auto motion_id = insert_data(sample_motion());
    auto curve_id = insert_data(sample_curve());
    auto point_id = insert_data(sample_point());
    auto tv_id = insert_data(sample_tv());
    auto filter_id = insert_data(sample_filter());
    auto partial_id = insert_data(sample_partial());
    auto settings_id = insert_data(sample_section_settings());
    auto study_id = insert_data(sample_study());
    auto table_id = insert_data(sample_table());
    auto geo_id = insert_data(sample_geo());
    auto detail_id = insert_data(sample_detail());
    auto section_style_id = insert_data(sample_section_style());
    auto link_id = insert_data(sample_link());
    auto ird_dict_id = insert_data(sample_ird_dict());
    auto ird_desc_id = insert_data(sample_ird_desc());
    auto ird_rec_id = insert_data(sample_ird_record());
    auto view_mgr_id = insert_data(sample_view_rep_mgr());
    auto view_std_id = insert_data(sample_view_rep_standard());
    auto view_ori_def_id = insert_data(ViewRepOrientationDefinition{});
    auto view_ori_id = insert_data(sample_view_rep_orientation());
    auto view_sec_id = insert_data(sample_view_rep_section());
    auto view_sel_id = insert_data(sample_view_rep_selset());
    auto view_src_id = insert_data(sample_view_rep_source());
    auto view_rep_id = insert_data(sample_view_rep());
    check_render(std::get<RenderSettings>(dwg.class_object(render_id)->data), render, true);
    check_rapid(std::get<RapidRtRenderSettings>(dwg.class_object(rapid_id)->data));
    check_mental(std::get<MentalRayRenderSettings>(dwg.class_object(mental_id)->data));
    check(std::get<SectionManager>(dwg.class_object(section_id)->data).sections ==
              std::vector<std::uint64_t>{0x1234},
          "DWG section manager");
    auto identity = *dwg.class_object(rapid_id);
    dwg.update_class_object(identity);
    check(std::get<RapidRtRenderSettings>(dwg.class_object(rapid_id)->data).base.class_version == 2,
          "DWG RapidRT identity rewrite");
    auto saved = dwg_bytes(dwg);
    auto damaged = *dwg.class_object(render_id);
    damaged.dxf_name = "SECTION_MANAGER";
    bool rollback = false;
    try {
        dwg.update_class_object(damaged);
    } catch (const std::invalid_argument &) {
        rollback = true;
    }
    check(rollback && dwg_bytes(dwg) == saved, "DWG name mismatch mutated document");
    ClassObject missing;
    missing.handle = 0xFFFFFFFFu;
    missing.dxf_name = "RENDERSETTINGS";
    missing.data = render;
    bool unknown = false;
    try {
        dwg.update_class_object(missing);
    } catch (const std::invalid_argument &) {
        unknown = true;
    }
    check(unknown, "Unknown DWG class object rewrite accepted");
    dwg.remap_class_object_handles({{0x1234, 0xABC},
                                    {0x2345, 0xBCD},
                                    {0x3456, 0xCDE},
                                    {0x4567, 0xDEF},
                                    {0x5678, 0xE01},
                                    {0x6789, 0xE02},
                                    {0x789A, 0xE03},
                                    {0x89AB, 0xE04},
                                    {0x9ABC, 0xF01},
                                    {0xABCD, 0xF02},
                                    {0xBCDE, 0xF03},
                                    {0xCDEF, 0xF04},
                                    {0xD012, 0xF05},
                                    {0xD123, 0xF06},
                                    {0xD234, 0xF07},
                                    {0xD345, 0xF08},
                                    {0xE101, 0xF09},
                                    {0xE102, 0xF0A},
                                    {0xE103, 0xF0B},
                                    {0xE104, 0xF0C},
                                    {0xE105, 0xF0D},
                                    {0xE106, 0xF0E},
                                    {0xE201, 0xF0F},
                                    {0xE202, 0xF10},
                                    {0xE203, 0xF11},
                                    {0xE204, 0xF12},
                                    {0xE205, 0xF13},
                                    {0xE206, 0xF14},
                                    {0xE301, 0xF15},
                                    {0xE302, 0xF16},
                                    {0xE401, 0xF17},
                                    {0xE402, 0xF18},
                                    {0xE501, 0xF19},
                                    {0xE502, 0xF1A},
                                    {0xE503, 0xF1B},
                                    {0xE504, 0xF1C},
                                    {0xE505, 0xF1D},
                                    {0xE506, 0xF1E},
                                    {0xE507, 0xF1F},
                                    {0xE508, 0xF20},
                                    {0xE509, 0xF21},
                                    {0xE50A, 0xF22},
                                    {0xE50B, 0xF23},
                                    {0xE50C, 0xF24},
                                    {0xE50D, 0xF25},
                                    {0xE50E, 0xF26},
                                    {0xE50F, 0xF27},
                                    {0xE510, 0xF28},
                                    {0xE511, 0xF29},
                                    {0xE512, 0xF2A},
                                    {0xE513, 0xF2B},
                                    {0xE514, 0xF2C},
                                    {0xE515, 0xF2D},
                                    {0xE516, 0xF2E},
                                    {0xE517, 0xF2F}});
    check(std::get<SectionManager>(dwg.class_object(section_id)->data).sections ==
              std::vector<std::uint64_t>{0xABC},
          "DWG section handle remap");
    check_gradient(std::get<GradientBackground>(dwg.class_object(gradient_id)->data));
    check_ground(std::get<GroundPlaneBackground>(dwg.class_object(ground_id)->data));
    check_ibl(std::get<IblBackground>(dwg.class_object(ibl_id)->data), 0xCDE);
    check_image(std::get<ImageBackground>(dwg.class_object(image_id)->data), true);
    check_sky(std::get<SkyLightBackground>(dwg.class_object(sky_id)->data), 0xDEF);
    check_solid(std::get<SolidBackground>(dwg.class_object(solid_id)->data));
    check_entry(std::get<RenderEntry>(dwg.class_object(entry_id)->data));
    check_environment(std::get<RenderEnvironment>(dwg.class_object(environment_id)->data));
    check_global(std::get<RenderGlobal>(dwg.class_object(global_id)->data));
    check_lights(std::get<LightList>(dwg.class_object(lights_id)->data), 0xBCD);
    check_sun(std::get<Sun>(dwg.class_object(sun_id)->data));
    check_motion(std::get<MotionPath>(dwg.class_object(motion_id)->data), 0xE01, 0xE02, 0xE03);
    check_curve(std::get<CurvePath>(dwg.class_object(curve_id)->data), 0xE04);
    check_point(std::get<PointPath>(dwg.class_object(point_id)->data));
    check_tv(std::get<TvDeviceProperties>(dwg.class_object(tv_id)->data));
    check_filter(std::get<LayerFilter>(dwg.class_object(filter_id)->data));
    check_partial(std::get<PartialViewingIndex>(dwg.class_object(partial_id)->data), 0xF03);
    check_section_settings(std::get<SectionSettings>(dwg.class_object(settings_id)->data), 0xF01, 0xF02);
    check_study(std::get<SunStudy>(dwg.class_object(study_id)->data), 0xF04, 0xF05, 0xF06, 0xF07);
    check_table(std::get<DataTable>(dwg.class_object(table_id)->data), 0xF08);
    check_geo(std::get<GeoMapImage>(dwg.class_object(geo_id)->data));
    check_detail(std::get<DetailViewStyle>(dwg.class_object(detail_id)->data), 0xF09, 0xF0A, 0xF0B, 0xF0C,
                 0xF0D, 0xF0E, true);
    check_section_style(std::get<SectionViewStyle>(dwg.class_object(section_style_id)->data), 0xF0F, 0xF10,
                        0xF11, 0xF12, 0xF13, 0xF14, true);
    check_link(std::get<DataLink>(dwg.class_object(link_id)->data), 0xF15, 0xF16, true, false);
    check_ird_dict(std::get<ObjectDataDictionary>(dwg.class_object(ird_dict_id)->data), 0xF17, 0xF18);
    check_ird_desc(std::get<ObjectDataDescriptor>(dwg.class_object(ird_desc_id)->data));
    check_ird_record(std::get<ObjectDataRecord>(dwg.class_object(ird_rec_id)->data), 0xF17);
    check_view_rep_mgr(std::get<ViewRepSourceManager>(dwg.class_object(view_mgr_id)->data), true);
    check_view_rep_standard(std::get<ViewRepStandard>(dwg.class_object(view_std_id)->data));
    (void)std::get<ViewRepOrientationDefinition>(dwg.class_object(view_ori_def_id)->data);
    check_view_rep_orientation(std::get<ViewRepOrientation>(dwg.class_object(view_ori_id)->data));
    check_view_rep_section(std::get<ViewRepSectionDefinition>(dwg.class_object(view_sec_id)->data));
    check_view_rep_selset(std::get<ViewRepModelSpaceViewSelectionSet>(dwg.class_object(view_sel_id)->data),
                          true);
    check_view_rep_source(std::get<ViewRepModelSpaceSource>(dwg.class_object(view_src_id)->data), true);
    check_view_rep(std::get<ViewRep>(dwg.class_object(view_rep_id)->data), true);
    auto identity_ird = *dwg.class_object(ird_desc_id);
    dwg.update_class_object(identity_ird);
    check_ird_desc(std::get<ObjectDataDescriptor>(dwg.class_object(ird_desc_id)->data));
    auto identity_link = *dwg.class_object(link_id);
    dwg.update_class_object(identity_link);
    check_link(std::get<DataLink>(dwg.class_object(link_id)->data), 0xF15, 0xF16, true, false);
    auto identity_detail = *dwg.class_object(detail_id);
    dwg.update_class_object(identity_detail);
    check_detail(std::get<DetailViewStyle>(dwg.class_object(detail_id)->data), 0xF09, 0xF0A, 0xF0B, 0xF0C,
                 0xF0D, 0xF0E, true);
    auto identity_ibl = *dwg.class_object(ibl_id);
    dwg.update_class_object(identity_ibl);
    check_ibl(std::get<IblBackground>(dwg.class_object(ibl_id)->data), 0xCDE);
    auto identity_settings = *dwg.class_object(settings_id);
    dwg.update_class_object(identity_settings);
    check_section_settings(std::get<SectionSettings>(dwg.class_object(settings_id)->data), 0xF01, 0xF02);
    auto identity_view = *dwg.class_object(view_rep_id);
    dwg.update_class_object(identity_view);
    check_view_rep(std::get<ViewRep>(dwg.class_object(view_rep_id)->data), true);
    write_file(prefix + ".dwg", dwg_bytes(dwg));
    auto reopened = parse_dwg(dwg_bytes(dwg));
    check_render(std::get<RenderSettings>(reopened.class_object(render_id)->data), render, true);
    check_rapid(std::get<RapidRtRenderSettings>(reopened.class_object(rapid_id)->data));
    check_mental(std::get<MentalRayRenderSettings>(reopened.class_object(mental_id)->data));
    check(std::get<SectionManager>(reopened.class_object(section_id)->data).sections[0] == 0xABC,
          "DWG section remap roundtrip");
    check_gradient(std::get<GradientBackground>(reopened.class_object(gradient_id)->data));
    check_ibl(std::get<IblBackground>(reopened.class_object(ibl_id)->data), 0xCDE);
    check_image(std::get<ImageBackground>(reopened.class_object(image_id)->data), true);
    check_sky(std::get<SkyLightBackground>(reopened.class_object(sky_id)->data), 0xDEF);
    check_lights(std::get<LightList>(reopened.class_object(lights_id)->data), 0xBCD);
    check_sun(std::get<Sun>(reopened.class_object(sun_id)->data));
    check_entry(std::get<RenderEntry>(reopened.class_object(entry_id)->data));
    check_environment(std::get<RenderEnvironment>(reopened.class_object(environment_id)->data));
    check_motion(std::get<MotionPath>(reopened.class_object(motion_id)->data), 0xE01, 0xE02, 0xE03);
    check_curve(std::get<CurvePath>(reopened.class_object(curve_id)->data), 0xE04);
    check_point(std::get<PointPath>(reopened.class_object(point_id)->data));
    check_tv(std::get<TvDeviceProperties>(reopened.class_object(tv_id)->data));
    check_filter(std::get<LayerFilter>(reopened.class_object(filter_id)->data));
    check_partial(std::get<PartialViewingIndex>(reopened.class_object(partial_id)->data), 0xF03);
    check_section_settings(std::get<SectionSettings>(reopened.class_object(settings_id)->data), 0xF01, 0xF02);
    check_study(std::get<SunStudy>(reopened.class_object(study_id)->data), 0xF04, 0xF05, 0xF06, 0xF07);
    check_table(std::get<DataTable>(reopened.class_object(table_id)->data), 0xF08);
    check_geo(std::get<GeoMapImage>(reopened.class_object(geo_id)->data));
    check_detail(std::get<DetailViewStyle>(reopened.class_object(detail_id)->data), 0xF09, 0xF0A, 0xF0B,
                 0xF0C, 0xF0D, 0xF0E, true);
    check_section_style(std::get<SectionViewStyle>(reopened.class_object(section_style_id)->data), 0xF0F,
                        0xF10, 0xF11, 0xF12, 0xF13, 0xF14, true);
    check_link(std::get<DataLink>(reopened.class_object(link_id)->data), 0xF15, 0xF16, true, false);
    check_ird_dict(std::get<ObjectDataDictionary>(reopened.class_object(ird_dict_id)->data), 0xF17, 0xF18);
    check_ird_desc(std::get<ObjectDataDescriptor>(reopened.class_object(ird_desc_id)->data));
    check_ird_record(std::get<ObjectDataRecord>(reopened.class_object(ird_rec_id)->data), 0xF17);
    check_view_rep_mgr(std::get<ViewRepSourceManager>(reopened.class_object(view_mgr_id)->data), true);
    check_view_rep_standard(std::get<ViewRepStandard>(reopened.class_object(view_std_id)->data));
    check_view_rep_orientation(std::get<ViewRepOrientation>(reopened.class_object(view_ori_id)->data));
    check_view_rep_section(std::get<ViewRepSectionDefinition>(reopened.class_object(view_sec_id)->data));
    check_view_rep_selset(std::get<ViewRepModelSpaceViewSelectionSet>(reopened.class_object(view_sel_id)->data),
                          true);
    check_view_rep_source(std::get<ViewRepModelSpaceSource>(reopened.class_object(view_src_id)->data), true);
    check_view_rep(std::get<ViewRep>(reopened.class_object(view_rep_id)->data), true);
    std::istringstream ac1027_in(create_dwg(DxfVersion::AC1027, {}));
    auto ac1027 = DwgDocument::read(ac1027_in);
    ClassObject rapid_2013;
    rapid_2013.data = rapid;
    auto rapid_2013_id = ac1027.insert_class_object(rapid_2013);
    check(std::get<RapidRtRenderSettings>(ac1027.class_object(rapid_2013_id)->data).base.class_version == 2,
          "DWG AC1027 RapidRT class version");
    ClassObject standard;
    standard.data = sample_view_rep_standard();
    const auto standard_2013_id = ac1027.insert_class_object(standard);
    check_view_rep_standard(std::get<ViewRepStandard>(ac1027.class_object(standard_2013_id)->data));
    write_file(prefix + "-ac1027.dwg", dwg_bytes(ac1027));
    std::istringstream ac1024_in(create_dwg(DxfVersion::AC1024, {}));
    auto ac1024 = DwgDocument::read(ac1024_in);
    const auto standard_2010_id = ac1024.insert_class_object(standard);
    ClassObject manager_without_source;
    manager_without_source.data = ViewRepSourceManager{false, 0, 42};
    const auto manager_id = ac1024.insert_class_object(manager_without_source);
    const auto ac1024_bytes = dwg_bytes(ac1024);
    std::istringstream ac1024_saved(ac1024_bytes);
    auto reopened_2010 = DwgDocument::read(ac1024_saved);
    auto expected_2010 = sample_view_rep_standard().values;
    expected_2010.back() = 0;
    check(std::get<ViewRepStandard>(reopened_2010.class_object(standard_2010_id)->data).values ==
              expected_2010,
          "DWG AC1024 ViewRep standard has five values");
    const auto source_manager = std::get<ViewRepSourceManager>(reopened_2010.class_object(manager_id)->data);
    check(!source_manager.has_source && source_manager.source == 0 && source_manager.status == 42,
          "DWG ViewRep source manager without a source");
    write_file(prefix + "-ac1024.dwg", ac1024_bytes);
    std::istringstream ac1015_in(create_dwg(DxfVersion::AC1015, {}));
    auto ac1015 = DwgDocument::read(ac1015_in);
    bool dwg_legacy = false;
    try {
        ClassObject rec;
        rec.data = render;
        ac1015.insert_class_object(rec);
    } catch (const std::invalid_argument &) {
        dwg_legacy = true;
    }
    check(dwg_legacy, "DWG AC1015 construction accepted");
    std::cout << "Typed render, path, TV and ViewRep class objects verified\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
