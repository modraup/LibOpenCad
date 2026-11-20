// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "color.hpp"
#include "dxf_raw_field.hpp"
#include "transform.hpp"
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
namespace libopencad {
struct GroupDefinition {
    std::string description;
    bool unnamed = false, selectable = true;
    std::vector<std::uint64_t> entities;
    std::vector<DxfRawField> extra_fields;
};
// rust objects::Group: dictionary key plus GROUP object fields.
struct GroupObject {
    std::uint64_t handle = 0, owner = 0;
    std::string name;
    GroupDefinition definition;
};
struct ScaleDefinition {
    std::string name = "1:1";
    double paper_units = 1, drawing_units = 1;
    bool unit_scale = true;
    std::vector<DxfRawField> extra_fields;
};
struct SortEntry {
    std::uint64_t entity = 0, sort_handle = 0;
};
struct SortEntitiesDefinition {
    std::uint64_t block = 0;
    std::vector<SortEntry> entries;
    std::vector<DxfRawField> extra_fields;
};
struct SpatialFilterDefinition {
    std::vector<Vector2> boundary;
    Point3 normal{0, 0, 1}, origin{};
    bool enabled = true;
    std::optional<double> front_clip, back_clip;
    Matrix4 inverse_block_transform, clip_transform;
    std::vector<DxfRawField> extra_fields;
};
struct DictionaryVariableDefinition {
    std::int16_t schema = 0;
    std::string value;
    std::vector<DxfRawField> extra_fields;
};
struct BookColorDefinition {
    Color color = Color::by_layer();
    std::string book, name;
    std::vector<DxfRawField> extra_fields;
};
struct WipeoutVariablesDefinition {
    std::int16_t frame = 1;
    std::vector<DxfRawField> extra_fields;
};
using AuxiliaryDefinition =
    std::variant<GroupDefinition, ScaleDefinition, SortEntitiesDefinition, SpatialFilterDefinition,
                 DictionaryVariableDefinition, BookColorDefinition, WipeoutVariablesDefinition>;
} // namespace libopencad
