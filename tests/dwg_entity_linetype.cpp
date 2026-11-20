// SPDX-License-Identifier: MPL-2.0
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

static void save(const DwgDocument &doc, const std::string &stem, int step) {
    std::ofstream out(stem + "." + std::to_string(step), std::ios::binary);
    doc.write(out);
    out.close();
    if (!out)
        throw std::runtime_error("Failed to save DWG");
}

static std::string upper_ascii(std::string name) {
    for (auto &c : name)
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
    return name;
}

int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto doc = DwgDocument::read(in);
    auto line = doc.objects().lines.begin()->first;
    auto circle = doc.objects().circles.begin()->first;
    auto arc = doc.objects().arcs.begin()->first;
    const std::string modes[] = {"测量-DASH", "byblock", "continuous", "ByLayer"};
    const std::string expected[] = {"测量-Dash", "ByBlock", "Continuous", ""};
    for (int i = 0; i < 4; ++i) {
        auto set = [&](auto read, std::uint64_t id, int index) {
            auto e = (doc.*read)(id);
            e.set_linetype(modes[index]);
            doc.update(e);
            auto got = (doc.*read)(id).linetype();
            // AC1009 and R13/R14 use the table entry's original name.
            if (upper_ascii(got) != upper_ascii(expected[index]))
                throw std::runtime_error("Incorrect decoded entity linetype");
            auto current = bytes(doc);
            doc.update(e);
            if (bytes(doc) != current)
                throw std::runtime_error("Repeated assignment changed DWG");
        };
        set(&DwgDocument::circle, circle, (i + 1) % 4);
        set(&DwgDocument::line, line, i);
        set(&DwgDocument::arc, arc, (i + 2) % 4);
        save(doc, argv[2], i + 1);
    }
    auto current = bytes(doc);
    auto changed = doc.circle(circle);
    changed.set_radius(23.5);
    changed.set_layer(doc.arc(arc).layer());
    changed.set_linetype("Unknown(123)");
    try {
        doc.update(changed);
        throw std::runtime_error("Accepted an unknown linetype");
    } catch (const std::invalid_argument &) {
    }
    if (bytes(doc) != current)
        throw std::runtime_error("Failed edit changed DWG");
    changed.set_linetype("Continuous");
    doc.update(changed);
    save(doc, argv[2], 5);
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
