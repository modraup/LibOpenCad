// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "color.hpp"
#include "dxf_raw_field.hpp"
#include "vector.hpp"
#include <array>
#include <variant>
#include <vector>
namespace libopencad {
using VisualStyleValue = std::variant<std::int32_t, double, bool, Color, std::string>;
struct VisualStyleProperty {
    VisualStyleValue value = std::int32_t{0};
    std::int16_t enabled = 1;
};
struct VisualStyleDefinition {
    std::string description;
    std::int16_t style_type = 0, lighting_model = 2;
    bool internal = false;
    // R2013+ ordered core (28) and extended (30) slots; constructor uses Rust defaults.
    std::vector<VisualStyleProperty> properties;
    std::vector<DxfRawField> extra_fields;
    static VisualStyleDefinition defaults();
};
struct GeoMeshPoint {
    Vector2 source{}, destination{};
};
struct GeoCivilData {
    bool obsolete = false, unknown_flag = false;
    Vector2 reference1{}, reference2{}, zero1{}, zero2{};
    std::int32_t unknown1 = 0, unknown2 = 0;
    double north_degrees = 0, north_radians = 0;
    std::vector<DxfRawField> extra_fields;
};
struct GeoDataDefinition {
    std::int32_t version = 2, horizontal_units = 0, vertical_units = 0, scale_method = 0;
    std::uint64_t host_block = 0;
    std::int16_t coordinate_type = 0;
    Point3 design{}, reference{}, up{};
    Vector2 north{};
    double horizontal_scale = 1, vertical_scale = 1, user_scale = 1, sea_elevation = 0, projection_radius = 0;
    bool sea_correction = false;
    std::string coordinate_system, datum, wkt, rss, observation_from, observation_to, observation_coverage;
    std::vector<GeoMeshPoint> points;
    std::vector<std::array<std::int32_t, 3>> faces;
    GeoCivilData civil;
    std::vector<DxfRawField> extra_fields;
};
using DrawingObjectDefinition = std::variant<VisualStyleDefinition, GeoDataDefinition>;
} // namespace libopencad
