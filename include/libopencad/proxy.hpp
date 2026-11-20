// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "dxf_raw_field.hpp"
#include "vector.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace libopencad {
struct ProxyPayload {
    std::uint32_t bit_count = 0;
    std::vector<std::uint8_t> data;
};
struct ProxyReference {
    int code = 330;
    std::uint64_t handle = 0;
};
struct ProxyDefinition {
    std::int32_t proxy_id = 498, class_id = 0, dwg_version = 32, maintenance_version = 0;
    bool from_dxf = false;
    std::vector<std::uint8_t> graphics;
    ProxyPayload payload, text_payload;
    std::vector<ProxyReference> references;
    std::vector<DxfRawField> dxf_data, extra_fields;
};
struct ClassDefinition {
    std::string name, cpp_name, application = "ObjectDBX Classes";
    std::uint32_t proxy_flags = 0;
    std::int32_t instance_count = 0;
    bool was_zombie = false, is_entity = false;
    std::vector<DxfRawField> extra_fields;
};
struct ProxyUnicodeText {
    Point3 position{}, normal{0, 0, 1}, direction{1, 0, 0};
    double height = 1, width_factor = 1, oblique_angle = 0;
    std::string text;
};
struct ProxyExtents {
    std::array<Point3, 2> points;
};
struct ProxyCircle {
    Point3 center{};
    double radius = 0;
    Point3 normal{0, 0, 1};
};
struct ProxyCircle3 {
    std::array<Point3, 3> points;
};
struct ProxyArc {
    Point3 center{};
    double radius = 0;
    Point3 normal{0, 0, 1}, start_direction{1, 0, 0};
    double sweep = 0;
    std::int32_t arc_type = 0;
};
struct ProxyArc3 {
    std::array<Point3, 3> points;
    std::int32_t arc_type = 0;
};
struct ProxyPolyline {
    enum class Kind { Polyline, Polygon, WithNormal };
    Kind kind = Kind::Polyline;
    std::vector<Point3> vertices;
    Point3 normal{0, 0, 1};
};
struct ProxyPushTransform {
    std::array<double, 16> values;
};
struct ProxyControl {
    enum class Kind {
        ColorIndex,
        LayerIndex,
        LineTypeIndex,
        SelectionMarker,
        Fill,
        EncodedColor,
        LineWeight,
        LineTypeScale,
        Thickness,
        PopTransform,
        Compatibility51Zero
    };
    Kind kind = Kind::PopTransform;
    std::uint32_t word = 0;
    double number = 0;
};
struct ProxyLegacyStyle {
    std::int32_t declared_length = -1, processing_mode = 0;
    double tracking = 1;
    std::array<bool, 5> flags{};
    std::vector<std::uint8_t> font, big_font;
};
struct ProxyLegacyText {
    Point3 position{}, normal{0, 0, 1}, direction{1, 0, 0};
    std::vector<std::uint8_t> text;
    double height = 1, width_factor = 1, oblique_angle = 0;
    std::optional<ProxyLegacyStyle> styling;
};
struct ProxyStyledUnicodeText {
    Point3 position{}, normal{0, 0, 1}, direction{1, 0, 0};
    std::string text;
    std::int32_t declared_length = -1, processing_mode = 0;
    double height = 1, width_factor = 1, oblique_angle = 0, tracking = 1;
    std::array<bool, 5> flags{};
    bool bold = false, italic = false;
    std::uint32_t charset = 0, pitch_family = 0;
    std::string family, font, big_font;
};
using ProxyCommand = std::variant<ProxyExtents, ProxyCircle, ProxyCircle3, ProxyArc, ProxyArc3, ProxyPolyline,
                                  ProxyPushTransform, ProxyControl, ProxyLegacyText, ProxyStyledUnicodeText>;
struct ProxyGraphicRecord {
    std::uint32_t type = 21;
    std::vector<std::uint8_t> data;
    // Type 36 decoded as UTF-8, with its source positions/dimensions.
    ProxyUnicodeText unicode_text() const;
    static ProxyGraphicRecord text(const ProxyUnicodeText &text);
    std::optional<ProxyCommand> command() const;
    static ProxyGraphicRecord from_command(const ProxyCommand &value);
};
struct ProxyGraphics {
    std::vector<ProxyGraphicRecord> records;
    static ProxyGraphics decode(const std::vector<std::uint8_t> &data);
    std::vector<std::uint8_t> encode() const;
};
} // namespace libopencad
