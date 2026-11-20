// SPDX-License-Identifier: MPL-2.0
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <libopencad/dwg_document.hpp>
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
    const bool legacy = bytes(doc).substr(0, 6) == "AC1009";
    bool boundary = std::string(argv[1]).find("boundary") != std::string::npos;
    std::uint64_t target = 0, paper = 0, neighbor = 0;
    for (const auto &[h, t] : doc.objects().texts) {
        if (t.value == "原始中文 A")
            target = h;
        if (t.value == "图纸中文 C")
            paper = h;
        if (t.value == "相邻中文 B")
            neighbor = h;
    }
    check(target && paper && neighbor, "TEXT fixture incomplete");
    const auto neighbor_source = doc.text(neighbor).definition();
    const auto original = bytes(doc);
    auto same = doc.text(target);
    auto definition = same.definition();
    definition.style = "nOtEs";
    same.set_definition(definition);
    doc.update(same);
    check(bytes(doc) == original, "Case-insensitive style no-op changed bytes");
    for (int step = 0; step < 10; ++step) {
        auto h = step == 7 ? paper : target;
        auto e = doc.text(h);
        auto d = e.definition();
        if (step == 0)
            d.height = 6.25;
        if (step == 1) {
            d.rotation = -.375;
            d.width_factor = 1.75;
        }
        if (step == 2) {
            d.horizontal_alignment = 5;
            d.vertical_alignment = 2;
            d.alignment_point = Point3{15, -9, 999};
            d.style = "中文样式";
        }
        if (step == 3) {
            d.height = 1;
            d.rotation = 0;
            d.width_factor = 1;
            d.horizontal_alignment = 0;
            d.vertical_alignment = 0;
            d.alignment_point.reset();
            d.style = "standard";
        }
        if (step == 4) {
            d.height = 2.75;
            d.rotation = std::acos(-1.) / 7.;
            d.width_factor = .625;
            d.horizontal_alignment = 3;
            d.vertical_alignment = 1;
            d.alignment_point = Point3{30, 40, 2};
        }
        if (step == 5) {
            d.style = "NOTES";
            d.horizontal_alignment = 4;
            d.vertical_alignment = 3;
            d.alignment_point = Point3{3, -4, 2};
        }
        if (step == 6) {
            d.text = "格式与中文 😀";
            d.insertion = {10, 20, 4};
            d.height = 9.5;
            d.rotation = .5;
            d.width_factor = 2;
            d.horizontal_alignment = 2;
            d.vertical_alignment = 0;
            d.alignment_point = Point3{-8, 12, 4};
            d.style = "中文样式";
        }
        if (step == 7) {
            d.height = 3;
            d.rotation = .125;
            d.width_factor = 1.5;
            d.horizontal_alignment = 1;
            d.vertical_alignment = 1;
            d.alignment_point = Point3{9, 11, 0};
            d.style = "NOTES";
        }
        if (step == 8)
            d.style = boundary ? "SLOT255" : "Standard";
        if (step == 9)
            d.style = boundary ? "SLOT256" : "NOTES";
        e.set_definition(d);
        const auto prior = bytes(doc);
        bool rejected = false;
        try {
            doc.update(e);
        } catch (const std::exception &) {
            rejected = true;
        }
        if (boundary && legacy && step == 9) {
            check(rejected && bytes(doc) == prior, "AC1009 slot256 must refuse atomically");
        } else {
            check(!rejected, "Valid TEXT format edit rejected");
            auto actual = doc.text(h).definition();
            check(actual.height == d.height && actual.width_factor == d.width_factor &&
                      std::abs(actual.rotation - d.rotation) < 1e-14 &&
                      actual.horizontal_alignment == d.horizontal_alignment &&
                      actual.vertical_alignment == d.vertical_alignment,
                  "TEXT format getter mismatch");
            auto a = actual.alignment_point.value_or(Point3{}), wanted = d.alignment_point.value_or(Point3{});
            check(a.x == wanted.x && a.y == wanted.y, "Alignment XY mismatch");
            auto saved = bytes(doc);
            doc.update(e);
            check(bytes(doc) == saved, "Reapplying format edit changed bytes");
            doc.update(doc.text(h));
            check(bytes(doc) == saved, "Format getter no-op changed bytes");
            if (boundary && legacy && step == 8)
                check(doc.objects().texts.at(h).style == 255, "Unsigned style slot truncated");
        }
        std::istringstream reload(bytes(doc));
        doc = DwgDocument::read(reload);
        check(doc.text(neighbor).text() == neighbor_source.text &&
                  doc.text(neighbor).insertion_point() == neighbor_source.insertion,
              "Neighbor changed");
        std::ofstream out(std::string(argv[2]) + "." + std::to_string(step), std::ios::binary);
        doc.write(out);
        out.close();
        check(bool(out), "Write failed");
    }
    const auto saved = bytes(doc);
    for (int mode = 0; mode < 6; ++mode) {
        bool rejected = false;
        try {
            auto e = doc.text(target);
            auto d = e.definition();
            if (mode == 0) {
                d.height = 77;
                d.style = "MISSING_STYLE";
            }
            if (mode == 1) {
                d.height = 77;
                d.text = std::string(1, char(255));
            }
            if (mode == 2)
                d.rotation = std::numeric_limits<double>::infinity();
            if (mode == 3)
                d.horizontal_alignment = 6;
            if (mode == 4)
                d.vertical_alignment = -1;
            if (mode == 5)
                e.set_layer("UnsupportedTextLayerEdit");
            e.set_definition(d);
            doc.update(e);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected && bytes(doc) == saved, "Invalid format edit was not atomic");
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
