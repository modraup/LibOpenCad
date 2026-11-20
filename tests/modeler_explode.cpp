// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "geometry_operations.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
using namespace libopencad;
static void point(std::ostream &out, Point3 p) { out << ' ' << p.x << ' ' << p.y << ' ' << p.z; }
static void trace(std::ostream &out, const ExplodedModelerPart &part) {
    std::visit(
        [&](const auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, DxfLine>) {
                out << "LINE";
                point(out, e.start());
                point(out, e.end());
            } else if constexpr (std::is_same_v<T, DxfCircle>) {
                out << "CIRCLE";
                point(out, e.center());
                out << ' ' << e.radius();
                point(out, e.normal());
            } else if constexpr (std::is_same_v<T, DxfEllipse>) {
                out << "ELLIPSE";
                point(out, e.center());
                point(out, e.major_axis());
                point(out, e.normal());
                out << ' ' << e.minor_axis_ratio() << ' ' << e.start_parameter() << ' ' << e.end_parameter();
            } else if constexpr (std::is_same_v<T, DxfSpline>) {
                auto d = e.definition();
                out << "SPLINE " << d.degree << ' ' << bool(d.flags & 4) << ' ' << d.knots.size() << ' '
                    << d.control_points.size() << ' ' << d.fit_points.size();
                for (double n : d.knots)
                    out << ' ' << n;
                for (double n : d.weights)
                    out << ' ' << n;
                for (auto p : d.control_points)
                    point(out, p);
                for (auto p : d.fit_points)
                    point(out, p);
                point(out, d.normal);
            } else {
                out << (e.planar ? "WIRE-REGION" : "WIRE-SURFACE");
                for (auto p : e.points)
                    point(out, p);
            }
            out << '\n';
        },
        part);
}
static void add(DxfDocument &d, const ExplodedModelerPart &part) {
    std::visit(
        [&](const auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, DxfLine>)
                d.add_line(e.start(), e.end());
            else if constexpr (std::is_same_v<T, DxfCircle>) {
                auto h = d.add_circle(e.center(), e.radius());
                d.for_each<DxfCircle>([&](auto &target) {
                    if (std::stoull(target.handle(), nullptr, 16) == h)
                        target.set_normal(e.normal());
                });
            } else if constexpr (std::is_same_v<T, DxfEllipse>)
                d.add_ellipse(e.center(), e.major_axis(), e.minor_axis_ratio(), e.start_parameter(),
                              e.end_parameter(), e.normal());
            else if constexpr (std::is_same_v<T, DxfSpline>)
                d.add_spline(e.definition());
        },
        part);
}
static void boundaries() {
    auto d = DxfDocument::create();
    bool rejected = false;
    try {
        d.explode_modeler(0);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    if (!rejected)
        throw std::runtime_error("unknown handle");
    auto g = acis::build_box({0, 0, 0}, 1, 2, 3);
    auto h = d.add_modeler_entity(ModelerKind::Region, g);
    auto before = d.explode_modeler(h).size();
    if (before != 24)
        throw std::runtime_error("box edge count");
    // Self-cyclic loops/coedges terminate; malformed references stop their walk.
    auto *loop = g.records_of_type("loop").front();
    auto index = loop->index;
    g.record(index)->tokens[1] = acis::SatToken(acis::SatPointer{index});
    d.set_modeler_geometry(h, g);
    (void)d.explode_modeler(h);
    for (auto &r : g.records)
        if (r.entity_type == "coedge")
            r.tokens[4] = acis::SatToken(acis::SatPointer{999999});
    d.set_modeler_geometry(h, g);
    (void)d.explode_modeler(h);
}
int main(int argc, char **argv) try {
    if (argc == 1) {
        boundaries();
        return 0;
    }
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto source = DxfDocument::read(in);
    std::string prefix = argv[2];
    for (int step : {0, 2, 3, 6, 13}) {
        auto d = source;
        std::vector<std::uint64_t> handles;
        d.for_each<DxfModelerEntity>(
            [&](const auto &e) { handles.push_back(std::stoull(e.handle(), nullptr, 16)); });
        auto output = DxfDocument::create();
        std::ofstream out(prefix + "." + std::to_string(step) + ".trace");
        out << std::setprecision(17);
        for (auto h : handles) {
            if (step)
                d.transform_modeler(h, operation(step));
            auto parts = d.explode_modeler(h);
            out << "COUNT " << parts.size() << '\n';
            for (const auto &part : parts) {
                trace(out, part);
                add(output, part);
            }
        }
        for (bool binary : {false, true}) {
            std::ofstream file(prefix + "." + std::to_string(step) + (binary ? ".binary.dxf" : ".ascii.dxf"),
                               std::ios::binary);
            output.write(file, binary);
        }
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
