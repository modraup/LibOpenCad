// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "color.hpp"
#include "vector.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace libopencad {
struct MLineSegment {
    std::vector<double> parameters, area_fill_parameters;
};
struct MLineVertex {
    Point3 position{}, direction{1, 0, 0}, miter{0, 1, 0};
    std::vector<MLineSegment> segments;
};
struct MLineDefinition {
    std::string style_name = "Standard";
    std::uint64_t style_handle = 0;
    double scale_factor = 1;
    std::int16_t justification = 1; // 0 top, 1 zero, 2 bottom
    std::uint16_t flags = 1;        // has vertices, closed, suppress start/end caps
    std::size_t style_element_count = 2;
    Point3 start_point{}, normal{0, 0, 1};
    std::vector<MLineVertex> vertices;
};
struct MLineStyleElement {
    double offset = 0;
    Color color;
    std::string linetype = "BYLAYER";
};
struct MLineStyleDefinition {
    std::string description;
    std::uint16_t flags = 0;
    Color fill_color;
    double start_angle = 1.5707963267948966, end_angle = 1.5707963267948966;
    std::vector<MLineStyleElement> elements;
};
} // namespace libopencad
