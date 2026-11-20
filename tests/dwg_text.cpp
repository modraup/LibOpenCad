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
    auto original = bytes(doc);
    check(doc.objects().texts.size() == 3, "Expected three TEXT entities");
    std::uint64_t target = 0, adjacent = 0, paper = 0;
    for (const auto &[h, t] : doc.objects().texts) {
        if (t.value == "原始中文 A")
            target = h;
        if (t.value == "相邻中文 B")
            adjacent = h;
        if (t.value == "图纸中文 C")
            paper = h;
        auto e = doc.text(h);
        check(e.text() == t.value && e.insertion_point() == t.insertion, "TEXT getter disagrees");
        doc.update(e);
    }
    check(target && adjacent && paper, "Incorrect input decoding");
    check(bytes(doc) == original, "No-op TEXT update changed bytes");
    const auto d = doc.text(target).definition();
    check(d.insertion == Point3{3, -4, 2} && d.alignment_point == Point3{3, 8, 2}, "Input positions");
    check(d.style == "NOTES", "TEXT style reference");
    check(d.height == 2.5 && d.width_factor == .75 && d.thickness == 1.25 &&
              std::abs(d.rotation - .25) < 1e-14 && std::abs(d.oblique_angle + .125) < 1e-14 &&
              d.normal == Vector3::UNIT_Y && d.generation_flags == 2 && d.horizontal_alignment == 1 &&
              d.vertical_alignment == 3,
          "Input text properties");
    for (int step = 0; step < 6; ++step) {
        std::string value;
        if (!step)
            for (int i = 0; i < 400; ++i)
                value += "修改中文 Ω 😀 ";
        else if (step == 2)
            value = "短文字";
        else if (step >= 3)
            value = "最终中文 😀";
        Point3 point = step == 5   ? Point3{25, -32, 0}
                       : step == 0 ? Point3{-11, 12, -3}
                       : step == 1 ? Point3{7, 8, 0}
                                   : Point3{1, -2, 4};
        auto h = step == 4 ? paper : target;
        auto e = doc.text(h);
        e.set_text(value);
        e.set_insertion_point(point);
        doc.update(e);
        auto saved = bytes(doc);
        doc.update(e);
        check(bytes(doc) == saved, "Reapplying TEXT snapshot changed bytes");
        auto current = doc.text(h);
        check(current.text() == value && current.insertion_point() == point, "TEXT edit roundtrip");
        doc.update(current);
        check(bytes(doc) == saved, "Repeated TEXT update changed bytes");
        std::istringstream reload(saved);
        doc = DwgDocument::read(reload);
        check(doc.text(adjacent).text() == "相邻中文 B" &&
                  doc.text(adjacent).insertion_point() == Point3{5, 6, 0},
              "Adjacent TEXT changed");
        std::ofstream out(std::string(argv[2]) + "." + std::to_string(step), std::ios::binary);
        doc.write(out);
        out.close();
        check(bool(out), "Cannot save TEXT output");
    }
    auto saved = bytes(doc);
    for (int mode = 0; mode < 5; ++mode) {
        bool rejected = false;
        try {
            auto e = doc.text(target);
            if (mode == 0)
                e.set_layer("UnsupportedTextLayerEdit");
            if (mode == 1)
                e.set_text(std::string("bad\0text", 8));
            if (mode == 2)
                e.set_text(std::string(1, char(255)));
            if (mode == 3)
                e.set_insertion_point({0, 0, std::numeric_limits<double>::infinity()});
            if (mode == 4)
                e.set_text(std::string(70000, 'x'));
            doc.update(e);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected && bytes(doc) == saved, "Rejected TEXT edit was not atomic");
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
