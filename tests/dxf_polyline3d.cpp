// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace libopencad;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
std::string file_bytes(const char* path) {
    std::ifstream in(path, std::ios::binary);
    check(in.is_open(), "cannot open DXF");
    return {std::istreambuf_iterator<char>(in), {}};
}

struct Identity {
    std::vector<std::string> handles;
    std::string second_parent;
};

Identity verify(const DxfDocument& doc, bool edited) {
    const Point3 expected[] = {{0,0,-2}, edited ? Point3{10,1.5,-6.25} : Point3{8,0,3},
        {8,6,7.5}, {0,6,1}, {12,1,11}, {16,2,-4.5}, {21,4,9}};
    Identity identity;
    std::size_t paths = 0;
    std::string model_owner;
    doc.for_each<DxfPolyline3D>([&](const DxfPolyline3D& p) {
        const bool first = paths++ == 0;
        check(paths <= 2 && p.is_closed() == first && p.layer() == "路径", "unexpected 3D path");
        check(p.vertex_count() == (first ? 4 : 3), "unexpected vertex count");
        if (first) model_owner = p.owner_handle();
        else identity.second_parent = p.handle();
        check(!model_owner.empty() && p.owner_handle() == model_owner, "parent owner");
        identity.handles.push_back(p.handle());
        for (std::size_t i = 0; i < p.vertex_count(); ++i) {
            const auto v = p.vertex(i);
            const auto xyz = expected[first ? i : i + 4];
            check(v.position.x == xyz.x && v.position.y == xyz.y && v.position.z == xyz.z,
                  "incorrect XYZ or vertex order");
            check(v.owner_handle == p.handle() && v.flags == 32, "VERTEX owner or flags");
            check(v.layer == (i == 1 ? "测点" : "路径"), "vertex layer");
            identity.handles.push_back(v.handle);
        }
        check(p.seqend() && p.seqend()->owner_handle() == p.handle(), "SEQEND owner");
        identity.handles.push_back(p.seqend()->handle());
    });
    const std::set<std::string> unique(identity.handles.begin(), identity.handles.end());
    check(paths == 2 && !unique.count("") && unique.size() == identity.handles.size(), "record identity");
    return identity;
}

void check_flat_sequence() {
    std::istringstream input("0\nSECTION\n2\nENTITIES\n0\nPOLYLINE\n70\n8\n0\nVERTEX\n"
        "10\n1\n20\n2\n0\nSEQEND\n0\nPOLYLINE\n70\n24\n0\nSEQEND\n0\nPOLYLINE\n70\n72\n"
        "0\nSEQEND\n0\nPOLYLINE\n70\n1\n0\nSEQEND\n0\nENDSEC\n0\nEOF\n");
    auto doc = DxfDocument::read(input);
    unsigned count = 0;
    doc.for_each<DxfPolyline3D>([&](DxfPolyline3D& p) {
        ++count;
        check(p.vertex_count() == 1 && p.seqend() && p.vertex(0).position.z == 0, "flat 3D sequence");
        p.set_vertex_position(0, {1,2,-3});
        check(p.vertex(0).position.z == -3, "missing Z not added");
    });
    check(count == 1, "2D or mesh flags mistaken for 3D");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 4, "expected format input.dxf output.dxf");
        check_flat_sequence();
        const bool binary = std::string_view(argv[1]) == "binary";
        const auto original = file_bytes(argv[2]);
        std::istringstream input(original);
        auto doc = DxfDocument::read(input);
        check(doc.is_binary() == binary, "input format");
        const auto before = verify(doc, false);
        unsigned index = 0;
        doc.for_each<DxfPolyline3D>([&](DxfPolyline3D& p) {
            if (index++ == 0) p.set_vertex_position(1, {10,1.5,-6.25});
        });
        std::ofstream out(argv[3], std::ios::binary);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        doc.write(out);
        out.close();

        const auto bytes = file_bytes(argv[3]);
        std::istringstream saved(bytes);
        const auto result = DxfDocument::read(saved);
        check(result.is_binary() == binary && verify(result, true).handles == before.handles,
              "output format or handles changed");
        using namespace std::string_literals;
        const auto marker = binary ? "\0\0POLYLINE\0\x05\0"s + before.second_parent + '\0'
                                   : "  0\r\nPOLYLINE\r\n  5\r\n"s + before.second_parent + "\r\n";
        const auto a = original.find(marker), b = bytes.find(marker);
        check(a != original.npos && b != bytes.npos &&
              original.compare(a, original.npos, bytes, b, bytes.npos) == 0, "second path bytes changed");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
