// SPDX-License-Identifier: MPL-2.0
#include <algorithm>
#include <fstream>
#include <iostream>
#include <libopencad/dwg_document.hpp>
#include <sstream>
using namespace libopencad;
static std::string bytes(const DwgDocument &doc) {
    std::ostringstream out;
    doc.write(out);
    return out.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto doc = DwgDocument::read(in);
    auto circle = doc.objects().circles.begin()->first, arc = doc.objects().arcs.begin()->first,
         line = doc.objects().lines.begin()->first;
    auto saved = bytes(doc);
    doc.move_entity(circle, "*Model_Space");
    if (bytes(doc) != saved)
        return 1;
    try {
        doc.move_entity(circle, "absent");
        return 1;
    } catch (const std::invalid_argument &) {
    }
    if (bytes(doc) != saved)
        return 1;
    auto owner = doc.objects().lines.at(line).properties.owner;
    auto layout = owner ? doc.objects().symbols.at(owner).layout : 0;
    auto save = [&](const std::string &path) {
        std::ofstream out(path, std::ios::binary);
        doc.write(out);
        out.close();
        if (!out)
            throw std::runtime_error("DWG write failed");
    };
    doc.move_entity(circle, "detail");
    save(std::string(argv[2]) + ".1");
    if (layout)
        doc.move_entity(arc, layout);
    else
        doc.move_entity(arc, "*Paper_Space");
    save(std::string(argv[2]) + ".2");
    doc.move_entity(line, "*Model_Space");
    const auto &o = doc.objects();
    if (o.model_space) {
        std::uint64_t detail = 0;
        for (auto &[id, s] : o.symbols)
            if (s.name == "DETAIL")
                detail = id;
        auto sheet = owner ? owner : o.paper_space;
        auto check = [&](std::uint64_t id, std::uint64_t member) {
            auto &b = o.symbols.at(id);
            if (saved.substr(0, 6) <= "AC1015") {
                if (b.last != member || !b.first)
                    throw std::runtime_error("Incorrect legacy endpoints");
            } else if (b.members.empty() || b.members.back() != member ||
                       std::count(b.members.begin(), b.members.end(), member) != 1)
                throw std::runtime_error("Incorrect block member list");
        };
        check(detail, circle);
        check(o.model_space, line);
        check(sheet, arc);
        if (saved.substr(0, 6) <= "AC1015" &&
            (o.circles.at(circle).properties.previous || o.circles.at(circle).properties.next ||
             o.lines.at(line).properties.next || !o.lines.at(line).properties.previous ||
             o.arcs.at(arc).properties.next))
            throw std::runtime_error("Incorrect legacy entity links");
    }
    std::ofstream out(argv[2], std::ios::binary);
    doc.write(out);
    out.close();
    return !out;
} catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
}
