// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace libopencad;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void verify(const DxfLwPolyline& path, bool edited) {
    check(path.is_closed() && path.plinegen(), "lost polyline flags");
    check(path.elevation() == 2.5 && path.thickness() == 0.25 && path.constant_width() == 0,
          "lost polyline dimensions");
    check(path.layer() == "轮廓" && path.normal().z == 1, "wrong layer or normal");
    const auto vertices = path.vertices();
    check(vertices.size() == 4, "wrong vertex count");
    const double x[] = {0, edited ? 10. : 8., 8, 0};
    const double y[] = {0, edited ? 1.5 : 0., 6, 6};
    const double bulges[] = {edited ? -0.5 : 0.5, 0, 0, edited ? 0.75 : -0.25};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto& v = vertices[i];
        check(v.x == x[i] && v.y == y[i] && v.bulge == bulges[i], "wrong geometry or bulge");
        check(v.start_width == i * 0.125 && v.end_width == v.start_width + 0.25, "lost widths");
        check(v.vertex_id == 70001 + static_cast<int>(i), "lost vertex ID");
    }
}

void sparse_vertices() {
    std::istringstream input(
        "0\nSECTION\n2\nENTITIES\n0\nLWPOLYLINE\n100\nAcDbPolyline\n90\n3\n70\n129\n"
        "10\n0\n20\n0\n102\n{APP\n10\n99\n20\n99\n42\n123\n102\n}\n"
        "10\n1\n20\n2\n91\n23\n10\n3\n20\n4\n42\n-1\n1001\nAPP\n1042\n17\n"
        "0\nENDSEC\n0\nEOF\n");
    auto doc = DxfDocument::read(input);
    doc.for_each<DxfLwPolyline>([](DxfLwPolyline& p) {
        auto v = p.vertices();
        check(v.size() == 3 && v[0].bulge == 0 && v[1].bulge == 0, "missing bulge defaults");
        p.set_bulge(1, 0.25);
        p.set_closed(false);
        check(!p.is_closed() && p.plinegen(), "closure changed PLINEGEN");
        try {
            p.set_vertex_position(1, 99, std::numeric_limits<double>::infinity());
            throw std::runtime_error("nonfinite position accepted");
        } catch (const std::invalid_argument&) {}
        try {
            p.set_bulge(3, 1);
            throw std::runtime_error("out-of-range index accepted");
        } catch (const std::out_of_range&) {}
    });
    std::ostringstream out;
    doc.write(out);
    check(out.str().find(" 42\r\n123\r\n") != std::string::npos, "lost application data");
    std::istringstream saved(out.str());
    int count = 0;
    DxfDocument::read(saved).for_each<DxfLwPolyline>([&](const DxfLwPolyline& p) {
        ++count;
        auto v = p.vertices();
        check(v.size() == 3 && v[1].x == 1 && v[1].y == 2 && v[1].vertex_id == 23,
              "partial vertex edit or shifted ID");
        check(v[0].bulge == 0 && v[1].bulge == 0.25 && v[2].bulge == -1, "shifted bulge");
    });
    check(count == 1, "missing sparse polyline");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 4, "expected format input.dxf output.dxf");
        sparse_vertices();
        const bool binary = std::string_view(argv[1]) == "binary";
        std::ifstream input(argv[2], std::ios::binary);
        auto doc = DxfDocument::read(input);
        check(doc.is_binary() == binary, "input format");
        int count = 0;
        doc.for_each<DxfLwPolyline>([&](DxfLwPolyline& p) {
            ++count;
            verify(p, false);
            p.set_vertex_position(1, 10, 1.5);
            p.set_bulge(0, -0.5);
            p.set_bulge(3, 0.75);
        });
        check(count == 1, "expected one input polyline");
        std::ofstream output(argv[3], std::ios::binary);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        doc.write(output);
        output.close();
        std::ifstream saved(argv[3], std::ios::binary);
        auto result = DxfDocument::read(saved);
        check(result.is_binary() == binary, "output format");
        count = 0;
        result.for_each<DxfLwPolyline>([&](const DxfLwPolyline& p) { ++count; verify(p, true); });
        check(count == 1, "expected one output polyline");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
