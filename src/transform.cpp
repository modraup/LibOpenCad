// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/types/transform.rs.
#include <libopencad/transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace libopencad {
namespace {
template <class Matrix> Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix result = Matrix::zero();
    for (std::size_t i = 0; i < result.m.size(); ++i)
        for (std::size_t j = 0; j < result.m.size(); ++j)
            for (std::size_t k = 0; k < result.m.size(); ++k)
                result.m[i][j] += a.m[i][k] * b.m[k][j];
    return result;
}

template <class Matrix> Matrix transposed(const Matrix& a) {
    Matrix result;
    for (std::size_t i = 0; i < a.m.size(); ++i)
        for (std::size_t j = 0; j < a.m.size(); ++j)
            result.m[i][j] = a.m[j][i];
    return result;
}

template <class Row> double dot_row(const Row& r, Vector3 v) {
    return r[0] * v.x + r[1] * v.y + r[2] * v.z;
}

bool finite(Vector3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
double max_abs(Vector3 v) { return std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)}); }
} // namespace

Matrix3 Matrix3::zero() { return from_rows({}, {}, {}); }
Matrix3 Matrix3::scaling(double sx, double sy, double sz) {
    return from_rows({sx, 0, 0}, {0, sy, 0}, {0, 0, sz});
}
Matrix3 Matrix3::rotation_z(double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return from_rows({c, -s, 0}, {s, c, 0}, {0, 0, 1});
}
Matrix3 Matrix3::arbitrary_axis(Vector3 normal) {
    const auto n = normal.normalize();
    const auto ref =
        std::abs(n.x) < 1.0 / 64 && std::abs(n.y) < 1.0 / 64 ? Vector3::UNIT_Y : Vector3::UNIT_Z;
    const auto x = ref.cross(n).normalize();
    const auto y = n.cross(x).normalize();
    return from_rows({x.x, y.x, n.x}, {x.y, y.y, n.y}, {x.z, y.z, n.z});
}
Matrix3 Matrix3::transpose() const { return transposed(*this); }
Matrix3 Matrix3::operator*(const Matrix3& rhs) const { return multiply(*this, rhs); }
Vector3 Matrix3::transform_point(Vector3 v) const {
    return {dot_row(m[0], v), dot_row(m[1], v), dot_row(m[2], v)};
}
double Matrix3::determinant() const {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}
std::optional<Matrix3> Matrix3::inverse() const {
    const double det = determinant();
    if (std::abs(det) < 1e-10)
        return std::nullopt;
    const double inv_det = 1.0 / det;
    // Columns of the inverse are the cross products of successive rows / det.
    const Vector3 a{m[0][0], m[0][1], m[0][2]}, b{m[1][0], m[1][1], m[1][2]},
        c{m[2][0], m[2][1], m[2][2]};
    const auto x = b.cross(c) * inv_det, y = c.cross(a) * inv_det, z = a.cross(b) * inv_det;
    return from_rows({x.x, y.x, z.x}, {x.y, y.y, z.y}, {x.z, y.z, z.z});
}

Matrix4 Matrix4::zero() { return {std::array<Row, 4>{}}; }
Matrix4 Matrix4::translation(double tx, double ty, double tz) {
    Matrix4 result;
    result.m[0][3] = tx;
    result.m[1][3] = ty;
    result.m[2][3] = tz;
    return result;
}
Matrix4 Matrix4::scaling(double sx, double sy, double sz) {
    return from_matrix3(Matrix3::scaling(sx, sy, sz));
}
Matrix4 Matrix4::scaling_with_origin(double sx, double sy, double sz, Vector3 origin) {
    return translation(origin.x, origin.y, origin.z) * scaling(sx, sy, sz) *
           translation(-origin.x, -origin.y, -origin.z);
}
Matrix4 Matrix4::rotation_x(double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return from_matrix3(Matrix3::from_rows({1, 0, 0}, {0, c, -s}, {0, s, c}));
}
Matrix4 Matrix4::rotation_y(double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return from_matrix3(Matrix3::from_rows({c, 0, s}, {0, 1, 0}, {-s, 0, c}));
}
Matrix4 Matrix4::rotation_z(double angle) { return from_matrix3(Matrix3::rotation_z(angle)); }
Matrix4 Matrix4::rotation(Vector3 axis, double angle) {
    const auto [x, y, z] = axis.normalize();
    const double c = std::cos(angle), s = std::sin(angle), t = 1.0 - c;
    return from_matrix3(Matrix3::from_rows({c + x * x * t, x * y * t - z * s, x * z * t + y * s},
                                           {y * x * t + z * s, c + y * y * t, y * z * t - x * s},
                                           {z * x * t - y * s, z * y * t + x * s, c + z * z * t}));
}
Matrix4 Matrix4::from_matrix3(const Matrix3& linear) {
    Matrix4 result;
    for (int i = 0; i < 3; ++i)
        std::copy(linear.m[i].begin(), linear.m[i].end(), result.m[i].begin());
    return result;
}
Matrix3 Matrix4::to_matrix3() const {
    Matrix3 result;
    for (int i = 0; i < 3; ++i)
        std::copy_n(m[i].begin(), 3, result.m[i].begin());
    return result;
}
Matrix4 Matrix4::transpose() const { return transposed(*this); }
Matrix4 Matrix4::operator*(const Matrix4& rhs) const { return multiply(*this, rhs); }
Vector3 Matrix4::transform_point(Vector3 v) const {
    const double w = dot_row(m[3], v) + m[3][3];
    const double divisor = std::abs(w) < 1e-10 ? 1.0 : w;
    return {(dot_row(m[0], v) + m[0][3]) / divisor, (dot_row(m[1], v) + m[1][3]) / divisor,
            (dot_row(m[2], v) + m[2][3]) / divisor};
}
Vector3 Matrix4::transform_direction(Vector3 v) const {
    return {dot_row(m[0], v), dot_row(m[1], v), dot_row(m[2], v)};
}

Transform Transform::from_rotation_with_origin(Vector3 axis, double angle, Vector3 origin) {
    if (!finite(axis) || !std::isfinite(angle) || !finite(origin) ||
        axis.length_squared() <= std::numeric_limits<double>::epsilon())
        return identity();
    return from_translation(-origin).then(from_rotation(axis, angle)).then(from_translation(origin));
}
Transform Transform::from_mirror_plane(Vector3 point, Vector3 normal) {
    if (!finite(point) || !finite(normal))
        return identity();
    const double magnitude = max_abs(normal);
    if (magnitude == 0)
        return identity();
    const auto scaled = normal / magnitude;
    const auto n = scaled * (1.0 / scaled.length());
    const double point_scale = max_abs(point);
    const double projection = point_scale > 0 ? n.dot(point / point_scale) : 0;
    const auto offset = (n * 2.0 * projection) * point_scale;
    if (!finite(offset))
        return identity();
    const double x = n.x, y = n.y, z = n.z;
    auto mirror = Matrix4::from_matrix3(Matrix3::from_rows({1 - 2 * x * x, -2 * x * y, -2 * x * z},
                                                           {-2 * y * x, 1 - 2 * y * y, -2 * y * z},
                                                           {-2 * z * x, -2 * z * y, 1 - 2 * z * z}));
    mirror.m[0][3] = offset.x;
    mirror.m[1][3] = offset.y;
    mirror.m[2][3] = offset.z;
    return {mirror};
}
Transform Transform::from_mirror_line(Vector3 p1, Vector3 p2) {
    if (!finite(p1) || !finite(p2))
        return identity();
    double dx = p2.x - p1.x, dy = p2.y - p1.y;
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        const double scale = std::max({std::abs(p1.x), std::abs(p1.y), std::abs(p2.x), std::abs(p2.y)});
        dx = p2.x / scale - p1.x / scale;
        dy = p2.y / scale - p1.y / scale;
    }
    return from_mirror_plane({p1.x, p1.y, 0}, {-dy, dx, 0});
}

MirrorPlane MirrorPlane::point_normal(Vector3 point, Vector3 normal) {
    MirrorPlane plane;
    plane.kind_ = Kind::PointNormal;
    plane.a_ = point;
    plane.b_ = normal;
    return plane;
}
MirrorPlane MirrorPlane::three_point(Vector3 first, Vector3 second, Vector3 third) {
    MirrorPlane plane;
    plane.kind_ = Kind::ThreePoint;
    plane.a_ = first;
    plane.b_ = second;
    plane.c_ = third;
    return plane;
}
MirrorPlane MirrorPlane::xy(Vector3 point) {
    MirrorPlane plane;
    plane.kind_ = Kind::XY;
    plane.a_ = point;
    return plane;
}
MirrorPlane MirrorPlane::yz(Vector3 point) {
    MirrorPlane plane;
    plane.kind_ = Kind::YZ;
    plane.a_ = point;
    return plane;
}
MirrorPlane MirrorPlane::zx(Vector3 point) {
    MirrorPlane plane;
    plane.kind_ = Kind::ZX;
    plane.a_ = point;
    return plane;
}
Transform MirrorPlane::transform() const {
    auto direction = [](Vector3 first, Vector3 end) -> std::optional<Vector3> {
        if (!finite(first) || !finite(end))
            return std::nullopt;
        auto delta = end - first;
        if (!finite(delta)) {
            const double scale =
                std::max({std::abs(first.x), std::abs(first.y), std::abs(first.z), std::abs(end.x),
                          std::abs(end.y), std::abs(end.z)});
            if (scale == 0)
                return std::nullopt;
            delta = Vector3{end.x / scale - first.x / scale, end.y / scale - first.y / scale,
                            end.z / scale - first.z / scale};
        }
        const double scale = max_abs(delta);
        if (scale <= 0)
            return std::nullopt;
        return Vector3{delta.x / scale, delta.y / scale, delta.z / scale};
    };
    Vector3 point = a_, normal = Vector3::UNIT_Z;
    switch (kind_) {
    case Kind::PointNormal:
        normal = b_;
        break;
    case Kind::ThreePoint: {
        auto a = direction(a_, b_), b = direction(a_, c_);
        if (!a || !b)
            return Transform::identity();
        point = a_;
        normal = a->cross(*b);
        break;
    }
    case Kind::XY:
        normal = Vector3::UNIT_Z;
        break;
    case Kind::YZ:
        normal = Vector3::UNIT_X;
        break;
    case Kind::ZX:
        normal = Vector3::UNIT_Y;
        break;
    }
    return Transform::from_mirror_plane(point, normal);
}
} // namespace libopencad
