// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "color.hpp"
#include "dxf_raw_field.hpp"
#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace libopencad {
struct MaterialColor {
    std::uint8_t flag = 0;
    double factor = 1;
    std::optional<std::int32_t> rgb;
};
struct MaterialTexture {
    std::int16_t mode = 0;
    MaterialColor color1, color2;
    std::variant<std::monostate, bool, std::int16_t, double, Color, std::string> value;
    std::vector<std::pair<std::string, MaterialTexture>> children;
    bool table = false, table_end = false;
};
struct MaterialMap {
    double blend_factor = 1;
    std::uint8_t projection = 2, tiling = 1, auto_transform = 1, source = 0;
    std::array<double, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::string file_name;
    std::optional<MaterialTexture> texture;
};
struct AdvancedMaterial {
    double color_bleed_scale = 1, indirect_bump_scale = 1, reflectance_scale = 1, transmittance_scale = 1,
           luminance = 0;
    bool two_sided = false, anonymous = false;
    std::int16_t luminance_mode = 0, global_illumination = 0, final_gather = 0, normal_map_method = 0;
    double normal_map_strength = 0;
    std::vector<DxfRawField> extra_fields;
};
struct MaterialDefinition {
    std::string name, description;
    MaterialColor ambient, diffuse, specular;
    // Diffuse, specular, reflection, opacity, bump, refraction.
    std::array<MaterialMap, 6> maps;
    MaterialMap normal_map;
    double gloss = .5, opacity = 1, refraction_index = 1, translucence = 0, self_illumination = 0,
           reflectivity = 0;
    std::int32_t illumination_model = 0, channel_flags = 127, mode = 0;
    std::optional<AdvancedMaterial> advanced;
    std::vector<DxfRawField> extra_fields;
};
} // namespace libopencad
