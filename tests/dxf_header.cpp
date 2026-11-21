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

void check_header(std::string fields, DxfVersion version, int units) {
    // Header-like values in other sections must not override the document metadata.
    std::istringstream input("0\nSECTION\n2\nHEADER\n" + fields +
        "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nTEXT\n9\n$INSUNITS\n70\n9\n"
        "9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n0\nEOF\n");
    auto doc = DxfDocument::read(input);
    check(doc.version() == version, "unexpected $ACADVER");
    check(doc.insertion_units() == units, "unexpected $INSUNITS");
}

void check_ucs_roundtrip(bool binary) {
    std::istringstream input("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1032\n"
        "9\n$UCSORG\n20\n2\n9\n$LUNITS\n70\n3\n9\n$PSTYLEMODE\n290\n0\n0\nENDSEC\n0\nEOF\n");
    std::ostringstream encoded;
    DxfDocument::read(input).write(encoded, binary);
    std::istringstream source(encoded.str());
    auto doc = DxfDocument::read(source);
    const Point3 origin{2.5, -3.5, 4.5};
    doc.set_model_space_ucs(origin, {0, 1, 0}, {-1, 0, 0});
    check(doc.model_space_ucs_origin() == origin, "UCS partial point update");
    std::ostringstream output;
    doc.write(output);
    const auto bytes = output.str();
    check(bytes.find("$UCSORG") == bytes.rfind("$UCSORG"), "duplicated UCS header variable");
    std::istringstream saved(bytes);
    const auto result = DxfDocument::read(saved);
    check(result.model_space_ucs_origin() == origin, "UCS point readback");
    check(result.linear_unit_format() == 3 && !result.plot_style_mode(), "numeric header values");
}

void check_values() {
    const DxfVersion versions[] = {DxfVersion::AC1009, DxfVersion::AC1012, DxfVersion::AC1014,
        DxfVersion::AC1015, DxfVersion::AC1018, DxfVersion::AC1021, DxfVersion::AC1024,
        DxfVersion::AC1027, DxfVersion::AC1032};
    for (auto version : versions) {
        const auto suffix = std::to_string(static_cast<unsigned>(version));
        check(dxf_version_string(version) == "AC" + suffix, "canonical DXF version");
        for (const auto prefix : {"AC", "AD"})
            check_header("9\n$ACADVER\n1\n" + std::string(prefix) + suffix + "\n", version, 0);
    }
    check_header("", DxfVersion::AC1032, 0);
    check_header("9\n$INSUNITS\n70\n-32768\n9\n$ACADVER\n1\nAC9999\n", DxfVersion::Unknown, -32768);
    check_header("9\n$INSUNITS\n70\n +32767 \n9\n$INSUNITS\n70\n32768\n"
                 "9\n$INSUNITS\n70\noops\n9\n$INSUNITS\n1\n7\n", DxfVersion::AC1032, 32767);
    check_header("9\n$INSUNITS\n70\n4\n9\n$INSUNITS\n90\n2\n", DxfVersion::AC1032, 2);
    check(dxf_version_string(DxfVersion::Unknown) == "UNKNOWN", "unknown version label");
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            check_values();
            check_ucs_roundtrip(false);
            check_ucs_roundtrip(true);
            return 0;
        }
        check(argc == 4, "expected format input.dxf result.txt");
        std::ifstream input(argv[2], std::ios::binary);
        check(input.is_open(), "cannot open drawing");
        const auto doc = DxfDocument::read(input);
        const bool binary = std::string_view(argv[1]) == "binary";
        check(doc.is_binary() == binary, "wrong drawing format");
        check(doc.version() == (binary ? DxfVersion::AC1027 : DxfVersion::AC1018), "fixture version");
        check(doc.insertion_units() == (binary ? 1 : 4), "fixture units");
        std::ofstream output(argv[3]);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output << dxf_version_string(doc.version()) << ' ' << doc.insertion_units() << '\n';
        for (const auto &name : doc.dimension_header_variables()) output << name << '\n';
        output.close();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
