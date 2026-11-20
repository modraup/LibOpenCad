// SPDX-License-Identifier: MPL-2.0
// Port of acadrust entities/surface.rs construction parameters.
#pragma once
#include "vector.hpp"
#include "embedded_profile.hpp"
#include <array>
#include <cstdint>
#include <variant>
namespace libopencad {
// Column-major placement, matching SurfaceData. DXF stores its transpose.
using SurfaceMatrix = std::array<double, 16>;
inline SurfaceMatrix surface_identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
struct SurfaceSweepOptions {
    double draft_angle = 0, draft_start_distance = 0, draft_end_distance = 0, twist_angle = 0;
    double scale_factor = 1, align_angle = 0;
    SurfaceMatrix sweep_entity_transform = surface_identity(), path_entity_transform = surface_identity();
    bool is_solid = false;
    std::int16_t sweep_alignment_flags = 0, path_flags = 0;
    bool align_start = false, bank = false, base_point_set = false;
    bool sweep_entity_transform_computed = false, path_entity_transform_computed = false;
    Vector3 reference_vector{0, 0, 1};
};
struct ExtrudedSurfaceDefinition {
    SurfaceSweepOptions options;
    Vector3 sweep_vector;
    SurfaceMatrix sweep_transform = surface_identity();
};
struct LoftedSurfaceDefinition {
    SurfaceMatrix loft_transform = surface_identity();
    std::int16_t plane_normal_lofting_type = 0;
    double start_draft_angle = 0, end_draft_angle = 0, start_draft_magnitude = 0, end_draft_magnitude = 0;
    bool arc_length_parameterization = false, no_twist = false, align_direction = false,
         simple_surfaces = false;
    bool closed_surfaces = false, solid = false, ruled_surface = false, virtual_guide = false;
};
struct RevolvedSurfaceDefinition {
    Vector3 axis_point, axis_vector{0, 0, 1};
    double revolve_angle = 0, start_angle = 0;
    SurfaceMatrix entity_transform = surface_identity();
    double draft_angle = 0, draft_start_distance = 0, draft_end_distance = 0, twist_angle = 0;
    bool solid = false, close_to_axis = false;
};
struct SweptSurfaceDefinition {
    SurfaceMatrix sweep_transform = surface_identity(), path_transform = surface_identity();
    SurfaceSweepOptions options;
};
struct NurbSurfaceDefinition {
    std::int16_t short_170 = 0;
    bool cv_hull_display = false;
    Vector3 u_vector1, v_vector1, u_vector2, v_vector2;
};
// Stored embedded inputs only. Loft database references remain in the entity's
// XDATA; replacing these inputs does not resolve or rewrite those references.
struct SurfaceProfiles {
    std::optional<EmbeddedProfile> profile, path;
    std::vector<EmbeddedProfile> sections, guides;
    // Two legacy REVOLVEDSURFACE group-90 values when no profile body exists.
    std::int32_t revolve_class_version = 0, revolve_entity_id = 0;
    // Older SWEPTSURFACE prefixes; absent in the current native writer layout.
    std::optional<std::int32_t> swept_legacy_class_version;
};
// Parameters only. Embedded profile records and history remain in the drawing;
// setters preserve those records and do not regenerate the ACIS B-rep.
using SurfaceDefinition =
    std::variant<std::monostate, ExtrudedSurfaceDefinition, LoftedSurfaceDefinition,
                 RevolvedSurfaceDefinition, SweptSurfaceDefinition, NurbSurfaceDefinition>;
} // namespace libopencad
