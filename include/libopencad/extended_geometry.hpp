// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "extended_entity.hpp"
#include "bounds.hpp"
#include "transform.hpp"
namespace libopencad {
// Detached numeric operations. Serialization can impose additional DXF limits.
ExtendedEntityDefinition translate_extended_definition(const ExtendedEntityDefinition&, Vector3);
ExtendedEntityDefinition transform_extended_definition(const ExtendedEntityDefinition&, const Transform&);
BoundingBox3D extended_bounding_box(const ExtendedEntityDefinition&);
std::array<Point3,8> point_cloud_world_corners(const PointCloudExDefinition&);
}
