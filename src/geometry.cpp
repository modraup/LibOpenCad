// SPDX-License-Identifier: MPL-2.0
// Port of acadrust 99d5f488 types/bounds.rs and entities/{line,circle,arc,ellipse,lwpolyline}.rs.
#include <libopencad/bounds.hpp>
#include <libopencad/dxf_document.hpp>
#include <libopencad/transform.hpp>
#include <cmath>
#include <limits>
#include <ostream>
namespace libopencad {
namespace {
constexpr double pi = 3.14159265358979323846264338327950288;
Vector3 minimum(Vector3 a, Vector3 b) {
    return {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)};
}
Vector3 maximum(Vector3 a, Vector3 b) {
    return {std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)};
}
} // namespace
std::optional<BoundingBox3D> BoundingBox3D::from_points(const std::vector<Vector3> &points) {
    if (points.empty())
        return std::nullopt;
    auto box = from_point(points.front());
    for (std::size_t i = 1; i < points.size(); ++i)
        box.expand_to_include(points[i]);
    return box;
}
bool BoundingBox3D::contains(Vector3 p) const noexcept {
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
}
void BoundingBox3D::expand_to_include(Vector3 p) noexcept {
    min = minimum(min, p);
    max = maximum(max, p);
}
BoundingBox3D BoundingBox3D::merge(const BoundingBox3D &b) const noexcept {
    return {minimum(min, b.min), maximum(max, b.max)};
}
BoundingBox3D BoundingBox3D::ocs_to_wcs(Vector3 normal) const {
    if (normal == Vector3::UNIT_Z)
        return *this;
    auto matrix = Matrix3::arbitrary_axis(normal);
    auto inf = std::numeric_limits<double>::infinity();
    BoundingBox3D box{{inf, inf, inf}, {-inf, -inf, -inf}};
    for (double x : {min.x, max.x})
        for (double y : {min.y, max.y})
            for (double z : {min.z, max.z})
                box.expand_to_include(matrix.transform_point({x, y, z}));
    return box;
}
double DxfLine::length() const { return start().distance(end()); }
Vector3 DxfLine::direction() const { return (end() - start()).normalize(); }
Vector3 DxfLine::midpoint() const { return (start() + end()) / 2.; }
BoundingBox3D DxfLine::bounding_box() const { return *BoundingBox3D::from_points({start(), end()}); }
double DxfCircle::diameter() const { return radius() * 2.; }
double DxfCircle::circumference() const { return 2. * pi * radius(); }
double DxfCircle::area() const { return pi * radius() * radius(); }
Vector3 DxfCircle::center_wcs() const { return Matrix3::arbitrary_axis(normal()) * center(); }
std::pair<Vector3, Vector3> DxfCircle::axes_wcs() const {
    auto m = Matrix3::arbitrary_axis(normal());
    return {m * Vector3::UNIT_X, m * Vector3::UNIT_Y};
}
Vector3 DxfCircle::point_at_angle_wcs(double angle) const {
    auto [x, y] = axes_wcs();
    return center_wcs() + x * (radius() * std::cos(angle)) + y * (radius() * std::sin(angle));
}
BoundingBox3D DxfCircle::bounding_box() const {
    auto c = center_wcs();
    auto [x, y] = axes_wcs();
    Vector3 extent{radius() * std::hypot(x.x, y.x), radius() * std::hypot(x.y, y.y),
                   radius() * std::hypot(x.z, y.z)};
    auto opposite = c + normal().normalize() * thickness();
    return {minimum(c - extent, opposite - extent), maximum(c + extent, opposite + extent)};
}
double DxfArc::sweep_angle() const {
    auto sweep = end_angle_radians() - start_angle_radians();
    if (sweep < 0)
        sweep += 2. * pi;
    return sweep;
}
double DxfArc::arc_length() const { return radius() * sweep_angle(); }
Vector3 DxfArc::start_point() const {
    auto a = start_angle_radians();
    auto c = center();
    return {c.x + radius() * std::cos(a), c.y + radius() * std::sin(a), c.z};
}
Vector3 DxfArc::end_point() const {
    auto a = end_angle_radians();
    auto c = center();
    return {c.x + radius() * std::cos(a), c.y + radius() * std::sin(a), c.z};
}
Vector3 DxfArc::midpoint() const {
    auto a = start_angle_radians() + sweep_angle() / 2.;
    auto c = center();
    return {c.x + radius() * std::cos(a), c.y + radius() * std::sin(a), c.z};
}
Vector3 DxfArc::point_at_angle_wcs(double angle) const {
    auto local = center() + Vector3{radius() * std::cos(angle), radius() * std::sin(angle), 0};
    return Matrix3::arbitrary_axis(normal()) * local;
}
Vector3 DxfArc::start_point_wcs() const { return point_at_angle_wcs(start_angle_radians()); }
Vector3 DxfArc::end_point_wcs() const { return point_at_angle_wcs(end_angle_radians()); }
Vector3 DxfArc::midpoint_wcs() const {
    return point_at_angle_wcs(start_angle_radians() + sweep_angle() * 0.5);
}
BoundingBox3D DxfArc::bounding_box() const {
    auto basis = Matrix3::arbitrary_axis(normal());
    auto x = basis * Vector3::UNIT_X, y = basis * Vector3::UNIT_Y;
    std::vector<double> angles{start_angle_radians(), end_angle_radians()};
    auto sweep = sweep_angle();
    for (double a : {std::atan2(y.x, x.x), std::atan2(y.y, x.y), std::atan2(y.z, x.z)}) {
        for (double candidate : {a, a + pi}) {
            // Rust rem_euclid: adjust the signed remainder once, with the same tolerance.
            auto delta = std::fmod(candidate - start_angle_radians(), 2. * pi);
            if (delta < 0)
                delta += 2. * pi;
            if (sweep >= 2. * pi - 1e-12 || delta <= sweep + 1e-12)
                angles.push_back(candidate);
        }
    }
    std::vector<Vector3> points;
    auto n = normal().normalize();
    for (double a : angles) {
        auto p = point_at_angle_wcs(a);
        points.push_back(p);
        if (thickness() != 0)
            points.push_back(p + n * thickness());
    }
    return BoundingBox3D::from_points(points).value_or(BoundingBox3D{});
}
double DxfEllipse::major_axis_length() const { return major_axis().length(); }
double DxfEllipse::minor_axis_length() const { return major_axis_length() * minor_axis_ratio(); }
bool DxfEllipse::is_full() const { return std::abs(end_parameter() - start_parameter() - 2. * pi) < 1e-10; }
BoundingBox3D DxfEllipse::bounding_box() const {
    // Preserve upstream's simplified XY extent; it does not evaluate normal or parameter extrema.
    auto r = std::fmax(major_axis_length(), minor_axis_length());
    auto c = center();
    return {{c.x - r, c.y - r, c.z}, {c.x + r, c.y + r, c.z}};
}
std::size_t DxfLwPolyline::vertex_count() const { return vertices().size(); }
BoundingBox3D DxfLwPolyline::bounding_box() const {
    // Upstream uses vertex extrema only: bulge, widths and thickness do not expand this box.
    auto vs = vertices();
    if (vs.empty())
        return BoundingBox3D::from_point(Vector3::ZERO);
    std::vector<Vector3> points;
    points.reserve(vs.size());
    const auto z = elevation();
    for (auto v : vs)
        points.push_back({v.x, v.y, z});
    return BoundingBox3D::from_points(points)->ocs_to_wcs(normal());
}
std::ostream &operator<<(std::ostream &out, const BoundingBox3D &box) {
    return out << "BBox3D[" << box.min << " -> " << box.max << ']';
}
} // namespace libopencad
