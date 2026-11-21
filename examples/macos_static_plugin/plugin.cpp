// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

// The host passes a file path and receives a status code across the C ABI.
extern "C" int cad_roundtrip(const char *path) noexcept try {
    using namespace libopencad;
    const Point3 start{1, 2, 3};
    const Point3 end{4, 6, 8};
    const auto bytes = create_dwg(DxfVersion::AC1032, {{DwgLineGeometry{start, end}}});

    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();

    std::ifstream input(path, std::ios::binary);
    const auto drawing = DwgDocument::read_with_stats(input);
    const auto &lines = drawing.document.objects().lines;
    if (drawing.stats.source_version != "AC1032" || lines.size() != 1)
        throw std::runtime_error("Expected an AC1032 drawing with one LINE");
    const auto &line = lines.begin()->second;
    if (!(line.start == start && line.end == end))
        throw std::runtime_error("LINE endpoints differ after reading the file");
    return 0;
} catch (const std::exception &error) {
    std::cerr << path << ": " << error.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "Unexpected error in DWG plugin\n";
    return 1;
}
