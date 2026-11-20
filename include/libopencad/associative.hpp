// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "dxf_version.hpp"
#include "vector.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace libopencad {
std::string associative_canonical_name(std::string_view name);
bool is_associative_object_name(std::string_view name);
std::optional<std::string> associative_cpp_class_name(std::string_view name);

using AssocEvalValue =
    std::variant<std::monostate, double, std::int32_t, std::int16_t, std::uint8_t, std::string,
                 std::uint64_t>;
struct AssocEvalVariant {
    std::int16_t code = 0;
    AssocEvalValue value;
};
struct AssocValueParamVariable {
    AssocEvalVariant value;
    std::uint64_t handle = 0;
};
struct AssocValueParam {
    std::int32_t class_version = 0;
    std::string name;
    std::int32_t unit_type = 0;
    std::vector<AssocValueParamVariable> variables;
    std::uint64_t controlled_object_dependency = 0;
};
struct AssocActionDependency {
    bool is_owned = false;
    std::uint64_t dependency = 0;
};
struct AssocAction {
    std::int16_t class_version = 0;
    std::int32_t geometry_status = 0;
    std::uint64_t owning_network = 0;
    std::uint64_t action_body = 0;
    std::int32_t action_index = 0;
    std::int32_t max_dependency_index = 0;
    std::vector<AssocActionDependency> dependencies;
    std::vector<std::uint64_t> owned_parameters;
    std::vector<AssocValueParam> values;
};
struct AssocDependency {
    std::int16_t class_version = 0;
    std::int32_t status = 0;
    bool is_read_dependency = false;
    bool is_write_dependency = false;
    bool is_attached_to_object = false;
    bool is_delegating_to_owning_action = false;
    std::int32_t order = 0;
    std::uint64_t dependent_on = 0;
    std::optional<std::string> name;
    std::uint64_t read_dependency = 0;
    std::uint64_t node = 0;
    std::uint64_t dependency_body = 0;
    std::int32_t dependency_body_id = 0;
};
struct AssocValueDependency {
    AssocDependency dependency;
    std::int32_t class_version = 0;
    std::string name;
    AssocEvalVariant value;
};
struct AssocPersistentSubentId {
    std::string class_name;
    bool dependent_on_compound_object = false;
    std::int32_t class_code = 0;
    std::vector<std::int32_t> values;
    bool leading_flag = false;
};
struct AssocGeomDependency {
    AssocDependency dependency;
    std::int16_t class_version = 0;
    bool enabled = false;
    AssocPersistentSubentId persistent_subent;
};
struct AssocNetwork {
    AssocAction action;
    std::int16_t network_version = 0;
    std::int32_t network_action_index = 0;
    std::vector<AssocActionDependency> actions;
    std::vector<std::uint64_t> owned_actions;
};
struct AssocActionParam {
    std::int16_t is_r2013 = 0;
    std::int32_t version = 0;
    std::string name;
};
struct AssocActionBody {
    std::int32_t version = 0;
};
struct AssocParamBasedActionBody {
    std::int32_t version = 0;
    std::int32_t minor = 0;
    std::vector<std::uint64_t> dependencies;
    std::int32_t marker = 0;
    std::vector<AssocValueParam> values;
    std::int32_t empty_value_marker = 0;
    std::uint64_t dependency = 0;
};
struct AssocSurfaceBody {
    std::int32_t version = 0;
    std::uint64_t dependency = 0;
    bool is_semi_associative = false;
    std::int32_t marker = 0;
    bool is_semi_override = false;
    std::int16_t grip_status = 0;
};
enum class AssocSurfaceActionKind {
    Plane,
    Extend,
    Extruded,
    Lofted,
    Network,
    Offset,
    Revolved,
    Trim,
    Blend,
    Patch,
    Fillet,
    Swept,
    EdgeChamfer,
    EdgeFillet
};
struct AssocSurfaceActionBody {
    AssocSurfaceActionKind kind = AssocSurfaceActionKind::Plane;
    AssocActionBody action_body;
    AssocParamBasedActionBody parameter_body;
    AssocSurfaceBody surface_body;
    std::int32_t path_status = 0;
    std::int32_t class_version = 0;
    std::uint8_t option = 0;
    std::array<bool, 5> flags{};
    std::int16_t status = 0;
    std::int16_t secondary_status = 0;
    double distance = 0;
    Vector2 first_point;
    Vector2 second_point;
};
struct AssocAnnotationBase {
    AssocActionBody action_body;
    AssocParamBasedActionBody parameter_body;
    std::int16_t version = 0;
    std::uint64_t dependency = 0;
};
enum class AssocAnnotationKind {
    RestoreEntityState,
    MLeader,
    AlignedDimension,
    ThreePointAngularDimension,
    OrdinateDimension,
    RotatedDimension
};
struct AssocAnnotationDependency {
    std::int32_t dependency_id = 0;
    std::uint64_t dependency = 0;
};
struct AssocAnnotationActionBody {
    AssocAnnotationKind kind = AssocAnnotationKind::RestoreEntityState;
    AssocAnnotationBase annotation;
    AssocActionBody action_body;
    std::int32_t class_version = 0;
    std::uint64_t entity = 0;
    std::vector<AssocAnnotationDependency> actions;
    std::uint64_t read_node = 0;
    std::uint64_t dimension_node = 0;
    std::uint64_t dependency = 0;
};
struct AssocPersSubentManager {
    std::int32_t class_version = 0;
    std::array<std::int32_t, 3> markers{};
    std::vector<std::int32_t> values;
    bool final_flag = false;
};
struct AssocSingleDependencyActionParam {
    AssocActionParam action_param;
    std::int32_t dependency_class_version = 0;
    std::uint64_t dependency = 0;
    std::int32_t class_version = 0;
};
enum class AssocSubcurveKind { None, Arc, Ellipse, Line, LineSegment3d, Nurb3d, Curve3d };
using AssocCurveValue = std::variant<bool, std::int32_t, double, Vector3>;
struct AssocEdgeActionParam {
    AssocSingleDependencyActionParam single_dependency;
    std::uint64_t parameter = 0;
    bool has_action = false;
    std::int32_t action_type = 0;
    AssocSubcurveKind subcurve_kind = AssocSubcurveKind::None;
    std::vector<AssocCurveValue> curve;
};
struct AssocConstraintGeometrical {
    std::int32_t owner_id = 0;
    bool is_implied = false;
    bool is_active = false;
};
struct AssocConstraintComposite : AssocConstraintGeometrical {
    std::vector<std::int32_t> owned_constraint_ids;
};
struct AssocConstraintHelpParameter {
    double value = 0;
    bool reserved = false;
};
struct AssocConstraintAngle : AssocConstraintGeometrical {
    std::uint64_t value_dependency = 0;
    std::uint64_t dimension_dependency = 0;
    std::uint8_t sector_type = 0;
};
struct AssocConstraintParallel : AssocConstraintGeometrical {
    std::optional<std::int32_t> datum_line_index;
};
struct AssocConstraintDistance : AssocConstraintGeometrical {
    std::uint64_t value_dependency = 0;
    std::uint64_t dimension_dependency = 0;
    std::uint8_t direction_type = 0;
    std::optional<Vector3> distance;
};
struct AssocConstraintRadiusDiameter : AssocConstraintGeometrical {
    std::uint64_t value_dependency = 0;
    std::uint64_t dimension_dependency = 0;
    std::uint8_t mode = 0;
};
struct AssocConstraintImplicitPoint {
    std::uint64_t geometry_dependency = 0;
    std::int32_t geometry_node_id = 0;
    std::optional<Vector3> point;
    std::uint8_t point_type = 0;
    std::int32_t point_index = 0;
    std::int32_t curve_id = 0;
};
struct AssocConstraintPoint {
    std::uint64_t geometry_dependency = 0;
    std::int32_t geometry_node_id = 0;
    std::optional<Vector3> point;
};
struct AssocConstraintRigidSet {
    std::uint64_t geometry_dependency = 0;
    std::int32_t geometry_node_id = 0;
    bool reserved = false;
    std::array<double, 16> transform{};
    std::vector<std::int32_t> geometry_ids;
};
struct AssocConstraintLine {
    std::uint64_t geometry_dependency = 0;
    std::int32_t geometry_node_id = 0;
    Vector3 point;
    Vector3 direction;
};
struct AssocConstraintBoundedLine : AssocConstraintLine {
    bool is_ray = false;
    Vector3 start_point;
    Vector3 end_point;
};
struct AssocConstraintCircle {
    std::uint64_t geometry_dependency = 0;
    std::int32_t geometry_node_id = 0;
    Vector3 center;
    Vector3 normal;
    Vector3 direction;
    double radius = 0;
    double start_parameter = 0;
    double end_parameter = 0;
    double reserved = 0;
};
struct AssocConstraintArc : AssocConstraintCircle {
    Vector3 start_point;
    Vector3 end_point;
};
struct AssocConstraintEllipse {
    std::uint64_t geometry_dependency = 0;
    std::int32_t geometry_node_id = 0;
    Vector3 center;
    Vector3 major_axis;
    double axis_ratio = 0;
};
struct AssocConstraintBoundedEllipse : AssocConstraintEllipse {
    Vector3 start_point;
    Vector3 end_point;
};
struct AssocConstraintSpline {
    std::uint64_t geometry_dependency = 0;
    std::int32_t geometry_node_id = 0;
    bool rational = false;
    bool periodic = false;
    std::int32_t degree = 0;
    double knot_tolerance = 0;
    std::int32_t knot_physical_length = 0;
    std::int32_t knot_grow_length = 0;
    std::vector<double> knots;
    std::int32_t weight_physical_length = 0;
    std::int32_t weight_grow_length = 0;
    std::vector<double> weights;
    std::int32_t control_point_physical_length = 0;
    std::int32_t control_point_grow_length = 0;
    std::vector<Vector3> control_points;
    std::vector<std::int32_t> implicit_point_ids;
};
using AssocConstraintNodeData =
    std::variant<std::monostate, AssocConstraintGeometrical, AssocConstraintComposite,
                 AssocConstraintHelpParameter, AssocConstraintAngle, AssocConstraintParallel,
                 AssocConstraintDistance, AssocConstraintRadiusDiameter, AssocConstraintImplicitPoint,
                 AssocConstraintPoint, AssocConstraintRigidSet, AssocConstraintLine,
                 AssocConstraintBoundedLine, AssocConstraintCircle, AssocConstraintArc,
                 AssocConstraintEllipse, AssocConstraintBoundedEllipse, AssocConstraintSpline>;
struct AssocConstraintNode {
    std::int32_t node_id = 0;
    std::uint8_t status = 0;
    std::vector<std::int32_t> connections;
    std::string class_name;
    bool registry_flag = false;
    AssocConstraintNodeData data;
};
struct Assoc2dConstraintGroup {
    AssocAction action;
    std::int32_t version = 0;
    bool flag = false;
    std::array<Vector3, 3> work_plane{};
    std::uint64_t dependency = 0;
    std::vector<std::uint64_t> actions;
    std::int32_t next_node_id = 0;
    std::vector<AssocConstraintNode> nodes;
    std::int32_t effective_next_node_id() const;
};
struct AssocVariableDependency {
    std::uint64_t dependency = 0;
    std::int32_t flags = 0;
};
struct AssocVariable {
    AssocAction action;
    std::int32_t class_version = 0;
    std::string name;
    std::string expression;
    std::string evaluator;
    std::string description;
    AssocEvalVariant value;
    bool has_cached_value = false;
    std::string cached_value;
    bool flag = false;
    std::vector<AssocVariableDependency> dependencies;
};
struct AssocChildParameter {
    std::int16_t status = 0;
    std::int32_t id = 0;
    std::uint64_t parameter = 0;
    std::uint64_t secondary_parameter = 0;
    std::int32_t marker = 0;
    std::uint64_t tertiary_parameter = 0;
};
struct AssocCompoundActionParam {
    AssocActionParam action_param;
    std::int16_t class_version = 0;
    std::int16_t status = 0;
    std::vector<std::uint64_t> parameters;
    std::optional<AssocChildParameter> child_parameter;
};
struct AssocPointRefActionParam {
    AssocCompoundActionParam compound;
};
struct AssocOsnapPointRefActionParam {
    AssocCompoundActionParam compound;
    std::int16_t status = 0;
    std::uint8_t osnap_mode = 0;
    double parameter = 0;
};
struct AssocPathActionParam {
    AssocCompoundActionParam compound;
    std::int32_t version = 0;
};
struct AssocDimDependencyBody {
    std::int16_t dependency_body_version = 0;
    std::int16_t base_version = 0;
    std::string name;
    std::int16_t class_version = 0;
};
struct AssocFaceActionParam {
    AssocSingleDependencyActionParam single_dependency;
    std::int32_t index = 0;
};
struct AssocVertexActionParam {
    AssocSingleDependencyActionParam single_dependency;
    Vector3 point;
};
struct AssocAsmBodyActionParam {
    AssocSingleDependencyActionParam single_dependency;
    std::string sat_data;
    std::vector<std::uint8_t> sab_data;
    bool sat_encoded = false;
    bool is_binary = false;
    Vector3 point_of_reference;
    std::uint64_t history = 0;
};
struct AssocArrayItem {
    std::int32_t class_version = 0;
    std::array<std::int32_t, 3> location{};
    std::int32_t flags = 0;
    bool uses_default_transform = false;
    Vector3 x_direction;
    std::array<double, 16> transform{};
    std::optional<std::array<double, 16>> relative_transform;
    std::optional<std::uint64_t> first_handle;
    std::optional<std::uint64_t> second_handle;
};
struct AssocArrayParameters {
    std::int32_t version = 0;
    std::string class_name;
    std::vector<AssocArrayItem> items;
    std::int32_t item_count = 0;
    std::int32_t row_count = 0;
    std::int32_t level_count = 0;
};
struct AssocArrayActionBody {
    AssocActionBody action_body;
    AssocParamBasedActionBody parameter_body;
    std::int32_t version = 0;
    std::string parameter_block;
    std::int32_t item_list_version = 0;
    std::string item_class;
    std::vector<AssocArrayItem> items;
    std::array<double, 16> transform{};
};
struct AssocArrayModifyActionBody {
    AssocArrayActionBody body;
    std::int16_t status = 0;
    std::vector<std::array<std::int32_t, 3>> item_locations;
};
struct AssocDimensionReference {
    std::string class_name;
    std::uint8_t osnap_type = 0;
    std::vector<std::uint64_t> xrefs;
    std::int32_t main_subent_type = 0;
    std::int32_t main_gs_marker = 0;
    std::vector<std::string> xref_paths;
    double osnap_distance = 0;
    Vector3 osnap_point;
    std::vector<std::uint64_t> intersection_objects;
    std::int32_t intersection_subent_type = 0;
    std::int32_t intersection_gs_marker = 0;
    std::vector<std::string> intersection_xref_paths;
    bool has_last_point_reference = false;
    static bool stores_main_subentity(std::uint8_t, std::size_t object_count);
    static bool stores_intersection(std::uint8_t osnap_type);
    static bool stores_intersection_subentity(std::uint8_t osnap_type, std::size_t object_count);
};
struct AssocDimensionAssociation {
    std::int32_t associativity = 0;
    bool trans_space = false;
    std::uint8_t rotated_type = 0;
    std::uint64_t dimension = 0;
    std::array<std::vector<AssocDimensionReference>, 4> references;
};
struct PersSubentManager {
    std::int32_t class_version = 0;
    std::int32_t marker_zero = 0;
    std::int32_t marker_two = 0;
    std::int32_t associative_step_count = 0;
    std::int32_t associative_subent_count = 0;
    std::vector<std::int32_t> values;
};
struct AssocSmartCenterActionBody {
    AssocActionBody action_body;
    AssocParamBasedActionBody parameter_body;
    std::int32_t version = 0;
};
struct AssocViewRepActionBody {
    AssocActionBody action_body;
    std::int16_t class_version = 0;
    std::uint64_t view_rep = 0;
    std::int32_t view_type = 0;
    double rotation = 0;
};
enum class AssocViewObjectActionParamKind { ViewBorder, ViewRep, ViewSymbol, ViewStyle };
struct AssocViewObjectActionParam {
    AssocViewObjectActionParamKind kind = AssocViewObjectActionParamKind::ViewBorder;
    AssocSingleDependencyActionParam single_dependency;
    std::int16_t class_version = 0;
};
struct AssocViewRepHatchManagerItem {
    std::int64_t first_id = 0;
    std::int64_t second_id = 0;
    std::int32_t status = 0;
    std::uint64_t parameter = 0;
};
struct AssocViewRepHatchManager {
    AssocCompoundActionParam compound;
    std::int16_t class_version = 0;
    std::vector<AssocViewRepHatchManagerItem> items;
};
struct AssocViewRepHatchActionParam {
    AssocSingleDependencyActionParam single_dependency;
    std::int16_t class_version = 0;
    Vector3 normal;
    std::int32_t hatch_index = 0;
    std::int32_t flags = 0;
};
struct AssocViewLabelActionParam {
    AssocSingleDependencyActionParam single_dependency;
    std::int16_t class_version = 0;
    std::int16_t label_version = 0;
    Vector2 offset;
    std::uint8_t flag = 0;
};

using AssociativeData =
    std::variant<std::monostate, AssocDependency, AssocValueDependency, AssocGeomDependency,
                 AssocSurfaceActionBody, AssocAction, AssocNetwork, AssocAnnotationActionBody,
                 AssocPersSubentManager, AssocEdgeActionParam, Assoc2dConstraintGroup, AssocVariable,
                 AssocActionParam, AssocCompoundActionParam, AssocOsnapPointRefActionParam,
                 AssocPointRefActionParam, AssocSingleDependencyActionParam, AssocPathActionParam,
                 AssocDimDependencyBody, AssocFaceActionParam, AssocVertexActionParam,
                 AssocAsmBodyActionParam, AssocArrayParameters, AssocArrayActionBody,
                 AssocArrayModifyActionBody, AssocDimensionAssociation, PersSubentManager,
                 AssocViewRepActionBody, AssocViewObjectActionParam, AssocViewRepHatchManager,
                 AssocViewRepHatchActionParam, AssocViewLabelActionParam, AssocSmartCenterActionBody>;
enum class AssociativeKind {
    Unknown,
    Dependency,
    ValueDependency,
    GeomDependency,
    SurfaceActionBody,
    Action,
    Network,
    AnnotationActionBody,
    PersSubentManager,
    EdgeActionParam,
    ConstraintGroup,
    Variable,
    ActionParam,
    CompoundActionParam,
    OsnapPointRefActionParam,
    PointRefActionParam,
    ObjectActionParam,
    PathActionParam,
    DimDependencyBody,
    FaceActionParam,
    VertexActionParam,
    AsmBodyActionParam,
    ArrayParameters,
    ArrayActionBody,
    ArrayModifyActionBody,
    DimensionAssociation,
    PersSubentManagerStatic,
    ViewRepActionBody,
    ViewObjectActionParam,
    ViewRepHatchManager,
    ViewRepHatchActionParam,
    ViewLabelActionParam,
    SmartCenterActionBody
};
struct AssociativeObject {
    std::uint64_t handle = 0;
    std::uint64_t owner = 0;
    std::vector<std::uint64_t> reactors;
    std::optional<std::uint64_t> xdictionary_handle;
    std::string dxf_name;
    std::string cpp_class_name;
    AssociativeData data;
    std::optional<DxfVersion> source_version;
    AssociativeKind kind() const;
    bool references_handle(std::uint64_t target) const;
    void visit_handles(const std::function<void(std::uint64_t &)> &visit);
    void visit_handles(const std::function<void(std::uint64_t)> &visit) const;
    std::vector<std::uint64_t> referenced_handles() const;
    void remap_handles(const std::unordered_map<std::uint64_t, std::uint64_t> &map);
};

inline AssociativeKind AssociativeObject::kind() const {
    return static_cast<AssociativeKind>(data.index());
}
inline bool AssocDimensionReference::stores_main_subentity(std::uint8_t, std::size_t object_count) {
    return object_count > 0;
}
inline bool AssocDimensionReference::stores_intersection(std::uint8_t osnap_type) {
    return osnap_type == 6 || osnap_type == 11;
}
inline bool AssocDimensionReference::stores_intersection_subentity(std::uint8_t osnap_type,
                                                                   std::size_t object_count) {
    return stores_intersection(osnap_type) && object_count > 0;
}
} // namespace libopencad
