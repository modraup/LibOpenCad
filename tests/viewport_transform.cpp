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
        d.for_each<DxfViewport>([&](auto &e) {
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
        check(count == 20, "Missing viewport targets");
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
        d.for_each<DxfViewport>([&](const auto &e) {
            trace << e.handle();
            auto v = e.definition();
            for (auto q : {v.center, v.view_direction, v.view_target, v.ucs_origin, v.ucs_x_axis,
                           v.ucs_y_axis, v.view_center, v.snap_base, v.snap_spacing, v.grid_spacing})
                p(q);
            for (auto x : {v.width, v.height, v.lens_length, v.front_clip_z, v.back_clip_z, v.view_height,
                           v.snap_angle, v.twist_angle, v.elevation, v.brightness, v.contrast})
                n(x);
            trace << '\n';
        });
        trace.close();
        check(bool(trace), "Trace failed");
    }
    source.for_each<DxfViewport>([](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(operation(11));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Nonfinite transform accepted");
    });
    check(bytes(source) == original, "Invalid matrix partially committed");
    source.for_each<DxfViewport>([](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(Transform::from_scale(std::numeric_limits<double>::max()));
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "Overflow accepted");
    });
    check(bytes(source) == original, "Overflow partially committed");
    // Direct translation and a translation matrix intentionally have different camera semantics.
    auto d = DxfDocument::create();
    ViewportDefinition v;
    v.center = {1, 2, 3};
    v.view_target = {4, 5, 6};
    v.ucs_origin = {7, 8, 9};
    v.view_direction = {0, 0, 2};
    v.ucs_x_axis = {2, 0, 0};
    v.ucs_y_axis = {1, 3, 0};
    v.width = 8;
    v.height = 6;
    v.view_height = 42;
    v.twist_angle = .3;
    EntityOptions options;
    options.space = EntitySpace::Paper;
    d.add_viewport(v, options);
    d.for_each<DxfViewport>([&](auto &e) {
        auto a = e, b = e;
        a.translate({3, -4, 5});
        b.apply_transform(Transform::from_translation({3, -4, 5}));
        auto x = a.definition(), y = b.definition();
        check(x.center == Point3{4, -2, 8} && y.center == x.center, "Center translation");
        check(x.view_target == v.view_target && x.ucs_origin == v.ucs_origin &&
                  x.view_direction == v.view_direction,
              "Direct translation changed camera");
        check(y.view_target == Point3{7, 1, 11} && y.ucs_origin == Point3{10, 4, 14} &&
                  y.view_direction == Vector3::UNIT_Z,
              "Matrix camera transform");
        b = e;
        b.apply_transform(Transform::from_scaling({2, .5, 3}));
        y = b.definition();
        check(y.width == 16 && y.height == 12 && y.view_height == 42, "World-X scale contract");
        check(std::abs(y.ucs_y_axis.length() - 1) < 1e-12 && std::abs(y.twist_angle - .3) < 1e-12,
              "UCS normalization/twist");
    });
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
