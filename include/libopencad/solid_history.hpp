// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "dynamic_block.hpp"
#include "embedded_profile.hpp"
#include "color.hpp"
#include "surface.hpp"
#include "dxf_raw_field.hpp"
namespace libopencad {
enum class SolidHistoryKind {
    Box,
    Wedge,
    Sphere,
    Cylinder,
    Cone,
    Pyramid,
    Torus,
    Boolean,
    Brep,
    Fillet,
    Chamfer,
    Sweep,
    Extrusion,
    Loft,
    Revolve
};
struct SolidHistoryRoot {
    std::uint64_t handle = 0, owner = 0, evaluation_graph = 0;
    std::int32_t major = 0, minor = 0, history_node_id = 0;
    bool show_history = false, record_history = false;
};
struct SolidHistoryNode {
    std::uint64_t handle = 0, owner = 0, material = 0;
    SolidHistoryKind kind = SolidHistoryKind::Box;
    std::int32_t major = 0, minor = 0, step_id = 0, eval_node_id = 0, eval_major = 0, eval_minor = 0;
    // DXF does not persist the legacy parent-id field; the evaluation graph
    // carries parent/operand relationships. Value codes retain their DXF types.
    std::int32_t parent_id = -1;
    std::int16_t value_code = -9999;
    BlockEvalValue value;
    SurfaceMatrix transform{};
    Color color;
    std::uint8_t boolean_operation = 0;
    std::int32_t first_operand = 0, second_operand = 0;
    std::int32_t node_id() const { return eval_node_id > 0 ? eval_node_id : step_id; }
};
// Operation-specific DXF parameters. Raw profiles retain their native form;
// embedded_profile.hpp supplies typed reading, construction and editing.
struct HistoryBox {
    double length = 0, width = 0, height = 0;
};
struct HistorySphere {
    double radius = 0;
};
struct HistoryCylinder {
    double height = 0, major_radius = 0, minor_radius = 0, x_radius = 0;
};
struct HistoryCone {
    double height = 0, base_x_radius = 0, base_y_radius = 0, top_radius = 0;
};
struct HistoryPyramid {
    double height = 0;
    std::int32_t sides = 0;
    double radius = 0, top_radius = 0;
};
struct HistoryTorus {
    double major_radius = 0, minor_radius = 0;
};
struct HistoryBoolean {
    std::uint8_t operation = 0;
    std::int32_t first_operand = 0, second_operand = 0;
};
struct HistoryBrep {
    std::string sat;
};
struct HistoryFillet {
    std::int32_t method = 0;
    std::vector<std::int32_t> edges;
    std::vector<double> radii, start_setbacks, end_setbacks;
};
struct HistoryChamfer {
    std::int32_t method = 0;
    double base_distance = 0, other_distance = 0;
    std::vector<std::int32_t> edges;
    std::int32_t base_face = 0;
};
struct HistoryProfiles {
    // Exact type/size/chunk or encrypted SAT fields, without scalar options.
    // Nonempty encoded profiles must stay in their originating DXF version.
    std::vector<DxfRawField> fields;
    std::string source_version;
};
struct HistorySweep {
    Vector3 direction;
    HistoryProfiles profiles;
    double draft_angle = 0, start_draft_distance = 0, end_draft_distance = 0, twist_angle = 0,
           scale_factor = 0, align_angle = 0;
    SurfaceMatrix sweep_entity_transform{}, path_entity_transform{};
    bool has_align_start = false;
    std::uint8_t align_option = 0, miter_option = 0;
    bool align_start = true, bank = false;
    std::array<bool, 3> flags_294_296{};
    Vector3 reference_point;
};
struct HistoryLoftSettings {
    std::int16_t normals = 1, start_continuity = 1, end_continuity = 1;
    double start_draft_angle = 1.5707963267948966, end_draft_angle = 1.5707963267948966;
    double start_magnitude = 0, end_magnitude = 0, start_bulge = .5, end_bulge = .5;
    bool closed = false, periodic = true, surface = false, align_direction = true;
    std::vector<std::int32_t> section_counts;
    // AC1032 encoded path from the versioned metadata, independent of document version.
    std::vector<DxfRawField> path;
};
struct HistoryLoft {
    HistoryProfiles profiles;
    std::int16_t surface_option = 0;
    double start_draft_angle = 1.5707963267948966, end_draft_angle = 1.5707963267948966;
    double start_magnitude = 0, end_magnitude = 0;
    std::array<bool, 8> flags{{false, true, true, true, false, true, false, true}};
    std::optional<HistoryLoftSettings> settings;
};
struct HistoryRevolve {
    Vector3 axis_point, direction;
    double revolve_angle = 0, start_angle = 0, draft_angle = 0, field_44 = 0, field_45 = 0, twist_angle = 0;
    bool flag_290 = false, close_to_axis = false;
    HistoryProfiles profiles;
};
using SolidHistoryParameters =
    std::variant<HistoryBox, HistorySphere, HistoryCylinder, HistoryCone, HistoryPyramid, HistoryTorus,
                 HistoryBoolean, HistoryBrep, HistoryFillet, HistoryChamfer, HistorySweep, HistoryLoft,
                 HistoryRevolve>;
struct SolidHistoryOperation {
    SolidHistoryNode base;
    std::int32_t operation_major = 0, operation_minor = 0;
    SolidHistoryParameters parameters;
    explicit SolidHistoryOperation(SolidHistoryKind kind = SolidHistoryKind::Box);
};
struct SolidHistoryGraph {
    std::uint64_t root = 0;
    std::vector<std::uint64_t> nodes;
    std::optional<std::uint64_t> evaluation_graph;
};
struct SolidHistoryTree {
    SolidHistoryNode operation;
    std::vector<SolidHistoryTree> operands;
};
enum class HistoryReferenceRole {
    Owner,
    Reactor,
    ExtensionDictionary,
    HistoryRoot,
    EvaluationGraph,
    Expression,
    Material,
    EvaluationValue,
    Other
};
struct HistoryReference {
    std::uint64_t source = 0, target = 0;
    HistoryReferenceRole role = HistoryReferenceRole::Other;
    int group_code = 0;
};
} // namespace libopencad
