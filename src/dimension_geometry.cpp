// SPDX-License-Identifier: MPL-2.0
// Port of acadrust entities/dimension.rs and modification/{translate,transform}.rs.
#include <libopencad/dxf_document.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace libopencad {
namespace {
constexpr double pi = 3.1415926535897932384626433832795, tau = 2 * pi,
                 eps = std::numeric_limits<double>::epsilon();
double wrap(double x) {
    auto r = std::fmod(x, tau);
    return r < 0 ? r + tau : r;
}
bool finite(const Transform &t) {
    for (auto &row : t.matrix.m)
        for (auto x : row)
            if (!std::isfinite(x))
                return false;
    return true;
}
void validate(const Transform &t) {
    if (!finite(t))
        throw std::invalid_argument("Nonfinite dimension transform");
}
Vector3 normal_transform(const Transform &t, Vector3 n) {
    if (auto inv = t.matrix.to_matrix3().inverse()) {
        auto v = inv->transpose() * n;
        double length = v.length();
        if (length >= 1e-10)
            return v * (1. / length);
    }
    return n;
}
double angle_transform(Vector3 old, Vector3 next, double a, const Transform &t) {
    auto v = Matrix3::arbitrary_axis(old) * Vector3{std::cos(a), std::sin(a), 0};
    v = Matrix3::arbitrary_axis(next).transpose() * t.apply_rotation(v);
    return v.length_squared() > 1e-24 ? std::atan2(v.y, v.x) : a;
}
double minor(Vector3 a, Vector3 b) {
    auto x = a.length(), y = b.length();
    if (x <= eps || y <= eps)
        return 0;
    return std::acos(std::clamp(a.dot(b) / (x * y), -1., 1.));
}
std::optional<Vector3> intersection(Vector3 p, Vector3 a, Vector3 q, Vector3 b) {
    auto c = a.cross(b);
    double den = c.length_squared(), scale = a.length_squared() * b.length_squared();
    if (!std::isfinite(den) || den <= scale * 1e-24)
        return {};
    auto offset = q - p;
    auto first = p + a * (offset.cross(b).dot(c) / den), second = q + b * (offset.cross(a).dot(c) / den);
    scale = std::max({first.length(), second.length(), 1.});
    if ((first - second).length() <= scale * 1e-9)
        return (first + second) * .5;
    return {};
}
std::optional<double> sector(Vector3 vertex, Vector3 first, Vector3 second, Vector3 arc, Vector3 preferred) {
    if (first.length_squared() <= eps || second.length_squared() <= eps)
        return {};
    auto direction = arc - vertex;
    if (direction.length_squared() <= eps)
        return {};
    auto cross = first.cross(second);
    auto scale = preferred.length() * std::max(first.length(), second.length());
    Vector3 normal;
    if (preferred.length_squared() > eps && std::abs(preferred.dot(first)) <= scale * 1e-9 &&
        std::abs(preferred.dot(second)) <= scale * 1e-9)
        normal = preferred.normalize();
    else if (cross.length_squared() > eps)
        normal = cross.normalize();
    else
        return {};
    auto x = first.normalize(), y = normal.cross(x).normalize();
    auto angle = [&](Vector3 v) { return wrap(std::atan2(v.dot(y), v.dot(x))); };
    double a = angle(first), b = angle(second), selected = angle(direction);
    double limits[] = {a, wrap(a + pi), b, wrap(b + pi)};
    std::sort(std::begin(limits), std::end(limits));
    for (int i = 0; i < 4; ++i) {
        double start = limits[i], end = i < 3 ? limits[i + 1] : limits[0] + tau,
               v = selected + 1e-12 < start ? selected + tau : selected;
        if (v >= start - 1e-12 && v <= end + 1e-12)
            return std::clamp(end - start, 0., pi);
    }
    return {};
}
double stored(const DimensionDefinition &d) {
    return std::visit(
        [&](const auto &g) -> double {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, LinearDimension>) {
                auto v = Matrix3::arbitrary_axis(d.normal).transpose() * (g.second_point - g.first_point);
                auto n = v.x * std::cos(g.rotation) + v.y * std::sin(g.rotation);
                return std::isfinite(n) ? std::abs(n) : 0.;
            } else if constexpr (std::is_same_v<T, AlignedDimension>)
                return (g.second_point - g.first_point).length();
            else if constexpr (std::is_same_v<T, RadiusDimension> || std::is_same_v<T, LargeRadialDimension>)
                return (g.chord_point - d.definition_point).length();
            else if constexpr (std::is_same_v<T, DiameterDimension>)
                return (g.angle_vertex - d.definition_point).length();
            else if constexpr (std::is_same_v<T, OrdinateDimension>) {
                auto v =
                    Matrix3::arbitrary_axis(d.normal).transpose() * (g.feature_location - d.definition_point);
                double a = -d.horizontal_direction;
                return std::abs(g.is_x ? v.x * std::cos(a) + v.y * std::sin(a)
                                       : -v.x * std::sin(a) + v.y * std::cos(a));
            } else if constexpr (std::is_same_v<T, Angular2LineDimension>) {
                auto a = g.second_point - g.first_point, b = d.definition_point - g.angle_vertex;
                auto v = intersection(g.first_point, a, g.angle_vertex, b);
                return v ? sector(*v, a, b, g.dimension_arc, d.normal).value_or(minor(a, b)) : minor(a, b);
            } else if constexpr (std::is_same_v<T, Angular3PointDimension>) {
                auto a = g.first_point - g.angle_vertex, b = g.second_point - g.angle_vertex;
                return sector(g.angle_vertex, a, b, d.definition_point, d.normal).value_or(minor(a, b));
            } else {
                double r = (g.first_extension_point - g.center_point).length(),
                       raw = g.end_parameter - g.start_parameter, sweep = wrap(raw);
                if (sweep <= 1e-12 && std::abs(raw) > 1e-12)
                    sweep = tau;
                if (sweep <= 1e-12 && std::abs(raw) <= 1e-12) {
                    auto a = g.first_extension_point - g.center_point,
                         b = g.second_extension_point - g.center_point;
                    sweep = wrap(std::atan2(b.y, b.x) - std::atan2(a.y, a.x));
                }
                return r * sweep;
            }
        },
        d.geometry);
}
template <class Fn> void points(DimensionDefinition &d, Fn f) {
    f(d.definition_point);
    f(d.text_middle_point);
    std::visit(
        [&](auto &g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, LinearDimension> || std::is_same_v<T, AlignedDimension>) {
                f(g.first_point);
                f(g.second_point);
            } else if constexpr (std::is_same_v<T, RadiusDimension>)
                f(g.chord_point);
            else if constexpr (std::is_same_v<T, DiameterDimension>)
                f(g.angle_vertex);
            else if constexpr (std::is_same_v<T, Angular2LineDimension> ||
                               std::is_same_v<T, Angular3PointDimension>) {
                f(g.first_point);
                f(g.second_point);
                f(g.angle_vertex);
                if constexpr (std::is_same_v<T, Angular2LineDimension>)
                    f(g.dimension_arc);
            } else if constexpr (std::is_same_v<T, OrdinateDimension>) {
                f(g.feature_location);
                f(g.leader_endpoint);
            } else if constexpr (std::is_same_v<T, ArcDimension>) {
                f(g.first_extension_point);
                f(g.second_extension_point);
                f(g.center_point);
                f(g.first_leader_point);
                f(g.second_leader_point);
            } else {
                f(g.chord_point);
                f(g.override_center);
                f(g.jog_point);
            }
        },
        d.geometry);
}
void transform(DimensionDefinition &d, const Transform &t) {
    auto old = d.normal, next = normal_transform(t, old);
    const bool same_plane = (old - next).length_squared() < 1e-18;
    auto ocs = Matrix3::arbitrary_axis(old), inverse = Matrix3::arbitrary_axis(next).transpose();
    if (std::holds_alternative<OrdinateDimension>(d.geometry)) {
        auto angle = -d.horizontal_direction;
        auto axis = ocs * Vector3{std::cos(angle), std::sin(angle), 0};
        auto text = ocs * Vector3{std::cos(angle + d.text_rotation), std::sin(angle + d.text_rotation), 0};
        axis = inverse * t.apply_rotation(axis);
        text = inverse * t.apply_rotation(text);
        if (!same_plane && axis.length_squared() > 1e-24) {
            angle = std::atan2(axis.y, axis.x);
            d.horizontal_direction = -angle;
        }
        if (!same_plane && text.length_squared() > 1e-24) {
            auto relative = std::atan2(text.y, text.x) - angle;
            d.text_rotation = std::atan2(std::sin(relative), std::cos(relative));
        }
    } else {
        if (t.matrix.to_matrix3().determinant() >= 0 && !same_plane) {
            if (d.text_rotation != 0)
                d.text_rotation = angle_transform(old, next, d.text_rotation, t);
            d.horizontal_direction = angle_transform(old, next, d.horizontal_direction, t);
        }
        d.insertion_rotation = angle_transform(old, next, d.insertion_rotation, t);
        if (auto g = std::get_if<LinearDimension>(&d.geometry)) {
            g->rotation = angle_transform(old, next, g->rotation, t);
            if (g->ext_line_rotation != 0)
                g->ext_line_rotation = angle_transform(old, next, g->ext_line_rotation, t);
        }
        auto scale = t.apply_rotation(Vector3::UNIT_X).length();
        if (auto g = std::get_if<RadiusDimension>(&d.geometry))
            g->leader_length *= scale;
        if (auto g = std::get_if<DiameterDimension>(&d.geometry))
            g->leader_length *= scale;
    }
    points(d, [&](auto &p) { p = t.apply(p); });
    d.insertion_point = inverse * t.apply(ocs * d.insertion_point);
    d.normal = next;
    d.actual_measurement = stored(d);
}
} // namespace
void DxfDimension::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    auto d = definition();
    points(d, [&](auto &p) { p = p + offset; });
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfDimension::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto d = definition();
    transform(d, t);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfDimension::apply_mirror(const Transform &t) {
    if (!finite(t) || t.is_identity())
        return;
    apply_transform(t);
}
double DxfDimension::stored_measurement() const { return stored(definition()); }
double DxfDimension::measurement() const {
    auto d = definition();
    double n = stored(d);
    return std::holds_alternative<Angular2LineDimension>(d.geometry) ||
                   std::holds_alternative<Angular3PointDimension>(d.geometry)
               ? n * 180. / pi
               : n;
}
void DxfDimension::refresh_measurement() {
    auto d = definition();
    d.actual_measurement = stored(d);
    set_definition(d);
}
BoundingBox3D DxfDimension::bounding_box() const {
    auto d = definition();
    std::vector<Point3> p;
    points(d, [&](auto &v) { p.push_back(v); });
    p.erase(p.begin() + 1);
    if (std::holds_alternative<OrdinateDimension>(d.geometry))
        p.erase(p.begin());
    return BoundingBox3D::from_points(p).value_or(BoundingBox3D{});
}
} // namespace libopencad
