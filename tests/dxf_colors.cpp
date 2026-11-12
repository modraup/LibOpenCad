// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace libopencad;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

const std::array<Color, 4> source{Color::by_layer(), Color::by_block(), Color::from_index(34),
                                  Color::from_rgb(228, 103, 33)};
const std::array<Color, 4> edited{Color::by_block(), Color::by_layer(), Color::from_rgb(18, 160, 231),
                                  Color::from_index(201)};

void verify(const DxfDocument& doc, const std::array<Color, 4>& expected) {
    std::size_t i = 0;
    doc.for_each<DxfEntity>([&](const DxfEntity& entity) {
        check(entity.color() == expected.at(i++), "unexpected color type or value");
        check(entity.layer() == "Colors", "entity layer");
    });
    check(i == expected.size(), "missing color on drawing");
}

void check_field_scope() {
    // Unsigned spelling with a method byte, repeated indices, and non-entity color fields.
    std::istringstream input("0\nSECTION\n2\nENTITIES\n0\nLINE\n100\nAcDbEntity\n"
        "62\n2\n62\n3\n420\n3271526400\n420\n4294967296\n102\n{APP\n62\n99\n102\n}\n"
        "100\nAcDbLine\n62\n45\n1001\nAPP\n1070\n3\n0\nENDSEC\n0\nEOF\n");
    auto doc = DxfDocument::read(input);
    doc.for_each<DxfLine>([](DxfLine& line) {
        check(line.color() == Color::from_rgb(255, 136, 0), "unsigned 420");
        line.set_color(Color::by_layer());
        check(line.color() == Color::by_layer(), "stale entity color");
        line.set_color(Color::none());
        check(line.color() == Color::from_index(257), "None color");
    });
    std::ostringstream out;
    doc.write(out);
    check(out.str().find("420\r\n") == std::string::npos, "stale true color code");
    check(out.str().find("{APP\r\n 62\r\n99") != std::string::npos &&
          out.str().find("AcDbLine\r\n 62\r\n45") != std::string::npos, "unrelated field changed");
}

void check_palette_and_legacy() {
    check(Color::from_index(-32768) == Color::from_index(255), "negative ACI saturation");
    check(Color::from_index(258) == Color::from_index(7), "invalid ACI fallback");
    check(Color::from_index(5).rgb() == Color::Rgb{0,0,255}, "ACI palette");
    check(!Color::by_layer().rgb() && !Color::by_block().rgb(), "inherited RGB");
    check(Color::from_rgb(255,0,0).approximate_index() == 1, "palette tie break");
    std::istringstream in("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n0\nENDSEC\n0\nEOF\n");
    auto doc = DxfDocument::read(in);
    doc.for_each<DxfLine>([](DxfLine& line) {
        line.set_color(Color::from_rgb(250,5,5));
        check(line.color() == Color::from_index(1), "pre-2004 ACI fallback");
    });
    std::ostringstream out;
    doc.write(out);
    check(out.str().find("420\r\n") == std::string::npos, "pre-2004 true color code");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 4, "expected format input.dxf output.dxf");
        check_palette_and_legacy();
        check_field_scope();
        const bool binary = std::string_view(argv[1]) == "binary";
        std::ifstream input(argv[2], std::ios::binary);
        auto doc = DxfDocument::read(input);
        check(doc.is_binary() == binary, "input encoding");
        verify(doc, source);
        std::size_t i = 0;
        doc.for_each<DxfEntity>([&](DxfEntity& entity) { entity.set_color(edited.at(i++)); });
        std::ofstream output(argv[3], std::ios::binary);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        doc.write(output);
        output.close();
        std::ifstream saved(argv[3], std::ios::binary);
        const auto result = DxfDocument::read(saved);
        check(result.is_binary() == binary, "output encoding");
        verify(result, edited);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
