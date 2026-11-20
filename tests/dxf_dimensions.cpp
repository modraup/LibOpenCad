// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <limits>
namespace {
DimensionDefinition definition(std::size_t kind) {
    DimensionDefinition v;
    v.definition_point = {1, 2, 3};
    v.text_middle_point = {4, 5, 6};
    v.insertion_point = {7, 8, 9};
    v.normal = {0, 1, 1};
    v.style_name = "中文标注";
    v.text_override = "尺寸<>";
    v.actual_measurement = 123.25 + kind;
    v.text_rotation = .25;
    v.horizontal_direction = .5;
    v.line_spacing_factor = 1.25;
    v.line_spacing_style = 2;
    v.attachment_point = 8;
    v.text_user_positioned = true;
    v.flip_arrow1 = true;
    v.flip_arrow2 = true;
    switch (kind) {
    case 0:
        v.geometry = LinearDimension{{10, 20, 30}, {40, 50, 60}, .5, .25};
        break;
    case 1:
        v.geometry = AlignedDimension{{10, 20, 30}, {40, 50, 60}, .25};
        break;
    case 2:
        v.geometry = Angular2LineDimension{{10, 20, 30}, {40, 50, 60}, {2, 3, 4}, {5, 6, 7}};
        break;
    case 3:
        v.geometry = DiameterDimension{{10, 20, 30}, 0};
        break;
    case 4:
        v.geometry = RadiusDimension{{10, 20, 30}, 2};
        break;
    case 5:
        v.geometry = Angular3PointDimension{{10, 20, 30}, {40, 50, 60}, {2, 3, 4}};
        break;
    case 6:
        v.geometry = OrdinateDimension{{10, 20, 30}, {40, 50, 60}, false};
        break;
    case 7:
        v.geometry =
            ArcDimension{{10, 20, 30}, {40, 50, 60}, {2, 3, 4}, true, true, .2, 2.4, {5, 6, 7}, {8, 9, 10}};
        break;
    case 8:
        v.geometry = LargeRadialDimension{{10, 20, 30}, {40, 50, 60}, {2, 3, 4}, .75};
        break;
    }
    return v;
}
void change(DimensionDefinition &v) {
    v.text_rotation = -.75;
    v.line_spacing_factor = 1.5;
    v.text_override = "修改<>";
    std::visit(
        [](auto &g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, LinearDimension> || std::is_same_v<T, AlignedDimension> ||
                          std::is_same_v<T, Angular2LineDimension> ||
                          std::is_same_v<T, Angular3PointDimension>)
                g.first_point.x += 10;
            else if constexpr (std::is_same_v<T, DiameterDimension>)
                g.angle_vertex.x += 10;
            else if constexpr (std::is_same_v<T, RadiusDimension>)
                g.chord_point.x += 10;
            else if constexpr (std::is_same_v<T, OrdinateDimension>)
                g.feature_location.x += 10;
            else if constexpr (std::is_same_v<T, ArcDimension>)
                g.first_extension_point.x += 10;
            else if constexpr (std::is_same_v<T, LargeRadialDimension>)
                g.jog_point.x += 10;
        },
        v.geometry);
}
void verify(const DxfDocument &d) {
    std::array<int, 9> counts{};
    d.for_each<DxfDimension>([&](auto &e) {
        auto v = e.definition();
        ++counts[v.geometry.index()];
        check(v.definition_point == Point3{1, 2, 3} && v.normal == Point3{0, 1, 1} &&
                  v.text_override == std::optional<std::string>{"尺寸<>"} && v.line_spacing_factor == 1.25 &&
                  v.text_user_positioned && v.insertion_point == Point3{7, 8, 9} && v.flip_arrow1 &&
                  v.flip_arrow2 && v.horizontal_direction == .5,
              "dimension base scope");
        if (auto arc = std::get_if<ArcDimension>(&v.geometry))
            check(arc->partial && arc->has_leader && arc->end_parameter == 2.4 && v.attachment_point == 8,
                  "arc duplicate group scopes");
    });
    for (auto n : counts)
        check(n == 2, "nine dimension types in two spaces");
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "dimension failure rollback");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        made.add_dim_style("中文标注");
        for (auto space : {EntitySpace::Model, EntitySpace::Paper})
            for (std::size_t kind = 0; kind < 9; ++kind) {
                EntityOptions o;
                o.space = space;
                made.add_dimension(definition(kind), o);
            }
        verify(made);
        write(made, prefix + "-created.dxf");
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        verify(d);
        auto sources = d.model_space_entity_handles();
        check(sources.size() == 9, "model dimensions");
        for (auto h : sources) {
            auto dictionary = d.add_extension_dictionary(h);
            d.add_xrecord(hex(dictionary), "Data", {{340, h}});
        }
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        d.for_each<DxfDimension>([&](auto &e) {
            if (std::find(sources.begin(), sources.end(), e.handle()) == sources.end())
                return;
            auto v = e.definition();
            change(v);
            e.set_definition(v);
        });
        auto after = records(save(d), binary);
        for (auto h : d.paper_space_entity_handles())
            check(at(before, h) == at(after, h), "paper dimensions unchanged");
        write(d, prefix + "-edited.dxf");
        std::vector<std::string> copies;
        for (auto h : sources)
            copies.push_back(hex(d.copy_entity(h, EntitySpace::Paper)));
        write(d, prefix + "-copied.dxf");
        for (auto h : copies)
            check(d.remove_entity(h), "delete dimension copy");
        for (auto h : sources)
            check(d.remove_entity(h), "delete source dimension");
        write(d, prefix + "-deleted.dxf");
        check(d.paper_space_entity_handles().size() == 9 && d.model_space_entity_handles().empty() &&
                  d.reference_issues().empty(),
              "dimension deletion/ownership");
        d.rename_symbol(SymbolTable::DimStyle, "中文标注", "改名标注");
        write(d, prefix + "-renamed.dxf");
        fails(made, [&] {
            auto v = definition(0);
            v.style_name = "missing";
            made.add_dimension(v);
        });
        fails(made, [&] {
            auto v = definition(7);
            std::get<ArcDimension>(v.geometry).end_parameter = std::numeric_limits<double>::infinity();
            made.add_dimension(v);
        });
        made.for_each<DxfDimension>([](auto &e) {
            auto v = e.definition();
            if (auto g = std::get_if<DiameterDimension>(&v.geometry)) {
                g->leader_length = 8;
                e.set_definition(v);
                check(std::get<DiameterDimension>(e.definition().geometry).leader_length == 8,
                      "preserve diameter leader length");
            }
        });
        write(made, prefix + "-reader-gaps.dxf");
        // Clearing an override removes group 1; absent caches omit the invalid empty group 2.
        made.for_each<DxfDimension>([](auto &e) {
            auto v = e.definition();
            v.text_override.reset();
            e.set_definition(v);
            check(!e.definition().text_override, "clear dimension text override");
        });
        write(made, prefix + "-cleared.dxf");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
