// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace libopencad;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

std::vector<std::string> verify(const DxfDocument& doc, bool edited) {
    std::vector<std::string> handles;
    unsigned count = 0;
    doc.for_each<DxfPolygonMesh>([&](const DxfPolygonMesh& mesh) {
        const bool first = count++ == 0;
        const int rows = first ? 3 : 4, cols = first ? 4 : 3;
        check(mesh.m_vertex_count() == rows && mesh.n_vertex_count() == cols, "grid dimensions");
        check(mesh.is_closed_m() == !first && mesh.is_closed_n() == first, "grid closure");
        check(mesh.vertex_count() == 12 && mesh.layer() == "网格", "mesh header");
        handles.push_back(mesh.handle());
        for (int m = 0; m < rows; ++m) {
            for (int n = 0; n < cols; ++n) {
                const auto index = mesh.grid_index(m, n);
                check(index == static_cast<std::size_t>(m * cols + n), "grid stride");
                const auto v = mesh.vertex(index);
                const bool selected = m == 1 && n == 2;
                const Point3 xyz = first && edited && selected ? Point3{1.5, 2.25, -7.}
                    : Point3{(first ? 0. : 20.) + m, double(n), double(10 * m - n)};
                check(v.position.x == xyz.x && v.position.y == xyz.y && v.position.z == xyz.z,
                      "unexpected grid vertex position");
                check(v.layer == (selected ? "测点" : "网格") && v.flags == 64, "vertex properties");
                check(v.owner_handle == mesh.handle(), "vertex owner");
                handles.push_back(v.handle);
            }
        }
        check(mesh.seqend() && mesh.seqend()->owner_handle() == mesh.handle(), "SEQEND owner");
        handles.push_back(mesh.seqend()->handle());
    });
    std::set<std::string> unique(handles.begin(), handles.end());
    check(count == 2 && !unique.count("") && unique.size() == handles.size(), "mesh identity");
    return handles;
}

void check_flat_mesh() {
    // The grid is truncated. A subsequent polyface record also has bit 16 set.
    std::istringstream in("0\nSECTION\n2\nENTITIES\n0\nPOLYLINE\n70\n24\n71\n2\n72\n3\n"
        "0\nVERTEX\n10\n4\n20\n5\n0\nSEQEND\n0\nPOLYLINE\n70\n80\n0\nSEQEND\n0\nENDSEC\n0\nEOF\n");
    auto doc = DxfDocument::read(in);
    unsigned count = 0;
    doc.for_each<DxfPolygonMesh>([&](DxfPolygonMesh& mesh) {
        ++count;
        check(mesh.vertex_count() == 1 && mesh.vertex(mesh.grid_index(0, 0)).position.z == 0,
              "flat polygon mesh");
        auto rejects = [&](std::size_t m, std::size_t n) {
            try { mesh.grid_index(m, n); }
            catch (const std::out_of_range&) { return true; }
            return false;
        };
        check(rejects(1, 0) && rejects(0, 3) && rejects(2, 0), "missing or out-of-grid vertex");
    });
    check(count == 1, "mesh flag precedence");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 4, "expected format input.dxf output.dxf");
        check_flat_mesh();
        std::ifstream input(argv[2], std::ios::binary);
        auto doc = DxfDocument::read(input);
        const bool binary = std::string_view(argv[1]) == "binary";
        check(doc.is_binary() == binary, "input format");
        const auto handles = verify(doc, false);
        unsigned index = 0;
        doc.for_each<DxfPolygonMesh>([&](DxfPolygonMesh& mesh) {
            if (index++ == 0) mesh.set_vertex_position(mesh.grid_index(1, 2), {1.5, 2.25, -7.});
        });
        std::ofstream output(argv[3], std::ios::binary);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        doc.write(output);
        output.close();
        std::ifstream saved(argv[3], std::ios::binary);
        const auto result = DxfDocument::read(saved);
        check(result.is_binary() == binary && verify(result, true) == handles, "format or handles changed");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
