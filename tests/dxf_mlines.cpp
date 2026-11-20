// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
namespace {
MLineStyleDefinition style() {
    MLineStyleDefinition s;
    s.description = "道路断面";
    s.flags = 1 | 2 | 16 | 512;
    s.start_angle = .75;
    s.end_angle = 1.25;
    s.fill_color = Color::from_rgb(0x12, 0x34, 0x56);
    s.elements = {{1.5, Color::from_index(2), "Continuous"},
                  {-.5, Color::from_rgb(0xab, 0x12, 0xef), "BYLAYER"}};
    return s;
}
MLineDefinition line(bool paper) {
    MLineDefinition m;
    m.style_name = "中文多线";
    m.flags = paper ? 9 : 7;
    m.justification = 2;
    m.scale_factor = 2.5;
    m.start_point = {1, 2, 3};
    m.normal = {0, 0, 2};
    for (int i = 0; i < 3; ++i) {
        double n = i;
        MLineVertex v;
        v.position = {1 + n * 10, 2 + n * 20, 3 + n};
        v.direction = {1, n, 0};
        v.miter = {-n, 1, 0};
        for (int j = 0; j < 2; ++j)
            v.segments.push_back({{n + j, 0, 10 + n}, {1 + n, 4 + n}});
        m.vertices.push_back(v);
    }
    return m;
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "MLINE rejected change must be atomic");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        const bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        auto style_id = made.add_mline_style("中文多线", style());
        for (auto space : {EntitySpace::Model, EntitySpace::Paper}) {
            EntityOptions o;
            o.space = space;
            made.add_mline(line(space == EntitySpace::Paper), o);
        }
        check(made.get_mline_style("STANDARD") && made.get_mline_style("中文多线") &&
                  !made.get_mline_style("unknown"),
              "style lookup");
        made.for_each<DxfMLine>(
            [&](const auto &e) { check(e.definition().style_handle == style_id, "style reference"); });
        write(made, prefix + "-created.dxf");
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        const auto source = d.model_space_entity_handles().at(0);
        const auto neighbor = d.paper_space_entity_handles().at(0);
        auto extension = d.add_extension_dictionary(source);
        d.add_xrecord(hex(extension), "Data", {{340, source}});
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        d.for_each<DxfMLine>([&](auto &e) {
            if (e.handle() != source)
                return;
            auto v = e.definition();
            check(v.vertices.size() == 3 && v.vertices[1].segments[0].parameters[0] == 1,
                  "read variable lists");
            v.vertices[1].position = {14, 25, 36};
            v.vertices[1].segments[0].parameters = {8, 9, 10, 11};
            v.vertices[2].segments[1].area_fill_parameters = {2, 3};
            v.scale_factor = 3;
            e.set_definition(v);
        });
        d.for_each<DxfMLineStyle>([](auto &s) {
            if (s.name() != "中文多线")
                return;
            auto v = s.definition();
            check(v.fill_color == Color::from_rgb(0x12, 0x34, 0x56), "style fill scope");
            v.start_angle = .5;
            v.fill_color = Color::by_block();
            v.elements[1].color = Color::from_index(3);
            s.set_definition(v);
        });
        auto edited_records = records(save(d), binary);
        check(at(edited_records, neighbor) == at(before, neighbor), "neighbor untouched");
        write(d, prefix + "-edited.dxf");
        auto copy = hex(d.copy_entity(source, EntitySpace::Paper));
        write(d, prefix + "-copied.dxf");
        const auto edited_source = at(edited_records, source);
        d.for_each<DxfMLine>([&](auto &e) {
            if (e.handle() == copy) {
                auto v = e.definition();
                v.vertices[0].position.x = 99;
                e.set_definition(v);
            }
        });
        auto copied_records = records(save(d), binary);
        check(at(copied_records, source) == edited_source, "copy edit independent");
        write(d, prefix + "-copy-edited.dxf");
        check(d.remove_entity(copy) && d.remove_entity(source), "MLINE deletion");
        check(d.reference_issues().empty(), "MLINE references after deletion");
        write(d, prefix + "-deleted.dxf");
        fails(made, [&] { made.add_mline_style("中文多线", style()); });
        fails(made, [&] {
            auto v = line(false);
            v.style_handle = 1;
            made.add_mline(v);
        });
        fails(made, [&] {
            auto v = line(false);
            v.vertices[0].segments.clear();
            made.add_mline(v);
        });
        fails(made, [&] {
            auto v = line(false);
            v.vertices[1].segments[0].parameters[0] = std::numeric_limits<double>::infinity();
            made.add_mline(v);
        });
        // The Rust reader ignores declared counts, defaults partial points, and accepts 41 after 75.
        auto data = records(save(made), binary);
        const auto first = made.model_space_entity_handles().front();
        for (auto &r : data)
            if (identity(r) == first) {
                bool vertex = false, removed = false, fill = false;
                for (auto it = r.begin(); it != r.end();) {
                    if (it->first == 11)
                        vertex = true;
                    if (vertex && it->first == 21 && !removed) {
                        it = r.erase(it);
                        removed = true;
                        continue;
                    }
                    if (it->first == 74)
                        fill = false;
                    if (it->first == 75)
                        fill = true;
                    if (fill && it->first == 42)
                        it->first = 41;
                    ++it;
                }
            }
        auto partial = read(encode(data, binary));
        partial.for_each<DxfMLine>([&](auto &e) {
            if (e.handle() == first)
                check(e.definition().vertices[0].position == Point3{}, "partial point defaults");
        });
        write(partial, prefix + "-partial.dxf");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
