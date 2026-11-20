// SPDX-License-Identifier: MPL-2.0
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
static std::string bytes(const DwgDocument &d) {
    std::ostringstream out;
    d.write(out);
    return out.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto doc = DwgDocument::read(in);
    const auto version = bytes(doc).substr(0, 6);
    std::uint64_t target = 0, paper = 0, neighbor = 0;
    for (const auto &[h, t] : doc.objects().texts) {
        if (t.value == "原始中文 A")
            target = h;
        if (t.value == "图纸中文 C")
            paper = h;
        if (t.value == "相邻中文 B")
            neighbor = h;
        auto e = doc.text(h);
        check(e.color() == t.properties.color && e.linetype_scale() == t.properties.linetype_scale &&
                  e.transparency() == Transparency::from_alpha_value(t.properties.transparency) &&
                  e.is_invisible() == t.properties.invisible,
              "Source common getter mismatch");
    }
    check(target && paper && neighbor, "Incomplete TEXT fixture");
    auto saved = bytes(doc);
    auto alias = doc.text(target);
    alias.set_layer("details");
    doc.update(alias);
    check(bytes(doc) == saved, "Layer case alias changed bytes");
    for (int which = 0; which < 4; ++which) {
        auto e = doc.text(target);
        auto before = bytes(doc);
        bool unsupported = false;
        if (which == 0) {
            e.set_invisible(true);
            unsupported = version < "AC1012";
        }
        if (which == 1) {
            e.set_line_weight(LineWeight::from_value(53));
            unsupported = version < "AC1015";
        }
        if (which == 2) {
            e.set_linetype_scale(5.);
            unsupported = version < "AC1012";
        }
        if (which == 3) {
            e.set_transparency(Transparency::explicit_alpha(128));
            unsupported = version < "AC1018";
        }
        if (unsupported) {
            doc.update(e);
            check(bytes(doc) == before, "Unsupported common field changed bytes");
        }
    }
    const auto adjacent_book = doc.objects().texts.at(neighbor).properties.color_book;
    for (int step = 0; step < 10; ++step) {
        auto h = step == 8 ? paper : target;
        auto e = doc.text(h);
        auto previous = e;
        const auto book = doc.objects().texts.at(h).properties.color_book;
        switch (step) {
        case 0:
            e.set_transparency(Transparency::by_block());
            break;
        case 1:
            e.set_layer("测量");
            e.set_linetype("中文线型");
            e.set_invisible(true);
            e.set_line_weight(LineWeight::from_value(53));
            e.set_linetype_scale(2.25);
            break;
        case 2:
            e.set_color(e.color());
            break;
        case 3:
            e.set_color(Color::by_block());
            e.set_linetype("byblock");
            e.set_transparency(Transparency::explicit_alpha(0));
            break;
        case 4:
            e.set_color(Color::from_index(17));
            e.set_linetype("continuous");
            e.set_transparency(Transparency::explicit_alpha(255));
            e.set_line_weight(LineWeight::by_block());
            e.set_linetype_scale(0);
            break;
        case 5:
            e.set_color(Color::from_rgb(12, 34, 56));
            e.set_linetype("bylayer");
            e.set_transparency(Transparency::explicit_alpha(71));
            e.set_line_weight(LineWeight::default_weight());
            e.set_linetype_scale(-.375);
            break;
        case 6:
            e.set_color(Color::from_index(255));
            e.set_linetype("中文线型");
            e.set_layer("0");
            e.set_invisible(false);
            e.set_line_weight(LineWeight::from_value(211));
            break;
        case 7:
            e.set_color(Color::by_layer());
            e.set_linetype("");
            e.set_transparency(Transparency::by_layer());
            e.set_line_weight(LineWeight::by_layer());
            e.set_linetype_scale(1);
            break;
        case 8:
        case 9: {
            e.set_layer(step == 8 ? "测量" : "details");
            e.set_linetype(step == 8 ? "中文线型" : "ByBlock");
            e.set_color(step == 8 ? Color::from_rgb(90, 120, 150) : Color::from_index(2));
            e.set_invisible(true);
            e.set_line_weight(LineWeight::from_value(step == 8 ? 25 : 50));
            e.set_linetype_scale(step == 8 ? 3.5 : .625);
            e.set_transparency(step == 8 ? Transparency::explicit_alpha(128) : Transparency::by_block());
            auto d = e.definition();
            d.text = step == 8 ? "图纸属性 中文 😀" : "组合属性 中文 😀";
            d.insertion = step == 8 ? Point3{11, 22, 3} : Point3{3, 4, 5};
            d.height = 9;
            d.thickness = -.75;
            d.normal = Vector3::UNIT_X;
            e.set_definition(d);
            break;
        }
        }
        doc.update(e);
        auto result = doc.text(h);
        check(result.color() == e.color(), "Saved color mismatch");
        check(result.is_invisible() == (version >= "AC1012" ? e.is_invisible() : previous.is_invisible()),
              "Saved visibility mismatch");
        check(result.line_weight() == (version >= "AC1015" ? e.line_weight() : previous.line_weight()),
              "Saved lineweight mismatch");
        check(result.linetype_scale() ==
                  (version >= "AC1012" ? e.linetype_scale() : previous.linetype_scale()),
              "Saved scale mismatch");
        check(result.transparency() == (version >= "AC1018" ? e.transparency() : previous.transparency()),
              "Saved transparency mismatch");
        if (step < 2)
            check(doc.objects().texts.at(h).properties.color_book == book, "Non-color edit detached book");
        if (step == 2)
            check(doc.objects().texts.at(h).properties.color_book == 0,
                  "Setting fallback color retained book");
        check(doc.objects().texts.at(neighbor).properties.color_book == adjacent_book,
              "Neighbor color-book reference changed");
        saved = bytes(doc);
        doc.update(e);
        check(bytes(doc) == saved, "Repeated common update changed bytes");
        doc.update(doc.text(h));
        check(bytes(doc) == saved, "Common getter no-op changed bytes");
        if (version == "AC1009" && step == 6) {
            auto r = doc.objects().records.at(h);
            check(static_cast<unsigned char>(saved[r.offset + r.color_bit / 8]) == 255,
                  "Unsigned ACI 255 lost");
        }
        std::istringstream reload(saved);
        doc = DwgDocument::read(reload);
        check(doc.text(neighbor).text() == "相邻中文 B", "Neighbor text changed");
        std::ofstream out(std::string(argv[2]) + "." + std::to_string(step), std::ios::binary);
        doc.write(out);
        out.close();
        check(bool(out), "Write failed");
    }
    saved = bytes(doc);
    for (int mode = 0; mode < 4; ++mode) {
        bool rejected = false;
        try {
            auto e = doc.text(target);
            e.set_height(13);
            e.set_color(Color::from_index(7));
            e.set_transparency(Transparency::explicit_alpha(201));
            if (mode == 0)
                e.set_layer("MISSING_LAYER");
            if (mode == 1) {
                e.set_layer("测量");
                e.set_linetype("MISSING_LINETYPE");
            }
            if (mode == 2)
                e.set_text(std::string(1, char(255)));
            if (mode == 3)
                e.set_linetype_scale(std::numeric_limits<double>::infinity());
            doc.update(e);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected && bytes(doc) == saved, "Combined common update not atomic");
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
