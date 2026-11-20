// SPDX-License-Identifier: MPL-2.0
// acadrust document.rs solid_history_* and objects/dynamic_block.rs handle visits.
#include <libopencad/dxf_document.hpp>
#include "dxf_binary.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <limits>
#include <cctype>
#include <set>
#include <sstream>
namespace libopencad {
namespace {
std::string hex(std::uint64_t n) {
    std::ostringstream s;
    s << std::hex << std::uppercase << n;
    return s.str();
}
std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    return a == s.npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::uint64_t handle(std::string s) {
    s = trim(s);
    if (s.empty())
        return 0;
    if (s.find_first_not_of("0123456789abcdefABCDEF") != s.npos)
        throw std::invalid_argument("Invalid history handle");
    return std::stoull(s, nullptr, 16);
}
std::int32_t integer(std::string s) {
    s = trim(s);
    std::size_t used = 0;
    auto n = std::stoll(s, &used);
    if (used != s.size() || n < INT32_MIN || n > INT32_MAX)
        throw std::invalid_argument("Invalid history integer");
    return std::int32_t(n);
}
double real(const std::string &s) {
    std::istringstream in(s);
    in.imbue(std::locale::classic());
    double n;
    if (!(in >> n) || !(in >> std::ws).eof() || !std::isfinite(n))
        throw std::invalid_argument("Invalid history real");
    return n;
}
struct Fields {
    std::vector<DxfRawField> values;
    std::string text(int code, std::string fallback = "0") const {
        for (auto i = values.rbegin(); i != values.rend(); ++i)
            if (i->code == code)
                return i->value;
        return fallback;
    }
    std::vector<std::string> all(int code) const {
        std::vector<std::string> v;
        for (const auto &x : values)
            if (x.code == code)
                v.push_back(x.value);
        return v;
    }
    std::int32_t i(int code, std::int32_t fallback = 0) const {
        return integer(text(code, std::to_string(fallback)));
    }
    double d(int code) const { return real(text(code)); }
    std::uint64_t h(int code) const { return handle(text(code)); }
};
std::optional<SolidHistoryKind> kind(std::string_view type) {
    const char *names[] = {"BOX",     "WEDGE", "SPHERE",    "CYLINDER", "CONE",
                           "PYRAMID", "TORUS", "BOOLEAN",   "BREP",     "FILLET",
                           "CHAMFER", "SWEEP", "EXTRUSION", "LOFT",     "REVOLVE"};
    for (std::size_t i = 0; i < 15; ++i)
        if (type == std::string("ACSH_") + names[i] + "_CLASS")
            return static_cast<SolidHistoryKind>(i);
    return {};
}
} // namespace
class SolidHistoryReader {
  public:
    const DxfDocument &doc;
    explicit SolidHistoryReader(const DxfDocument &d) : doc(d) {}
    const DxfRecord *record(std::uint64_t id) const {
        if (!id)
            return nullptr;
        auto f = doc.handle_index_.find(id);
        if (f == doc.handle_index_.end())
            return nullptr;
        if (f->second.size() != 1)
            throw std::invalid_argument("Ambiguous history identity");
        return &doc.record_at(f->second.front());
    }
    static Fields fields(const DxfRecord &r, std::string_view scope) {
        Fields f;
        bool active = false;
        unsigned depth = 0;
        for (const auto &p : r.pairs_) {
            if (p.code == 102) {
                if (p.value == "}") {
                    if (depth)
                        --depth;
                } else if (!p.value.empty() && p.value.front() == '{')
                    ++depth;
                continue;
            }
            if (depth)
                continue;
            if (p.code == 1001 || p.code == 101)
                break;
            if (p.code == 100) {
                active = p.value == scope;
                continue;
            }
            if (active)
                f.values.push_back(
                    {p.code, detail::is_string_value(p.code)
                                 ? r.decode_string(p.value)
                                 : (r.binary_ ? detail::text_value_from_binary(p.code, p.value) : p.value)});
        }
        return f;
    }
    static DxfRecord &mutable_record(DxfDocument &doc, std::uint64_t id) {
        auto f = doc.handle_index_.find(id);
        if (f == doc.handle_index_.end() || f->second.size() != 1 || f->second[0].vertex ||
            f->second[0].seqend)
            throw std::invalid_argument("History edit requires a unique object");
        return std::visit([](DxfRecord &r) -> DxfRecord & { return r; }, doc.records_[f->second[0].record]);
    }
    bool reaches(std::uint64_t start, std::uint64_t target) const {
        auto cur = start;
        for (unsigned n = 0; n < 16; ++n) {
            if (cur == target)
                return true;
            auto r = record(cur);
            if (!r)
                break;
            auto next = handle(r->owner_handle());
            if (!next || next == cur)
                break;
            cur = next;
        }
        return cur == target;
    }
    std::uint64_t entity_root(std::uint64_t id) const {
        auto r = record(id);
        if (!r)
            return 0;
        auto t = r->type();
        if (t != "3DSOLID" && t != "REGION" && t != "BODY" && t.find("SURFACE") == std::string::npos)
            return 0;
        // Native solid links take precedence over surface fallback metadata.
        auto root = fields(*r, "AcDb3dSolid").h(350);
        if (root)
            return root;
        if (t == "3DSOLID" || t == "REGION" || t == "BODY")
            return 0;
        auto dictionary = doc.extension_dictionary_handle(r->handle());
        if (dictionary.empty())
            return 0;
        auto entry = doc.dictionary_entry(dictionary, "CADCODEC_SURFACE_HISTORY_V1");
        if (entry.empty())
            return 0;
        auto xr = record(handle(entry));
        if (!xr || xr->type() != "XRECORD")
            return 0;
        auto f = fields(*xr, "AcDbXrecord");
        if (f.i(90) != 1)
            return 0;
        root = f.h(330);
        auto h = doc.solid_history_root(root);
        return h && h->owner == id ? root : 0;
    }
    std::vector<const DxfRecord *> objects() const {
        std::vector<const DxfRecord *> out;
        bool objects = false;
        for (const auto &v : doc.records_) {
            const auto &r = std::visit([](const DxfRecord &r) -> const DxfRecord & { return r; }, v);
            if (r.type() == "SECTION")
                objects = r.field("", 2) == "OBJECTS";
            else if (r.type() == "ENDSEC")
                objects = false;
            else if (objects)
                out.push_back(&r);
        }
        return out;
    }
    std::optional<SolidHistoryNode> active(const SolidHistoryGraph &g) const {
        auto root = doc.solid_history_root(g.root);
        if (!root)
            return {};
        std::optional<SolidHistoryNode> result;
        for (auto id : g.nodes) {
            auto n = doc.solid_history_node(id);
            if (n && n->node_id() == root->history_node_id) {
                if (result)
                    return {};
                result = n;
            }
        }
        return result;
    }
    static void set(DxfDocument &d, std::uint64_t id, std::string_view scope, int code, std::string value) {
        mutable_record(d, id).set_field(scope, code, std::move(value));
    }
    static void replace_graph(DxfDocument &d, std::uint64_t id, const BlockEvaluationGraph &g) {
        std::vector<DxfRawField> f;
        auto put = [&](int c, std::int32_t n) { f.push_back({c, std::to_string(n)}); };
        put(96, g.first_node_id);
        put(97, g.first_node_id_copy);
        for (const auto &n : g.nodes) {
            put(91, n.id);
            put(93, n.edge_flags);
            put(95, n.next_id);
            f.push_back({360, hex(n.expression)});
            for (auto x : n.node_data)
                put(92, x);
            if (n.active_cycles)
                put(290, *n.active_cycles);
        }
        for (const auto &e : g.edges) {
            put(92, e.id);
            put(93, e.next_id);
            put(94, e.incoming_edge);
            put(91, e.source_node);
            put(91, e.destination_node);
            for (auto x : e.outgoing_edges)
                put(92, x);
        }
        mutable_record(d, id).set_subclass_fields("AcDbEvalGraph", f);
    }
    std::vector<HistoryReference> references(std::uint64_t id) const {
        std::vector<HistoryReference> out;
        auto r = record(id);
        if (!r)
            return out;
        std::string scope, group;
        unsigned depth = 0;
        for (const auto &p : r->pairs_) {
            if (p.code == 102) {
                if (p.value == "}") {
                    if (depth)
                        --depth;
                    if (!depth)
                        group.clear();
                } else if (!p.value.empty() && p.value.front() == '{') {
                    if (!depth)
                        group = p.value;
                    ++depth;
                }
                continue;
            }
            if (p.code == 1001 || p.code == 101)
                break;
            if (!depth && p.code == 100) {
                scope = p.value;
                continue;
            }
            if (depth && (depth != 1 || (group != "{ACAD_REACTORS" && group != "{ACAD_XDICTIONARY")))
                continue;
            if (!(p.code >= 330 && p.code <= 369))
                continue;
            HistoryReferenceRole role = HistoryReferenceRole::Other;
            if (group == "{ACAD_REACTORS")
                role = HistoryReferenceRole::Reactor;
            else if (group == "{ACAD_XDICTIONARY")
                role = HistoryReferenceRole::ExtensionDictionary;
            else if (scope.empty() && p.code == 330)
                role = HistoryReferenceRole::Owner;
            else if ((scope == "AcDb3dSolid" && p.code == 350) || (scope == "AcDbXrecord" && p.code == 330))
                role = HistoryReferenceRole::HistoryRoot;
            else if (scope == "AcDbShHistory" && p.code == 360)
                role = HistoryReferenceRole::EvaluationGraph;
            else if (scope == "AcDbEvalGraph" && p.code == 360)
                role = HistoryReferenceRole::Expression;
            else if (scope == "AcDbShHistoryNode" && p.code == 347)
                role = HistoryReferenceRole::Material;
            out.push_back({id, handle(p.value), role, p.code});
        }
        if (auto n = doc.solid_history_node(id))
            if (auto h = std::get_if<std::uint64_t>(&n->value))
                out.push_back({id, *h, HistoryReferenceRole::EvaluationValue, 91});
        return out;
    }
};
std::optional<SolidHistoryRoot> DxfDocument::solid_history_root(std::uint64_t id) const {
    auto r = SolidHistoryReader(*this).record(id);
    if (!r || r->type() != "ACSH_HISTORY_CLASS")
        return {};
    auto f = SolidHistoryReader::fields(*r, "AcDbShHistory");
    return SolidHistoryRoot{
        id, handle(r->owner_handle()), f.h(360), f.i(90), f.i(91), f.i(92), f.i(280) != 0, f.i(281) != 0};
}
std::optional<SolidHistoryNode> DxfDocument::solid_history_node(std::uint64_t id) const {
    auto r = SolidHistoryReader(*this).record(id);
    if (!r)
        return {};
    auto k = kind(r->type());
    if (!k)
        return {};
    auto f = SolidHistoryReader::fields(*r, "AcDbShHistoryNode"),
         ev = SolidHistoryReader::fields(*r, "AcDbEvalExpr");
    SolidHistoryNode n;
    n.handle = id;
    n.owner = handle(r->owner_handle());
    n.kind = *k;
    n.major = f.i(90);
    n.minor = f.i(91);
    n.step_id = f.i(92);
    n.material = f.h(347);
    auto longs = ev.all(90), shorts = ev.all(70);
    n.eval_node_id = longs.empty() ? 0 : integer(longs.front());
    n.eval_major = ev.i(98);
    n.eval_minor = ev.i(99);
    auto code = shorts.empty() ? -9999 : integer(shorts.front());
    if (code < INT16_MIN || code > INT16_MAX)
        throw std::invalid_argument("Invalid history evaluation type");
    n.value_code = std::int16_t(code);
    switch (code) {
    case 40:
        n.value = ev.all(140).empty() ? ev.d(40) : ev.d(140);
        break;
    case 10:
    case 11:
        n.value = Vector2{ev.d(code), ev.d(code + 10)};
        break;
    case 1:
        n.value = ev.text(1, "");
        break;
    case 90:
        n.value = ev.i(90);
        break;
    case 91:
        n.value = ev.h(91);
        break;
    case 70: {
        auto value = shorts.size() > 1 ? integer(shorts[1]) : 0;
        if (value < INT16_MIN || value > INT16_MAX)
            throw std::invalid_argument("Invalid history short");
        n.value = std::int16_t(value);
        break;
    }
    default:
        break;
    }
    auto aci = f.i(62);
    if (aci < INT16_MIN || aci > INT16_MAX)
        throw std::invalid_argument("Invalid history ACI");
    n.color = f.all(420).empty() ? Color::from_index(std::int16_t(aci))
                                 : Color::from_true_color_value(std::uint32_t(f.i(420)));
    SurfaceMatrix m{};
    if (f.all(41).empty()) {
        auto values = f.all(40);
        if (values.size() > 16)
            throw std::invalid_argument("Excess history matrix entries");
        for (std::size_t i = 0; i < values.size(); ++i)
            m[i] = real(values[i]);
    } else
        for (std::size_t i = 0; i < 16; ++i) {
            m[i] = f.d(40 + int(i));
            if (i >= 10)
                m[i] *= 3.14159265358979323846 / 180.;
        }
    for (std::size_t i = 0; i < 16; ++i)
        n.transform[i] = m[(i % 4) * 4 + i / 4];
    if (*k == SolidHistoryKind::Boolean) {
        auto b = SolidHistoryReader::fields(*r, "AcDbShBoolean");
        n.boolean_operation = std::uint8_t(std::clamp(b.i(280), 0, 255));
        n.first_operand = b.i(92);
        n.second_operand = b.i(93);
    }
    return n;
}
std::optional<SolidHistoryGraph> DxfDocument::solid_history_graph(std::uint64_t entity) const {
    SolidHistoryReader read(*this);
    auto root = read.entity_root(entity);
    auto value = solid_history_root(root);
    if (!value)
        return {};
    SolidHistoryGraph g;
    g.root = root;
    for (auto r : read.objects())
        if (kind(r->type()) && read.reaches(handle(r->owner_handle()), root))
            g.nodes.push_back(handle(r->handle()));
    std::sort(g.nodes.begin(), g.nodes.end(), [&](auto a, auto b) {
        return std::make_pair(solid_history_node(a)->step_id, a) <
               std::make_pair(solid_history_node(b)->step_id, b);
    });
    auto graph = read.record(value->evaluation_graph);
    if (graph && graph->type() == "ACAD_EVALUATION_GRAPH")
        g.evaluation_graph = value->evaluation_graph;
    return g;
}
std::optional<SolidHistoryNode> DxfDocument::solid_history_operation(std::uint64_t entity) const {
    auto g = solid_history_graph(entity);
    if (!g)
        return {};
    auto root = solid_history_root(g->root);
    for (auto id : g->nodes) {
        auto n = solid_history_node(id);
        if (n && n->node_id() == root->history_node_id)
            return n;
    }
    return g->nodes.empty() ? std::nullopt : solid_history_node(g->nodes.back());
}
std::optional<std::vector<SolidHistoryNode>>
DxfDocument::solid_history_operations(std::uint64_t entity) const {
    auto g = solid_history_graph(entity);
    if (!g)
        return {};
    SolidHistoryReader read(*this);
    auto active = read.active(*g);
    if (!active)
        return {};
    std::vector<SolidHistoryNode> chain;
    std::set<std::uint64_t> seen;
    if (g->evaluation_graph) {
        auto eg = evaluation_graph(*g->evaluation_graph);
        if (!eg)
            return {};
        auto it = std::find_if(eg->nodes.begin(), eg->nodes.end(),
                               [&](const auto &n) { return n.expression == active->handle; });
        for (;;) {
            if (it == eg->nodes.end() || !seen.insert(it->expression).second)
                return {};
            auto op = solid_history_node(it->expression);
            if (!op)
                return {};
            chain.push_back(*op);
            if (it->node_data[0] < 0)
                break;
            auto edge = std::find_if(eg->edges.begin(), eg->edges.end(),
                                     [&](const auto &e) { return e.id == it->node_data[0]; });
            if (edge == eg->edges.end())
                return {};
            it = std::find_if(eg->nodes.begin(), eg->nodes.end(),
                              [&](const auto &n) { return n.id == edge->source_node; });
        }
    } else {
        for (;;) {
            if (active->node_id() <= 0 || !seen.insert(std::uint64_t(active->node_id())).second)
                return {};
            chain.push_back(*active);
            if (active->parent_id <= 0)
                break;
            std::optional<SolidHistoryNode> parent;
            for (auto h : g->nodes) {
                auto n = solid_history_node(h);
                if (n && n->node_id() == active->parent_id) {
                    if (parent)
                        return {};
                    parent = n;
                }
            }
            if (!parent)
                return {};
            active = parent;
        }
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}
std::optional<SolidHistoryTree> DxfDocument::solid_history_tree(std::uint64_t entity) const {
    auto g = solid_history_graph(entity);
    if (!g)
        return {};
    if (!g->evaluation_graph) {
        auto chain = solid_history_operations(entity);
        if (!chain || chain->empty())
            return {};
        SolidHistoryTree tree{chain->front(), {}};
        for (std::size_t i = 1; i < chain->size(); ++i) {
            SolidHistoryTree next{(*chain)[i], {}};
            next.operands.push_back(std::move(tree));
            tree = std::move(next);
        }
        return tree;
    }
    auto graph = evaluation_graph(*g->evaluation_graph);
    auto active = SolidHistoryReader(*this).active(*g);
    if (!graph || !active)
        return {};
    auto it = std::find_if(graph->nodes.begin(), graph->nodes.end(),
                           [&](const auto &n) { return n.expression == active->handle; });
    if (it == graph->nodes.end())
        return {};
    std::set<std::size_t> path;
    std::size_t count = 0;
    std::function<std::optional<SolidHistoryTree>(std::size_t)> walk =
        [&](std::size_t index) -> std::optional<SolidHistoryTree> {
        if (path.size() >= 512 || ++count > 100000)
            throw std::length_error("History tree traversal limit");
        if (!path.insert(index).second)
            return {};
        const auto &node = graph->nodes[index];
        auto op = solid_history_node(node.expression);
        if (!op)
            return {};
        SolidHistoryTree tree{*op, {}};
        auto edge_id = node.node_data[0];
        std::set<std::int32_t> edges;
        while (edge_id >= 0) {
            if (!edges.insert(edge_id).second)
                return {};
            auto e = std::find_if(graph->edges.begin(), graph->edges.end(),
                                  [&](const auto &x) { return x.id == edge_id; });
            if (e == graph->edges.end())
                return {};
            auto from = std::find_if(graph->nodes.begin(), graph->nodes.end(),
                                     [&](const auto &x) { return x.id == e->source_node; });
            if (from == graph->nodes.end())
                return {};
            auto child = walk(std::size_t(from - graph->nodes.begin()));
            if (!child)
                return {};
            tree.operands.push_back(std::move(*child));
            edge_id = e->outgoing_edges[1];
        }
        path.erase(index);
        if (op->kind == SolidHistoryKind::Boolean && tree.operands.size() == 2 &&
            tree.operands[1].operation.node_id() == op->first_operand)
            std::swap(tree.operands[0], tree.operands[1]);
        return tree;
    };
    return walk(std::size_t(it - graph->nodes.begin()));
}
std::vector<HistoryReference> DxfDocument::solid_history_references(std::uint64_t entity) const {
    auto graph = solid_history_graph(entity);
    if (!graph)
        return {};
    SolidHistoryReader read(*this);
    std::vector<std::uint64_t> ids{entity, graph->root};
    if (graph->evaluation_graph)
        ids.push_back(*graph->evaluation_graph);
    ids.insert(ids.end(), graph->nodes.begin(), graph->nodes.end());
    // Include the surface fallback attachment path, not just the resolved root.
    auto dictionary = extension_dictionary_handle(hex(entity));
    if (!dictionary.empty()) {
        auto entry = dictionary_entry(dictionary, "CADCODEC_SURFACE_HISTORY_V1");
        auto xr = read.record(handle(entry));
        if (xr && xr->type() == "XRECORD") {
            auto f = SolidHistoryReader::fields(*xr, "AcDbXrecord");
            if (f.i(90) == 1 && f.h(330) == graph->root) {
                ids.push_back(handle(dictionary));
                ids.push_back(handle(entry));
            }
        }
    }
    std::vector<HistoryReference> out;
    for (auto id : ids) {
        auto refs = read.references(id);
        out.insert(out.end(), refs.begin(), refs.end());
    }
    return out;
}
void DxfDocument::validate_history_mutation(std::uint64_t entity) const {
    SolidHistoryReader read(*this);
    auto root = read.entity_root(entity);
    if (!root)
        return;
    auto value = solid_history_root(root);
    if (!value || value->owner != entity)
        throw std::invalid_argument("History root has missing or foreign ownership");
    auto graph = solid_history_graph(entity);
    for (auto id : graph->nodes) {
        auto n = solid_history_node(id);
        if (n->value_code == 91)
            throw std::invalid_argument(
                "DXF history evaluation-handle code 91 is not safely mutable across encodings");
    }
    if (value->evaluation_graph) {
        auto eval = read.record(value->evaluation_graph);
        if (!eval || eval->type() != "ACAD_EVALUATION_GRAPH" || handle(eval->owner_handle()) != root)
            throw std::invalid_argument("History evaluation graph has missing or foreign ownership");
        auto g = evaluation_graph(value->evaluation_graph);
        for (const auto &node : g->nodes)
            if (std::find(graph->nodes.begin(), graph->nodes.end(), node.expression) == graph->nodes.end())
                throw std::invalid_argument("History expression is missing or foreign");
    }
}
void DxfDocument::set_solid_history_active_node(std::uint64_t entity, std::int32_t id) {
    validate_history_mutation(entity);
    auto g = solid_history_graph(entity);
    if (!g)
        throw std::invalid_argument("Missing modeler history");
    std::size_t matches = 0;
    for (auto h : g->nodes)
        if (solid_history_node(h)->node_id() == id)
            ++matches;
    if (matches != 1)
        throw std::invalid_argument("Missing or ambiguous active history node");
    auto copy = *this;
    auto &r = SolidHistoryReader::mutable_record(copy, g->root);
    r.set_field("AcDbShHistory", 92,
                r.binary_ ? detail::binary_value_from_text(92, std::to_string(id)) : std::to_string(id));
    if (!copy.solid_history_tree(entity))
        throw std::invalid_argument("Active history graph is unusable");
    *this = std::move(copy);
}
void DxfDocument::set_solid_history_node_material(std::uint64_t id, std::uint64_t material) {
    if (!solid_history_node(id))
        throw std::invalid_argument("Missing history node");
    auto target = SolidHistoryReader(*this).record(material);
    if (material && (!target || target->type() != "MATERIAL"))
        throw std::invalid_argument("History material must name a material object");
    auto copy = *this;
    SolidHistoryReader::set(copy, id, "AcDbShHistoryNode", 347, hex(material));
    *this = std::move(copy);
}
void DxfDocument::set_solid_history_evaluation_graph(std::uint64_t entity,
                                                     const BlockEvaluationGraph &graph) {
    validate_history_mutation(entity);
    auto current = solid_history_graph(entity);
    if (!current || !current->evaluation_graph)
        throw std::invalid_argument("Missing evaluation graph");
    std::set<std::int32_t> nodes, edges;
    std::set<std::uint64_t> expressions;
    for (const auto &n : graph.nodes) {
        if (n.id < 0 || !nodes.insert(n.id).second || !expressions.insert(n.expression).second ||
            std::find(current->nodes.begin(), current->nodes.end(), n.expression) == current->nodes.end())
            throw std::invalid_argument("Duplicate or foreign history node");
    }
    for (const auto &e : graph.edges)
        if (e.id < 0 || !edges.insert(e.id).second || !nodes.count(e.source_node) ||
            !nodes.count(e.destination_node))
            throw std::invalid_argument("Invalid history edge");
    for (const auto &n : graph.nodes) {
        std::set<std::int32_t> seen;
        auto id = n.node_data[0];
        while (id >= 0) {
            if (!seen.insert(id).second)
                throw std::invalid_argument("Cyclic history incoming-edge list");
            auto e = std::find_if(graph.edges.begin(), graph.edges.end(),
                                  [&](const auto &e) { return e.id == id; });
            if (e == graph.edges.end() || e->destination_node != n.id)
                throw std::invalid_argument("Invalid history incoming edge");
            id = e->outgoing_edges[1];
        }
    }
    // Reject cycles throughout the proposed graph, including inactive branches.
    std::map<std::int32_t, std::size_t> degree;
    for (auto n : nodes)
        degree[n] = 0;
    for (const auto &e : graph.edges)
        ++degree[e.destination_node];
    std::vector<std::int32_t> ready;
    for (auto [id, n] : degree)
        if (!n)
            ready.push_back(id);
    for (std::size_t i = 0; i < ready.size(); ++i)
        for (const auto &e : graph.edges)
            if (e.source_node == ready[i] && !--degree[e.destination_node])
                ready.push_back(e.destination_node);
    if (ready.size() != nodes.size())
        throw std::invalid_argument("Cyclic history evaluation graph");
    auto copy = *this;
    SolidHistoryReader::replace_graph(copy, *current->evaluation_graph, graph);
    if (!copy.solid_history_tree(entity))
        throw std::invalid_argument("Unusable history graph");
    *this = std::move(copy);
}
#include "solid_history_operations.inc"
#include "solid_history_merge.inc"
} // namespace libopencad
