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

std::string serialized(const DxfDocument& doc) {
    std::ostringstream out;
    doc.write(out);
    return out.str();
}

std::vector<std::string> verify(const DxfDocument& doc, bool edited) {
    const double x[] = {0, edited ? 10. : 8., 8, 0, 12, 16, 21};
    const double y[] = {0, edited ? 1.5 : 0., 6, 6, 1, 2, 4};
    std::vector<std::string> handles;
    std::string model_owner;
    unsigned paths = 0;
    doc.for_each<DxfPolyline2D>([&](const DxfPolyline2D& p) {
        const bool first = paths++ == 0;
        check(p.is_closed() == first && p.layer() == "轮廓", "wrong polyline");
        check(p.vertex_count() == (first ? 4 : 3), "wrong sequence length");
        check(p.default_start_width() == 0.25 && p.default_end_width() == 0.5, "default widths");
        if (first) model_owner = p.owner_handle();
        check(!model_owner.empty() && model_owner == p.owner_handle(), "model space owner");
        handles.push_back(p.handle());
        for (std::size_t i = 0; i < p.vertex_count(); ++i) {
            const auto v = p.vertex(i);
            handles.push_back(v.handle);
            check(v.owner_handle == p.handle(), "VERTEX owner");
            const auto j = first ? i : i + 4;
            check(v.location.x == x[j] && v.location.y == y[j] && v.location.z == 0, "vertex position");
            check(v.start_width == (i ? i * 0.75 : 0.25) &&
                  v.end_width == (i ? i * 0.875 : 0.5), "effective vertex width");
            check(v.bulge == (first && i == 3 ? (edited ? 0.75 : -0.25) : 0.), "outgoing bulge");
        }
        check(p.seqend() && p.seqend()->owner_handle() == p.handle(), "SEQEND owner");
        handles.push_back(p.seqend()->handle());
    });
    check(paths == 2, "expected two adjacent heavy polylines");
    std::set<std::string> distinct(handles.begin(), handles.end());
    check(!distinct.count("") && distinct.size() == handles.size(), "missing or duplicate handle");
    return handles;
}

void check_flat_sequences() {
    // Older records may omit subclass markers, handles, and explicit widths.
    // An incomplete sequence must stop at the next parent, including non-2D variants.
    std::istringstream src("0\nSECTION\n2\nENTITIES\n0\nPOLYLINE\n70\n1\n40\n2\n41\n3\n"
        "0\nVERTEX\n10\n1\n20\n2\n40\n0\n"
        "0\nPOLYLINE\n70\n8\n0\nVERTEX\n10\n3\n20\n4\n0\nSEQEND\n"
        "0\nPOLYLINE\n70\n16\n0\nSEQEND\n0\nPOLYLINE\n70\n64\n0\nSEQEND\n"
        "0\nPOLYLINE\n70\n0\n0\nSEQEND\n0\nVERTEX\n10\n5\n20\n6\n0\nENDSEC\n0\nEOF\n");
    auto doc = DxfDocument::read(src);
    unsigned count = 0;
    doc.for_each<DxfPolyline2D>([&](DxfPolyline2D& p) {
        if (count++ == 0) {
            check(p.vertex_count() == 1 && !p.seqend(), "sequence crossed next parent");
            check(p.vertex(0).start_width == 0 && p.vertex(0).end_width == 3, "explicit zero width");
            p.set_vertex_widths(0, 4, 0);
            check(p.vertex(0).start_width == 4 && p.vertex(0).end_width == 0, "width edit");
        } else check(p.seqend() && !p.vertex_count(), "sequence crossed SEQEND");
    });
    check(count == 2, "3D or mesh sequence exposed as 2D");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 4, "expected format input.dxf output.dxf");
        check_flat_sequences();
        const bool binary = std::string_view(argv[1]) == "binary";
        std::ifstream input(argv[2], std::ios::binary);
        auto doc = DxfDocument::read(input);
        check(doc.is_binary() == binary, "input format");
        const auto handles = verify(doc, false);
        const auto original = serialized(doc);
        unsigned index = 0;
        doc.for_each<DxfPolyline2D>([&](DxfPolyline2D& p) {
            if (index++ != 0) return;
            p.set_vertex_position(1, 10, 1.5);
            p.set_bulge(p.vertex_count() - 1, 0.75);
        });
        std::ofstream out(argv[3], std::ios::binary);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        doc.write(out);
        out.close();
        std::ifstream saved(argv[3], std::ios::binary);
        auto result = DxfDocument::read(saved);
        check(result.is_binary() == binary && verify(result, true) == handles, "lost record identity");

        // Compare the actual output from the second header through EOF. Include all opaque fields.
        const auto second = handles.at(6); // parent, four VERTEXes, SEQEND, next parent
        using namespace std::string_literals;
        const auto marker = binary ? "\0\0POLYLINE\0\x05\0"s + second + '\0'
                                   : "  0\r\nPOLYLINE\r\n  5\r\n"s + second + "\r\n";
        std::ifstream raw(argv[3], std::ios::binary);
        std::ostringstream buffer;
        buffer << raw.rdbuf();
        const auto bytes = buffer.str();
        const auto a = original.find(marker), b = bytes.find(marker);
        check(a != original.npos && b != bytes.npos &&
              original.compare(a, original.npos, bytes, b, bytes.npos) == 0, "second sequence changed");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
