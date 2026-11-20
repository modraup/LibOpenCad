// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "geometry_operations.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
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
    std::ifstream in(argv[1], std::ios::binary);
    auto source = DxfDocument::read(in);
    auto original = bytes(source);
    for (int step = 0; step < 14; ++step) {
        auto d = source;
        auto t = operation(step);
        unsigned count = 0;
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
        d.for_each<DxfLine>(edit);
        d.for_each<DxfCircle>(edit);
        d.for_each<DxfArc>(edit);
        d.for_each<DxfEllipse>(edit);
        d.for_each<DxfLwPolyline>(edit);
        d.for_each<DxfPolyline2D>(edit);
        d.for_each<DxfPolyline3D>(edit);
        d.for_each<DxfPolygonMesh>(edit);
        d.for_each<DxfPolyfaceMesh>(edit);
        d.for_each<DxfPoint>(edit);
        d.for_each<DxfRay>(edit);
        d.for_each<DxfXLine>(edit);
        d.for_each<DxfSolid>(edit);
        d.for_each<DxfFace3D>(edit);
        d.for_each<DxfSpline>(edit);
        d.for_each<DxfMesh>(edit);
        check(count == 249, "Missing transform targets");
        if (step == 7 || step == 11)
            check(bytes(d) == original, "Invalid/identity mirror mutated document");
        std::ofstream out(std::string(argv[2]) + "." + std::to_string(step) + ".dxf", std::ios::binary);
        d.write(out);
        out.close();
        check(bool(out), "Save failed");
        std::ifstream stored(std::string(argv[2]) + "." + std::to_string(step) + ".dxf", std::ios::binary);
        auto reloaded = DxfDocument::read(stored);
        std::ofstream trace(std::string(argv[2]) + "." + std::to_string(step) + ".normal");
        trace << std::setprecision(17);
        reloaded.for_each<DxfPolyfaceMesh>([&](const auto &m) {
            auto n = m.polyline_header().normal;
            trace << m.handle() << ' ' << n.x << ' ' << n.y << ' ' << n.z << '\n';
        });
        trace.close();
        check(bool(trace), "Trace write failed");
    }
    auto reject = [&](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(operation(11));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Accepted non-finite transform");
    };
    source.for_each<DxfLine>(reject);
    source.for_each<DxfCircle>(reject);
    source.for_each<DxfArc>(reject);
    source.for_each<DxfEllipse>(reject);
    source.for_each<DxfLwPolyline>(reject);
    source.for_each<DxfPolyline2D>(reject);
    source.for_each<DxfPolyline3D>(reject);
    source.for_each<DxfPolygonMesh>(reject);
    source.for_each<DxfPolyfaceMesh>(reject);
    source.for_each<DxfPoint>(reject);
    source.for_each<DxfRay>(reject);
    source.for_each<DxfXLine>(reject);
    source.for_each<DxfSolid>(reject);
    source.for_each<DxfFace3D>(reject);
    source.for_each<DxfSpline>(reject);
    source.for_each<DxfMesh>(reject);
    check(bytes(source) == original, "Failed transform partially edited source");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
