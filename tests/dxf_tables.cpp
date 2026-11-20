// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
namespace {
TableStyleDefinition style() {
    TableStyleDefinition s;
    s.description = "明细样式";
    s.version = 1;
    s.horizontal_margin = .5;
    s.vertical_margin = .75;
    s.header_suppressed = true;
    s.data.text_height = 3;
    s.data.text_color = Color::from_rgb(0x12, 0x34, 0x56);
    s.data.fill_enabled = true;
    s.data.fill_color = Color::from_rgb(0xab, 0xcd, 0xef);
    s.data.borders[0].color = Color::from_rgb(0x11, 0x22, 0x33);
    s.data.borders[0].line_weight = 35;
    s.title.text_height = 5;
    s.header.text_style.clear();
    s.header.text_height = 4;
    s.header.borders[5].invisible = true;
    return s;
}
TableDefinition definition(std::uint64_t style) {
    TableDefinition t;
    t.insertion = {1, 2, 3};
    t.style = style;
    t.horizontal_direction = {0, 1, 0};
    t.data_version = 2;
    t.value_flags = 7;
    t.row_heights = {10, 11, 12};
    t.column_widths = {20, 21, 22};
    t.cells.resize(3, std::vector<TableCell>(3));
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            TableValue v;
            v.type = 4;
            v.text = "项目" + std::to_string(r) + std::to_string(c);
            v.flags = 6;
            v.formatted = v.text;
            t.cells[r][c].value = v;
        }
    auto &a = *t.cells[1][0].value;
    a.type = 2;
    a.text.clear();
    a.number = 12.5;
    a.flags = 0;
    a.formatted = "12.5";
    auto &b = *t.cells[1][1].value;
    b.type = 1;
    b.text.clear();
    b.number = -12;
    b.flags = 0;
    b.formatted = "-12";
    auto &c = *t.cells[1][2].value;
    c.type = 8;
    c.text.clear();
    c.binary = {0, 1, 127, 128, 255};
    t.cells[2][2].rotation = .5;
    t.cells[2][2].state = 7;
    TableCellStyle s;
    s.overrides = 0x3ffff;
    s.property_flags = 16 | 32 | 64 | 128 | 512;
    s.alignment = 6;
    s.fill_enabled = true;
    s.background_color = Color::from_rgb(0x44, 0x55, 0x66);
    s.content_color = Color::from_index(3);
    s.text_style = "Standard";
    s.text_height = 6;
    for (auto &border : s.borders)
        border.line_weight = -1;
    s.borders[0].invisible = true;
    s.borders[1].color = Color::from_rgb(0x77, 0x88, 0x99);
    t.cells[2][2].style = s;
    t.merged_ranges = {{0, 0, 0, 1}};
    t.overrides.flags = 1 | 8 | 32 | 0x20000;
    t.overrides.title_suppressed = true;
    t.overrides.horizontal_margin = 1.25;
    t.overrides.row_colors = {Color::from_index(5)};
    t.overrides.text_styles = {"Standard"};
    return t;
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "table rejected operation must be atomic");
}
void verify(const DxfDocument &d) {
    int count = 0;
    d.for_each<DxfTable>([&](const auto &e) {
        auto t = e.definition();
        check(t.cells.size() == 3 && t.column_widths.size() == 3 && t.cells[1].size() == 3,
              "table row/column distribution");
        check(t.cells[1][0].value->number == 12.5 && t.cells[1][1].value->number == -12 &&
                  t.cells[1][2].value->binary == std::vector<std::uint8_t>({0, 1, 127, 128, 255}),
              "typed cell values");
        check(t.merged_ranges.size() == 1 && t.merged_ranges[0].right == 1 && t.cells[0][1].merged == 1,
              "merged cells");
        check(t.data_version == 2 && t.overrides.title_suppressed && t.overrides.horizontal_margin == 1.25,
              "duplicate 280 scopes");
        check(t.cells[2][2].style->background_color == Color::from_rgb(0x44, 0x55, 0x66), "cell RGB");
        ++count;
    });
    check(count == 2, "two tables");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        auto s = made.add_table_style("中文表格", style());
        for (auto space : {EntitySpace::Model, EntitySpace::Paper}) {
            EntityOptions o;
            o.space = space;
            made.add_table(definition(s), o);
        }
        verify(made);
        write(made, prefix + "-created.dxf");
        check(made.get_table_style("STANDARD") && made.get_table_style("中文表格") &&
                  !made.get_table_style("missing"),
              "dictionary style name lookup");
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        verify(d);
        const auto source = d.model_space_entity_handles().front(),
                   neighbor = d.paper_space_entity_handles().front();
        auto original = records(save(d), binary);
        d.for_each<DxfTable>([&](auto &e) {
            if (e.handle() != source)
                return;
            auto t = e.definition();
            t.cells[1][0].value->number = 88.25;
            TableValue text;
            text.type = 4;
            text.text = "已修改";
            text.formatted = text.text;
            text.flags = 6;
            t.cells[1][1].value = text;
            t.cells[2][2].style->background_color = Color::by_block();
            t.row_heights[1] = 18;
            e.set_definition(t);
        });
        write(d, prefix + "-edited.dxf");
        auto edited = records(save(d), binary);
        check(at(original, neighbor) == at(edited, neighbor), "neighbor table unchanged");
        const auto custom = d.get_table_style("中文表格")->handle();
        d.for_each<DxfTableStyle>([&](auto &s) {
            if (s.handle() != custom)
                return;
            auto v = s.definition();
            v.data.text_height = 8;
            v.data.text_color = Color::by_layer();
            s.set_definition(v);
        });
        write(d, prefix + "-styled.dxf");
        // The shared owner-chain traversal preserves and remaps the raw modern objects.
        const auto rawcopy = hex(d.copy_entity(source, EntitySpace::Paper));
        write(d, prefix + "-raw-copied.dxf");
        check(d.remove_entity(rawcopy), "remove table with modern owned objects");
        write(d, prefix + "-raw-deleted.dxf");
        const auto first = made.model_space_entity_handles().front();
        auto dictionary = made.add_extension_dictionary(first);
        made.add_xrecord(hex(dictionary), "Data", {{340, first}});
        write(made, prefix + "-before.dxf");
        auto copy = hex(made.copy_entity(first, EntitySpace::Paper));
        write(made, prefix + "-copied.dxf");
        const auto before_copy = records(save(made), binary);
        made.for_each<DxfTable>([&](auto &e) {
            if (e.handle() == copy) {
                auto v = e.definition();
                v.row_heights[1] = 99;
                e.set_definition(v);
            }
        });
        write(made, prefix + "-copy-edited.dxf");
        auto changed = records(save(made), binary);
        auto stable = before_copy;
        check(at(stable, first) == at(changed, first), "source independent of table copy");
        check(made.remove_entity(copy) && made.remove_entity(first), "table copy/source removal");
        check(made.reference_issues().empty(), "table ownership graph");
        write(made, prefix + "-deleted.dxf");
        fails(made, [&] { made.add_table_style("中文表格", style()); });
        fails(made, [&] {
            auto v = definition(s);
            v.cells[0].pop_back();
            made.add_table(v);
        });
        fails(made, [&] {
            auto v = definition(s);
            v.merged_ranges[0].bottom = 99;
            made.add_table(v);
        });
        fails(made, [&] {
            auto v = definition(s);
            v.row_heights[0] = std::numeric_limits<double>::infinity();
            made.add_table(v);
        });
        // Named graphics blocks and text styles rename only their reference fields.
        made.add_symbol(SymbolTable::BlockRecord, "Graphics");
        auto v = definition(s);
        v.block_name = "Graphics";
        v.cells[0][0].value->text = "Graphics";
        const auto id = made.add_table(v);
        made.rename_symbol(SymbolTable::BlockRecord, "Graphics", "Renamed");
        made.for_each<DxfTable>([&](auto &e) {
            if (e.handle() == hex(id)) {
                check(e.definition().block_name == "Renamed" &&
                          e.definition().cells[0][0].value->text == "Graphics",
                      "table text not a block name");
            }
        });
        // Explicit merge replacement clears the former anchor and covered cells.
        made.for_each<DxfTable>([&](auto &e) {
            if (e.handle() == hex(id)) {
                e.set_merged_ranges({});
                auto v = e.definition();
                check(v.merged_ranges.empty() && !v.cells[0][1].merged && v.cells[0][0].merge_width == 1,
                      "unmerge old span");
                e.set_merged_ranges({{1, 1, 2, 2}});
            }
        });
        write(made, prefix + "-merged.dxf");
        // Private groups carry colliding codes and must not introduce cells or styles.
        auto scoped = DxfDocument::create(binary);
        auto sv = definition(0);
        sv.extra_fields = {{102, "{PRIVATE"}, {171, "2"}, {91, "999"}, {102, "}"}, {290, "1"}};
        sv.cells[0][2].extra_fields = {{102, "{PRIVATE"}, {421, "123"}, {171, "2"}, {102, "}"}, {291, "1"}};
        sv.cells[0][2].value->extra_fields = {{102, "{PRIVATE"}, {90, "999"}, {102, "}"}};
        scoped.add_table(sv);
        scoped = read(save(scoped));
        scoped.for_each<DxfTable>([&](auto &e) {
            auto v = e.definition();
            check(v.cells.size() == 3 && v.cells[0].size() == 3 && !v.cells[0][2].style,
                  "private table scopes");
            check(v.extra_fields.size() == 5 && v.cells[0][2].extra_fields.size() == 5 &&
                      v.cells[0][2].value->extra_fields.size() == 3,
                  "unknown fields retained");
            e.set_definition(v);
        });
        scoped = read(save(scoped));
        scoped.for_each<DxfTable>([&](const auto &e) {
            auto v = e.definition();
            check(!v.cells[0][2].style && v.cells[0][2].value->text == "项目02" && v.extra_fields.size() == 5,
                  "private round trip");
        });
        // Point cells have their own 11/21/31 coordinates, separate from table direction.
        auto points = DxfDocument::create(binary);
        auto pv = definition(0);
        TableValue point;
        point.type = 32;
        point.point = {4, 5, 6};
        pv.cells[1][0].value = point;
        points.add_table(pv);
        write(points, prefix + "-points.dxf");
        points = read(save(points));
        points.for_each<DxfTable>([&](const auto &e) {
            auto v = e.definition();
            check(v.horizontal_direction.x == 0 && v.horizontal_direction.y == 1 &&
                      v.cells[1][0].value->point.z == 6,
                  "point cell does not alter direction");
        });
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
