// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
using namespace libopencad;
static void check(bool x, const char *m) {
    if (!x)
        throw std::runtime_error(m);
}
static std::string hex(std::uint64_t v) {
    std::ostringstream o;
    o << std::hex << v;
    return o.str();
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static void save(const DxfDocument &d, const std::string &p) {
    std::ofstream o(p, std::ios::binary);
    d.write(o);
    o.close();
    check(bool(o), "Write failed");
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    auto d = DxfDocument::create(std::string(argv[1]) == "binary");
    auto prefix = std::string(argv[2]);
    d.add_layer("中文图层");
    d.add_symbol(SymbolTable::BlockRecord, "Leaf");
    d.add_symbol(SymbolTable::BlockRecord, "Assembly");
    d.for_each<DxfBlock>([](auto &b) {
        if (b.name() == "Leaf")
            b.set_base_point({2, -1, 3});
        if (b.name() == "Assembly")
            b.set_base_point({-1, 2, 1});
    });
    EntityOptions member;
    member.block_name = "Leaf";
    member.color = Color::by_block();
    member.linetype = "ByBlock";
    member.line_weight = LineWeight::by_block();
    auto leaf_line = d.add_line({1, 2, 3}, {4, 5, 6}, member);
    auto leaf_dictionary = d.add_extension_dictionary(hex(leaf_line));
    d.add_xrecord(hex(leaf_dictionary), "Leaf metadata", {{1, "Leaf 中文"}, {330, hex(leaf_line)}});
    d.add_circle({2, 3, 4}, 3, member);
    d.add_arc({4, 3, 2}, 2, .3, 5.2, member);
    d.add_lwpolyline({{0, 0, .5, .1, .2, 0}, {3, 0, 0, .2, .3, 1}, {3, 4, -.3, .3, .4, 2}}, true, member);
    d.add_text("中文", {1, 2, 0}, 2, member);
    MTextDefinition mt;
    mt.text = "多行\\P文字";
    mt.insertion = {2, 4, 1};
    mt.rotation = .4;
    d.add_mtext(mt, member);
    EntityOptions assembly = member;
    assembly.block_name = "Assembly";
    InsertDefinition nested;
    nested.block_name = "Leaf";
    nested.insertion = {7, 8, 9};
    nested.rotation = .7;
    d.add_insert(nested, assembly);
    auto line = d.add_line({-1, 0, 1}, {5, 4, 3}, assembly);
    auto dictionary = d.add_extension_dictionary(hex(line));
    d.add_xrecord(hex(dictionary), "Metadata", {{1, "中文数据"}, {40, "2.5"}, {330, hex(line)}});
    EntityOptions root_options;
    root_options.layer = "中文图层";
    root_options.color = Color::from_rgb(10, 20, 30);
    root_options.line_weight = LineWeight::from_value(35);
    root_options.linetype = "Continuous";
    InsertDefinition root;
    root.block_name = "Assembly";
    root.insertion = {10, 20, 30};
    auto h = hex(d.add_insert(root, root_options));
    EntityOptions target;
    target.space = EntitySpace::Paper;
    for (int step = 0; step < 4; ++step) {
        auto source = d;
        source.for_each<DxfInsert>([&](auto &i) {
            if (i.handle() != h)
                return;
            auto v = i.definition();
            v.rotation = .3;
            v.scale = step == 0 || step == 3 ? Vector3{2, 2, 2} : Vector3{-2, 3, 4};
            if (step == 2)
                v.normal = {1, 2, 3};
            if (step == 3) {
                v.columns = 2;
                v.rows = 2;
                v.column_spacing = -15;
                v.row_spacing = 17;
            }
            i.set_definition(v);
        });
        auto before = bytes(source);
        auto path = prefix + "." + std::to_string(step);
        save(source, path + ".source.dxf");
        if (step == 3)
            check(source.expand_insert_into(h, target).empty() && bytes(source) == before,
                  "MINSERT no-opt-in modified source");
        auto edited = source;
        auto added = edited.expand_insert_into(h, target, false, step == 3);
        check(!added.empty(), "Missing expansion outputs");
        check(edited.paper_space_entity_handles().size() == added.size(), "Incorrect output ownership");
        check(edited.duplicate_handles().empty(), "Duplicate expansion handles");
        std::ofstream ids(path + ".parts");
        ids << h << '\n';
        for (auto id : added)
            ids << hex(id) << '\n';
        ids.close();
        save(edited, path + ".dxf");
        auto erased = source;
        auto replaced = erased.expand_insert_into(h, target, true, step == 3);
        check(!erased.get_record(h) && replaced.size() == added.size(), "Source erasure changed parts");
        check(source.get_record(h) != nullptr, "Expansion changed source");
        for (int bad = 0; bad < 2; ++bad) {
            auto failed = source;
            auto invalid = target;
            if (bad)
                invalid.block_name = "Missing";
            bool refused = false;
            try {
                failed.expand_insert_into(h, invalid, true, step == 3, bad ? 1000000 : 1);
            } catch (const std::exception &) {
                refused = true;
            }
            check(refused && bytes(failed) == before, "Expansion failure was not atomic");
        }
    }
    for (int bad = 0; bad < 3; ++bad) {
        auto ineligible = d;
        if (bad == 0)
            ineligible.for_each<DxfBlock>([](auto &b) {
                if (b.name() == "Assembly")
                    b.set_flags(b.flags() | 4);
            });
        if (bad == 1)
            ineligible.set_symbol_properties(SymbolTable::BlockRecord, "Assembly", {{280, "0"}});
        if (bad == 2) {
            EntityOptions o;
            o.block_name = "Assembly";
            InsertDefinition i;
            i.block_name = "Leaf";
            i.rows = 2;
            ineligible.add_insert(i, o);
        }
        auto before = bytes(ineligible);
        bool refused = false;
        try {
            ineligible.expand_insert_into(h, target);
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused && bytes(ineligible) == before, "Ineligible expansion changed source");
    }
    auto cyclic = d;
    EntityOptions leaf;
    leaf.block_name = "Leaf";
    InsertDefinition self;
    self.block_name = "Assembly";
    cyclic.add_insert(self, leaf);
    auto snapshot = bytes(cyclic);
    bool refused = false;
    try {
        cyclic.expand_insert_into(h, target);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused && bytes(cyclic) == snapshot, "Block cycle did not refuse atomically");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
