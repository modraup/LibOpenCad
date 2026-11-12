// SPDX-License-Identifier: MPL-2.0
#include <libopencad/ascii_dxf.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

using namespace libopencad;

namespace {
void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

void check_text(const std::string& version, const std::string& page,
                const std::string& bytes, const std::string& expected) {
    // The declarations can occur in either order in HEADER.
    std::istringstream in("0\nSECTION\n2\nHEADER\n9\n$DWGCODEPAGE\n3\n" + page +
        "\n9\n$ACADVER\n1\n" + version + "\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
        "0\nTEXT\n1\n" + bytes + "\n0\nENDSEC\n0\nEOF\n");
    auto doc = AsciiDxf::read(in);
    int texts = 0;
    doc.for_each<DxfText>([&](const DxfText& t) {
        ++texts;
        check(t.text() == expected, "page selection or escape decoding");
    });
    check(texts == 1, "missing TEXT");
}

void edit(const char* src, const char* dst) {
    std::ifstream in(src, std::ios::binary);
    auto doc = AsciiDxf::read(in);
    int layers = 0, texts = 0;
    doc.for_each<DxfLayer>([&](const DxfLayer& layer) {
        if (layer.name() == "建筑" || layer.name() == "标注") ++layers;
    });
    doc.for_each<DxfText>([&](DxfText& t) {
        ++texts;
        check(t.layer() == "建筑" && t.text() == "一层平面图", "GBK input");
        t.set_layer("标注");
        t.set_text("二层平面图 😀");
    });
    check(layers == 2 && texts == 1, "unexpected fixture");
    std::ofstream out(dst, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    doc.write(out);
    out.close();

    std::ifstream saved(dst, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(saved), {}};
    check(bytes.find("  8\r\n\xb1\xea\xd7\xa2\r\n") != bytes.npos, "layer is not GBK");
    check(bytes.find("  1\r\n\xb6\xfe\xb2\xe3\xc6\xbd\xc3\xe6\xcd\xbc "
                     "\\U+D83D\\U+DE00\r\n") != bytes.npos, "text is not GBK plus CIF");
    std::istringstream reopen(bytes);
    AsciiDxf::read(reopen).for_each<DxfText>([](const DxfText& t) {
        check(t.layer() == "标注" && t.text() == "二层平面图 😀", "C++ GBK read-back");
    });
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 3, "expected input.dxf output.dxf");
        check_text("AC1018", "gB2312", "\x81\x5eJ", "乛J"); // 5E inside GBK is not '^'.
        check_text("AC1018", "ANSI_936", "\\M+5BCFE\\U+4E2D", "件中");
        check_text("AC1018", "ANSI_936", "\x80", "€");
        check_text("AC1015", "ANSI_1252", "caf\xe9", "café");
        check_text("AD1015", "ANSI_1252", "caf\xe9", "café");
        check_text("AC1021", "ANSI_936", "中文\\U+4E2D", "中文\\U+4E2D");
        edit(argv[1], argv[2]);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
