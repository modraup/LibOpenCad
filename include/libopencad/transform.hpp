// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/types/transform.rs.
#pragma once

#include "vector.hpp"
#include <array>
#include <optional>

namespace libopencad {

// Row-major matrices act on column vectors. Angles are in radians.
struct Matrix3 {
    using Row = std::array<double, 3>;
    std::array<Row, 3> m{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

    static Matrix3 identity() { return {}; }
    static Matrix3 zero();
    static Matrix3 from_rows(Row a, Row b, Row c) { return {{a, b, c}}; }
    static Matrix3 rotation_z(double angle);
    static Matrix3 scaling(double sx, double sy, double sz);
    static Matrix3 arbitrary_axis(Vector3 normal);
    Matrix3 transpose() const;
    double determinant() const;
    std::optional<Matrix3> inverse() const;
    Vector3 transform_point(Vector3 v) const;
    Vector3 operator*(Vector3 v) const { return transform_point(v); }
    Matrix3 operator*(const Matrix3& rhs) const;
    bool operator==(const Matrix3& rhs) const { return m == rhs.m; }
    bool operator!=(const Matrix3& rhs) const { return !(*this == rhs); }
};

struct Matrix4 {
    using Row = std::array<double, 4>;
    std::array<Row, 4> m{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};

    static Matrix4 identity() { return {}; }
    static Matrix4 zero();
    static Matrix4 translation(double tx, double ty, double tz);
    static Matrix4 scaling(double sx, double sy, double sz);
    static Matrix4 scaling_with_origin(double sx, double sy, double sz, Vector3 origin);
    static Matrix4 rotation_x(double angle);
    static Matrix4 rotation_y(double angle);
    static Matrix4 rotation_z(double angle);
    static Matrix4 rotation(Vector3 axis, double angle);
    static Matrix4 from_matrix3(const Matrix3& linear);
    Matrix3 to_matrix3() const;
    Matrix4 transpose() const;
    Vector3 transform_point(Vector3 v) const;
    Vector3 transform_direction(Vector3 v) const;
    Matrix4 operator*(const Matrix4& rhs) const;
    bool operator==(const Matrix4& rhs) const { return m == rhs.m; }
    bool operator!=(const Matrix4& rhs) const { return !(*this == rhs); }
};

struct Transform {
    Matrix4 matrix;
    static Transform identity() { return {}; }
    static Transform from_matrix(const Matrix4& m) { return {m}; }
    static Transform from_translation(Vector3 t) { return {Matrix4::translation(t.x, t.y, t.z)}; }
    static Transform from_scale(double s) { return {Matrix4::scaling(s, s, s)}; }
    static Transform from_scaling(Vector3 s) { return {Matrix4::scaling(s.x, s.y, s.z)}; }
    static Transform from_scaling_with_origin(Vector3 s, Vector3 origin) {
        return {Matrix4::scaling_with_origin(s.x, s.y, s.z, origin)};
    }
    static Transform from_rotation(Vector3 axis, double angle) {
        return {Matrix4::rotation(axis, angle)};
    }
    static Transform from_rotation_with_origin(Vector3 axis, double angle, Vector3 origin);
    static Transform from_mirror_x() { return from_scaling({-1, 1, 1}); }
    static Transform from_mirror_y() { return from_scaling({1, -1, 1}); }
    static Transform from_mirror_z() { return from_scaling({1, 1, -1}); }
    static Transform from_mirror_plane(Vector3 point, Vector3 normal);
    static Transform from_mirror_line(Vector3 p1, Vector3 p2);

    Vector3 apply(Vector3 point) const { return matrix.transform_point(point); }
    // The upstream name includes the entire linear part, including scale.
    Vector3 apply_rotation(Vector3 direction) const { return matrix.transform_direction(direction); }
    Transform then(const Transform& next) const { return {next.matrix * matrix}; }
    Transform compose(const Transform& first) const { return {matrix * first.matrix}; }
    Transform operator*(const Transform& rhs) const { return compose(rhs); }
    bool is_identity() const { return matrix == Matrix4::identity(); }
    bool operator==(const Transform& rhs) const { return matrix == rhs.matrix; }
    bool operator!=(const Transform& rhs) const { return !(*this == rhs); }
};

} // namespace libopencad
