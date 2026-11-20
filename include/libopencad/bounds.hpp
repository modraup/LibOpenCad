// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/types/bounds.rs (BoundingBox3D).
#pragma once
#include <libopencad/vector.hpp>
#include <optional>
#include <vector>
namespace libopencad {
struct BoundingBox3D {
    Vector3 min{}, max{};
    constexpr bool operator==(const BoundingBox3D &other) const noexcept {
        return min == other.min && max == other.max;
    }
    constexpr bool operator!=(const BoundingBox3D &other) const noexcept { return !(*this == other); }
    static BoundingBox3D from_point(Vector3 point) noexcept { return {point, point}; }
    static std::optional<BoundingBox3D> from_points(const std::vector<Vector3> &points);
    double width() const noexcept { return max.x - min.x; }
    double height() const noexcept { return max.y - min.y; }
    double depth() const noexcept { return max.z - min.z; }
    Vector3 center() const noexcept { return (min + max) / 2.; }
    bool contains(Vector3 point) const noexcept;
    void expand_to_include(Vector3 point) noexcept;
    BoundingBox3D merge(const BoundingBox3D &other) const noexcept;
    // Maps all eight corners; returns this box unchanged for the +Z normal.
    BoundingBox3D ocs_to_wcs(Vector3 normal) const;
};
std::ostream &operator<<(std::ostream &out, const BoundingBox3D &box);
} // namespace libopencad
