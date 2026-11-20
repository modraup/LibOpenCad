// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "dxf_raw_field.hpp"
#include "vector.hpp"
#include <string>
#include <variant>
#include <vector>
namespace libopencad {
struct BreakPointReference {
    std::int16_t type = 0, flags = 0;
    std::int32_t identifier = 0;
    Point3 first{}, second{};
    std::uint64_t subentity_object = 0;
    std::int32_t subentity_type = 0, subentity_index = 0;
};
struct BreakDataDefinition {
    std::int16_t version = 0;
    std::uint64_t dimension = 0;
    std::vector<BreakPointReference> points;
};
struct IdBufferDefinition {
    std::vector<std::uint64_t> objects;
};
struct IndexDefinition {
    std::int32_t julian_day = 0, milliseconds = 0;
};
struct LayerIndexEntry {
    std::int32_t count = 0;
    std::string name;
    std::uint64_t id_buffer = 0;
};
struct LayerIndexDefinition {
    IndexDefinition updated;
    std::vector<LayerIndexEntry> entries;
};
// These upstream DXF classes carry only their common header; no vendor payload is interpreted.
struct EmptyDataDefinition {
    enum class Kind {
        BreakPointRef,
        AcDsRecord,
        AcDsSchema,
        Dummy,
        LongTransaction,
        ObjectPointer,
        PartialViewingFilter
    };
    Kind kind = Kind::Dummy;
};
struct CloudFileDefinition {
    bool extended = false;
    std::int32_t version = 0;
    std::string filename;
    bool loaded = false;
    std::int64_t point_count = 0;
    Point3 min{}, max{};
};
struct CloudReactorDefinition {
    bool extended = false;
    std::int32_t version = 0;
};
struct CloudRampColor {
    std::int32_t color = 0;
    bool visible = false;
};
struct CloudColorRamp {
    std::string id, name;
    std::int16_t version = 0;
    std::vector<CloudRampColor> colors;
};
struct CloudColorMapDefinition {
    std::int16_t version = 0;
    std::string intensity, elevation, classification;
    std::vector<CloudColorRamp> ramps, classification_ramps;
};
struct CoordinationFileDefinition {
    std::int16_t flags = 0;
    std::string path;
    bool loaded = false, host_visibility = false;
    Point3 min{}, max{};
};
struct ContextManagerEntry {
    std::uint64_t item = 0;
    std::string name;
};
struct ContextSubManager {
    std::uint64_t handle = 0;
    std::vector<ContextManagerEntry> entries;
};
struct ContextManagerDefinition {
    std::uint64_t context = 0;
    std::vector<ContextSubManager> managers;
};
using DataObjectPayload =
    std::variant<BreakDataDefinition, IdBufferDefinition, IndexDefinition, LayerIndexDefinition,
                 EmptyDataDefinition, CloudFileDefinition, CloudReactorDefinition, CloudColorMapDefinition,
                 CoordinationFileDefinition, ContextManagerDefinition>;
struct DataObjectDefinition {
    DataObjectPayload payload;
    std::vector<DxfRawField> extra_fields;
};
} // namespace libopencad
