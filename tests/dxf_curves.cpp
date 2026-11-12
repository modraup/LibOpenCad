// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace libopencad;

namespace {
void check(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}
bool same(Point3 a, Point3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool near(double a, double b) { return std::abs(a - b) < 1e-12; }

void curves(DxfDocument& doc, bool edit) {
    const double pi = std::acos(-1.);
    int arcs = 0, ellipses = 0;
    doc.for_each<DxfArc>([&](DxfArc& a) {
        ++arcs;
        check(a.layer() == "曲线", "ARC layer");
        if (edit) {
            check(same(a.center(), {2,3,4}) && a.radius() == 5, "initial ARC geometry");
            check(same(a.normal(), {0,0,1}) && a.thickness() == 0, "default ARC extrusion");
            check(near(a.start_angle_radians(), 11*pi/6) && near(a.end_angle_radians(), pi/6),
                  "initial ARC angles");
            a.set_center({4,-2,3});
            a.set_radius(7);
            a.set_normal({0,1,0});
            a.set_thickness(1.5);
            a.set_start_angle_radians(1.5*pi);
            a.set_end_angle_radians(pi/4);
        }
        check(same(a.center(), {4,-2,3}) && a.radius() == 7 && a.thickness() == 1.5, "edited ARC");
        check(same(a.normal(), {0,1,0}) && near(a.start_angle_radians(), 1.5*pi) &&
              near(a.end_angle_radians(), pi/4), "edited ARC orientation");
    });
    doc.for_each<DxfEllipse>([&](DxfEllipse& e) {
        ++ellipses;
        check(e.layer() == "曲线", "ELLIPSE layer");
        if (edit) {
            check(same(e.center(), {9,3,2}) && same(e.major_axis(), {4,3,0}) &&
                  near(e.minor_axis_ratio(), 0.6), "initial ELLIPSE geometry");
            check(same(e.normal(), {0,0,1}) && near(e.start_parameter(), 0) &&
                  near(e.end_parameter(), 2*pi), "initial full ellipse");
            e.set_center({1,2,3});
            e.set_major_axis({6,0,2});
            e.set_minor_axis_ratio(0.25);
            e.set_normal({0,1,0});
            e.set_start_parameter(0.125);
            e.set_end_parameter(3.5);
        }
        check(same(e.center(), {1,2,3}) && same(e.major_axis(), {6,0,2}) &&
              e.minor_axis_ratio() == 0.25 && same(e.normal(), {0,1,0}), "edited ELLIPSE");
        check(e.start_parameter() == 0.125 && e.end_parameter() == 3.5, "edited ellipse arc");
    });
    check(arcs == 1 && ellipses == 1, "curve inventory");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 4, "expected format input.dxf output.dxf");
        bool binary = std::string_view(argv[1]) == "binary";
        std::ifstream input(argv[2], std::ios::binary);
        auto doc = DxfDocument::read(input);
        check(doc.is_binary() == binary, "input format");
        curves(doc, true);
        std::ofstream output(argv[3], std::ios::binary);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        doc.write(output);
        output.close();

        std::ifstream saved(argv[3], std::ios::binary);
        auto copy = DxfDocument::read(saved);
        check(copy.is_binary() == binary, "output format");
        curves(copy, false);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
