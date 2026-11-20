// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <libopencad/dxf_version.hpp>
#include <libopencad/vector.hpp>
#include <string>
#include <variant>
#include <vector>
namespace libopencad {
struct DwgLineGeometry {
    Point3 start, end;
};
struct DwgCircleGeometry {
    Point3 center;
    double radius;
};
struct DwgArcGeometry {
    Point3 center;
    double radius, start_angle, end_angle;
};
struct DwgTextGeometry {
    std::string value; // UTF-8; encoded in the target release
    Point3 insertion;
    double height = 1;
};
struct DwgPrimitive {
    std::variant<DwgLineGeometry, DwgCircleGeometry, DwgArcGeometry, DwgTextGeometry> geometry;
    bool paper_space = false;
    double thickness = 0;
    Vector3 normal = Vector3::UNIT_Z;
};
// Create a native drawing on layer 0 using the release's standard tables and objects.
std::string create_dwg(DxfVersion version, const std::vector<DwgPrimitive> &entities);
} // namespace libopencad
