// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <algorithm>
using namespace libopencad;
static void check(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
template <class Range> static void numbers(std::ostream &out, const char *name, const Range &values) {
    out << name;
    for (auto x : values)
        out << ' ' << double(x);
    out << '\n';
}
static void nums(std::ostream &o, const char *name, std::initializer_list<double> values) {
    numbers(o, name, values);
}
static void operation(std::ostream &o, const char *name, const SolidHistoryNode &n) {
    nums(o, name, {double(n.kind), double(n.node_id())});
}
static void tree(std::ostream &o, const SolidHistoryTree &t) {
    operation(o, "tree", t.operation);
    nums(o, "children", {double(t.operands.size())});
    for (const auto &c : t.operands)
        tree(o, c);
}
static void trace(std::ostream &out, const DxfDocument &d) {
    out << std::setprecision(17);
    d.for_each<DxfModelerEntity>([&](const auto &e) {
        auto h = std::stoull(e.handle(), nullptr, 16);
        nums(out, "entity", {double(h)});
        auto g = d.solid_history_graph(h);
        if (!g) {
            out << "graph none\n";
            return;
        }
        auto r = *d.solid_history_root(g->root);
        nums(out, "root",
             {double(r.handle), double(r.owner), double(r.evaluation_graph), double(r.major), double(r.minor),
              double(r.history_node_id), double(r.show_history), double(r.record_history)});
        numbers(out, "nodes", g->nodes);
        nums(out, "evaluation", {double(g->evaluation_graph.value_or(0))});
        for (auto id : g->nodes) {
            auto n = *d.solid_history_node(id);
            nums(out, "node",
                 {double(n.handle), double(n.owner), double(n.kind), double(n.major), double(n.minor),
                  double(n.step_id), double(n.eval_node_id), double(n.eval_major), double(n.eval_minor),
                  double(n.parent_id), double(n.value_code), double(n.material)});
            numbers(out, "matrix", n.transform);
            nums(out, "color",
                 {n.color.to_true_color_value() ? double(*n.color.to_true_color_value()) : -1.,
                  n.color.index() ? double(*n.color.index()) : -1.});
            out << "value " << n.value.index();
            std::visit(
                [&](const auto &v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, Vector2>)
                        out << ' ' << v.x << ' ' << v.y;
                    else if constexpr (!std::is_same_v<T, std::monostate>)
                        out << ' ' << v;
                },
                n.value);
            out << '\n';
            if (n.kind == SolidHistoryKind::Boolean)
                nums(out, "boolean",
                     {double(n.boolean_operation), double(n.first_operand), double(n.second_operand)});
        }
        if (auto op = d.solid_history_operation(h))
            operation(out, "active", *op);
        else
            out << "active none\n";
        if (auto chain = d.solid_history_operations(h)) {
            nums(out, "chain", {double(chain->size())});
            for (const auto &n : *chain)
                operation(out, "step", n);
        } else
            out << "chain none\n";
        if (auto t = d.solid_history_tree(h))
            tree(out, *t);
        else
            out << "tree none\n";
        std::vector<std::pair<std::uint64_t, std::uint64_t>> refs;
        for (auto ref : d.solid_history_references(h))
            if (ref.source != h && ref.target)
                refs.emplace_back(ref.source, ref.target);
        std::sort(refs.begin(), refs.end());
        for (auto [a, b] : refs)
            nums(out, "reference", {double(a), double(b)});
    });
}
static void save(const DxfDocument &d, const std::string &prefix) {
    for (bool binary : {false, true}) {
        std::ofstream o(prefix + (binary ? ".binary.dxf" : ".ascii.dxf"), std::ios::binary);
        d.write(o, binary);
    }
}
static void boundaries(const DxfDocument &source, std::uint64_t entity) {
    check(!source.solid_history_graph(UINT64_MAX) && !source.solid_history_node(UINT64_MAX),
          "unknown history queries");
    auto d = source;
    auto before = bytes(d);
    auto h = *d.solid_history_graph(entity);
    auto graph = *d.evaluation_graph(*h.evaluation_graph);
    for (int k = 0; k < 6; ++k) {
        bool rejected = false;
        try {
            if (k == 0)
                d.set_solid_history_active_node(entity, INT32_MAX);
            if (k == 1)
                d.set_solid_history_node_material(h.nodes[0], entity);
            if (k >= 2) {
                auto bad = graph;
                if (k == 2)
                    bad.nodes[0].expression = UINT64_MAX;
                if (k == 3)
                    bad.nodes.push_back(bad.nodes[0]);
                if (k == 4)
                    bad.nodes[0].node_data[0] = 999999;
                if (k == 5) {
                    auto &e = bad.edges[0];
                    e.source_node = e.destination_node;
                }
                d.set_solid_history_evaluation_graph(entity, bad);
            }
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && bytes(d) == before, "history mutation must reject atomically");
    }
    // The nonstandard code-91 handle form is not safe for cross-encoding mutation.
    auto limited = source;
    bool found = false;
    limited.for_each<DxfSemanticObject>([&](auto &r) {
        if (std::stoull(r.handle(), nullptr, 16) != h.nodes[0])
            return;
        auto p = r.properties();
        p.erase(std::remove_if(p.begin(), p.end(),
                               [](const auto &x) {
                                   return x.subclass == "AcDbEvalExpr" && (x.code == 70 || x.code == 91);
                               }),
                p.end());
        p.push_back({"AcDbEvalExpr", 70, std::int16_t(91)});
        p.push_back({"AcDbEvalExpr", 91, std::int32_t(1234)});
        r.set_properties(p);
        found = true;
    });
    check(found, "history node typed storage");
    before = bytes(limited);
    bool rejected = false;
    try {
        limited.copy_entity(entity);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected && bytes(limited) == before, "nonstandard handle mutation must be atomic");
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream input(argv[1], std::ios::binary);
    auto source = DxfDocument::read(input);
    std::string prefix = argv[2];
    std::ofstream out(prefix + ".trace");
    trace(out, source);
    out.close();
    if (std::string(argv[1]).find("bad-foreign") != std::string::npos) {
        std::uint64_t entity = 0;
        source.for_each<DxfModelerEntity>([&](const auto &e) {
            if (!entity && e.kind() == ModelerKind::Solid3D)
                entity = std::stoull(e.handle(), nullptr, 16);
        });
        for (bool removing : {false, true}) {
            auto doc = source;
            auto before = bytes(doc);
            bool rejected = false;
            try {
                if (removing)
                    doc.remove_entity(entity);
                else
                    doc.copy_entity(entity);
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            check(rejected && bytes(doc) == before, "foreign history mutation must be atomic");
        }
    }
    if (std::string(argv[1]).find("bad-") != std::string::npos)
        return 0;
    std::uint64_t entity = 0, material = 0, surface = 0;
    source.for_each<DxfModelerEntity>([&](const auto &e) {
        if (!entity && e.kind() == ModelerKind::Solid3D)
            entity = std::stoull(e.handle(), nullptr, 16);
        if (e.kind() == ModelerKind::PlaneSurface)
            surface = std::stoull(e.handle(), nullptr, 16);
    });
    source.for_each<DxfMaterial>([&](const auto &m) {
        if (m.definition().name == "替换材质")
            material = std::stoull(m.handle(), nullptr, 16);
    });
    check(entity && material && surface, "history fixture identities");
    boundaries(source, entity);
    auto d = source;
    auto copy = d.copy_entity(entity, EntitySpace::Paper);
    auto graph = *d.solid_history_graph(copy);
    for (auto n : graph.nodes)
        d.set_solid_history_node_material(n, material);
    save(d, prefix + ".copy");
    check(d.remove_entity(copy), "remove history copy");
    save(d, prefix + ".deleted");
    auto surface_doc = source;
    auto surface_copy = surface_doc.copy_entity(surface, EntitySpace::Paper);
    check(surface_doc.solid_history_graph(surface_copy)->nodes.size() == 15,
          "surface fallback history remapped");
    save(surface_doc, prefix + ".surface-copy");
    check(surface_doc.remove_entity(surface_copy), "remove surface history copy");
    save(surface_doc, prefix + ".surface-deleted");
    auto original = *source.solid_history_graph(entity);
    auto earliest = source.solid_history_operations(entity)->front().node_id();
    auto active = source;
    active.set_solid_history_active_node(entity, earliest);
    save(active, prefix + ".active");
    auto reordered = source;
    auto eg = *source.evaluation_graph(*original.evaluation_graph);
    std::reverse(eg.nodes.begin(), eg.nodes.end());
    std::reverse(eg.edges.begin(), eg.edges.end());
    reordered.set_solid_history_evaluation_graph(entity, eg);
    save(reordered, prefix + ".reordered");
    std::ofstream manifest(prefix + ".manifest");
    manifest << std::hex << entity << ' ' << copy << ' ' << material << ' ' << earliest << ' ' << surface
             << ' ' << surface_copy << '\n';
    std::cout << "Native history graph/reference suite passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
