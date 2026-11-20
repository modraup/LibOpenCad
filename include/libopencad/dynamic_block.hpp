// SPDX-License-Identifier: MPL-2.0
#pragma once
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
// Matches acadrust BlockEvalValue; handles and integers remain distinct types.
using BlockEvalValue =
    std::variant<std::monostate, double, std::string, std::int32_t, std::uint64_t, std::int16_t, Vector2>;
struct DynamicBlockProperty {
    std::uint64_t parameter_handle = 0;
    std::string name, description;
    std::int16_t type_code = 1, units = 0; // units: unitless, radians, distance, area.
    bool show = false, read_only = false, visible = true;
    BlockEvalValue value;
    std::vector<BlockEvalValue> allowed_values;
};
struct BlockEvaluationNode {
    std::int32_t id = 0, edge_flags = 0, next_id = 0;
    std::uint64_t expression = 0;
    std::array<std::int32_t, 4> node_data{};
    std::optional<bool> active_cycles;
};
struct BlockEvaluationEdge {
    std::int32_t id = 0, next_id = 0, incoming_edge = 0, source_node = 0, destination_node = 0;
    std::array<std::int32_t, 5> outgoing_edges{};
};
struct BlockEvaluationGraph {
    std::int32_t first_node_id = 0, first_node_id_copy = 0;
    std::vector<BlockEvaluationNode> nodes;
    std::vector<BlockEvaluationEdge> edges;
};
struct BlockVisibilityState {
    std::string name;
    std::vector<std::uint64_t> visible_blocks, visible_params;
};
struct BlockEvalExpression {
    std::int32_t parent_id = -1, major = 0, minor = 0, node_id = 0;
    std::int16_t value_code = 0;
    BlockEvalValue value;
};
struct BlockElement {
    BlockEvalExpression eval;
    std::string name;
    std::int32_t major = 0, minor = 0, eed_1071 = 0;
};
struct BlockConnection {
    std::int32_t code = 0;
    std::string name;
};
struct BlockParameterProperty {
    std::vector<BlockConnection> connections;
};
struct BlockParameterValueSet {
    std::string description;
    std::int32_t flags = 0;
    double minimum = 0, maximum = 0, increment = 0;
    std::vector<double> values;
};
struct BlockParameter {
    BlockElement element;
    bool show_properties = false, chain_actions = false;
};
struct BlockOnePointParameter {
    BlockParameter parameter;
    Vector3 definition_point{};
    std::array<BlockParameterProperty, 2> properties{};
    std::int32_t property_count = 0;
};
struct BlockTwoPointParameter {
    BlockParameter parameter;
    Vector3 definition_base_point{}, definition_end_point{};
    std::array<BlockParameterProperty, 4> properties{};
    std::array<std::int32_t, 4> property_states{};
    std::int16_t parameter_base_location = 0;
};
struct BlockGrip {
    BlockElement element;
    std::int32_t flags_91 = 0, flags_92 = 0, insert_cycling_weight = 0;
    Vector3 location{};
    bool insert_cycling = false;
};
struct BlockGripExpression {
    BlockEvalExpression eval;
    std::int32_t grip_type = 0;
    std::string expression;
};
struct BlockOrientedGrip {
    BlockGrip grip;
    Vector3 orientation{};
};
struct BlockFlipGrip {
    BlockGrip grip;
    std::int32_t combined_state = 0;
    Vector3 orientation{};
};
struct BlockAction {
    BlockElement element;
    Vector3 display_location{};
    std::vector<std::uint64_t> dependencies;
    std::vector<std::int32_t> action_ids;
};
struct BlockActionOffsets {
    double distance_multiplier = 0, angle_offset = 0;
    std::uint8_t flags = 0;
};
struct BlockActionWithBasePoint {
    BlockAction action;
    Vector3 offset{}, base_point{};
    std::vector<BlockConnection> connections;
    bool dependent = false;
};
struct BlockConstraintParameter {
    BlockTwoPointParameter parameter;
    std::uint64_t dependency = 0;
};
struct BlockLinearConstraintParameter {
    BlockConstraintParameter constraint;
    std::string expression_name, expression_description;
    double value = 0;
    BlockParameterValueSet value_set;
};
struct BlockRepresentationData {
    std::int16_t flags = 0;
    std::uint64_t block = 0;
};
struct BlockAlignmentParameter {
    BlockTwoPointParameter parameter;
    bool align_perpendicular = false;
};
struct BlockLinearParameter {
    BlockTwoPointParameter parameter;
    std::string distance_name, distance_description;
    double distance = 0;
    BlockParameterValueSet value_set;
};
struct BlockBasePointParameter {
    BlockOnePointParameter parameter;
    Vector3 point{}, base_point{};
};
struct BlockFlipParameter {
    BlockTwoPointParameter parameter;
    std::string flip_label, flip_label_description, base_state_label, flipped_state_label, tooltip;
    Vector3 definition_label_point{};
    std::int32_t flags_96 = 0;
};
struct BlockXYParameter {
    BlockTwoPointParameter parameter;
    std::string x_label, x_label_description, y_label, y_label_description;
    double x_value = 0, y_value = 0;
    BlockParameterValueSet x_value_set, y_value_set;
};
struct BlockRotationParameter {
    BlockTwoPointParameter parameter;
    Vector3 definition_base_angle_point{};
    std::string angle_name, angle_description;
    double angle = 0;
    BlockParameterValueSet value_set;
};
struct BlockPolarParameter {
    BlockTwoPointParameter parameter;
    std::string angle_name, angle_description, distance_name, distance_description;
    double offset = 0;
    BlockParameterValueSet angle_value_set, distance_value_set;
};
struct BlockLookupParameter {
    BlockOnePointParameter parameter;
    std::int32_t index = 0;
    std::string lookup_name, lookup_description;
};
struct BlockPointParameter {
    BlockOnePointParameter parameter;
    std::string position_name, position_description;
    Vector3 definition_label_point{};
};
struct BlockUserParameter {
    BlockOnePointParameter parameter;
    std::int16_t flags = 0, value_code = 0, value_type = 0;
    std::uint64_t associated_variable = 0;
    std::string expression;
    BlockEvalValue value;
};
struct BlockAngularConstraintParameter {
    BlockConstraintParameter constraint;
    Vector3 center_point{}, end_point{};
    std::string expression_name, expression_description;
    double angle = 0;
    bool orientation_on_both_grips = false;
    BlockParameterValueSet value_set;
};
struct BlockAngularConstraintParameterEntity {
    BlockConstraintParameter constraint;
    Vector3 center_point{}, label_point{};
    std::string expression_name, expression_description;
    double angle = 0;
    bool orientation_on_both_grips = false;
    BlockParameterValueSet value_set;
};
struct BlockDistanceConstraintParameter {
    BlockConstraintParameter constraint;
    std::string expression_name, expression_description;
    double distance = 0;
    BlockParameterValueSet value_set;
};
struct BlockParameterDependencyBody {
    std::int16_t dependency_body_version = 0, dimension_base_version = 0, class_version = 0;
    std::string name;
};
struct BlockMoveAction {
    BlockAction action;
    std::array<BlockConnection, 2> connections{};
    BlockActionOffsets offsets;
};
struct BlockFlipAction {
    BlockAction action;
    std::array<BlockConnection, 4> connections{};
};
struct BlockBasePointAction {
    BlockActionWithBasePoint action;
    std::vector<BlockConnection> connections;
    std::uint8_t dwg_tail_byte = 0;
};
struct BlockArrayAction {
    BlockAction action;
    std::array<BlockConnection, 4> connections{};
    double column_offset = 0, row_offset = 0;
};
struct BlockLookupColumn {
    std::int32_t node_id = 0, value_type = 0, property_type = 0;
    bool lookup_property = false, writable = false;
    std::string unmatched_name, connection_name;
};
struct BlockLookupAction {
    BlockAction action;
    std::int32_t row_count = 0, column_count = 0;
    std::vector<std::string> expressions;
    std::vector<BlockLookupColumn> columns;
    bool flag_280 = false;
};
using BlockPropertiesTableValue =
    std::variant<std::string, bool, std::int64_t, double, std::uint64_t, std::vector<std::uint8_t>>;
struct BlockPropertiesTableField {
    std::int32_t code = 0;
    BlockPropertiesTableValue value;
};
struct BlockPropertiesTable {
    BlockOnePointParameter parameter;
    std::vector<BlockPropertiesTableField> fields;
};
struct BlockStretchHandle {
    std::uint64_t handle = 0;
    std::vector<std::int32_t> indexes;
};
struct BlockStretchCode {
    std::int32_t code = 0;
    std::vector<std::int32_t> indexes;
};
struct BlockStretchAction {
    BlockAction action;
    std::array<BlockConnection, 2> connections{};
    std::vector<Vector2> points;
    std::vector<BlockStretchHandle> handles;
    std::vector<BlockStretchCode> codes;
    BlockActionOffsets offsets;
};
struct BlockPolarStretchAction {
    BlockAction action;
    std::array<BlockConnection, 6> connections{};
    std::vector<Vector2> points;
    std::vector<std::uint64_t> handles;
    std::vector<BlockStretchHandle> bindings;
    std::vector<BlockStretchCode> codes;
    double distance_multiplier = 0, angle_offset = 0;
    std::vector<std::int32_t> extra;
};
struct BlockVisibilityParameter {
    std::uint64_t handle = 0, owner = 0;
    std::int32_t eval_node_id = 0, eval_parent_id = 0, eval_major = 0, eval_minor = 0;
    std::int16_t eval_value_code = 0;
    BlockEvalValue eval_value;
    std::string element_name, name, description;
    std::int32_t element_major = 0, element_minor = 0, element_eed_1071 = 0, property_count = 0;
    bool show_properties = false, chain_actions = false, is_initialized = false, unknown_bool = false;
    Vector3 definition_point{};
    std::array<BlockParameterProperty, 2> property_info{};
    std::vector<std::uint64_t> all_blocks;
    std::vector<BlockVisibilityState> states;
};
enum class DynamicBlockKind {
    Unknown,
    Representation,
    ProxyNode,
    GripLocationComponent,
    AlignmentGrip,
    FlipGrip,
    LinearGrip,
    LookupGrip,
    PolarGrip,
    RotationGrip,
    VisibilityGrip,
    XYGrip,
    PropertiesTableGrip,
    AlignmentParameter,
    BasePointParameter,
    FlipParameter,
    LinearParameter,
    LookupParameter,
    PointParameter,
    PolarParameter,
    RotationParameter,
    UserParameter,
    VisibilityParameter,
    XYParameter,
    AngularConstraintParameter,
    DiametricConstraintParameter,
    RadialConstraintParameter,
    AlignedConstraintParameter,
    LinearConstraintParameter,
    HorizontalConstraintParameter,
    VerticalConstraintParameter,
    ParameterDependencyBody,
    MoveAction,
    FlipAction,
    RotateAction,
    ScaleAction,
    ArrayAction,
    LookupAction,
    PropertiesTable,
    StretchAction,
    PolarStretchAction,
    EmptyEntity,
    AngularConstraintParameterEntity
};
using DynamicBlockPayload = std::variant<
    std::monostate, BlockRepresentationData, BlockEvalExpression, BlockGripExpression, BlockOrientedGrip,
    BlockFlipGrip, BlockGrip, BlockAlignmentParameter, BlockBasePointParameter, BlockFlipParameter,
    BlockLinearParameter, BlockLookupParameter, BlockPointParameter, BlockPolarParameter,
    BlockRotationParameter, BlockUserParameter, BlockVisibilityParameter, BlockXYParameter,
    BlockAngularConstraintParameter, BlockDistanceConstraintParameter, BlockLinearConstraintParameter,
    BlockParameterDependencyBody, BlockMoveAction, BlockFlipAction, BlockBasePointAction, BlockArrayAction,
    BlockLookupAction, BlockPropertiesTable, BlockStretchAction, BlockPolarStretchAction,
    BlockAngularConstraintParameterEntity>;
bool is_dynamic_block_object_name(std::string_view name);
bool is_dynamic_block_entity_name(std::string_view name);
std::optional<std::string> dynamic_block_cpp_class_name(std::string_view name);
DynamicBlockKind dynamic_block_kind_from_name(std::string_view name);
struct DynamicBlockObject {
    std::uint64_t handle = 0, owner = 0;
    std::vector<std::uint64_t> reactors;
    std::optional<std::uint64_t> xdictionary_handle;
    std::string dxf_name, cpp_class_name, layer = "0";
    bool is_entity = false, paper_space = false;
    DynamicBlockKind kind = DynamicBlockKind::Unknown;
    DynamicBlockPayload data;
    void visit_handles(const std::function<void(std::uint64_t &)> &visit);
    void visit_handles(const std::function<void(std::uint64_t)> &visit) const;
    std::vector<std::uint64_t> referenced_handles() const;
    bool references_handle(std::uint64_t target) const;
    void remap_handles(const std::unordered_map<std::uint64_t, std::uint64_t> &map);
};
} // namespace libopencad
