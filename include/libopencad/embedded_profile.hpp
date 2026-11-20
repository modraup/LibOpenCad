// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "dxf_version.hpp"
#include "vector.hpp"
#include <optional>
#include <array>
#include <string>
#include <variant>
#include <vector>
namespace libopencad {
// Type-specific construction geometry; no entity handles or common properties.
// Angles are radians. Circle/arc centres use OCS, matching acadrust entities.
struct ProfilePoint {
    Vector3 location;
    double thickness = 0;
    Vector3 normal{0, 0, 1};
    double x_axis_angle = 0;
};
struct ProfileLine {
    Vector3 start, end;
    Vector3 normal{0, 0, 1};
};
struct ProfileCircle {
    Vector3 center;
    double radius = 0;
    Vector3 normal{0, 0, 1};
};
struct ProfileArc : ProfileCircle {
    double start_angle = 0, end_angle = 0;
};
struct ProfileEllipse {
    Vector3 center, normal{0, 0, 1}, major_axis{1, 0, 0};
    double ratio = 1, start_parameter = 0, end_parameter = 6.283185307179586;
};
struct ProfileRay {
    Vector3 base_point, direction{1, 0, 0};
};
struct ProfileXLine : ProfileRay {};
struct ProfileVertex {
    Vector2 location;
    double bulge = 0, start_width = 0, end_width = 0;
    std::int32_t id = 0;
};
struct ProfilePolyline {
    std::vector<ProfileVertex> vertices;
    double constant_width = 0, elevation = 0, thickness = 0;
    Vector3 normal{0, 0, 1};
    bool closed = false, plinegen = false;
};
struct ProfileSpline {
    std::int32_t degree = 3, knot_parameterization = 0, dwg_flags = 0;
    bool rational = false, closed = false, periodic = false, cv_frame_visible = false;
    double knot_tolerance = 0, control_tolerance = 0, fit_tolerance = 0;
    std::vector<double> knots, weights;
    std::vector<Vector3> control_points, fit_points;
    Vector3 begin_tangent, end_tangent;
};
// Wire-body SAT profiles (native types 15 and 16) are supported by sweep/extrusion.
struct ProfileBody {
    std::int32_t type_code = 15;
    std::string sat;
};
// Inline ACIS geometry and its optional display cache; no external history handle.
struct ProfileAcis {
    bool is_binary = false;
    std::string sat;
    std::vector<std::uint8_t> sab;
};
struct ProfileWire {
    std::uint8_t type = 0;
    std::int32_t selection_marker = 0, color = 256, acis_index = 0;
    std::vector<Vector3> points;
    bool has_transform = false, rotation = false, reflection = false, shear = false;
    Vector3 x_axis{1, 0, 0}, y_axis{0, 1, 0}, z_axis{0, 0, 1}, translation;
    double scale = 1;
};
struct ProfileSilhouette {
    std::uint64_t viewport_id = 0;
    Vector3 target, direction, up;
    bool perspective = false, has_wires = false;
    std::vector<ProfileWire> wires;
};
struct ProfileMaterial {
    std::int32_t array_index = 0, absolute_reference = 0;
    std::uint64_t handle = 0;
};
struct ProfileRevision {
    bool has_guid = false;
    std::uint32_t major = 0, end_marker = 0;
    std::int16_t minor1 = 0, minor2 = 0;
    std::array<std::uint8_t, 8> bytes{};
};
struct ProfileRegion {
    ProfileAcis geometry;
    std::optional<ProfileAcis> extra;
    Vector3 reference_point;
    bool wireframe_present = false, point_present = false, isoline_present = false;
    std::int32_t isolines = 0;
    std::vector<ProfileWire> wires;
    std::vector<ProfileSilhouette> silhouettes;
    std::vector<ProfileMaterial> materials;
    ProfileRevision revision;
};
// Includes unknown types, malformed known bodies and noncanonical ACIS layouts.
// Opaque bodies can be retained/replaced in the same version without reinterpretation.
struct ProfileOpaque {
    std::int32_t type_code = 0;
    std::size_t bit_count = 0;
    std::string bytes;
    DxfVersion source_version = DxfVersion::Unknown;
};
using EmbeddedProfile =
    std::variant<ProfilePoint, ProfileLine, ProfileArc, ProfileCircle, ProfileEllipse, ProfileSpline,
                 ProfilePolyline, ProfileRay, ProfileXLine, ProfileBody, ProfileRegion, ProfileOpaque>;
struct EncodedProfile {
    std::int32_t type_code = 0;
    std::size_t bit_count = 0;
    std::string bytes;
};
// Invalid lengths throw; truncated/unknown body layouts return ProfileOpaque.
std::optional<EmbeddedProfile> decode_embedded_profile(const EncodedProfile &, DxfVersion);
EncodedProfile encode_embedded_profile(const EmbeddedProfile &, DxfVersion);
struct SolidHistoryOperation;
struct HistoryProfileGeometry {
    std::optional<EmbeddedProfile> sweep, path;
    std::vector<EmbeddedProfile> sections, guides;
    // Loft options store their own R2018 path; it overrides the native path when present.
    std::optional<EmbeddedProfile> metadata_path;
};
HistoryProfileGeometry read_history_profiles(const SolidHistoryOperation &, DxfVersion);
// Strong exception guarantee; use update_solid_history_step to persist the operation.
void set_history_profiles(SolidHistoryOperation &, const HistoryProfileGeometry &, DxfVersion);
} // namespace libopencad
