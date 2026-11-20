// SPDX-License-Identifier: MPL-2.0
// acadrust entities/{light,section_symbol,view_border,extended_entity}.rs.
#include <libopencad/extended_geometry.hpp>
#include <cmath>
#include <stdexcept>
#include <type_traits>
namespace libopencad {
namespace {
void validate(const Transform &t) {
    for (auto &r : t.matrix.m)
        for (auto v : r)
            if (!std::isfinite(v))
                throw std::invalid_argument("Nonfinite extended geometry transform");
}
std::vector<Point3> corners(Point3 a, Point3 b) {
    std::vector<Point3> p;
    for (int i = 0; i < 8; ++i)
        p.push_back({i & 1 ? b.x : a.x, i & 2 ? b.y : a.y, i & 4 ? b.z : a.z});
    return p;
}
} // namespace
ExtendedEntityDefinition translate_extended_definition(const ExtendedEntityDefinition &source, Vector3 d) {
    validate(Transform::from_translation(d));
    auto result = source;
    std::visit(
        [&](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, OleFrameDefinition>) {
                v.upper_left = v.upper_left + d;
                v.lower_right = v.lower_right + d;
            } else if constexpr (std::is_same_v<T, LightDefinition>) {
                v.position = v.position + d;
                v.target = v.target + d;
            } else if constexpr (std::is_same_v<T, SectionObjectDefinition>) {
                for (auto &p : v.vertices)
                    p = p + d;
                for (auto &p : v.back_vertices)
                    p = p + d;
            } else if constexpr (std::is_same_v<T, ArcAlignedTextDefinition>) {
                v.center = v.center + d;
            } else if constexpr (std::is_same_v<T, RemoteTextDefinition> ||
                                 std::is_same_v<T, PositionMarkerDefinition>) {
                v.position = v.position + d;
            } else if constexpr (std::is_same_v<T, PointCloudDefinition>) {
                v.origin = v.origin + d;
                v.extents_min = v.extents_min + d;
                v.extents_max = v.extents_max + d;
                v.ucs_origin = v.ucs_origin + d;
            } else if constexpr (std::is_same_v<T, PointCloudExDefinition>) {
                v.ucs_origin = v.ucs_origin + d;
            }
        },
        result);
    return result;
}
ExtendedEntityDefinition transform_extended_definition(const ExtendedEntityDefinition &source,
                                                       const Transform &t) {
    validate(t);
    auto result = source;
    std::visit(
        [&](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, OleFrameDefinition>) {
                v.upper_left = t.apply(v.upper_left);
                v.lower_right = t.apply(v.lower_right);
            } else if constexpr (std::is_same_v<T, LightDefinition>) {
                v.position = t.apply(v.position);
                v.target = t.apply(v.target);
                if (v.photometric)
                    v.photometric->web_rotation = t.apply_rotation(v.photometric->web_rotation);
            } else if constexpr (std::is_same_v<T, SectionSymbolDefinition>) {
                for (auto &p : v.points) {
                    auto old = p.position;
                    p.position = t.apply(old);
                    p.label_offset = t.apply(old + p.label_offset) - p.position;
                }
            } else if constexpr (std::is_same_v<T, ViewBorderDefinition>) {
                std::vector<Point3> p;
                for (auto xy : {v.min, Vector2{v.max.x, v.min.y}, v.max, Vector2{v.min.x, v.max.y}})
                    p.push_back(t.apply({xy.x, xy.y, 0}));
                auto b = *BoundingBox3D::from_points(p);
                v.min = {b.min.x, b.min.y};
                v.max = {b.max.x, b.max.y};
                auto c = t.apply({v.center.x, v.center.y, 0});
                v.center = {c.x, c.y};
                auto d = t.apply_rotation({std::cos(v.rotation), std::sin(v.rotation), 0});
                if (d.x != 0 || d.y != 0)
                    v.rotation = std::atan2(d.y, d.x);
            } else if constexpr (std::is_same_v<T, SectionObjectDefinition>) {
                for (auto &p : v.vertices)
                    p = t.apply(p);
                for (auto &p : v.back_vertices)
                    p = t.apply(p);
                v.vertical = t.apply_rotation(v.vertical).normalize();
            } else if constexpr (std::is_same_v<T, ArcAlignedTextDefinition>) {
                auto old = Matrix3::arbitrary_axis(v.normal);
                auto n = t.apply_rotation(v.normal).normalize();
                auto local = Matrix3::arbitrary_axis(n).transpose();
                auto angle = [&](double a) {
                    auto d = local * t.apply_rotation(old * Vector3{std::cos(a), std::sin(a), 0});
                    return std::atan2(d.y, d.x);
                };
                v.start_angle = angle(v.start_angle);
                v.end_angle = angle(v.end_angle);
                if (t.matrix.to_matrix3().determinant() < 0)
                    std::swap(v.start_angle, v.end_angle);
                v.center = t.apply(v.center);
                v.normal = n;
                double scale = t.apply_rotation(old * Vector3::UNIT_X).length();
                for (double *x :
                     {&v.radius, &v.size, &v.spacing, &v.arc_offset, &v.right_offset, &v.left_offset})
                    *x *= scale;
            } else if constexpr (std::is_same_v<T, RemoteTextDefinition>) {
                auto old = Matrix3::arbitrary_axis(v.normal);
                auto direction = old * Vector3{std::cos(v.rotation), std::sin(v.rotation), 0};
                v.normal = t.apply_rotation(v.normal).normalize();
                auto rotated = Matrix3::arbitrary_axis(v.normal).transpose() * t.apply_rotation(direction);
                if (rotated.x != 0 || rotated.y != 0)
                    v.rotation = std::atan2(rotated.y, rotated.x);
                v.position = t.apply(v.position);
                v.height *= t.apply_rotation(direction).length();
            } else if constexpr (std::is_same_v<T, PositionMarkerDefinition>) {
                v.position = t.apply(v.position);
                double s = t.apply_rotation(Vector3::UNIT_X).length();
                v.radius *= s;
                v.landing_gap *= s;
                if (v.embedded) {
                    auto &m = *v.embedded;
                    m.insertion = t.apply(m.insertion);
                    m.normal = t.apply_rotation(m.normal).normalize();
                    m.height *= s;
                    m.width *= s;
                }
            } else if constexpr (std::is_same_v<T, CoordinationModelDefinition>) {
                Matrix4 m;
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                        m.m[r][c] = v.transform[4 * r + c];
                m = t.matrix * m;
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                        v.transform[4 * r + c] = m.m[r][c];
            } else if constexpr (std::is_same_v<T, PointCloudDefinition> ||
                                 std::is_same_v<T, PointCloudExDefinition>) {
                if constexpr (std::is_same_v<T, PointCloudDefinition>) {
                    v.origin = t.apply(v.origin);
                    auto p = corners(v.extents_min, v.extents_max);
                    for (auto &x : p)
                        x = t.apply(x);
                    auto b = *BoundingBox3D::from_points(p);
                    v.extents_min = b.min;
                    v.extents_max = b.max;
                }
                v.ucs_origin = t.apply(v.ucs_origin);
                v.ucs_x = t.apply_rotation(v.ucs_x);
                v.ucs_y = t.apply_rotation(v.ucs_y);
                v.ucs_z = t.apply_rotation(v.ucs_z);
            }
        },
        result);
    return result;
}
std::array<Point3, 8> point_cloud_world_corners(const PointCloudExDefinition &v) {
    std::array<Point3, 8> out;
    auto p = corners(v.extents_min, v.extents_max);
    for (int i = 0; i < 8; ++i)
        out[i] = v.ucs_origin + v.ucs_x * p[i].x + v.ucs_y * p[i].y + v.ucs_z * p[i].z;
    return out;
}
BoundingBox3D extended_bounding_box(const ExtendedEntityDefinition &value) {
    return std::visit(
        [](const auto &v) -> BoundingBox3D {
            using T = std::decay_t<decltype(v)>;
            std::vector<Point3> p;
            if constexpr (std::is_same_v<T, OleFrameDefinition>)
                p = {v.upper_left, v.lower_right};
            else if constexpr (std::is_same_v<T, LightDefinition> ||
                               std::is_same_v<T, RemoteTextDefinition> ||
                               std::is_same_v<T, PositionMarkerDefinition>)
                p = {v.position};
            else if constexpr (std::is_same_v<T, ArcAlignedTextDefinition>)
                p = {v.center};
            else if constexpr (std::is_same_v<T, SectionSymbolDefinition>)
                for (auto &x : v.points)
                    p.push_back(x.position);
            else if constexpr (std::is_same_v<T, SectionObjectDefinition>) {
                p = v.vertices;
                p.insert(p.end(), v.back_vertices.begin(), v.back_vertices.end());
            } else if constexpr (std::is_same_v<T, ViewBorderDefinition>)
                return {{v.min.x, v.min.y, 0}, {v.max.x, v.max.y, 0}};
            else if constexpr (std::is_same_v<T, PointCloudDefinition>)
                p = {v.extents_min, v.extents_max};
            else if constexpr (std::is_same_v<T, PointCloudExDefinition>) {
                auto a = point_cloud_world_corners(v);
                p.assign(a.begin(), a.end());
            }
            return BoundingBox3D::from_points(p).value_or(BoundingBox3D{});
        },
        value);
}
} // namespace libopencad
