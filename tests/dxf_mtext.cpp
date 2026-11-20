// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <limits>
namespace {
MTextDefinition definition() {
    MTextDefinition t;
    for (int i = 0; i < 90; ++i)
        t.text += "中";
    t.text += "{\\C1;红色}\\P第二行\\S1/2;";
    t.insertion = {1, 2, 3};
    t.height = 2;
    t.rectangle_width = 40;
    t.rectangle_height = 12;
    t.rotation = .5;
    t.attachment = 5;
    t.drawing_direction = 3;
    t.line_spacing_style = 2;
    t.line_spacing_factor = 1.25;
    t.extents_width = 35;
    t.extents_height = 8;
    t.background_flags = 17;
    t.background_scale = 1.75;
    t.background_color = Color::from_rgb(12, 34, 56);
    t.background_transparency = 0x02000080;
    t.columns = {2, 2, true, false, 18, 4, {9, 11}};
    return t;
}
void verify(const DxfMText &e) {
    auto t = e.definition(), r = definition();
    check(t.text == r.text && e.text() == r.text && t.style == r.style, "MTEXT text and style");
    check(t.insertion == r.insertion && t.height == r.height && t.rectangle_width == r.rectangle_width &&
              t.rectangle_height == r.rectangle_height && std::abs(t.rotation - r.rotation) < 1e-12 &&
              t.normal == r.normal,
          "MTEXT geometry");
    check(t.attachment == 5 && t.drawing_direction == 3 && t.line_spacing_style == 2 &&
              t.line_spacing_factor == 1.25 && t.extents_width == 35 && t.extents_height == 8,
          "MTEXT layout");
    check(t.background_flags == 17 && t.background_scale == 1.75 &&
              t.background_color == r.background_color && t.background_transparency == 0x02000080,
          "MTEXT background");
    check(t.columns.type == 2 && t.columns.count == 2 && t.columns.flow_reversed && !t.columns.auto_height &&
              t.columns.width == 18 && t.columns.gutter == 4 &&
              t.columns.heights == std::vector<double>{9, 11},
          "MTEXT columns");
}
template <class F> void fail(DxfDocument &d, F fn) {
    const auto before = save(d);
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed && save(d) == before, "MTEXT atomic rejection");
}
template <class F> void edit(DxfDocument &d, const std::string &h, F fn) {
    int count = 0;
    d.for_each<DxfMText>([&](auto &e) {
        if (e.handle() == h) {
            fn(e);
            ++count;
        }
    });
    check(count == 1, "MTEXT identity");
}
void unchanged(const Records &before, Records after, const std::set<std::string> &except = {}) {
    for (auto &r : before) {
        auto h = identity(r);
        if (!h.empty() && !except.count(h))
            check(at(after, h) == r, "MTEXT neighbor preserved");
    }
}
void boundaries(DxfDocument d, bool binary, const std::string &prefix) {
    auto h = d.model_space_entity_handles().front();
    fail(d, [&] {
        edit(d, h, [](auto &e) {
            auto t = e.definition();
            t.height = std::numeric_limits<double>::infinity();
            e.set_definition(t);
        });
    });
    fail(d, [&] {
        edit(d, h, [](auto &e) {
            auto t = e.definition();
            t.columns.heights[0] = std::numeric_limits<double>::quiet_NaN();
            e.set_definition(t);
        });
    });
    fail(d, [&] {
        auto t = definition();
        t.style = "missing";
        d.add_mtext(t);
    });
    auto data = records(save(d), binary);
    auto &raw = at(data, h);
    auto marker = std::find(raw.begin(), raw.end(), Pair{100, "AcDbMText"});
    Record private_group{
        {102, "{PRIVATE"}, {1, "private"}, {40, binary ? detail::binary_double(777) : "777"}, {102, "}"}};
    raw.insert(marker + 1, private_group.begin(), private_group.end());
    raw.insert(raw.end(), {{1001, "OTHER"}, {1000, "untouched"}});
    auto protected_doc = read(encode(data, binary));
    edit(protected_doc, h, [](auto &e) {
        verify(e);
        auto t = e.definition();
        t.columns.heights = {20, 30};
        e.set_definition(t);
        e.set_text("一\r\n二\n三\r四");
        check(e.text() == "一\\P二\\P三\\P四", "literal newlines sanitized");
    });
    auto result = records(save(protected_doc), binary);
    auto &r = at(result, h);
    check(std::search(r.begin(), r.end(), private_group.begin(), private_group.end()) != r.end() &&
              r.back() == Pair{1000, "untouched"},
          "MTEXT private groups and unrelated XDATA");
    edit(protected_doc, h, [](auto &e) {
        check(e.definition().columns.heights == std::vector<double>{20, 30},
              "MTEXT XDATA replacement in same application");
        auto t = e.definition();
        t.columns = {};
        e.set_definition(t);
        check(e.definition().columns.type == 0, "MTEXT remove columns");
    });
    // Standard column codes reuse 50 for heights; a preceding 50 is rotation.
    auto raw_doc = DxfDocument::create(binary);
    auto t = definition();
    t.columns = {};
    auto first = hex(raw_doc.add_mtext(t)), second = hex(raw_doc.add_mtext(t));
    auto values = records(save(raw_doc), binary);
    for (auto id : {first, second}) {
        auto &r = at(values, id);
        r.erase(std::remove_if(r.begin(), r.end(),
                               [](const auto &p) { return p.first == 11 || p.first == 21 || p.first == 31; }),
                r.end());
        for (auto p : Record{{50, "90"},
                             {75, "2"},
                             {76, "2"},
                             {78, "1"},
                             {79, "0"},
                             {48, "18"},
                             {49, "4"},
                             {50, "9"},
                             {50, "11"}})
            r.push_back({p.first, binary ? detail::binary_value_from_text(p.first, p.second) : p.second});
    }
    for (auto p : Record{{11, "0"}, {21, "0"}, {31, "1"}})
        at(values, second)
            .push_back({p.first, binary ? detail::binary_value_from_text(p.first, p.second) : p.second});
    auto raw_d = read(encode(values, binary));
    edit(raw_d, first, [](auto &e) {
        auto t = e.definition();
        check(std::abs(t.rotation - 1.5707963267948966) < 1e-12 &&
                  t.columns.heights == std::vector<double>{9, 11},
              "MTEXT angle and column scope");
    });
    edit(raw_d, second, [](auto &e) {
        check(e.definition().rotation == 0 && !e.definition().x_direction,
              "MTEXT zero explicit direction precedence");
    });
    write(raw_d, prefix + "-raw.dxf");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 5, "format version input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string version = argv[2], prefix = argv[4];
        auto made = DxfDocument::create(binary);
        if (version == "2007") {
            auto data = records(save(made), binary);
            for (auto &r : data)
                for (auto &p : r)
                    if (p.first == 1 && p.second == "AC1032")
                        p.second = "AC1021";
            made = read(encode(data, binary));
        }
        for (auto space : {EntitySpace::Model, EntitySpace::Paper}) {
            EntityOptions o;
            o.space = space;
            made.add_mtext(definition(), o);
        }
        made.for_each<DxfMText>(verify);
        write(made, prefix + "-created.dxf");
        std::ifstream in(argv[3], std::ios::binary);
        auto d = DxfDocument::read(in);
        int count = 0;
        d.for_each<DxfMText>([&](const auto &e) {
            verify(e);
            ++count;
        });
        check(count == 2, "MTEXT count");
        boundaries(d, binary, prefix);
        auto h = d.model_space_entity_handles().front();
        auto paper = d.paper_space_entity_handles();
        auto dict = hex(d.add_extension_dictionary(h));
        auto xr = hex(d.add_xrecord(dict, "Data", {{340, h}}));
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        edit(d, h, [](auto &e) {
            auto t = e.definition();
            t.text = "已修改\\P{\\H2x;大字}";
            t.insertion = {7, 8, 9};
            t.rotation = -.75;
            t.x_direction.reset();
            t.columns.heights = {15, 17, 19};
            t.columns.count = 3;
            t.columns.width = 12;
            t.columns.gutter = 2;
            t.background_color = Color::from_index(4);
            t.attachment = 9;
            e.set_definition(t);
        });
        auto after = records(save(d), binary);
        unchanged(before, after, {h});
        write(d, prefix + "-edited.dxf");
        auto copy = hex(d.copy_entity(h, EntitySpace::Paper));
        edit(d, copy, [](auto &e) { e.set_text("副本\\P独立"); });
        unchanged(after, records(save(d), binary));
        write(d, prefix + "-copied.dxf");
        check(d.remove_entity(copy) && d.remove_entity(h), "MTEXT removal");
        unchanged(before, records(save(d), binary), {h, dict, xr});
        check(d.paper_space_entity_handles() == paper && d.model_space_entity_handles().empty() &&
                  d.reference_issues().empty(),
              "MTEXT ownership and graph");
        write(d, prefix + "-deleted.dxf");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
