// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "color.hpp"
#include "vector.hpp"
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
namespace libopencad {
struct HatchLineEdge {
    Vector2 start{}, end{};
};
struct HatchArcEdge {
    Vector2 center{};
    double radius = 0, start_angle = 0, end_angle = 0; // Radians; clockwise semantics match Rust.
    bool counter_clockwise = true;
};
struct HatchEllipseEdge {
    Vector2 center{}, major_axis_endpoint{1, 0};
    double minor_axis_ratio = 1, start_angle = 0, end_angle = 6.28318530717958647692;
    // Angles are ellipse parameters, converted to/from DXF circular angles.
    bool counter_clockwise = true;
};
struct HatchSplineEdge {
    std::int32_t degree = 3;
    bool rational = false, periodic = false;
    std::vector<double> knots;
    std::vector<Point3> control_points; // x/y coordinates; z stores weight.
    std::vector<Vector2> fit_points;
    Vector2 start_tangent{}, end_tangent{};
};
struct HatchPolylineEdge {
    std::vector<Point3> vertices;
    bool closed = false;
}; // z stores bulge.
using HatchEdge =
    std::variant<HatchLineEdge, HatchArcEdge, HatchEllipseEdge, HatchSplineEdge, HatchPolylineEdge>;
struct HatchPath {
    std::uint32_t flags = 0; // 1 external, 2 polyline, 4 derived, 8 textbox, 16 outermost.
    std::vector<HatchEdge> edges;
    std::vector<std::uint64_t> boundary_handles;
};
struct HatchPatternLine {
    double angle = 0;
    Vector2 base_point{}, offset{};
    std::vector<double> dash_lengths;
};
struct HatchGradientColor {
    double value = 0;
    Color color;
};
struct HatchGradient {
    bool enabled = false;
    std::int32_t reserved = 0;
    double angle = 0, shift = 0, color_tint = 0; // Gradient angle is already radians in DXF.
    bool single_color = false;
    std::vector<HatchGradientColor> colors;
    std::string name;
};
struct HatchDefinition {
    bool is_mpolygon = false;
    Color mpolygon_hatch_color;
    Vector2 mpolygon_x_direction{};
    double elevation = 0;
    Point3 normal{0, 0, 1};
    std::string pattern_name = "SOLID";
    std::vector<HatchPatternLine> pattern_lines;
    bool solid = true, associative = false, double_pattern = false;
    std::int16_t pattern_type = 1, style = 0;
    double pattern_angle = 0, pattern_scale = 1, pixel_size = 0;
    std::vector<HatchPath> paths;
    std::vector<Vector2> seed_points;
    HatchGradient gradient;
    // Definition lines are stored already transformed, as in Rust and DXF.
    // Direct field assignment remains available for importing stored values.
    void update_pattern(Vector2 base_point, double angle, double scale);
    void set_pattern_angle(double angle);
    void set_pattern_scale(double scale);
};
} // namespace libopencad
