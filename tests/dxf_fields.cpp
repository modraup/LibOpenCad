// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
namespace {
FieldDefinition field(const DxfDocument &d, std::string_view h) {
    FieldDefinition v;
    d.for_each<DxfField>([&](const auto &f) {
        if (f.handle() == h)
            v = f.definition();
    });
    return v;
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        std::string prefix = argv[3];
        auto e = d.model_space_entity_handles()[0];
        auto dict = d.extension_dictionary_handle(e);
        auto h = d.dictionary_entry(dict, "字段");
        auto v = field(d, h);
        check(v.children.size() == 1 && v.child_values.size() == 3 && v.child_values[1].value.point.z == 3 &&
                  v.child_values[2].value.number == 0,
              "field scope/defaults");
        d.for_each<DxfField>([](auto &f) { f.set_definition(f.definition()); });
        d.for_each<DxfFieldList>([](auto &f) { f.set_definition(f.definition()); });
        write(d, prefix + "-roundtrip.dxf");
        v.code = "修改代码";
        v.child_values[0].value.number = 42;
        v.value = {};
        v.value.type = 2;
        v.value.number = 12.5;
        v.value.formatted = "12.5";
        d.set_field_definition(h, v);
        write(d, prefix + "-edited.dxf");
        auto copy = hex(d.copy_entity(e, EntitySpace::Paper));
        auto fh = d.dictionary_entry(d.extension_dictionary_handle(copy), "字段");
        check(field(d, fh).references[0] == std::stoull(copy, nullptr, 16), "FIELD self reference remapped");
        write(d, prefix + "-copied.dxf");
        check(d.remove_entity(copy) && !d.get_record(fh), "FIELD owned cascade");
        write(d, prefix + "-deleted.dxf");
        auto saved = save(d);
        bool rejected = false;
        try {
            auto bad = v;
            bad.children.clear();
            d.set_field_definition(h, bad);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && save(d) == saved, "reject child orphan");
        rejected = false;
        try {
            auto bad = v;
            bad.references = {0xfffffff};
            d.set_field_definition(h, bad);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && save(d) == saved, "reject missing reference");
        auto fresh = DxfDocument::create(binary);
        auto entity = hex(fresh.add_line({0, 0, 0}, {1, 0, 0}));
        auto directory = hex(fresh.add_extension_dictionary(entity));
        FieldDefinition f;
        f.evaluator_id = "AcVar";
        f.value.type = 4;
        f.value.text = "新字段";
        auto parent = hex(fresh.add_field(directory, "字段", f));
        f.value = {};
        f.value.type = 1;
        f.value.number = 7;
        auto child = fresh.add_field_child(parent, f);
        FieldListDefinition list;
        list.fields = {std::stoull(parent, nullptr, 16), child};
        fresh.add_field_list(directory, "列表", list);
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "fresh FIELD graph");
        auto private_doc = d;
        auto p = field(private_doc, h);
        p.value.extra_fields = {{102, "{PRIVATE"}, {90, "999"}, {102, "}"}};
        private_doc.set_field_definition(h, p);
        check(field(read(save(private_doc)), h).value.type == 2, "private value codes not interpreted");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
