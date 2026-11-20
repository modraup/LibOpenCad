// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <libopencad/extended_geometry.hpp>
#include "geometry_operations.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cmath>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
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
        auto doc = source;
        auto t = operation(step == 14 ? 4 : step == 15 ? 6 : step);
        std::ofstream trace(std::string(argv[2]) + "." + std::to_string(step) + ".trace");
        trace << std::setprecision(17);
        doc.for_each<DxfExtendedEntity>([&](auto &e) {
            auto before = e;
            bool reject = false;
            auto expected = e.definition();
            if (e.layer() != "Control") {
                if (step == 0)
                    expected = translate_extended_definition(expected, {3, -4, 5});
                else if (step != 7 && step != 11) {
                    expected = transform_extended_definition(expected, t);
                    if (step == 15)
                        expected = transform_extended_definition(expected, t);
                }
                try {
                    if (step == 0)
                        e.translate({3, -4, 5});
                    else if (step == 5 || step == 6 || step == 7 || step == 11 || step >= 14) {
                        e.apply_mirror(t);
                        if (step == 15)
                            e.apply_mirror(t);
                    } else
                        e.apply_transform(t);
                } catch (const std::invalid_argument &) {
                    reject = true;
                    check(e.type() == "LIGHT", "Unexpected transform rejection");
                    auto v = std::get<LightDefinition>(transform_extended_definition(before.definition(), t));
                    check(v.photometric.has_value(), "Light missing photometry");
                    auto z = v.photometric->web_rotation.z;
                    check(z != std::trunc(z) || z < -32768 || z > 32767, "Spurious LIGHT rejection");
                    // No point/XDATA edit is allowed to precede the failed encoding commit.
                    auto check_doc = source;
                    check_doc.for_each<DxfExtendedEntity>([&](auto &a) {
                        if (a.handle() == e.handle())
                            a = e;
                    });
                    check(bytes(check_doc) == original, "Partial failed LIGHT edit");
                }
            }
            trace << e.handle() << ' ' << reject;
            auto b = extended_bounding_box(reject ? before.definition() : expected);
            for (auto p : {b.min, b.max})
                trace << ' ' << p.x << ' ' << p.y << ' ' << p.z;
            auto stored = e.bounding_box();
            for (auto p : {stored.min, stored.max})
                trace << ' ' << p.x << ' ' << p.y << ' ' << p.z;
            if (auto *l = std::get_if<LightDefinition>(&expected); l && l->photometric) {
                auto p = l->photometric->web_rotation;
                trace << ' ' << p.x << ' ' << p.y << ' ' << p.z;
            }
            trace << '\n';
        });
        if (step == 7 || step == 11)
            check(bytes(doc) == original, "No-op mirror changed bytes");
        trace.close();
        check(bool(trace), "Trace write");
        std::ofstream out(std::string(argv[2]) + "." + std::to_string(step) + ".dxf", std::ios::binary);
        doc.write(out);
        out.close();
        check(bool(out), "DXF write");
    }
    source.for_each<DxfExtendedEntity>([&](auto &e) {
        bool rejected = false;
        try {
            e.apply_transform(operation(11));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "Nonfinite matrix accepted");
    });
    source.for_each<DxfExtendedEntity>([&](auto &e) {
        bool rejected = false;
        try {
            e.apply_transform(Transform::from_scale(std::numeric_limits<double>::max()));
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "Overflowing geometry or XDATA accepted");
    });
    check(bytes(source) == original, "Invalid edit changed source");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
