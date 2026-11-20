// SPDX-License-Identifier: MPL-2.0
// Port of acadrust entities/modification/{translate,transform,mirror} and xdata/mod.rs.
#include <libopencad/dxf_document.hpp>
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace libopencad {
namespace {
constexpr double tau = 6.283185307179586476925286766559;
bool finite(const Transform &t) {
    for (auto row : t.matrix.m)
        for (auto v : row)
            if (!std::isfinite(v))
                return false;
    return true;
}
void validate(const Transform &t) {
    if (!finite(t))
        throw std::invalid_argument("Non-finite entity transform");
}
Vector3 offset_ocs(Vector3 n, Vector3 o) { return Matrix3::arbitrary_axis(n).transpose() * o; }
double scale(const Transform &t) { return t.apply_rotation(Vector3::UNIT_X).length(); }
bool reflecting(const Transform &t) { return t.matrix.to_matrix3().determinant() < 0; }
double rem(double x) {
    auto r = std::fmod(x, tau);
    return r < 0 ? r + tau : r;
}
} // namespace
void DxfEntity::transform_extended_data(const Transform &t) {
    const auto factor = std::cbrt(std::abs(t.matrix.to_matrix3().determinant()));
    for (auto record : extended_data()) {
        bool changed = false;
        for (auto &value : record.values) {
            auto before = value.value;
            if (value.code == 1011)
                value.value = t.apply(std::get<Point3>(before));
            else if (value.code == 1012)
                value.value = t.apply_rotation(std::get<Point3>(before));
            else if (value.code == 1013) {
                auto p = t.apply_rotation(std::get<Point3>(before));
                if (p.length() > std::numeric_limits<double>::epsilon())
                    value.value = p.normalize();
            } else if (value.code == 1041 || value.code == 1042)
                value.value = std::get<double>(before) * factor;
            changed |= value.value != before;
        }
        if (changed)
            upsert_extended_data(record);
    }
}
void DxfLine::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    e.set_start(start() + offset);
    e.set_end(end() + offset);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfCircle::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    e.set_center(center() + offset_ocs(normal(), offset));
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfArc::translate(Vector3 offset) { DxfCircle::translate(offset); }
void DxfEllipse::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    e.set_center(center() + offset);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfLwPolyline::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    auto o = offset_ocs(normal(), offset);
    auto vs = vertices();
    for (auto &v : vs) {
        v.x += o.x;
        v.y += o.y;
    }
    e.set_vertices(vs);
    e.set_elevation(elevation() + o.z);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfLine::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    e.set_start(t.apply(start()));
    e.set_end(t.apply(end()));
    e.set_normal(t.apply_rotation(normal()).normalize());
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfCircle::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto n = t.apply_rotation(normal()).normalize();
    e.set_center(Matrix3::arbitrary_axis(n).transpose() *
                 t.apply(Matrix3::arbitrary_axis(normal()) * center()));
    e.set_radius(radius() * scale(t));
    e.set_normal(n);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfArc::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto ocs = Matrix3::arbitrary_axis(normal());
    auto c = ocs * center();
    auto endpoint = [&](double a) {
        return c + ocs * Vector3{radius() * std::cos(a), radius() * std::sin(a), 0};
    };
    auto ws = t.apply(endpoint(start_angle_radians())), we = t.apply(endpoint(end_angle_radians()));
    auto nc = t.apply(c), nn = t.apply_rotation(normal()).normalize();
    auto inv = Matrix3::arbitrary_axis(nn).transpose();
    auto angle = [&](Vector3 p) {
        auto l = inv * (p - nc);
        return std::atan2(l.y, l.x);
    };
    auto a = angle(ws), b = angle(we);
    e.set_start_angle_radians(reflecting(t) ? b : a);
    e.set_end_angle_radians(reflecting(t) ? a : b);
    e.set_center(inv * nc);
    e.set_radius(radius() * scale(t));
    e.set_normal(nn);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfEllipse::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto fallback = [&] {
        e.set_center(t.apply(center()));
        e.set_major_axis(t.apply_rotation(major_axis()));
        e.set_normal(t.apply_rotation(normal()).normalize());
    };
    auto nl = normal().length();
    if (nl <= 1e-12 || major_axis().length() <= 1e-12)
        fallback();
    else {
        auto minor = (normal() * (1. / nl)).cross(major_axis()) * minor_axis_ratio();
        auto a = t.apply_rotation(major_axis()), b = t.apply_rotation(minor);
        auto theta = .5 * std::atan2(2. * a.dot(b), a.dot(a) - b.dot(b));
        auto major = a * std::cos(theta) + b * std::sin(theta);
        minor = a * (-std::sin(theta)) + b * std::cos(theta);
        auto cross = major.cross(minor);
        auto ml = major.length(), cl = cross.length();
        if (ml <= 1e-12 || cl <= 1e-12 * ml * ml)
            fallback();
        else {
            e.set_center(t.apply(center()));
            e.set_major_axis(major);
            e.set_normal(cross * (1. / cl));
            e.set_minor_axis_ratio(std::fmin(minor.length() / ml, 1.));
            if (!is_full()) {
                auto sweep = rem(end_parameter() - start_parameter());
                auto start = rem(start_parameter() - theta);
                e.set_start_parameter(start);
                e.set_end_parameter(start + (sweep <= 1e-12 ? tau : sweep));
            }
        }
    }
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfLwPolyline::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto nn = t.apply_rotation(normal()).normalize();
    auto ocs = Matrix3::arbitrary_axis(normal()), inv = Matrix3::arbitrary_axis(nn).transpose();
    auto z = elevation();
    auto plane = inv * t.apply(ocs * Vector3{0, 0, z});
    auto factor = scale(t);
    auto flip = reflecting(t);
    auto vs = vertices();
    for (auto &v : vs) {
        auto p = inv * t.apply(ocs * Vector3{v.x, v.y, z});
        v.x = p.x;
        v.y = p.y;
        v.start_width *= factor;
        v.end_width *= factor;
        if (flip)
            v.bulge = -v.bulge;
    }
    e.set_vertices(vs);
    e.set_constant_width(constant_width() * factor);
    e.set_elevation(plane.z);
    e.set_normal(nn);
    e.transform_extended_data(t);
    *this = std::move(e);
}

void DxfPolyline2D::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    auto o = offset_ocs(normal(), offset);
    for (std::size_t i = 0; i < vertex_count(); ++i)
        e.children_[i].set_point("AcDb2dVertex", 10, vertex(i).location + o);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfPolyline2D::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto n = t.apply_rotation(normal()).normalize();
    auto o = Matrix3::arbitrary_axis(normal()), inv = Matrix3::arbitrary_axis(n).transpose();
    auto factor = scale(t);
    auto flip = reflecting(t);
    e.set_default_widths(default_start_width() * factor, default_end_width() * factor);
    for (std::size_t i = 0; i < vertex_count(); ++i) {
        auto v = vertex(i);
        e.children_[i].set_point("AcDb2dVertex", 10, inv * t.apply(o * v.location));
        e.set_vertex_widths(i, v.start_width * factor, v.end_width * factor);
        if (flip)
            e.set_bulge(i, -v.bulge);
    }
    e.set_normal(n);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfPolyline3D::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    for (std::size_t i = 0; i < vertex_count(); ++i)
        e.set_vertex_position(i, vertex(i).position + offset);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfPolyline3D::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    for (std::size_t i = 0; i < vertex_count(); ++i)
        e.set_vertex_position(i, t.apply(vertex(i).position));
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfPolygonMesh::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    for (std::size_t i = 0; i < vertex_count(); ++i)
        e.set_vertex_position(i, vertex(i).position + offset);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfPolygonMesh::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    for (std::size_t i = 0; i < vertex_count(); ++i)
        e.set_vertex_position(i, t.apply(vertex(i).position));
    auto h = polyline_header();
    h.normal = t.apply_rotation(h.normal).normalize();
    e.set_polyline_header(h);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfPolygonMesh::apply_mirror(const Transform &t) {
    if (!finite(t) || t.is_identity())
        return;
    auto e = *this;
    e.apply_transform(t);
    auto m = m_vertex_count();
    if (reflecting(t) && m > 0)
        for (std::size_t i = 0; i + std::size_t(m) <= e.children_.size(); i += m)
            std::reverse(e.children_.begin() + i, e.children_.begin() + i + m);
    *this = std::move(e);
}
void DxfPolyfaceMesh::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    for (std::size_t i = 0; i < vertex_count(); ++i)
        e.set_vertex_position(i, vertex(i).position + offset);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfPolyfaceMesh::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    for (std::size_t i = 0; i < vertex_count(); ++i)
        e.set_vertex_position(i, t.apply(vertex(i).position));
    auto h = polyline_header();
    h.normal = t.apply_rotation(h.normal).normalize();
    e.set_polyline_header(h);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfPolyfaceMesh::apply_mirror(const Transform &t) {
    if (!finite(t) || t.is_identity())
        return;
    auto e = *this;
    e.apply_transform(t);
    if (reflecting(t))
        for (std::size_t i = 0; i < face_count(); ++i) {
            auto f = face(i);
            int n = f.is_triangle() ? 3 : 4;
            std::array<int, 4> reversed{};
            for (int j = 0; j < n; ++j) {
                auto k = n - 1 - j;
                auto value = std::abs(f.indices[k]);
                reversed[j] = f.indices[(k + n - 1) % n] < 0 ? -value : value;
            }
            e.set_face_indices(i, reversed);
        }
    *this = std::move(e);
}

void DxfPoint::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    e.set_location(location() + offset);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfPoint::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto n = t.apply_rotation(normal()).normalize();
    auto a = x_axis_angle();
    auto direction = Matrix3::arbitrary_axis(normal()) * Vector3{std::cos(a), std::sin(a), 0};
    auto transformed = Matrix3::arbitrary_axis(n).transpose() * t.apply_rotation(direction);
    if (transformed.x != 0 || transformed.y != 0)
        e.set_x_axis_angle(std::atan2(transformed.y, transformed.x));
    e.set_location(t.apply(location()));
    e.set_normal(n);
    e.transform_extended_data(t);
    *this = std::move(e);
}
#define RAY_OPERATIONS(Type)                                                                                 \
    void Type::translate(Vector3 offset) {                                                                   \
        auto t = Transform::from_translation(offset);                                                        \
        validate(t);                                                                                         \
        auto e = *this;                                                                                      \
        e.set_base_point(base_point() + offset);                                                             \
        e.transform_extended_data(t);                                                                        \
        *this = std::move(e);                                                                                \
    }                                                                                                        \
    void Type::apply_transform(const Transform &t) {                                                         \
        validate(t);                                                                                         \
        auto e = *this;                                                                                      \
        e.set_base_point(t.apply(base_point()));                                                             \
        e.set_direction(t.apply_rotation(direction()).normalize());                                          \
        e.transform_extended_data(t);                                                                        \
        *this = std::move(e);                                                                                \
    }
RAY_OPERATIONS(DxfRay)
RAY_OPERATIONS(DxfXLine)
#undef RAY_OPERATIONS
void DxfSolid::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    auto o = offset_ocs(normal(), offset);
    for (unsigned i = 0; i < 4; ++i)
        e.set_corner(i, corner(i) + o);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfSolid::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto n = t.apply_rotation(normal()).normalize();
    auto o = Matrix3::arbitrary_axis(normal()), inv = Matrix3::arbitrary_axis(n).transpose();
    for (unsigned i = 0; i < 4; ++i)
        e.set_corner(i, inv * t.apply(o * corner(i)));
    e.set_normal(n);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfFace3D::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    for (unsigned i = 0; i < 4; ++i)
        e.set_corner(i, corner(i) + offset);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfFace3D::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    for (unsigned i = 0; i < 4; ++i)
        e.set_corner(i, t.apply(corner(i)));
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfFace3D::apply_mirror(const Transform &t) {
    if (!finite(t) || t.is_identity())
        return;
    auto e = *this;
    e.apply_transform(t);
    if (reflecting(t)) {
        std::array<Vector3, 4> points{e.corner(0), e.corner(1), e.corner(2), e.corner(3)};
        for (unsigned i = 0; i < 4; ++i)
            e.set_corner(i, points[3 - i]);
        auto bits = e.invisible_edges();
        e.set_invisible_edges((bits & ~5) | ((bits & 1) << 2) | ((bits & 4) >> 2));
    }
    *this = std::move(e);
}
void DxfSpline::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    auto d = definition();
    for (auto &p : d.control_points)
        p = p + offset;
    for (auto &p : d.fit_points)
        p = p + offset;
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfSpline::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto d = definition();
    for (auto &p : d.control_points)
        p = t.apply(p);
    for (auto &p : d.fit_points)
        p = t.apply(p);
    d.begin_tangent = t.apply_rotation(d.begin_tangent);
    d.end_tangent = t.apply_rotation(d.end_tangent);
    std::array<Vector3, 3> axes{t.apply_rotation(Vector3::UNIT_X), t.apply_rotation(Vector3::UNIT_Y),
                                t.apply_rotation(Vector3::UNIT_Z)};
    double max_scale = 0, sum = 0;
    for (auto a : axes) {
        max_scale = std::fmax(max_scale, a.length());
        sum += a.length_squared();
    }
    bool orthogonal = true;
    for (auto ij : {std::pair<int, int>{0, 1}, {0, 2}, {1, 2}})
        orthogonal &= std::abs(axes[ij.first].dot(axes[ij.second])) <= 1e-12 * max_scale * max_scale;
    auto factor = orthogonal ? max_scale : std::sqrt(sum);
    d.control_tolerance *= factor;
    d.fit_tolerance *= factor;
    if (auto inverse = t.matrix.to_matrix3().inverse()) {
        auto n = inverse->transpose() * d.normal;
        auto len = n.length();
        if (!(len < 1e-10))
            d.normal = n * (1. / len);
    }
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfMesh::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    auto d = definition();
    d.translate(offset);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfMesh::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto d = definition();
    for (auto &v : d.vertices)
        v = t.apply(v);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
#define MIRROR(Type)                                                                                         \
    void Type::apply_mirror(const Transform &t) {                                                            \
        if (finite(t) && !t.is_identity())                                                                   \
            apply_transform(t);                                                                              \
    }
MIRROR(DxfLine)
MIRROR(DxfCircle)
MIRROR(DxfArc)
MIRROR(DxfEllipse)
MIRROR(DxfLwPolyline)
MIRROR(DxfPolyline2D)
MIRROR(DxfPolyline3D)
MIRROR(DxfPoint)
MIRROR(DxfRay)
MIRROR(DxfXLine)
MIRROR(DxfSolid)
MIRROR(DxfSpline)
MIRROR(DxfMesh)
#undef MIRROR
} // namespace libopencad
