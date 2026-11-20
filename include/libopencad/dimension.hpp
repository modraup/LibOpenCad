// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "vector.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
namespace libopencad {
struct LinearDimension {
    Point3 first_point{}, second_point{};
    double rotation = 0, ext_line_rotation = 0;
};
struct AlignedDimension {
    Point3 first_point{}, second_point{};
    double ext_line_rotation = 0;
};
struct Angular2LineDimension {
    Point3 first_point{}, second_point{}, angle_vertex{}, dimension_arc{};
};
struct DiameterDimension {
    Point3 angle_vertex{};
    double leader_length = 0;
};
struct RadiusDimension {
    Point3 chord_point{};
    double leader_length = 0;
};
struct Angular3PointDimension {
    Point3 first_point{}, second_point{}, angle_vertex{};
};
struct OrdinateDimension {
    Point3 feature_location{}, leader_endpoint{};
    bool is_x = true;
};
struct ArcDimension {
    Point3 first_extension_point{}, second_extension_point{}, center_point{};
    bool partial = false, has_leader = false;
    double start_parameter = 0, end_parameter = 0;
    Point3 first_leader_point{}, second_leader_point{};
};
struct LargeRadialDimension {
    Point3 jog_point{}, override_center{}, chord_point{};
    double jog_angle = 0;
};
using DimensionGeometry =
    std::variant<LinearDimension, AlignedDimension, Angular2LineDimension, DiameterDimension, RadiusDimension,
                 Angular3PointDimension, OrdinateDimension, ArcDimension, LargeRadialDimension>;
struct DimensionDefinition {
    DimensionGeometry geometry;
    // DXF group 10: dimension-line point; centre for radius; opposite chord for diameter.
    Point3 definition_point{}, text_middle_point{}, insertion_point{}, normal{0, 0, 1};
    std::string style_name = "Standard", block_name;
    std::optional<std::string> text_override;
    std::optional<double> actual_measurement; // Null computes the upstream constructor default.
    double text_rotation = 0, horizontal_direction = 0, line_spacing_factor = 1;
    std::int16_t attachment_point = 5, line_spacing_style = 1;
    bool text_user_positioned = false, flip_arrow1 = false, flip_arrow2 = false;
    double insertion_rotation = 0; // In-memory/DWG value; omitted by upstream DXF I/O.
};
} // namespace libopencad
