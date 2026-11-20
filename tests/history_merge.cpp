// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
#include <map>
using namespace libopencad;
static std::string ascii(const DxfDocument &d) {
    std::ostringstream out;
    d.write(out, false);
    return out.str();
}
static void output(const DxfDocument &d, const std::string &prefix) {
    for (bool b : {false, true}) {
        std::ofstream out(prefix + (b ? ".binary.dxf" : ".ascii.dxf"), std::ios::binary);
        d.write(out, b);
    }
}
static void field(Records &data, std::uint64_t h, const std::string &subclass, int code, std::string value) {
    auto &r = at(data, hex(h));
    std::string scope;
    unsigned depth = 0;
    for (auto &p : r) {
        if (p.first == 102) {
            if (p.second == "}") {
                if (depth)
                    --depth;
            } else if (!p.second.empty() && p.second.front() == '{')
                ++depth;
            continue;
        }
        if (depth)
            continue;
        if (p.first == 100)
            scope = p.second;
        else if (scope == subclass && p.first == code) {
            p.second = value;
            return;
        }
    }
    throw std::runtime_error("missing patch field");
}
static void reject(DxfDocument d, std::uint64_t a, std::uint64_t b) {
    auto before = save(d);
    bool caught = false;
    try {
        d.merge_solid_history_boolean(a, b, 2);
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught && save(d) == before, "failed merge must leave document unchanged");
}
static void merge(DxfDocument &d, std::uint64_t a, std::uint64_t b, std::uint8_t op) {
    auto ga = *d.solid_history_graph(a), gb = *d.solid_history_graph(b);
    auto active_a = *d.solid_history_operation(a), active_b = *d.solid_history_operation(b);
    std::int32_t offset = 0;
    for (auto h : ga.nodes) {
        auto n = *d.solid_history_node(h);
        offset = std::max({offset, n.step_id, n.eval_node_id});
    }
    std::map<std::uint64_t, SolidHistoryNode> old;
    for (auto h : gb.nodes)
        old.emplace(h, *d.solid_history_node(h));
    auto entity_a = d.modeler_geometry(a), entity_b = d.modeler_geometry(b);
    auto merged = d.merge_solid_history_boolean(a, b, op);
    check(merged.root == ga.root && merged.evaluation_graph == ga.evaluation_graph,
          "target root/evaluation retained");
    check(merged.nodes.size() == ga.nodes.size() + gb.nodes.size() + 1, "merged operation count");
    check(!d.solid_history_graph(b) && !d.solid_history_root(gb.root) &&
              !d.evaluation_graph(*gb.evaluation_graph),
          "tool history detached and retired");
    for (auto [h, n] : old) {
        auto now = *d.solid_history_node(h);
        check(now.owner == *ga.evaluation_graph && now.material == n.material, "moved owner/shared material");
        check(now.step_id == (n.step_id > 0 ? n.step_id + offset : n.step_id) &&
                  now.eval_node_id == (n.eval_node_id > 0 ? n.eval_node_id + offset : n.eval_node_id),
              "operation IDs offset");
        if (n.kind == SolidHistoryKind::Boolean)
            check(now.first_operand == n.first_operand + offset &&
                      now.second_operand == n.second_operand + offset,
                  "nested Boolean operands offset");
    }
    auto tree = *d.solid_history_tree(a);
    check(tree.operands.size() == 2 && tree.operation.boolean_operation == op, "Boolean operation");
    check(tree.operands[0].operation.handle == active_a.handle &&
              tree.operands[1].operation.handle == active_b.handle,
          "noncommutative operand order");
    check(d.modeler_geometry(a)->to_sat_string() == entity_a->to_sat_string() &&
              d.modeler_geometry(b)->to_sat_string() == entity_b->to_sat_string(),
          "stored ACIS geometry unchanged");
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream input(argv[1], std::ios::binary);
    auto source = DxfDocument::read(input);
    std::string prefix = argv[2];
    std::uint64_t main = 0, neighbor = 0, surface = 0;
    source.for_each<DxfModelerEntity>([&](const auto &e) {
        auto h = std::stoull(e.handle(), nullptr, 16);
        if (e.kind() == ModelerKind::Solid3D) {
            if (!main)
                main = h;
            else
                neighbor = h;
        } else if (e.kind() == ModelerKind::PlaneSurface)
            surface = h;
    });
    check(main && neighbor && surface, "fixture entities");
    auto private_node = source.solid_history_graph(main)->nodes.front();
    auto dict = source.add_extension_dictionary(hex(private_node));
    auto xr = source.add_xrecord(hex(dict), "MERGE_KEEP", {{1, "history payload"}, {330, hex(private_node)}});
    output(source, prefix + ".prepared");
    std::ofstream manifest(prefix + ".manifest");
    manifest << hex(main) << ' ' << hex(neighbor) << ' ' << hex(surface) << ' ' << hex(private_node) << ' '
             << hex(dict) << ' ' << hex(xr) << '\n';
    manifest.close();
    struct Case {
        const char *name;
        std::uint64_t a, b;
        std::uint8_t op;
    };
    for (auto c : {Case{"union", main, neighbor, 0}, Case{"intersect", neighbor, main, 1},
                   Case{"subtract", main, neighbor, 2}, Case{"surface-tool", main, surface, 2},
                   Case{"surface-target", surface, main, 2}}) {
        auto d = source;
        merge(d, c.a, c.b, c.op);
        check(d.extension_dictionary_handle(hex(private_node)) == hex(dict), "node dictionary retained");
        output(d, prefix + "." + c.name);
    }
    auto repeated = source;
    merge(repeated, main, neighbor, 0);
    merge(repeated, main, surface, 2);
    output(repeated, prefix + ".repeat");
    auto legacy_raw = records(ascii(source), false);
    auto legacy_graph = *source.solid_history_graph(neighbor);
    drop(legacy_raw, {hex(*legacy_graph.evaluation_graph)});
    field(legacy_raw, legacy_graph.root, "AcDbShHistory", 360, "0");
    field(legacy_raw, legacy_graph.nodes[0], "", 330, hex(legacy_graph.root));
    auto legacy = read(encode(legacy_raw, false));
    if (source.is_binary()) {
        std::ostringstream b;
        legacy.write(b, true);
        legacy = read(b.str());
    }
    output(legacy, prefix + ".legacy-before");
    legacy.merge_solid_history_boolean(main, neighbor, 2);
    check(legacy.solid_history_tree(main)->operands.size() == 2, "graphless tool promoted");
    output(legacy, prefix + ".legacy");
    check(read(ascii(legacy)).version() == source.version(),
          "HEADER survives HANDSEED/evaluation handle collision");
    auto legacy_target = read(encode(legacy_raw, false));
    if (source.is_binary()) {
        std::ostringstream b;
        legacy_target.write(b, true);
        legacy_target = read(b.str());
    }
    legacy_target.merge_solid_history_boolean(neighbor, main, 2);
    check(read(ascii(legacy_target)).version() == source.version(), "promoted target HEADER retained");
    output(legacy_target, prefix + ".legacy-target");
    // Rejections include late failures after staging ownership/ID changes.
    reject(source, main, main);
    reject(source, main, 0xFFFFFFFF);
    auto raw = records(ascii(source), false);
    auto ga = *source.solid_history_graph(main), gb = *source.solid_history_graph(neighbor);
    auto bad = raw;
    field(bad, ga.root, "AcDbShHistory", 92,
          std::to_string(source.solid_history_node(ga.nodes.back())->node_id()));
    reject(read(encode(bad, false)), main, neighbor);
    bad = raw;
    field(bad, ga.nodes.front(), "AcDbShHistoryNode", 92, std::to_string(INT32_MAX));
    reject(read(encode(bad, false)), main, neighbor);
    bad = raw;
    field(bad, ga.nodes[1], "AcDbEvalExpr", 90,
          std::to_string(source.solid_history_node(ga.nodes[0])->node_id()));
    reject(read(encode(bad, false)), main, neighbor);
    bad = raw;
    field(bad, *gb.evaluation_graph, "", 330, hex(ga.root));
    reject(read(encode(bad, false)), main, neighbor);
    auto id_overflow = source;
    auto large = *id_overflow.evaluation_graph(*gb.evaluation_graph);
    large.nodes[0].id = INT32_MAX;
    large.nodes[0].next_id = INT32_MAX;
    id_overflow.set_solid_history_evaluation_graph(neighbor, large);
    reject(id_overflow, main, neighbor);
    // Patch the stored step only; an active evaluation ID still selects the node.
    bad = raw;
    field(bad, gb.nodes.front(), "AcDbShHistoryNode", 92, std::to_string(INT32_MAX));
    reject(read(encode(bad, false)), main, neighbor);
    auto external = source;
    auto external_dict = std::stoull(external.extension_dictionary_handle(hex(surface)), nullptr, 16);
    external.add_xrecord(hex(external_dict), "PROTECT", {{340, hex(gb.root)}});
    reject(external, main, neighbor);
    for (auto h : {*gb.evaluation_graph, gb.nodes.front()}) {
        bad = raw;
        auto &record = at(bad, hex(h));
        auto at_scope =
            std::find_if(record.begin(), record.end(), [](const auto &p) { return p.first == 100; });
        record.insert(at_scope + 1, {{102, "{PRIVATE_HISTORY"}, {90, "123"}, {102, "}"}});
        reject(read(encode(bad, false)), main, neighbor);
    }
    auto owned = source;
    owned.add_extension_dictionary(hex(*gb.evaluation_graph));
    reject(owned, main, neighbor);
    bad = raw;
    inject(bad, hex(surface), {hex(gb.root), hex(*gb.evaluation_graph), hex(private_node)});
    auto reactors = read(encode(bad, false));
    reactors.merge_solid_history_boolean(main, neighbor, 2);
    auto after_reactors = records(ascii(reactors), false);
    auto refs = at(after_reactors, hex(surface));
    unsigned count = 0;
    for (auto &p : refs)
        if (p.first == 330 && p.second == hex(private_node))
            ++count;
    check(count == 1, "unrelated reactor retained");
    std::cout << "History Boolean merge verified\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
