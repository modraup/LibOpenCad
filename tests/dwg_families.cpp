// SPDX-License-Identifier: MPL-2.0
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_r2000.hpp>
#include <libopencad/dwg_sections.hpp>
using namespace libopencad;
int main(int argc, char **argv) try {
    if (argc != 4)
        return 2;
    std::ifstream stream(argv[2], std::ios::binary);
    std::string data{std::istreambuf_iterator<char>(stream), {}};
    std::string prefix = argv[3];
    if (std::string_view(argv[1]) == "create") {
        auto version = dxf_version_from_string(argv[2]);
        data = create_dwg(version, {{DwgLineGeometry{{7., -2., 4.}, {-8., 6., 9.}}},
                                    {DwgCircleGeometry{{-2., 4., 0.}, 4.25}, false, 2., Vector3::UNIT_Y},
                                    {DwgArcGeometry{{8., 3., 2.}, 7.25, 5.5, 0.25}, true},
                                    {DwgLineGeometry{{0., 0., 0.}, {-1., 3., 0.}}, true}});
        std::ofstream dwg(prefix + ".dwg", std::ios::binary);
        dwg.write(data.data(), data.size());
        dwg.close();
        if (!dwg)
            return 1;
    }
    if (std::string_view(argv[1]) == "parity") {
        // Flip one parity byte in the first of the three header codewords.
        data.at(128 + 3 * 239) ^= 1;
        try {
            read_dwg_paged_section(data, "AcDb:Header");
        } catch (const std::invalid_argument &e) {
            return std::string_view(e.what()) == "AC21 Reed-Solomon parity mismatch" ? 0 : 1;
        }
        return 1;
    }
    if (std::string_view(argv[1]) == "block") {
        auto objects = read_dwg_objects(data);
        if (objects.arcs.size() != 1 ||
            objects.arcs.begin()->second.properties.legacy_block_name != "DETAIL" ||
            objects.arcs.begin()->second.properties.mode != 0)
            throw std::runtime_error("AC1009 block owner mismatch");
        data = set_dwg_radius(data, objects.arcs.begin()->first, 12.25);
        std::ofstream dwg(prefix, std::ios::binary);
        dwg.write(data.data(), data.size());
        dwg.close();
        return !dwg;
    }
    if (std::string_view(argv[1]) == "sections") {
        if (data.compare(0, 6, "AC1021") != 0) {
            auto damaged = data;
            damaged.at(0x100 + 32 + 1) ^= 1;
            try {
                read_dwg_paged_section(damaged, "AcDb:Header");
                throw std::runtime_error("Accepted damaged AC18 header data page");
            } catch (const std::invalid_argument &err) {
                if (std::string_view(err.what()) != "DWG data page checksum mismatch") throw;
            }
        }
        for (std::string name : {"Header", "Classes", "Handles", "AcDbObjects"}) {
            auto bytes = read_dwg_paged_section(data, "AcDb:" + name);
            std::ofstream output(prefix + "-" + name, std::ios::binary);
            output.write(bytes.data(), bytes.size());
            output.close();
            if (!output)
                return 1;
        }
        return 0;
    }
    if (std::string_view(argv[1]) == "edit") {
        auto original = read_dwg_objects(data);
        if (original.circles.size() != 1 || original.arcs.size() != 1)
            return 1;
        data = set_dwg_radius(data, original.circles.begin()->first, 1.);
        data = set_dwg_radius(data, original.arcs.begin()->first, 14.75);
        std::ofstream dwg(prefix + ".dwg", std::ios::binary);
        dwg.write(data.data(), data.size());
        dwg.close();
        if (!dwg)
            return 1;
    }
    auto records = read_dwg_objects(data);
    if (std::string_view(argv[1]) == "edit" &&
        (records.circles.begin()->second.radius != 1. || records.arcs.begin()->second.radius != 14.75))
        return 1;
    if (std::string_view(argv[1]) == "create") {
        if (records.lines.size() != 2 || records.circles.size() != 1 || records.arcs.size() != 1)
            throw std::runtime_error("Incomplete new drawing");
        if (data.substr(0, 6) != "AC1009") {
            auto &model = records.symbols.at(21);
            auto &paper = records.symbols.at(24);
            if (model.name != "*Model_Space" || paper.name != "*Paper_Space" ||
                records.symbols.at(16).name != "0")
                throw std::runtime_error("Unexpected default symbol names");
            if (data.substr(0, 6) >= "AC1018") {
                if (model.members != std::vector<std::uint64_t>{256, 257} ||
                    paper.members != std::vector<std::uint64_t>{258, 259})
                    throw std::runtime_error("DWG block member list mismatch");
            } else if (model.first != 256 || model.last != 257 || paper.first != 258 || paper.last != 259)
                throw std::runtime_error("DWG block member links mismatch");
        } else if (records.legacy_symbol_names.at(51).at(0) != "0")
            throw std::runtime_error("Unexpected AC1009 layer slot");
    }
    std::ofstream out(prefix);
    auto row = [&](const char *kind, std::uint64_t h, unsigned mode, Vector3 a, Vector3 b,
                   std::initializer_list<double> fields) {
        out << kind << " " << h << " " << (mode == 1 ? "true" : "false");
        auto scalar = [&](double x) {
            std::uint64_t raw;
            std::memcpy(&raw, &x, 8);
            out << " " << raw;
        };
        for (double x : {a.x, a.y, a.z, b.x, b.y, b.z})
            scalar(x);
        for (double x : fields)
            scalar(x);
        out << "\n";
    };
    for (auto &[h, l] : records.lines)
        row("LINE", h, l.properties.mode, l.start, l.end, {l.thickness, l.normal.x, l.normal.y, l.normal.z});
    for (auto &[h, c] : records.circles)
        row("CIRCLE", h, c.properties.mode, c.center, c.normal, {c.radius, c.thickness});
    for (auto &[h, c] : records.arcs)
        row("ARC", h, c.properties.mode, c.center, c.normal,
            {c.radius, c.thickness, c.start_angle, c.end_angle});
    out.close();
    return !out;
} catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
}
