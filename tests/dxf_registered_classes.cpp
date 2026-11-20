// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        std::string prefix = argv[3];
        int entities = 0, objects = 0;
        d.for_each<DxfSemanticEntity>([&](auto &e) {
            auto v = e.properties();
            check(v.size() == 8 && std::get<std::int64_t>(v[5].value) == 123456789012LL,
                  "typed semantic widths");
            e.set_properties(v);
            ++entities;
        });
        d.for_each<DxfSemanticObject>([&](auto &e) {
            e.set_properties(e.properties());
            ++objects;
        });
        check(entities == 2 && objects == 2, "registered typed records");
        write(d, prefix + "-roundtrip.dxf");
        auto h = d.model_space_entity_handles()[0];
        d.for_each<DxfSemanticEntity>([&](auto &e) {
            if (e.handle() == h) {
                auto v = e.properties();
                v[0].value = std::string("修改扩展");
                e.set_properties(v);
            }
        });
        write(d, prefix + "-edited.dxf");
        auto copy = hex(d.copy_entity(h, EntitySpace::Paper));
        write(d, prefix + "-copied.dxf");
        check(d.remove_entity(copy), "registered cascade");
        write(d, prefix + "-deleted.dxf");
        auto fresh = DxfDocument::create(binary);
        for (auto c : d.classes())
            if (c.name == "ACAD_PROXY_ENTITY_WRAPPER" || c.name == "ACAD_PROXY_OBJECT_WRAPPER")
                fresh.set_class(c);
        std::vector<SemanticProperty> p{{"AcDbCustomData", 1, std::string("新扩展")},
                                        {"AcDbCustomData", 40, 2.5}};
        auto id = hex(fresh.add_registered_entity("ACAD_PROXY_ENTITY_WRAPPER", p));
        auto dict = hex(fresh.add_extension_dictionary(id));
        fresh.add_registered_object(dict, "自定义", "ACAD_PROXY_OBJECT_WRAPPER", p);
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "registered graph");
        auto before = save(fresh);
        bool bad = false;
        try {
            p[1].value = std::int32_t(2);
            fresh.add_registered_entity("ACAD_PROXY_ENTITY_WRAPPER", p);
        } catch (const std::invalid_argument &) {
            bad = true;
        }
        check(bad && before == save(fresh), "semantic group-type rollback");
        // Unknown application classes use the same typed-value API. Upstream's
        // reader has a fixed whitelist, so this boundary is verified natively.
        auto custom = DxfDocument::create(binary);
        ClassDefinition klass;
        klass.name = "EXAMPLE_OBJECTCONTEXTDATA_PRIVATE";
        klass.cpp_name = "ExamplePrivateObject";
        custom.set_class(klass);
        p[1].value = 2.5;
        auto custom_object =
            hex(custom.add_registered_object(custom.root_dictionary_handle(), "私有", klass.name, p));
        klass.name = "EXAMPLE_ENTITY";
        klass.cpp_name = "ExampleEntity";
        klass.is_entity = true;
        custom.set_class(klass);
        auto custom_entity = hex(custom.add_registered_entity(klass.name, p));
        std::istringstream custom_input(save(custom));
        auto reread = DxfDocument::read(custom_input);
        int private_objects = 0, private_entities = 0;
        reread.for_each<DxfSemanticObject>([&](auto &v) {
            if (v.handle() == custom_object) {
                check(v.properties().size() == 2, "unknown class property preservation");
                ++private_objects;
            }
        });
        reread.for_each<DxfSemanticEntity>([&](auto &v) {
            if (v.handle() == custom_entity)
                ++private_entities;
        });
        check(private_objects == 1 && private_entities == 1, "exact registered class dispatch");
        check(reread.remove_entity(custom_entity), "remove final class instance");
        std::istringstream empty_input(save(reread));
        auto empty = DxfDocument::read(empty_input);
        for (const auto &c : empty.classes())
            if (c.name == klass.name)
                check(c.instance_count == 0, "retired class count");
        klass.name = "LINE";
        custom.set_class(klass);
        auto protected_before = save(custom);
        bad = false;
        try {
            custom.add_registered_entity("LINE", p);
        } catch (const std::invalid_argument &) {
            bad = true;
        }
        check(bad && save(custom) == protected_before, "typed builtin cannot be replaced by generic record");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
