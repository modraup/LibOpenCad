// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        std::string prefix = argv[3];
        auto h = d.model_space_entity_handles()[0];
        int styles = 0;
        for (const auto &s : d.symbols(SymbolTable::TextStyle)) {
            auto def = d.get_text_style_by_handle(s.handle);
            if (def->flags & 1) {
                ++styles;
                check(s.name.empty(), "anonymous shape STYLE");
                d.set_text_style_by_handle(s.handle, *def);
            }
        }
        check(styles == 2, "distinct anonymous handles");
        d.for_each<DxfShape>([](auto &s) {
            check(s.display_properties().transparency == Transparency::explicit_alpha(77), "Rust rounding");
            s.set_definition(s.definition());
        });
        write(d, prefix + "-roundtrip.dxf");
        d.for_each<DxfShape>([&](auto &s) {
            if (s.handle() == h) {
                auto v = s.definition();
                v.size = 9;
                v.rotation = .75;
                s.set_definition(v);
                auto p = s.display_properties();
                p.transparency = Transparency::by_block();
                p.linetype_scale = 4;
                p.color_name.clear();
                s.set_display_properties(p);
            }
        });
        d.for_each<DxfLine>([](auto &l) {
            check(l.thickness() == 2, "LINE thickness read");
            l.set_thickness(6);
            std::vector<std::uint8_t> graphics(300);
            for (std::size_t i = 0; i < graphics.size(); ++i)
                graphics[i] = i % 256;
            l.set_graphic_data(graphics);
            check(l.graphic_data() == graphics, "graphic byte chunks");
            l.set_normal({0, 1, 1});
        });
        write(d, prefix + "-edited.dxf");
        auto graphics_loaded = read(save(d));
        graphics_loaded.for_each<DxfLine>([](auto &l) {
            auto v = l.graphic_data();
            check(v && v->size() == 300 && (*v)[299] == 43, "graphic bytes saved");
            l.set_graphic_data(std::nullopt);
            check(!l.graphic_data(), "clear graphic bytes");
        });
        auto copy = hex(d.copy_entity(h, EntitySpace::Paper));
        check(d.remove_entity(copy), "SHAPE copy/delete");
        write(d, prefix + "-deleted.dxf");
        auto fresh = DxfDocument::create(binary);
        TextStyleDefinition a;
        a.font_file = "first.shx";
        auto aid = fresh.add_shape_file_style(a);
        a.font_file = "second.shx";
        auto bid = fresh.add_shape_file_style(a);
        check(aid != bid, "unique anonymous style handles");
        LineTypeDefinition pattern;
        pattern.name = "形线型";
        pattern.pattern_length = 3;
        LineTypeComplexData glyph;
        glyph.style_handle = aid;
        glyph.shape_number = 42;
        glyph.scale = 2;
        glyph.rotation = .5;
        glyph.offset = {1, -2};
        pattern.elements = {{2, glyph}, {-1, std::nullopt}};
        fresh.add_linetype(pattern);
        check(fresh.get_linetype("形线型")->elements()[0].complex->style_handle == aid,
              "complex linetype references anonymous shape STYLE by handle");
        ShapeDefinition shape;
        shape.name = "新符号";
        auto id = hex(fresh.add_shape(shape));
        EntityDisplayProperties p;
        p.transparency = Transparency::from_percent(.33);
        p.linetype_scale = 3;
        fresh.set_entity_display_properties(id, p);
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "shape graph");
        auto before = save(fresh);
        bool rejected = false;
        try {
            fresh.add_shape_file_style(a);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && save(fresh) == before, "duplicate shape font atomic rejection");
        rejected = false;
        try {
            auto v = shape;
            v.size = std::numeric_limits<double>::quiet_NaN();
            fresh.add_shape(v);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && save(fresh) == before, "nonfinite geometry rejected");
        for (int alpha = 0; alpha < 256; ++alpha) {
            auto t = Transparency::explicit_alpha(alpha);
            check(Transparency::from_alpha_value(t.to_dxf_value()) == t, "all explicit transparency bytes");
        }
        check(Transparency::from_alpha_value(0x3000000) == Transparency::explicit_alpha(255),
              "legacy alpha method");
        check(!Transparency::from_layer_alpha_value(0x4000000), "unknown layer alpha");
        check(Transparency::from_percent(std::numeric_limits<double>::quiet_NaN()) ==
                  Transparency::explicit_alpha(0),
              "Rust NaN cast");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
