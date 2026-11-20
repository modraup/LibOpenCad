// SPDX-License-Identifier: MPL-2.0
// Port of acadrust io/dxf/writer/r12.rs (initial acad0df2, geometry 0c3ae80d):
// names, legacy tables, primitives/compound records and geometric fallbacks.
#include "dxf_approximation.hpp"
#include "dxf_binary.hpp"
#include "dxf_code_page.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <libopencad/dxf_document.hpp>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
namespace libopencad {
namespace {
using Fields = std::vector<DxfRawField>;
std::string upper(std::string name) {
    for (auto &c : name)
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
    return name;
}
std::string sanitize(std::string_view name) {
    std::string result;
    for (auto c : detail::utf8_scalars(name)) {
        if (result.size() == 31)
            break;
        result += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$' ||
                   c == '-' || c == '_')
                      ? char(c)
                      : '_';
    }
    return result.empty() ? "_" : result;
}
bool space_block(const std::string &name) {
    auto n = upper(name);
    return n == "*MODEL_SPACE" || n == "$MODEL_SPACE" || n.rfind("*PAPER_SPACE", 0) == 0 ||
           n == "$PAPER_SPACE";
}
bool pseudo(const std::string &s) { return upper(s) == "BYLAYER" || upper(s) == "BYBLOCK"; }
std::string value(const Fields &f, int code, std::string fallback = {}) {
    for (const auto &p : f)
        if (p.code == code)
            return p.value;
    return fallback;
}
double real(const Fields &f, int code, double fallback = 0) {
    const auto text = value(f, code);
    if (text.empty())
        return fallback;
    return detail::binary_double(detail::binary_value_from_text(40, text));
}
int integer_value(const Fields &f, int code, int fallback = 0) {
    const auto text = value(f, code);
    return text.empty() ? fallback : detail::binary_short(detail::binary_value_from_text(70, text));
}
std::string decimal(double v) {
    if (!std::isfinite(v))
        throw std::invalid_argument("Non-finite R12 numeric value");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << v;
    return out.str();
}
std::string transport(std::string s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '^':
            out += "^ ";
            break;
        case '\n':
            out += "^J";
            break;
        case '\r':
            out += "^M";
            break;
        case '\t':
            out += "^I";
            break;
        case '\0':
            throw std::invalid_argument("Null byte in R12 string");
        default:
            out += c;
        }
    }
    return out;
}
struct NameMap {
    std::map<std::string, std::string> names;
    std::set<std::string> used;
    std::string add(std::string source, bool anonymous = false) {
        const auto key = upper(source);
        if (auto i = names.find(key); i != names.end())
            return i->second;
        auto base = anonymous && !source.empty() && source[0] == '*' ? "*" + sanitize(source.substr(1))
                                                                     : sanitize(source);
        if (base.size() > 31)
            base.resize(31);
        auto candidate = base;
        for (std::size_t i = 1; used.count(upper(candidate)); ++i) {
            const auto suffix = "_" + std::to_string(i);
            candidate = base.substr(0, 31 - std::min<std::size_t>(31, suffix.size())) + suffix;
        }
        names.emplace(key, candidate);
        used.insert(upper(candidate));
        return candidate;
    }
    std::string get(const std::string &s, const std::string &fallback) const {
        const auto it = names.find(upper(s));
        return it == names.end() ? fallback : it->second;
    }
};
} // namespace
class R12Exporter {
    const DxfDocument &doc;
    R12ExportOptions options;
    R12ExportResult result;
    std::ostringstream output;
    const char *page = nullptr;
    std::string declaration = "ANSI_1252";
    std::map<std::string, NameMap> names;
    struct Located {
        const DxfRecord *record;
        const DxfCompoundEntity *compound;
        std::string section, table, block;
    };
    std::vector<Located> records;
    std::unordered_map<const DxfRecord *, const DxfLwPolyline *> lightweight;
    std::unordered_map<const DxfRecord *, const DxfSpline *> splines;
    std::unordered_map<const DxfRecord *, const DxfEllipse *> ellipses;
    std::unordered_map<const DxfRecord *, const DxfLeader *> leaders;
    std::unordered_map<const DxfRecord *, const DxfMText *> mtexts;
    std::unordered_map<const DxfRecord *, const DxfHatch *> hatches;
    std::unordered_map<const DxfRecord *, const DxfMultiLeader *> multileaders;
    std::size_t multileader(const DxfMultiLeader &r, bool paper);
    std::unordered_map<const DxfRecord *, const DxfMLine *> mlines;
    std::size_t mline(const DxfMLine &r, const Fields &f, bool paper);
    std::unordered_map<const DxfRecord *, const DxfDimension *> dimensions;
    std::size_t dimension_geometry(const DxfDimension &r, const Fields &f, bool paper);
    double reach = 1e6;
    std::unordered_map<const DxfRecord *, std::string> hatch_blocks;
    std::map<std::string, const DxfBlock *> blocks;
    std::set<std::string> xrefs;
    std::set<std::string> reported;
    void issue(R12IssueKind kind, const std::string &code, const DxfRecord *r, std::string message) {
        // Same record can be scanned twice (inventory and export); report each fact once.
        auto key = std::to_string(int(kind)) + code +
                   (r ? std::to_string(reinterpret_cast<std::uintptr_t>(r)) : "") + message;
        if (reported.insert(key).second)
            result.report.issues.push_back(
                {kind, code, r ? r->type() : "", r ? r->handle() : "", std::move(message)});
    }
    void pair(int code, std::string text) {
        if (detail::is_string_value(code))
            text = detail::to_legacy(transport(text), page);
        else
            detail::binary_value_from_text(code, text); // validate width, finiteness and transport
        if (text.size() + 32 > options.max_output_bytes ||
            static_cast<std::size_t>(output.tellp()) > options.max_output_bytes - text.size() - 32)
            throw std::length_error("R12 output exceeds max_output_bytes");
        output << std::setw(3) << code << "\r\n" << text << "\r\n";
    }
    void num(int code, double v) { pair(code, decimal(v)); }
    void point(int code, Point3 p) {
        num(code, p.x);
        num(code + 10, p.y);
        num(code + 20, p.z);
    }
    void section(std::string s) {
        pair(0, "SECTION");
        pair(2, std::move(s));
    }
    void table(std::string s, std::size_t n) {
        if (n > 32767)
            throw std::invalid_argument("R12 table exceeds signed 16-bit count");
        pair(0, "TABLE");
        pair(2, std::move(s));
        pair(70, std::to_string(n));
    }
    Fields fields(const DxfRecord &r, bool audit = true) {
        Fields out;
        int private_depth = 0;
        bool embedded = false, xdata = false;
        for (const auto &p : r.pairs_) {
            if (p.code >= 1000 && private_depth == 0) {
                xdata = true;
                if (audit)
                    issue(R12IssueKind::Loss, "xdata-removed", &r, "R12 writer does not export XDATA");
            }
            if (xdata)
                continue;
            if (p.code == 101 && private_depth == 0) {
                embedded = true;
                if (audit)
                    issue(R12IssueKind::Loss, "embedded-data-removed", &r,
                          "Embedded object data is not exported");
            }
            if (embedded)
                continue;
            if (p.code == 102) {
                if (audit)
                    issue(R12IssueKind::Loss, "application-data-removed", &r,
                          "Private application/reactor/extension groups are removed");
                if (!p.value.empty() && p.value.front() == '{')
                    ++private_depth;
                else if (p.value == "}") {
                    if (!private_depth)
                        throw std::invalid_argument("Unbalanced private group");
                    --private_depth;
                }
                continue;
            }
            if (private_depth)
                continue;
            if (p.code == 0 || p.code == 5 || p.code == 105 || p.code == 100 || p.code == 330)
                continue;
            if (!detail::is_string_value(p.code) && !r.binary_)
                detail::binary_value_from_text(p.code, p.value);
            out.push_back({p.code, detail::is_string_value(p.code) ? r.decode_string(p.value)
                                   : r.binary_ ? detail::text_value_from_binary(p.code, p.value)
                                               : p.value});
        }
        if (private_depth)
            throw std::invalid_argument("Unclosed private group");
        return out;
    }
    void flags_changed(const DxfRecord &r, int code, int before, int after) {
        if (before != after)
            issue(R12IssueKind::Loss, "flags-normalized", &r,
                  "Group " + std::to_string(code) + ": " + std::to_string(before) + " -> " +
                      std::to_string(after));
    }
    double normalized(const DxfRecord *r, int code, double before, double after) {
        if (before != after)
            issue(R12IssueKind::Loss, "value-normalized", r,
                  "Group " + std::to_string(code) + ": " + decimal(before) + " -> " + decimal(after));
        return after;
    }
    void sequence_ownership(const DxfCompoundEntity &parent) {
        auto check = [&](const DxfRecord &child) {
            auto owner = upper(child.owner_handle());
            if (!owner.empty() && owner != "0" && owner != upper(parent.handle()))
                throw std::invalid_argument("Compound child has a foreign owner");
        };
        for (const auto &child : parent.children_)
            check(child);
        if (parent.seqend())
            check(*parent.seqend());
    }
    void omitted_fields(const DxfRecord &r, const Fields &f, const std::set<int> &allowed) {
        std::set<int> omitted;
        for (const auto &p : f)
            if (!allowed.count(p.code))
                omitted.insert(p.code);
        if (!omitted.empty()) {
            std::string message = "Removed DXF groups:";
            for (auto c : omitted)
                message += " " + std::to_string(c);
            issue(R12IssueKind::Loss, "fields-removed", &r, message);
        }
    }
    std::string mapped(const std::string &table_name, const std::string &source,
                       const std::string &fallback) {
        return names[table_name].get(source, fallback);
    }
    void map_name(const std::string &table_name, const std::string &source, bool anonymous = false) {
        auto target = names[table_name].add(source, anonymous);
        result.report.names.push_back({table_name, source, target});
        if (source != target) {
            issue(R12IssueKind::Loss, "symbol-renamed", nullptr,
                  table_name + ": " + source + " -> " + target);
        }
    }
    std::vector<const DxfRecord *> entries(const std::string &t) {
        std::vector<const DxfRecord *> out;
        for (const auto &r : records)
            if (r.section == "TABLES" && r.table == t && r.record->type() == t)
                out.push_back(r.record);
        return out;
    }
    Color color(const Fields &f) {
        if (!value(f, 420).empty()) {
            auto bits = detail::as_i32_bits(detail::binary_value_from_text(420, value(f, 420)), true);
            return Color::from_true_color_value(*bits);
        }
        return Color::from_index(static_cast<std::int16_t>(integer_value(f, 62, 256)));
    }
    void common(const DxfRecord &r, const Fields &f, bool paper) {
        auto layer = value(f, 8, "0");
        if (layer.empty())
            layer = "0";
        if (!names["LAYER"].names.count(upper(layer)))
            issue(R12IssueKind::Error, "missing-layer", &r, "Unresolved layer " + layer);
        pair(8, mapped("LAYER", layer, sanitize(layer)));
        const auto lt = value(f, 6);
        if (upper(lt) == "BYBLOCK")
            pair(6, "BYBLOCK");
        else if (!lt.empty() && upper(lt) != "BYLAYER") {
            const auto mapped_lt = mapped("LTYPE", lt, "");
            if (mapped_lt.empty())
                issue(R12IssueKind::Error, "missing-linetype", &r, "Unresolved linetype " + lt);
            else
                pair(6, mapped_lt);
        }
        const auto c = color(f);
        if (c.kind() == Color::Kind::Rgb)
            issue(R12IssueKind::Loss, "true-color-to-aci", &r, "RGB is approximated by an ACI palette index");
        if (c.kind() != Color::Kind::ByLayer && c.kind() != Color::Kind::None)
            pair(62, std::to_string(c.approximate_index()));
        if (paper)
            pair(67, "1");
    }
    void entity_fields(const DxfRecord &r, const Fields &f, const std::set<int> &geometric, bool paper) {
        pair(0, r.type());
        common(r, f, paper);
        for (auto p : f)
            if (geometric.count(p.code)) {
                if (p.code == 7) {
                    if (!p.value.empty() && !names["STYLE"].names.count(upper(p.value)))
                        issue(R12IssueKind::Error, "missing-style", &r, "Unresolved text style " + p.value);
                    p.value = mapped("STYLE", p.value, "STANDARD");
                }
                pair(p.code, p.value);
            }
        auto allowed = geometric;
        allowed.insert({8, 6, 62, 420, 67});
        omitted_fields(r, f, allowed);
    }
    void unsupported(const DxfRecord &r, const std::string &why) {
        const bool omit = options.allow_loss && options.omit_unsupported_entities;
        issue(omit ? R12IssueKind::Loss : R12IssueKind::Error, "unsupported-entity", &r, why);
        ++result.report.omitted_entities;
    }
    std::size_t mesh_faces(const DxfRecord& r, const Fields& common_fields, bool paper) {
        const auto body = r.subclass_fields("AcDbSubDMesh");
        auto start = std::find_if(body.begin(), body.end(), [](const auto& p) { return p.code == 92; });
        std::size_t pos = start - body.begin();
        auto take = [&](int code) -> const std::string& {
            if (pos >= body.size() || body[pos].code != code)
                throw std::invalid_argument("Malformed subdivision mesh");
            return body[pos++].value;
        };
        auto count = [&](int code) {
            const auto n = detail::binary_int32(detail::binary_value_from_text(90, take(code)));
            if (n < 0) throw std::invalid_argument("Negative mesh count or index");
            return static_cast<std::size_t>(n);
        };
        auto coordinate = [&](int code) {
            const auto value=detail::binary_double(detail::binary_value_from_text(40,take(code)));
            if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite mesh coordinate or crease");
            return value;
        };
        const auto n = count(92);
        if (n > (body.size()-pos)/3) throw std::invalid_argument("Truncated mesh vertices");
        std::vector<Point3> vertices;
        vertices.reserve(n);
        for (std::size_t i=0; i<n; ++i) vertices.push_back({coordinate(10), coordinate(20), coordinate(30)});
        const auto size = count(93);
        if (size > body.size()-pos) throw std::invalid_argument("Truncated mesh face list");
        const auto end = pos + size;
        std::size_t triangles = 0;
        while (pos < end) {
            const auto sides = count(90);
            if (sides > end-pos) throw std::invalid_argument("Truncated mesh face");
            Point3 first{}, previous{};
            for (std::size_t j=0; j<sides; ++j) {
                const auto index = count(90);
                if (index >= n) throw std::invalid_argument("Mesh face index outside vertex array");
                const auto& current = vertices[index];
                if (j == 0) first = current;
                if (j >= 2) {
                    pair(0,"3DFACE");
                    common(r,common_fields,paper);
                    point(10,first); point(11,previous); point(12,current); point(13,current);
                    ++triangles;
                }
                previous = current;
            }
        }
        if(pos<body.size() && body[pos].code==94) {
            const auto edges=count(94);
            if(edges>(body.size()-pos)/2) throw std::invalid_argument("Truncated mesh edge list");
            for(std::size_t i=0;i<edges;++i) {
                const auto a=count(90), b=count(90);
                if(a>=n || b>=n) throw std::invalid_argument("Mesh edge index outside vertex array");
            }
            if(pos<body.size() && body[pos].code==95) {
                const auto creases=count(95);
                if(creases>edges || creases>body.size()-pos) throw std::invalid_argument("Invalid mesh crease count");
                for(std::size_t i=0;i<creases;++i) coordinate(140);
            }
        }
        if(pos<body.size() && body[pos].code==90 && count(90)!=0)
            throw std::invalid_argument("Unsupported mesh subentity overrides");
        if(pos!=body.size()) throw std::invalid_argument("Unexpected subdivision mesh tail");
        if (!triangles) unsupported(r,"Subdivision mesh has no drawable faces");
        else issue(R12IssueKind::Loss, "mesh-triangulated", &r,
                   "Subdivision mesh control faces replaced by triangle fans; crease and subdivision properties lost");
        return triangles;
    }
    void sampled_polyline(const DxfRecord &r, const Fields &f, bool paper, std::vector<Point3> points,
                          bool closed, Point3 normal) {
        if (points.size() < 2)
            throw std::invalid_argument("Curve has fewer than two sample points");
        bool flat =
            (normal - Point3{0, 0, 1}).length() < 1e-9 &&
            std::all_of(
                points.begin(), points.end(), [&](auto p) { return std::abs(p.z - points[0].z) < 1e-9; });
        pair(0, "POLYLINE");
        common(r, f, paper);
        pair(66, "1");
        point(10, {0, 0, flat ? points[0].z : 0});
        pair(70, std::to_string((closed ? 1 : 0) | (flat ? 0 : 8)));
        for (auto p : points) {
            pair(0, "VERTEX");
            pair(8, mapped("LAYER", value(f, 8, "0"), "0"));
            if (paper)
                pair(67, "1");
            point(10, p);
            pair(70, flat ? "0" : "32");
        }
        pair(0, "SEQEND");
        pair(8, mapped("LAYER", value(f, 8, "0"), "0"));
        if (paper)
            pair(67, "1");
    }
    void text_piece(const DxfRecord &r, Fields f, bool paper, const detail::ExplodedText &piece) {
        const auto &t = piece.text;
        if (piece.color) {
            f.erase(std::remove_if(f.begin(), f.end(), [](auto &p) { return p.code == 62 || p.code == 420; }),
                    f.end());
            f.push_back({62, std::to_string(piece.color->approximate_index())});
        }
        pair(0, "TEXT");
        common(r, f, paper);
        point(10, t.insertion);
        num(40, t.height);
        pair(1, t.text);
        num(50, t.rotation * 180 / std::acos(-1.));
        num(41, t.width_factor);
        num(51, t.oblique_angle * 180 / std::acos(-1.));
        if (!names["STYLE"].names.count(upper(t.style)))
            issue(R12IssueKind::Loss, "mtext-font-fallback", &r,
                  "Unregistered inline font replaced by STANDARD: " + t.style);
        pair(7, mapped("STYLE", t.style, "STANDARD"));
        pair(71, std::to_string(t.generation_flags));
        pair(72, std::to_string(t.horizontal_alignment));
        if (t.alignment_point)
            point(11, *t.alignment_point);
        pair(73, std::to_string(t.vertical_alignment));
        if (t.normal != Point3{0, 0, 1})
            point(210, t.normal);
    }
    std::size_t hatch_geometry(const DxfRecord &r, const Fields &f, bool paper) {
        const auto definition = hatches.at(&r)->definition();
        auto geometry = detail::approximate_hatch(definition, options.max_output_bytes / 32);
        issue(R12IssueKind::Loss, "hatch-decomposed", &r,
              "Hatch replaced by upstream even-odd SOLID strips or clipped pattern LINE/POINT geometry; "
              "curves sampled, associativity, gradients and style semantics are lost");
        if (geometry.boundary_only)
            issue(R12IssueKind::Loss, "hatch-boundary-only", &r,
                  "Empty or excessively dense pattern replaced by its boundary");
        for (const auto &p : geometry.primitives) {
            pair(0, p.kind == detail::HatchPrimitive::Kind::Solid  ? "SOLID"
                    : p.kind == detail::HatchPrimitive::Kind::Line ? "LINE"
                                                                   : "POINT");
            common(r, f, paper);
            point(10, p.points[0]);
            if (p.kind != detail::HatchPrimitive::Kind::Point)
                point(11, p.points[1]);
            if (p.kind == detail::HatchPrimitive::Kind::Solid) {
                point(12, p.points[2]);
                point(13, p.points[3]);
                if (definition.normal != Point3{0, 0, 1})
                    point(210, definition.normal);
            }
        }
        return geometry.primitives.size();
    }
    void header();
    void tables();
    void entity(const Located &located, bool paper, bool count = true);
    void polyline(const Located &located, const Fields &f, bool paper);

  public:
    R12Exporter(const DxfDocument &source, const R12ExportOptions &opts) : doc(source), options(opts) {
        output.imbue(std::locale::classic());
    }
    R12ExportResult run();
};
#include "dxf_r12_multileader.inc"
#include "dxf_r12_mline.inc"
#include "dxf_r12_dimension.inc"
void R12Exporter::header() {
    section("HEADER");
    pair(9, "$ACADVER");
    pair(1, "AC1009");
    pair(9, "$DWGCODEPAGE");
    pair(3, declaration);
    const std::set<std::string> kept{
        "$INSBASE", "$EXTMIN",   "$EXTMAX", "$LIMMIN",    "$LIMMAX",   "$ORTHOMODE", "$FILLMODE",
        "$LTSCALE", "$TEXTSIZE", "$CLAYER", "$TEXTSTYLE", "$DIMSTYLE", "$DIMSCALE",  "$LUNITS",
        "$LUPREC",  "$AUNITS",   "$AUPREC", "$ANGBASE",   "$ANGDIR",   "$PDMODE",    "$PDSIZE"};
    for (const auto &l : records)
        if (l.section == "HEADER" && l.record->type() == "SECTION") {
            auto f = fields(*l.record, false);
            for (std::size_t i = 0; i < f.size(); ++i)
                if (f[i].code == 9) {
                    const auto variable = f[i].value;
                    const auto end =
                        std::find_if(f.begin() + i + 1, f.end(), [](const auto &p) { return p.code == 9; });
                    if (kept.count(variable)) {
                        pair(9, variable);
                        for (auto p = f.begin() + i + 1; p != end; ++p) {
                            auto text = p->value;
                            if (variable == "$CLAYER")
                                text = mapped("LAYER", text, "0");
                            if (variable == "$TEXTSTYLE")
                                text = mapped("STYLE", text, "STANDARD");
                            if (variable == "$DIMSTYLE")
                                text = mapped("DIMSTYLE", text, "STANDARD");
                            if (variable == "$LTSCALE" || variable == "$DIMSCALE" ||
                                variable == "$TEXTSIZE") {
                                const auto n = real(Fields{*p}, p->code);
                                if (n <= 0)
                                    text = variable == "$TEXTSIZE" ? "2.5" : "1";
                            }
                            if (variable == "$LUNITS" || variable == "$AUNITS" || variable == "$LUPREC" ||
                                variable == "$AUPREC") {
                                const int low = variable == "$LUNITS" ? 1 : 0,
                                          high = variable == "$LUNITS"   ? 5
                                                 : variable == "$AUNITS" ? 4
                                                                         : 8;
                                text =
                                    std::to_string(std::clamp(integer_value(Fields{*p}, p->code), low, high));
                            }
                            pair(p->code, text);
                        }
                    } else if (variable != "$ACADVER" && variable != "$DWGCODEPAGE" &&
                               variable != "$HANDSEED" && variable != "$HANDLING" &&
                               variable != "$ACADMAINTVER")
                        issue(R12IssueKind::Loss, "header-variable-removed", nullptr, variable);
                }
        }
    pair(9, "$HANDLING");
    pair(70, "0");
    pair(0, "ENDSEC");
}
void R12Exporter::tables() {
    section("TABLES");
    table("VPORT", 1);
    pair(0, "VPORT");
    pair(2, "*ACTIVE");
    pair(70, "0");
    const DxfRecord *active = nullptr;
    for (auto r : entries("VPORT")) {
        auto f = fields(*r);
        if (upper(value(f, 2)) == "*ACTIVE")
            active = r;
        else
            issue(R12IssueKind::Loss, "vport-removed", r, "Only *ACTIVE is retained");
    }
    if (active) {
        auto f = fields(*active);
        std::set<int> allowed{2,  70, 10, 20, 11, 21, 12, 22, 13, 23, 14, 24, 15, 25, 16, 26, 36, 17,
                              27, 37, 40, 41, 42, 43, 44, 50, 51, 71, 72, 73, 74, 75, 76, 77, 78};
        for (auto p : f)
            if (allowed.count(p.code) && p.code != 2 && p.code != 70) {
                if (p.code == 40 || p.code == 41 || p.code == 42)
                    if (real(Fields{p}, p.code) <= 0)
                        p.value = p.code == 42 ? "50" : "1";
                if (p.code == 71)
                    p.value = "0";
                if (p.code == 74)
                    p.value = "3";
                pair(p.code, p.value);
            }
        omitted_fields(*active, f, allowed);
    } else {
        point(16, {0, 0, 1});
        num(10, 0);
        num(20, 0);
        num(11, 1);
        num(21, 1);
        num(12, 0);
        num(22, 0);
        num(40, 100);
        num(41, 1.5);
        num(42, 50);
        pair(71, "0");
        pair(72, "100");
        pair(73, "1");
        pair(74, "3");
    }
    pair(0, "ENDTAB");
    auto ltypes = entries("LTYPE");
    ltypes.erase(std::remove_if(ltypes.begin(), ltypes.end(),
                                [&](auto r) { return pseudo(value(fields(*r, false), 2)); }),
                 ltypes.end());
    bool continuous = false;
    for (auto r : ltypes)
        continuous |= upper(value(fields(*r, false), 2)) == "CONTINUOUS";
    table("LTYPE", ltypes.size() + !continuous);
    if (!continuous) {
        pair(0, "LTYPE");
        pair(2, "CONTINUOUS");
        pair(70, "0");
        pair(3, "Solid line");
        pair(72, "65");
        pair(73, "0");
        num(40, 0);
    }
    for (auto r : ltypes) {
        auto f = fields(*r);
        pair(0, "LTYPE");
        pair(2, mapped("LTYPE", value(f, 2), "CONTINUOUS"));
        flags_changed(*r, 70, integer_value(f, 70), 0);
        pair(70, "0");
        pair(3, value(f, 3));
        pair(72, "65");
        std::vector<double> lengths;
        bool complex = false;
        for (const auto &p : f) {
            if (p.code == 49)
                lengths.push_back(real(Fields{p}, 49));
            if (p.code == 74 && integer_value(Fields{p}, 74) != 0)
                complex = true;
        }
        if (complex)
            issue(R12IssueKind::Loss, "complex-linetype-simplified", r,
                  "Text/shape segments become simple dash lengths");
        if (lengths.size() > 12) {
            issue(R12IssueKind::Loss, "linetype-truncated", r, "R12 keeps the first 12 pattern elements");
            lengths.resize(12);
        }
        pair(73, std::to_string(lengths.size()));
        double total = 0;
        for (auto n : lengths)
            total += std::abs(n);
        num(40, real(f, 40) > 0 ? real(f, 40) : total);
        for (auto n : lengths)
            num(49, n);
        omitted_fields(*r, f, {2, 3, 70, 72, 73, 40, 49, 74, 75, 340, 46, 50, 44, 45, 9});
    }
    pair(0, "ENDTAB");
    auto layers = entries("LAYER");
    bool zero = false;
    for (auto r : layers)
        zero |= value(fields(*r, false), 2) == "0";
    table("LAYER", layers.size() + !zero);
    if (!zero) {
        pair(0, "LAYER");
        pair(2, "0");
        pair(70, "0");
        pair(62, "7");
        pair(6, "CONTINUOUS");
    }
    for (auto r : layers) {
        auto f = fields(*r);
        auto c = color(f);
        auto index = c.kind() == Color::Kind::Rgb ? std::clamp(c.approximate_index(), 1, 255)
                                                  : std::abs(integer_value(f, 62, 7));
        if (index < 1 || index > 255)
            index = 7;
        if (c.kind() == Color::Kind::Rgb)
            issue(R12IssueKind::Loss, "true-color-to-aci", r, "Layer RGB is approximated by ACI");
        pair(0, "LAYER");
        pair(2, mapped("LAYER", value(f, 2), "0"));
        flags_changed(*r, 70, integer_value(f, 70), integer_value(f, 70) & 5);
        pair(70, std::to_string(integer_value(f, 70) & 5));
        pair(62, std::to_string(integer_value(f, 62, 7) < 0 ? -index : index));
        pair(6, mapped("LTYPE", value(f, 6), "CONTINUOUS"));
        omitted_fields(*r, f, {2, 70, 62, 420, 6});
    }
    pair(0, "ENDTAB");
    auto styles = entries("STYLE");
    styles.erase(std::remove_if(styles.begin(), styles.end(),
                                [&](auto r) {
                                    if (!value(fields(*r, false), 2).empty())
                                        return false;
                                    issue(R12IssueKind::Loss, "anonymous-style-removed", r,
                                          "Anonymous shape-file STYLE is not emitted by the R12 writer");
                                    return true;
                                }),
                 styles.end());
    bool standard = false;
    for (auto r : styles)
        standard |= upper(value(fields(*r, false), 2)) == "STANDARD";
    table("STYLE", styles.size() + !standard);
    if (!standard) {
        pair(0, "STYLE");
        pair(2, "STANDARD");
        pair(70, "0");
        num(40, 0);
        num(41, 1);
        num(50, 0);
        pair(71, "0");
        num(42, 2.5);
        pair(3, "txt");
        pair(4, "");
    }
    for (auto r : styles) {
        auto f = fields(*r);
        pair(0, "STYLE");
        pair(2, mapped("STYLE", value(f, 2), "STANDARD"));
        flags_changed(*r, 70, integer_value(f, 70), integer_value(f, 70) & 5);
        pair(70, std::to_string(integer_value(f, 70) & 5));
        num(40, real(f, 40));
        num(41, normalized(r, 41, real(f, 41, 1), real(f, 41, 1) > 0 ? real(f, 41, 1) : 1));
        num(50, real(f, 50));
        flags_changed(*r, 71, integer_value(f, 71), integer_value(f, 71) & 6);
        pair(71, std::to_string(integer_value(f, 71) & 6));
        num(42, normalized(r, 42, real(f, 42, 2.5), real(f, 42, 2.5) > 0 ? real(f, 42, 2.5) : 2.5));
        auto font = value(f, 3);
        if (font.empty()) {
            if (auto style = doc.get_text_style_by_handle(r->handle()))
                font = style->true_type_font;
            if (font.empty())
                font = "txt";
            issue(R12IssueKind::Loss, "font-fallback", r, "Empty SHX font replaced by " + font);
        }
        const auto font_upper = upper(font);
        if (font_upper.size() >= 4 && (font_upper.substr(font_upper.size() - 4) == ".TTF" ||
                                       font_upper.substr(font_upper.size() - 4) == ".OTF"))
            issue(R12IssueKind::Loss, "font-not-converted", r,
                  "Font filename is retained; no TrueType/OpenType to SHX conversion");
        pair(3, font);
        pair(4, value(f, 4));
        omitted_fields(*r, f, {2, 70, 40, 41, 50, 71, 42, 3, 4});
    }
    pair(0, "ENDTAB");
    for (auto t : {"VIEW", "UCS"}) {
        table(t, 0);
        pair(0, "ENDTAB");
        for (auto r : entries(t))
            issue(R12IssueKind::Loss, "table-entry-removed", r,
                  std::string(t) + " is emitted empty by the pinned R12 writer");
    }
    table("APPID", 1);
    pair(0, "APPID");
    pair(2, "ACAD");
    pair(70, "0");
    pair(0, "ENDTAB");
    for (auto r : entries("APPID"))
        if (value(fields(*r, false), 2) != "ACAD")
            issue(R12IssueKind::Loss, "appid-removed", r,
                  "Only ACAD is retained because XDATA is not exported");
    auto dims = entries("DIMSTYLE");
    standard = false;
    for (auto r : dims)
        standard |= upper(value(fields(*r, false), 2)) == "STANDARD";
    table("DIMSTYLE", dims.size() + !standard);
    if (!standard) {
        pair(0, "DIMSTYLE");
        pair(2, "STANDARD");
        pair(70, "0");
    }
    for (auto r : dims) {
        auto f = fields(*r);
        pair(0, "DIMSTYLE");
        pair(2, mapped("DIMSTYLE", value(f, 2), "STANDARD"));
        flags_changed(*r, 70, integer_value(f, 70), 0);
        pair(70, "0");
        std::set<int> allowed{2,   70,  3,   4,   40,  41,  42,  43,  44,  45,  46,  47, 48,
                              140, 141, 142, 143, 144, 145, 146, 147, 71,  72,  73,  74, 75,
                              76,  77,  78,  170, 171, 172, 173, 174, 175, 176, 177, 178};
        for (auto p : f)
            if (allowed.count(p.code) && p.code != 2 && p.code != 70) {
                if (p.code == 40 || p.code == 144 || p.code == 146) {
                    const auto n = real(Fields{p}, p.code);
                    p.value = decimal(normalized(r, p.code, n, (p.code == 144 ? n != 0 : n > 0) ? n : 1));
                }
                pair(p.code, p.value);
            }
        omitted_fields(*r, f, allowed);
    }
    pair(0, "ENDTAB");
    pair(0, "ENDSEC");
}
void R12Exporter::polyline(const Located &l, const Fields &f, bool paper) {
    const auto &r = *l.record;
    // Typed access is obtained from the document's variant, without RTTI on records.
    const auto found = lightweight.find(l.record);
    const DxfLwPolyline *light = found == lightweight.end() ? nullptr : found->second;
    pair(0, "POLYLINE");
    common(r, f, paper);
    pair(66, "1");
    if (light) {
        point(10, {0, 0, light->elevation()});
        const auto legacy_flags = (light->is_closed() ? 1 : 0) | (light->plinegen() ? 128 : 0);
        flags_changed(r, 70, integer_value(f, 70), legacy_flags);
        pair(70, std::to_string(legacy_flags));
        if (light->constant_width() != 0) {
            num(40, light->constant_width());
            num(41, light->constant_width());
        }
        if (light->thickness() != 0)
            num(39, light->thickness());
        auto n = light->normal();
        if (n.x != 0 || n.y != 0 || n.z != 1)
            point(210, n);
        for (const auto &v : light->vertices()) {
            pair(0, "VERTEX");
            pair(8, mapped("LAYER", value(f, 8, "0"), "0"));
            if (paper)
                pair(67, "1");
            point(10, {v.x, v.y, light->elevation()});
            if (v.start_width != 0 || v.end_width != 0) {
                num(40, v.start_width);
                num(41, v.end_width);
            }
            if (v.bulge != 0)
                num(42, v.bulge);
            pair(70, "0");
            if (v.vertex_id)
                issue(R12IssueKind::Loss, "vertex-id-removed", &r, "LWPOLYLINE vertex IDs are not exported");
        }
        omitted_fields(r, f, {8, 6, 62, 420, 67, 90, 70, 38, 39, 43, 210, 220, 230, 10, 20, 40, 41, 42, 91});
        ++result.report.converted_entities;
        issue(R12IssueKind::Information, "lwpolyline-to-polyline", &r,
              "Expanded to ordered VERTEX records with widths and closing bulge");
    } else {
        if (!l.compound || !l.compound->seqend())
            throw std::invalid_argument("POLYLINE is missing SEQEND");
        const auto flags = integer_value(f, 70);
        bool face = (flags & 64) != 0, mesh = !face && (flags & 16) != 0,
             three = !face && !mesh && (flags & 8) != 0;
        const int mask = face    ? 64
                         : mesh  ? ((flags & (1 | 4 | 32)) | 16)
                         : three ? ((flags & (1 | 4 | 128)) | 8)
                                 : (flags & (1 | 2 | 4 | 128));
        std::size_t coordinates = 0, faces = 0;
        if (face)
            for (const auto &child : l.compound->children_) {
                const auto cf = fields(child, false);
                const auto vf = integer_value(cf, 70) & 192;
                if (vf == 192)
                    ++coordinates;
                else if (vf == 128)
                    ++faces;
                else
                    throw std::invalid_argument("Unclassified Polyface VERTEX");
            }
        if (face) {
            for (const auto &child : l.compound->children_) {
                const auto cf = fields(child, false);
                if ((integer_value(cf, 70) & 192) != 128)
                    continue;
                for (int c = 71; c <= 74; ++c) {
                    const auto index = integer_value(cf, c);
                    if ((c < 74 && index == 0) || std::abs(index) > static_cast<int>(coordinates))
                        throw std::invalid_argument("Polyface index does not reference a coordinate vertex");
                }
            }
        }
        if (mesh && (integer_value(f, 71) <= 0 || integer_value(f, 72) <= 0 ||
                     static_cast<std::size_t>(integer_value(f, 71)) *
                             static_cast<std::size_t>(integer_value(f, 72)) !=
                         l.compound->children_.size()))
            throw std::invalid_argument("Polygon mesh M/N counts do not match its vertices");
        point(10, {0, 0, three ? 0 : real(f, 30)});
        flags_changed(r, 70, flags, mask);
        pair(70, std::to_string(mask));
        std::set<int> header_fields = three  ? std::set<int>{}
                                      : face ? std::set<int>{71, 72, 210, 220, 230}
                                      : mesh ? std::set<int>{71, 72, 73, 74, 210, 220, 230}
                                             : std::set<int>{39, 40, 41, 210, 220, 230};
        for (auto p : f)
            if (header_fields.count(p.code) && !(face && (p.code == 71 || p.code == 72)))
                pair(p.code, p.value);
        if (face) {
            if (integer_value(f, 71) != static_cast<int>(coordinates) ||
                integer_value(f, 72) != static_cast<int>(faces))
                issue(R12IssueKind::Loss, "mesh-counts-rebuilt", &r,
                      "Polyface counts rebuilt from coordinate/face records");
            pair(71, std::to_string(coordinates));
            pair(72, std::to_string(faces));
        }
        header_fields.insert({8, 6, 62, 420, 67, 66, 10, 20, 30, 70});
        omitted_fields(r, f, header_fields);
        for (const auto &child : l.compound->children_) {
            if (child.type() != "VERTEX")
                throw std::invalid_argument("Non-VERTEX child in POLYLINE");
            auto cf = fields(child);
            const int vf = integer_value(cf, 70);
            const bool face_record = face && (vf & 128) && !(vf & 64);
            pair(0, "VERTEX");
            pair(8, mapped("LAYER", value(f, 8, "0"), "0"));
            if (paper)
                pair(67, "1");
            point(10, face_record ? Point3{} : Point3{real(cf, 10), real(cf, 20), real(cf, 30)});
            int target_flags = face    ? (face_record ? 128 : 192)
                               : mesh  ? 64
                               : three ? ((vf & (1 | 8 | 16)) | 32)
                                       : (vf & (1 | 2 | 8 | 16));
            if (face_record) {
                const auto c = color(cf);
                if (c.kind() == Color::Kind::Rgb)
                    issue(R12IssueKind::Loss, "true-color-to-aci", &child, "Face RGB is approximated by ACI");
                if (c.kind() != Color::Kind::ByLayer && c.kind() != Color::Kind::None)
                    pair(62, std::to_string(c.approximate_index()));
            }
            flags_changed(child, 70, vf, target_flags);
            pair(70, std::to_string(target_flags));
            std::set<int> kept{8, 67, 10, 20, 30, 70};
            if (face_record)
                kept.insert({62, 420, 71, 72, 73, 74});
            else if (!face && !mesh && !three) {
                kept.insert({40, 41, 42});
                if (vf & 2)
                    kept.insert(50);
            }
            for (auto p : cf)
                if (kept.count(p.code) && p.code != 8 && p.code != 67 && p.code != 10 && p.code != 20 &&
                    p.code != 30 && p.code != 70 && p.code != 62 && p.code != 420)
                    pair(p.code, p.value);
            if (value(cf, 8, value(f, 8, "0")) != value(f, 8, "0"))
                issue(R12IssueKind::Loss, "child-layer-normalized", &child,
                      "Vertex layer follows parent in the upstream R12 writer");
            omitted_fields(child, cf, kept);
        }
        omitted_fields(*l.compound->seqend(), fields(*l.compound->seqend()), {8, 67});
    }
    pair(0, "SEQEND");
    pair(8, mapped("LAYER", value(f, 8, "0"), "0"));
    if (paper)
        pair(67, "1");
}
void R12Exporter::entity(const Located &l, bool paper, bool count) {
    const auto &r = *l.record;
    auto f = fields(r);
    const auto type = r.type();
    if (count)
        ++result.report.source_entities;
    try {
        if (l.compound)
            sequence_ownership(*l.compound);
        if (type == "LWPOLYLINE" || type == "POLYLINE")
            polyline(l, f, paper);
        else if (type == "MESH") {
            const auto triangles = mesh_faces(r,f,paper);
            if (triangles) ++result.report.converted_entities;
            if (count) result.report.output_entities += triangles;
            return;
        } else if (type == "ELLIPSE" || type == "SPLINE" || type == "HELIX") {
            auto points = type == "ELLIPSE" ? detail::approximate_ellipse(*ellipses.at(&r))
                                            : detail::approximate_spline(splines.at(&r)->definition(),
                                                                         options.max_output_bytes / 32);
            bool closed =
                type == "ELLIPSE" && points.size() > 2 && (points.front() - points.back()).length() < 1e-9;
            if (closed)
                points.pop_back();
            auto normal = type == "ELLIPSE" ? ellipses.at(&r)->normal()
                          : type == "HELIX" ? Point3{1, 0, 0}
                                            : splines.at(&r)->definition().normal;
            sampled_polyline(r, f, paper, std::move(points), closed, normal);
            issue(R12IssueKind::Loss, "curve-approximated", &r,
                  "Curve replaced by sampled POLYLINE using upstream R12 sampling; analytic curve and edit "
                  "parameters are lost");
            omitted_fields(r, f, {8,  6,  62, 420, 67, 10, 20, 30,  11,  21,  31, 12, 22, 32, 13,
                                  23, 33, 40, 41,  42, 43, 44, 210, 220, 230, 70, 71, 72, 73, 74});
            ++result.report.converted_entities;
        } else if (type == "RAY" || type == "XLINE") {
            Point3 base{real(f, 10), real(f, 20), real(f, 30)}, dir{real(f, 11), real(f, 21), real(f, 31)};
            auto delta = dir.normalize() * reach;
            pair(0, "LINE");
            common(r, f, paper);
            point(10, type == "XLINE" ? base - delta : base);
            point(11, base + delta);
            issue(R12IssueKind::Loss, "infinite-line-clipped", &r,
                  "Infinite geometry clipped using HEADER extents; reach=" + decimal(reach));
            omitted_fields(r, f, {8, 6, 62, 420, 67, 10, 20, 30, 11, 21, 31});
            ++result.report.converted_entities;
        } else if (type == "MLINE") {
            const auto pieces = mline(*mlines.at(&r), f, paper);
            ++result.report.converted_entities;
            if (count)
                        result.report.output_entities += pieces;
            return;
        } else if (type == "MULTILEADER") {
            const auto pieces = multileader(*multileaders.at(&r), paper);
            if (!pieces) {
                unsupported(r, "No drawable MULTILEADER components in upstream R12 flattening");
                return;
            }
            ++result.report.converted_entities;
            if (count)
                        result.report.output_entities += pieces;
            return;
        } else if (type == "LEADER") {
            // R12Writer::write_exploded uses flatten_leader, not the native EXPLODE command.
            const auto v = leaders.at(&r)->definition();
            if (v.vertices.size() < 2)
                throw std::invalid_argument("LEADER needs at least two vertices to emit a line");
            if (integer_value(f, 76, static_cast<int>(v.vertices.size())) !=
                static_cast<int>(v.vertices.size()))
                throw std::invalid_argument("LEADER vertex count does not match group 76");
            issue(R12IssueKind::Loss, "leader-decomposed", &r,
                  "Editable LEADER and dimension style replaced by independent LINE segments");
            if (v.arrow_enabled)
                issue(R12IssueKind::Loss, "leader-arrow-omitted", &r,
                      "R12 flattening does not draw the enabled arrowhead");
            if (v.path_type == 1)
                issue(R12IssueKind::Loss, "leader-spline-linearized", &r,
                      "Spline path replaced by straight segments between stored vertices");
            if (v.hookline_enabled)
                issue(R12IssueKind::Loss, "leader-hookline-omitted", &r,
                      "No additional hook line is generated");
            if (v.annotation_handle)
                issue(R12IssueKind::Loss, "leader-annotation-detached", &r,
                      "Annotation association (group 340, handle " + value(f, 340) + ") is removed");
            // Keep work linear: common() should not rescan the entire vertex list per segment.
            Fields props;
            for (const auto &p : f)
                if (p.code == 8 || p.code == 6 || p.code == 62 || p.code == 420)
                    props.push_back(p);
            for (std::size_t i = 1; i < v.vertices.size(); ++i) {
                pair(0, "LINE");
                common(r, props, paper);
                point(10, v.vertices[i - 1]);
                point(11, v.vertices[i]);
                if (std::abs(v.normal.x) > 1e-12 || std::abs(v.normal.y) > 1e-12 ||
                    std::abs(v.normal.z - 1) > 1e-12)
                    point(210, v.normal);
            }
            omitted_fields(r, f, {8, 6, 62, 420, 67, 10, 20, 30, 71, 72, 75, 76, 210, 220, 230, 340});
            ++result.report.converted_entities;
            if (count)
                result.report.output_entities += v.vertices.size() - 1;
            return;
        } else if (type == "HATCH" || type == "MPOLYGON") {
            if (auto it = hatch_blocks.find(&r); it != hatch_blocks.end()) {
                pair(0, "INSERT");
                common(r, f, paper);
                pair(2, it->second);
                point(10, {});
                if (count)
                    ++result.report.output_entities;
            } else {
                auto n = hatch_geometry(r, f, paper);
                if (count)
                    result.report.output_entities += n;
            }
            ++result.report.converted_entities;
            return;
        } else if (type == "MTEXT") {
            auto pieces =
                detail::approximate_mtext(mtexts.at(&r)->definition(), options.max_output_bytes / 32);
            issue(R12IssueKind::Loss, "mtext-decomposed", &r,
                  "MTEXT replaced by TEXT with upstream approximate character widths and balanced columns; "
                  "wrapping, fonts, rich formatting and background are not exact");
            for (const auto &piece : pieces)
                text_piece(r, f, paper, piece);
            omitted_fields(r, f, {8,  6,  62, 420, 67, 1,  3,  7,  10, 20, 30,  11,  21, 31,
                                  40, 41, 42, 43,  44, 46, 50, 71, 72, 73, 210, 220, 230});
            ++result.report.converted_entities;
            if (count)
                result.report.output_entities += pieces.size();
            return;
        } else if (type == "INSERT") {
            const auto source = value(f, 2);
            const auto block = mapped("BLOCK_RECORD", source, "");
            if (block.empty() || !blocks.count(upper(source)) || xrefs.count(upper(source))) {
                unsupported(r, "Missing or external block: " + source);
                return;
            }
            if (!l.compound)
                throw std::invalid_argument("INSERT is not a parsed compound record");
            if ((integer_value(f, 66) != 0 || !l.compound->children_.empty()) && !l.compound->seqend())
                throw std::invalid_argument("INSERT is missing SEQEND");
            pair(0, "INSERT");
            common(r, f, paper);
            pair(2, block);
            const std::set<int> body{10, 20, 30, 41, 42, 43, 50, 70, 71, 44, 45, 210, 220, 230};
            if (!l.compound->children_.empty())
                pair(66, "1");
            for (const auto &p : f)
                if (body.count(p.code))
                    pair(p.code, p.value);
            auto allowed = body;
            allowed.insert({2, 8, 6, 62, 420, 67, 66});
            omitted_fields(r, f, allowed);
            for (const auto &a : l.compound->children_) {
                if (a.type() != "ATTRIB")
                    throw std::invalid_argument("Non-ATTRIB child in INSERT");
                entity({&a, nullptr, l.section, l.table, l.block}, paper, false);
            }
            if (!l.compound->children_.empty()) {
                omitted_fields(*l.compound->seqend(), fields(*l.compound->seqend()), {8, 67});
                pair(0, "SEQEND");
                pair(8, mapped("LAYER", value(f, 8, "0"), "0"));
                if (paper)
                    pair(67, "1");
            }
        } else if (type == "TEXT" || type == "ATTDEF" || type == "ATTRIB") {
            const bool attribute = type != "TEXT";
            if (attribute)
                for (auto &p : f)
                    if (p.code == 2) {
                        auto mapped_tag = sanitize(p.value);
                        if (mapped_tag != p.value)
                            issue(R12IssueKind::Loss, "attribute-tag-renamed", &r,
                                  p.value + " -> " + mapped_tag);
                        p.value = mapped_tag;
                    }
            std::set<int> body{10, 20, 30, 40, 1, 50, 41, 51, 7, 71, 72, 11, 21, 31, 39, 210, 220, 230, 73};
            if (attribute)
                body.insert({2, 70, 74});
            if (type == "ATTDEF")
                body.insert(3);
            if (attribute)
                for (auto &p : f)
                    if (p.code == 70)
                        p.value = std::to_string(integer_value(Fields{p}, 70) & 15);
            entity_fields(r, f, body, paper);
        } else if (type == "DIMENSION" || type == "ARC_DIMENSION" || type == "LARGE_RADIAL_DIMENSION") {
            const int kind = integer_value(f, 70) & 15;
            if (kind > 6 || type != "DIMENSION") {
                auto block = mapped("BLOCK_RECORD", value(f, 2), "");
                if (block.empty() || !blocks.count(upper(value(f, 2))) || xrefs.count(upper(value(f, 2)))) {
                    if (type == "DIMENSION") {
                        unsupported(r, "Unknown DIMENSION kind without a local graphics block");
                        return;
                    }
                    const auto pieces = dimension_geometry(*dimensions.at(&r), f, paper);
                    ++result.report.converted_entities;
                    if (count)
                        result.report.output_entities += pieces;
                    return;
                }
                pair(0, "INSERT");
                common(r, f, paper);
                pair(2, block);
                point(10, {});
                issue(
                    R12IssueKind::Loss, "dimension-graphics-block", &r,
                    "Unsupported R12 dimension replaced by its graphics block; dimension semantics are lost");
                ++result.report.converted_entities;
                if (count)
                    ++result.report.output_entities;
                return;
            }
            for (auto &p : f) {
                if (p.code == 2 && !p.value.empty()) {
                    auto name = mapped("BLOCK_RECORD", p.value, "");
                    if (name.empty() || xrefs.count(upper(p.value)))
                        throw std::invalid_argument("Missing dimension graphics block");
                    p.value = name;
                }
                if (p.code == 3)
                    p.value = mapped("DIMSTYLE", p.value, "STANDARD");
                if (p.code == 70)
                    p.value = std::to_string(integer_value(Fields{p}, 70) & (7 | 64 | 128));
            }
            entity_fields(r, f, {1,  2,  3,  10, 20, 30, 11, 21, 31, 12, 22, 32, 13, 23,  33,  14, 24,
                                 34, 15, 25, 35, 16, 26, 36, 40, 50, 51, 52, 53, 70, 210, 220, 230},
                          paper);
        } else {
            std::set<int> body{10, 20, 30};
            if (type == "POINT") {
                body.insert({39, 210, 220, 230, 50});
                for (auto &p : f)
                    if (p.code == 50) {
                        if (real(Fields{p}, 50) != 0)
                            issue(R12IssueKind::Loss, "point-angle-converted", &r,
                                  "Matches pinned R12 degrees conversion of the upstream raw POINT angle");
                        p.value = decimal(real(Fields{p}, 50) * 180 / std::acos(-1.0));
                    }
            } else if (type == "LINE")
                body.insert({11, 21, 31, 39, 210, 220, 230});
            else if (type == "CIRCLE" || type == "ARC") {
                body.insert({40, 39, 210, 220, 230});
                if (type == "ARC")
                    body.insert({50, 51});
            } else if (type == "SOLID" || type == "TRACE" || type == "3DFACE") {
                body.insert({11, 21, 31, 12, 22, 32, 13, 23, 33});
                if (type == "3DFACE")
                    body.insert(70);
                else
                    body.insert({39, 210, 220, 230});
            } else {
                if (type.rfind("AEC", 0) == 0 || type.rfind("ARCH", 0) == 0)
                    issue(R12IssueKind::Error, "architecture-class-rejected", &r,
                          "R12 cannot retain architecture classes");
                else
                    unsupported(r,
                                "No native R12 mapping in this batch; geometry fallback is not implemented");
                return;
            }
            entity_fields(r, f, body, paper);
        }
        if (count)
            ++result.report.output_entities;
    } catch (const std::exception &e) {
        issue(R12IssueKind::Error, "invalid-entity", &r, e.what());
    }
}
R12ExportResult R12Exporter::run() {
    try {
        if (doc.version() == DxfVersion::Unknown)
            issue(R12IssueKind::Error, "unknown-source-version", nullptr,
                  "Cannot downgrade an unknown DXF version");
        for (const auto &item : doc.records_) {
            const auto &r = std::visit([](const DxfRecord &rec) -> const DxfRecord & { return rec; }, item);
            if (!is_architecture_name(r.type()))
                continue;
            auto text = r.handle();
            auto begin = text.find_first_not_of(" \t\r\n");
            auto handle = begin == std::string::npos ? std::string{} : text.substr(begin);
            issue(R12IssueKind::Error, "arch-unsupported", &r,
                  "Cannot export Arch " + std::string(r.type()) + " at handle " + handle +
                      " to DXF: R12 does not support Arch classes; save as the source DWG version to preserve its payload");
            break;
        }
        for (auto handle : doc.duplicate_handles())
            issue(R12IssueKind::Error, "duplicate-handle", nullptr,
                  "Ambiguous source handle " + std::to_string(handle));
        std::string sec, tbl, block;
        for (const auto &v : doc.records_)
            std::visit(
                [&](const auto &r) {
                    const auto type = r.type();
                    if (type == "SECTION") {
                        sec = r.text_field("", 2);
                        tbl.clear();
                        block.clear();
                    }
                    if (type == "TABLE" && sec == "TABLES")
                        tbl = r.text_field("", 2);
                    const DxfCompoundEntity *compound = nullptr;
                    if constexpr (std::is_same_v<DxfLwPolyline, std::decay_t<decltype(r)>>)
                        lightweight[&r] = &r;
                    if constexpr (std::is_base_of_v<DxfSpline, std::decay_t<decltype(r)>>)
                        splines[&r] = &r;
                    if constexpr (std::is_same_v<DxfEllipse, std::decay_t<decltype(r)>>)
                        ellipses[&r] = &r;
                    if constexpr (std::is_same_v<DxfDimension, std::decay_t<decltype(r)>>)
                        dimensions[&r] = &r;
                    if constexpr (std::is_same_v<DxfMLine, std::decay_t<decltype(r)>>)
                        mlines[&r] = &r;
                    if constexpr (std::is_same_v<DxfMultiLeader, std::decay_t<decltype(r)>>)
                        multileaders[&r] = &r;
                    if constexpr (std::is_same_v<DxfLeader, std::decay_t<decltype(r)>>)
                        leaders[&r] = &r;
                    if constexpr (std::is_same_v<DxfMText, std::decay_t<decltype(r)>>)
                        mtexts[&r] = &r;
                    if constexpr (std::is_same_v<DxfHatch, std::decay_t<decltype(r)>>)
                        hatches[&r] = &r;
                    if constexpr (std::is_base_of_v<DxfCompoundEntity, std::decay_t<decltype(r)>>)
                        compound = &r;
                    if constexpr (std::is_same_v<DxfBlock, std::decay_t<decltype(r)>>) {
                        block = r.name();
                        if (block.empty())
                            issue(R12IssueKind::Error, "missing-block-name", &r, "BLOCK has no name");
                        const auto key = upper(block);
                        if (blocks.count(key))
                            issue(R12IssueKind::Error, "duplicate-block", &r,
                                  "Duplicate block definition " + block);
                        else
                            blocks[key] = &r;
                        if (r.flags() & (4 | 8))
                            xrefs.insert(key);
                    }
                    records.push_back({&r, compound, sec, tbl, block});
                    if (type == "ENDBLK")
                        block.clear();
                    if (type == "ENDTAB")
                        tbl.clear();
                    if (type == "ENDSEC")
                        sec.clear();
                },
                v);
        for (const auto &l : records)
            if (l.section == "HEADER" && l.record->type() == "SECTION") {
                auto f = fields(*l.record, false);
                for (std::size_t i = 0; i + 1 < f.size(); ++i)
                    if (f[i].code == 9 && f[i].value == "$DWGCODEPAGE" && f[i + 1].code == 3)
                        declaration = f[i + 1].value;
            }
        // Upstream defaults both model extents to zero when the variables are absent.
        Point3 min{}, max{};
        if (doc.header_index_) {
            auto hf = fields(doc.record_at({*doc.header_index_, std::nullopt, false}), false);
            for (std::size_t i = 0; i < hf.size(); ++i)
                if (hf[i].code == 9 && (hf[i].value == "$EXTMIN" || hf[i].value == "$EXTMAX")) {
                    auto &p = hf[i].value == "$EXTMIN" ? min : max;
                    Fields components;
                    for (auto j = i + 1; j < hf.size() && hf[j].code != 9; ++j)
                        components.push_back(hf[j]);
                    p = {real(components, 10), real(components, 20), real(components, 30)};
                }
        }
        if (max.x >= min.x && max.y >= min.y)
            reach = std::max((max - min).length() * 2 + std::max({std::abs(min.x), std::abs(min.y),
                                                                  std::abs(max.x), std::abs(max.y)}),
                             1000.);
        page = detail::legacy_page("AC1009", declaration);
        names["LAYER"].add("0");
        names["LTYPE"].add("CONTINUOUS");
        names["STYLE"].add("STANDARD");
        names["DIMSTYLE"].add("STANDARD");
        for (auto t : {"LAYER", "LTYPE", "STYLE", "DIMSTYLE"}) {
            std::set<std::string> seen;
            for (auto r : entries(t)) {
                auto name = value(fields(*r, false), 2);
                if (name.empty()) {
                    if (std::string(t) != "STYLE")
                        issue(R12IssueKind::Error, "missing-symbol-name", r, "Unnamed table entry");
                    continue;
                }
                if (std::string(t) == "LTYPE" && pseudo(name))
                    continue;
                if (!seen.insert(upper(name)).second)
                    issue(R12IssueKind::Error, "duplicate-symbol", r,
                          "Ambiguous " + std::string(t) + " name " + name);
                map_name(t, name);
            }
        }
        // Source order, rather than sorted block names, determines collision suffixes.
        for (const auto &l : records)
            if (l.record->type() == "BLOCK" && !space_block(l.block))
                map_name("BLOCK_RECORD", l.block, true);
        for (const auto &l : records) {
            const auto type = l.record->type();
            if (type == "SECTION" || type == "ENDSEC" || type == "EOF" || type == "TABLE" || type == "ENDTAB")
                continue;
            if (l.section == "TABLES") {
                fields(*l.record); // private/XDATA payloads on table entries are also lost
                if (type == "BLOCK_RECORD") {
                    const auto name = value(fields(*l.record, false), 2);
                    if (!space_block(name) && !blocks.count(upper(name)))
                        issue(R12IssueKind::Error, "missing-block-definition", l.record,
                              "No BLOCK definition for " + name);
                    issue(R12IssueKind::Loss, "block-record-metadata-removed", l.record,
                          "R13 block-record metadata is removed; supported BLOCK definitions are retained");
                } else if (type != "LAYER" && type != "LTYPE" && type != "STYLE" && type != "DIMSTYLE" &&
                           type != "VPORT" && type != "VIEW" && type != "UCS" && type != "APPID")
                    issue(R12IssueKind::Loss, "table-record-removed", l.record,
                          "No R12 table representation for " + l.table);
            }
            if (type == "BLOCK" && space_block(l.block)) {
                auto f = fields(*l.record);
                omitted_fields(*l.record, f, {2, 3, 8, 70, 10, 20, 30});
                if (real(f, 10) != 0 || real(f, 20) != 0 || real(f, 30) != 0 || integer_value(f, 70) != 0)
                    issue(R12IssueKind::Loss, "space-block-normalized", l.record,
                          "R12 layout blocks are emitted empty at the origin with flags zero");
            }
            if (l.section == "OBJECTS") {
                ++result.report.removed_objects;
                issue(R12IssueKind::Loss, "object-removed", l.record, "R12 has no OBJECTS section");
            } else if (l.section == "CLASSES")
                issue(R12IssueKind::Information, "class-registration-removed", l.record,
                      "R12 has no CLASSES section");
            else if (l.section != "ENTITIES" && l.section != "BLOCKS" && l.section != "TABLES" &&
                     l.section != "HEADER")
                issue(R12IssueKind::Loss, "section-record-removed", l.record, l.section + " is not exported");
        }
        issue(R12IssueKind::Information, "legacy-structure", nullptr,
              "R12 ASCII: no modern handles, owners, subclasses, BLOCK_RECORD table, CLASSES or OBJECTS; "
              "$HANDLING=0");
        header();
        tables();
        auto location = [&](const Located &l) {
            auto owner = l.record->owner_handle();
            if (!owner.empty() && owner != "0") {
                if (auto br = doc.block_record(owner))
                    return br->name();
                issue(R12IssueKind::Error, "unresolved-owner", l.record,
                      "Entity owner does not resolve to a unique BLOCK_RECORD");
            }
            if (!l.block.empty())
                return l.block;
            return integer_value(fields(*l.record, false), 67) ? std::string("*Paper_Space")
                                                               : std::string("*Model_Space");
        };
        auto candidates = [&](const std::string &wanted, bool paper) {
            for (const auto &l : records) {
                const auto type = l.record->type();
                if (l.section != "ENTITIES" && l.section != "BLOCKS")
                    continue;
                if (type == "SECTION" || type == "ENDSEC" || type == "BLOCK" || type == "ENDBLK")
                    continue;
                auto where = upper(location(l));
                if (where == "$MODEL_SPACE")
                    where = "*MODEL_SPACE";
                if (where == "$PAPER_SPACE")
                    where = "*PAPER_SPACE";
                if (where == upper(wanted))
                    entity(l, paper);
            }
        };
        auto begin_block = [&](const std::string &name, int flags, Point3 base) {
            pair(0, "BLOCK");
            pair(8, "0");
            pair(2, name);
            pair(70, std::to_string(flags));
            point(10, base);
            pair(3, name);
        };
        auto end_block = [&] {
            pair(0, "ENDBLK");
            pair(8, "0");
        };
        section("BLOCKS");
        for (auto name : {"$MODEL_SPACE", "$PAPER_SPACE"}) {
            begin_block(name, 0, {});
            end_block();
        }
        // Same traversal as Rust: BLOCK_RECORD order, then ordered members.
        std::size_t hatch_number = 0;
        for (auto br : entries("BLOCK_RECORD")) {
            auto bf = fields(*br, false);
            auto block_name = value(bf, 2);
            if (xrefs.count(upper(block_name)))
                continue;
            for (const auto &l : records) {
                if (!hatches.count(l.record) || l.record->handle().empty() || l.record->handle() == "0" ||
                    upper(location(l)) != upper(block_name))
                    continue;
                std::string name;
                do {
                    name = "*X" + std::to_string(++hatch_number);
                } while (names["BLOCK_RECORD"].used.count(upper(name)));
                names["BLOCK_RECORD"].used.insert(upper(name));
                begin_block(name, 1, {});
                try {
                    hatch_geometry(*l.record, fields(*l.record), false);
                } catch (const std::exception &e) {
                    issue(R12IssueKind::Error, "invalid-hatch", l.record, e.what());
                }
                end_block();
                hatch_blocks[l.record] = name;
            }
        }
        for (const auto &l : records)
            if (l.record->type() == "BLOCK" && !space_block(l.block)) {
                auto b = blocks.at(upper(l.block));
                if (xrefs.count(upper(l.block))) {
                    issue(options.allow_loss && options.omit_unsupported_entities ? R12IssueKind::Loss
                                                                                  : R12IssueKind::Error,
                          "xref-block-removed", b,
                          "External block definitions cannot be embedded by this exporter");
                    continue;
                }
                auto f = fields(*b);
                int flags = !l.block.empty() && l.block.front() == '*' ? 1 : 0;
                for (const auto &e : records)
                    if (e.block == l.block && e.record->type() == "ATTDEF")
                        flags |= 2;
                begin_block(mapped("BLOCK_RECORD", l.block, sanitize(l.block)), flags, b->base_point());
                candidates(l.block, false);
                end_block();
                omitted_fields(*b, f, {2, 3, 8, 70, 10, 20, 30});
            }
        pair(0, "ENDSEC");
        section("ENTITIES");
        candidates("*Model_Space", false);
        candidates("*Paper_Space", true);
        pair(0, "ENDSEC");
        pair(0, "EOF");
        // Extra layouts are never silently folded into the default paper space.
        for (const auto &l : records) {
            const auto type = l.record->type();
            if (l.section != "ENTITIES" && l.section != "BLOCKS")
                continue;
            if (type == "SECTION" || type == "ENDSEC" || type == "BLOCK" || type == "ENDBLK")
                continue;
            auto where = upper(location(l));
            if ((where.rfind("*PAPER_SPACE", 0) == 0 && where != "*PAPER_SPACE") || xrefs.count(where) ||
                (!space_block(where) && !blocks.count(where))) {
                ++result.report.source_entities;
                unsupported(*l.record,
                            "Entity belongs to an unavailable block or additional paper layout: " + where);
            }
        }
    } catch (const std::exception &e) {
        issue(R12IssueKind::Error, "conversion-failed", nullptr, e.what());
    }
    bool error = false, loss = false;
    for (const auto &i : result.report.issues) {
        error |= i.kind == R12IssueKind::Error;
        loss |= i.kind == R12IssueKind::Loss;
    }
    result.report.can_write = !error && (!loss || options.allow_loss);
    if (result.report.can_write)
        result.ascii = output.str();
    return std::move(result);
}
R12ExportResult DxfDocument::export_r12(const R12ExportOptions &options) const {
    return R12Exporter(*this, options).run();
}
R12ExportReport DxfDocument::write_r12(std::ostream &out, const R12ExportOptions &options) const {
    auto result = export_r12(options);
    if (!result.report.can_write)
        throw R12ExportError(std::move(result.report));
    out.write(result.ascii.data(), static_cast<std::streamsize>(result.ascii.size()));
    if (!out)
        throw std::runtime_error("R12 output stream failed");
    return std::move(result.report);
}
} // namespace libopencad
