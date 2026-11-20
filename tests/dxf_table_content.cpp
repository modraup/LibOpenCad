// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
namespace {
std::string object(const DxfDocument &d, std::string_view name) {
    std::string result;
    d.for_each<DxfTableContent>([&](const auto &o) {
        if (o.definition().name == name)
            result = o.handle();
    });
    check(!result.empty(), "missing modern table object");
    return result;
}
TableContentDefinition definition(const DxfDocument &d, std::string_view id) {
    TableContentDefinition result;
    d.for_each<DxfTableContent>([&](const auto &o) {
        if (o.handle() == id)
            result = o.definition();
    });
    return result;
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught && save(d) == before, "modern table failure must be atomic");
}
void text(TableValue &value, const std::string &s) {
    value = {};
    value.type = 4;
    value.flags = 6;
    value.text = value.formatted = s;
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        const bool binary = std::string(argv[1]) == "binary";
        const std::string prefix = argv[3];
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        const auto standalone = object(d, "独立内容"), pattern = object(d, "模板");
        const auto root = d.get_record(standalone)->owner_handle();
        auto value = definition(d, standalone);
        check(value.rows.size() == 2 && value.columns.size() == 2, "modern dimensions");
        check(value.rows[1].cells[1].contents.size() == 2 &&
                  value.rows[1].cells[1].contents[1].value.text == "第二内容",
              "multiple contents");
        check(value.rows[0].cells[0].custom[1].value.binary.size() == 256 &&
                  value.rows[1].cells[0].contents[1].type == 4,
              "binary chunks and block content");
        check(value.rows[0].cells[0].custom[0].value.point.z == 6 &&
                  value.columns[0].custom[0].value.text == "列值",
              "custom data scopes");
        check(value.format->borders.size() == 2 && value.format->borders[1].edge == 32 &&
                  value.format->margins[1] == 1,
              "inside border and margins");
        d.for_each<DxfTableContent>([&](auto &o) { o.set_definition(o.definition()); });
        write(d, prefix + "-roundtrip.dxf");
        d.for_each<DxfTableContent>([&](auto &o) {
            if (o.handle() != pattern)
                return;
            auto v = o.definition();
            text(v.rows[1].cells[1].contents[1].value, "修改第二项");
            v.rows[1].cells[1].format->margins[1] = 9;
            v.columns[0].width = 33;
            v.format->content.color = Color::from_index(6);
            o.set_definition(v);
        });
        write(d, prefix + "-edited.dxf");
        value.name = "C++内容";
        d.add_table_content(root, "C++内容", value);
        value.name = "C++模板";
        d.add_table_content(root, "C++模板", value, true);
        write(d, prefix + "-created.dxf");
        const auto model = d.model_space_entity_handles().front(),
                   paper = d.paper_space_entity_handles().front();
        const auto old_content = d.table_content_handle(model), paper_content = d.table_content_handle(paper);
        auto before = records(save(d), binary);
        auto v = definition(d, old_content);
        v.rows[1].height = 24;
        v.columns[0].width = 31;
        text(v.rows[1].cells[0].contents[0].value, "同步文字");
        v.rows[1].cells[0].contents[0].format.value_format = "%tc2";
        v.rows[1].cells[0].format->content.value_format = "%lu2%pr5";
        text(v.rows[1].cells[1].contents[1].value, "额外内容");
        d.set_table_content(model, v);
        check(d.table_content_handle(model) == old_content, "stable content handle");
        auto after = records(save(d), binary);
        check(at(before, paper) == at(after, paper) && at(before, paper_content) == at(after, paper_content),
              "neighbor content preserved");
        write(d, prefix + "-synced.dxf");
        d.for_each<DxfTable>([&](auto &o) {
            if (o.handle() != model)
                return;
            auto v = o.definition();
            v.row_heights[1] = 28;
            text(*v.cells[1][0].value, "旧接口修改");
            o.set_definition(v);
        });
        d.synchronize_table_content(model);
        write(d, prefix + "-refreshed.dxf");
        const auto copy = hex(d.copy_entity(model, EntitySpace::Paper));
        check(d.table_content_handle(copy) != old_content, "modern copy fresh identity");
        write(d, prefix + "-copied.dxf");
        auto copy_value = d.table_content_definition(copy);
        check(copy_value.rows.size() == 2 && copy_value.rows[1].cells[1].contents.size() == 2,
              "copy remains typed before reload");
        text(copy_value.rows[1].cells[1].contents[1].value, "独立副本");
        d.set_table_content(copy, copy_value);
        check(definition(d, old_content).rows[1].cells[1].contents[1].value.text == "额外内容",
              "copy editing preserves source");
        write(d, prefix + "-copy-edited.dxf");
        check(d.remove_entity(copy), "delete modern copy");
        write(d, prefix + "-deleted.dxf");
        fails(d, [&] { d.add_table_content(root, "C++模板", value, true); });
        fails(d, [&] {
            auto bad = v;
            bad.rows[0].cells.pop_back();
            d.set_table_content(model, bad);
        });
        fails(d, [&] {
            auto bad = v;
            bad.rows[1].cells[0].format->margins[0] = std::numeric_limits<double>::infinity();
            d.set_table_content(model, bad);
        });
        fails(d, [&] {
            auto bad = v;
            bad.rows[1].cells[0].contents[0].format.text_style = 0xfffffff;
            d.set_table_content(model, bad);
        });
        auto rgb = d;
        const auto rgb_color = Color::from_rgb(18, 52, 86);
        rgb.for_each<DxfTableContent>([&](auto &o) {
            if (o.handle() != pattern)
                return;
            auto v = o.definition();
            v.format->background = rgb_color;
            v.format->content.color = rgb_color;
            v.format->borders[0].color = rgb_color;
            o.set_definition(v);
        });
        write(rgb, prefix + "-rgb.dxf");
        auto quantized = definition(read(save(rgb)), pattern);
        check(quantized.format->background == Color::from_index(rgb_color.approximate_index()),
              "modern RGB follows upstream ACI approximation");
        // Private groups with colliding delimiters must not change scope or create contents.
        auto scoped = v;
        scoped.rows[0].cells[0].custom_extra = {{102, "{PRIVATE"}, {301, "DATAMAP_VALUE"}, {102, "}"}};
        scoped.rows[0].cells[0].contents[0].format.extra_fields = {
            {102, "{PRIVATE"}, {309, "CONTENTFORMAT_END"}, {90, "999"}, {102, "}"}, {290, "1"}};
        d.set_table_content(model, scoped);
        auto reopened = read(save(d));
        auto retained = definition(reopened, reopened.table_content_handle(model));
        check(retained.rows[0].cells[0].custom_extra.size() == 3 &&
                  retained.rows[0].cells[0].contents[0].format.extra_fields.size() == 5,
              "private content format retained");
        auto fresh = DxfDocument::create(binary);
        TableDefinition legacy;
        legacy.row_heights = {10};
        legacy.column_widths = {20};
        legacy.cells = {{TableCell{}}};
        TableValue cell;
        text(cell, "新表格");
        legacy.cells[0][0].value = cell;
        const auto id = hex(fresh.add_table(legacy));
        fresh.synchronize_table_content(id);
        write(fresh, prefix + "-new-link.dxf");
        check(!fresh.table_content_handle(id).empty() && fresh.reference_issues().empty(),
              "new roundtrip ownership graph");
        // A row, column and the table supply different edge properties to a legacy cell.
        auto inherited = fresh.table_content_definition(id);
        inherited.rows.resize(2, inherited.rows.front());
        inherited.columns.resize(2, inherited.columns.front());
        for (auto &row : inherited.rows) row.cells.resize(2, row.cells.front());
        TableGridFormat edge;
        edge.edge = 2;
        edge.overrides = 16;
        edge.invisible = true;
        inherited.format.emplace().borders = {edge};
        edge = {};
        edge.edge = 4;
        edge.overrides = 2;
        edge.line_weight = 35;
        inherited.rows[0].format.emplace().borders = {edge};
        edge = {};
        edge.edge = 32;
        edge.overrides = 8;
        edge.color = Color::from_index(4);
        inherited.columns[1].format.emplace().borders = {edge};
        fresh.set_table_content(id, inherited);
        auto result = fresh.export_version(DxfVersion::AC1021, binary, {true});
        check(result.report.can_write, "legacy table style projection");
        auto projected = read(result.bytes);
        projected.for_each<DxfTable>([](const auto &t) {
            const auto cells = t.definition().cells;
            check(cells[0][0].style->borders[2].invisible &&
                  cells[0][0].style->borders[2].line_weight == 35 &&
                  cells[1][1].style->borders[0].invisible &&
                  cells[1][1].style->borders[1].color == Color::from_index(4),
                  "legacy edge inheritance");
        });
        write(projected, prefix + "-inherited.dxf");
        // Refuse wrong ownership without changing either representation or allocator.
        auto damaged = records(save(d), binary);
        auto &content_record = at(damaged, old_content);
        for (auto &[code, s] : content_record) {
            if (code == 100)
                break;
            if (code == 330)
                s = root;
        }
        auto bad = read(encode(damaged, binary));
        fails(bad, [&] { bad.synchronize_table_content(model); });
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
