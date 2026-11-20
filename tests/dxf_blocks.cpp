// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
namespace {
InsertDefinition definition(std::string name) {
    return {std::move(name), {10, 20, 30}, {-2, 3, 4}, {0, 0, 1}, .5, 12, 14, 2, 3};
}
void verify(const DxfDocument &d) {
    check(d.entity_handles_in_block("部件").size() == 2 && d.entity_handles_in_block("assembly").size() == 1,
          "block members");
    int count = 0;
    d.for_each<DxfInsert>([&](auto &e) {
        ++count;
        auto v = e.definition();
        check(v.insertion == Point3{10, 20, 30} && v.scale == Point3{-2, 3, 4} &&
                  std::abs(v.rotation - .5) < 1e-12 && v.columns == 2 && v.rows == 3 &&
                  v.column_spacing == 12 && v.row_spacing == 14,
              "INSERT fields");
    });
    check(count == 3, "nested INSERT typed");
}
template <class F> void failure(DxfDocument &d, F f) {
    auto before = save(d);
    bool fail = false;
    try {
        f();
    } catch (const std::exception &) {
        fail = true;
    }
    check(fail && save(d) == before, "block atomic failure");
}
void unchanged(const Records &before, Records after, const std::set<std::string> &exclude = {}) {
    for (const auto &r : before) {
        auto h = identity(r);
        if (!h.empty() && !exclude.count(h))
            check(at(after, h) == r, "unrelated block records changed");
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto created = DxfDocument::create(binary);
        created.add_symbol(SymbolTable::BlockRecord, "部件");
        created.add_symbol(SymbolTable::BlockRecord, "Assembly");
        created.for_each<DxfBlock>([](auto &b) {
            if (b.name() == "部件" || b.name() == "Assembly") {
                b.set_base_point({1, 2, 3});
                b.set_description("嵌套块");
            }
        });
        EntityOptions o;
        o.block_name = "部件";
        created.add_line({0, 0, 0}, {4, 5, 6}, o);
        created.add_circle({1, 2, 3}, 5, o);
        o.block_name = "assembly";
        created.add_insert(definition("部件"), o);
        created.add_insert(definition("Assembly"));
        o.block_name.clear();
        o.space = EntitySpace::Paper;
        created.add_insert(definition("Assembly"), o);
        verify(created);
        write(created, prefix + "-created.dxf");
        failure(created, [&] { created.add_insert(definition("missing")); });
        failure(created, [&] { created.add_insert(definition("*Model_Space")); });
        failure(created, [&] {
            EntityOptions invalid;
            invalid.block_name = "missing";
            created.add_line({}, {}, invalid);
        });
        failure(created, [&] { created.remove_symbol(SymbolTable::BlockRecord, "部件"); });
        std::ifstream input(argv[2], std::ios::binary);
        auto d = DxfDocument::read(input);
        verify(d);
        check(d.reference_issues().empty(), "initial block graph");
        auto model = d.model_space_entity_handles().front(), paper = d.paper_space_entity_handles().front();
        auto dict = hex(d.add_extension_dictionary(model));
        auto xr = hex(d.add_xrecord(dict, "Data", {{340, model}}));
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        d.for_each<DxfInsert>([&](auto &e) {
            if (e.handle() != model)
                return;
            auto value = e.definition();
            value.insertion = {7, 8, 9};
            value.scale.x = 0;
            value.rotation = -.25;
            value.columns = value.rows = 1;
            e.set_definition(value);
            check(e.definition().scale.x == 1e-12, "INSERT zero scale guard");
        });
        auto edited = records(save(d), binary);
        unchanged(before, edited, {model});
        write(d, prefix + "-edited.dxf");
        auto copy = hex(d.copy_entity(model, EntitySpace::Paper));
        unchanged(edited, records(save(d), binary));
        write(d, prefix + "-copied.dxf");
        check(d.remove_entity(copy) && d.remove_entity(model), "INSERT delete");
        unchanged(before, records(save(d), binary), {model, dict, xr});
        write(d, prefix + "-deleted.dxf");
        d.rename_symbol(SymbolTable::BlockRecord, "部件", "零件");
        check(d.entity_handles_in_block("零件").size() == 2 && d.reference_issues().empty(),
              "block rename graph");
        write(d, prefix + "-renamed.dxf");
        check(d.paper_space_entity_handles() == std::vector<std::string>{paper}, "neighbor INSERT ownership");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
