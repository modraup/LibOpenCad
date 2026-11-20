// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <libopencad/transform.hpp>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
using namespace libopencad;
namespace {
constexpr double pi = 3.14159265358979323846264338327950288;
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
void point(std::ostream &o, Vector3 p) { o << ' ' << p.x << ' ' << p.y << ' ' << p.z; }
void bounds(std::ostream &o, BoundingBox3D b) {
    point(o, b.min);
    point(o, b.max);
}
void box_values(std::ostream &o, BoundingBox3D b) {
    bounds(o, b);
    o << ' ' << b.width() << ' ' << b.height() << ' ' << b.depth();
    point(o, b.center());
    o << ' ' << b.contains(b.min) << ' ' << b.contains(b.max) << ' ' << b.contains({1000, 1000, 1000});
    auto expanded = b;
    expanded.expand_to_include({12, -9, 5});
    bounds(o, expanded);
    bounds(o, b.merge({{-7, -8, -9}, {1, 2, 3}}));
    bounds(o, b.ocs_to_wcs({1, 2, 3}));
}
void contains_approximately(BoundingBox3D b, Vector3 p) {
    b.min = b.min - Vector3{1e-10, 1e-10, 1e-10};
    b.max = b.max + Vector3{1e-10, 1e-10, 1e-10};
    check(b.contains(p), "Circle/arc sampled point outside bounds");
}
std::string bytes(const DxfDocument &d) {
    std::ostringstream s;
    d.write(s);
    return s.str();
}
void known_results() {
    check(!BoundingBox3D::from_points({}), "Empty point set must be nullopt");
    check(BoundingBox3D{}.min == Vector3::ZERO && BoundingBox3D{}.max == Vector3::ZERO, "Default bounds");
    auto b = *BoundingBox3D::from_points({{1, 2, 3}, {-5, 4, -2}, {7, -3, 8}});
    check(b.min == Vector3{-5, -3, -2} && b.max == Vector3{7, 4, 8}, "Point extrema");
    check(b.width() == 12 && b.height() == 7 && b.depth() == 10 && b.center() == Vector3{1, 0.5, 3},
          "Dimensions");
    check(b.contains(b.min) && b.contains(b.max) && !b.contains({7.1, 4, 8}), "Inclusive bounds");
    auto flipped = b.ocs_to_wcs({0, 0, -1});
    check(flipped.min == Vector3{-7, -3, -8} && flipped.max == Vector3{5, 4, 2}, "Mirrored OCS box");
    BoundingBox3D inverted{{3, 2, 1}, {-1, -2, -3}};
    check(inverted.ocs_to_wcs(Vector3::UNIT_Z).min == inverted.min && inverted.width() == -4,
          "Do not reorder input bounds");
    auto nan = std::numeric_limits<double>::quiet_NaN();
    auto finite = *BoundingBox3D::from_points({{nan, 1, 2}, {3, 4, 5}});
    check(finite.min == Vector3{3, 1, 2} && finite.max == Vector3{3, 4, 5}, "Rust NaN min/max behavior");
    auto one = BoundingBox3D::from_point({1, 2, 3});
    check(one.min == one.max && one.width() == 0, "Point box");
}
} // namespace
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    known_results();
    std::ifstream in(argv[1], std::ios::binary);
    const auto d = DxfDocument::read(in);
    auto before = bytes(d);
    std::ofstream o(argv[2]);
    o.exceptions(std::ios::badbit | std::ios::failbit);
    o << std::setprecision(17);
    unsigned count = 0;
    auto prefix = [&](const auto &e) {
        ++count;
        o << e.handle();
        box_values(o, e.bounding_box());
    };
    d.for_each<DxfLine>([&](const DxfLine &l) {
        prefix(l);
        o << ' ' << l.length();
        point(o, l.direction());
        point(o, l.midpoint());
        o << '\n';
    });
    d.for_each<DxfCircle>([&](const DxfCircle &c) {
        prefix(c);
        o << ' ' << c.diameter() << ' ' << c.circumference() << ' ' << c.area();
        point(o, c.center_wcs());
        auto [x, y] = c.axes_wcs();
        point(o, x);
        point(o, y);
        for (double a : {-1.2, 0., 0.7, pi, 2. * pi})
            point(o, c.point_at_angle_wcs(a));
        o << '\n';
        if (c.radius() > 0)
            for (unsigned i = 0; i < 128; ++i) {
                auto p = c.point_at_angle_wcs(i * 2. * pi / 128.);
                contains_approximately(c.bounding_box(), p);
                contains_approximately(c.bounding_box(), p + c.normal().normalize() * c.thickness());
            }
    });
    d.for_each<DxfArc>([&](const DxfArc &a) {
        prefix(a);
        o << ' ' << a.sweep_angle() << ' ' << a.arc_length();
        point(o, a.start_point());
        point(o, a.end_point());
        point(o, a.midpoint());
        point(o, a.center_wcs());
        point(o, a.start_point_wcs());
        point(o, a.end_point_wcs());
        point(o, a.midpoint_wcs());
        for (double t : {-1.2, 0., 0.7, pi, 2. * pi})
            point(o, a.point_at_angle_wcs(t));
        o << '\n';
        if (a.sweep_angle() >= 0 && a.sweep_angle() <= 2. * pi)
            for (unsigned i = 0; i <= 128; ++i) {
                auto p = a.point_at_angle_wcs(a.start_angle_radians() + a.sweep_angle() * i / 128.);
                contains_approximately(a.bounding_box(), p);
                contains_approximately(a.bounding_box(), p + a.normal().normalize() * a.thickness());
            }
    });
    d.for_each<DxfEllipse>([&](const DxfEllipse &e) {
        prefix(e);
        o << ' ' << e.major_axis_length() << ' ' << e.minor_axis_length() << ' ' << e.is_full() << '\n';
    });
    d.for_each<DxfLwPolyline>([&](const DxfLwPolyline &p) {
        prefix(p);
        o << ' ' << p.vertex_count() << '\n';
        auto changed = p;
        auto b = p.bounding_box();
        changed.set_thickness(100);
        changed.set_constant_width(100);
        for (std::size_t i = 0; i < p.vertex_count(); ++i)
            changed.set_bulge(i, 10);
        auto other = changed.bounding_box();
        check(b.min == other.min && b.max == other.max, "Upstream vertex-only bounds changed");
    });
    check(count == 200, "Missing geometry cases");
    check(bytes(d) == before, "Evaluation mutated drawing");
    o.close();
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
