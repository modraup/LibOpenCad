// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "geometry_operations.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace libopencad;
static void check(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream s;
    d.write(s);
    return s.str();
}
static Transform hatch_operation(int step) {
    if (step == 12)
        return Transform::from_scaling({.5, 2, 1.5});
    if (step == 14)
        return operation(5);
    if (step == 15)
        return operation(6);
    if (step == 16)
        return operation(3);
    if (step == 17)
        return Transform::from_translation({0, 0, 3});
    return operation(step);
}
static void trace(std::ostream &o, const DxfHatch &e) {
    auto d = e.definition();
    o << e.handle();
    auto n = [&](double x) { o << ' ' << x; };
    auto p = [&](Vector2 p) {
        n(p.x);
        n(p.y);
    };
    auto v = [&](Vector3 p) {
        n(p.x);
        n(p.y);
        n(p.z);
    };
    n(d.elevation);
    v(d.normal);
    n(d.pattern_angle);
    n(d.pattern_scale);
    n(d.gradient.angle);
    p(e.pattern_origin());
    n(bool(e.stored_pattern_origin()));
    n(d.paths.size());
    for (auto &path : d.paths) {
        n(path.edges.size());
        for (auto &edge : path.edges) {
            n(edge.index());
            std::visit(
                [&](const auto &a) {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, HatchLineEdge>) {
                        p(a.start);
                        p(a.end);
                    } else if constexpr (std::is_same_v<T, HatchArcEdge>) {
                        p(a.center);
                        n(a.radius);
                        n(a.start_angle);
                        n(a.end_angle);
                        n(a.counter_clockwise);
                    } else if constexpr (std::is_same_v<T, HatchEllipseEdge>) {
                        p(a.center);
                        p(a.major_axis_endpoint);
                        n(a.minor_axis_ratio);
                        n(a.start_angle);
                        n(a.end_angle);
                        n(a.counter_clockwise);
                    } else if constexpr (std::is_same_v<T, HatchSplineEdge>) {
                        n(a.control_points.size());
                        for (auto q : a.control_points)
                            v(q);
                        n(a.fit_points.size());
                        for (auto q : a.fit_points)
                            p(q);
                        p(a.start_tangent);
                        p(a.end_tangent);
                    } else {
                        n(a.vertices.size());
                        for (auto q : a.vertices)
                            v(q);
                        n(a.closed);
                    }
                },
                edge);
        }
    }
    n(d.seed_points.size());
    for (auto q : d.seed_points)
        p(q);
    n(d.pattern_lines.size());
    for (auto &l : d.pattern_lines) {
        n(l.angle);
        p(l.base_point);
        p(l.offset);
        n(l.dash_lengths.size());
        for (auto x : l.dash_lengths)
            n(x);
    }
    o << '\n';
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto source = DxfDocument::read(in);
    auto original = bytes(source);
    for (int step = 0; step < 18; ++step) {
        auto d = source;
        auto t = hatch_operation(step);
        int count = 0;
        d.for_each<DxfHatch>([&](auto &e) {
            if (e.layer() == "Control")
                return;
            ++count;
            if (step == 0)
                e.translate({3, -4, 5});
            else if (step == 5 || step == 6 || step == 7 || step == 11 || step >= 14) {
                MirrorOptions o;
                o.mirror_hatch = step == 14 || step == 15;
                e.apply_mirror(t, o);
            } else
                e.apply_transform(t);
        });
        check(count == 24, "Missing hatch targets");
        if (step == 7 || step == 11)
            check(bytes(d) == original, "No-op mirror changed drawing");
        auto prefix = std::string(argv[2]) + "." + std::to_string(step);
        std::ofstream out(prefix + ".dxf", std::ios::binary);
        d.write(out);
        out.close();
        check(bool(out), "Save failed");
        std::ofstream values(prefix + ".trace");
        values << std::setprecision(17);
        d.for_each<DxfHatch>([&](const auto &e) { trace(values, e); });
        values.close();
        check(bool(values), "Trace failed");
    }
    source.for_each<DxfHatch>([](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(operation(11));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Invalid matrix accepted");
    });
    check(bytes(source) == original, "Invalid transform partially committed");
    source.for_each<DxfHatch>([](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(Transform::from_scale(std::numeric_limits<double>::max()));
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "Overflow accepted");
    });
    check(bytes(source) == original, "Overflow partially committed");
    source.for_each<DxfHatch>([](auto &e) {
        auto o = e.pattern_origin();
        auto before = e.definition();
        check(!e.set_pattern_origin({std::numeric_limits<double>::infinity(), 0}), "Invalid origin accepted");
        check(e.pattern_origin() == o, "Invalid origin changed metadata");
        check(e.set_pattern_origin(o + Vector2{2, 3}), "Origin edit failed");
        auto after = e.definition();
        for (std::size_t i = 0; i < before.pattern_lines.size(); ++i)
            check((after.pattern_lines[i].base_point - before.pattern_lines[i].base_point - Vector2{2, 3})
                          .length() < 1e-10,
                  "Pattern origin translation");
    });
    // Independent known-result checks for the MIRRHATCH switch and degeneracy.
    auto known = DxfDocument::create();
    HatchDefinition h;
    h.solid = false;
    h.pattern_name = "TEST";
    h.pattern_angle = .25;
    h.pattern_scale = 2;
    h.elevation = 2;
    h.pattern_lines = {{.4, {1, 2}, {3, 4}, {2, -1}}};
    h.seed_points = {{1, 1}};
    h.paths = {{2, {HatchPolylineEdge{{{1, 2, .5}, {3, 4, 0}}, false}}, {}},
               {0, {HatchArcEdge{{5, 5}, 1, 0, 2 * std::acos(-1.), false}}, {}}};
    known.add_hatch(h);
    known.for_each<DxfHatch>([](auto &e) { check(e.record_pattern_origin({3, 4}), "Record origin"); });
    for (bool reflect_pattern : {false, true}) {
        auto d = known;
        d.for_each<DxfHatch>([&](auto &e) {
            MirrorOptions options;
            options.mirror_hatch = reflect_pattern;
            e.apply_mirror(Transform::from_mirror_x(), options);
            auto v = e.definition();
            check(v.elevation == 2 && v.seed_points[0] == Vector2{-1, 1}, "Mirror OCS elevation/seeds");
            auto vertex = std::get<HatchPolylineEdge>(v.paths[0].edges[0]).vertices[0];
            check(vertex == Vector3{-1, 2, -.5}, "Mirror bulge");
            auto arc = std::get<HatchArcEdge>(v.paths[1].edges[0]);
            check(arc.counter_clockwise &&
                      std::abs(arc.end_angle - arc.start_angle - 2 * std::acos(-1.)) < 1e-12,
                  "Mirror full arc sweep");
            check(std::abs(v.pattern_angle - .25) < 1e-12 && v.pattern_scale == 2,
                  "Authored pattern settings");
            check(e.pattern_origin() == Vector2{reflect_pattern ? -3. : 3., 4}, "Mirror origin policy");
            const auto &line = v.pattern_lines[0];
            check(line.base_point == Vector2{reflect_pattern ? -1. : 1., 2} &&
                      line.offset == Vector2{reflect_pattern ? -3. : 3., 4},
                  "Mirror explicit pattern family");
            check(std::abs(line.angle - (reflect_pattern ? std::acos(-1.) - .4 : .4)) < 1e-12,
                  "Mirror pattern direction");
        });
    }
    auto before = bytes(known);
    known.for_each<DxfHatch>([](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(Transform::from_scaling({0, 1, 1}));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Collapsed circular OCS axis should refuse nonfinite ellipse geometry");
    });
    check(bytes(known) == before, "Collapsed-axis transform partially committed");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
