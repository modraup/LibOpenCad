// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <limits>
namespace {
bool has(const DxfDocument &d, ReferenceStatus status, const std::string &handle) {
    for (const auto &i : d.reference_issues())
        if (i.status == status && i.source_handle == handle)
            return true;
    return false;
}
HatchDefinition pattern() {
    HatchDefinition h;
    h.solid = false;
    h.pattern_name = "SYNC";
    h.pattern_type = 2;
    h.pattern_lines = {{.2, {3, 5}, {0, 10}, {2, -1, 0}}, {1.1, {-2, 8}, {4, 3}, {}}};
    h.paths = {{3, {HatchPolylineEdge{{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}}, true}}, {}}};
    return h;
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format payload prefix");
        const bool binary = std::string(argv[1]) == "binary";
        const std::string prefix = argv[3];
        auto d = DxfDocument::create(binary);
        auto root = d.root_dictionary_handle();
        const auto variables = d.dictionary_entry(root, "AcDbVariableDictionary");
        check(!variables.empty() && d.get_record(variables)->type() == "DICTIONARY" &&
                  d.get_record(variables)->owner_handle() == root,
              "default variable dictionary backlink");
        auto h = pattern();
        h.set_pattern_scale(.2);
        h.set_pattern_angle(1.5707963267948966);
        auto once = h;
        h.set_pattern_scale(.2);
        h.set_pattern_angle(1.5707963267948966);
        check(h.pattern_lines[0].base_point == once.pattern_lines[0].base_point &&
                  h.pattern_lines[0].dash_lengths == once.pattern_lines[0].dash_lengths,
              "idempotent hatch setters");
        check(std::abs(h.pattern_lines[0].base_point.x + 1) < 1e-12 &&
                  std::abs(h.pattern_lines[0].offset.x + 2) < 1e-12,
              "rotated origin and offset");
        d.add_hatch(h);
        h.set_pattern_angle(-.35);
        h.set_pattern_scale(-2);
        d.add_hatch(h);
        h.update_pattern({7, -3}, .4, 1.5); // non-origin pattern update
        d.add_hatch(h);
        h = pattern();
        h.pattern_scale = 0;
        h.set_pattern_scale(3);
        d.add_hatch(h);
        h = pattern();
        h.pattern_scale = std::numeric_limits<double>::epsilon();
        h.set_pattern_scale(.5);
        d.add_hatch(h);
        std::ifstream input(argv[2], std::ios::binary);
        check(bool(input), "source OLE payload fixture");
        OleFrameDefinition ole;
        ole.payload.assign(std::istreambuf_iterator<char>(input), {});
        check(ole.payload.size() > 9000, "noncanonical OLE compound file fixture");
        ole.upper_left = {10, 40, 0};
        ole.lower_right = {70, 5, 0};
        const auto source = hex(d.add_extended_entity(ole));
        d = read(save(d));
        const auto copied = hex(d.copy_entity(source, EntitySpace::Paper));
        d.for_each<DxfExtendedEntity>([&](auto &e) {
            if (e.handle() != copied)
                return;
            auto v = std::get<OleFrameDefinition>(e.definition());
            v.upper_left.x += 5;
            v.lower_right.x += 5;
            e.set_definition(v);
            check(std::get<OleFrameDefinition>(e.definition()).payload == ole.payload,
                  "moving copied OLE preserves all storage bytes");
        });
        const auto a = hex(d.add_line({0, 0, 0}, {10, 0, 0}));
        d.add_line({0, 5, 0}, {10, 5, 0});
        d.for_each<DxfLine>([&](auto &e) {
            if (e.handle() == a) {
                auto copy = e;
                copy.set_end({25, 0, 0});
                e = copy;
            }
        });
        auto reopened = read(save(d));
        reopened.for_each<DxfLine>([&](const auto &e) {
            check(e.end() == (e.handle() == a ? Point3{25, 0, 0} : Point3{10, 5, 0}),
                  "edited copy persisted and neighbor unchanged");
        });
        check(d.reference_issues().empty(), "healthy sync fixture references");
        write(d, prefix + ".dxf");
        // Owner checks apply to OBJECTS, not table headers, and do not repair
        // ambiguous object ownership or delete unnamed children.
        auto audit = DxfDocument::create(binary);
        root = audit.root_dictionary_handle();
        auto dict = hex(audit.add_dictionary(root, "Sync"));
        FieldDefinition f;
        f.evaluator_id = "AcExpr";
        f.code = "1";
        auto field = hex(audit.add_field(dict, "Field", f));
        auto child = hex(audit.add_field_child(field, f));
        auto data = records(save(audit), binary);
        append(data, xrecord("F001", "0"), binary);
        append(data, xrecord("F002", dict), binary);
        auto &parent = at(data, field);
        parent.erase(std::remove_if(parent.begin(), parent.end(),
                                    [&](const auto &p) { return p.first == 360 && p.second == child; }),
                     parent.end());
        audit = read(encode(data, binary));
        check(has(audit, ReferenceStatus::MissingOwner, "F001"), "null object owner");
        check(has(audit, ReferenceStatus::UnlistedChild, "F002"), "dictionary unlisted child");
        check(has(audit, ReferenceStatus::UnlistedChild, child), "field unlisted child");
        write(audit, prefix + "-owners.dxf");
        audit = DxfDocument::create(binary);
        audit.add_symbol(SymbolTable::BlockRecord, "Empty");
        audit.add_symbol(SymbolTable::BlockRecord, "WithAttribute");
        EntityOptions options;
        options.block_name = "WithAttribute";
        AttributeDefinition attr;
        attr.tag = "A";
        attr.text.text = "value";
        attr.text.height = 1;
        audit.add_attribute_definition(attr, options);
        std::string empty;
        audit.for_each<DxfBlock>([&](auto &block) {
            if (block.name() == "Empty" || block.name() == "WithAttribute")
                block.set_flags(block.flags() | 2);
            if (block.name() == "Empty")
                empty = block.handle();
        });
        check(has(audit, ReferenceStatus::StaleAttributeFlag, empty), "stale ATTDEF flag");
        write(audit, prefix + "-flags-before.dxf");
        const auto before_repair = records(save(audit), binary);
        check(audit.repair_attribute_definition_flags() == 1 &&
                  audit.repair_attribute_definition_flags() == 0,
              "deterministic idempotent attribute flag repair");
        check(audit.reference_issues().empty(), "repaired flags and live attribute preserved");
        const auto after_repair = records(save(audit), binary);
        check(before_repair.size() == after_repair.size(), "flag repair retains all records");
        for (std::size_t i = 0; i < before_repair.size(); ++i)
            if (identity(before_repair[i]) != empty)
                check(before_repair[i] == after_repair[i], "flag repair preserves neighboring records");
        write(audit, prefix + "-flags-after.dxf");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
