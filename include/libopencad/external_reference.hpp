// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "dxf_raw_field.hpp"
#include "vector.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace libopencad {
struct RasterImageDefinition {
    Point3 insertion{}, u_vector{1, 0, 0}, v_vector{0, 1, 0};
    Vector2 size{1, 1};
    std::int32_t class_version = 0;
    std::uint64_t definition = 0, reactor = 0;
    std::int16_t flags = 5, clip_type = 1;
    bool clipping = false, clip_inside = false;
    std::uint8_t brightness = 50, contrast = 50, fade = 0;
    std::vector<Vector2> boundary{{-.5, -.5}, {.5, .5}};
    std::vector<DxfRawField> extra_fields;
};
struct ImageDefinition {
    std::string file_name;
    std::uint32_t width = 0, height = 0;
    Vector2 pixel_size{};
    std::int32_t class_version = 0;
    bool loaded = false;
    std::uint8_t resolution_unit = 0;
    std::vector<DxfRawField> extra_fields;
};
struct RasterVariablesDefinition {
    std::int32_t class_version = 0;
    std::int16_t frame = 1, quality = 1, units = 0;
    std::vector<DxfRawField> extra_fields;
};
enum class UnderlayType { Pdf, Dwf, Dgn };
struct UnderlayDefinition {
    std::uint64_t definition = 0;
    Point3 insertion{}, scale{1, 1, 1}, normal{0, 0, 1};
    double rotation = 0; // radians
    std::uint8_t flags = 2, contrast = 100, fade = 0;
    std::vector<Vector2> boundary;
    std::vector<DxfRawField> extra_fields;
};
struct UnderlayFileDefinition {
    std::string file_name, page_name;
    bool unloaded = false;
    std::vector<DxfRawField> extra_fields;
};
} // namespace libopencad
