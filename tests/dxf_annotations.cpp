// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
namespace {
ToleranceDefinition tolerance() {
    ToleranceDefinition v;
    v.text = "{\\Fgdt;p}%%v0.5%%v基准A";
    v.dimension_style = "中文标注";
    v.insertion = {10, 20, 30};
    v.direction = {0, 2, 0};
    v.normal = {0, 0, 2};
    return v;
}
LeaderDefinition leader(std::uint64_t annotation) {
    LeaderDefinition v;
    v.dimension_style = "中文标注";
    v.vertices = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    v.path_type = 1;
    v.creation_type = 1;
    v.hookline_direction = 1;
    v.hookline_enabled = true;
    v.text_height = 3;
    v.text_width = 7;
    v.override_color = Color::from_index(4);
    v.annotation_handle = annotation;
    v.horizontal_direction = {0, 1, 0};
    v.block_offset = {2, 3, 4};
    v.annotation_offset = {5, 6, 7};
    return v;
}
void verify(const DxfDocument &d) {
    int n = 0;
    d.for_each<DxfTolerance>([&](auto &e) {
        auto v = e.definition();
        check(v.text == tolerance().text && v.direction == Point3{0, 2, 0} && v.normal == Point3{0, 0, 2} &&
                  v.insertion == Point3{10, 20, 30},
              "tolerance geometry");
        ++n;
    });
    d.for_each<DxfLeader>([&](auto &e) {
        auto v = e.definition();
        check(v.vertices == leader(0).vertices && v.path_type == 1 && v.creation_type == 1 &&
                  v.hookline_direction == 1 && v.hookline_enabled &&
                  v.override_color == Color::from_index(4) && v.annotation_offset == Point3{5, 6, 7},
              "leader geometry");
        check(d.get_record(hex(v.annotation_handle))->type() == "TOLERANCE", "annotation reference");
        ++n;
    });
    check(n == 4, "two annotation pairs");
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "annotation rollback");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        made.add_dim_style("中文标注");
        for (auto space : {EntitySpace::Model, EntitySpace::Paper}) {
            EntityOptions o;
            o.space = space;
            auto t = made.add_tolerance(tolerance(), o);
            made.add_leader(leader(t), o);
        }
        verify(made);
        write(made, prefix + "-created.dxf");
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        verify(d);
        auto targets = d.model_space_entity_handles();
        check(targets.size() == 2, "model annotation pair");
        auto tol = targets[0], lead = targets[1];
        for (auto h : targets) {
            auto dictionary = d.add_extension_dictionary(h);
            d.add_xrecord(hex(dictionary), "Data", {{340, h}});
        }
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        d.for_each<DxfTolerance>([&](auto &e) {
            if (e.handle() != tol)
                return;
            auto v = e.definition();
            v.text = "{\\Fgdt;c}%%v0.25";
            v.insertion = {11, 22, 33};
            e.set_definition(v);
        });
        d.for_each<DxfLeader>([&](auto &e) {
            if (e.handle() != lead)
                return;
            auto v = e.definition();
            v.vertices[1] = {44, 55, 66};
            v.vertices.push_back({10, 11, 12});
            v.arrow_enabled = false;
            v.override_color = Color::by_block();
            e.set_definition(v);
        });
        auto after = records(save(d), binary);
        for (auto &r : before) {
            auto h = identity(r);
            if (!h.empty() && h != tol && h != lead)
                check(at(after, h) == r, "unmodified annotation records");
        }
        write(d, prefix + "-edited.dxf");
        auto tc = hex(d.copy_entity(tol, EntitySpace::Paper));
        auto lc = hex(d.copy_entity(lead, EntitySpace::Paper));
        write(d, prefix + "-copied.dxf");
        check(d.remove_entity(lc) && d.remove_entity(tc), "remove annotation copies");
        fails(d, [&] { d.remove_entity(tol); });
        check(d.remove_entity(lead) && d.remove_entity(tol), "remove annotation pair in reference order");
        check(d.reference_issues().empty(), "annotation deletion references");
        write(d, prefix + "-deleted.dxf");
        d.rename_symbol(SymbolTable::DimStyle, "中文标注", "改名标注");
        d.for_each<DxfLeader>(
            [&](auto &e) { check(e.definition().dimension_style == "改名标注", "leader style rename"); });
        d.for_each<DxfTolerance>(
            [&](auto &e) { check(e.definition().dimension_style == "改名标注", "tolerance style rename"); });
        write(d, prefix + "-renamed.dxf");
        fails(d, [&] { d.remove_symbol(SymbolTable::DimStyle, "改名标注"); });
        // Missing creation/hookline codes and incomplete first vertex follow Rust's reader defaults.
        auto raw = records(save(made), binary);
        std::string first;
        made.for_each<DxfLeader>([&](auto &e) {
            if (first.empty())
                first = e.handle();
        });
        auto &record = at(raw, first);
        bool removed_y = false;
        record.erase(std::remove_if(record.begin(), record.end(),
                                    [&](auto &p) {
                                        if (p.first == 73 || p.first == 74)
                                            return true;
                                        if (p.first == 20 && !removed_y) {
                                            removed_y = true;
                                            return true;
                                        }
                                        return false;
                                    }),
                     record.end());
        auto partial = read(encode(raw, binary));
        partial.for_each<DxfLeader>([&](auto &e) {
            if (e.handle() != first)
                return;
            auto v = e.definition();
            check(v.creation_type == 3 && v.hookline_direction == 1 && v.vertices.size() == 2,
                  "leader missing/partial fields");
        });
        write(partial, prefix + "-partial.dxf");
        fails(made, [&] {
            auto v = leader(0);
            v.override_color = Color::from_rgb(1, 2, 3);
            made.add_leader(v);
        });
        fails(made, [&] {
            auto v = tolerance();
            v.normal.z = std::numeric_limits<double>::infinity();
            made.add_tolerance(v);
        });
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
