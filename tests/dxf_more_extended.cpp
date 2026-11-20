// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        std::string prefix = argv[3];
        auto model = d.model_space_entity_handles();
        check(model.size() == 12 && d.paper_space_entity_handles().size() == 12, "extended spaces");
        int count = 0;
        d.for_each<DxfExtendedEntity>([&](auto &e) {
            e.set_definition(e.definition());
            ++count;
        });
        check(count == 24, "extended types");
        write(d, prefix + "-roundtrip.dxf");
        d.for_each<DxfExtendedEntity>([&](auto &e) {
            if (std::find(model.begin(), model.end(), e.handle()) == model.end())
                return;
            auto v = e.definition();
            std::visit(
                [](auto &v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, SectionObjectDefinition>)
                        v.vertices[0].z = 9;
                    else if constexpr (std::is_same_v<T, ArcAlignedTextDefinition>)
                        v.text = "修改圆弧";
                    else if constexpr (std::is_same_v<T, RemoteTextDefinition>)
                        v.rotation = .75;
                    else if constexpr (std::is_same_v<T, PositionMarkerDefinition>)
                        v.landing_gap = 4;
                    else if constexpr (std::is_same_v<T, CoordinationModelDefinition>)
                        v.transform[12] = 9;
                    else if constexpr (std::is_same_v<T, OleLegacyDefinition>)
                        v.payload = {9, 8, 7};
                    else if constexpr (std::is_same_v<T, LayoutPrintDefinition>)
                        v.flags = 12;
                    else if constexpr (std::is_same_v<T, PointCloudDefinition>)
                        v.origin.z = 99;
                    else if constexpr (std::is_same_v<T, PointCloudExDefinition>)
                        v.crops[0].points[1].z = 99;
                },
                v);
            e.set_definition(v);
        });
        write(d, prefix + "-edited.dxf");
        std::vector<std::string> copies;
        for (auto h : model)
            copies.push_back(hex(d.copy_entity(h, EntitySpace::Paper)));
        write(d, prefix + "-copied.dxf");
        for (auto h : copies)
            check(d.remove_entity(h), "extended remove");
        write(d, prefix + "-deleted.dxf");
        auto fresh = DxfDocument::create(binary);
        d.for_each<DxfExtendedEntity>([&](auto &e) {
            if (std::find(model.begin(), model.end(), e.handle()) != model.end())
                fresh.add_extended_entity(e.definition());
        });
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "extended refs");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
