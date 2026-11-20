// SPDX-License-Identifier: MPL-2.0
#include <cmath>
#include <fstream>
#include <iostream>
#include <libopencad/dwg_document.hpp>
#include <limits>
#include <sstream>
using namespace libopencad;
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
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
    bool legacy = bytes(doc).substr(0, 6) == "AC1009";
    std::uint64_t target = 0, paper = 0, neighbor = 0;
    for (const auto &[h, t] : doc.objects().texts) {
        if (t.value == "原始中文 A")
            target = h;
        if (t.value == "图纸中文 C")
            paper = h;
        if (t.value == "相邻中文 B")
            neighbor = h;
    }
    check(target && paper && neighbor, "Incomplete TEXT geometry fixture");
    auto original = bytes(doc);
    auto same = doc.text(target);
    same.set_definition(same.definition());
    doc.update(same);
    check(bytes(doc) == original, "Definition projection changed original TEXT");
    const auto adjacent = doc.text(neighbor).definition();
    for (int step = 0; step < 12; ++step) {
        auto h = (step == 8 || step == 9) ? paper : target;
        auto e = doc.text(h);
        auto d = e.definition();
        switch (step) {
        case 0:
            d.oblique_angle = .375;
            d.generation_flags = 4;
            d.thickness = -2.25;
            d.normal = {1, 2, 3};
            break;
        case 1:
        case 7:
        case 9:
            d.oblique_angle = 0;
            d.generation_flags = 0;
            d.thickness = 0;
            d.normal = {0, 0, 1};
            break;
        case 2:
            d.oblique_angle = -std::acos(-1.) / 9.;
            d.generation_flags = 2;
            d.thickness = .75;
            d.normal = {1, 0, 0};
            break;
        case 3:
            d.oblique_angle = .148847420517342;
            d.generation_flags = 6;
            d.thickness = 0;
            d.normal = {0, 0, -1};
            break;
        case 4:
            d.oblique_angle = 0;
            d.generation_flags = 255;
            d.thickness = -1.5;
            d.normal = {0, 0, 0};
            break;
        case 5:
            d.oblique_angle = -.25;
            d.generation_flags = 262;
            d.thickness = 1.25;
            d.normal = {.25, -.5, 2};
            break;
        case 6:
            d.generation_flags = -1;
            break;
        case 8:
            d.oblique_angle = -.125;
            d.generation_flags = 6;
            d.thickness = 2.5;
            d.normal = {-2, 3, 4};
            break;
        case 10:
            d.oblique_angle = .148847420517342;
            d.generation_flags = 6;
            d.thickness = -.625;
            d.normal = {1, -2, 3};
            d.text = "倾斜镜像 中文 😀";
            d.insertion = {10, -12, 5};
            d.alignment_point = Point3{-2, 9, 5};
            d.style = "中文样式";
            d.height = 6;
            d.rotation = -.5;
            d.width_factor = .75;
            d.horizontal_alignment = 2;
            d.vertical_alignment = 1;
            break;
        case 11:
            d.height = 8;
            d.width_factor = 1.25;
            break;
        }
        const auto prior_oblique = doc.objects().texts.at(h).oblique_angle;
        e.set_definition(d);
        doc.update(e);
        auto actual = doc.text(h).definition();
        check(std::abs(actual.oblique_angle - d.oblique_angle) < 1e-14 && actual.thickness == d.thickness &&
                  actual.normal == d.normal,
              "TEXT geometry mismatch");
        check(actual.generation_flags ==
                  (legacy ? static_cast<std::uint8_t>(d.generation_flags) : d.generation_flags),
              "TEXT generation flags mismatch");
        if (step == 11)
            check(doc.objects().texts.at(h).oblique_angle == prior_oblique,
                  "Formatting changed stored oblique angle");
        auto saved = bytes(doc);
        doc.update(e);
        check(bytes(doc) == saved, "Reapplying geometry changed bytes");
        auto current = doc.text(h);
        current.set_definition(current.definition());
        doc.update(current);
        check(bytes(doc) == saved, "Getter/setter changed TEXT geometry");
        std::istringstream reload(saved);
        doc = DwgDocument::read(reload);
        auto n = doc.text(neighbor).definition();
        check(n.text == adjacent.text && n.insertion == adjacent.insertion && n.normal == adjacent.normal &&
                  n.thickness == adjacent.thickness,
              "Neighbor TEXT changed");
        std::ofstream out(std::string(argv[2]) + "." + std::to_string(step), std::ios::binary);
        doc.write(out);
        out.close();
        check(bool(out), "Save failed");
    }
    const auto saved = bytes(doc);
    for (int mode = 0; mode < 7; ++mode) {
        bool rejected = false;
        try {
            auto e = doc.text(target);
            auto d = e.definition();
            d.thickness = 7;
            if (mode == 0)
                d.oblique_angle = std::numeric_limits<double>::quiet_NaN();
            if (mode == 1)
                d.thickness = std::numeric_limits<double>::infinity();
            if (mode == 2)
                d.normal.x = std::numeric_limits<double>::quiet_NaN();
            if (mode == 3)
                d.normal.z = std::numeric_limits<double>::infinity();
            if (mode == 4)
                d.style = "MISSING_STYLE";
            if (mode == 5)
                d.text = std::string(1, char(255));
            if (mode == 6)
                e.set_layer("UnsupportedTextLayerEdit");
            e.set_definition(d);
            doc.update(e);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected && bytes(doc) == saved, "Invalid TEXT geometry edit was not atomic");
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
