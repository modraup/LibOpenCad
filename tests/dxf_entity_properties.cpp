// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>

#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace libopencad;

namespace {
struct Properties {
    std::string_view linetype;
    LineWeight weight;
    bool invisible;
};
using Drawing = std::array<Properties, 5>;
const Drawing source{{
    {"", LineWeight::by_layer(), false}, {"Dashed", LineWeight::from_value(50), true},
    {"Dotted", LineWeight::by_block(), false}, {"ByBlock", LineWeight::default_weight(), true},
    {"Continuous", LineWeight::from_value(211), false}
}};
const Drawing edited{{
    {"Dashed", LineWeight::from_value(35), true}, {"", LineWeight::by_layer(), false},
    {"ByBlock", LineWeight::default_weight(), true}, {"Dotted", LineWeight::from_value(0), false},
    source.back()
}};

void check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

void verify(const DxfDocument& doc, const Drawing& expected) {
    std::size_t i = 0;
    doc.for_each<DxfEntity>([&](const DxfEntity& entity) {
        const auto& properties = expected.at(i++);
        if (entity.linetype() != properties.linetype || entity.line_weight() != properties.weight ||
            entity.is_invisible() != properties.invisible)
            throw std::runtime_error("unexpected entity properties on handle " + entity.handle());
    });
    check(i == expected.size(), "entity count");
}

void check_record_scope() {
    std::istringstream input("0\nSECTION\n2\nENTITIES\n0\nLINE\n100\nAcDbEntity\n"
        "6\nOld\n6\nDashed\n370\n50\n370\n-2\n370\n32768\n60\n0\n60\n-4\n"
        "102\n{APP\n370\n17\n102\n}\n100\nAcDbLine\n6\nNested\n"
        "1001\nAPP\n1070\n3\n0\nENDSEC\n0\nEOF\n");
    auto doc = DxfDocument::read(input);
    doc.for_each<DxfLine>([](DxfLine& line) {
        check(line.linetype() == "Dashed" && line.line_weight() == LineWeight::by_block() &&
              line.is_invisible(), "last valid common group");
        line.set_linetype("ByLayer");
        line.set_line_weight(LineWeight::by_layer());
        line.set_invisible(false);
    });
    std::ostringstream out;
    doc.write(out);
    check(out.str().find("AcDbEntity\r\n102\r\n{APP\r\n370\r\n17\r\n102\r\n}\r\n"
                         "100\r\nAcDbLine\r\n  6\r\nNested\r\n1001\r\nAPP\r\n1070\r\n3") !=
          std::string::npos, "default omission or group scope");
}

void check_flat_records() {
    std::istringstream input("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n0\nENDSEC\n0\nEOF\n");
    auto doc = DxfDocument::read(input);
    doc.for_each<DxfLine>([](DxfLine& line) {
        line.set_linetype("DASHED");
        line.set_line_weight(LineWeight::from_value(25));
        line.set_invisible(true);
        check(line.linetype() == "DASHED" && line.line_weight() == LineWeight::by_layer() &&
              !line.is_invisible(), "R12 common fields");
    });
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 4, "expected format input.dxf output.dxf");
        check_record_scope();
        check_flat_records();
        check(LineWeight::from_value(35).millimeters() == 0.35 && !LineWeight::by_block().millimeters(),
              "lineweight units");
        const bool binary = std::string_view(argv[1]) == "binary";
        std::ifstream input(argv[2], std::ios::binary);
        auto doc = DxfDocument::read(input);
        check(doc.is_binary() == binary, "input encoding");
        verify(doc, source);
        std::size_t i = 0;
        doc.for_each<DxfEntity>([&](DxfEntity& entity) {
            const auto& value = edited.at(i++);
            if (i == edited.size()) return; // the last entity is the unedited control
            entity.set_linetype(value.linetype.empty() ? "ByLayer" : value.linetype);
            entity.set_line_weight(value.weight);
            entity.set_invisible(value.invisible);
        });
        std::ofstream out(argv[3], std::ios::binary);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        doc.write(out);
        out.close();
        std::ifstream saved(argv[3], std::ios::binary);
        const auto result = DxfDocument::read(saved);
        check(result.is_binary() == binary, "output encoding");
        verify(result, edited);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
