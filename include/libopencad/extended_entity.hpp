// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "color.hpp"
#include "dxf_raw_field.hpp"
#include "vector.hpp"
#include <array>
#include <optional>
#include <variant>
#include <vector>
namespace libopencad {
struct OleFrameDefinition {
    std::int16_t version = 2, object_type = 2;
    std::uint8_t lock_aspect = 0;
    std::string source_application;
    Point3 upper_left{1, 1, 0}, lower_right{};
    bool paper_space = false;
    // Complete encoded OLE frame payload, including any storage envelope.
    std::vector<std::uint8_t> payload;
    std::vector<DxfRawField> extra_fields;
};
struct LightPhotometricData {
    bool has_web_file = false;
    std::string web_file;
    std::int16_t intensity_method = 0, lamp_color_type = 0, lamp_color_preset = 0, shape = 0,
                 web_file_type = 0, web_symmetry = 0, target_grip = 0, glyph_display_type = 0;
    double intensity = 0, illuminance_distance = 0, temperature = 0, length = 0, width = 0, radius = 0,
           web_flux = 0;
    Point3 web_rotation{1, 1, 1};
    std::array<double, 5> web_angles{};
    std::vector<DxfRawField> extra_fields;
};
struct LightDefinition {
    std::string name;
    std::int32_t class_version = 1, type = 2, attenuation = 0, shadow_type = 0;
    bool status = true, plot_glyph = false, use_attenuation_limits = false, cast_shadows = false,
         photometric_mode = false;
    Color color = Color::from_index(7);
    double intensity = 1, attenuation_start = 0, attenuation_end = 0, hotspot_angle = 0, falloff_angle = 0;
    Point3 position{}, target{};
    std::int16_t shadow_map_size = 0;
    std::uint8_t shadow_softness = 0;
    std::optional<LightPhotometricData> photometric;
    std::vector<DxfRawField> extra_fields;
};
struct SectionSymbolPoint {
    Point3 position{}, label_offset{};
    double bulge = 0;
    std::string label;
    std::uint8_t flags = 0;
    std::vector<DxfRawField> extra_fields;
};
struct SectionSymbolDefinition {
    std::int16_t view_version = 0, view_flags = 0, version = 0;
    std::int32_t point_count = 0, flags = 0, point_record_count = 0;
    std::uint64_t style = 0, view_rep = 0;
    double scale = 1;
    std::vector<SectionSymbolPoint> points;
    std::vector<DxfRawField> view_extra_fields, extra_fields;
};
struct ViewBorderDefinition {
    std::int16_t version = 0;
    Vector2 min{}, max{}, center{};
    double scale = 1, rotation = 0;
    std::uint64_t viewport = 0, scale_handle = 0;
    std::vector<DxfRawField> extra_fields;
};
struct MarkerTextDefinition {
    Point3 insertion{}, normal{0, 0, 1};
    double height = 1, width = 0;
    std::string text, style = "Standard";
    std::vector<DxfRawField> extra_fields;
};
struct CameraDefinition {
    std::uint64_t view = 0;
    std::vector<DxfRawField> extra_fields;
};
struct SectionObjectDefinition {
    std::int32_t state = 0;
    std::int32_t flags = 0;
    std::string name{};
    Point3 vertical{};
    double top = 0;
    double bottom = 0;
    std::int32_t indicator_alpha = 0;
    std::uint64_t settings = 0;
    Color indicator_color;
    std::vector<Point3> vertices, back_vertices;
    std::vector<DxfRawField> extra_fields;
};
struct ArcAlignedTextDefinition {
    std::string text{};
    std::string font{};
    std::string big_font{};
    std::string style{};
    Point3 center{};
    double radius = 0;
    double x_scale = 1;
    double size = 0;
    double spacing = 0;
    double arc_offset = 0;
    double right_offset = 0;
    double left_offset = 0;
    double start_angle = 0;
    double end_angle = 0;
    bool reverse = false;
    std::int32_t direction = 0;
    std::int32_t alignment = 0;
    std::int32_t position = 0;
    bool bold = false;
    bool italic = false;
    bool underlined = false;
    std::int32_t charset = 0;
    std::int32_t pitch_family = 0;
    bool shx = false;
    std::int32_t color = 0;
    Point3 normal{0, 0, 1};
    bool wizard = false;
    std::uint64_t arc = 0;
    std::vector<DxfRawField> extra_fields;
};
struct RemoteTextDefinition {
    Point3 position{};
    Point3 normal{0, 0, 1};
    double rotation = 0;
    double height = 0;
    std::string style{};
    std::int32_t flags = 0;
    std::string text{};
    std::vector<DxfRawField> extra_fields;
};
struct PositionMarkerDefinition {
    std::int32_t class_version = 0;
    Point3 position{};
    std::string notes{};
    std::int32_t alignment = 0;
    double radius = 0, landing_gap = 0;
    bool text_visible = false, frame_text = false;
    std::optional<MarkerTextDefinition> embedded;
    std::vector<DxfRawField> extra_fields;
};
struct CoordinationModelDefinition {
    std::int32_t flags = 0;
    std::uint64_t definition = 0;
    std::array<double, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    double unit_factor = 1;
    std::vector<DxfRawField> extra_fields;
};
struct OleLegacyDefinition {
    std::int32_t flags = 0;
    std::int32_t mode = 0;
    std::vector<std::uint8_t> payload;
    std::vector<DxfRawField> extra_fields;
};
struct LayoutPrintDefinition {
    std::int32_t flags = 0;
    std::vector<DxfRawField> extra_fields;
};
struct FormatEntityDefinition {
    std::vector<DxfRawField> extra_fields;
};
struct PointCloudDefinition {
    std::int16_t class_version = 0, intensity_scheme = 0;
    Point3 origin{}, extents_min{}, extents_max{}, ucs_origin{}, ucs_x{1, 0, 0}, ucs_y{0, 1, 0},
        ucs_z{0, 0, 1};
    std::string filename, ucs_name;
    std::vector<std::string> source_files;
    // DXF group 92 is signed 32-bit, unlike the DWG 64-bit count.
    std::int32_t point_count = 0;
    std::uint64_t definition = 0, reactor = 0;
    bool show_intensity = false;
    double minimum = 0, maximum = 0, low_threshold = 0, high_threshold = 0;
    std::vector<DxfRawField> extra_fields;
};
struct PointCloudCrop {
    std::int16_t type = 0;
    bool inside = false, inverted = false;
    Point3 plane{}, x_direction{1, 0, 0}, y_direction{0, 1, 0};
    std::vector<Point3> points;
};
struct PointCloudExDefinition {
    std::int16_t class_version = 0, stylization = 0, intensity_out_of_range = 0, elevation_out_of_range = 0;
    Point3 extents_min{}, extents_max{}, ucs_origin{}, ucs_x{1, 0, 0}, ucs_y{0, 1, 0}, ucs_z{0, 0, 1};
    std::uint64_t definition = 0, reactor = 0;
    bool locked = false, show_intensity = false, show_cropping = false, elevation_fixed_range = false,
         intensity_gradient = false, elevation_gradient = false;
    std::string name, intensity_scheme, current_scheme, classification_scheme;
    double elevation_min = 0, elevation_max = 0;
    std::int32_t intensity_min = 0, intensity_max = 0;
    std::vector<PointCloudCrop> crops;
    std::vector<std::string> hidden_scans;
    std::vector<std::int32_t> hidden_regions;
    std::vector<DxfRawField> extra_fields;
};
using ExtendedEntityDefinition =
    std::variant<OleFrameDefinition, LightDefinition, SectionSymbolDefinition, ViewBorderDefinition,
                 CameraDefinition, SectionObjectDefinition, ArcAlignedTextDefinition, RemoteTextDefinition,
                 PositionMarkerDefinition, CoordinationModelDefinition, OleLegacyDefinition,
                 LayoutPrintDefinition, FormatEntityDefinition, PointCloudDefinition, PointCloudExDefinition>;
} // namespace libopencad
