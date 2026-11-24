// SPDX-License-Identifier: MPL-2.0
// Scalar DXF maps from acadrust multileader section I/O; included by dxf_multileader.inc.
void mleader_read_fields(MLFields &f, MultiLeaderLine &v) {
    v.index = f.i(91, v.index);
    v.path_type = f.e(170, v.path_type, 0, 2);
    v.line_color = f.c(92, v.line_color);
    v.line_type_handle = f.h(340, v.line_type_handle);
    v.line_weight = f.i(171, v.line_weight);
    v.arrowhead_size = f.r(40, v.arrowhead_size);
    v.arrowhead_handle = f.h(341, v.arrowhead_handle);
    v.override_flags = f.i(93, v.override_flags);
}
void mleader_write_fields(MLFields &f, const MultiLeaderLine &v) {
    f.put_i(91, v.index);
    if (v.path_type < 0 || v.path_type > 2)
        throw std::invalid_argument("Invalid MULTILEADER path_type");
    f.put_i(170, v.path_type);
    f.put_c(92, v.line_color);
    f.put_h(340, v.line_type_handle);
    f.put_i(171, v.line_weight);
    f.put_r(40, v.arrowhead_size);
    f.put_h(341, v.arrowhead_handle);
    f.put_i(93, v.override_flags);
}
void mleader_read_fields(MLFields &f, MultiLeaderRoot &v) {
    v.content_valid = f.b(290, v.content_valid);
    v.unknown = f.b(291, v.unknown);
    v.connection_point = f.p(10, v.connection_point);
    v.direction = f.p(11, v.direction);
    v.leader_index = f.i(90, v.leader_index);
    v.landing_distance = f.r(40, v.landing_distance);
    v.text_attachment_direction = f.e(271, v.text_attachment_direction, 0, 1);
}
void mleader_write_fields(MLFields &f, const MultiLeaderRoot &v) {
    f.put_b(290, v.content_valid);
    f.put_b(291, v.unknown);
    f.put_p(10, v.connection_point);
    f.put_p(11, v.direction);
    f.put_i(90, v.leader_index);
    f.put_r(40, v.landing_distance);
    if (v.text_attachment_direction < 0 || v.text_attachment_direction > 1)
        throw std::invalid_argument("Invalid MULTILEADER text_attachment_direction");
    f.put_i(271, v.text_attachment_direction);
}
void mleader_read_fields(MLFields &f, MultiLeaderContext &v) {
    v.scale_factor = f.r(40, v.scale_factor);
    v.content_base_point = f.p(10, v.content_base_point);
    v.text_height = f.r(41, v.text_height);
    v.arrowhead_size = f.r(140, v.arrowhead_size);
    v.landing_gap = f.r(145, v.landing_gap);
    v.text_left_attachment = f.e(174, v.text_left_attachment, 0, 10);
    v.text_right_attachment = f.e(175, v.text_right_attachment, 0, 10);
    v.text_alignment = f.e(176, v.text_alignment, 0, 2);
    v.block_connection_type = f.e(177, v.block_connection_type, 0, 1);
    v.has_text_contents = f.b(290, v.has_text_contents);
    v.text_string = f.s(304, v.text_string);
    v.text_normal = f.p(11, v.text_normal);
    v.text_style_handle = f.h(340, v.text_style_handle);
    v.text_location = f.p(12, v.text_location);
    v.text_direction = f.p(13, v.text_direction);
    v.text_rotation = f.r(42, v.text_rotation);
    v.text_width = f.r(43, v.text_width);
    v.text_boundary_height = f.r(44, v.text_boundary_height);
    v.line_spacing_factor = f.r(45, v.line_spacing_factor);
    v.line_spacing_style = f.e(170, v.line_spacing_style, 1, 2);
    v.text_color = f.c(90, v.text_color);
    v.text_attachment_point = f.e(171, v.text_attachment_point, 1, 9);
    v.text_flow_direction = f.e(172, v.text_flow_direction, 1, 5);
    v.background_fill_color = f.c(91, v.background_fill_color);
    v.background_scale_factor = f.r(141, v.background_scale_factor);
    v.background_transparency = f.i(92, v.background_transparency);
    v.background_fill_enabled = f.b(291, v.background_fill_enabled);
    v.background_mask_fill_on = f.b(292, v.background_mask_fill_on);
    v.column_type = f.i(173, v.column_type);
    v.text_height_automatic = f.b(293, v.text_height_automatic);
    v.column_width = f.r(142, v.column_width);
    v.column_gutter = f.r(143, v.column_gutter);
    v.column_flow_reversed = f.b(294, v.column_flow_reversed);
    v.word_break = f.b(295, v.word_break);
    v.has_block_contents = f.b(296, v.has_block_contents);
    v.block_content_handle = f.h(341, v.block_content_handle);
    v.block_content_normal = f.p(14, v.block_content_normal);
    v.block_content_location = f.p(15, v.block_content_location);
    v.block_content_scale = f.p(16, v.block_content_scale);
    v.block_rotation = f.r(46, v.block_rotation);
    v.block_content_color = f.c(93, v.block_content_color);
    v.base_point = f.p(110, v.base_point);
    v.base_direction = f.p(111, v.base_direction);
    v.base_vertical = f.p(112, v.base_vertical);
    v.normal_reversed = f.b(297, v.normal_reversed);
    v.text_bottom_attachment = f.e(272, v.text_bottom_attachment, 0, 10);
    v.text_top_attachment = f.e(273, v.text_top_attachment, 0, 10);
}
void mleader_write_fields(MLFields &f, const MultiLeaderContext &v) {
    f.put_r(40, v.scale_factor);
    f.put_p(10, v.content_base_point);
    f.put_r(41, v.text_height);
    f.put_r(140, v.arrowhead_size);
    f.put_r(145, v.landing_gap);
    if (v.text_left_attachment < 0 || v.text_left_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_left_attachment");
    f.put_i(174, v.text_left_attachment);
    if (v.text_right_attachment < 0 || v.text_right_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_right_attachment");
    f.put_i(175, v.text_right_attachment);
    if (v.text_alignment < 0 || v.text_alignment > 2)
        throw std::invalid_argument("Invalid MULTILEADER text_alignment");
    f.put_i(176, v.text_alignment);
    if (v.block_connection_type < 0 || v.block_connection_type > 1)
        throw std::invalid_argument("Invalid MULTILEADER block_connection_type");
    f.put_i(177, v.block_connection_type);
    f.put_b(290, v.has_text_contents);
    if (v.has_text_contents) {
        f.put_s(304, v.text_string);
        f.put_p(11, v.text_normal);
        f.put_h(340, v.text_style_handle);
        f.put_p(12, v.text_location);
        f.put_p(13, v.text_direction);
        f.put_r(42, v.text_rotation);
        f.put_r(43, v.text_width);
        f.put_r(44, v.text_boundary_height);
        f.put_r(45, v.line_spacing_factor);
        if (v.line_spacing_style < 1 || v.line_spacing_style > 2)
            throw std::invalid_argument("Invalid MULTILEADER line_spacing_style");
        f.put_i(170, v.line_spacing_style);
        f.put_c(90, v.text_color);
        if (v.text_attachment_point < 1 || v.text_attachment_point > 9)
            throw std::invalid_argument("Invalid MULTILEADER text_attachment_point");
        f.put_i(171, v.text_attachment_point);
        if (v.text_flow_direction < 1 || v.text_flow_direction > 5)
            throw std::invalid_argument("Invalid MULTILEADER text_flow_direction");
        if (v.text_flow_direction != 1 && v.text_flow_direction != 3 && v.text_flow_direction != 5)
            throw std::invalid_argument("Invalid MULTILEADER text flow");
        f.put_i(172, v.text_flow_direction);
        f.put_c(91, v.background_fill_color);
        f.put_r(141, v.background_scale_factor);
        f.put_i(92, v.background_transparency);
        f.put_b(291, v.background_fill_enabled);
        f.put_b(292, v.background_mask_fill_on);
        f.put_i(173, v.column_type);
        f.put_b(293, v.text_height_automatic);
        f.put_r(142, v.column_width);
        f.put_r(143, v.column_gutter);
        f.put_b(294, v.column_flow_reversed);
        f.put_b(295, v.word_break);
        for (auto size : v.column_sizes)
            f.put_r(144, size);
    }
    f.put_b(296, v.has_block_contents);
    if (v.has_block_contents) {
        f.put_h(341, v.block_content_handle);
        f.put_p(14, v.block_content_normal);
        f.put_p(15, v.block_content_location);
        f.put_p(16, v.block_content_scale);
        f.put_r(46, v.block_rotation);
        f.put_c(93, v.block_content_color);
        for (auto n : v.transform_matrix)
            f.put_r(47, n);
    }
    f.put_p(110, v.base_point);
    f.put_p(111, v.base_direction);
    f.put_p(112, v.base_vertical);
    f.put_b(297, v.normal_reversed);
    if (v.text_bottom_attachment < 0 || v.text_bottom_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_bottom_attachment");
    f.put_i(272, v.text_bottom_attachment);
    if (v.text_top_attachment < 0 || v.text_top_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_top_attachment");
    f.put_i(273, v.text_top_attachment);
}
void mleader_read_fields(MLFields &f, MultiLeaderDefinition &v) {
    v.dwg_version = f.i(270, v.dwg_version);
    v.style_handle = f.h(340, v.style_handle);
    v.property_override_flags = f.i(90, v.property_override_flags);
    v.path_type = f.e(170, v.path_type, 0, 2);
    v.line_color = f.c(91, v.line_color);
    v.line_type_handle = f.h(341, v.line_type_handle);
    v.line_weight = f.i(171, v.line_weight);
    v.enable_landing = f.b(290, v.enable_landing);
    v.enable_dogleg = f.b(291, v.enable_dogleg);
    v.dogleg_length = f.r(41, v.dogleg_length);
    v.arrowhead_handle = f.h(342, v.arrowhead_handle);
    v.arrowhead_size = f.r(42, v.arrowhead_size);
    v.content_type = f.e(172, v.content_type, 0, 3);
    v.text_style_handle = f.h(343, v.text_style_handle);
    v.text_left_attachment = f.e(173, v.text_left_attachment, 0, 10);
    v.text_right_attachment = f.e(95, v.text_right_attachment, 0, 10);
    v.text_angle_type = f.e(174, v.text_angle_type, 0, 2);
    v.text_alignment = f.e(175, v.text_alignment, 0, 2);
    v.text_color = f.c(92, v.text_color);
    v.text_frame = f.b(292, v.text_frame);
    v.block_content_handle = f.h(344, v.block_content_handle);
    v.block_content_color = f.c(93, v.block_content_color);
    v.block_scale = f.p(10, v.block_scale);
    v.block_rotation = f.r(43, v.block_rotation);
    v.scale_factor = f.r(45, v.scale_factor);
    v.block_connection_type = f.e(176, v.block_connection_type, 0, 1);
    v.enable_annotation_scale = f.b(293, v.enable_annotation_scale);
    v.text_direction_negative = f.b(294, v.text_direction_negative);
    v.text_align_in_ipe = f.i(178, v.text_align_in_ipe);
    v.text_attachment_point = f.e(179, v.text_attachment_point, 1, 9);
    v.text_attachment_direction = f.e(271, v.text_attachment_direction, 0, 1);
    v.text_bottom_attachment = f.e(272, v.text_bottom_attachment, 0, 10);
    v.text_top_attachment = f.e(273, v.text_top_attachment, 0, 10);
    v.extend_leader_to_text = f.b(295, v.extend_leader_to_text);
}
void mleader_write_fields(MLFields &f, const MultiLeaderDefinition &v) {
    f.put_i(270, v.dwg_version);
    f.put_h(340, v.style_handle);
    f.put_i(90, v.property_override_flags);
    if (v.path_type < 0 || v.path_type > 2)
        throw std::invalid_argument("Invalid MULTILEADER path_type");
    f.put_i(170, v.path_type);
    f.put_c(91, v.line_color);
    f.put_h(341, v.line_type_handle);
    f.put_i(171, v.line_weight);
    f.put_b(290, v.enable_landing);
    f.put_b(291, v.enable_dogleg);
    f.put_r(41, v.dogleg_length);
    f.put_h(342, v.arrowhead_handle);
    f.put_r(42, v.arrowhead_size);
    if (v.content_type < 0 || v.content_type > 3)
        throw std::invalid_argument("Invalid MULTILEADER content_type");
    f.put_i(172, v.content_type);
    f.put_h(343, v.text_style_handle);
    if (v.text_left_attachment < 0 || v.text_left_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_left_attachment");
    f.put_i(173, v.text_left_attachment);
    if (v.text_right_attachment < 0 || v.text_right_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_right_attachment");
    f.put_i(95, v.text_right_attachment);
    if (v.text_angle_type < 0 || v.text_angle_type > 2)
        throw std::invalid_argument("Invalid MULTILEADER text_angle_type");
    f.put_i(174, v.text_angle_type);
    if (v.text_alignment < 0 || v.text_alignment > 2)
        throw std::invalid_argument("Invalid MULTILEADER text_alignment");
    f.put_i(175, v.text_alignment);
    f.put_c(92, v.text_color);
    f.put_b(292, v.text_frame);
    f.put_h(344, v.block_content_handle);
    f.put_c(93, v.block_content_color);
    f.put_p(10, v.block_scale);
    f.put_r(43, v.block_rotation);
    f.put_r(45, v.scale_factor);
    if (v.block_connection_type < 0 || v.block_connection_type > 1)
        throw std::invalid_argument("Invalid MULTILEADER block_connection_type");
    f.put_i(176, v.block_connection_type);
    f.put_b(293, v.enable_annotation_scale);
    f.put_b(294, v.text_direction_negative);
    f.put_i(178, v.text_align_in_ipe);
    if (v.text_attachment_point < 1 || v.text_attachment_point > 9)
        throw std::invalid_argument("Invalid MULTILEADER text_attachment_point");
    f.put_i(179, v.text_attachment_point);
    if (v.text_attachment_direction < 0 || v.text_attachment_direction > 1)
        throw std::invalid_argument("Invalid MULTILEADER text_attachment_direction");
    f.put_i(271, v.text_attachment_direction);
    if (v.text_bottom_attachment < 0 || v.text_bottom_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_bottom_attachment");
    f.put_i(272, v.text_bottom_attachment);
    if (v.text_top_attachment < 0 || v.text_top_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_top_attachment");
    f.put_i(273, v.text_top_attachment);
    f.put_b(295, v.extend_leader_to_text);
}
void mleader_read_fields(MLFields &f, MultiLeaderStyleDefinition &v) {
    v.class_version = f.i(179, v.class_version);
    v.content_type = f.e(170, v.content_type, 0, 3);
    v.multileader_draw_order = f.e(171, v.multileader_draw_order, 0, 1);
    v.leader_draw_order = f.e(172, v.leader_draw_order, 0, 1);
    v.max_leader_points = f.i(90, v.max_leader_points);
    v.first_segment_angle = f.r(40, v.first_segment_angle);
    v.second_segment_angle = f.r(41, v.second_segment_angle);
    v.path_type = f.e(173, v.path_type, 0, 2);
    v.line_color = f.c(91, v.line_color);
    v.line_type_handle = f.h(340, v.line_type_handle);
    v.line_weight = f.i(92, v.line_weight);
    v.enable_landing = f.b(290, v.enable_landing);
    v.landing_gap = f.r(42, v.landing_gap);
    v.enable_dogleg = f.b(291, v.enable_dogleg);
    v.landing_distance = f.r(43, v.landing_distance);
    v.description = f.s(3, v.description);
    v.arrowhead_handle = f.h(341, v.arrowhead_handle);
    v.arrowhead_size = f.r(44, v.arrowhead_size);
    v.default_text = f.s(300, v.default_text);
    v.text_style_handle = f.h(342, v.text_style_handle);
    v.text_left_attachment = f.e(174, v.text_left_attachment, 0, 10);
    v.text_right_attachment = f.e(178, v.text_right_attachment, 0, 10);
    v.text_angle_type = f.e(175, v.text_angle_type, 0, 2);
    v.text_alignment = f.e(176, v.text_alignment, 0, 2);
    v.text_color = f.c(93, v.text_color);
    v.text_height = f.r(45, v.text_height);
    v.text_frame = f.b(292, v.text_frame);
    v.text_always_left = f.b(297, v.text_always_left);
    v.align_space = f.r(46, v.align_space);
    v.block_content_handle = f.h(343, v.block_content_handle);
    v.block_content_color = f.c(94, v.block_content_color);
    v.block_content_scale_x = f.r(47, v.block_content_scale_x);
    v.block_content_scale_y = f.r(49, v.block_content_scale_y);
    v.block_content_scale_z = f.r(140, v.block_content_scale_z);
    v.enable_block_scale = f.b(293, v.enable_block_scale);
    v.block_content_rotation = f.r(141, v.block_content_rotation);
    v.enable_block_rotation = f.b(294, v.enable_block_rotation);
    v.block_content_connection = f.e(177, v.block_content_connection, 0, 1);
    v.scale_factor = f.r(142, v.scale_factor);
    v.property_changed = f.b(295, v.property_changed);
    v.is_annotative = f.b(296, v.is_annotative);
    v.break_gap_size = f.r(143, v.break_gap_size);
    v.text_attachment_direction = f.e(271, v.text_attachment_direction, 0, 1);
    v.text_bottom_attachment = f.e(272, v.text_bottom_attachment, 0, 10);
    v.text_top_attachment = f.e(273, v.text_top_attachment, 0, 10);
    v.unknown_flag_298 = f.b(298, v.unknown_flag_298);
}
void mleader_write_fields(MLFields &f, const MultiLeaderStyleDefinition &v) {
    f.put_i(179, v.class_version);
    if (v.content_type < 0 || v.content_type > 3)
        throw std::invalid_argument("Invalid MULTILEADER content_type");
    f.put_i(170, v.content_type);
    if (v.multileader_draw_order < 0 || v.multileader_draw_order > 1)
        throw std::invalid_argument("Invalid MULTILEADER multileader_draw_order");
    f.put_i(171, v.multileader_draw_order);
    if (v.leader_draw_order < 0 || v.leader_draw_order > 1)
        throw std::invalid_argument("Invalid MULTILEADER leader_draw_order");
    f.put_i(172, v.leader_draw_order);
    f.put_i(90, v.max_leader_points);
    f.put_r(40, v.first_segment_angle);
    f.put_r(41, v.second_segment_angle);
    if (v.path_type < 0 || v.path_type > 2)
        throw std::invalid_argument("Invalid MULTILEADER path_type");
    f.put_i(173, v.path_type);
    f.put_c(91, v.line_color);
    f.put_h(340, v.line_type_handle);
    f.put_i(92, v.line_weight);
    f.put_b(290, v.enable_landing);
    f.put_r(42, v.landing_gap);
    f.put_b(291, v.enable_dogleg);
    f.put_r(43, v.landing_distance);
    f.put_s(3, v.description);
    f.put_h(341, v.arrowhead_handle);
    f.put_r(44, v.arrowhead_size);
    f.put_s(300, v.default_text);
    f.put_h(342, v.text_style_handle);
    if (v.text_left_attachment < 0 || v.text_left_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_left_attachment");
    f.put_i(174, v.text_left_attachment);
    if (v.text_right_attachment < 0 || v.text_right_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_right_attachment");
    f.put_i(178, v.text_right_attachment);
    if (v.text_angle_type < 0 || v.text_angle_type > 2)
        throw std::invalid_argument("Invalid MULTILEADER text_angle_type");
    f.put_i(175, v.text_angle_type);
    if (v.text_alignment < 0 || v.text_alignment > 2)
        throw std::invalid_argument("Invalid MULTILEADER text_alignment");
    f.put_i(176, v.text_alignment);
    f.put_c(93, v.text_color);
    f.put_r(45, v.text_height);
    f.put_b(292, v.text_frame);
    f.put_b(297, v.text_always_left);
    f.put_r(46, v.align_space);
    f.put_h(343, v.block_content_handle);
    f.put_c(94, v.block_content_color);
    f.put_r(47, v.block_content_scale_x);
    f.put_r(49, v.block_content_scale_y);
    f.put_r(140, v.block_content_scale_z);
    f.put_b(293, v.enable_block_scale);
    f.put_r(141, v.block_content_rotation);
    f.put_b(294, v.enable_block_rotation);
    if (v.block_content_connection < 0 || v.block_content_connection > 1)
        throw std::invalid_argument("Invalid MULTILEADER block_content_connection");
    f.put_i(177, v.block_content_connection);
    f.put_r(142, v.scale_factor);
    f.put_b(295, v.property_changed);
    f.put_b(296, v.is_annotative);
    f.put_r(143, v.break_gap_size);
    if (v.text_attachment_direction < 0 || v.text_attachment_direction > 1)
        throw std::invalid_argument("Invalid MULTILEADER text_attachment_direction");
    f.put_i(271, v.text_attachment_direction);
    if (v.text_bottom_attachment < 0 || v.text_bottom_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_bottom_attachment");
    f.put_i(272, v.text_bottom_attachment);
    if (v.text_top_attachment < 0 || v.text_top_attachment > 10)
        throw std::invalid_argument("Invalid MULTILEADER text_top_attachment");
    f.put_i(273, v.text_top_attachment);
    f.put_b(298, v.unknown_flag_298);
}
