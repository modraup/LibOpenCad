// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <limits>
namespace {
HatchDefinition definition(std::uint64_t boundary, bool curved = false) {
    HatchDefinition v;
    v.associative = boundary != 0;
    v.elevation = 2;
    v.pixel_size = .02;
    HatchPath outer;
    outer.flags = curved ? 1 : 7;
    if (boundary)
        outer.boundary_handles = {boundary};
    if (curved) {
        outer.edges = {HatchLineEdge{{0, 0}, {4, 0}},
                       HatchArcEdge{{4, 2}, 2, -1.5707963267948966, 1.5707963267948966, true},
                       HatchEllipseEdge{{2, 4}, {2, 0}, .5, 0, 3.14159265358979323846, true}};
        HatchSplineEdge s;
        s.degree = 2;
        s.rational = true;
        s.knots = {0, 0, 0, 1, 1, 1};
        s.control_points = {{0, 4, 1}, {0, 2, .5}, {0, 0, 1}};
        s.fit_points = {{0, 4}, {0, 0}};
        s.start_tangent = {0, -1};
        s.end_tangent = {0, -1};
        outer.edges.push_back(s);
        v.gradient = {
            true,    7, .3, .4, .6, false, {{0, Color::from_rgb(12, 34, 56)}, {1, Color::from_index(2)}},
            "LINEAR"};
    } else {
        outer.edges = {HatchPolylineEdge{{{0, 0, 0}, {10, 0, .25}, {10, 10, 0}, {0, 10, 0}}, true}};
        v.solid = false;
        v.pattern_name = "CUSTOM";
        v.pattern_type = 2;
        v.pattern_angle = .25;
        v.pattern_scale = 2;
        v.double_pattern = true;
        v.pattern_lines = {{.5, {1, 2}, {3, 4}, {2, -1, 0}}};
    }
    v.paths.push_back(outer);
    HatchPath hole;
    hole.edges = {HatchArcEdge{curved ? Vector2{2, 2} : Vector2{5, 5}, curved ? .5 : 1., 0,
                               6.28318530717958647692, false}};
    v.paths.push_back(hole);
    v.seed_points = {{1, 1}};
    return v;
}
void verify(const DxfDocument &d) {
    int count = 0;
    d.for_each<DxfHatch>([&](auto &e) {
        ++count;
        auto v = e.definition();
        check(v.paths.size() == 2 && v.seed_points == std::vector<Vector2>{{1, 1}} && v.elevation == 2,
              "hatch paths/seeds/elevation");
        if (v.solid) {
            check(v.paths[0].edges.size() == 4 && v.gradient.enabled && v.gradient.reserved == 7 &&
                      v.gradient.colors.size() == 2 &&
                      v.gradient.colors[0].color == Color::from_rgb(12, 34, 56) &&
                      v.gradient.colors[1].color == Color::from_index(2),
                  "gradient and mixed edges");
            auto &s = std::get<HatchSplineEdge>(v.paths[0].edges[3]);
            check(s.control_points[1] == Point3{0, 2, .5} && s.fit_points.size() == 2,
                  "hatch rational spline");
        } else {
            check(v.pattern_lines.size() == 1 &&
                      v.pattern_lines[0].dash_lengths == std::vector<double>{2, -1, 0} &&
                      v.pixel_size == .02 && v.associative,
                  "hatch pattern");
            check(std::get<HatchPolylineEdge>(v.paths[0].edges[0]).vertices[1].z == .25, "polyline bulge");
        }
    });
    check(count == 3, "hatch count");
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "hatch failure rollback");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        for (auto space : {EntitySpace::Model, EntitySpace::Paper}) {
            EntityOptions o;
            o.space = space;
            auto boundary = made.add_lwpolyline(
                {{0, 0, 0, 0, 0, 0}, {10, 0, .25, 0, 0, 0}, {10, 10, 0, 0, 0, 0}, {0, 10, 0, 0, 0, 0}}, true,
                o);
            made.for_each<DxfLwPolyline>([&](auto &e) {
                if (e.handle() == hex(boundary))
                    e.set_elevation(2);
            });
            made.add_hatch(definition(boundary), o);
        }
        made.add_hatch(definition(0, true));
        verify(made);
        write(made, prefix + "-created.dxf");
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        verify(d);
        std::string target;
        d.for_each<DxfHatch>([&](auto &e) {
            if (target.empty() && !e.definition().solid)
                target = e.handle();
        });
        auto dictionary = d.add_extension_dictionary(target);
        d.add_xrecord(hex(dictionary), "Data", {{340, target}});
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        std::uint64_t boundary = 0;
        d.for_each<DxfHatch>([&](auto &e) {
            if (e.handle() != target)
                return;
            auto v = e.definition();
            boundary = v.paths[0].boundary_handles[0];
            auto &p = std::get<HatchPolylineEdge>(v.paths[0].edges[0]);
            p.vertices[1].z = -.5;
            p.vertices[2].x = 12;
            v.pattern_lines[0].dash_lengths = {3, -2};
            v.pattern_angle = -.5;
            v.seed_points.push_back({8, 8});
            e.set_definition(v);
        });
        auto after = records(save(d), binary);
        for (auto &r : before) {
            auto h = identity(r);
            if (!h.empty() && h != target)
                check(at(after, h) == r, "neighbor hatch/outline unchanged");
        }
        write(d, prefix + "-edited.dxf");
        auto copied = hex(d.copy_entity(target, EntitySpace::Paper));
        write(d, prefix + "-copied.dxf");
        d.for_each<DxfHatch>([&](auto &e) {
            if (e.handle() != copied)
                return;
            auto v = e.definition();
            v.seed_points = {{9, 9}};
            e.set_definition(v);
        });
        write(d, prefix + "-copymodified.dxf");
        check(d.remove_entity(copied), "delete hatch copy");
        fails(d, [&] { d.remove_entity(boundary); });
        check(d.remove_entity(target) && d.remove_entity(boundary), "remove hatch before boundary");
        write(d, prefix + "-deleted.dxf");
        fails(made, [&] {
            auto v = definition(0);
            v.paths[0].flags = 0;
            made.add_hatch(v);
        });
        fails(made, [&] {
            auto v = definition(0);
            std::get<HatchPolylineEdge>(v.paths[0].edges[0]).vertices[0].z =
                std::numeric_limits<double>::quiet_NaN();
            made.add_hatch(v);
        });
        // Test ellipse parameters beyond one revolution and clockwise edge semantics.
        auto turns = made;
        turns.for_each<DxfHatch>([&](auto &e) {
            auto v = e.definition();
            if (!v.solid)
                return;
            auto &edge = std::get<HatchEllipseEdge>(v.paths[0].edges[2]);
            edge.start_angle = -.75;
            edge.end_angle = 7.4;
            edge.counter_clockwise = false;
            e.set_definition(v);
            auto read = e.definition();
            auto actual = std::get<HatchEllipseEdge>(read.paths[0].edges[2]);
            check(std::abs(actual.start_angle + .75) < 1e-12 && std::abs(actual.end_angle - 7.4) < 1e-12,
                  "ellipse parameter revolution");
        });
        write(turns, prefix + "-turns.dxf");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
