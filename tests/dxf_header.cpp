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
        output.close();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
