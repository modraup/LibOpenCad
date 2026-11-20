// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        std::string prefix = argv[3], h, neighbor;
        d.for_each<DxfMaterial>([&](auto &m) {
            if (m.definition().name == "中文材质")
                h = m.handle();
            if (m.definition().name == "邻居材质")
                neighbor = m.handle();
            m.set_definition(m.definition());
        });
        check(!h.empty(), "typed material");
        write(d, prefix + "-roundtrip.dxf");
        auto before = records(save(d), binary);
        auto m = d.material_definition(h);
        check(m.ambient.rgb == 0x123456 && m.maps[5].file_name == "纹理5.png" && m.self_illumination == 2.5,
              "material fields");
        m.opacity = .3;
        m.maps[0].transform[12] = 99;
        m.advanced.emplace();
        m.advanced->color_bleed_scale = .75;
        m.advanced->reflectance_scale = 1.5;
        m.advanced->two_sided = true;
        m.advanced->normal_map_method = 1;
        m.advanced->normal_map_strength = .7;
        m.normal_map.source = 1;
        m.normal_map.file_name = "法线.png";
        d.set_material_definition(h, m);
        write(d, prefix + "-edited.dxf");
        auto after = records(save(d), binary);
        check(at(before, neighbor) == at(after, neighbor), "neighbor material unchanged");
        auto loaded = read(save(d));
        check(loaded.material_definition(h).advanced->reflectance_scale == 1.5, "advanced scale restore");
        auto fresh = DxfDocument::create(binary);
        MaterialDefinition p;
        p.name = "程序材质";
        for (auto &map : p.maps) {
            map.source = 2;
            map.texture.emplace();
            map.texture->mode = 2;
        }
        p.maps[0].texture->value = true;
        p.maps[1].texture->value = std::int16_t(-7);
        p.maps[2].texture->value = 1.25;
        p.maps[3].texture->value = Color::from_true_color_value(0x223344);
        p.maps[4].texture->value = std::string("纹理参数");
        auto &table = *p.maps[5].texture;
        table.table = true;
        table.table_end = true;
        MaterialTexture a;
        a.color1.flag = 1;
        a.color1.rgb = 0xabcdef;
        a.color2.flag = 1;
        a.color2.rgb = 0x123456;
        table.children.push_back({"棋盘", a});
        a.mode = 1;
        table.children.push_back({"渐变", a});
        auto id = hex(fresh.add_material(p));
        auto line = hex(fresh.add_line({0, 0, 0}, {1, 2, 3}));
        EntityDisplayProperties display;
        display.material = std::stoull(id, nullptr, 16);
        fresh.set_entity_display_properties(line, display);
        write(fresh, prefix + "-fresh.dxf");
        auto reload = read(save(fresh));
        check(reload.material_definition(id).maps[5].texture->children.size() == 2,
              "recursive material texture native read");
        reload.for_each<DxfLine>([&](const auto &l) {
            check(l.display_properties().material == display.material, "DXF material reference retained");
        });
        check(fresh.reference_issues().empty(), "material graph");
        auto saved = save(fresh);
        bool rejected = false;
        try {
            fresh.add_material(p);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && save(fresh) == saved, "duplicate material atomic");
        rejected = false;
        try {
            p.maps[0].transform[7] = std::numeric_limits<double>::infinity();
            p.name = "bad";
            fresh.add_material(p);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && save(fresh) == saved, "nonfinite matrix atomic");
        // Asset XRECORD checker overlays must follow edits instead of restoring stale colors.
        MaterialDefinition asset;
        asset.name = "棋盘资产";
        asset.maps[0].texture.emplace();
        asset.maps[0].texture->color1 = {1, 1, 0x123456};
        asset.maps[0].texture->color2 = {1, 1, 0xabcdef};
        auto ah = hex(fresh.add_material(asset));
        auto av = read(save(fresh)).material_definition(ah);
        check(av.maps[0].texture->color1.rgb == 0x123456, "checker asset restore");
        av.maps[0].texture->color1.rgb = 0x654321;
        fresh.set_material_definition(ah, av);
        write(fresh, prefix + "-assets.dxf");
        check(read(save(fresh)).material_definition(ah).maps[0].texture->color1.rgb == 0x654321,
              "checker asset synchronized");
        av.maps[0].texture.reset();
        fresh.set_material_definition(ah, av);
        check(!read(save(fresh)).material_definition(ah).maps[0].texture, "checker asset clear");
        auto scoped = d;
        auto s = scoped.material_definition(h);
        s.extra_fields = {{102, "{PRIVATE"}, {90, "999"}, {102, "}"}};
        scoped.set_material_definition(h, s);
        check(read(save(scoped)).material_definition(h).self_illumination == 2.5,
              "material private scalar scope");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
