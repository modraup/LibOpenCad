// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace libopencad;
using Indices = std::array<int, 4>;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

std::vector<std::string> verify(const DxfDocument& doc, bool edited) {
    std::vector<std::string> handles;
    unsigned count = 0;
    doc.for_each<DxfPolyfaceMesh>([&](const DxfPolyfaceMesh& mesh) {
        const bool first = count++ == 0;
        check(mesh.vertex_count() == 5 && mesh.face_count() == 3, "polyface child counts");
        check(mesh.layer() == "网面" && !mesh.owner_handle().empty(), "mesh header");
        handles.push_back(mesh.handle());
        const Point3 xyz[] = {{0,0,0}, {4,0,0}, {4,4,0}, {0,4,0}, {2,2,3}};
        for (std::size_t i = 0; i < mesh.vertex_count(); ++i) {
            const auto v = mesh.vertex(i);
            check(v.position.x == xyz[i].x + (first ? 0 : 20) &&
                  v.position.y == xyz[i].y && v.position.z == xyz[i].z, "coordinate vertex or order");
            check(v.flags == 192 && v.owner_handle == mesh.handle(), "coordinate record identity");
            handles.push_back(v.handle);
        }
        const bool changed = first && edited;
        const Indices expected[] = {changed ? Indices{-1,3,-5,0} : Indices{1,-2,5,0},
                                    changed ? Indices{2,-3,4,-1} : Indices{-1,2,3,4},
                                    {3,4,-5,5}};
        for (std::size_t i = 0; i < mesh.face_count(); ++i) {
            const auto face = mesh.face(i);
            check(face.indices == expected[i] && face.is_triangle() == (i != 1), "signed face indices");
            check(face.owner_handle == mesh.handle() && face.layer == (i == 0 ? "网面" : "0"),
                  "face record identity");
            handles.push_back(face.handle);
        }
        check(mesh.seqend() && mesh.seqend()->owner_handle() == mesh.handle(), "SEQEND owner");
        handles.push_back(mesh.seqend()->handle());
    });
    std::set<std::string> unique(handles.begin(), handles.end());
    check(count == 2 && !unique.count("") && unique.size() == handles.size(), "polyface record handles");
    return handles;
}

// Legacy flat records, interleaved child types, and inaccurate header counts.
void check_flat_faces() {
    std::istringstream in("0\nSECTION\n2\nENTITIES\n0\nPOLYLINE\n70\n80\n71\n9\n72\n8\n"
        "0\nVERTEX\n70\n64\n10\n1\n0\nVERTEX\n70\n128\n71\n1\n72\n-2\n73\n3\n"
        "0\nVERTEX\n70\n192\n10\n2\n0\nVERTEX\n70\n0\n10\n99\n"
        "0\nVERTEX\n70\n64\n10\n3\n0\nVERTEX\n70\n64\n10\n4\n0\nSEQEND\n0\nENDSEC\n0\nEOF\n");
    auto doc = DxfDocument::read(in);
    std::ostringstream before;
    doc.write(before);
    int count = 0;
    doc.for_each<DxfPolyfaceMesh>([&](DxfPolyfaceMesh& mesh) {
        ++count;
        check(mesh.vertex_count() == 4 && mesh.face_count() == 1 &&
              mesh.vertex(2).position.x == 3, "flat flag classification");
        const auto triangle = mesh.face(0).indices;
        check(triangle == Indices{1,-2,3,0}, "flat triangle");
        for (const auto bad : {Indices{-1,2,0,0}, Indices{-1,2,3,-5}, Indices{1,2,3,65537}}) {
            try { mesh.set_face_indices(0, bad); }
            catch (const std::invalid_argument&) {
                check(mesh.face(0).indices == triangle, "partial edit after invalid index");
                continue;
            }
            throw std::runtime_error("accepted invalid face reference");
        }
        mesh.set_face_indices(0, {1,2,3,-4});
        check(!mesh.face(0).is_triangle() && mesh.face(0).indices[3] == -4, "triangle to quad");
        mesh.set_face_indices(0, triangle);
    });
    std::ostringstream after;
    doc.write(after);
    check(count == 1 && before.str() == after.str(), "quad to triangle or child order changed");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 4, "expected format input.dxf output.dxf");
        check_flat_faces();
        std::ifstream input(argv[2], std::ios::binary);
        auto doc = DxfDocument::read(input);
        const bool binary = std::string_view(argv[1]) == "binary";
        check(doc.is_binary() == binary, "input encoding");
        const auto handles = verify(doc, false);
        unsigned i = 0;
        doc.for_each<DxfPolyfaceMesh>([&](DxfPolyfaceMesh& mesh) {
            if (i++ == 0) {
                mesh.set_face_indices(0, {-1,3,-5,0});
                mesh.set_face_indices(1, {2,-3,4,-1});
            }
        });
        std::ofstream out(argv[3], std::ios::binary);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        doc.write(out);
        out.close();
        std::ifstream saved(argv[3], std::ios::binary);
        const auto result = DxfDocument::read(saved);
        check(result.is_binary() == binary && verify(result, true) == handles, "output format or handles");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
