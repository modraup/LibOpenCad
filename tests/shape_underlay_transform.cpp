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
        auto edit = [&](auto &e) {
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
        };
        d.for_each<DxfShape>(edit);
        d.for_each<DxfUnderlay>(edit);
        check(count == 60, "Missing transform targets");
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
        d.for_each<DxfShape>([&](const auto &e) {
            trace << e.handle();
            auto v = e.definition();
            p(v.insertion);
            p(v.normal);
            n(v.size);
            n(v.rotation);
            n(v.relative_x_scale);
            n(v.oblique_angle);
            n(v.thickness);
            trace << '\n';
        });
        d.for_each<DxfUnderlay>([&](const auto &e) {
            trace << e.handle();
            auto v = e.definition();
            p(v.insertion);
            p(v.normal);
            p(v.scale);
            n(v.rotation);
            auto boundary = e.world_clip_boundary();
            n(boundary.size());
            for (auto q : boundary)
                p(q);
            trace << '\n';
        });
        trace.close();
        check(bool(trace), "Trace failed");
    }
    auto invalid = [&](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(operation(11));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Nonfinite transform accepted");
    };
    source.for_each<DxfShape>(invalid);
    source.for_each<DxfUnderlay>(invalid);
    check(bytes(source) == original, "Invalid matrix partially committed");
    auto overflow = [&](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(Transform::from_scale(std::numeric_limits<double>::max()));
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "Overflow accepted");
    };
    source.for_each<DxfShape>(overflow);
    source.for_each<DxfUnderlay>(overflow);
    check(bytes(source) == original, "Overflow partially committed");
    // Independent world-space mirror and involution checks, without external files/fonts.
    auto doc = DxfDocument::create();
    UnderlayFileDefinition file;
    file.file_name = "reference.pdf";
    file.page_name = "1";
    auto ref = doc.add_underlay_definition(UnderlayType::Pdf, "底图", file);
    UnderlayDefinition u;
    u.definition = ref;
    u.insertion = {2, 3, 4};
    u.normal = Vector3{1, -2, 3}.normalize();
    u.rotation = .37;
    u.scale = {-2, 3, -4};
    u.boundary = {{-1, 2}, {4, -3}, {5, 6}};
    doc.add_underlay(UnderlayType::Pdf, u);
    auto t = Transform::from_mirror_plane({-2, 1, 3}, {1, 3, -2});
    doc.for_each<DxfUnderlay>([&](auto &e) {
        auto before = e.world_clip_boundary();
        e.apply_mirror(t);
        auto actual = e.world_clip_boundary();
        for (std::size_t i = 0; i < before.size(); ++i)
            check((actual[i] - t.apply(before[i])).length() < 1e-10, "Mirror world clip geometry");
        check(e.definition().definition == ref, "Definition reference changed");
        e.apply_mirror(t);
        actual = e.world_clip_boundary();
        for (std::size_t i = 0; i < before.size(); ++i)
            check((actual[i] - before[i]).length() < 1e-10, "Double mirror clip geometry");
    });
    ShapeDefinition s;
    s.name = "符号";
    s.insertion = {1, 2, 3};
    s.normal = {0, 0, -1};
    s.size = 4;
    s.oblique_angle = .25;
    s.relative_x_scale = 2;
    s.thickness = 3;
    doc.add_shape(s);
    doc.for_each<DxfShape>([&](auto &e) {
        e.translate({3, 4, 5});
        check(e.definition().insertion == Point3{-2, 6, -2}, "Shape OCS displacement");
        e.apply_mirror(Transform::from_mirror_x());
        auto v = e.definition();
        check(std::abs(v.size - 4 * std::cos(.25)) < 1e-12 &&
                  std::abs(v.relative_x_scale + 2 / std::cos(.25)) < 1e-12,
              "Shape oblique mirror sizing");
        check(std::abs(v.oblique_angle + .25) < 1e-12 && v.thickness == 3, "Shape oblique/thickness");
    });
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
