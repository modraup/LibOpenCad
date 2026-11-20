// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream input(argv[2], std::ios::binary);
        auto d = DxfDocument::read(input);
        std::string prefix = argv[3];
        auto model = d.model_space_entity_handles();
        check(model.size() == 5 && d.paper_space_entity_handles().size() == 5, "extended entity spaces");
        int count = 0;
        d.for_each<DxfExtendedEntity>([&](auto &e) {
            auto v = e.definition();
            e.set_definition(v);
            ++count;
        });
        check(count == 8, "typed extended entities");
        d.for_each<DxfHatch>([](auto &e) {
            auto v = e.definition();
            check(v.is_mpolygon && v.paths.size() == 2, "MPOLYGON holes");
            e.set_definition(v);
        });
        write(d, prefix + "-roundtrip.dxf");
        d.for_each<DxfExtendedEntity>([&](auto &e) {
            if (std::find(model.begin(), model.end(), e.handle()) == model.end())
                return;
            auto v = e.definition();
            std::visit(
                [](auto &v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, OleFrameDefinition>) {
                        v.upper_left.x = -10;
                        v.payload = {9, 8, 7};
                    } else if constexpr (std::is_same_v<T, LightDefinition>) {
                        v.intensity = 4;
                        v.photometric->intensity = 240;
                    } else if constexpr (std::is_same_v<T, SectionSymbolDefinition>) {
                        v.points[1].bulge = -.5;
                        v.points[0].label = "修改剖面";
                    } else if constexpr (std::is_same_v<T, ViewBorderDefinition>) {
                        v.rotation = -.25;
                        v.center = {9, 10};
                    }
                },
                v);
            e.set_definition(v);
        });
        d.for_each<DxfHatch>([&](auto &e) {
            if (std::find(model.begin(), model.end(), e.handle()) == model.end())
                return;
            auto v = e.definition();
            v.mpolygon_x_direction = {7, 8};
            e.set_definition(v);
        });
        write(d, prefix + "-edited.dxf");
        std::vector<std::string> copies;
        for (auto h : model)
            copies.push_back(hex(d.copy_entity(h, EntitySpace::Paper)));
        write(d, prefix + "-copied.dxf");
        for (auto h : copies)
            check(d.remove_entity(h), "extended deletion");
        write(d, prefix + "-deleted.dxf");
        auto fresh = DxfDocument::create(binary);
        d.for_each<DxfExtendedEntity>([&](auto &e) {
            if (std::find(model.begin(), model.end(), e.handle()) != model.end())
                fresh.add_extended_entity(e.definition());
        });
        d.for_each<DxfHatch>([&](auto &e) {
            if (std::find(model.begin(), model.end(), e.handle()) != model.end())
                fresh.add_hatch(e.definition());
        });
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "extended references");
        auto before = save(fresh);
        bool failed = false;
        try {
            LightDefinition v;
            v.intensity = std::numeric_limits<double>::infinity();
            fresh.add_extended_entity(v);
        } catch (const std::invalid_argument &) {
            failed = true;
        }
        check(failed && before == save(fresh), "nonfinite rollback");
        // Repeated code 53 is distinct in rotation and web-angle fields.
        fresh.for_each<DxfExtendedEntity>([](auto &e) {
            if (e.type() != "LIGHT")
                return;
            auto v = std::get<LightDefinition>(e.definition());
            v.photometric->web_rotation.y = 8;
            v.photometric->web_angles[3] = 9;
            e.set_definition(v);
            auto got = std::get<LightDefinition>(e.definition());
            check(got.photometric->web_rotation.y == 8 && got.photometric->web_angles[3] == 9,
                  "photometric repeated group preservation");
        });
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
