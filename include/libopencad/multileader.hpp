// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "color.hpp"
#include "dxf_raw_field.hpp"
#include "vector.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace libopencad {
// Unknown fields are retained within their original nesting level. Values use
// decoded UTF-8 strings, decimal numbers, hexadecimal handles/binary data.
using MultiLeaderRawField = DxfRawField;
struct MultiLeaderBreakPair {
    Point3 start{}, end{};
};
struct MultiLeaderBreak {
    std::int32_t segment_index = 0;
    std::vector<MultiLeaderBreakPair> points;
};
struct MultiLeaderArrowOverride {
    std::int32_t index = 0;
    std::uint64_t arrowhead_handle = 0;
};
struct MultiLeaderBlockAttribute {
    std::uint64_t definition_handle = 0;
    std::int16_t index = 0;
    double width = 0;
    std::string text;
};
struct MultiLeaderLine {
    std::int32_t index = 0;
    std::int16_t path_type = 1;
    Color line_color = Color::by_block();
    std::uint64_t line_type_handle = 0;
    std::int32_t line_weight = -1;
    double arrowhead_size = .18;
    std::uint64_t arrowhead_handle = 0;
    std::int32_t override_flags = 0;
    std::vector<Point3> points;
    std::vector<MultiLeaderBreak> breaks;
    std::vector<MultiLeaderRawField> extra_fields;
};
struct MultiLeaderRoot {
    bool content_valid = true;
    bool unknown = true;
    Point3 connection_point = {};
    Point3 direction = {1, 0, 0};
    std::int32_t leader_index = 0;
    double landing_distance = .36;
    std::int16_t text_attachment_direction = 0;
    std::vector<MultiLeaderBreakPair> break_points;
    std::vector<MultiLeaderLine> lines;
    std::vector<MultiLeaderRawField> extra_fields;
};
struct MultiLeaderContext {
    double scale_factor = 1;
    Point3 content_base_point = {};
    double text_height = .18;
    double arrowhead_size = .18;
    double landing_gap = .09;
    std::int16_t text_left_attachment = 2;
    std::int16_t text_right_attachment = 2;
    std::int16_t text_alignment = 0;
    std::int16_t block_connection_type = 0;
    bool has_text_contents = false;
    std::string text_string = {};
    Point3 text_normal = {0, 0, 1};
    std::uint64_t text_style_handle = 0;
    Point3 text_location = {};
    Point3 text_direction = {1, 0, 0};
    double text_rotation = 0;
    double text_width = 0;
    double text_boundary_height = 0;
    double line_spacing_factor = 1;
    std::int16_t line_spacing_style = 1;
    Color text_color = Color::by_block();
    std::int16_t text_attachment_point = 2;
    std::int16_t text_flow_direction = 1;
    Color background_fill_color = Color::by_block();
    double background_scale_factor = 1.5;
    std::int32_t background_transparency = 0;
    bool background_fill_enabled = false;
    bool background_mask_fill_on = false;
    std::int32_t column_type = 0;
    bool text_height_automatic = false;
    double column_width = 0;
    double column_gutter = 0;
    bool column_flow_reversed = false;
    bool word_break = true;
    bool has_block_contents = false;
    std::uint64_t block_content_handle = 0;
    Point3 block_content_normal = {0, 0, 1};
    Point3 block_content_location = {};
    Point3 block_content_scale = {1, 1, 1};
    double block_rotation = 0;
    Color block_content_color = Color::by_block();
    Point3 base_point = {};
    Point3 base_direction = {1, 0, 0};
    Point3 base_vertical = {0, 1, 0};
    bool normal_reversed = false;
    std::int16_t text_bottom_attachment = 9;
    std::int16_t text_top_attachment = 9;
    std::vector<double> column_sizes;
    std::array<double, 16> transform_matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::vector<MultiLeaderRoot> leader_roots;
    std::vector<MultiLeaderRawField> extra_fields;
};
struct MultiLeaderDefinition {
    std::int32_t dwg_version = 2;
    std::uint64_t style_handle = 0;
    std::int32_t property_override_flags = 0;
    std::int16_t path_type = 1;
    Color line_color = Color::by_block();
    std::uint64_t line_type_handle = 0;
    std::int32_t line_weight = -1;
    bool enable_landing = true;
    bool enable_dogleg = true;
    double dogleg_length = .36;
    std::uint64_t arrowhead_handle = 0;
    double arrowhead_size = .18;
    std::int16_t content_type = 2;
    std::uint64_t text_style_handle = 0;
    std::int16_t text_left_attachment = 2;
    std::int16_t text_right_attachment = 2;
    std::int16_t text_angle_type = 1;
    std::int16_t text_alignment = 0;
    Color text_color = Color::by_block();
    bool text_frame = false;
    std::uint64_t block_content_handle = 0;
    Color block_content_color = Color::by_block();
    Point3 block_scale = {1, 1, 1};
    double block_rotation = 0;
    double scale_factor = 1;
    std::int16_t block_connection_type = 0;
    bool enable_annotation_scale = false;
    bool text_direction_negative = false;
    std::int32_t text_align_in_ipe = 0;
    std::int16_t text_attachment_point = 2;
    std::int16_t text_attachment_direction = 0;
    std::int16_t text_bottom_attachment = 9;
    std::int16_t text_top_attachment = 9;
    bool extend_leader_to_text = false;
    MultiLeaderContext context;
    std::vector<MultiLeaderArrowOverride> arrowhead_overrides;
    std::vector<MultiLeaderBlockAttribute> block_attributes;
    std::vector<MultiLeaderRawField> extra_fields;
    double text_height = .18; // In-memory/DWG field; DXF stores context.text_height only.
};
struct MultiLeaderStyleDefinition {
    std::int32_t class_version = 2;
    std::int16_t content_type = 2;
    std::int16_t multileader_draw_order = 0;
    std::int16_t leader_draw_order = 0;
    std::int32_t max_leader_points = 2;
    double first_segment_angle = 0;
    double second_segment_angle = 0;
    std::int16_t path_type = 1;
    Color line_color = Color::by_block();
    std::uint64_t line_type_handle = 0;
    std::int32_t line_weight = -2;
    bool enable_landing = true;
    double landing_gap = .09;
    bool enable_dogleg = true;
    double landing_distance = .36;
    std::string description = {};
    std::uint64_t arrowhead_handle = 0;
    double arrowhead_size = .18;
    std::string default_text = {};
    std::uint64_t text_style_handle = 0;
    std::int16_t text_left_attachment = 1;
    std::int16_t text_right_attachment = 1;
    std::int16_t text_angle_type = 1;
    std::int16_t text_alignment = 0;
    Color text_color = Color::by_block();
    double text_height = .18;
    bool text_frame = false;
    bool text_always_left = false;
    double align_space = 0;
    std::uint64_t block_content_handle = 0;
    Color block_content_color = Color::by_block();
    double block_content_scale_x = 1;
    double block_content_scale_y = 1;
    double block_content_scale_z = 1;
    bool enable_block_scale = false;
    double block_content_rotation = 0;
    bool enable_block_rotation = false;
    std::int16_t block_content_connection = 0;
    double scale_factor = 1;
    bool property_changed = false;
    bool is_annotative = false;
    double break_gap_size = .125;
    std::int16_t text_attachment_direction = 0;
    std::int16_t text_bottom_attachment = 4;
    std::int16_t text_top_attachment = 0;
    bool unknown_flag_298 = false;

    std::vector<MultiLeaderRawField> extra_fields;
};
} // namespace libopencad
