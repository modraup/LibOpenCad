// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "color.hpp"
#include "dxf_version.hpp"
#include "object_data.hpp"
#include "table_content.hpp"
#include "vector.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
namespace libopencad {
// Typed ClassObject families from acadrust objects/class_object.rs:
// render/section-manager, render-scene remainder, motion paths, TV device,
// layer filter, partial viewing index, section settings, sun study, data table,
// geomap image, model-doc view styles, data links, Map 3D object data and
// ViewRep families.
struct RenderSettings {
    std::int32_t class_version = 0;
    std::string name;
    bool fog_enabled = false;
    bool fog_background_enabled = false;
    bool backfaces_enabled = false;
    bool environment_image_enabled = false;
    std::string environment_image_filename;
    std::string description;
    std::int32_t display_index = 0;
    bool has_predefined = false;
};
struct MentalRayRenderSettings {
    RenderSettings base;
    std::int32_t version = 0;
    std::int32_t sampling_min = 0;
    std::int32_t sampling_max = 0;
    std::int16_t sampling_filter = 0;
    double sampling_filter_width = 0;
    double sampling_filter_height = 0;
    std::array<double, 4> sampling_contrast{};
    std::int16_t shadow_mode = 0;
    bool shadow_maps_enabled = false;
    bool ray_tracing_enabled = false;
    std::array<std::int32_t, 3> ray_trace_depth{};
    bool global_illumination_enabled = false;
    std::int32_t global_illumination_sample_count = 0;
    bool global_illumination_sample_radius_enabled = false;
    double global_illumination_sample_radius = 0;
    std::int32_t photons_per_light = 0;
    std::array<std::int32_t, 3> photon_trace_depth{};
    bool final_gathering_enabled = false;
    std::int32_t final_gathering_ray_count = 0;
    std::array<bool, 3> final_gathering_sample_radius_state{};
    std::array<double, 2> final_gathering_sample_radius{};
    double light_luminance_scale = 0;
    std::int16_t diagnostics_mode = 0;
    std::int16_t diagnostics_grid_mode = 0;
    double diagnostics_grid_size = 0;
    std::int16_t diagnostics_photon_mode = 0;
    std::int16_t diagnostics_bsp_mode = 0;
    bool export_mi_enabled = false;
    std::string description;
    std::int32_t tile_size = 0;
    std::int16_t tile_order = 0;
    std::int32_t memory_limit = 0;
    bool diagnostics_samples_mode = false;
    double energy_multiplier = 0;
};
struct RapidRtRenderSettings {
    RenderSettings base;
    std::int32_t version = 0;
    std::int32_t render_target = 0;
    std::int32_t render_level = 0;
    std::int32_t render_time = 0;
    std::int32_t lighting_model = 0;
    std::int32_t filter_type = 0;
    double filter_width = 0;
    double filter_height = 0;
};
struct SectionManager {
    bool is_live = false;
    std::vector<std::uint64_t> sections;
};
struct GradientBackground {
    std::int32_t class_version = 0;
    std::uint32_t color_top = 0;
    std::uint32_t color_middle = 0;
    std::uint32_t color_bottom = 0;
    double horizon = 0;
    double height = 0;
    double rotation = 0;
};
struct GroundPlaneBackground {
    std::int32_t class_version = 0;
    std::uint32_t color_sky_zenith = 0;
    std::uint32_t color_sky_horizon = 0;
    std::uint32_t color_underground_horizon = 0;
    std::uint32_t color_underground_azimuth = 0;
    std::uint32_t color_near = 0;
    std::uint32_t color_far = 0;
};
struct IblBackground {
    std::int32_t class_version = 0;
    bool enabled = false;
    std::string name;
    double rotation = 0;
    bool display_image = false;
    std::uint64_t secondary_background = 0;
};
struct ImageBackground {
    std::int32_t class_version = 0;
    std::string filename;
    bool fit_to_screen = false;
    bool maintain_aspect_ratio = false;
    bool use_tiling = false;
    Vector2 offset;
    Vector2 scale;
};
struct SkyLightBackground {
    std::int32_t class_version = 0;
    std::uint64_t sun = 0;
};
struct SolidBackground {
    std::int32_t class_version = 0;
    std::uint32_t color = 0;
};
struct RenderEntry {
    std::int32_t class_version = 0;
    std::string image_filename;
    std::string preset_name;
    std::string view_name;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int16_t start_year = 0;
    std::int16_t start_month = 0;
    std::int16_t start_day = 0;
    std::int16_t start_hour = 0;
    std::int16_t start_minute = 0;
    std::int16_t start_second = 0;
    std::int16_t start_millisecond = 0;
    std::int16_t end_year = 0;
    std::int16_t end_month = 0;
    std::int16_t end_day = 0;
    std::int16_t end_hour = 0;
    std::int16_t end_minute = 0;
    std::int16_t end_second = 0;
    std::int16_t end_millisecond = 0;
    double render_time = 0;
    std::int32_t memory_amount = 0;
    std::int32_t material_count = 0;
    std::int32_t light_count = 0;
    std::int32_t triangle_count = 0;
    std::int32_t display_index = 0;
};
struct RenderEnvironment {
    std::int32_t class_version = 0;
    bool fog_enabled = false;
    bool fog_background_enabled = false;
    std::array<std::uint8_t, 3> fog_color{};
    double fog_density_near = 0;
    double fog_density_far = 0;
    double fog_distance_near = 0;
    double fog_distance_far = 0;
    bool environment_image_enabled = false;
    std::string environment_image_filename;
};
struct RenderGlobal {
    std::int32_t class_version = 0;
    std::int32_t procedure = 0;
    std::int32_t destination = 0;
    bool save_enabled = false;
    std::string save_filename;
    std::int32_t image_width = 0;
    std::int32_t image_height = 0;
    bool predefined_presets_first = false;
    bool high_level_info = false;
};
struct LightListEntry {
    std::uint64_t handle = 0;
    std::string name;
    bool operator==(const LightListEntry &other) const {
        return handle == other.handle && name == other.name;
    }
    bool operator!=(const LightListEntry &other) const { return !(*this == other); }
};
struct LightList {
    std::int32_t class_version = 0;
    std::vector<LightListEntry> lights;
};
struct Sun {
    std::int32_t class_version = 0;
    bool is_on = false;
    Color color;
    double intensity = 0;
    bool has_shadow = false;
    std::int32_t julian_day = 0;
    std::int32_t milliseconds = 0;
    bool is_daylight_savings_on = false;
    std::int32_t shadow_type = 0;
    std::int16_t shadow_map_size = 0;
    std::uint8_t shadow_softness = 0;
};
struct MotionPath {
    std::int32_t class_version = 0;
    std::uint64_t camera_path = 0;
    std::uint64_t target_path = 0;
    std::uint64_t view = 0;
    std::int16_t frames = 0;
    std::int16_t frame_rate = 0;
    bool corner_deceleration = false;
};
struct CurvePath {
    std::int32_t class_version = 0;
    std::uint64_t entity = 0;
};
struct PointPath {
    std::int16_t class_version = 0;
    Vector3 point;
};
struct TvDeviceProperties {
    std::uint32_t flags = 0;
    std::int16_t max_regen_threads = 0;
    std::int32_t use_lut_palette = 0;
    std::int64_t alternate_highlight = 0;
    std::int64_t alternate_highlight_color = 0;
    std::int64_t geometry_shader_usage = 0;
    std::int32_t blending_mode = 0;
    double antialiasing_level = 0;
    double reserved_double = 0;
};
struct LayerFilter {
    std::vector<std::string> names;
};
struct PartialViewingIndexEntry {
    Vector3 extents_min;
    Vector3 extents_max;
    std::uint64_t object = 0;
};
struct PartialViewingIndex {
    bool has_entries = false;
    std::vector<PartialViewingIndexEntry> entries;
};
struct SectionGeometrySettings {
    std::int32_t geometry_count = 0;
    std::int32_t index = 0;
    std::int32_t flags = 0;
    Color color;
    std::string layer;
    std::string linetype;
    double linetype_scale = 0;
    std::string plot_style;
    std::int32_t lineweight = 0;
    std::int16_t face_transparency = 0;
    std::int16_t edge_transparency = 0;
    std::int16_t hatch_type = 0;
    std::string hatch_pattern;
    double hatch_angle = 0;
    double hatch_spacing = 0;
    double hatch_scale = 0;
};
struct SectionTypeSettings {
    std::int32_t section_type = 0;
    std::int32_t generation = 0;
    std::vector<std::uint64_t> sources;
    std::uint64_t destination_block = 0;
    std::string destination_file;
    std::vector<SectionGeometrySettings> geometry;
};
struct SectionSettings {
    std::int32_t current_type = 0;
    std::vector<SectionTypeSettings> types;
};
struct SunStudyDate {
    std::int32_t julian_day = 0;
    std::int32_t milliseconds = 0;
};
struct SunStudy {
    std::int32_t class_version = 0;
    std::string setup_name;
    std::string description;
    std::int32_t output_type = 0;
    bool use_subset = false;
    std::string sheet_set_name;
    std::string sheet_subset_name;
    bool select_dates_from_calendar = false;
    std::vector<SunStudyDate> dates;
    bool select_range_of_dates = false;
    std::int32_t start_time = 0;
    std::int32_t end_time = 0;
    std::int32_t interval = 0;
    std::vector<bool> hours;
    std::int32_t shade_plot_type = 0;
    std::int32_t viewport_count = 0;
    std::int32_t rows = 0;
    std::int32_t columns = 0;
    double spacing = 0;
    bool lock_viewports = false;
    bool label_viewports = false;
    std::uint64_t page_setup_wizard = 0;
    std::uint64_t view = 0;
    std::uint64_t visual_style = 0;
    std::uint64_t text_style = 0;
};
enum class DataTableCellType : std::int32_t {
    Integer = 1,
    Double = 2,
    Text = 3,
    Point = 4,
    ObjectId = 5,
};
struct DataTableValue {
    std::int32_t integer = 0;
    double real = 0;
    std::string text;
    Vector3 point;
    std::uint64_t handle = 0;
};
struct DataTableColumn {
    std::int32_t value_type = 0;
    std::string name;
    std::vector<DataTableValue> rows;
};
struct DataTable {
    std::int16_t flags = 0;
    std::string name;
    std::int32_t row_count = 0;
    std::vector<DataTableColumn> columns;
};
struct GeoMapImage {
    std::int32_t class_version = 0;
    Vector3 origin;
    Vector2 image_size;
    std::int16_t display_properties = 0;
    bool clipping_enabled = false;
    std::uint8_t brightness = 0;
    std::uint8_t contrast = 0;
    std::uint8_t fade = 0;
};
struct ModelDocViewStyle {
    std::int16_t class_version = 0;
    std::string description;
    bool modified_for_recompute = false;
    std::string display_name;
    std::int32_t flags = 0;
    const std::string &display_name_or_description() const {
        return display_name.empty() ? description : display_name;
    }
};
struct DetailViewStyle {
    ModelDocViewStyle base;
    std::int16_t class_version = 0;
    std::int32_t flags = 0;
    std::uint64_t identifier_style = 0;
    Color identifier_color;
    double identifier_height = 0;
    std::string identifier_excluded_characters;
    double identifier_offset = 0;
    std::uint8_t identifier_placement = 0;
    std::uint64_t arrow_symbol = 0;
    Color arrow_symbol_color;
    double arrow_symbol_size = 0;
    std::uint64_t boundary_linetype = 0;
    std::int32_t boundary_lineweight = 0;
    Color boundary_color;
    std::uint64_t view_label_text_style = 0;
    Color view_label_text_color;
    double view_label_text_height = 0;
    std::int32_t view_label_attachment = 0;
    double view_label_offset = 0;
    std::int32_t view_label_alignment = 0;
    std::string view_label_pattern;
    std::uint64_t connection_linetype = 0;
    std::int32_t connection_lineweight = 0;
    Color connection_color;
    std::uint64_t border_linetype = 0;
    std::int32_t border_lineweight = 0;
    Color border_color;
    std::uint8_t model_edge = 0;
};
struct SectionViewStyle {
    ModelDocViewStyle base;
    std::int16_t class_version = 0;
    std::int32_t flags = 0;
    std::uint64_t identifier_style = 0;
    Color identifier_color;
    double identifier_height = 0;
    std::uint64_t arrow_start_symbol = 0;
    std::uint64_t arrow_end_symbol = 0;
    Color arrow_symbol_color;
    double arrow_symbol_size = 0;
    std::string identifier_excluded_characters;
    double arrow_symbol_extension_length = 0;
    std::uint64_t plane_linetype = 0;
    std::int32_t plane_lineweight = 0;
    Color plane_color;
    std::uint64_t bend_linetype = 0;
    std::int32_t bend_lineweight = 0;
    Color bend_color;
    double bend_line_length = 0;
    double end_line_length = 0;
    std::uint64_t view_label_text_style = 0;
    Color view_label_text_color;
    double view_label_text_height = 0;
    std::int32_t view_label_attachment = 0;
    double view_label_offset = 0;
    std::int32_t view_label_alignment = 0;
    std::string view_label_pattern;
    Color hatch_color;
    Color hatch_background_color;
    std::string hatch_pattern;
    double hatch_scale = 0;
    std::int32_t hatch_transparency = 0;
    std::array<bool, 2> reserved_flags{};
    std::int32_t identifier_position = 0;
    double identifier_offset = 0;
    std::int32_t arrow_position = 0;
    double end_line_overshoot = 0;
    std::vector<double> hatch_angles;
};
struct DataLinkCustomData {
    std::uint64_t target = 0;
    std::string value;
};
struct DataLink {
    static constexpr std::int32_t has_data_map = 8;
    std::string data_adapter;
    std::string description;
    std::string tooltip;
    std::string connection_string;
    std::int32_t option = 0;
    std::int32_t update_option = 0;
    std::int32_t flags = 0;
    std::int16_t year = 0;
    std::int16_t month = 0;
    std::int16_t day = 0;
    std::int16_t hour = 0;
    std::int16_t minute = 0;
    std::int16_t second = 0;
    std::int16_t millisecond = 0;
    std::int16_t path_option = 0;
    std::int32_t status_flags = 0;
    std::string update_status;
    std::vector<DataLinkCustomData> custom_data;
    std::vector<TableCustomData> data_map;
    std::uint64_t hard_owner = 0;
};
struct ViewRepSourceManager {
    bool has_source = false;
    std::uint64_t source = 0;
    std::int32_t status = 0;
};
struct ViewRepStandard {
    std::array<std::int32_t, 6> values{};
};
struct ViewRepOrientationDefinition {};
struct ViewRepOrientation {
    Vector3 camera{};
    Vector3 target{};
    Vector3 normal{};
};
struct ViewRepSectionDefinition {
    std::int32_t version = 0;
    double section_depth = 0;
    std::array<std::int32_t, 2> flags{};
};
struct ViewRepModelSpaceViewSelectionSet {
    std::int32_t version = 0;
    std::vector<std::uint64_t> entities;
};
struct ViewRepGuid {
    std::int32_t data1 = 0;
    std::int16_t data2 = 0;
    std::int16_t data3 = 0;
    std::array<std::uint8_t, 8> data4{};
};
struct ViewRepModelSpaceSource {
    bool enabled = false;
    std::array<std::int32_t, 6> header_values{};
    std::array<double, 16> transform{};
    std::int32_t source_version = 0;
    std::int32_t source_status = 0;
    std::uint64_t model = 0;
    ViewRepGuid guid;
    std::array<std::uint64_t, 2> references{};
    std::array<std::int32_t, 2> tail_values{};
    std::uint64_t orientation = 0;
};
struct ViewRepSketchReference {
    std::uint64_t object = 0;
    bool flag = false;
};
struct ViewRepSketchGeometry {
    enum class Kind { None, Line, Circle, Nurb } kind = Kind::None;
    std::int32_t type_code = 0;
    Vector3 first{}, second{}, center{}, normal{}, direction{};
    double radius = 0, start_parameter = 0, end_parameter = 0, reserved = 0, tolerance = 0;
    std::array<bool, 2> flags{};
    std::int32_t degree = 0;
    std::array<std::int32_t, 3> knot_header{}, weight_header{}, point_header{};
    std::vector<double> knots, weights;
    std::vector<Vector3> control_points;
};
struct ViewRepSketch {
    std::int32_t id = 0;
    std::int32_t version = 0;
    std::vector<ViewRepSketchReference> references;
    std::int32_t reserved = 0;
    bool enabled = false;
    ViewRepSketchGeometry geometry;
    bool final_flag = false;
};
struct ViewRepObjectPath {
    std::string class_name;
    std::vector<std::uint64_t> objects;
};
struct ViewRepBlockPathEntry {
    std::uint8_t flag = 0;
    std::uint8_t kind = 0;
    std::uint64_t object = 0;
};
struct ViewRepBlockPath {
    std::string class_name;
    std::int32_t version = 0;
    std::vector<ViewRepBlockPathEntry> entries;
};
struct ViewRep {
    std::array<std::int32_t, 5> header_values{};
    std::string name;
    std::int32_t scale = 0;
    std::int32_t header_status = 0;
    std::string description;
    std::int64_t source_id = 0;
    bool source_enabled = false;
    std::int32_t source_version = 0;
    std::int64_t model_id = 0;
    ViewRepGuid guid;
    std::uint8_t marker = 0;
    std::array<double, 16> transform{};
    std::int32_t transform_version = 0;
    std::int64_t database_id = 0;
    std::int32_t geometry_version = 0;
    std::int32_t geometry_marker = 0;
    std::vector<ViewRepSketch> sketches;
    std::array<std::uint64_t, 2> related_objects{};
    std::uint64_t source_manager = 0;
    std::array<std::uint64_t, 2> owned_objects{};
    std::array<std::uint64_t, 2> optional_objects{};
    Vector2 position{};
    double rotation = 0;
    std::uint64_t orientation = 0;
    bool is_active = false;
    std::int16_t projection = 0;
    std::array<std::uint64_t, 2> linked_views{};
    std::vector<ViewRepObjectPath> section_sketches;
    std::int32_t action_mode = 0;
    std::optional<std::uint64_t> action;
    bool has_parent = false;
    std::uint64_t parent = 0;
    std::int32_t tail_version = 0;
    std::int32_t tail_state = 0;
    std::int64_t tail_id = 0;
    std::int32_t path_count = 0;
    std::int32_t path_version = 0;
    std::int64_t path_id = 0;
    bool has_block_path = false;
    std::optional<ViewRepBlockPath> block_path;
    std::uint64_t style = 0;
};
using ClassObjectData =
    std::variant<RenderSettings, MentalRayRenderSettings, RapidRtRenderSettings, SectionManager,
                 GradientBackground, GroundPlaneBackground, IblBackground, ImageBackground,
                 SkyLightBackground, SolidBackground, RenderEntry, RenderEnvironment, RenderGlobal,
                 LightList, Sun, MotionPath, CurvePath, PointPath, TvDeviceProperties, LayerFilter,
                 PartialViewingIndex, SectionSettings, SunStudy, DataTable, GeoMapImage,
                 DetailViewStyle, SectionViewStyle, DataLink, ObjectDataDictionary,
                 ObjectDataDescriptor, ObjectDataRecord, ViewRepSourceManager, ViewRepStandard,
                 ViewRepOrientationDefinition, ViewRepOrientation, ViewRepSectionDefinition,
                 ViewRepModelSpaceViewSelectionSet, ViewRepModelSpaceSource, ViewRep>;
inline bool data_table_has_native_codec(std::int32_t value_type) {
    return value_type >= 1 && value_type <= 5;
}
struct ClassObject {
    std::uint64_t handle = 0;
    std::uint64_t owner = 0;
    std::vector<std::uint64_t> reactors;
    std::optional<std::uint64_t> xdictionary_handle;
    std::string dxf_name;
    std::string cpp_class_name;
    ClassObjectData data;
    std::optional<DxfVersion> source_version;
    void visit_handles(const std::function<void(std::uint64_t &)> &visit);
    std::vector<std::uint64_t> referenced_handles() const;
    void remap_handles(const std::unordered_map<std::uint64_t, std::uint64_t> &map);
};

inline std::string class_object_ascii_upper(std::string_view name) {
    std::string out(name);
    for (char &c : out)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return out;
}
inline bool is_class_object_name(std::string_view name) {
    auto n = class_object_ascii_upper(name);
    return n == "RENDERSETTINGS" || n == "MENTALRAYRENDERSETTINGS" || n == "RAPIDRTRENDERSETTINGS" ||
           n == "SECTION_MANAGER" || n == "GRADIENT_BACKGROUND" || n == "GROUND_PLANE_BACKGROUND" ||
           n == "RAPIDRTRENDERENVIRONMENT" || n == "IBL_BACKGROUND" || n == "IMAGE_BACKGROUND" ||
           n == "SKYLIGHT_BACKGROUND" || n == "SOLID_BACKGROUND" || n == "RENDERENTRY" ||
           n == "RENDERENVIRONMENT" || n == "RENDERGLOBAL" || n == "LIGHTLIST" || n == "SUN" ||
           n == "ACDBMOTIONPATH" || n == "MOTIONPATH" || n == "ACDBCURVEPATH" || n == "CURVEPATH" ||
           n == "ACDBPOINTPATH" || n == "POINTPATH" || n == "TVDEVICEPROPERTIES" || n == "LAYERFILTER" ||
           n == "PARTIAL_VIEWING_INDEX" || n == "SECTION_SETTINGS" || n == "SUNSTUDY" || n == "DATATABLE" ||
           n == "ACDBDATATABLE" || n == "GEOMAPIMAGE" || n == "ACDBDETAILVIEWSTYLE" || n == "DETAILVIEWSTYLE" ||
           n == "ACDBSECTIONVIEWSTYLE" || n == "SECTIONVIEWSTYLE" || n == "DATALINK" ||
           n == "IRD_DSC_DICT" || n == "IRD_DSC_RECORD" || n == "IRD_OBJ_RECORD" ||
           n == "ACDBVIEWREPSOURCEMGR" || n == "ACDBVIEWREPSTANDARD" ||
           n == "ACDBVIEWREPORIENTATIONDEF" || n == "ACDBVIEWREPORIENTATION" ||
           n == "ACDBVIEWREPSECTIONDEFINITION" || n == "ACDBSYMODELSPACEVIEWSELSET" ||
           n == "ACDBVIEWREP" || n == "ACDBVIEWREPMODELSPACESOURCE";
}
inline std::string class_object_dxf_name(const ClassObjectData &data) {
    if (std::holds_alternative<RenderSettings>(data))
        return "RENDERSETTINGS";
    if (std::holds_alternative<MentalRayRenderSettings>(data))
        return "MENTALRAYRENDERSETTINGS";
    if (std::holds_alternative<RapidRtRenderSettings>(data))
        return "RAPIDRTRENDERSETTINGS";
    if (std::holds_alternative<SectionManager>(data))
        return "SECTION_MANAGER";
    if (std::holds_alternative<GradientBackground>(data))
        return "GRADIENT_BACKGROUND";
    if (std::holds_alternative<GroundPlaneBackground>(data))
        return "GROUND_PLANE_BACKGROUND";
    if (std::holds_alternative<IblBackground>(data))
        return "RAPIDRTRENDERENVIRONMENT";
    if (std::holds_alternative<ImageBackground>(data))
        return "IMAGE_BACKGROUND";
    if (std::holds_alternative<SkyLightBackground>(data))
        return "SKYLIGHT_BACKGROUND";
    if (std::holds_alternative<SolidBackground>(data))
        return "SOLID_BACKGROUND";
    if (std::holds_alternative<RenderEntry>(data))
        return "RENDERENTRY";
    if (std::holds_alternative<RenderEnvironment>(data))
        return "RENDERENVIRONMENT";
    if (std::holds_alternative<RenderGlobal>(data))
        return "RENDERGLOBAL";
    if (std::holds_alternative<LightList>(data))
        return "LIGHTLIST";
    if (std::holds_alternative<Sun>(data))
        return "SUN";
    if (std::holds_alternative<MotionPath>(data))
        return "ACDBMOTIONPATH";
    if (std::holds_alternative<CurvePath>(data))
        return "ACDBCURVEPATH";
    if (std::holds_alternative<PointPath>(data))
        return "ACDBPOINTPATH";
    if (std::holds_alternative<TvDeviceProperties>(data))
        return "TVDEVICEPROPERTIES";
    if (std::holds_alternative<LayerFilter>(data))
        return "LAYERFILTER";
    if (std::holds_alternative<PartialViewingIndex>(data))
        return "PARTIAL_VIEWING_INDEX";
    if (std::holds_alternative<SectionSettings>(data))
        return "SECTION_SETTINGS";
    if (std::holds_alternative<SunStudy>(data))
        return "SUNSTUDY";
    if (std::holds_alternative<DataTable>(data))
        return "DATATABLE";
    if (std::holds_alternative<GeoMapImage>(data))
        return "GEOMAPIMAGE";
    if (std::holds_alternative<DetailViewStyle>(data))
        return "ACDBDETAILVIEWSTYLE";
    if (std::holds_alternative<SectionViewStyle>(data))
        return "ACDBSECTIONVIEWSTYLE";
    if (std::holds_alternative<DataLink>(data))
        return "DATALINK";
    if (std::holds_alternative<ObjectDataDictionary>(data))
        return "IRD_DSC_DICT";
    if (std::holds_alternative<ObjectDataDescriptor>(data))
        return "IRD_DSC_RECORD";
    if (std::holds_alternative<ObjectDataRecord>(data))
        return "IRD_OBJ_RECORD";
    if (std::holds_alternative<ViewRepSourceManager>(data))
        return "ACDBVIEWREPSOURCEMGR";
    if (std::holds_alternative<ViewRepStandard>(data))
        return "ACDBVIEWREPSTANDARD";
    if (std::holds_alternative<ViewRepOrientationDefinition>(data))
        return "ACDBVIEWREPORIENTATIONDEF";
    if (std::holds_alternative<ViewRepOrientation>(data))
        return "ACDBVIEWREPORIENTATION";
    if (std::holds_alternative<ViewRepSectionDefinition>(data))
        return "ACDBVIEWREPSECTIONDEFINITION";
    if (std::holds_alternative<ViewRepModelSpaceViewSelectionSet>(data))
        return "ACDBSYMODELSPACEVIEWSELSET";
    if (std::holds_alternative<ViewRepModelSpaceSource>(data))
        return "ACDBVIEWREPMODELSPACESOURCE";
    return "ACDBVIEWREP";
}
inline std::string class_object_cpp_name(std::string_view dxf_name) {
    auto n = class_object_ascii_upper(dxf_name);
    if (n == "RENDERSETTINGS")
        return "AcDbRenderSettings";
    if (n == "MENTALRAYRENDERSETTINGS")
        return "AcDbMentalRayRenderSettings";
    if (n == "RAPIDRTRENDERSETTINGS")
        return "AcDbRapidRTRenderSettings";
    if (n == "SECTION_MANAGER")
        return "AcDbSectionManager";
    if (n == "GRADIENT_BACKGROUND")
        return "AcDbGradientBackground";
    if (n == "GROUND_PLANE_BACKGROUND")
        return "AcDbGroundPlaneBackground";
    if (n == "RAPIDRTRENDERENVIRONMENT" || n == "IBL_BACKGROUND")
        return "AcDbIBLBackground";
    if (n == "IMAGE_BACKGROUND")
        return "AcDbImageBackground";
    if (n == "SKYLIGHT_BACKGROUND")
        return "AcDbSkyBackground";
    if (n == "SOLID_BACKGROUND")
        return "AcDbSolidBackground";
    if (n == "RENDERENTRY")
        return "AcDbRenderEntry";
    if (n == "RENDERENVIRONMENT")
        return "AcDbRenderEnvironment";
    if (n == "RENDERGLOBAL")
        return "AcDbRenderGlobal";
    if (n == "LIGHTLIST")
        return "AcDbLightList";
    if (n == "SUN")
        return "AcDbSun";
    if (n == "ACDBMOTIONPATH" || n == "MOTIONPATH")
        return "AcDbMotionPath";
    if (n == "ACDBCURVEPATH" || n == "CURVEPATH")
        return "AcDbCurvePath";
    if (n == "ACDBPOINTPATH" || n == "POINTPATH")
        return "AcDbPointPath";
    if (n == "TVDEVICEPROPERTIES")
        return "AcDbTvDeviceProperties";
    if (n == "LAYERFILTER")
        return "AcDbLayerFilter";
    if (n == "PARTIAL_VIEWING_INDEX")
        return "OdDbPartialViewingIndex";
    if (n == "SECTION_SETTINGS")
        return "AcDbSectionSettings";
    if (n == "SUNSTUDY")
        return "AcDbSunStudy";
    if (n == "DATATABLE" || n == "ACDBDATATABLE")
        return "AcDbDataTable";
    if (n == "GEOMAPIMAGE")
        return "AcDbGeomapImage";
    if (n == "ACDBDETAILVIEWSTYLE" || n == "DETAILVIEWSTYLE")
        return "AcDbDetailViewStyle";
    if (n == "ACDBSECTIONVIEWSTYLE" || n == "SECTIONVIEWSTYLE")
        return "AcDbSectionViewStyle";
    if (n == "DATALINK")
        return "AcDbDataLink";
    if (n == "IRD_DSC_DICT")
        return "CIrdDscDict";
    if (n == "IRD_DSC_RECORD")
        return "CIrdDscRecord";
    if (n == "IRD_OBJ_RECORD")
        return "CIrdObjRecord";
    if (n == "ACDBVIEWREPSOURCEMGR")
        return "AcDbViewRepSourceMgr";
    if (n == "ACDBVIEWREPSTANDARD")
        return "AcDbViewRepStandard";
    if (n == "ACDBVIEWREPORIENTATIONDEF")
        return "AcDbViewRepOrientationDef";
    if (n == "ACDBVIEWREPORIENTATION")
        return "AcDbViewRepOrientation";
    if (n == "ACDBVIEWREPSECTIONDEFINITION")
        return "AcDbViewRepSectionDefinition";
    if (n == "ACDBSYMODELSPACEVIEWSELSET")
        return "AcDbViewRepModelSpaceViewSelSet";
    if (n == "ACDBVIEWREP")
        return "AcDbViewRep";
    if (n == "ACDBVIEWREPMODELSPACESOURCE")
        return "AcDbViewRepModelSpaceSource";
    return {};
}
inline std::vector<std::string> class_object_subclasses(std::string_view dxf_name) {
    auto n = class_object_ascii_upper(dxf_name);
    if (n == "MENTALRAYRENDERSETTINGS")
        return {"AcDbRenderSettings", "AcDbMentalRayRenderSettings"};
    if (n == "RAPIDRTRENDERSETTINGS")
        return {"AcDbRenderSettings", "AcDbRapidRTRenderSettings"};
    if (n == "ACDBDETAILVIEWSTYLE" || n == "DETAILVIEWSTYLE")
        return {"AcDbModelDocViewStyle", "AcDbDetailViewStyle"};
    if (n == "ACDBSECTIONVIEWSTYLE" || n == "SECTIONVIEWSTYLE")
        return {"AcDbModelDocViewStyle", "AcDbSectionViewStyle"};
    if (n == "IRD_DSC_DICT")
        return {"AcDbDictionary", "CIrdDscDict"};
    if (n == "IRD_DSC_RECORD")
        return {"CIrdBaseRecord", "CIrdDscRecord"};
    if (n == "IRD_OBJ_RECORD")
        return {"CIrdBaseRecord", "CIrdObjRecord"};
    if (n == "ACDBVIEWREPMODELSPACESOURCE")
        return {"AcDbViewRepSource", "AcDbViewRepModelSpaceSource"};
    if (n == "ACDBVIEWREPSECTIONDEFINITION")
        return {"AcDbViewRepCutDefinition", "AcDbViewRepSectionDefinition"};
    auto cpp = class_object_cpp_name(n);
    if (cpp.empty())
        return {};
    return {cpp};
}
inline bool class_object_name_matches(std::string_view name, const ClassObjectData &data) {
    auto n = class_object_ascii_upper(name);
    if (n == class_object_dxf_name(data))
        return true;
    if (std::holds_alternative<IblBackground>(data) && n == "IBL_BACKGROUND")
        return true;
    if (std::holds_alternative<MotionPath>(data) && n == "MOTIONPATH")
        return true;
    if (std::holds_alternative<CurvePath>(data) && n == "CURVEPATH")
        return true;
    if (std::holds_alternative<PointPath>(data) && n == "POINTPATH")
        return true;
    if (std::holds_alternative<DataTable>(data) && n == "ACDBDATATABLE")
        return true;
    if (std::holds_alternative<DetailViewStyle>(data) && n == "DETAILVIEWSTYLE")
        return true;
    if (std::holds_alternative<SectionViewStyle>(data) && n == "SECTIONVIEWSTYLE")
        return true;
    return false;
}
inline void ClassObject::visit_handles(const std::function<void(std::uint64_t &)> &visit) {
    visit(owner);
    for (auto &handle : reactors)
        visit(handle);
    if (xdictionary_handle)
        visit(*xdictionary_handle);
    if (auto *manager = std::get_if<SectionManager>(&data))
        for (auto &section : manager->sections)
            visit(section);
    else if (auto *lights = std::get_if<LightList>(&data))
        for (auto &light : lights->lights)
            visit(light.handle);
    else if (auto *ibl = std::get_if<IblBackground>(&data))
        visit(ibl->secondary_background);
    else if (auto *sky = std::get_if<SkyLightBackground>(&data))
        visit(sky->sun);
    else if (auto *path = std::get_if<MotionPath>(&data)) {
        visit(path->camera_path);
        visit(path->target_path);
        visit(path->view);
    } else if (auto *path = std::get_if<CurvePath>(&data))
        visit(path->entity);
    else if (auto *index = std::get_if<PartialViewingIndex>(&data))
        for (auto &entry : index->entries)
            visit(entry.object);
    else if (auto *settings = std::get_if<SectionSettings>(&data))
        for (auto &type : settings->types) {
            for (auto &source : type.sources)
                visit(source);
            visit(type.destination_block);
        }
    else if (auto *study = std::get_if<SunStudy>(&data)) {
        visit(study->page_setup_wizard);
        visit(study->view);
        visit(study->visual_style);
        visit(study->text_style);
    } else if (auto *table = std::get_if<DataTable>(&data)) {
        for (auto &column : table->columns)
            if (column.value_type == static_cast<std::int32_t>(DataTableCellType::ObjectId))
                for (auto &row : column.rows)
                    visit(row.handle);
    } else if (auto *style = std::get_if<DetailViewStyle>(&data)) {
        visit(style->identifier_style);
        visit(style->arrow_symbol);
        visit(style->boundary_linetype);
        visit(style->view_label_text_style);
        visit(style->connection_linetype);
        visit(style->border_linetype);
    } else if (auto *style = std::get_if<SectionViewStyle>(&data)) {
        visit(style->identifier_style);
        visit(style->arrow_start_symbol);
        visit(style->arrow_end_symbol);
        visit(style->plane_linetype);
        visit(style->bend_linetype);
        visit(style->view_label_text_style);
    } else if (auto *link = std::get_if<DataLink>(&data)) {
        for (auto &item : link->custom_data)
            visit(item.target);
        for (auto &item : link->data_map)
            visit(item.value.handle);
        visit(link->hard_owner);
    } else if (auto *dictionary = std::get_if<ObjectDataDictionary>(&data)) {
        for (auto &entry : dictionary->entries)
            visit(entry.second);
    } else if (auto *record = std::get_if<ObjectDataRecord>(&data))
        visit(record->table);
    else if (auto *manager = std::get_if<ViewRepSourceManager>(&data))
        visit(manager->source);
    else if (auto *set = std::get_if<ViewRepModelSpaceViewSelectionSet>(&data))
        for (auto &handle : set->entities)
            visit(handle);
    else if (auto *source = std::get_if<ViewRepModelSpaceSource>(&data)) {
        visit(source->model);
        for (auto &handle : source->references)
            visit(handle);
        visit(source->orientation);
    } else if (auto *view = std::get_if<ViewRep>(&data)) {
        for (auto &sketch : view->sketches)
            for (auto &reference : sketch.references)
                visit(reference.object);
        for (auto &handle : view->related_objects)
            visit(handle);
        visit(view->source_manager);
        for (auto &handle : view->owned_objects)
            visit(handle);
        for (auto &handle : view->optional_objects)
            visit(handle);
        visit(view->orientation);
        for (auto &handle : view->linked_views)
            visit(handle);
        for (auto &path : view->section_sketches)
            for (auto &handle : path.objects)
                visit(handle);
        if (view->action)
            visit(*view->action);
        visit(view->parent);
        if (view->block_path)
            for (auto &entry : view->block_path->entries)
                visit(entry.object);
        visit(view->style);
    }
}
inline std::vector<std::uint64_t> ClassObject::referenced_handles() const {
    std::vector<std::uint64_t> handles;
    auto copy = *this;
    copy.visit_handles([&](std::uint64_t &handle) {
        if (handle)
            handles.push_back(handle);
    });
    return handles;
}
inline void ClassObject::remap_handles(const std::unordered_map<std::uint64_t, std::uint64_t> &map) {
    visit_handles([&](std::uint64_t &handle) {
        auto found = map.find(handle);
        if (found != map.end())
            handle = found->second;
    });
}
} // namespace libopencad
