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
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto source = DxfDocument::read(in);
    auto original = bytes(source);
    for (int step = 0; step < 16; ++step) {
        auto d = source;
        auto t = operation(step == 14 ? 4 : step == 15 ? 6 : step);
        int count = 0;
        d.for_each<DxfRasterImage>([&](auto &e) {
            if (e.layer() == "Control")
                return;
            ++count;
            if (step == 0)
                e.translate({3, -4, 5});
            else if (step == 5 || step == 6 || step == 7 || step == 11 || step >= 14) {
                e.apply_mirror(t);
                if (step == 15)
                    e.apply_mirror(t);
            } else
                e.apply_transform(t);
        });
        check(count == 32, "Missing raster targets");
        if (step == 7 || step == 11)
            check(bytes(d) == original, "No-op mirror changed drawing");
        auto path = std::string(argv[2]) + "." + std::to_string(step);
        std::ofstream out(path + ".dxf", std::ios::binary);
        d.write(out);
        out.close();
        check(bool(out), "Save failed");
        std::ofstream trace(path + ".trace");
        trace << std::setprecision(17);
        auto n = [&](double x) { trace << ' ' << x; };
        auto p = [&](Point3 p) {
            n(p.x);
            n(p.y);
            n(p.z);
        };
        d.for_each<DxfRasterImage>([&](const auto &e) {
            trace << e.handle();
            auto v = e.definition();
            p(v.insertion);
            p(v.u_vector);
            p(v.v_vector);
            n(v.size.x);
            n(v.size.y);
            for (auto q : e.corners())
                p(q);
            auto b = e.bounding_box();
            p(b.min);
            p(b.max);
            n(v.boundary.size());
            for (auto q : v.boundary) {
                n(q.x);
                n(q.y);
            }
            if (e.is_wipeout()) {
                for (auto q : e.wipeout_world_boundary_vertices())
                    p(q);
            }
            trace << '\n';
        });
        trace.close();
        check(bool(trace), "Trace failed");
    }
    source.for_each<DxfRasterImage>([](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(operation(11));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Nonfinite transform accepted");
    });
    check(bytes(source) == original, "Invalid matrix partially committed");
    source.for_each<DxfRasterImage>([](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(Transform::from_scale(std::numeric_limits<double>::max()));
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "Overflow accepted");
    });
    check(bytes(source) == original, "Overflow partially committed");
    // Verify the affine identity independently of Rust, including the former double-scale bug.
    source.for_each<DxfRasterImage>([](auto &e) {
        auto v = e.definition();
        auto corners = e.corners();
        auto clip = e.is_wipeout() ? e.wipeout_world_boundary_vertices() : std::vector<Point3>{};
        auto t = operation(8);
        e.apply_transform(t);
        auto actual = e.corners();
        for (std::size_t i = 0; i < 4; ++i)
            check((actual[i] - t.apply(corners[i])).length() < 1e-9, "Affine corner identity");
        auto result = e.definition();
        check(result.size == v.size && result.boundary == v.boundary, "Pixel coordinates changed");
        if (e.is_wipeout()) {
            auto mapped = e.wipeout_world_boundary_vertices();
            for (std::size_t i = 0; i < clip.size(); ++i)
                check((mapped[i] - t.apply(clip[i])).length() < 1e-9, "Affine wipeout clip identity");
        } else {
            bool refused = false;
            try {
                e.wipeout_world_boundary_vertices();
            } catch (const std::invalid_argument &) {
                refused = true;
            }
            check(refused, "IMAGE accepted WIPEOUT mapping");
        }
    });
    auto d = DxfDocument::create();
    RasterImageDefinition v;
    v.insertion = {1, 2, 3};
    v.u_vector = {2, 0, 0};
    v.v_vector = {0, 3, 0};
    v.size = {4, 2};
    v.boundary = {{-2, -1}, {2, 1}};
    d.add_wipeout(v);
    d.for_each<DxfRasterImage>([](auto &e) {
        auto p = e.wipeout_world_boundary_vertices();
        check(p[0] == Point3{1, 8, 3} && p[1] == Point3{9, 2, 3}, "Wipeout pixel center/Y convention");
        e.apply_transform(Transform::from_scale(2));
        auto v = e.definition();
        check(v.u_vector == Point3{4, 0, 0} && v.v_vector == Point3{0, 6, 0}, "Pixel vectors scaled twice");
        check(v.size == Vector2{4, 2}, "Pixel dimensions scaled");
    });
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
