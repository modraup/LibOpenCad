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
    for (int step = 0; step < 14; ++step) {
        auto d = source;
        auto t = operation(step);
        std::size_t count = 0;
        auto edit = [&](auto &e) {
            if (e.layer() == "Control")
                return;
            ++count;
            if (step == 0)
                e.translate({3, -4, 5});
            else if (step == 5 || step == 6 || step == 7 || step == 11)
                e.apply_mirror(t);
            else
                e.apply_transform(t);
        };
        d.for_each<DxfLeader>(edit);
        d.for_each<DxfTolerance>(edit);
        d.for_each<DxfMLine>(edit);
        d.for_each<DxfMultiLeader>(edit);
        check(count == 20, "Missing annotation targets");
        if (step == 7 || step == 11)
            check(bytes(d) == original, "No-op mirror edited document");
        auto path = std::string(argv[2]) + "." + std::to_string(step);
        std::ofstream out(path + ".dxf", std::ios::binary);
        d.write(out);
        out.close();
        check(bool(out), "Save failed");
        std::ofstream trace(path + ".trace");
        trace << std::setprecision(17);
        auto box = [&](const auto &e, BoundingBox3D b) {
            trace << e.handle() << ' ' << b.min.x << ' ' << b.min.y << ' ' << b.min.z << ' ' << b.max.x << ' '
                  << b.max.y << ' ' << b.max.z;
        };
        d.for_each<DxfLeader>([&](const auto &e) {
            box(e, e.bounding_box());
            auto p = e.definition().origin;
            trace << ' ' << e.length() << ' ' << p.x << ' ' << p.y << ' ' << p.z << '\n';
        });
        d.for_each<DxfTolerance>([&](const auto &e) {
            box(e, e.bounding_box());
            auto v = e.definition();
            trace << ' ' << e.line_count() << ' ' << v.text_height << ' ' << v.dimension_gap << '\n';
        });
        d.for_each<DxfMLine>([&](const auto &e) {
            auto b = e.bounding_box();
            box(e, b.value_or(BoundingBox3D{}));
            trace << ' ' << bool(b) << ' ' << e.length() << '\n';
        });
        d.for_each<DxfMultiLeader>([&](const auto &e) {
            box(e, e.bounding_box());
            auto v = e.definition();
            const auto &c = v.context;
            trace << ' ' << v.text_height;
            for (auto p : {c.text_normal, c.text_location, c.text_direction, c.block_content_location,
                           c.block_content_scale})
                trace << ' ' << p.x << ' ' << p.y << ' ' << p.z;
            trace << ' ' << c.text_rotation << ' ' << c.text_width << ' ' << c.text_boundary_height << ' '
                  << c.block_rotation << '\n';
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
    source.for_each<DxfLeader>(reject);
    source.for_each<DxfTolerance>(reject);
    source.for_each<DxfMLine>(reject);
    source.for_each<DxfMultiLeader>(reject);
    check(bytes(source) == original, "Rejected transform changed document");
    auto overflow = source;
    auto refuse_overflow = [&](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(Transform::from_scale(std::numeric_limits<double>::max()));
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "Overflowing transform accepted");
    };
    overflow.for_each<DxfLeader>(refuse_overflow);
    overflow.for_each<DxfTolerance>(refuse_overflow);
    overflow.for_each<DxfMLine>(refuse_overflow);
    overflow.for_each<DxfMultiLeader>(refuse_overflow);
    check(bytes(overflow) == original, "Overflowing transform partially committed");
    auto lenient = DxfDocument::create(source.is_binary());
    MultiLeaderDefinition extra;
    extra.extra_fields = {{44, "7.25"}};
    lenient.add_multileader(extra);
    std::istringstream encoded(bytes(lenient));
    lenient = DxfDocument::read(encoded);
    lenient.for_each<DxfMultiLeader>([](auto &e) {
        check(e.definition().text_height == 7.25, "Lenient top-level height was not decoded");
        e.apply_transform(Transform::from_scale(2));
        check(e.definition().text_height == 14.5, "In-memory height did not scale");
    });
    std::istringstream stored(bytes(lenient));
    auto restored = DxfDocument::read(stored);
    restored.for_each<DxfMultiLeader>([](const auto &e) {
        check(e.definition().text_height == .18, "DXF height did not reset to upstream default");
    });
    DxfLeader empty;
    check(empty.length() == 0 && empty.bounding_box() == BoundingBox3D{}, "Empty leader bounds");
    DxfMLine m;
    check(!m.bounding_box() && m.length() == 0, "Empty multiline bounds");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
