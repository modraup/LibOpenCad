// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <set>
using namespace libopencad;
static std::string hex(std::uint64_t h) {
    std::ostringstream s;
    s << std::hex << std::uppercase << h;
    return s.str();
}
static void check(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream s;
    d.write(s);
    return s.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream f(argv[1], std::ios::binary);
    auto original = DxfDocument::read(f);
    auto source = original;
    std::vector<std::string> candidates;
    source.for_each<DxfEntity>([&](const auto &e) {
        if (e.layer() != "Control" && e.type() != "INSERT" && e.type() != "ATTDEF")
            candidates.push_back(e.handle());
    });
    check(!candidates.empty(), "No candidates");
    source.add_symbol(SymbolTable::BlockRecord, "ExpandedSpecialized");
    source.for_each<DxfBlock>([&](auto &b) {
        if (b.name() == "ExpandedSpecialized")
            b.set_base_point({1, 2, 3});
    });
    EntityOptions target;
    target.block_name = "ExpandedSpecialized";
    std::ofstream origins(std::string(argv[2]) + ".origins");
    for (const auto &h : candidates) {
        source.copy_entity(h, target);
        origins << h << '\n';
    }
    origins.close();
    InsertDefinition ins;
    ins.block_name = "ExpandedSpecialized";
    ins.insertion = {10, -20, 30};
    ins.scale = {2, 2, 2};
    ins.rotation = 0.5;
    auto root = source.add_insert(ins);
    auto before = bytes(source);
    std::ofstream(std::string(argv[2]) + ".source.dxf", std::ios::binary) << before;
    EntityOptions paper;
    paper.space = EntitySpace::Paper;
    auto ids = source.expand_insert_into(hex(root), paper);
    check(ids.size() == candidates.size(), "Expanded member count");
    std::ofstream trace(std::string(argv[2]) + ".parts");
    trace << hex(root) << '\n';
    for (auto id : ids)
        trace << hex(id) << '\n';
    std::ofstream(std::string(argv[2]) + ".dxf", std::ios::binary) << bytes(source);
    std::ifstream reopen(std::string(argv[2]) + ".dxf", std::ios::binary);
    auto roundtrip = DxfDocument::read(reopen);
    std::set<std::string> paper_ids;
    for (auto h : roundtrip.paper_space_entity_handles())
        paper_ids.insert(h);
    for (auto id : ids)
        check(paper_ids.count(hex(id)), "Expanded space membership");
    // Unsupported raw members and resource limits must reject the whole operation.
    auto limit = original;
    limit.add_symbol(SymbolTable::BlockRecord, "Overflow");
    target.block_name = "Overflow";
    for (const auto &h : candidates)
        limit.copy_entity(h, target);
    ins.block_name = "Overflow";
    auto limited_root = limit.add_insert(ins);
    auto unchanged = bytes(limit);
    bool refused = false;
    try {
        limit.expand_insert_into(hex(limited_root), paper, false, false, 0);
    } catch (const std::exception &) {
        refused = true;
    }
    check(refused && bytes(limit) == unchanged, "Limit rollback");
    auto shear = original;
    shear.add_symbol(SymbolTable::BlockRecord, "OwnedLeaf");
    shear.add_symbol(SymbolTable::BlockRecord, "ShearedParent");
    target.block_name = "OwnedLeaf";
    const std::set<std::string> specialized = {
        "LEADER",      "TOLERANCE", "MLINE",    "MULTILEADER", "DIMENSION", "HATCH", "SHAPE",
        "PDFUNDERLAY", "IMAGE",     "VIEWPORT", "ACAD_TABLE",  "OLE2FRAME", "LIGHT"};
    auto selected = std::find_if(candidates.begin(), candidates.end(), [&](const auto &h) {
        return specialized.count(shear.get_record(h)->type());
    });
    check(selected != candidates.end(), "Missing specialized rollback target");
    auto leaf = shear.copy_entity(*selected, target);
    try {
        shear.add_extension_dictionary(hex(leaf));
    } catch (const std::invalid_argument &e) {
        check(std::string(e.what()) == "Extension dictionary already exists",
              "Unexpected dictionary refusal");
    }
    InsertDefinition nested;
    nested.block_name = "OwnedLeaf";
    nested.rotation = 0.5;
    target.block_name = "ShearedParent";
    shear.add_insert(nested, target);
    InsertDefinition parent;
    parent.block_name = "ShearedParent";
    parent.scale = {2, 1, 1};
    auto parent_id = shear.add_insert(parent);
    unchanged = bytes(shear);
    refused = false;
    try {
        shear.expand_insert_into(hex(parent_id), paper);
    } catch (const std::invalid_argument &e) {
        refused = std::string(e.what()) == "Owned specialized shear expansion is unsupported";
    }
    check(refused && bytes(shear) == unchanged, "Owned specialized shear rollback");
    std::cout << ids.size() << " specialized block members expanded\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
