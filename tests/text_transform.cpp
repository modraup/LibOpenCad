// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "geometry_operations.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace libopencad;
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream input(argv[1], std::ios::binary);
    auto source = DxfDocument::read(input);
    auto oversized = source;
    oversized.for_each<DxfInsert>([](auto &e) {
        auto d = e.definition();
        d.rows = 1001;
        d.columns = 1000;
        e.set_definition(d);
        bool refused = false;
        try {
            (void)e.array_points();
        } catch (const std::length_error &) {
            refused = true;
        }
        check(refused, "Oversized array was not refused");
    });
    auto original = bytes(source);
    for (int step = 0; step < 18; ++step) {
        auto d = source;
        auto t = operation(step < 14 ? step : 6);
        MirrorOptions o;
        if (step == 14)
            o.mirror_text = true;
        if (step == 15)
            o.text_width = 19.25;
        if (step == 16)
            o.text_width = std::numeric_limits<double>::quiet_NaN();
        if (step == 17)
            o.text_width = -1.;
        unsigned count = 0;
        auto edit = [&](auto &e) {
            if (e.layer() == "Control")
                return;
            ++count;
            if (step == 0)
                e.translate({3, -4, 5});
            else if (step == 5 || step == 6 || step == 7 || step == 11 || step >= 14)
                e.apply_mirror(t, o);
            else
                e.apply_transform(t);
        };
        d.for_each<DxfText>(edit);
        d.for_each<DxfMText>(edit);
        d.for_each<DxfAttribute>(edit);
        d.for_each<DxfInsert>(edit);
        check(count == 160, "Wrong transform count");
        if (step == 7 || step == 11)
            check(bytes(d) == original, "No-op mirror edited drawing");
        auto path = std::string(argv[2]) + "." + std::to_string(step);
        std::ofstream out(path + ".dxf", std::ios::binary);
        d.write(out);
        out.close();
        check(bool(out), "Write failed");
        std::ofstream trace(path + ".insert");
        trace << std::setprecision(17);
        d.for_each<DxfInsert>([&](const auto &e) {
            auto p = e.get_transform().apply({1, 2, 3}), v = e.get_transform().apply_rotation({-2, 1, 4});
            auto points = e.array_points();
            trace << e.handle() << ' ' << points.size() << ' ' << p.x << ' ' << p.y << ' ' << p.z << ' '
                  << v.x << ' ' << v.y << ' ' << v.z;
            for (auto q : points)
                trace << ' ' << q.x << ' ' << q.y << ' ' << q.z;
            trace << '\n';
        });
        trace.close();
        check(bool(trace), "Trace failed");
    }
    auto reject = [&](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(operation(11));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Nonfinite transform accepted");
    };
    source.for_each<DxfText>(reject);
    source.for_each<DxfMText>(reject);
    source.for_each<DxfAttribute>(reject);
    source.for_each<DxfInsert>(reject);
    check(bytes(source) == original, "Partial failed transform");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
