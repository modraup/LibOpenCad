// SPDX-License-Identifier: MPL-2.0
#include <libopencad/ascii_dxf.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

using namespace libopencad;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
bool equal(Point3 a, Point3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

void check_parser() {
    for (const auto* data : {"0\nSECTION\n2\nENTITIES\n0\nEOF\n", "0\n", "bad\nLINE\n", ""}) {
        std::istringstream input(data);
        bool rejected = false;
        try { AsciiDxf::read(input); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "invalid DXF accepted");
    }
    // Flat records, optional Z, opaque application data and a text in a block.
    std::istringstream input(
        "0\nSECTION\n2\nBLOCKS\n0\nTEXT\n1\nuntouched\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n0\nTEXT\n102\n{APP\n1\nprivate\n102\n}\n"
        "1\n A^ B \n10\n1\n20\n2\n1001\nAPP\n1000\npayload\n0\nENDSEC\n0\nEOF\n");
    auto doc = AsciiDxf::read(input);
    int count = 0;
    doc.for_each<DxfText>([&](DxfText& t) {
        ++count;
        check(t.text() == " A^B " && equal(t.insertion_point(), {1, 2, 0}), "flat TEXT");
        t.set_text(" ^J \t");
        t.set_insertion_point({1, 2, 4});
    });
    check(count == 1, "entity section scope");
    std::ostringstream bytes;
    doc.write(bytes);
    for (auto expected : {"untouched", "private", "payload", " 30\r\n4\r\n1001"})
        check(bytes.str().find(expected) != std::string::npos, "lost opaque field or optional Z");
    std::istringstream saved(bytes.str());
    AsciiDxf::read(saved).for_each<DxfText>([](const DxfText& t) {
        check(t.text() == " ^J \t" && t.insertion_point().z == 4, "flat TEXT read-back");
    });
}

void check_binary_stream() {
    using namespace std::string_literals;
    const auto signature = "AutoCAD Binary DXF\r\n\x1a\0"s;
    // R12-style one-byte codes, escaped 1004 binary chunk and 1071 integer.
    auto compact = signature + "\0SECTION\0\x02" "ENTITIES\0\0TEXT\0\x01" "A^J\0"s +
        "\xff\xec\x03\x03\0\xff\n\xff\x2f\x04\x00\x01\x02\xfe"s + "\0ENDSEC\0\0EOF\0"s;
    std::istringstream input(compact);
    auto doc = DxfDocument::read(input);
    std::ostringstream copy;
    doc.write(copy);
    check(copy.str() == compact, "compact group or binary chunk changed");
    int texts = 0;
    doc.for_each<DxfText>([&](DxfText& t) {
        ++texts;
        check(t.text() == "A^J", "binary caret is literal");
        t.set_text("A\0B\r\nC^J"s);
        check(t.text() == "AB\\PC^J", "binary string terminator and paragraph handling");
    });
    check(texts == 1, "missing compact TEXT");
    for (const auto& tail : {"\0\0EOF"s, "\0"s, "\x36\x01\x03\xff"s, "\x0a\0\x01"s}) {
        bool rejected = false;
        std::istringstream incomplete(signature + tail);
        try { DxfDocument::read(incomplete); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "incomplete binary pair accepted");
    }
}

void edit_fixture(const char* input_path, const char* output_path, bool binary) {
    std::ifstream input(input_path, std::ios::binary);
    check(input.is_open(), "cannot open input fixture");
    auto doc = DxfDocument::read(input);
    check(doc.is_binary() == binary, "wrong input format");
    if (binary) {
        std::ifstream original(input_path, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(original), {}};
        std::ostringstream copy;
        doc.write(copy);
        check(copy.str() == bytes, "unmodified binary DXF changed");
    }
    const auto geometry = binary ? "几何" : "Geometry";
    const auto notes = binary ? "标注" : "Notes";
    unsigned layers = 0, changed_layers = 0, lines = 0, circles = 0, texts = 0;
    doc.for_each<DxfLayer>([&](DxfLayer& layer) {
        ++layers;
        if (layer.name() == geometry) {
            check(layer.signed_color_index() == 3, "initial Geometry layer color");
            layer.set_signed_color_index(5);
            ++changed_layers;
        }
    });
    doc.for_each<DxfLine>([&](DxfLine& line) {
        ++lines;
        check(line.layer() == geometry && equal(line.start(), {1,2,0}) &&
              equal(line.end(), {10,20,0}), "initial LINE");
        line.set_end({25, -1.5, 2});
    });
    doc.for_each<DxfCircle>([&](DxfCircle& c) {
        ++circles;
        check(c.layer() == geometry && equal(c.center(), {-2,4,1}) &&
              c.radius() == 3.5, "initial CIRCLE");
        c.set_radius(7.25);
    });
    doc.for_each<DxfText>([&](DxfText& t) {
        ++texts;
        check(t.layer() == notes && t.text() == (binary ? "一层平面图" : "Revision A") && t.height() == 2.5 &&
              equal(t.insertion_point(), {3,5,0}), "initial TEXT");
        t.set_text(binary ? "二层平面图 ^J" : "修订 B");
        t.set_layer(geometry);
    });
    check(layers == 3 && changed_layers == 1 && lines == 1 && circles == 1 && texts == 1,
          "unexpected fixture inventory");
    std::ofstream output(output_path, std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    doc.write(output);
    output.close();
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool binary = argc == 4 && std::string_view(argv[3]) == "binary";
        check(argc == 3 || binary, "expected input.dxf output.dxf [binary]");
        if (binary) check_binary_stream();
        else check_parser();
        edit_fixture(argv[1], argv[2], binary);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
