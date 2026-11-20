// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
namespace {
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "cell style rejection must be atomic");
}
NamedTableCellStyle &named(CellStyleMapDefinition &v, std::string_view n) {
    for (auto &s : v.styles)
        if (s.name == n)
            return s;
    throw std::runtime_error("cell style not found");
}
TableContentDefinition content(const DxfDocument &d, std::string_view h) {
    TableContentDefinition result;
    d.for_each<DxfTableContent>([&](const auto &c) {
        if (c.handle() == h)
            result = c.definition();
    });
    return result;
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        const std::string prefix = argv[3];
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        const auto h = d.get_table_style("中文样式")->handle(),
                   neighbor = d.get_table_style("邻居样式")->handle();
        const auto th = d.table_style_template_handle(h);
        auto v = d.table_cell_styles(h);
        auto &c = named(v, "自定义");
        check(c.id == 101 && c.format.content.color == Color::from_true_color_value(0xabcdef) &&
                  c.format.background == Color::from_true_color_value(0x123456),
              "named RGB styles");
        check(c.format.margins[0] == 2.5 && c.format.margins[1] == 1.25 && c.format.borders[1].edge == 0,
              "margins and zero-edge slot");
        d.for_each<DxfCellStyleMap>([](auto &m) { m.set_definition(m.definition()); });
        write(d, prefix + "-roundtrip.dxf");
        auto before = records(save(d), binary);
        c.format.background = Color::from_true_color_value(0x654321);
        c.format.content.text_height = 7;
        c.format.borders[0].invisible = true;
        auto &data = named(v, "_DATA");
        data.format.content.text_height = 9;
        data.format.margins = {5, 4, 5, 4, .18, .18};
        data.format.borders[0].invisible = false;
        d.set_table_cell_styles(h, v);
        auto t = content(d, th);
        t.rows[0].cells[0].contents[0].value.text = "C++模板";
        d.set_table_style_template(h, t);
        check(d.table_style_template_handle(h) == th, "stable template handle");
        write(d, prefix + "-edited.dxf");
        auto after = records(save(d), binary);
        check(at(before, neighbor) == at(after, neighbor), "neighbor unchanged");
        fails(d, [&] {
            auto bad = v;
            bad.styles.push_back(bad.styles.back());
            d.set_table_cell_styles(h, bad);
        });
        fails(d, [&] {
            auto bad = v;
            bad.styles[0].format.content.text_height = std::numeric_limits<double>::infinity();
            d.set_table_cell_styles(h, bad);
        });
        fails(d, [&] {
            auto bad = v;
            bad.styles[0].format.content.text_style = 0xfffffff;
            d.set_table_cell_styles(h, bad);
        });
        fails(d, [&] { d.set_table_cell_styles("FFFFFFF", v); });
        auto scoped = v;
        named(scoped, "自定义").format.content.extra_fields = {{102, "{PRIVATE"}, {144, "1234"}, {102, "}"}};
        auto private_doc = d;
        private_doc.set_table_cell_styles(h, scoped);
        auto reloaded = read(save(private_doc));
        auto values = reloaded.table_cell_styles(h);
        check(named(values, "自定义").format.content.text_height == 7 &&
                  named(values, "自定义").format.content.extra_fields.size() == 3,
              "private colliding values retained");
        auto fresh = DxfDocument::create(binary);
        auto id = hex(fresh.add_table_style("新样式", {}));
        CellStyleMapDefinition custom;
        NamedTableCellStyle s;
        s.id = 101;
        s.style_type = 2;
        s.name = "新单元";
        s.data_flags = 1;
        s.format.type = 5;
        s.format.layout = 1;
        s.format.content.text_height = 6;
        s.format.background = Color::from_true_color_value(0x135790);
        custom.styles.push_back(s);
        fresh.set_table_cell_styles(id, custom);
        fresh.set_table_style_template(id, t);
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "fresh style graph");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
