// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/count.rs block instance counting.
#include <libopencad/count.hpp>
#include <libopencad/dxf_document.hpp>
#include <libopencad/transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace libopencad {
namespace {

std::string ascii_upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

bool ascii_ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::uint64_t handle_value(std::string_view s) {
    std::uint64_t v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9')
            v += static_cast<unsigned>(c - '0');
        else if (c >= 'A' && c <= 'F')
            v += static_cast<unsigned>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f')
            v += static_cast<unsigned>(c - 'a' + 10);
    }
    return v;
}

bool same_scale(std::array<double, 3> a, std::array<double, 3> b) {
    for (int i = 0; i < 3; ++i) {
        auto d = std::abs(a[i] - b[i]);
        if (d > 1e-9 * std::max({std::abs(a[i]), std::abs(b[i]), 1.0}))
            return false;
    }
    return true;
}

std::string json_string(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::int64_t round6(double v) { return static_cast<std::int64_t>(std::llround(v * 1e6)); }

bool point_in_polygon(std::array<double, 2> p, const std::vector<std::array<double, 2>> &polygon) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        auto yi = polygon[i][1], yj = polygon[j][1], xi = polygon[i][0], xj = polygon[j][0];
        if (((yi > p[1]) != (yj > p[1])) && (p[0] < (xj - xi) * (p[1] - yi) / (yj - yi + 0.0) + xi))
            inside = !inside;
    }
    return inside;
}

bool inside_polygon(const std::vector<CountPiece> &pieces, const std::vector<std::array<double, 2>> &polygon);

} // namespace

CountKey CountKey::of(const BlockInstance &instance, bool layer, bool scale, bool mirror) {
    CountKey key;
    if (layer)
        key.layer = instance.layer;
    if (scale)
        key.scale = instance.scale;
    if (mirror)
        key.mirror_state = instance.mirrored;
    return key;
}

bool CountKey::matches(const BlockInstance &instance) const {
    if (layer && !ascii_ieq(*layer, instance.layer))
        return false;
    if (scale && !same_scale(*scale, instance.scale))
        return false;
    if (mirror_state && *mirror_state != instance.mirrored)
        return false;
    return true;
}

std::string CountKey::json() const {
    std::vector<std::string> parts;
    if (layer)
        parts.push_back("\"layer\":" + json_string(*layer));
    if (mirror_state)
        parts.push_back(std::string("\"mirrorState\":") + (*mirror_state ? "true" : "false"));
    if (scale) {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o << "\"scale\":{\"x\":" << (*scale)[0] << ",\"y\":" << (*scale)[1] << ",\"z\":" << (*scale)[2] << "}";
        parts.push_back(o.str());
    }
    std::string out = "{";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += ",";
        out += parts[i];
    }
    out += "}";
    return out;
}

std::string CountKey::label() const {
    std::vector<std::string> parts;
    if (layer)
        parts.push_back(*layer);
    if (scale) {
        auto [x, y, z] = *scale;
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o.setf(std::ios::fixed);
        o.precision(4);
        if (same_scale({x, x, x}, {x, y, z}))
            o << x;
        else
            o << x << "," << y << "," << z;
        parts.push_back(o.str());
    }
    if (mirror_state)
        parts.push_back(*mirror_state ? "Mirrored" : "Default");
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += "_";
        out += parts[i];
    }
    return out;
}

std::vector<BlockInstance> block_instances(const DxfDocument &doc,
                                           const std::optional<std::vector<std::array<double, 2>>> &area) {
    std::unordered_map<std::string, const DxfBlockRecord *> records;
    doc.for_each<DxfBlockRecord>([&](const DxfBlockRecord &r) { records[ascii_upper(r.name())] = &r; });
    std::map<std::pair<std::string, std::array<std::int64_t, 10>>, std::uint64_t> seen;
    std::vector<BlockInstance> out;
    for (const auto &h : doc.model_space_entity_handles()) {
        const DxfInsert *insert = nullptr;
        doc.for_each<DxfInsert>([&](const DxfInsert &i) {
            if (ascii_ieq(i.handle(), h))
                insert = &i;
        });
        if (!insert)
            continue;
        auto def = insert->definition();
        auto rec = records.find(ascii_upper(def.block_name));
        if (rec == records.end())
            continue;
        const auto *record = rec->second;
        std::int16_t type_flags = 0;
        const auto rec_handle = handle_value(record->handle());
        doc.for_each<DxfBlock>([&](const DxfBlock &b) {
            if (handle_value(b.owner_handle()) == rec_handle)
                type_flags = b.flags();
        });
        if (!def.block_name.empty() && def.block_name.front() == '*')
            continue;
        if ((type_flags & 1) || (type_flags & 4) || (type_flags & 8) || (type_flags & 16))
            continue;
        if (area) {
            auto outline = insert_outline(doc, handle_value(insert->handle()), 0);
            if (!inside_polygon(outline, *area))
                continue;
        }
        auto sx = def.scale.x, sy = def.scale.y, sz = def.scale.z;
        std::array<std::int64_t, 10> key_n{round6(def.insertion.x), round6(def.insertion.y), round6(def.insertion.z),
                                           round6(def.rotation),    round6(sx),              round6(sy),
                                           round6(sz),              round6(def.normal.x),    round6(def.normal.y),
                                           round6(def.normal.z)};
        auto key = std::make_pair(ascii_upper(record->name()), key_n);
        auto handle = handle_value(insert->handle());
        std::optional<std::uint64_t> duplicate;
        auto it = seen.find(key);
        if (it != seen.end())
            duplicate = it->second;
        else
            seen.emplace(key, handle);
        BlockInstance inst;
        inst.handle = handle;
        inst.name = record->name();
        inst.layer = insert->layer();
        inst.scale = {std::abs(sx), std::abs(sy), std::abs(sz)};
        inst.mirrored = sx * sy * sz < 0.0;
        inst.duplicate_of = duplicate;
        out.push_back(std::move(inst));
    }
    return out;
}

std::vector<BlockCount> block_counts(const std::vector<BlockInstance> &instances) {
    std::map<std::string, BlockCount> map;
    for (const auto &i : instances) {
        auto &entry = map[ascii_upper(i.name)];
        if (entry.name.empty())
            entry.name = i.name;
        if (i.duplicate_of)
            entry.duplicates.push_back(i.handle);
        else
            entry.counted.push_back(i.handle);
    }
    std::vector<BlockCount> out;
    for (auto &p : map)
        out.push_back(std::move(p.second));
    return out;
}

std::size_t count_of(const std::vector<BlockInstance> &instances, std::string_view name, const CountKey &key) {
    std::size_t n = 0;
    for (const auto &i : instances)
        if (!i.duplicate_of && ascii_ieq(i.name, name) && key.matches(i))
            ++n;
    return n;
}

std::vector<std::pair<CountKey, std::size_t>> expanded_counts(const std::vector<BlockInstance> &instances,
                                                              std::string_view name, bool layer, bool scale,
                                                              bool mirror) {
    std::vector<std::pair<CountKey, std::size_t>> out;
    for (const auto &i : instances) {
        if (i.duplicate_of || !ascii_ieq(i.name, name))
            continue;
        auto key = CountKey::of(i, layer, scale, mirror);
        auto found = std::find_if(out.begin(), out.end(), [&](const auto &p) { return p.first.matches(i); });
        if (found != out.end())
            ++found->second;
        else
            out.emplace_back(key, 1);
    }
    return out;
}

std::string block_count_json(std::string_view name, const CountKey &key) {
    return std::string("{\"key\":") + key.json() + ",\"name\":" + json_string(name) + ",\"type\":\"block\"}";
}

std::string area_count_json(std::string_view name, const CountKey &key, std::uint64_t boundary) {
    std::ostringstream o;
    o << std::hex << std::uppercase << boundary;
    return std::string("{\"boundaryObjectHandle\":\"") + o.str() +
           "\",\"evaluatorId\":\"AcCount2\",\"key\":" + key.json() + ",\"name\":" + json_string(name) +
           ",\"type\":\"block\"}";
}

std::string single_count_json(const std::vector<std::uint64_t> &targets) {
    std::string out = "{\"targets\":[";
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (i)
            out += ",";
        std::ostringstream o;
        o << std::hex << std::uppercase << targets[i];
        out += "\"" + o.str() + "\"";
    }
    out += "],\"type\":\"single\"}";
    return out;
}

namespace {

std::optional<std::string> json_string_field(std::string_view json, std::string_view key) {
    auto pat = std::string("\"") + std::string(key) + "\"";
    auto at = json.find(pat);
    if (at == std::string_view::npos)
        return std::nullopt;
    auto colon = json.find(':', at + pat.size());
    if (colon == std::string_view::npos)
        return std::nullopt;
    auto q = json.find('"', colon + 1);
    if (q == std::string_view::npos)
        return std::nullopt;
    auto e = json.find('"', q + 1);
    if (e == std::string_view::npos)
        return std::nullopt;
    return std::string(json.substr(q + 1, e - q - 1));
}

std::optional<std::string> json_object_field(std::string_view json, std::string_view key) {
    auto pat = std::string("\"") + std::string(key) + "\"";
    auto at = json.find(pat);
    if (at == std::string_view::npos)
        return std::nullopt;
    auto brace = json.find('{', at + pat.size());
    if (brace == std::string_view::npos)
        return std::nullopt;
    int depth = 0;
    for (std::size_t i = brace; i < json.size(); ++i) {
        if (json[i] == '{')
            ++depth;
        else if (json[i] == '}') {
            --depth;
            if (depth == 0)
                return std::string(json.substr(brace, i - brace + 1));
        }
    }
    return std::nullopt;
}

std::optional<double> json_number_field(std::string_view json, std::string_view key) {
    auto pat = std::string("\"") + std::string(key) + "\"";
    auto at = json.find(pat);
    if (at == std::string_view::npos)
        return std::nullopt;
    auto colon = json.find(':', at + pat.size());
    if (colon == std::string_view::npos)
        return std::nullopt;
    auto i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t'))
        ++i;
    try {
        std::size_t n = 0;
        auto v = std::stod(std::string(json.substr(i)), &n);
        if (!n)
            return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> json_bool_field(std::string_view json, std::string_view key) {
    auto pat = std::string("\"") + std::string(key) + "\"";
    auto at = json.find(pat);
    if (at == std::string_view::npos)
        return std::nullopt;
    auto colon = json.find(':', at + pat.size());
    if (colon == std::string_view::npos)
        return std::nullopt;
    auto i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t'))
        ++i;
    if (json.substr(i, 4) == "true")
        return true;
    if (json.substr(i, 5) == "false")
        return false;
    return std::nullopt;
}

std::optional<CountKey> parse_count_key(std::string_view json) {
    if (json.find("\"key\"") == std::string_view::npos)
        return CountKey{};
    auto obj = json_object_field(json, "key");
    if (!obj)
        return std::nullopt;
    CountKey key;
    key.layer = json_string_field(*obj, "layer");
    if (obj->find("\"scale\"") != std::string::npos) {
        auto scale = json_object_field(*obj, "scale");
        if (!scale)
            return std::nullopt;
        auto x = json_number_field(*scale, "x");
        auto y = json_number_field(*scale, "y");
        auto z = json_number_field(*scale, "z");
        if (!x || !y || !z)
            return std::nullopt;
        key.scale = {*x, *y, *z};
    }
    key.mirror_state = json_bool_field(*obj, "mirrorState");
    return key;
}

} // namespace

namespace {

std::vector<std::uint64_t> json_hex_array(std::string_view json, std::string_view key) {
    auto pat = std::string("\"") + std::string(key) + "\"";
    auto at = json.find(pat);
    if (at == std::string_view::npos)
        return {};
    auto lb = json.find('[', at);
    auto rb = json.find(']', lb == std::string_view::npos ? 0 : lb);
    if (lb == std::string_view::npos || rb == std::string_view::npos)
        return {};
    std::vector<std::uint64_t> out;
    auto chunk = json.substr(lb + 1, rb - lb - 1);
    std::size_t i = 0;
    while (i < chunk.size()) {
        auto q = chunk.find('"', i);
        if (q == std::string_view::npos)
            break;
        auto e = chunk.find('"', q + 1);
        if (e == std::string_view::npos)
            break;
        out.push_back(handle_value(chunk.substr(q + 1, e - q - 1)));
        i = e + 1;
    }
    return out;
}

bool segments_cross(std::array<double, 2> a, std::array<double, 2> b, std::array<double, 2> c,
                    std::array<double, 2> d) {
    auto orient = [](std::array<double, 2> p, std::array<double, 2> q, std::array<double, 2> r) {
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
    };
    auto on = [](std::array<double, 2> p, std::array<double, 2> q, std::array<double, 2> r) {
        return r[0] >= std::min(p[0], q[0]) && r[0] <= std::max(p[0], q[0]) && r[1] >= std::min(p[1], q[1]) &&
               r[1] <= std::max(p[1], q[1]);
    };
    auto d1 = orient(c, d, a), d2 = orient(c, d, b), d3 = orient(a, b, c), d4 = orient(a, b, d);
    return (d1 * d2 < 0.0 && d3 * d4 < 0.0) || (d1 == 0.0 && on(c, d, a)) || (d2 == 0.0 && on(c, d, b)) ||
           (d3 == 0.0 && on(a, b, c)) || (d4 == 0.0 && on(a, b, d));
}

bool piece_meets(const CountPiece &piece, std::array<double, 2> a, std::array<double, 2> b) {
    if (piece.kind == CountPiece::Kind::Segment)
        return segments_cross({piece.a.x, piece.a.y}, {piece.b.x, piece.b.y}, a, b);
    if (piece.kind == CountPiece::Kind::Conic) {
        auto d = std::array<double, 2>{b[0] - a[0], b[1] - a[1]};
        auto n = std::array<double, 2>{-d[1], d[0]};
        auto dot = [&](double x, double y) { return n[0] * x + n[1] * y; };
        auto k = dot(piece.c.x - a[0], piece.c.y - a[1]);
        auto al = dot(piece.u.x, piece.u.y);
        auto be = dot(piece.v.x, piece.v.y);
        auto r = std::hypot(al, be);
        if (r < 1e-15 || std::abs(k) > r)
            return false;
        auto phi = std::atan2(be, al);
        auto w = std::acos(std::clamp(-k / r, -1.0, 1.0));
        for (double t : {phi + w, phi - w}) {
            t = piece.t0 + std::fmod(t - piece.t0 + 8 * std::acos(-1.0), 2 * std::acos(-1.0));
            if (t > piece.t1 + 1e-12)
                continue;
            auto p = piece.c + piece.u * std::cos(t) + piece.v * std::sin(t);
            auto den = d[0] * d[0] + d[1] * d[1];
            if (den == 0.0)
                continue;
            auto s = ((p.x - a[0]) * d[0] + (p.y - a[1]) * d[1]) / den;
            if (s >= -1e-12 && s <= 1.0 + 1e-12)
                return true;
        }
        return false;
    }
    auto pts = piece.points();
    for (std::size_t i = 1; i < pts.size(); ++i)
        if (segments_cross({pts[i - 1].x, pts[i - 1].y}, {pts[i].x, pts[i].y}, a, b))
            return true;
    return false;
}

bool inside_polygon(const std::vector<CountPiece> &pieces, const std::vector<std::array<double, 2>> &polygon) {
    if (pieces.empty() || polygon.empty())
        return false;
    auto n = polygon.size();
    for (const auto &piece : pieces) {
        auto s = piece.start();
        if (!point_in_polygon({s.x, s.y}, polygon))
            return false;
        for (std::size_t k = 0; k < n; ++k)
            if (piece_meets(piece, polygon[k], polygon[(k + 1) % n]))
                return false;
    }
    return true;
}

template <class T>
const T *entity_by_handle(const DxfDocument &doc, std::uint64_t handle) {
    const T *found = nullptr;
    doc.for_each<T>([&](const T &entity) {
        if (handle_value(entity.handle()) == handle)
            found = &entity;
    });
    return found;
}

const DxfInsert *insert_by_handle(const DxfDocument &doc, std::uint64_t handle) {
    return entity_by_handle<DxfInsert>(doc, handle);
}

std::string entity_type(const DxfDocument &doc, std::uint64_t handle) {
    std::ostringstream o;
    o << std::hex << handle;
    auto *rec = doc.get_record(o.str());
    return rec ? std::string(rec->type()) : std::string{};
}

std::optional<std::pair<bool, std::vector<std::array<double, 3>>>> polyline_points(const DxfDocument &doc,
                                                                                   std::uint64_t handle) {
    if (auto *pl = entity_by_handle<DxfLwPolyline>(doc, handle)) {
        std::vector<std::array<double, 3>> pts;
        for (const auto &v : pl->vertices())
            pts.push_back({v.x, v.y, v.bulge});
        return std::make_pair(pl->is_closed(), std::move(pts));
    }
    if (auto *pl = entity_by_handle<DxfPolyline2D>(doc, handle)) {
        std::vector<std::array<double, 3>> pts;
        for (std::size_t i = 0; i < pl->vertex_count(); ++i) {
            auto v = pl->vertex(i);
            pts.push_back({v.location.x, v.location.y, v.bulge});
        }
        return std::make_pair(pl->is_closed(), std::move(pts));
    }
    return std::nullopt;
}

std::optional<std::vector<std::array<double, 3>>> polyline_shape(const DxfDocument &doc, std::uint64_t handle) {
    auto pts = polyline_points(doc, handle);
    if (!pts)
        return std::nullopt;
    auto closed = pts->first;
    const auto &p = pts->second;
    auto n = p.size();
    auto segs = closed ? n : n ? n - 1 : 0;
    auto dir = [&](std::size_t i) {
        auto a = p[i % n], b = p[(i + 1) % n];
        return std::pair<double, double>{b[0] - a[0], b[1] - a[1]};
    };
    double total = 0;
    for (std::size_t i = 0; i < segs; ++i) {
        auto d = dir(i);
        total += std::hypot(d.first, d.second);
    }
    if (total <= 1e-12)
        return std::nullopt;
    std::vector<std::array<double, 3>> out;
    for (std::size_t i = 0; i < segs; ++i) {
        auto d = dir(i);
        double turn = 0;
        if (closed || i + 1 < segs) {
            auto e = dir(i + 1);
            turn = std::atan2(d.first * e.second - d.second * e.first, d.first * e.first + d.second * e.second);
        }
        out.push_back({std::hypot(d.first, d.second) / total, turn, p[i][2]});
    }
    return out;
}

bool same_shape(const std::optional<std::vector<std::array<double, 3>>> &a,
                const std::optional<std::vector<std::array<double, 3>>> &b, bool closed) {
    if (!a || !b || a->size() != b->size())
        return false;
    auto close = [](const std::array<double, 3> &x, const std::array<double, 3> &y) {
        return std::abs(x[0] - y[0]) < 1e-6 && std::abs(x[1] - y[1]) < 1e-6 && std::abs(x[2] - y[2]) < 1e-6;
    };
    auto shifts = closed ? a->size() : 1;
    std::vector<std::array<double, 3>> mirrored;
    for (const auto &x : *b)
        mirrored.push_back({x[0], -x[1], -x[2]});
    for (const auto *cand : {&*b, static_cast<const std::vector<std::array<double, 3>> *>(&mirrored)}) {
        for (std::size_t k = 0; k < shifts; ++k) {
            bool ok = true;
            for (std::size_t i = 0; i < a->size(); ++i)
                if (!close((*a)[i], (*cand)[(i + k) % cand->size()])) {
                    ok = false;
                    break;
                }
            if (ok)
                return true;
        }
    }
    return false;
}

std::string text_of(const DxfDocument &doc, std::uint64_t handle) {
    if (auto *t = entity_by_handle<DxfText>(doc, handle))
        return t->text();
    if (auto *t = entity_by_handle<DxfMText>(doc, handle))
        return t->definition().text;
    return {};
}

std::vector<std::array<double, 2>> normalized_xy(const std::vector<Point3> &points) {
    if (points.empty())
        return {};
    auto first = points.front();
    std::optional<Point3> dir;
    for (const auto &p : points) {
        auto d = p - first;
        if (std::hypot(d.x, d.y) > 1e-12) {
            dir = d;
            break;
        }
    }
    if (!dir)
        return {};
    auto len = std::hypot(dir->x, dir->y);
    auto ang = std::atan2(dir->y, dir->x);
    auto c = std::cos(ang), s = std::sin(ang);
    std::vector<std::array<double, 2>> out;
    for (const auto &p : points) {
        auto x = p.x - first.x, y = p.y - first.y;
        out.push_back({(x * c + y * s) / len, (y * c - x * s) / len});
    }
    return out;
}

bool same_hatch(const HatchDefinition &a, const HatchDefinition &b);

bool similar(const DxfDocument &doc, std::uint64_t target, std::uint64_t handle) {
    auto ta = entity_type(doc, target);
    auto tb = entity_type(doc, handle);
    auto eq = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    auto sweep = [](double a, double b) {
        auto s = std::fmod(b - a, 2 * std::acos(-1.0));
        if (s < 0)
            s += 2 * std::acos(-1.0);
        return s;
    };
    if (ta == "LINE" && tb == "LINE")
        return true;
    if (ta == "ARC" && tb == "ARC") {
        auto *a = entity_by_handle<DxfArc>(doc, target);
        auto *b = entity_by_handle<DxfArc>(doc, handle);
        return a && b && eq(sweep(a->start_angle_radians(), a->end_angle_radians()),
                            sweep(b->start_angle_radians(), b->end_angle_radians()));
    }
    if (ta == "ELLIPSE" && tb == "ELLIPSE") {
        auto *a = entity_by_handle<DxfEllipse>(doc, target);
        auto *b = entity_by_handle<DxfEllipse>(doc, handle);
        return a && b && eq(a->minor_axis_ratio(), b->minor_axis_ratio()) &&
               eq(sweep(a->start_parameter(), a->end_parameter()), sweep(b->start_parameter(), b->end_parameter()));
    }
    if (ta == "LWPOLYLINE" && tb == "LWPOLYLINE") {
        auto pa = polyline_points(doc, target);
        auto pb = polyline_points(doc, handle);
        auto closed = pa ? std::optional<bool>{pa->first} : std::nullopt;
        return closed && pb && *closed == pb->first &&
               same_shape(polyline_shape(doc, target), polyline_shape(doc, handle), *closed);
    }
    if (ta == "POLYLINE" && tb == "POLYLINE") {
        if (entity_by_handle<DxfPolyline2D>(doc, target) && entity_by_handle<DxfPolyline2D>(doc, handle)) {
            auto pa = polyline_points(doc, target);
            auto pb = polyline_points(doc, handle);
            auto closed = pa ? std::optional<bool>{pa->first} : std::nullopt;
            return closed && pb && *closed == pb->first &&
                   same_shape(polyline_shape(doc, target), polyline_shape(doc, handle), *closed);
        }
        auto kind = [&](std::uint64_t h) {
            if (entity_by_handle<DxfPolyline2D>(doc, h))
                return 1;
            if (entity_by_handle<DxfPolyline3D>(doc, h))
                return 2;
            if (entity_by_handle<DxfPolygonMesh>(doc, h))
                return 3;
            if (entity_by_handle<DxfPolyfaceMesh>(doc, h))
                return 4;
            return 0;
        };
        auto ka = kind(target), kb = kind(handle);
        return ka != 0 && ka == kb;
    }
    if ((ta == "TEXT" || ta == "MTEXT") && (tb == "TEXT" || tb == "MTEXT"))
        return text_of(doc, target) == text_of(doc, handle);
    if (ta == "SPLINE" && tb == "SPLINE") {
        auto *a = entity_by_handle<DxfSpline>(doc, target);
        auto *b = entity_by_handle<DxfSpline>(doc, handle);
        if (!a || !b)
            return false;
        auto da = a->definition(), db = b->definition();
        if (da.degree != db.degree)
            return false;
        auto pts = [](const SplineDefinition &s) {
            return s.control_points.empty() ? s.fit_points : s.control_points;
        };
        auto na = normalized_xy(pts(da));
        auto nb = normalized_xy(pts(db));
        if (na.size() != nb.size() || na.empty())
            return false;
        for (std::size_t i = 0; i < na.size(); ++i)
            if (!eq(na[i][0], nb[i][0]) || !eq(na[i][1], nb[i][1]))
                return false;
        return true;
    }
    if (ta == "HATCH" && tb == "HATCH") {
        auto *a = entity_by_handle<DxfHatch>(doc, target);
        auto *b = entity_by_handle<DxfHatch>(doc, handle);
        return a && b && same_hatch(a->definition(), b->definition());
    }
    return ta == tb;
}

std::string fmt6(double v) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o.setf(std::ios::fixed);
    o.precision(6);
    o << (v + 0.0);
    return o.str();
}

std::string vec3(Vector3 p) { return fmt6(p.x) + "," + fmt6(p.y) + "," + fmt6(p.z); }

std::optional<std::pair<std::string, Vector3>> shape_signature(const DxfDocument &doc, std::uint64_t handle) {
    auto type = entity_type(doc, handle);
    if (auto *i = insert_by_handle(doc, handle)) {
        auto d = i->definition();
        return std::make_pair("INSERT|" + ascii_upper(d.block_name) + "|" + fmt6(d.rotation) + "|" + fmt6(d.scale.x) +
                                  "|" + fmt6(d.scale.y) + "|" + fmt6(d.scale.z) + "|" + vec3(d.normal),
                              d.insertion);
    }
    if (auto *l = entity_by_handle<DxfLine>(doc, handle)) {
        auto d = l->end() - l->start();
        return std::make_pair("LINE|" + vec3(d), l->start());
    }
    if (auto *c = entity_by_handle<DxfCircle>(doc, handle)) {
        if (type == "ARC")
            ;
        else
            return std::make_pair("CIRCLE|" + fmt6(c->radius()) + "|" + vec3(c->normal()), c->center());
    }
    if (auto *a = entity_by_handle<DxfArc>(doc, handle))
        return std::make_pair("ARC|" + fmt6(a->radius()) + "|" + fmt6(a->start_angle_radians()) + "|" +
                                  fmt6(a->end_angle_radians()) + "|" + vec3(a->normal()),
                              a->center());
    if (auto *pl = entity_by_handle<DxfLwPolyline>(doc, handle)) {
        auto verts = pl->vertices();
        if (verts.empty())
            return std::nullopt;
        auto first = verts.front();
        std::string shape;
        for (std::size_t i = 0; i < verts.size(); ++i) {
            if (i)
                shape += ";";
            shape += fmt6(verts[i].x - first.x) + "," + fmt6(verts[i].y - first.y) + "," + fmt6(verts[i].bulge);
        }
        return std::make_pair("LWPOLYLINE|" + std::string(pl->is_closed() ? "true" : "false") + "|" +
                                  vec3(pl->normal()) + "|" + shape,
                              Vector3{first.x, first.y, pl->elevation()});
    }
    return std::nullopt;
}

std::vector<CountPiece> bulge_pieces(const std::vector<std::array<double, 3>> &v, bool closed, double elevation,
                                     const Matrix3 &m) {
    std::vector<CountPiece> out;
    auto n = v.size();
    if (!n)
        return out;
    auto segs = closed ? n : n - 1;
    auto at = [&](double x, double y) { return m * Vector3{x, y, elevation}; };
    for (std::size_t i = 0; i < segs; ++i) {
        auto a = v[i], b = v[(i + 1) % n];
        auto dx = b[0] - a[0], dy = b[1] - a[1];
        auto chord = std::hypot(dx, dy);
        auto bulge = a[2];
        if (std::abs(bulge) > 1e-12 && chord > 1e-12) {
            auto sweep = 4.0 * std::atan(bulge);
            auto r = chord / (2.0 * std::abs(std::sin(sweep / 2.0)));
            auto h = r * std::cos(sweep / 2.0) * (sweep >= 0 ? 1.0 : -1.0);
            auto cx = (a[0] + b[0]) / 2.0 - dy / chord * h;
            auto cy = (a[1] + b[1]) / 2.0 + dx / chord * h;
            auto a0 = std::atan2(a[1] - cy, a[0] - cx);
            double t0 = sweep > 0.0 ? a0 : a0 + sweep;
            double t1 = sweep > 0.0 ? a0 + sweep : a0;
            out.push_back(CountPiece::conic(at(cx, cy), m * Vector3{r, 0, 0}, m * Vector3{0, r, 0}, t0, t1));
        } else
            out.push_back(CountPiece::segment(at(a[0], a[1]), at(b[0], b[1])));
    }
    return out;
}

#include "count_hatch.inc"

std::vector<CountPiece> outline_of(const DxfDocument &doc, std::uint64_t handle, std::size_t depth);

Vector3 block_base(const DxfDocument &doc, std::string_view name) {
    Vector3 base{};
    doc.for_each<DxfBlock>([&](const DxfBlock &block) {
        if (ascii_ieq(block.name(), name))
            base = block.base_point();
    });
    return base;
}

std::vector<CountPiece> outline_of(const DxfDocument &doc, std::uint64_t handle, std::size_t depth) {
    const double tau = 2 * std::acos(-1.0);
    auto span = [&](double a0, double a1) {
        auto s = std::fmod(a1 - a0, tau);
        if (s < 0)
            s += tau;
        if (s < 1e-12)
            return tau;
        return s;
    };
    if (insert_by_handle(doc, handle))
        return insert_outline(doc, handle, depth);
    if (auto *l = entity_by_handle<DxfLine>(doc, handle))
        return {CountPiece::segment(l->start(), l->end())};
    if (auto *a = entity_by_handle<DxfArc>(doc, handle)) {
        auto m = Matrix3::arbitrary_axis(a->normal());
        auto r = a->radius();
        return {CountPiece::conic(m * a->center(), m * Vector3{r, 0, 0}, m * Vector3{0, r, 0}, a->start_angle_radians(),
                                  a->start_angle_radians() + span(a->start_angle_radians(), a->end_angle_radians()))};
    }
    if (auto *c = entity_by_handle<DxfCircle>(doc, handle)) {
        auto m = Matrix3::arbitrary_axis(c->normal());
        auto r = c->radius();
        return {CountPiece::conic(m * c->center(), m * Vector3{r, 0, 0}, m * Vector3{0, r, 0}, 0.0, tau)};
    }
    if (auto *e = entity_by_handle<DxfEllipse>(doc, handle)) {
        auto n = e->normal();
        auto major = e->major_axis();
        auto minor = Vector3{n.y * major.z - n.z * major.y, n.z * major.x - n.x * major.z, n.x * major.y - n.y * major.x} *
                     e->minor_axis_ratio();
        return {CountPiece::conic(e->center(), major, minor, e->start_parameter(),
                                  e->start_parameter() + span(e->start_parameter(), e->end_parameter()))};
    }
    if (auto *pl = entity_by_handle<DxfLwPolyline>(doc, handle)) {
        std::vector<std::array<double, 3>> v;
        for (const auto &q : pl->vertices())
            v.push_back({q.x, q.y, q.bulge});
        return bulge_pieces(v, pl->is_closed(), pl->elevation(), Matrix3::arbitrary_axis(pl->normal()));
    }
    if (auto *h = entity_by_handle<DxfHatch>(doc, handle))
        return hatch_pieces(h->definition());
    BoundingBox3D box{};
    bool have = false;
    auto take_box = [&](const BoundingBox3D &b) {
        box = b;
        have = true;
    };
    if (auto *p = entity_by_handle<DxfPoint>(doc, handle)) {
        auto loc = p->location();
        take_box({{loc.x, loc.y, loc.z}, {loc.x, loc.y, loc.z}});
    } else if (auto *t = entity_by_handle<DxfText>(doc, handle)) {
        auto loc = t->insertion_point();
        take_box({{loc.x, loc.y, loc.z}, {loc.x, loc.y, loc.z}});
    } else if (auto *t = entity_by_handle<DxfMText>(doc, handle)) {
        auto loc = t->definition().insertion;
        take_box({{loc.x, loc.y, loc.z}, {loc.x, loc.y, loc.z}});
    }
    if (!have)
        return {};
    auto z = box.min.z;
    Vector3 c[4]{{box.min.x, box.min.y, z}, {box.max.x, box.min.y, z}, {box.max.x, box.max.y, z}, {box.min.x, box.max.y, z}};
    std::vector<CountPiece> out;
    for (int i = 0; i < 4; ++i)
        out.push_back(CountPiece::segment(c[i], c[(i + 1) % 4]));
    return out;
}

void nested_matches(const DxfDocument &doc, std::uint64_t handle, std::uint64_t target,
                    std::vector<InsertDefinition> &chain, std::size_t depth,
                    std::vector<std::vector<CountPiece>> &found) {
    auto type = entity_type(doc, handle);
    std::optional<InsertDefinition> placed;
    if (auto *ins = insert_by_handle(doc, handle))
        placed = ins->definition();
    else if (auto *table = entity_by_handle<DxfTable>(doc, handle)) {
        auto def = table->definition();
        InsertDefinition ins;
        ins.block_name = def.block_name;
        if (def.block_record) {
            std::ostringstream o;
            o << std::hex << def.block_record;
            if (auto *rec = doc.block_record(o.str()))
                ins.block_name = rec->name();
        }
        ins.insertion = def.insertion;
        ins.rotation = std::atan2(def.horizontal_direction.y, def.horizontal_direction.x);
        ins.normal = def.normal;
        placed = ins;
    }
    if (placed) {
        if (depth >= 8)
            return;
        chain.push_back(*placed);
        for (const auto &child : doc.entity_handles_in_block(placed->block_name))
            nested_matches(doc, handle_value(child), target, chain, depth + 1, found);
        chain.pop_back();
        return;
    }
    if (auto *ent = entity_by_handle<DxfEntity>(doc, handle))
        if (ent->is_invisible())
            return;
    if (!similar(doc, target, handle))
        return;
    auto outline = outline_of(doc, handle, 0);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        InsertDefinition d = *it;
        auto t = Transform::from_matrix(Matrix4::from_matrix3(Matrix3::arbitrary_axis(d.normal)) *
                                        Matrix4::translation(d.insertion.x, d.insertion.y, d.insertion.z) *
                                        Matrix4::rotation_z(d.rotation) *
                                        Matrix4::scaling(d.scale.x, d.scale.y, d.scale.z));
        auto base = block_base(doc, d.block_name);
        for (auto &piece : outline)
            piece = piece.placed(t, base);
    }
    found.push_back(std::move(outline));
}

} // namespace

std::vector<CountPiece> hatch_outline(const HatchDefinition &hatch) { return hatch_pieces(hatch); }

bool self_intersects(const std::vector<std::array<double, 2>> &ring) {
    auto n = ring.size();
    auto orient = [](std::array<double, 2> p, std::array<double, 2> q, std::array<double, 2> r) {
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
    };
    auto on = [](std::array<double, 2> p, std::array<double, 2> q, std::array<double, 2> r) {
        return r[0] >= std::min(p[0], q[0]) && r[0] <= std::max(p[0], q[0]) && r[1] >= std::min(p[1], q[1]) &&
               r[1] <= std::max(p[1], q[1]);
    };
    auto segments_cross = [&](std::array<double, 2> a, std::array<double, 2> b, std::array<double, 2> c,
                              std::array<double, 2> d) {
        auto d1 = orient(c, d, a), d2 = orient(c, d, b), d3 = orient(a, b, c), d4 = orient(a, b, d);
        return (d1 * d2 < 0.0 && d3 * d4 < 0.0) || (d1 == 0.0 && on(c, d, a)) || (d2 == 0.0 && on(c, d, b)) ||
               (d3 == 0.0 && on(a, b, c)) || (d4 == 0.0 && on(a, b, d));
    };
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 2; j < n; ++j)
            if (!(i == 0 && j == n - 1) &&
                segments_cross(ring[i], ring[(i + 1) % n], ring[j], ring[(j + 1) % n]))
                return true;
    return false;
}

bool valid_boundary(const DxfDocument &doc, std::uint64_t handle) {
    auto *pl = entity_by_handle<DxfLwPolyline>(doc, handle);
    if (!pl)
        return false;
    std::vector<std::array<double, 2>> ring;
    for (const auto &v : pl->vertices())
        ring.push_back({v.x, v.y});
    if (!pl->is_closed() || ring.size() < 3)
        return false;
    for (const auto &v : pl->vertices())
        if (std::abs(v.bulge) >= 1e-12)
            return false;
    return !self_intersects(ring);
}

std::optional<std::vector<std::array<double, 2>>> boundary_polygon(const DxfDocument &doc, std::uint64_t handle) {
    if (!valid_boundary(doc, handle))
        return std::nullopt;
    auto *pl = entity_by_handle<DxfLwPolyline>(doc, handle);
    std::vector<std::array<double, 2>> ring;
    for (const auto &v : pl->vertices())
        ring.push_back({v.x, v.y});
    return ring;
}

CountPiece CountPiece::segment(Vector3 p, Vector3 q) {
    CountPiece piece;
    piece.kind = Kind::Segment;
    piece.a = p;
    piece.b = q;
    return piece;
}

CountPiece CountPiece::conic(Vector3 center, Vector3 major, Vector3 minor, double start, double end) {
    CountPiece piece;
    piece.kind = Kind::Conic;
    piece.c = center;
    piece.u = major;
    piece.v = minor;
    piece.t0 = start;
    piece.t1 = end;
    return piece;
}

Vector3 CountPiece::start() const {
    if (kind == Kind::Segment)
        return a;
    if (kind == Kind::Conic)
        return c + u * std::cos(t0) + v * std::sin(t0);
    if (!bezier.empty()) {
        auto h = bezier.front();
        auto w = h[3] == 0 ? 1.0 : h[3];
        return {h[0] / w, h[1] / w, h[2] / w};
    }
    return {};
}

CountPiece CountPiece::placed(const Transform &transform, Vector3 base) const {
    auto at = [&](Vector3 p) { return transform.apply(p - base); };
    if (kind == Kind::Segment)
        return segment(at(a), at(b));
    if (kind == Kind::Conic) {
        auto c2 = at(c);
        return conic(c2, at(c + u) - c2, at(c + v) - c2, t0, t1);
    }
    CountPiece out;
    out.kind = Kind::Bezier;
    for (auto h : bezier) {
        auto w = h[3] == 0 ? 1.0 : h[3];
        auto q = at(Vector3{h[0] / w, h[1] / w, h[2] / w});
        out.bezier.push_back({q.x * h[3], q.y * h[3], q.z * h[3], h[3]});
    }
    return out;
}

std::vector<Vector3> CountPiece::points() const {
    if (kind == Kind::Segment)
        return {a, b};
    if (kind == Kind::Conic) {
        auto n = static_cast<std::size_t>(std::ceil((t1 - t0) / (5.0 * std::acos(-1.0) / 180.0)));
        if (n < 2)
            n = 2;
        std::vector<Vector3> out;
        for (std::size_t k = 0; k <= n; ++k) {
            auto t = t0 + (t1 - t0) * static_cast<double>(k) / static_cast<double>(n);
            out.push_back(c + u * std::cos(t) + v * std::sin(t));
        }
        return out;
    }
    std::vector<Vector3> out;
    for (auto h : bezier) {
        auto w = h[3] == 0 ? 1.0 : h[3];
        out.push_back({h[0] / w, h[1] / w, h[2] / w});
    }
    return out;
}

std::vector<CountPiece> insert_outline(const DxfDocument &doc, std::uint64_t insert, std::size_t depth) {
    auto *ins = insert_by_handle(doc, insert);
    if (!ins)
        return {};
    auto def = ins->definition();
    auto base = block_base(doc, def.block_name);
    auto t = ins->get_transform();
    std::vector<CountPiece> out;
    for (const auto &h : doc.entity_handles_in_block(def.block_name)) {
        auto child = handle_value(h);
        if (insert_by_handle(doc, child)) {
            if (depth < 8) {
                auto nested = insert_outline(doc, child, depth + 1);
                for (auto &p : nested)
                    out.push_back(p.placed(t, base));
            }
            continue;
        }
        if (entity_type(doc, child) == "ATTDEF")
            continue;
        for (auto p : outline_of(doc, child, 0))
            out.push_back(p.placed(t, base));
    }
    return out;
}

std::vector<CountPiece> entity_outline(const DxfDocument &doc, std::uint64_t handle) {
    return outline_of(doc, handle, 0);
}

std::optional<std::vector<Vector3>> insert_corners(const DxfDocument &doc, std::uint64_t insert, std::size_t depth) {
    std::vector<Vector3> pts;
    for (const auto &piece : insert_outline(doc, insert, depth))
        for (auto p : piece.points())
            pts.push_back(p);
    if (pts.empty())
        return std::nullopt;
    auto lo = pts.front(), hi = pts.front();
    for (auto p : pts) {
        lo.x = std::min(lo.x, p.x);
        lo.y = std::min(lo.y, p.y);
        lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x);
        hi.y = std::max(hi.y, p.y);
        hi.z = std::max(hi.z, p.z);
    }
    std::vector<Vector3> out;
    for (auto x : {lo.x, hi.x})
        for (auto y : {lo.y, hi.y})
            for (auto z : {lo.z, hi.z})
                out.push_back({x, y, z});
    return out;
}

std::vector<std::vector<std::uint64_t>> similar_matches(const DxfDocument &doc, std::uint64_t target,
                                                        const std::optional<std::vector<std::array<double, 2>>> &area) {
    std::vector<std::uint64_t> duplicates;
    for (const auto &i : block_instances(doc, {}))
        if (i.duplicate_of)
            duplicates.push_back(i.handle);
    std::vector<std::vector<std::uint64_t>> out;
    for (const auto &h : doc.model_space_entity_handles()) {
        auto top = handle_value(h);
        if (std::find(duplicates.begin(), duplicates.end(), top) != duplicates.end())
            continue;
        std::vector<InsertDefinition> chain;
        std::vector<std::vector<CountPiece>> found;
        nested_matches(doc, top, target, chain, 0, found);
        for (const auto &outline : found)
            if (!area || inside_polygon(outline, *area))
                out.push_back({top});
    }
    return out;
}

std::vector<std::vector<std::uint64_t>> group_matches(const DxfDocument &doc, const std::vector<std::uint64_t> &targets,
                                                      const std::optional<std::vector<std::array<double, 2>>> &area) {
    if (targets.size() == 1) {
        auto type = entity_type(doc, targets[0]);
        if (type != "INSERT" && !type.empty())
            return similar_matches(doc, targets[0], area);
        if (type.empty())
            return {};
    }
    auto r = [](double v) { return static_cast<std::int64_t>(std::llround(v * 1e6)); };
    auto key_of = [&](const std::string &sig, Vector3 p) {
        return std::make_tuple(sig, r(p.x), r(p.y), r(p.z));
    };
    std::unordered_set<std::uint64_t> duplicates;
    for (const auto &i : block_instances(doc, {}))
        if (i.duplicate_of)
            duplicates.insert(i.handle);
    std::map<std::tuple<std::string, std::int64_t, std::int64_t, std::int64_t>, std::vector<std::uint64_t>> index;
    std::vector<std::tuple<std::uint64_t, std::string, Vector3>> order;
    for (const auto &h : doc.model_space_entity_handles()) {
        auto handle = handle_value(h);
        if (duplicates.count(handle))
            continue;
        if (auto sig = shape_signature(doc, handle)) {
            index[key_of(sig->first, sig->second)].push_back(handle);
            order.emplace_back(handle, sig->first, sig->second);
        }
    }
    std::vector<std::pair<std::string, Vector3>> wanted;
    for (auto t : targets) {
        if (auto s = shape_signature(doc, t))
            wanted.push_back(*s);
        else {
            bool all = true;
            for (auto x : targets)
                if (entity_type(doc, x).empty())
                    all = false;
            if (all)
                return {targets};
            return {};
        }
    }
    if (wanted.empty())
        return {};
    auto sig0 = wanted[0].first;
    auto anchor0 = wanted[0].second;
    std::vector<std::vector<std::uint64_t>> out;
    std::set<std::vector<std::uint64_t>> seen;
    for (const auto &[h, sig, anchor] : order) {
        if (sig != sig0)
            continue;
        auto d = anchor - anchor0;
        std::vector<std::uint64_t> members{h};
        bool ok = true;
        for (std::size_t i = 1; i < wanted.size(); ++i) {
            auto need = key_of(wanted[i].first, wanted[i].second + d);
            auto it = index.find(need);
            if (it == index.end()) {
                ok = false;
                break;
            }
            std::optional<std::uint64_t> pick;
            for (auto m : it->second)
                if (std::find(members.begin(), members.end(), m) == members.end()) {
                    pick = m;
                    break;
                }
            if (!pick) {
                ok = false;
                break;
            }
            members.push_back(*pick);
        }
        if (!ok || members.size() != wanted.size())
            continue;
        if (area) {
            bool inside = true;
            for (auto m : members)
                if (!inside_polygon(entity_outline(doc, m), *area))
                    inside = false;
            if (!inside)
                continue;
        }
        auto sorted = members;
        std::sort(sorted.begin(), sorted.end());
        if (seen.insert(sorted).second)
            out.push_back(members);
    }
    return out;
}

std::optional<CountQuery> parse_query(std::string_view json) {
    while (!json.empty() && std::isspace(static_cast<unsigned char>(json.front())))
        json.remove_prefix(1);
    while (!json.empty() && std::isspace(static_cast<unsigned char>(json.back())))
        json.remove_suffix(1);
    auto type = json_string_field(json, "type");
    if (!type)
        return std::nullopt;
    if (*type == "single") {
        if (json.find("\"targets\"") == std::string_view::npos)
            return std::nullopt;
        CountQuery query;
        query.kind = CountQuery::Kind::Single;
        query.targets = json_hex_array(json, "targets");
        return query;
    }
    if (*type == "block") {
        auto name = json_string_field(json, "name");
        if (!name)
            return std::nullopt;
        auto key = parse_count_key(json);
        if (!key)
            return std::nullopt;
        CountQuery query;
        query.kind = CountQuery::Kind::Block;
        query.name = *name;
        query.key = *key;
        if (auto handle = json_string_field(json, "boundaryObjectHandle"))
            query.boundary = handle_value(*handle);
        return query;
    }
    return std::nullopt;
}

std::string evaluate_count(const DxfDocument &doc, std::string_view code) {
    std::string body(code);
    auto t = body.find_first_not_of(" \t");
    if (t != std::string::npos)
        body.erase(0, t);
    if (body.size() >= 2 && body.substr(0, 2) == "%<" && body.size() >= 4 && body.substr(body.size() - 2) == ">%")
        body = body.substr(2, body.size() - 4);
    if (!body.empty() && body[0] == '\\')
        body.erase(body.begin());
    auto ws = body.find_first_of(" \t");
    auto json = ws == std::string::npos ? std::string{} : body.substr(ws);
    t = json.find_first_not_of(" \t");
    if (t != std::string::npos)
        json.erase(0, t);
    else
        json.clear();
    if (json.empty())
        return "####";
    auto type = json_string_field(json, "type").value_or("");
    if (type == "block") {
        auto name = json_string_field(json, "name").value_or("");
        auto key = parse_count_key(json);
        if (!key)
            return "0";
        std::optional<std::vector<std::array<double, 2>>> area;
        if (auto handle = json_string_field(json, "boundaryObjectHandle"))
            area = boundary_polygon(doc, handle_value(*handle));
        auto instances = block_instances(doc, area);
        return std::to_string(count_of(instances, name, *key));
    }
    auto instances = block_instances(doc);
    if (type == "single") {
        auto targets = json_hex_array(json, "targets");
        if (targets.empty())
            return "0";
        if (targets.size() == 1) {
            if (insert_by_handle(doc, targets[0])) {
                auto *ins = insert_by_handle(doc, targets[0]);
                return std::to_string(count_of(instances, ins->definition().block_name, CountKey{}));
            }
            return std::to_string(group_matches(doc, targets, {}).size());
        }
        return std::to_string(group_matches(doc, targets, {}).size());
    }
    return "0";
}

} // namespace libopencad
