// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <limits>
namespace {
TextStyleDefinition definition() {
    TextStyleDefinition v;
    v.flags = 4;
    v.generation_flags = 6;
    v.height = 3;
    v.width_factor = .75;
    v.oblique_angle = .25;
    v.last_height = 4;
    v.font_file = "simplex.shx";
    v.big_font_file = "中文.shx";
    v.true_type_font = " Noto Sans CJK SC ";
    v.true_type_font_flags = 0x1000022;
    v.annotative = true;
    return v;
}
void verify(const DxfDocument &d) {
    for (auto name : {"中文样式", "Neighbor"}) {
        auto v = d.get_text_style(name);
        check(v && v->flags == 4 && v->generation_flags == 6 && v->height == 3 && v->width_factor == .75 &&
                  std::abs(v->oblique_angle - .25) < 1e-12 && v->last_height == 4 &&
                  v->big_font_file == "中文.shx" && v->true_type_font == "Noto Sans CJK SC" &&
                  v->true_type_font_flags == 0x1000022 && v->annotative,
              "text style fields");
    }
    check(!d.get_text_style("missing"), "unknown text style");
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "style failure rollback");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        for (auto name : {"中文样式", "Neighbor"})
            made.add_text_style(name, definition());
        verify(made);
        write(made, prefix + "-created.dxf");
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        verify(d);
        auto before = records(save(d), binary);
        auto h = d.get_symbol(SymbolTable::TextStyle, "中文样式")->handle,
             neighbor = d.get_symbol(SymbolTable::TextStyle, "Neighbor")->handle;
        auto value = *d.get_text_style("中文样式");
        value.true_type_font = "";
        value.true_type_font_flags = 34;
        value.annotative = false;
        value.last_height = 0;
        value.oblique_angle = -.5;
        value.height = 5;
        d.set_text_style("中文样式", value);
        check(d.get_text_style("中文样式")->last_height == 2.5 && !d.get_text_style("中文样式")->annotative &&
                  d.get_text_style("中文样式")->true_type_font.empty(),
              "clear font and annotation");
        auto after = records(save(d), binary);
        check(at(before, neighbor) == at(after, neighbor), "neighbor style unchanged");
        auto data = d.get_record(h)->extended_data();
        auto acad =
            std::find_if(data.begin(), data.end(), [](auto &r) { return r.application_name == "ACAD"; });
        check(acad != data.end() && acad->values.size() == 4 &&
                  std::get<double>(acad->values[2].value) == 9.5 &&
                  std::get<std::string>(acad->values[3].value) == "unmodeled",
              "retain app-owned values");
        write(d, prefix + "-edited.dxf");
        fails(d, [&] {
            auto bad = value;
            bad.width_factor = std::numeric_limits<double>::quiet_NaN();
            d.set_text_style("中文样式", bad);
        });
        fails(d, [&] {
            auto bad = value;
            bad.flags |= 1;
            d.set_text_style("中文样式", bad);
        });
        fails(d, [&] { d.add_text_style("NEIGHBOR", value); });
        // A private group with an annotative lookalike must not change modeled fields.
        auto &raw = at(after, h);
        raw.insert(raw.begin() + 2, {{102, "{PRIVATE"}, {1001, "ACAD"}, {1000, "hidden font"}, {102, "}"}});
        auto protected_doc = read(encode(after, binary));
        check(protected_doc.get_text_style("中文样式")->true_type_font.empty(), "private XDATA excluded");
        protected_doc.set_text_style("中文样式", value);
        auto protected_raw = records(save(protected_doc), binary);
        check(at(protected_raw, h)[4] == raw[4], "private app text retained");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
