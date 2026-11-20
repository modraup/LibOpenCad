// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        std::string prefix = argv[3];
        int count = 0;
        d.for_each<DxfObjectContext>([&](auto &e) {
            e.set_definition(e.definition());
            ++count;
        });
        check(count == 32, "context kinds");
        write(d, prefix + "-roundtrip.dxf");
        auto source = d.model_space_entity_handles()[0];
        auto dict = d.extension_dictionary_handle(source);
        std::vector<ObjectContextDefinition> values;
        d.for_each<DxfObjectContext>([&](auto &e) {
            if (e.owner_handle() != dict)
                return;
            auto v = e.definition();
            v.version = 9;
            v.is_default = false;
            std::visit(
                [](auto &p) {
                    using T = std::decay_t<decltype(p)>;
                    if constexpr (std::is_same_v<T, BlockReferenceContext>)
                        p.insertion.x = 99;
                    else if constexpr (std::is_same_v<T, TextContext>)
                        p.rotation = .75;
                    else if constexpr (std::is_same_v<T, MTextContext>)
                        p.columns->heights[0] = 12;
                    else if constexpr (std::is_same_v<T, DimensionContext>)
                        p.first.z = 99;
                    else if constexpr (std::is_same_v<T, MTextAttributeContext>)
                        p.embedded->text.insertion.z = 99;
                    else if constexpr (std::is_same_v<T, LeaderContext>)
                        p.points[1].z = 99;
                    else if constexpr (std::is_same_v<T, FeatureControlFrameContext>)
                        p.location.z = 99;
                    else if constexpr (std::is_same_v<T, HatchScaleContext>)
                        p.pattern_lines[0].dash_lengths[0] = 9;
                    else if constexpr (std::is_same_v<T, HatchViewContext>)
                        p.rotation = .75;
                },
                v.payload);
            e.set_definition(v);
            values.push_back(v);
        });
        write(d, prefix + "-edited.dxf");
        auto copy = hex(d.copy_entity(source, EntitySpace::Paper));
        write(d, prefix + "-copied.dxf");
        check(d.remove_entity(copy), "context cascade");
        write(d, prefix + "-deleted.dxf");
        auto fresh = DxfDocument::create(binary);
        auto root = fresh.root_dictionary_handle();
        ScaleDefinition scale;
        scale.name = "1:50";
        scale.paper_units = 1;
        scale.drawing_units = 50;
        auto scale_id = fresh.add_auxiliary_object(root, "比例", scale);
        auto line = hex(fresh.add_line({}, {1, 0, 0}));
        int n = 0;
        for (auto v : values) {
            v.scale = scale_id;
            if (auto a = std::get_if<MTextAttributeContext>(&v.payload))
                a->embedded->scale = scale_id;
            fresh.add_annotation_context(line, "*A" + std::to_string(n++), v);
        }
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "context references");
        auto before = save(fresh);
        bool bad = false;
        try {
            auto v = values[0];
            v.scale = 0xFFFFFF;
            fresh.add_annotation_context(line, "bad", v);
        } catch (const std::invalid_argument &) {
            bad = true;
        }
        check(bad && save(fresh) == before, "context rollback");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
