// SPDX-License-Identifier: MPL-2.0
// Port of acadrust document/dynamic_properties.rs and DXF dynamic object schemas.
#include <libopencad/dxf_document.hpp>
#include "dxf_binary.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace libopencad {
namespace {
std::string hex(std::uint64_t value) {
    std::ostringstream s;
    s << std::hex << std::uppercase << value;
    return s.str();
}
std::string ascii_upper(std::string_view value) {
    std::string out(value);
    for (char &c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trimmed(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos)
        return {};
    return std::string(value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1));
}
std::uint64_t handle(const std::string &input) {
    auto value = trimmed(input);
    if (value.empty())
        return 0;
    std::size_t n = 0;
    if (value.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        throw std::invalid_argument("Invalid dynamic block handle");
    auto h = std::stoull(value, &n, 16);
    if (n != value.size())
        throw std::invalid_argument("Invalid dynamic block handle");
    return h;
}
std::int32_t integer(const std::string &input) {
    const auto s = trimmed(input);
    std::size_t n = 0;
    auto v = std::stoll(s, &n);
    if (n != s.size() || v < std::numeric_limits<std::int32_t>::min() ||
        v > std::numeric_limits<std::int32_t>::max())
        throw std::invalid_argument("Invalid dynamic block integer");
    return static_cast<std::int32_t>(v);
}
double real(const std::string &input) {
    const auto s = trimmed(input);
    std::size_t n = 0;
    auto v = std::stod(s, &n);
    if (n != s.size() || !std::isfinite(v))
        throw std::invalid_argument("Invalid dynamic block real");
    return v;
}
struct Fields {
    std::vector<DxfRawField> values;
    std::string text(int code, std::string fallback = {}) const {
        for (auto it = values.rbegin(); it != values.rend(); ++it)
            if (it->code == code)
                return it->value;
        return fallback;
    }
    std::int32_t first_i(int code) const {
        for (const auto &f : values)
            if (f.code == code)
                return integer(f.value);
        return 0;
    }
    std::int32_t i(int code) const { return integer(text(code, "0")); }
    double d(int code) const { return real(text(code, "0")); }
    std::uint64_t h(int code) const { return handle(text(code, "0")); }
    Vector3 point(int code) const { return {d(code), d(code + 10), d(code + 20)}; }
    std::vector<std::uint64_t> handles(int code) const {
        std::vector<std::uint64_t> r;
        for (const auto &f : values)
            if (f.code == code)
                r.push_back(handle(f.value));
        return r;
    }
    std::vector<BlockEvalValue> choices(int code) const {
        std::vector<BlockEvalValue> r;
        for (const auto &f : values)
            if (f.code == code)
                r.emplace_back(real(f.value));
        return r;
    }
};
double angle(Vector3 v) {
    auto a = std::atan2(v.y, v.x);
    return a < 0 ? a + 6.2831853071795864769 : a;
}
double wrap(double a) {
    a = std::fmod(a, 6.2831853071795864769);
    return a < 0 ? a + 6.2831853071795864769 : a;
}
struct Cache {
    bool present = false;
    std::vector<Vector3> points;
    std::optional<std::string> text;
    std::optional<std::int16_t> flip;
};
} // namespace

class DynamicBlockReader {
  public:
    explicit DynamicBlockReader(const DxfDocument &doc) : doc(doc) {}
    const DxfDocument &doc;
    const DxfRecord *record(std::uint64_t id) const { return doc.get_record(hex(id)); }
    static Fields fields(const DxfRecord &r, std::string_view scope) {
        Fields out;
        bool active = scope.empty();
        unsigned depth = 0;
        for (const auto &p : r.pairs_) {
            // In an XRECORD payload, 100/102 are also ordinary string entries.
            // They count toward cache_flip's four-entry header. Only the two
            // common application groups receive control semantics upstream.
            if (active && !depth && scope == "AcDbXrecord" &&
                ((p.code == 100 && p.value != "AcDbXrecord") ||
                 (p.code == 102 && p.value != "{ACAD_REACTORS" && p.value != "{ACAD_XDICTIONARY"))) {
                out.values.push_back({p.code, r.decode_string(p.value)});
                continue;
            }
            if (p.code == 102) {
                if (!p.value.empty() && p.value.front() == '{')
                    ++depth;
                else if (p.value == "}" && depth)
                    --depth;
                continue;
            }
            if (depth)
                continue;
            if (p.code == 1001 && scope != "AcDbXrecord")
                break; // 1010/1071 within dynamic subclasses are NOT application XDATA.
            if (p.code == 100) {
                active = p.value == scope;
                continue;
            }
            if (active)
                out.values.push_back(
                    {p.code, detail::is_string_value(p.code)
                                 ? r.decode_string(p.value)
                                 : (r.binary_ ? detail::text_value_from_binary(p.code, p.value) : p.value)});
        }
        return out;
    }
    std::optional<std::uint64_t> xdict(std::uint64_t id) const {
        const auto *r = record(id);
        if (!r)
            return {};
        bool active = false;
        for (const auto &p : r->pairs_) {
            if (p.code == 102)
                active = p.value == "{ACAD_XDICTIONARY";
            else if (active && p.code == 360)
                return handle(p.value);
        }
        // Upstream also recognizes the uniquely owned dictionary of a symbol
        // record or non-dictionary object when the explicit extension link is absent.
        if (r->type() == "DICTIONARY" || id == 0)
            return {};
        std::optional<std::uint64_t> found;
        each([&](const DxfRecord &candidate) {
            if (candidate.type() == "DICTIONARY" && handle(candidate.owner_handle()) == id) {
                auto h = handle(candidate.handle());
                if (found && *found != h)
                    throw std::invalid_argument("Ambiguous extension dictionary");
                if (h)
                    found = h;
            }
        });
        return found;
    }
    std::optional<std::uint64_t> entry(std::uint64_t id, const std::string &name) const {
        const auto *r = record(id);
        if (!r || r->type() != "DICTIONARY")
            return {};
        auto f = fields(*r, "AcDbDictionary");
        for (std::size_t i = 0; i + 1 < f.values.size(); ++i)
            if (f.values[i].code == 3 && f.values[i].value == name &&
                (f.values[i + 1].code == 350 || f.values[i + 1].code == 360))
                return handle(f.values[i + 1].value);
        return {};
    }
    const DxfRecord *graph_record(std::uint64_t definition) const {
        auto d = xdict(definition);
        if (!d)
            return nullptr;
        auto g = entry(*d, "ACAD_ENHANCEDBLOCK");
        if (!g)
            return nullptr;
        const auto *r = record(*g);
        return r && r->type() == "ACAD_EVALUATION_GRAPH" ? r : nullptr;
    }
    bool reaches(std::uint64_t start, std::uint64_t target) const {
        std::unordered_set<std::uint64_t> seen;
        while (start && seen.insert(start).second) {
            if (start == target)
                return true;
            const auto *r = record(start);
            if (!r)
                return false;
            start = handle(r->owner_handle());
        }
        return false;
    }
    template <class Fn> void each(Fn fn) const {
        for (const auto &v : doc.records_)
            std::visit([&](const auto &r) { fn(static_cast<const DxfRecord &>(r)); }, v);
    }
    Cache cache(std::optional<std::uint64_t> insert, std::int32_t node) const {
        Cache result;
        if (!insert)
            return result;
        auto dict = xdict(*insert);
        for (const auto *key : {"AcDbBlockRepresentation", "AppDataCache", "ACAD_ENHANCEDBLOCKDATA"}) {
            if (!dict)
                return result;
            dict = entry(*dict, key);
        }
        if (!dict)
            return result;
        auto id = entry(*dict, std::to_string(node));
        if (!id)
            return result;
        auto r = record(*id);
        if (!r || r->type() != "XRECORD")
            return result;
        result.present = true;
        const auto f = fields(*r, "AcDbXrecord");
        std::size_t count = 0;
        bool cloning = false;
        for (std::size_t i = 0; i < f.values.size(); ++i) {
            const auto &p = f.values[i];
            if (p.code == 280 && !cloning) {
                cloning = true;
                continue;
            }
            if ((p.code >= 10 && p.code <= 37) || (p.code >= 110 && p.code <= 139) ||
                (p.code >= 210 && p.code <= 269) || (p.code >= 1010 && p.code <= 1039) ||
                (p.code >= 1043 && p.code <= 1069)) {
                Vector3 v{real(p.value), 0, 0};
                if (i + 2 >= f.values.size() || f.values[i + 1].code != p.code + 10 ||
                    f.values[i + 2].code != p.code + 20)
                    throw std::invalid_argument("Incomplete dynamic parameter cache point");
                v.y = real(f.values[++i].value);
                v.z = real(f.values[++i].value);
                result.points.push_back(v);
            } else if (p.code == 1 && !result.text)
                result.text = p.value;
            else if (count >= 4 && ((p.code >= 60 && p.code <= 79) || (p.code >= 170 && p.code <= 179) ||
                                    (p.code >= 270 && p.code <= 279) || (p.code >= 370 && p.code <= 389) ||
                                    (p.code >= 400 && p.code <= 409) || p.code == 1070)) {
                auto n = integer(p.value);
                if (n < -32768 || n > 32767)
                    throw std::invalid_argument("Invalid flip cache");
                result.flip = static_cast<std::int16_t>(n);
            }
            ++count;
        }
        return result;
    }
    static BlockEvaluationGraph graph(const DxfRecord &r) {
        auto f = fields(r, "AcDbEvalGraph");
        BlockEvaluationGraph g;
        g.first_node_id = f.i(96);
        g.first_node_id_copy = f.i(97);
        const auto &a = f.values;
        auto require = [&](std::size_t i, int code) {
            if (i >= a.size() || a[i].code != code)
                throw std::invalid_argument("Incomplete dynamic evaluation graph");
        };
        for (std::size_t i = 0; i < a.size();) {
            if (a[i].code == 96 || a[i].code == 97) {
                ++i;
                continue;
            }
            if (a[i].code == 91) {
                require(i + 1, 93);
                require(i + 2, 95);
                require(i + 3, 360);
                BlockEvaluationNode n;
                n.id = integer(a[i].value);
                n.edge_flags = integer(a[i + 1].value);
                n.next_id = integer(a[i + 2].value);
                n.expression = handle(a[i + 3].value);
                for (int j = 0; j < 4; ++j) {
                    require(i + 4 + j, 92);
                    n.node_data[j] = integer(a[i + 4 + j].value);
                }
                i += 8;
                if (i < a.size() && a[i].code == 290)
                    n.active_cycles = integer(a[i++].value) != 0;
                g.nodes.push_back(n);
            } else if (a[i].code == 92) {
                require(i + 1, 93);
                require(i + 2, 94);
                require(i + 3, 91);
                require(i + 4, 91);
                BlockEvaluationEdge e;
                e.id = integer(a[i].value);
                e.next_id = integer(a[i + 1].value);
                e.incoming_edge = integer(a[i + 2].value);
                e.source_node = integer(a[i + 3].value);
                e.destination_node = integer(a[i + 4].value);
                for (int j = 0; j < 5; ++j) {
                    require(i + 5 + j, 92);
                    e.outgoing_edges[j] = integer(a[i + 5 + j].value);
                }
                i += 10;
                g.edges.push_back(e);
            } else
                throw std::invalid_argument("Unexpected dynamic evaluation graph field");
        }
        return g;
    }
    static BlockVisibilityParameter visibility(const DxfRecord &r) {
        BlockVisibilityParameter p;
        p.handle = handle(r.handle());
        p.eval_node_id = fields(r, "AcDbEvalExpr").first_i(90);
        p.show_properties = fields(r, "AcDbBlockParameter").i(280) != 0;
        p.definition_point = fields(r, "AcDbBlock1PtParameter").point(1010);
        auto f = fields(r, "AcDbBlockVisibilityParameter");
        p.name = f.text(301);
        p.description = f.text(302);
        p.all_blocks = f.handles(331);
        for (const auto &v : f.values) {
            if (v.code == 303)
                p.states.push_back({v.value, {}, {}});
            else if (v.code == 332 || v.code == 333) {
                if (p.states.empty())
                    throw std::invalid_argument("Visibility member without a state");
                (v.code == 332 ? p.states.back().visible_blocks : p.states.back().visible_params)
                    .push_back(handle(v.value));
            }
        }
        return p;
    }
};

bool DxfDocument::is_dynamic_block(std::uint64_t definition) const {
    return DynamicBlockReader(*this).graph_record(definition) != nullptr;
}
std::optional<BlockEvaluationGraph> DxfDocument::evaluation_graph(std::uint64_t object) const {
    auto r=DynamicBlockReader(*this).record(object);
    if(!r||r->type()!="ACAD_EVALUATION_GRAPH")return {};
    return DynamicBlockReader::graph(*r);
}
std::optional<BlockEvaluationGraph> DxfDocument::dynamic_block_graph(std::uint64_t definition) const {
    auto r = DynamicBlockReader(*this).graph_record(definition);
    if (!r)
        return {};
    return DynamicBlockReader::graph(*r);
}
std::optional<std::uint64_t> DxfDocument::dynamic_definition_for_insert(std::uint64_t insert) const {
    DynamicBlockReader reader(*this);
    const DxfInsert *ref = nullptr;
    for_each<DxfInsert>([&](const auto &i) {
        if (handle(i.handle()) == insert)
            ref = &i;
    });
    if (!ref)
        return {};
    if (auto dict = reader.xdict(insert)) {
        std::optional<std::uint64_t> definition;
        reader.each([&](const DxfRecord &r) {
            if ((r.type() == "ACDB_BLOCKREPRESENTATION_DATA" || r.type() == "BLOCKREPRESENTATION") &&
                reader.reaches(handle(r.handle()), *dict)) {
                auto d = reader.fields(r, "AcDbBlockRepresentationData").h(340);
                if (definition && *definition != d)
                    throw std::invalid_argument("Ambiguous dynamic block representation");
                definition = d;
            }
        });
        if (definition)
            return definition;
    }
    auto symbol = get_symbol(SymbolTable::BlockRecord, ref->definition().block_name);
    return symbol ? std::optional<std::uint64_t>(handle(symbol->handle)) : std::nullopt;
}
std::optional<std::uint64_t> DxfDocument::dynamic_block_definition(std::uint64_t block) const {
    if (is_dynamic_block(block))
        return block;
    std::optional<std::uint64_t> found;
    for_each<DxfInsert>([&](const auto &i) {
        auto b = get_symbol(SymbolTable::BlockRecord, i.definition().block_name);
        if (b && handle(b->handle) == block) {
            auto d = dynamic_definition_for_insert(handle(i.handle()));
            if (d && *d != block) {
                if (found && found != d)
                    throw std::invalid_argument("Ambiguous anonymous block definition");
                found = d;
            }
        }
    });
    return found;
}
std::optional<BlockVisibilityParameter>
DxfDocument::block_visibility_parameter(std::uint64_t definition) const {
    DynamicBlockReader reader(*this);
    std::optional<BlockVisibilityParameter> result;
    reader.each([&](const auto &r) {
        if (r.type() == "BLOCKVISIBILITYPARAMETER" && reader.reaches(handle(r.owner_handle()), definition)) {
            if (result)
                throw std::invalid_argument("Ambiguous block visibility parameter");
            result = reader.visibility(r);
        }
    });
    return result;
}
std::optional<std::pair<std::uint64_t, BlockVisibilityParameter>>
DxfDocument::dynamic_visibility_for_insert(std::uint64_t insert) const {
    auto d = dynamic_definition_for_insert(insert);
    if (!d)
        return {};
    auto v = block_visibility_parameter(*d);
    if (!v)
        return {};
    return std::make_pair(*d, std::move(*v));
}

std::vector<DynamicBlockProperty>
DxfDocument::dynamic_block_properties(std::uint64_t definition, std::optional<std::uint64_t> insert) const {
    DynamicBlockReader reader(*this);
    double scale = 1;
    if (insert)
        for_each<DxfInsert>([&](const auto &i) {
            if (handle(i.handle()) == *insert) {
                auto s = i.definition().scale;
                scale = std::abs(s.x);
                if (std::abs(std::abs(s.y) - scale) > 1e-10 * std::max(scale, 1.) ||
                    std::abs(std::abs(s.z) - scale) > 1e-10 * std::max(scale, 1.))
                    scale = -1;
            }
        });
    if (scale < 0)
        return {};
    auto graph = dynamic_block_graph(definition);
    if (!graph)
        return {};
    std::vector<DynamicBlockProperty> result;
    for (const auto &node : graph->nodes) {
        const auto *r = reader.record(node.expression);
        if (!r)
            throw std::invalid_argument("Missing dynamic evaluation node " + hex(node.expression));
        auto t = r->type();
        const auto eval = reader.fields(*r, "AcDbEvalExpr");
        const auto id = eval.first_i(90);
        const auto param = reader.fields(*r, "AcDbBlockParameter");
        auto add = [&](std::string name, std::string description, std::int16_t units, BlockEvalValue value,
                       std::vector<BlockEvalValue> allowed = std::vector<BlockEvalValue>{}) {
            DynamicBlockProperty p;
            p.parameter_handle = node.expression;
            p.name = std::move(name);
            p.description = std::move(description);
            p.units = units;
            p.show = param.i(280) != 0;
            p.value = std::move(value);
            p.type_code = std::holds_alternative<std::string>(p.value)    ? 5
                          : std::holds_alternative<std::int16_t>(p.value) ? 3
                          : std::holds_alternative<std::int32_t>(p.value) ? 2
                                                                          : 1;
            p.allowed_values = std::move(allowed);
            result.push_back(std::move(p));
        };
        auto saved = [&](std::size_t count, bool text = false, bool flip = false) {
            auto c = reader.cache(insert, id);
            if (c.present &&
                ((text && !c.text) || (flip && !c.flip) || (!text && !flip && c.points.size() < count)))
                throw std::invalid_argument("Malformed dynamic parameter cache for node " +
                                            std::to_string(id));
            return c;
        };
        if (t == "BLOCKVISIBILITYPARAMETER") {
            auto p = reader.visibility(*r);
            auto c = saved(0, true);
            std::vector<BlockEvalValue> choices;
            for (const auto &s : p.states)
                choices.emplace_back(s.name);
            add(p.name, p.description, 0, c.text.value_or(p.states.empty() ? "" : p.states.front().name),
                choices);
        } else if (t == "BLOCKPOINTPARAMETER") {
            auto p = reader.fields(*r, "AcDbBlockPointParameter");
            auto c = saved(1);
            auto pt =
                c.points.empty() ? reader.fields(*r, "AcDbBlock1PtParameter").point(1010) : c.points.front();
            add(p.text(303) + " X", p.text(304), 2, pt.x);
            add(p.text(303) + " Y", p.text(304), 2, pt.y);
        } else if (t == "BLOCKFLIPPARAMETER") {
            auto p = reader.fields(*r, "AcDbBlockFlipParameter");
            auto c = saved(0, false, true);
            add(p.text(305), p.text(306), 0, c.flip.value_or(0), {std::int16_t(0), std::int16_t(1)});
        } else if (t == "BLOCKUSERPARAMETER") {
            auto p = reader.fields(*r, "AcDbBlockUserParameter");
            BlockEvalValue v;
            // Match the pinned DXF reader's last-occurrence projection, including
            // repeated 70/90 fields. The preserved record retains the full payload.
            switch (p.i(70)) {
            case 40:
                v = p.d(40);
                break;
            case 1:
                v = p.text(1);
                break;
            case 90:
                v = p.i(90);
                break;
            case 91:
                v = p.h(91);
                break;
            case 70:
                v = static_cast<std::int16_t>(p.i(70));
                break;
            default:
                break;
            }
            add(reader.fields(*r, "AcDbBlockElement").text(300), "", static_cast<std::int16_t>(p.i(170)), v);
        } else if (t == "BLOCKLOOKUPPARAMETER") {
            auto p = reader.fields(*r, "AcDbBlockLookupParameter");
            const DxfRecord *action = nullptr;
            for (const auto &n : graph->nodes)
                if (auto a = reader.record(n.expression))
                    if (a->type() == "BLOCKLOOKUPACTION" &&
                        reader.fields(*a, "AcDbEvalExpr").first_i(90) == p.i(94)) {
                        action = a;
                        break;
                    }
            if (!action)
                throw std::invalid_argument("Missing lookup action for " + p.text(303));
            auto a = reader.fields(*action, "AcDbBlockLookupAction");
            std::vector<std::string> expressions;
            std::vector<Fields> columns;
            for (const auto &f : a.values) {
                if (f.code == 302)
                    expressions.push_back(f.value);
                else if (f.code == 303)
                    columns.emplace_back();
                else if (!columns.empty())
                    columns.back().values.push_back(f);
            }
            auto width = a.i(93), rows = a.i(92);
            if (width <= 0 || rows < 0 || columns.size() != static_cast<std::size_t>(width) ||
                static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(rows) != expressions.size())
                throw std::invalid_argument("Incomplete lookup table");
            std::size_t column = 0;
            while (column < columns.size() && columns[column].i(94) != id)
                ++column;
            if (column == columns.size())
                throw std::invalid_argument("Missing lookup column");
            std::vector<BlockEvalValue> choices;
            auto push = [&](const std::string &s) {
                if (std::none_of(choices.begin(), choices.end(),
                                 [&](const auto &v) { return std::get<std::string>(v) == s; }))
                    choices.emplace_back(s);
            };
            for (int row = 0; row < rows; ++row)
                push(expressions[static_cast<std::size_t>(row) * width + column]);
            bool inputs =
                std::any_of(columns.begin(), columns.end(), [](const auto &c) { return c.i(282) == 0; });
            const auto &c = columns[column];
            auto fallback = inputs ? c.text(305) : choices.empty() ? "" : std::get<std::string>(choices[0]);
            auto current = saved(0, true).text.value_or(fallback);
            if (inputs && current == fallback)
                push(fallback);
            add(p.text(303), p.text(304), 0, current, choices);
            result.back().read_only = c.i(281) == 0;
        } else if (t == "BLOCKLINEARPARAMETER" || t == "BLOCKPOLARPARAMETER" ||
                   t == "BLOCKROTATIONPARAMETER" || t == "BLOCKXYPARAMETER" ||
                   t == "BLOCKALIGNEDCONSTRAINTPARAMETER" || t == "BLOCKLINEARCONSTRAINTPARAMETER" ||
                   t == "BLOCKHORIZONTALCONSTRAINTPARAMETER" || t == "BLOCKVERTICALCONSTRAINTPARAMETER") {
            auto points = reader.fields(*r, "AcDbBlock2PtParameter");
            auto c = saved(t == "BLOCKROTATIONPARAMETER" ? 4 : 2);
            auto a = c.points.empty() ? points.point(1010) : c.points[0];
            auto b = c.points.empty() ? points.point(1011) : c.points[1];
            const auto delta = b - a;
            if (t == "BLOCKLINEARPARAMETER") {
                auto p = reader.fields(*r, "AcDbBlockLinearParameter");
                add(p.text(305), p.text(306), 2, delta.length(), p.choices(144));
            } else if (t == "BLOCKPOLARPARAMETER") {
                auto p = reader.fields(*r, "AcDbBlockPolarParameter");
                add(p.text(305), p.text(306), 2, delta.length(), p.choices(144));
                add(p.text(307), p.text(308), 1, angle(delta), p.choices(148));
            } else if (t == "BLOCKXYPARAMETER") {
                auto p = reader.fields(*r, "AcDbBlockXYParameter");
                add(p.text(306), p.text(308), 2, std::abs(delta.x), p.choices(145));
                add(p.text(305), p.text(307), 2, std::abs(delta.y), p.choices(149));
            } else if (t == "BLOCKROTATIONPARAMETER") {
                auto p = reader.fields(*r, "AcDbBlockRotationParameter");
                auto base = c.points.empty() ? p.point(1011) : c.points[3];
                auto value = angle(delta) - angle(base - a);
                if (insert)
                    for (const auto &n : graph->nodes)
                        if (auto act = reader.record(n.expression))
                            if (act->type() == "BLOCKFLIPACTION") {
                                auto deps = reader.fields(*act, "AcDbBlockAction").handles(330);
                                if (std::find(deps.begin(), deps.end(), node.expression) != deps.end()) {
                                    auto flip = reader.cache(
                                        insert, reader.fields(*act, "AcDbBlockFlipAction").i(92));
                                    if (flip.flip.value_or(0) == 1)
                                        value = -value;
                                }
                            }
                add(p.text(305), p.text(306), 1, wrap(value), p.choices(144));
            } else {
                auto p = reader.fields(*r, "AcDbBlockLinearConstraintParameter");
                auto value = t == "BLOCKHORIZONTALCONSTRAINTPARAMETER" ? std::abs(delta.x)
                             : t == "BLOCKVERTICALCONSTRAINTPARAMETER" ? std::abs(delta.y)
                                                                       : delta.length();
                add(p.text(305), p.text(306), 2, value, p.choices(131));
            }
        } else if (t == "BLOCKANGULARCONSTRAINTPARAMETER" || t == "BLOCKDIAMETRICCONSTRAINTPARAMETER" ||
                   t == "BLOCKRADIALCONSTRAINTPARAMETER") {
            throw std::invalid_argument("Unsupported dynamic constraint property");
        } else {
            // Known graph nodes that expose no scalar reference properties.
            static const std::unordered_set<std::string> ignored = {"BLOCKALIGNMENTPARAMETER",
                                                                    "BLOCKBASEPOINTPARAMETER",
                                                                    "BLOCKMOVEACTION",
                                                                    "BLOCKFLIPACTION",
                                                                    "BLOCKROTATEACTION",
                                                                    "BLOCKSCALEACTION",
                                                                    "BLOCKARRAYACTION",
                                                                    "BLOCKLOOKUPACTION",
                                                                    "BLOCKSTRETCHACTION",
                                                                    "BLOCKPOLARSTRETCHACTION",
                                                                    "BLOCKPROPERTIESTABLE",
                                                                    "BLOCKALIGNMENTGRIP",
                                                                    "BLOCKFLIPGRIP",
                                                                    "BLOCKLINEARGRIP",
                                                                    "BLOCKLOOKUPGRIP",
                                                                    "BLOCKPOLARGRIP",
                                                                    "BLOCKROTATIONGRIP",
                                                                    "BLOCKVISIBILITYGRIP",
                                                                    "BLOCKXYGRIP",
                                                                    "BLOCKPROPERTIESTABLEGRIP",
                                                                    "ACDB_DYNAMICBLOCKPROXYNODE",
                                                                    "ACDBBLOCKPARAMDEPENDENCYBODY",
                                                                    "BLOCKGRIPLOCATIONCOMPONENT",
                                                                    "ACDB_BLOCKREPRESENTATION_DATA",
                                                                    "ACAD_EVALUATION_GRAPH",
                                                                    "BLOCKPARAMDEPENDENCYBODY"};
            if (!ignored.count(t))
                throw std::invalid_argument("Unsupported dynamic evaluation object " + hex(node.expression));
        }
    }
    if (auto v = block_visibility_parameter(definition)) {
        auto property = std::find_if(result.begin(), result.end(),
                                     [&](const auto &p) { return p.parameter_handle == v->handle; });
        if (property != result.end())
            if (auto selected = std::get_if<std::string>(&property->value))
                for (const auto &state : v->states)
                    if (state.name == *selected)
                        for (auto &p : result)
                            p.visible = p.parameter_handle == v->handle ||
                                        std::find(state.visible_params.begin(), state.visible_params.end(),
                                                  p.parameter_handle) != state.visible_params.end();
    }
    for (auto &p : result) {
        auto multiplier = p.units == 2 ? scale : p.units == 3 ? scale * scale : 1;
        if (auto d = std::get_if<double>(&p.value))
            *d *= multiplier;
        for (auto &v : p.allowed_values)
            if (auto d = std::get_if<double>(&v))
                *d *= multiplier;
    }
    return result;
}

bool is_dynamic_block_object_name(std::string_view name) {
    auto n = ascii_upper(name);
    static const char *const names[] = {
        "ACDBBLOCKPARAMDEPENDENCYBODY",
        "ACDB_BLOCKREPRESENTATION_DATA",
        "ACDB_DYNAMICBLOCKPROXYNODE",
        "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION",
        "BLOCKALIGNEDCONSTRAINTPARAMETER",
        "BLOCKALIGNMENTGRIP",
        "BLOCKALIGNMENTPARAMETER",
        "BLOCKANGULARCONSTRAINTPARAMETER",
        "BLOCKARRAYACTION",
        "BLOCKBASEPOINTPARAMETER",
        "BLOCKDIAMETRICCONSTRAINTPARAMETER",
        "BLOCKFLIPACTION",
        "BLOCKFLIPGRIP",
        "BLOCKFLIPPARAMETER",
        "BLOCKGRIPLOCATIONCOMPONENT",
        "BLOCKHORIZONTALCONSTRAINTPARAMETER",
        "BLOCKLINEARCONSTRAINTPARAMETER",
        "BLOCKLINEARGRIP",
        "BLOCKLINEARPARAMETER",
        "BLOCKLOOKUPACTION",
        "BLOCKLOOKUPGRIP",
        "BLOCKLOOKUPPARAMETER",
        "BLOCKMOVEACTION",
        "BLOCKPARAMDEPENDENCYBODY",
        "BLOCKPOINTPARAMETER",
        "BLOCKPOLARGRIP",
        "BLOCKPOLARPARAMETER",
        "BLOCKPOLARSTRETCHACTION",
        "BLOCKPROPERTIESTABLE",
        "BLOCKPROPERTIESTABLEGRIP",
        "BLOCKRADIALCONSTRAINTPARAMETER",
        "BLOCKREPRESENTATION",
        "BLOCKROTATEACTION",
        "BLOCKROTATIONGRIP",
        "BLOCKROTATIONPARAMETER",
        "BLOCKSCALEACTION",
        "BLOCKSTRETCHACTION",
        "BLOCKUSERPARAMETER",
        "BLOCKVERTICALCONSTRAINTPARAMETER",
        "BLOCKVISIBILITYGRIP",
        "BLOCKVISIBILITYPARAMETER",
        "BLOCKXYGRIP",
        "BLOCKXYPARAMETER",
        "DYNAMICBLOCKPROXYNODE",
        "DYNAMICBLOCKPURGEPREVENTER"};
    for (auto item : names)
        if (n == item)
            return true;
    return false;
}
bool is_dynamic_block_entity_name(std::string_view name) {
    auto n = ascii_upper(name);
    return n == "ALIGNMENTPARAMETERENTITY" || n == "BASEPOINTPARAMETERENTITY" || n == "FLIPPARAMETERENTITY" ||
           n == "LINEARPARAMETERENTITY" || n == "POINTPARAMETERENTITY" || n == "ROTATIONPARAMETERENTITY" ||
           n == "VISIBILITYPARAMETERENTITY" || n == "XYPARAMETERENTITY" || n == "FLIPGRIPENTITY" ||
           n == "LINEARGRIPENTITY" || n == "POLARGRIPENTITY" || n == "ROTATIONGRIPENTITY" ||
           n == "VISIBILITYGRIPENTITY" || n == "XYGRIPENTITY" ||
           n == "BLOCKANGULARCONSTRAINTPARAMETERENTITY";
}
std::optional<std::string> dynamic_block_cpp_class_name(std::string_view name) {
    auto n = ascii_upper(name);
    if (n == "ACDB_BLOCKREPRESENTATION_DATA" || n == "BLOCKREPRESENTATION")
        return "AcDbBlockRepresentationData";
    if (n == "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION" || n == "DYNAMICBLOCKPURGEPREVENTER")
        return "AcDbDynamicBlockPurgePreventer";
    if (n == "ACDB_DYNAMICBLOCKPROXYNODE" || n == "DYNAMICBLOCKPROXYNODE")
        return "AcDbDynamicBlockProxyNode";
    if (n == "BLOCKGRIPLOCATIONCOMPONENT")
        return "AcDbBlockGripExpr";
    if (n == "BLOCKALIGNMENTPARAMETER")
        return "AcDbBlockAlignmentParameter";
    if (n == "BLOCKALIGNMENTGRIP")
        return "AcDbBlockAlignmentGrip";
    if (n == "BLOCKBASEPOINTPARAMETER")
        return "AcDbBlockBasepointParameter";
    if (n == "BLOCKFLIPACTION")
        return "AcDbBlockFlipAction";
    if (n == "BLOCKFLIPPARAMETER")
        return "AcDbBlockFlipParameter";
    if (n == "BLOCKFLIPGRIP")
        return "AcDbBlockFlipGrip";
    if (n == "BLOCKLINEARGRIP")
        return "AcDbBlockLinearGrip";
    if (n == "BLOCKLOOKUPGRIP")
        return "AcDbBlockLookupGrip";
    if (n == "BLOCKROTATIONGRIP")
        return "AcDbBlockRotationGrip";
    if (n == "BLOCKMOVEACTION")
        return "AcDbBlockMoveAction";
    if (n == "BLOCKROTATEACTION")
        return "AcDbBlockRotationAction";
    if (n == "BLOCKSCALEACTION")
        return "AcDbBlockScaleAction";
    if (n == "BLOCKVISIBILITYGRIP")
        return "AcDbBlockVisibilityGrip";
    if (n == "BLOCKVISIBILITYPARAMETER")
        return "AcDbBlockVisibilityParameter";
    if (n == "BLOCKLINEARPARAMETER")
        return "AcDbBlockLinearParameter";
    if (n == "BLOCKROTATIONPARAMETER")
        return "AcDbBlockRotationParameter";
    if (n == "BLOCKXYPARAMETER")
        return "AcDbBlockXYParameter";
    if (n == "BLOCKPOLARPARAMETER")
        return "AcDbBlockPolarParameter";
    if (n == "BLOCKPOLARGRIP")
        return "AcDbBlockPolarGrip";
    if (n == "ACDBBLOCKPARAMDEPENDENCYBODY" || n == "BLOCKPARAMDEPENDENCYBODY")
        return "AcDbBlockParameterDependencyBody";
    if (n == "BLOCKALIGNEDCONSTRAINTPARAMETER")
        return "AcDbBlockAlignedConstraintParameter";
    if (n == "BLOCKANGULARCONSTRAINTPARAMETER")
        return "AcDbBlockAngularConstraintParameter";
    if (n == "BLOCKARRAYACTION")
        return "AcDbBlockArrayAction";
    if (n == "BLOCKDIAMETRICCONSTRAINTPARAMETER")
        return "AcDbBlockDiametricConstraintParameter";
    if (n == "BLOCKHORIZONTALCONSTRAINTPARAMETER")
        return "AcDbBlockHorizontalConstraintParameter";
    if (n == "BLOCKLINEARCONSTRAINTPARAMETER")
        return "AcDbBlockLinearConstraintParameter";
    if (n == "BLOCKRADIALCONSTRAINTPARAMETER")
        return "AcDbBlockRadialConstraintParameter";
    if (n == "BLOCKVERTICALCONSTRAINTPARAMETER")
        return "AcDbBlockVerticalConstraintParameter";
    if (n == "BLOCKLOOKUPACTION")
        return "AcDbBlockLookupAction";
    if (n == "BLOCKLOOKUPPARAMETER")
        return "AcDbBlockLookupParameter";
    if (n == "BLOCKPOINTPARAMETER")
        return "AcDbBlockPointParameter";
    if (n == "BLOCKPOLARSTRETCHACTION")
        return "AcDbBlockPolarStretchAction";
    if (n == "BLOCKSTRETCHACTION")
        return "AcDbBlockStretchAction";
    if (n == "BLOCKUSERPARAMETER")
        return "AcDbBlockUserParameter";
    if (n == "BLOCKXYGRIP")
        return "AcDbBlockXYGrip";
    if (n == "BLOCKPROPERTIESTABLE")
        return "AcDbBlockPropertiesTable";
    if (n == "BLOCKPROPERTIESTABLEGRIP")
        return "AcDbBlockPropertiesTableGrip";
    if (n == "ALIGNMENTPARAMETERENTITY")
        return "AcDbBlockAlignmentParameterEntity";
    if (n == "BASEPOINTPARAMETERENTITY")
        return "AcDbBlockBasepointParameterEntity";
    if (n == "FLIPPARAMETERENTITY")
        return "AcDbBlockFlipParameterEntity";
    if (n == "LINEARPARAMETERENTITY")
        return "AcDbBlockLinearParameterEntity";
    if (n == "POINTPARAMETERENTITY")
        return "AcDbBlockPointParameterEntity";
    if (n == "ROTATIONPARAMETERENTITY")
        return "AcDbBlockRotationParameterEntity";
    if (n == "VISIBILITYPARAMETERENTITY")
        return "AcDbBlockVisibilityParameterEntity";
    if (n == "XYPARAMETERENTITY")
        return "AcDbBlockXYParameterEntity";
    if (n == "BLOCKANGULARCONSTRAINTPARAMETERENTITY")
        return "AcDbBlockAngularConstraintParameterEntity";
    if (n == "FLIPGRIPENTITY")
        return "AcDbBlockFlipGripEntity";
    if (n == "LINEARGRIPENTITY")
        return "AcDbBlockLinearGripEntity";
    if (n == "POLARGRIPENTITY")
        return "AcDbBlockPolarGripEntity";
    if (n == "ROTATIONGRIPENTITY")
        return "AcDbBlockRotationGripEntity";
    if (n == "VISIBILITYGRIPENTITY")
        return "AcDbBlockVisibilityGripEntity";
    if (n == "XYGRIPENTITY")
        return "AcDbBlockXYGripEntity";
    if (is_dynamic_block_object_name(n))
        return "AcDbObject";
    return std::nullopt;
}
DynamicBlockKind dynamic_block_kind_from_name(std::string_view name) {
    auto n = ascii_upper(name);
    if (n == "ACDB_BLOCKREPRESENTATION_DATA" || n == "BLOCKREPRESENTATION" ||
        n == "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION" || n == "DYNAMICBLOCKPURGEPREVENTER")
        return DynamicBlockKind::Representation;
    if (n == "DYNAMICBLOCKPROXYNODE" || n == "ACDB_DYNAMICBLOCKPROXYNODE")
        return DynamicBlockKind::ProxyNode;
    if (n == "BLOCKGRIPLOCATIONCOMPONENT")
        return DynamicBlockKind::GripLocationComponent;
    if (n == "BLOCKALIGNMENTGRIP")
        return DynamicBlockKind::AlignmentGrip;
    if (n == "BLOCKFLIPGRIP")
        return DynamicBlockKind::FlipGrip;
    if (n == "BLOCKLINEARGRIP")
        return DynamicBlockKind::LinearGrip;
    if (n == "BLOCKLOOKUPGRIP")
        return DynamicBlockKind::LookupGrip;
    if (n == "BLOCKPOLARGRIP")
        return DynamicBlockKind::PolarGrip;
    if (n == "BLOCKROTATIONGRIP")
        return DynamicBlockKind::RotationGrip;
    if (n == "BLOCKVISIBILITYGRIP")
        return DynamicBlockKind::VisibilityGrip;
    if (n == "BLOCKXYGRIP")
        return DynamicBlockKind::XYGrip;
    if (n == "BLOCKPROPERTIESTABLEGRIP")
        return DynamicBlockKind::PropertiesTableGrip;
    if (n == "BLOCKALIGNMENTPARAMETER")
        return DynamicBlockKind::AlignmentParameter;
    if (n == "BLOCKBASEPOINTPARAMETER")
        return DynamicBlockKind::BasePointParameter;
    if (n == "BLOCKFLIPPARAMETER")
        return DynamicBlockKind::FlipParameter;
    if (n == "BLOCKLINEARPARAMETER")
        return DynamicBlockKind::LinearParameter;
    if (n == "BLOCKLOOKUPPARAMETER")
        return DynamicBlockKind::LookupParameter;
    if (n == "BLOCKPOINTPARAMETER")
        return DynamicBlockKind::PointParameter;
    if (n == "BLOCKPOLARPARAMETER")
        return DynamicBlockKind::PolarParameter;
    if (n == "BLOCKROTATIONPARAMETER")
        return DynamicBlockKind::RotationParameter;
    if (n == "BLOCKUSERPARAMETER")
        return DynamicBlockKind::UserParameter;
    if (n == "BLOCKVISIBILITYPARAMETER")
        return DynamicBlockKind::VisibilityParameter;
    if (n == "BLOCKXYPARAMETER")
        return DynamicBlockKind::XYParameter;
    if (n == "BLOCKANGULARCONSTRAINTPARAMETER")
        return DynamicBlockKind::AngularConstraintParameter;
    if (n == "BLOCKDIAMETRICCONSTRAINTPARAMETER")
        return DynamicBlockKind::DiametricConstraintParameter;
    if (n == "BLOCKRADIALCONSTRAINTPARAMETER")
        return DynamicBlockKind::RadialConstraintParameter;
    if (n == "BLOCKALIGNEDCONSTRAINTPARAMETER")
        return DynamicBlockKind::AlignedConstraintParameter;
    if (n == "BLOCKLINEARCONSTRAINTPARAMETER")
        return DynamicBlockKind::LinearConstraintParameter;
    if (n == "BLOCKHORIZONTALCONSTRAINTPARAMETER")
        return DynamicBlockKind::HorizontalConstraintParameter;
    if (n == "BLOCKVERTICALCONSTRAINTPARAMETER")
        return DynamicBlockKind::VerticalConstraintParameter;
    if (n == "ACDBBLOCKPARAMDEPENDENCYBODY" || n == "BLOCKPARAMDEPENDENCYBODY")
        return DynamicBlockKind::ParameterDependencyBody;
    if (n == "BLOCKMOVEACTION")
        return DynamicBlockKind::MoveAction;
    if (n == "BLOCKFLIPACTION")
        return DynamicBlockKind::FlipAction;
    if (n == "BLOCKROTATEACTION")
        return DynamicBlockKind::RotateAction;
    if (n == "BLOCKSCALEACTION")
        return DynamicBlockKind::ScaleAction;
    if (n == "BLOCKARRAYACTION")
        return DynamicBlockKind::ArrayAction;
    if (n == "BLOCKLOOKUPACTION")
        return DynamicBlockKind::LookupAction;
    if (n == "BLOCKSTRETCHACTION")
        return DynamicBlockKind::StretchAction;
    if (n == "BLOCKPOLARSTRETCHACTION")
        return DynamicBlockKind::PolarStretchAction;
    if (n == "BLOCKPROPERTIESTABLE")
        return DynamicBlockKind::PropertiesTable;
    if (n == "BLOCKANGULARCONSTRAINTPARAMETERENTITY")
        return DynamicBlockKind::AngularConstraintParameterEntity;
    if (is_dynamic_block_entity_name(n))
        return DynamicBlockKind::EmptyEntity;
    return DynamicBlockKind::Unknown;
}

namespace {
#include "dynamic_block_dxf.inc"
}

void DynamicBlockObject::visit_handles(const std::function<void(std::uint64_t &)> &visit) {
    visit(owner);
    for (auto &reactor : reactors)
        visit(reactor);
    if (xdictionary_handle)
        visit(*xdictionary_handle);
    visit_dynamic_data(data, visit);
}
void DynamicBlockObject::visit_handles(const std::function<void(std::uint64_t)> &visit) const {
    auto copy = *this;
    copy.visit_handles([&](std::uint64_t &handle) { visit(handle); });
}
bool DynamicBlockObject::references_handle(std::uint64_t target) const {
    bool found = false;
    visit_handles([&](std::uint64_t handle) {
        if (handle == target)
            found = true;
    });
    return found;
}
std::vector<std::uint64_t> DynamicBlockObject::referenced_handles() const {
    std::vector<std::uint64_t> handles;
    visit_handles([&](std::uint64_t handle) {
        if (handle)
            handles.push_back(handle);
    });
    return handles;
}
void DynamicBlockObject::remap_handles(const std::unordered_map<std::uint64_t, std::uint64_t> &map) {
    visit_handles([&](std::uint64_t &handle) {
        auto found = map.find(handle);
        if (found != map.end())
            handle = found->second;
    });
}

class DynamicBlockObjectReader {
  public:
    static DynamicBlockObject parse(const DxfDocument &doc, const DxfRecord &record) {
        DynamicDxfFields fields;
        std::string section;
        std::string group;
        bool owner_seen = false;
        std::uint64_t handle = 0, owner = 0;
        std::vector<std::uint64_t> reactors;
        std::optional<std::uint64_t> xdictionary;
        for (const auto &pair : record.pairs_) {
            auto value = detail::is_string_value(pair.code)
                             ? record.decode_string(pair.value)
                             : (record.binary_ ? detail::text_value_from_binary(pair.code, pair.value)
                                               : pair.value);
            if (pair.code == 0)
                continue;
            if (pair.code == 5) {
                handle = h_of(value);
                continue;
            }
            if (pair.code == 102) {
                group = value;
                if (group == "}")
                    group.clear();
                continue;
            }
            if (pair.code == 330 && group == "{ACAD_REACTORS") {
                reactors.push_back(h_of(value));
                continue;
            }
            if (pair.code == 360 && group == "{ACAD_XDICTIONARY") {
                xdictionary = h_of(value);
                continue;
            }
            if (pair.code == 330 && !owner_seen && group.empty() && section.empty()) {
                owner = h_of(value);
                owner_seen = true;
                continue;
            }
            if (pair.code == 100) {
                section = value;
                continue;
            }
            fields.sections[section].push_back({pair.code, std::move(value)});
        }
        if (!handle)
            handle = h_of(record.handle());
        if (!owner)
            owner = h_of(record.owner_handle());
        (void)doc;
        auto object = parse_dynamic_object(record, handle, owner, reactors, xdictionary, fields);
        object.is_entity = is_dynamic_block_entity_name(object.dxf_name);
        object.layer = fields.text("AcDbEntity", 8);
        if (object.layer.empty())
            object.layer = "0";
        object.paper_space = fields.i16("AcDbEntity", 67) != 0;
        if (object.is_entity && object.cpp_class_name == "AcDbObject")
            object.cpp_class_name = dynamic_block_cpp_class_name(object.dxf_name).value_or("AcDbEntity");
        return object;
    }
    static void encode(DxfDocument &, DxfRecord &record, const DynamicBlockObject &object) {
        auto fields = encode_dynamic_fields(object);
        std::vector<DxfRecord::Pair> pairs;
        pairs.reserve(fields.size());
        for (const auto &field : fields) {
            auto binary = detail::binary_value_from_text(field.code, field.value);
            pairs.push_back({field.code, detail::is_string_value(field.code)
                                             ? record.encode_string(field.value)
                                             : (record.binary_ ? std::move(binary) : field.value)});
        }
        record.pairs_ = std::move(pairs);
    }
    static DxfRecord &mutable_record(DxfDocument &doc, std::uint64_t id) {
        auto found = doc.handle_index_.find(id);
        if (found == doc.handle_index_.end() || found->second.size() != 1 || found->second[0].vertex ||
            found->second[0].seqend)
            throw std::invalid_argument("Dynamic edit requires a unique object");
        return std::visit([](DxfRecord &record) -> DxfRecord & { return record; },
                          doc.records_[found->second[0].record]);
    }
    static std::vector<const DxfRecord *> dynamic_records(const DxfDocument &doc, bool entities) {
        std::vector<const DxfRecord *> records;
        for (std::size_t i = 0; i < doc.records_.size(); ++i) {
            const auto &record = doc.record_at({i, std::nullopt, false});
            if (entities ? is_dynamic_block_entity_name(record.type())
                         : is_dynamic_block_object_name(record.type()))
                records.push_back(&record);
        }
        return records;
    }
    static void ensure_class(DxfDocument &copy, const DynamicBlockObject &object, bool entity) {
        auto cpp = dynamic_block_cpp_class_name(object.dxf_name).value_or(object.cpp_class_name);
        bool registered = false;
        for (const auto &entry : copy.classes())
            if (entry.name == object.dxf_name) {
                if (entry.is_entity != entity)
                    throw std::invalid_argument("Conflicting dynamic CLASS");
                registered = true;
                break;
            }
        if (!registered) {
            ClassDefinition definition;
            definition.name = object.dxf_name;
            definition.cpp_name = cpp;
            definition.is_entity = entity;
            copy.set_class(definition);
        }
    }
    static bool rewrite_ok(const DynamicBlockObject &object) {
        if (object.kind == DynamicBlockKind::Unknown)
            return false;
        if (object.kind == DynamicBlockKind::EmptyEntity)
            return true;
        return !std::holds_alternative<std::monostate>(object.data);
    }
};

std::optional<DynamicBlockObject> DxfDocument::dynamic_object(std::uint64_t handle) const {
    if (!handle)
        return std::nullopt;
    auto record = get_record(hex(handle));
    if (!record || !is_dynamic_block_object_name(record->type()))
        return std::nullopt;
    return DynamicBlockObjectReader::parse(*this, *record);
}
void DxfDocument::update_dynamic_object(const DynamicBlockObject &object) {
    if (!DynamicBlockObjectReader::rewrite_ok(object) || object.is_entity ||
        is_dynamic_block_entity_name(object.dxf_name))
        throw std::invalid_argument("Cannot rewrite unknown dynamic object");
    if (!object.handle || object.dxf_name.empty())
        throw std::invalid_argument("Dynamic object requires an identity");
    auto copy = *this;
    auto &record = DynamicBlockObjectReader::mutable_record(copy, object.handle);
    if (!is_dynamic_block_object_name(record.type()))
        throw std::invalid_argument("Record is not a dynamic object");
    if (record.type() != object.dxf_name)
        throw std::invalid_argument("Dynamic object name mismatch");
    if (h_of(record.handle()) != object.handle)
        throw std::invalid_argument("Dynamic object handle mismatch");
    DynamicBlockObjectReader::encode(copy, record, object);
    DynamicBlockObjectReader::ensure_class(copy, object, false);
    *this = std::move(copy);
}
std::vector<std::uint64_t> DxfDocument::dynamic_object_references(std::uint64_t handle) const {
    auto object = dynamic_object(handle);
    if (!object)
        return {};
    return object->referenced_handles();
}
std::vector<std::uint64_t> DxfDocument::dynamic_objects_referencing(std::uint64_t target) const {
    std::vector<std::uint64_t> handles;
    for (auto *record : DynamicBlockObjectReader::dynamic_records(*this, false)) {
        auto object = DynamicBlockObjectReader::parse(*this, *record);
        if (object.references_handle(target))
            handles.push_back(object.handle);
    }
    std::sort(handles.begin(), handles.end());
    handles.erase(std::unique(handles.begin(), handles.end()), handles.end());
    return handles;
}
void DxfDocument::remap_dynamic_handles(const std::unordered_map<std::uint64_t, std::uint64_t> &map) {
    auto copy = *this;
    std::vector<DynamicBlockObject> objects;
    for (auto *record : DynamicBlockObjectReader::dynamic_records(copy, false)) {
        auto object = DynamicBlockObjectReader::parse(copy, *record);
        if (object.kind == DynamicBlockKind::Unknown)
            continue;
        object.remap_handles(map);
        objects.push_back(std::move(object));
    }
    for (const auto &object : objects)
        DynamicBlockObjectReader::encode(copy, DynamicBlockObjectReader::mutable_record(copy, object.handle),
                                         object);
    *this = std::move(copy);
}
std::optional<DynamicBlockObject> DxfDocument::dynamic_entity(std::uint64_t handle) const {
    if (!handle)
        return std::nullopt;
    auto record = get_record(hex(handle));
    if (!record || !is_dynamic_block_entity_name(record->type()))
        return std::nullopt;
    return DynamicBlockObjectReader::parse(*this, *record);
}
void DxfDocument::update_dynamic_entity(const DynamicBlockObject &entity) {
    if (!DynamicBlockObjectReader::rewrite_ok(entity) ||
        (!entity.is_entity && !is_dynamic_block_entity_name(entity.dxf_name)))
        throw std::invalid_argument("Cannot rewrite unknown dynamic entity");
    if (!entity.handle || entity.dxf_name.empty())
        throw std::invalid_argument("Dynamic entity requires an identity");
    auto copy = *this;
    auto &record = DynamicBlockObjectReader::mutable_record(copy, entity.handle);
    if (!is_dynamic_block_entity_name(record.type()))
        throw std::invalid_argument("Record is not a dynamic entity");
    if (record.type() != entity.dxf_name)
        throw std::invalid_argument("Dynamic entity name mismatch");
    if (h_of(record.handle()) != entity.handle)
        throw std::invalid_argument("Dynamic entity handle mismatch");
    auto staged = entity;
    staged.is_entity = true;
    DynamicBlockObjectReader::encode(copy, record, staged);
    DynamicBlockObjectReader::ensure_class(copy, staged, true);
    *this = std::move(copy);
}
std::uint64_t DxfDocument::insert_dynamic_object(DynamicBlockObject object) {
    object.is_entity = false;
    if (object.dxf_name.empty() || is_dynamic_block_entity_name(object.dxf_name) ||
        !is_dynamic_block_object_name(object.dxf_name) || !DynamicBlockObjectReader::rewrite_ok(object))
        throw std::invalid_argument("Cannot insert unknown dynamic object");
    auto copy = *this;
    object.handle = 0;
    if (object.cpp_class_name.empty())
        object.cpp_class_name = dynamic_block_cpp_class_name(object.dxf_name).value_or("AcDbObject");
    DxfRecord record;
    record.binary_ = copy.binary_;
    if (!copy.records_.empty())
        record.code_page_ =
            std::visit([](const DxfRecord &item) { return item.code_page_; }, copy.records_.front());
    DynamicBlockObjectReader::encode(copy, record, object);
    DynamicBlockObjectReader::ensure_class(copy, object, false);
    auto id = copy.insert_object(std::move(record));
    *this = std::move(copy);
    return id;
}
std::uint64_t DxfDocument::insert_dynamic_entity(DynamicBlockObject entity, const EntityOptions &options) {
    entity.is_entity = true;
    if (entity.dxf_name.empty() || !is_dynamic_block_entity_name(entity.dxf_name) ||
        !DynamicBlockObjectReader::rewrite_ok(entity))
        throw std::invalid_argument("Cannot insert unknown dynamic entity");
    auto copy = *this;
    entity.handle = 0;
    if (entity.layer.empty())
        entity.layer = options.layer.empty() ? "0" : options.layer;
    if (entity.cpp_class_name.empty())
        entity.cpp_class_name =
            dynamic_block_cpp_class_name(entity.dxf_name).value_or("AcDbEntity");
    entity.paper_space = options.space == EntitySpace::Paper;
    DxfSemanticEntity record;
    record.version_ = copy.version_;
    record.binary_ = copy.binary_;
    if (!copy.records_.empty())
        record.code_page_ =
            std::visit([](const DxfRecord &item) { return item.code_page_; }, copy.records_.front());
    DynamicBlockObjectReader::encode(copy, record, entity);
    DynamicBlockObjectReader::ensure_class(copy, entity, true);
    auto id = copy.insert_entity(std::move(record), options);
    *this = std::move(copy);
    return id;
}
} // namespace libopencad
