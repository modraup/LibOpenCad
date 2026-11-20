// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <iostream>
#include <libopencad/dwg_document.hpp>
#include <libopencad/dwg_sections.hpp>
#include <sstream>
#include <set>
using namespace libopencad;
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static std::string bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto d = DwgDocument::read(in);
    auto source = bytes(d);
    auto original = d.objects();
    bool legacy = source.substr(0, 6) == "AC1009";
    bool paged = source.substr(0, 6) >= "AC1018";
    auto payload = paged ? read_dwg_paged_section(source, "AcDb:AcDbObjects") : source;
    std::vector<std::uint64_t> parents, children;
    for (auto &[h, r] : original.records) {
        if (legacy ? (r.type == 14 || r.type == 19)
                   : (r.type == 7 || r.type == 8 || r.type == 15 || r.type == 16 || r.type == 29 ||
                      r.type == 30))
            parents.push_back(h);
        if (legacy ? (r.type == 16 || r.type == 17 || r.type == 20)
                   : (r.type == 2 || r.type == 6 || (r.type >= 10 && r.type <= 14)))
            children.push_back(h);
    }
    check(parents.size() == 12 && !children.empty(), "Missing compound fixtures");
    parents.resize(6);
    std::uint64_t layout = 0;
    auto lineowner = original.lines.begin()->second.properties.owner;
    if (lineowner)
        layout = original.symbols.at(lineowner).layout;
    for (unsigned step = 0; step < 4; ++step) {
        const char *dest = step == 0 ? "DETAIL" : step == 3 ? "*Model_Space" : "*Paper_Space";
        for (auto h : parents) {
            if (step == 1 && layout)
                d.move_entity(h, layout);
            else
                d.move_entity(h, dest);
            auto once = bytes(d);
            if (step == 1 && layout)
                d.move_entity(h, layout);
            else
                d.move_entity(h, dest);
            check(bytes(d) == once, "Repeated group move changed bytes");
        }
        auto data = bytes(d);
        auto raw = paged ? read_dwg_paged_section(data, "AcDb:AcDbObjects") : data;
        const auto &now = d.objects();
        check(now.records.size() == original.records.size(), "Group move lost or duplicated records");
        for (auto h : children) {
            auto &a = original.records.at(h);
            auto &b = now.records.at(h);
            if (!legacy)
                check(payload.substr(a.body_offset, a.body_size) == raw.substr(b.body_offset, b.body_size),
                      "Modern child record changed");
            else if (a.type != 17) {
                auto start = a.geometry_start / 8, after = b.geometry_start / 8;
                check(payload.substr(a.body_offset + start, a.body_size - start) ==
                          raw.substr(b.body_offset + after, b.body_size - after),
                      "Legacy child payload changed");
            }
        }
        if (legacy)
            check(now.legacy_sequences == original.legacy_sequences, "Legacy sequence order changed");
        for (auto h : children) {
            bool refused = false;
            try {
                d.move_entity(h, "DETAIL");
            } catch (const std::exception &) {
                refused = true;
            }
            check(refused && bytes(d) == data, "Detached compound child");
        }
        std::ofstream out(std::string(argv[2]) + "." + std::to_string(step), std::ios::binary);
        d.write(out);
        out.close();
        check(bool(out), "Write failed");
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
