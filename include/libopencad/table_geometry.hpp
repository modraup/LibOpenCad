// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "dxf_raw_field.hpp"
#include "vector.hpp"
#include <cstdint>
#include <vector>
namespace libopencad {
struct TableContentGeometry {
    Point3 distance_to_top_left{}, distance_to_center{};
    double width = 0, height = 0, outer_width = 0, outer_height = 0;
    std::int32_t flags = 0;
    std::vector<DxfRawField> extra_fields;
};
struct TableGeometryCell {
    std::int32_t data_flags = 0;
    double width_with_gap = 0, height_with_gap = 0;
    std::uint64_t table_geometry = 0;
    std::vector<TableContentGeometry> contents;
    std::vector<DxfRawField> extra_fields;
};
struct TableGeometryDefinition {
    std::int32_t rows = 0, columns = 0;
    // Row-major; standalone objects may contain a partial cache.
    std::vector<TableGeometryCell> cells;
    std::vector<DxfRawField> extra_fields;
};
} // namespace libopencad
