// SPDX-License-Identifier: MPL-2.0
#include <libopencad/acis.hpp>
#include <libopencad/dxf_document.hpp>
#include "geometry_operations.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace libopencad;
using namespace libopencad::acis;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
template <class T> void nums(std::ostream &out, const char *name, const T &values) {
    out << name;
    for (auto v : values)
        out << ' ' << v;
    out << '\n';
}
template <std::size_t N> void controls(std::ostream &out, const std::vector<std::array<double, N>> &values) {
    out << "controls";
    for (const auto &row : values)
        for (auto v : row)
            out << ' ' << v;
    out << '\n';
}
static void curve(std::ostream &out, const std::optional<SatBSplineCurve> &c) {
    if (!c) {
        out << "curve none\n";
        return;
    }
    nums(out, "curve",
         std::array<double, 3>{double(c->degree), double(c->knots.size()), double(c->controls.size())});
    nums(out, "knots", c->knots);
    controls(out, c->controls);
}
static void pc(std::ostream &out, const std::optional<SatBSplinePCurve> &c) {
    if (!c) {
        out << "uv none\n";
        return;
    }
    nums(out, "uv",
         std::array<double, 3>{double(c->degree), double(c->knots.size()), double(c->controls.size())});
    nums(out, "knots", c->knots);
    controls(out, c->controls);
}
static void net(std::ostream &out, const std::optional<SatBSplineSurface> &c) {
    if (!c) {
        out << "net none\n";
        return;
    }
    nums(out, "net",
         std::array<double, 5>{double(c->rational), double(c->degree_u), double(c->degree_v),
                               double(c->control_count_u), double(c->control_count_v)});
    out << "forms " << c->u_closure.value_or("-") << ' ' << c->v_closure.value_or("-") << ' '
        << c->u_singularity.value_or("-") << ' ' << c->v_singularity.value_or("-") << '\n';
    nums(out, "u", c->u_knots);
    nums(out, "v", c->v_knots);
    controls(out, c->control_points);
    if (c->fit_tolerance)
        nums(out, "tolerance", std::array<double, 1>{*c->fit_tolerance});
    else
        out << "tolerance none\n";
}
static void points(std::ostream &out, const char *name, const std::vector<Vector3> &v) {
    out << name;
    for (auto p : v)
        out << ' ' << p.x << ' ' << p.y << ' ' << p.z;
    out << '\n';
}
static void points(std::ostream &out, const char *name, const std::vector<Vector2> &v) {
    out << name;
    for (auto p : v)
        out << ' ' << p.x << ' ' << p.y;
    out << '\n';
}
static void trace(std::ostream &out, const SatDocument &d) {
    out << std::scientific << std::setprecision(17);
    for (const auto &r : d.records) {
        if (auto c = SatIntCurve::from_record(r)) {
            nums(out, "int",
                 std::array<double, 3>{double(r.index), double(c->sense() == Sense::Reversed),
                                       double(c->is_closed_in(d))});
            curve(out, c->bspline());
            curve(out, c->bspline_in(d));
            for (auto n : {0u, 7u})
                points(out, "sample", c->sample(n));
            for (auto range : {std::pair<double, double>{-.2, .7}, {.7, -.2}, {3, 4}, {0, 0}})
                points(out, "range", c->sample_range(range.first, range.second, 7));
            if (auto support = c->support_in(d)) {
                out << "support yes\n";
                net(out, support->surface);
                pc(out, support->curve);
                nums(out, "offset", std::array<double, 1>{support->offset});
            } else
                out << "support none\n";
        }
        if (auto c = SatPCurve::from_record(r)) {
            nums(out, "pc", std::array<double, 2>{double(r.index), double(c->sense() == Sense::Reversed)});
            pc(out, c->bspline_in(d));
            points(out, "sample", c->sample(7));
            points(out, "sample-in", c->sample_in(d, 7));
        }
        if (auto s = SatSplineSurface::from_record(r)) {
            nums(out, "surface",
                 std::array<double, 3>{double(r.index), double(s->kind()),
                                       double(s->sense() == Sense::Reversed)});
            net(out, s->bspline(d));
            if (auto base = s->offset_in(d)) {
                out << "offset-surface yes\n";
                net(out, base->first);
                nums(out, "offset", std::array<double, 1>{base->second});
            } else
                out << "offset-surface none\n";
        }
    }
    for (std::size_t i = 0; i < 16; ++i) {
        auto t = d.subtype_tokens(i);
        nums(out, "subtype", std::array<double, 2>{double(i), t ? double(t->size()) : -1.});
    }
}
static void run(const std::string &input, const std::string &prefix) {
    std::ifstream in(input, std::ios::binary);
    check(bool(in), "Missing spline fixture");
    SatDocument d;
    std::optional<DxfDocument> drawing;
    std::uint64_t handle = 0;
    if (input.substr(input.size() - 4) == ".dxf") {
        drawing = DxfDocument::read(in);
        drawing->for_each<DxfModelerEntity>([&](const auto &e) {
            if (!handle)
                handle = std::stoull(e.handle(), nullptr, 16);
        });
        d = *drawing->modeler_geometry(handle);
    } else {
        std::string content((std::istreambuf_iterator<char>(in)), {});
        d = input.substr(input.size() - 4) == ".sab" ? SabReader::read({content.begin(), content.end()})
                                                     : SatDocument::parse(content);
    }
    auto upgraded = d;
    upgraded.upgrade_to_v700();
    std::ofstream upgraded_file(prefix + ".upgraded.sat");
    upgraded_file << upgraded.to_sat_string();
    upgraded_file.close();
    std::ofstream out(prefix + ".trace");
    trace(out, d);
    out.close();
    std::ofstream sat(prefix + ".sat");
    sat << d.to_sat_string();
    sat.close();
    auto binary = SabWriter::write(d);
    std::ofstream sab(prefix + ".sab", std::ios::binary);
    sab.write(reinterpret_cast<const char *>(binary.data()), binary.size());
    sab.close();
    if (drawing) {
        drawing->transform_modeler(handle, operation(13));
        for (bool b : {false, true}) {
            std::ofstream o(prefix + (b ? ".binary.dxf" : ".ascii.dxf"), std::ios::binary);
            drawing->write(o, b);
        }
    }
}
static SatDocument parse(std::string record) {
    return SatDocument::parse("700 0 0 0\n@8 acadrust @8 ACIS 7.0 @0 \n1 1e-6 1e-10\n" + record +
                              "\nEnd-of-ACIS-data\n");
}
static std::optional<SatDocument> construct(const std::string &label) {
    auto d = SatDocument::new_body();
    bool rational = label.find("-r") != std::string::npos;
    if (label.rfind("curve-", 0) == 0) {
        int degree = label.back() - '0';
        std::vector<Vector3> control;
        std::vector<double> weights;
        for (int i = 0; i <= degree; ++i) {
            control.push_back({double(i), double((i * i + 1) % 4), double(i % 2)});
            weights.push_back(i % 2 ? .5 : 1.);
        }
        d.add_spline_curve(rational, degree, true, {{0, degree + 1}, {1, degree + 1}}, control, weights, 0);
    } else if (label.rfind("surface-", 0) == 0) {
        int degree = label.back() - '0';
        SplineNet net;
        net.rational = rational;
        net.degree_u = net.degree_v = degree;
        net.u_closed = true;
        net.u_knots = net.v_knots = {{0, degree + 1}, {1, degree + 1}};
        for (int v = 0; v <= degree; ++v)
            for (int u = 0; u <= degree; ++u) {
                net.control.push_back({double(u), double(v), double(u * v)});
                net.weights.push_back(net.weights.size() % 2 ? .5 : 1.);
            }
        net.fit_tolerance = 1e-8;
        auto h = d.add_spline_surface(true, net);
        d.add_pcurve(rational, 1, false, {{0, 2}, {1, 2}}, {{0, 0}, {1, 1}}, {1, .5}, 0, h, {2, -3});
    } else if (label.rfind("pcurve-", 0) == 0) {
        auto h = d.add_plane_surface({1, 2, 3}, {0, 0, 1}, {1, 0, 0});
        d.add_pcurve(rational, 2, true, {{0, 3}, {1, 3}}, {{0, 0}, {1, 2}, {3, -1}}, {1, .5, 1.25}, 1e-7, h,
                     {2, -3});
    } else if (label == "built-offset") {
        SplineNet net;
        net.u_knots = net.v_knots = {{0, 2}, {1, 2}};
        net.control = {{0, 0, 0}, {1, 0, 0}, {0, 1, 1}, {1, 1, 2}};
        auto approximation = net;
        approximation.rational = true;
        approximation.weights = {1, .5, 1, 1.25};
        for (auto &p : approximation.control)
            p.z += 2;
        d.add_offset_spline_surface(true, true, net, 2, approximation);
    } else
        return {};
    return d;
}
static void verify_builders(const std::string &input, const std::string &prefix) {
    auto label = input.substr(input.find_last_of("/") + 1);
    if (auto d = construct(label)) {
        std::ofstream sat(prefix + ".built.sat");
        sat << d->to_sat_string();
        auto bytes = SabWriter::write(*d);
        std::ofstream sab(prefix + ".built.sab", std::ios::binary);
        sab.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
}
static void boundaries() {
    auto document = SatDocument::new_body();
    auto before = document.to_sat_string();
    for (int failure = 0; failure < 7; ++failure) {
        bool rejected = false;
        try {
            if (failure == 0)
                document.add_spline_curve(false, -1, false, {{0, 2}, {1, 2}}, {{0, 0, 0}, {1, 1, 1}});
            if (failure == 1)
                document.add_spline_curve(false, 1, false, {{1, 2}, {0, 2}}, {{0, 0, 0}, {1, 1, 1}});
            if (failure == 2)
                document.add_pcurve(false, 1, false, {{0, 2}, {1, 2}}, {{0, 0}, {1, 1}}, {}, 0, 999, {0, 0});
            if (failure == 3) {
                SplineNet n;
                n.u_knots = n.v_knots = {{0, 2}, {1, 2}};
                document.add_spline_surface(false, n);
            }
            if (failure == 4)
                document.add_spline_curve(false, 1, false, {{0, 2}, {1, 2}}, {{0, 0, 0}, {1, INFINITY, 1}});
            if (failure == 5)
                document.add_spline_curve(false, 65, false, {{0, 66}, {1, 66}}, std::vector<Vector3>(66));
            if (failure == 6)
                document.add_spline_curve(false, 1, false, {{0, 2}, {.5, 3}, {1, 2}}, std::vector<Vector3>(5));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "invalid builder accepted");
        check(document.to_sat_string() == before, "failed builder mutated document");
    }

    auto d = parse(
        "-0 intcurve-curve $-1 -1 $-1 forward { exactcur full nubs 1 open 2 0 1 1 1 0 0 0 1 2 3 0 } I I #");
    auto c = SatIntCurve::from_record(d.records[0]);
    check(c && c->sample(0).size() == 2, "zero segments");
    check(c->sample_range(2, 3, 7).empty(), "disjoint range");
    check(c->sample_range(.5, .5, 7).empty(), "degenerate range");
    bool reject = false;
    try {
        c->sample(SIZE_MAX);
    } catch (const std::length_error &) {
        reject = true;
    }
    check(reject, "sampling resource limit");
    {
        auto high = SatDocument::new_body();
        auto id = high.add_spline_curve(false, 64, false, {{0, 65}, {1, 65}}, std::vector<Vector3>(65));
        auto view = SatIntCurve::from_record(*high.record(id));
        check(view && view->sample(1).size() == 2, "maximum spline degree");
        bool limited = false;
        try {
            view->sample(16000);
        } catch (const std::length_error &) {
            limited = true;
        }
        check(limited, "spline evaluation work limit");
    }
    // Borrow-free views and uncached subtype resolution must survive public record edits.
    auto old = c->sample(3);
    d.records[0].tokens.clear();
    check(c->sample(3).size() == old.size(), "snapshot lifetime");
    check(!d.subtype_tokens(0), "stale shared definition");
    for (const auto &body :
         {"{ ref 999999 }", "{ ref -1 }", "{ exactcur full nubs -1 open 2 0 1 1 1 0 0 0 1 2 3 }",
          "{ exactcur full nubs 1 open 2 0 9223372036854775807 1 1 0 0 0 1 2 3 }",
          "{ exactcur full nubs 1 open 100 0 1 }", "{ exactcur full nubs 999999 open 0 }",
          "{ exactcur full nubs 1 open 2 0 3 1 1 0 0 0 1 2 3 }",
          "{ exactcur full nubs 1 open 2 0 0 1 0 }"}) {
        auto malformed = parse(std::string("-0 intcurve-curve $-1 -1 $-1 forward ") + body + " I I #");
        auto view = SatIntCurve::from_record(malformed.records[0]);
        check(!view->bspline_in(malformed), "malformed curve accepted");
        check(view->sample(7).empty(), "malformed curve sampled");
    }
    auto shared = parse("-0 spline-surface $-1 -1 $-1 forward { ref 0 } I I I I #");
    auto s = SatSplineSurface::from_record(shared.records[0]);
    check(!s->bspline(shared), "unresolved surface ref");
    auto invalid_pcurve = parse("-0 pcurve $-1 -1 $-1 0 forward { exppc nubs -1 open 2 "
                                "0 1 1 1 0 0 1 1 0 } 2 -3 #");
    auto uv = SatPCurve::from_record(invalid_pcurve.records[0]);
    check(uv && !uv->bspline_in(invalid_pcurve), "negative pcurve degree");
    auto invalid_surface = parse("-0 spline-surface $-1 -1 $-1 forward { exactsur full nubs 1 1 "
                                 "open open none none 2 2 0 3 1 1 0 1 1 1 "
                                 "0 0 0 1 0 0 2 0 0 0 1 1 1 1 2 2 1 3 0 } I I I I #");
    auto invalid_net = SatSplineSurface::from_record(invalid_surface.records[0]);
    check(invalid_net && !invalid_net->bspline(invalid_surface), "surface knot multiplicity");
    // Every prefix of a valid record is safe to inspect, including grouped SAB tokens.
    auto full = parse("-0 spline-surface $-1 -1 $-1 forward { exactsur full nubs 1 1 open open none none 2 2 "
                      "0 1 1 1 0 1 1 1 0 0 0 1 0 0 0 1 1 1 1 2 0 } I I I I #");
    for (auto binary : {false, true}) {
        auto source = binary ? SabReader::read(SabWriter::write(full)) : full;
        auto n = source.records[0].tokens.size();
        for (std::size_t size = 0; size < n; ++size) {
            auto truncated = source;
            truncated.records[0].tokens.resize(size);
            auto surface = SatSplineSurface::from_record(truncated.records[0]);
            (void)surface->bspline(truncated);
            (void)surface->offset_in(truncated);
        }
    }
}
int main(int argc, char **argv) try {
    if (argc == 1) {
        boundaries();
        return 0;
    }
    if (argc != 3)
        return 2;
    for (auto ext :
         {".sat", ".sab", ".2010-ascii.dxf", ".2010-binary.dxf", ".2018-ascii.dxf", ".2018-binary.dxf"})
        run(std::string(argv[1]) + ext, std::string(argv[2]) + ext);
    verify_builders(argv[1], argv[2]);
    std::cout << "ACIS spline native suite passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
