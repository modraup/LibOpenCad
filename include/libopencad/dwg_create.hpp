// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <libopencad/dxf_document.hpp>
#include <libopencad/vector.hpp>
#include <array>
#include <map>
#include <libopencad/color.hpp>
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
struct DwgFaceGeometry {
    std::array<Point3, 4> corners; // Repeat corner 3 for a triangle.
    std::uint8_t invisible_edges = 0;
};
struct DwgPointGeometry { Point3 location; };
struct DwgRayGeometry { Point3 origin; Vector3 direction; bool infinite = false; };
struct DwgPrimitive {
    std::variant<DwgLineGeometry, DwgCircleGeometry, DwgArcGeometry, DwgTextGeometry, DwgFaceGeometry, DwgPointGeometry, DwgRayGeometry, InsertDefinition> geometry;
    bool paper_space = false;
    double thickness = 0;
    Vector3 normal = Vector3::UNIT_Z;
    std::string layer = "0";
    Color color = Color::by_layer();
    std::string block_name; // Empty selects the model or paper space.
    std::string linetype; // Empty / ByLayer, ByBlock, Continuous or a named simple pattern.
    double linetype_scale = 1;
};
// Create a native drawing using the release's standard tables and objects.
std::string create_dwg(DxfVersion version, const std::vector<DwgPrimitive> &entities);
// Named empty-document templates carry drawing settings in addition to the release.
enum class DwgTemplate { R2018Inches };
std::string create_dwg(DwgTemplate drawing_template, const std::vector<DwgPrimitive> &entities);
std::string create_dwg(DwgTemplate drawing_template, const std::vector<DwgPrimitive> &entities,
                       const std::vector<std::string> &block_names);
struct DwgLayerDefinition {
    std::string name;
    Color color = Color::from_index(7);
    bool off = false;
    std::string linetype = "Continuous";
};
std::string create_dwg(DwgTemplate drawing_template, const std::vector<DwgPrimitive> &entities,
                       const std::vector<std::string> &block_names, const std::vector<DwgLayerDefinition> &layers);
// Signed pattern lengths are expressed in drawing units, with zero denoting a dot.
std::string create_dwg(DwgTemplate drawing_template, const std::vector<DwgPrimitive> &entities,
                       const std::vector<std::string> &block_names, const std::vector<DwgLayerDefinition> &layers,
                       const std::map<std::string, std::vector<double>> &linetypes);
} // namespace libopencad
