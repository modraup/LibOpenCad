// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "dxf_raw_field.hpp"
#include "hatch.hpp"
#include "multileader.hpp"
#include <optional>
namespace libopencad {
struct AnnotationScaleContext {};
struct BlockReferenceContext {
    double rotation = 0;
    Point3 insertion{}, scale_factor{1, 1, 1};
};
struct TextContext {
    std::int16_t horizontal_mode = 0;
    double rotation = 0;
    Vector2 insertion{}, alignment{};
};
struct ContextColumns {
    std::int32_t count = 0;
    double width = 0, gutter = 0;
    bool auto_height = false, reversed = false;
    std::vector<double> heights;
};
struct MTextContext {
    std::int32_t attachment = 1, column_type = 0;
    Point3 x_axis{1, 0, 0}, insertion{};
    double width = 0, height = 0, extents_width = 0, extents_height = 0;
    std::optional<ContextColumns> columns;
};
struct DimensionContext {
    enum class Kind { Aligned, Angular, Diametric, Radial, RadialLarge, Ordinate };
    Kind kind = Kind::Aligned;
    std::string block_name;
    Vector2 text_location{};
    double text_rotation = 0;
    bool reserved = false, default_text_location = false, dimtofl = false, dimosxd = false, dimatfit = false,
         dimtix = false, dimtmove = false, has_arrow2 = false, flip_arrow2 = false, flip_arrow1 = false;
    std::uint8_t override_code = 0;
    // First: 11 (12 for RadialLarge); second: 12 (13 for RadialLarge).
    Point3 first{}, second{};
};
struct EmbeddedMTextContext {
    std::int16_t version = 3;
    bool is_default = false;
    std::uint64_t scale = 0;
    MTextContext text;
};
struct MTextAttributeContext {
    TextContext placement;
    bool enabled = false;
    std::optional<EmbeddedMTextContext> embedded;
};
struct LeaderContext {
    std::vector<Point3> points;
    Point3 x_direction{1, 0, 0}, insertion_offset{}, endpoint_projection{};
    bool annotation_enabled = false;
};
struct FeatureControlFrameContext {
    Point3 location{}, horizontal_direction{1, 0, 0};
};
struct HatchLoopContext {
    std::int32_t type = 0;
    bool supports_context = false;
    std::optional<HatchPath> boundary;
};
struct HatchScaleContext {
    std::vector<HatchPatternLine> pattern_lines;
    double scale = 1;
    Point3 base{};
    std::vector<HatchLoopContext> loops;
};
struct HatchViewContext {
    HatchScaleContext hatch;
    std::uint64_t view = 0;
    Point3 normal{0, 0, 1};
    double rotation = 0;
    bool evaluate = false;
};
struct OpaqueObjectContext {};
using ObjectContextPayload =
    std::variant<AnnotationScaleContext, BlockReferenceContext, TextContext, MTextContext, DimensionContext,
                 MultiLeaderContext, MTextAttributeContext, LeaderContext, FeatureControlFrameContext,
                 HatchScaleContext, HatchViewContext, OpaqueObjectContext>;
struct ObjectContextDefinition {
    std::int16_t version = 3;
    bool is_default = false;
    std::uint64_t scale = 0;
    ObjectContextPayload payload;
    std::vector<DxfRawField> base_extra, scale_extra, payload_extra;
};
} // namespace libopencad
