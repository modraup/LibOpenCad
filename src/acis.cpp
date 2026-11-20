// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/entities/acis/types.rs at 99d5f488.
#include "libopencad/acis.hpp"
#include "libopencad/transform.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>

namespace libopencad::acis {
namespace {
template <class T> T little(const std::uint8_t *p) {
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        bits |= std::uint64_t(p[i]) << (8 * i);
    if constexpr (sizeof(T) == 8) {
        T result;
        std::memcpy(&result, &bits, 8);
        return result;
    } else if constexpr (sizeof(T) == 4) {
        auto b = std::uint32_t(bits);
        T result;
        std::memcpy(&result, &b, 4);
        return result;
    } else if constexpr (sizeof(T) == 2) {
        auto b = std::uint16_t(bits);
        T result;
        std::memcpy(&result, &b, 2);
        return result;
    } else {
        auto b = std::uint8_t(bits);
        T result;
        std::memcpy(&result, &b, 1);
        return result;
    }
}
bool valid_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        auto b = std::uint8_t(s[i++]);
        if (b < 0x80)
            continue;
        unsigned n;
        std::uint32_t v, minimum;
        if (b >= 0xc2 && b <= 0xdf) {
            n = 1;
            v = b & 31;
            minimum = 0x80;
        } else if (b >= 0xe0 && b <= 0xef) {
            n = 2;
            v = b & 15;
            minimum = 0x800;
        } else if (b >= 0xf0 && b <= 0xf4) {
            n = 3;
            v = b & 7;
            minimum = 0x10000;
        } else
            return false;
        if (n > s.size() - i)
            return false;
        while (n--) {
            auto c = std::uint8_t(s[i++]);
            if ((c & 0xc0) != 0x80)
                return false;
            v = (v << 6) | (c & 63);
        }
        if (v < minimum || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
            return false;
    }
    return true;
}
std::optional<std::string_view> sab_string(const SabToken &t) {
    const auto n = (t.tag == 7 || t.tag == 13 || t.tag == 14) ? 1u
                   : t.tag == 8                               ? 2u
                   : (t.tag == 9 || t.tag == 18)              ? 4u
                                                              : 0u;
    if (!n || t.data.size() < n)
        return {};
    const auto len = n == 1   ? std::uint32_t(t.data[0])
                     : n == 2 ? std::uint32_t(little<std::uint16_t>(t.data.data()))
                              : little<std::uint32_t>(t.data.data());
    if (len > t.data.size() - n)
        return {};
    const std::string_view value(reinterpret_cast<const char *>(t.data.data() + n), len);
    return valid_utf8(value) ? std::optional<std::string_view>{value} : std::nullopt;
}
SatPointer p(const SatRecord &r, std::size_t n) { return r.token_pointer(n).value_or(SatPointer{}); }
Vector3 xyz(const SatRecord &r, std::size_t n, Vector3 fallback = {}) {
    return {r.token_float(n).value_or(fallback.x), r.token_float(n + 1).value_or(fallback.y),
            r.token_float(n + 2).value_or(fallback.z)};
}
bool core(std::string_view type) {
    const auto base = base_entity_type(type);
    for (auto t : {"body", "lump", "shell", "subshell", "face", "loop", "coedge", "edge", "vertex", "wire",
                   "point", "curve", "surface", "pcurve", "transform", "asmheader"})
        if (base == t)
            return true;
    return false;
}
std::vector<SatToken> missing_tail(const SatRecord &r) {
    std::size_t keywords = 0;
    for (std::size_t i = 1; i < r.tokens.size(); ++i) {
        const auto &t = r.tokens[i];
        if (!t.as_pointer() && !t.as_float() && !t.coordinates())
            ++keywords;
    }
    if ((r.entity_type == "plane-surface" || r.entity_type == "sphere-surface" ||
         r.entity_type == "torus-surface") &&
        keywords == 0)
        return {SatToken(SatIdent{"forward_v"}), SatToken(SatIdent{"I"}), SatToken(SatIdent{"I"}),
                SatToken(SatIdent{"I"}), SatToken(SatIdent{"I"})};
    if (r.entity_type == "cone-surface" && keywords <= 2)
        return {SatToken(SatIdent{"forward"}), SatToken(SatIdent{"I"}), SatToken(SatIdent{"I"}),
                SatToken(SatIdent{"I"}), SatToken(SatIdent{"I"})};
    if ((r.entity_type == "straight-curve" || r.entity_type == "ellipse-curve") && keywords == 0)
        return {SatToken(SatIdent{"I"}), SatToken(SatIdent{"I"})};
    if (r.entity_type == "edge" && r.tokens.size() == 8)
        return {SatToken(SatString{"unknown"})};
    return {};
}
} // namespace
std::uint32_t SatVersion::number() const {
    const auto n = std::uint64_t(major) * 100 + std::uint64_t(minor) * 10 + patch;
    if (n > UINT32_MAX)
        throw std::overflow_error("SAT version overflow");
    return static_cast<std::uint32_t>(n);
}
SatVersion SatVersion::from_number(std::uint32_t n) { return {n / 100, (n % 100) / 10, n % 10}; }
std::string_view base_entity_type(std::string_view type) {
    auto at = type.rfind('-');
    return at == type.npos ? type : type.substr(at + 1);
}
std::optional<double> SatToken::as_float() const {
    if (auto v = std::get_if<double>(&value))
        return *v;
    if (auto v = std::get_if<std::int64_t>(&value))
        return double(*v);
    if (auto v = std::get_if<SabToken>(&value)) {
        const auto *b = v->data.data();
        const auto n = v->data.size();
        switch (v->tag) {
        case 2:
            if (n == 1)
                return little<std::int8_t>(b);
            break;
        case 3:
            if (n == 2)
                return little<std::int16_t>(b);
            break;
        case 4:
        case 21:
            if (n == 4)
                return little<std::int32_t>(b);
            break;
        case 5:
            if (n == 4)
                return little<float>(b);
            break;
        case 6:
            if (n == 8)
                return little<double>(b);
            break;
        case 23:
            if (n == 8)
                return double(little<std::int64_t>(b));
            break;
        }
    }
    return {};
}
std::optional<std::int64_t> SatToken::as_integer() const {
    if (auto v = std::get_if<std::int64_t>(&value))
        return *v;
    if (auto v = std::get_if<SabToken>(&value); v && v->tag == 23 && v->data.size() == 8)
        return little<std::int64_t>(v->data.data());
    auto f = as_float();
    if (!f || !std::isfinite(*f) || std::trunc(*f) != *f || *f < double(INT64_MIN) || *f > double(INT64_MAX))
        return {};
    // Rust's floating cast saturates, including i64::MAX rounded up to 2^63.
    if (*f >= double(INT64_MAX))
        return INT64_MAX;
    return static_cast<std::int64_t>(*f);
}
std::optional<SatPointer> SatToken::as_pointer() const {
    if (auto v = std::get_if<SatPointer>(&value))
        return *v;
    return {};
}
std::optional<std::string_view> SatToken::as_ident() const {
    if (auto v = std::get_if<SatIdent>(&value))
        return v->value;
    if (auto v = std::get_if<SatEnum>(&value))
        return v->value;
    if (auto v = std::get_if<SabToken>(&value)) {
        if (v->tag == 15)
            return "{";
        if (v->tag == 16)
            return "}";
        return sab_string(*v);
    }
    return {};
}
std::optional<std::string_view> SatToken::as_string() const {
    if (auto v = std::get_if<SatString>(&value))
        return v->value;
    if (auto v = std::get_if<SabToken>(&value))
        return sab_string(*v);
    return {};
}
std::optional<std::pair<std::array<double, 3>, std::size_t>> SatToken::coordinates() const {
    if (auto v = std::get_if<Vector3>(&value))
        return std::make_pair(std::array<double, 3>{v->x, v->y, v->z}, 3u);
    if (auto v = std::get_if<SabToken>(&value)) {
        const auto n = (v->tag == 19 || v->tag == 20) ? 3u : v->tag == 22 ? 2u : 0u;
        if (n && v->data.size() == n * 8)
            return std::make_pair(std::array<double, 3>{little<double>(v->data.data()),
                                                        little<double>(v->data.data() + 8),
                                                        n == 3 ? little<double>(v->data.data() + 16) : 0},
                                  n);
    }
    return {};
}
bool SatRecord::is_a(std::string_view type) const {
    return entity_type == type || base_entity_type(entity_type) == type;
}
std::vector<SatPointer> SatRecord::pointers() const {
    std::vector<SatPointer> out{attribute};
    for (const auto &t : tokens)
        if (auto v = t.as_pointer())
            out.push_back(*v);
    return out;
}
std::optional<double> SatRecord::token_float(std::size_t index) const {
    std::size_t slot = 0;
    for (const auto &t : tokens) {
        if (auto c = t.coordinates()) {
            if (index >= slot && index - slot < c->second)
                return c->first[index - slot];
            slot += c->second;
        } else {
            if (slot == index)
                return t.as_float();
            ++slot;
        }
    }
    return {};
}
std::optional<std::int64_t> SatRecord::token_integer(std::size_t i) const {
    return i < tokens.size() ? tokens[i].as_integer() : std::nullopt;
}
std::optional<SatPointer> SatRecord::token_pointer(std::size_t i) const {
    return i < tokens.size() ? tokens[i].as_pointer() : std::nullopt;
}
std::optional<SatPointer> SatRecord::nth_pointer(std::size_t i) const {
    for (const auto &t : tokens)
        if (auto v = t.as_pointer()) {
            if (!i--)
                return v;
        }
    return {};
}
std::optional<std::string_view> SatRecord::token_string(std::size_t i) const {
    if (i >= tokens.size())
        return {};
    auto v = tokens[i].as_ident();
    return v ? v : tokens[i].as_string();
}
Sense SatRecord::token_sense(std::size_t i) const {
    if (auto n = token_integer(i))
        return *n ? Sense::Reversed : Sense::Forward;
    return token_string(i) == "reversed" ? Sense::Reversed : Sense::Forward;
}
Vector3 SatPlacement::apply(Vector3 p) const {
    return Vector3{p.x * matrix[0][0] + p.y * matrix[1][0] + p.z * matrix[2][0],
                   p.x * matrix[0][1] + p.y * matrix[1][1] + p.z * matrix[2][1],
                   p.x * matrix[0][2] + p.y * matrix[1][2] + p.z * matrix[2][2]} *
               scale +
           translation;
}
const SatRecord *SatDocument::record(std::int32_t i) const {
    if (i < 0)
        return nullptr;
    if (std::size_t(i) < records.size() && records[i].index == i)
        return &records[i];
    for (const auto &r : records)
        if (r.index == i)
            return &r;
    return nullptr;
}
SatRecord *SatDocument::record(std::int32_t i) {
    return const_cast<SatRecord *>(static_cast<const SatDocument *>(this)->record(i));
}
std::int32_t SatDocument::add_record(SatRecord r) {
    if (records.size() >= INT32_MAX)
        throw std::length_error("SAT record limit");
    r.index = static_cast<std::int32_t>(records.size());
    records.push_back(std::move(r));
    header.num_records = records.size();
    return records.back().index;
}
std::vector<const SatRecord *> SatDocument::records_of_type(std::string_view type) const {
    std::vector<const SatRecord *> out;
    for (const auto &r : records)
        if (r.entity_type == type)
            out.push_back(&r);
    return out;
}
SatPlacement SatDocument::placement() const {
    for (const auto &r : records)
        if (r.entity_type == "transform") {
            std::vector<double> v;
            for (const auto &t : r.tokens) {
                if (v.size() >= 13)
                    break;
                if (auto c = t.coordinates())
                    v.insert(v.end(), c->first.begin(), c->first.begin() + c->second);
                else if (auto f = t.as_float())
                    v.push_back(*f);
                else if (auto s = t.as_string()) {
                    std::istringstream in{std::string(*s)};
                    in.imbue(std::locale::classic());
                    double f;
                    while (v.size() < 13 && in >> f)
                        v.push_back(f);
                }
            }
            if (v.size() < 13)
                return {};
            return {
                {{{v[0], v[1], v[2]}, {v[3], v[4], v[5]}, {v[6], v[7], v[8]}}}, {v[9], v[10], v[11]}, v[12]};
        }
    return {};
}
void SatDocument::set_placement(const SatPlacement &a) {
    std::vector<SatToken> ts{SatToken(SatPointer{})};
    bool rotated = false, sheared = false;
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 3; ++j) {
            if (!std::isfinite(a.matrix[i][j]))
                throw std::invalid_argument("Non-finite SAT placement");
            ts.emplace_back(a.matrix[i][j]);
            if (i != j && std::abs(a.matrix[i][j]) > 1e-10)
                rotated = true;
        }
    for (auto f : {a.translation.x, a.translation.y, a.translation.z, a.scale}) {
        if (!std::isfinite(f))
            throw std::invalid_argument("Non-finite SAT placement");
        ts.emplace_back(f);
    }
    const auto &m = a.matrix;
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                       m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = i + 1; j < 3; ++j)
            if (std::abs(m[i][0] * m[j][0] + m[i][1] * m[j][1] + m[i][2] * m[j][2]) > 1e-10)
                sheared = true;
    for (auto s : {rotated ? "rotate" : "no_rotate", det * a.scale < 0 ? "reflect" : "no_reflect",
                   sheared ? "shear" : "no_shear"})
        ts.emplace_back(SatIdent{s});
    for (auto &r : records)
        if (r.entity_type == "transform") {
            r.tokens = std::move(ts);
            r.raw_text.reset();
            return;
        }
    // Stage the append so allocation failures cannot orphan a lifted end marker.
    auto copy = *this;
    std::optional<SatRecord> term;
    auto it = std::find_if(copy.records.begin(), copy.records.end(),
                           [](const auto &r) { return r.entity_type.rfind("End-of", 0) == 0; });
    if (it != copy.records.end()) {
        term = *it;
        copy.records.erase(it);
    }
    SatRecord r;
    r.entity_type = "transform";
    r.tokens = std::move(ts);
    const auto id = copy.add_record(std::move(r));
    for (auto &body : copy.records)
        if (body.entity_type == "body") {
            if (body.tokens.size() >= 4) {
                body.tokens[3] = SatToken(SatPointer{id});
                body.raw_text.reset();
            }
            break;
        }
    if (term)
        copy.records.push_back(std::move(*term));
    copy.header.num_records = copy.records.size();
    *this = std::move(copy);
}
void SatDocument::apply_transform(const libopencad::Transform &transform) {
    const auto old = placement();
    const auto &m = transform.matrix.m;
    for (const auto &row : m)
        for (auto value : row)
            if (!std::isfinite(value))
                throw std::invalid_argument("Non-finite modeler transform");
    SatPlacement result;
    result.scale = 1;
    double linear[3][3];
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 3; ++j)
            linear[i][j] = old.scale * old.matrix[j][i];
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 3; ++j)
            result.matrix[j][i] = m[i][0] * linear[0][j] + m[i][1] * linear[1][j] + m[i][2] * linear[2][j];
    const auto t = old.translation;
    result.translation = {m[0][0] * t.x + m[0][1] * t.y + m[0][2] * t.z + m[0][3],
                          m[1][0] * t.x + m[1][1] * t.y + m[1][2] * t.z + m[1][3],
                          m[2][0] * t.x + m[2][1] * t.y + m[2][2] * t.z + m[2][3]};
    set_placement(result);
}
std::vector<SatValidationError> SatDocument::validate() const {
    // Match upstream's bounds validation; this does not prove B-rep consistency.
    std::vector<SatValidationError> out;
    for (const auto &r : records) {
        auto check = [&](SatPointer p, std::string context) {
            if (!p.is_null() && std::size_t(p.value) >= records.size())
                out.push_back({r.index, p.value, std::move(context)});
        };
        check(r.attribute, "attribute");
        for (std::size_t i = 0; i < r.tokens.size(); ++i)
            if (auto p = r.tokens[i].as_pointer())
                check(*p, "token[" + std::to_string(i) + "]");
    }
    return out;
}
void SatDocument::complete_brep_links() {
    std::map<std::int32_t, std::int32_t> ve, ec;
    for (const auto &r : records)
        if (r.is_a("edge")) {
            for (auto n : {1u, 3u})
                if (auto v = r.token_pointer(n); v && !v->is_null())
                    ve.emplace(v->value, r.index);
        } else if (r.is_a("coedge")) {
            if (auto e = r.token_pointer(4); e && !e->is_null())
                ec.emplace(e->value, r.index);
        }
    for (auto &r : records) {
        const auto slot = r.is_a("vertex") ? 1u : 5u;
        const auto &map = r.is_a("vertex") ? ve : ec;
        if (!r.is_a("vertex") && !r.is_a("edge"))
            continue;
        auto it = map.find(r.index);
        if (p(r, slot).is_null() && it != map.end()) {
            if (r.tokens.size() <= slot)
                throw std::invalid_argument("Incomplete SAT topology record");
        }
    }
    for (auto &r : records) {
        const auto slot = r.is_a("vertex") ? 1u : 5u;
        const auto &map = r.is_a("vertex") ? ve : ec;
        if (!r.is_a("vertex") && !r.is_a("edge"))
            continue;
        auto it = map.find(r.index);
        if (p(r, slot).is_null() && it != map.end()) {
            r.tokens[slot] = SatToken(SatPointer{it->second});
            r.raw_text.reset();
        }
    }
}
void SatDocument::strip_for_sab() {
    std::vector<std::int32_t> map(records.size(), -1);
    std::int32_t count = 0;
    for (std::size_t i = 0; i < records.size(); ++i)
        if (core(records[i].entity_type) || records[i].entity_type == "color-adesk-attrib" ||
            records[i].entity_type == "truecolor-adesk-attrib")
            map[i] = count++;
    if (std::size_t(count) == records.size())
        return;
    auto remap = [&](SatPointer p) {
        return SatPointer{!p.is_null() && std::size_t(p.value) < map.size() ? map[p.value] : -1};
    };
    std::vector<SatRecord> out;
    for (std::size_t i = 0; i < records.size(); ++i)
        if (map[i] >= 0) {
            auto r = records[i];
            r.index = map[i];
            r.attribute = remap(r.attribute);
            r.raw_text.reset();
            for (auto &t : r.tokens)
                if (auto p = t.as_pointer())
                    t = SatToken(remap(*p));
            out.push_back(std::move(r));
        }
    records = std::move(out);
    header.num_records = records.size();
    header.spatial_resolution = 1;
}
std::optional<SatDocument> SatDocument::completed_for_restore() const {
    if (header.version.major < 7)
        return {};
    bool roots = header.num_bodies == 0 && std::any_of(records.begin(), records.end(),
                                                       [](const auto &r) { return r.entity_type == "body"; });
    auto out = *this;
    bool changed = roots;
    for (auto &r : out.records) {
        auto tail = missing_tail(r);
        if (!tail.empty()) {
            r.tokens.insert(r.tokens.end(), tail.begin(), tail.end());
            r.raw_text.reset();
            changed = true;
        }
    }
    if (!changed)
        return {};
    if (roots) {
        std::vector<std::size_t> order(records.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
            return (records[a].entity_type != "body") < (records[b].entity_type != "body");
        });
        std::vector<std::int32_t> map(order.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            map[order[i]] = static_cast<std::int32_t>(i);
        auto remap = [&](SatPointer &p) {
            if (!p.is_null() && std::size_t(p.value) < map.size())
                p.value = map[p.value];
        };
        auto old = std::move(out.records);
        out.header.num_bodies = 0;
        for (std::size_t i = 0; i < order.size(); ++i) {
            auto r = std::move(old[order[i]]);
            r.index = static_cast<std::int32_t>(i);
            r.raw_text.reset();
            remap(r.attribute);
            for (auto &t : r.tokens)
                if (auto p = std::get_if<SatPointer>(&t.value))
                    remap(*p);
            if (r.entity_type == "body")
                ++out.header.num_bodies;
            out.records.push_back(std::move(r));
        }
    }
    return out;
}
std::vector<SatDocument> SatDocument::split_bodies() const {
    std::vector<std::size_t> bodies;
    for (std::size_t i = 0; i < records.size(); ++i)
        if (records[i].entity_type == "body")
            bodies.push_back(i);
    if (bodies.size() <= 1)
        return {*this};
    auto position = [&](SatPointer p) -> std::optional<std::size_t> {
        auto r = resolve(p);
        if (!r)
            return {};
        return std::size_t(r - records.data());
    };
    std::vector<SatDocument> out;
    for (auto body : bodies) {
        std::vector<bool> keep(records.size(), false);
        for (std::size_t i = 0; i < records.size(); ++i)
            keep[i] = records[i].entity_type == "asmheader";
        std::vector<std::size_t> pending{body};
        keep[body] = true;
        while (!pending.empty()) {
            auto at = pending.back();
            pending.pop_back();
            for (auto p : records[at].pointers())
                if (auto next = position(p); next && !keep[*next] && records[*next].entity_type != "body") {
                    keep[*next] = true;
                    pending.push_back(*next);
                }
        }
        std::vector<std::int32_t> map(records.size(), -1);
        std::int32_t count = 0;
        for (std::size_t i = 0; i < keep.size(); ++i)
            if (keep[i])
                map[i] = count++;
        auto remap = [&](SatPointer p) {
            auto at = position(p);
            return SatPointer{at ? map[*at] : -1};
        };
        SatDocument d;
        d.header = header;
        for (std::size_t i = 0; i < records.size(); ++i)
            if (keep[i]) {
                auto r = records[i];
                r.index = map[i];
                r.attribute = remap(r.attribute);
                r.raw_text.reset();
                for (auto &t : r.tokens)
                    if (auto p = t.as_pointer())
                        t = SatToken(remap(*p));
                d.records.push_back(std::move(r));
            }
        d.header.num_records = d.records.size();
        d.header.num_bodies = 1;
        d.header.has_history = false;
        out.push_back(std::move(d));
    }
    return out;
}
std::optional<SatBody> SatBody::from_record(const SatRecord &r) {
    if (!r.is_a("body"))
        return {};
    return SatBody{p(r, 0), p(r, 1), p(r, 2), p(r, 3)};
}
std::optional<SatLump> SatLump::from_record(const SatRecord &r) {
    if (!r.is_a("lump"))
        return {};
    return SatLump{p(r, 1), p(r, 2), p(r, 3)};
}
std::optional<SatShell> SatShell::from_record(const SatRecord &r) {
    if (!r.is_a("shell"))
        return {};
    return SatShell{p(r, 1), p(r, 2), p(r, 3), p(r, 4), p(r, 5)};
}
std::optional<SatFace> SatFace::from_record(const SatRecord &r) {
    if (!r.is_a("face"))
        return {};
    return SatFace{r.attribute,
                   p(r, 1),
                   p(r, 2),
                   p(r, 3),
                   p(r, 4),
                   p(r, 5),
                   r.token_string(6) == "reversed" ? Sense::Reversed : Sense::Forward,
                   r.token_string(7) == "double" ? Sidedness::Double : Sidedness::Single};
}
std::optional<SatLoop> SatLoop::from_record(const SatRecord &r) {
    if (!r.is_a("loop"))
        return {};
    return SatLoop{p(r, 1), p(r, 2), p(r, 3)};
}
std::optional<SatCoedge> SatCoedge::from_record(const SatRecord &r) {
    if (!r.is_a("coedge"))
        return {};
    return SatCoedge{p(r, 1), p(r, 2), p(r, 3), p(r, 4), r.token_sense(5), p(r, 6), p(r, 7)};
}
std::optional<SatEdge> SatEdge::from_record(const SatRecord &r) {
    if (!r.is_a("edge"))
        return {};
    return SatEdge{p(r, 1),         r.token_float(2).value_or(0),
                   p(r, 3),         r.token_float(4).value_or(0),
                   p(r, 5),         p(r, 6),
                   r.token_sense(7)};
}
std::optional<SatVertex> SatVertex::from_record(const SatRecord &r) {
    if (!r.is_a("vertex"))
        return {};
    return SatVertex{r.nth_pointer(1).value_or(SatPointer{}), r.nth_pointer(2).value_or(SatPointer{})};
}
std::optional<SatPoint> SatPoint::from_record(const SatRecord &r) {
    if (r.entity_type != "point")
        return {};
    return SatPoint{xyz(r, 1)};
}
std::optional<SatStraightCurve> SatStraightCurve::from_record(const SatRecord &r) {
    if (r.entity_type != "straight-curve")
        return {};
    return SatStraightCurve{xyz(r, 1), xyz(r, 4, {0, 0, 1})};
}
std::optional<SatEllipseCurve> SatEllipseCurve::from_record(const SatRecord &r) {
    if (r.entity_type != "ellipse-curve")
        return {};
    return SatEllipseCurve{xyz(r, 1), xyz(r, 4, {0, 0, 1}), xyz(r, 7, {1, 0, 0}),
                           r.token_float(10).value_or(1)};
}
std::optional<SatPlaneSurface> SatPlaneSurface::from_record(const SatRecord &r) {
    if (r.entity_type != "plane-surface")
        return {};
    return SatPlaneSurface{xyz(r, 1), xyz(r, 4, {0, 0, 1}), xyz(r, 7, {1, 0, 0}), r.token_sense(10)};
}
std::optional<SatConeSurface> SatConeSurface::from_record(const SatRecord &r) {
    if (r.entity_type != "cone-surface")
        return {};
    return SatConeSurface{xyz(r, 1),
                          xyz(r, 4, {0, 0, 1}),
                          xyz(r, 7, {1, 0, 0}),
                          r.token_float(10).value_or(1),
                          r.token_float(13).value_or(0),
                          r.token_float(14).value_or(1),
                          r.token_float(15).value_or(1),
                          r.token_sense(16)};
}
std::optional<SatSphereSurface> SatSphereSurface::from_record(const SatRecord &r) {
    if (r.entity_type != "sphere-surface")
        return {};
    return SatSphereSurface{xyz(r, 1), r.token_float(4).value_or(1), xyz(r, 5, {1, 0, 0}),
                            xyz(r, 8, {0, 0, 1}), r.token_sense(11)};
}
std::optional<SatTorusSurface> SatTorusSurface::from_record(const SatRecord &r) {
    if (r.entity_type != "torus-surface")
        return {};
    return SatTorusSurface{xyz(r, 1),
                           xyz(r, 4, {0, 0, 1}),
                           r.token_float(7).value_or(1),
                           r.token_float(8).value_or(0.5),
                           xyz(r, 9, {1, 0, 0}),
                           r.token_sense(12)};
}
void SatDocument::upgrade_to_v700() {
    if (header.version.major >= 7)
        return;
    auto upgraded = *this;
    auto point = [&](SatPointer p) -> std::optional<Vector3> {
        auto r = resolve(p);
        if (!r)
            return {};
        auto v = SatVertex::from_record(*r);
        if (!v)
            return {};
        r = resolve(v->point);
        if (!r)
            return {};
        auto q = SatPoint::from_record(*r);
        if (!q)
            return {};
        return q->position;
    };
    for (auto &r : upgraded.records) {
        auto e = SatEdge::from_record(r);
        if (!e)
            continue;
        if (r.tokens.size() >= 8 && e->start_param == 0 && e->end_param == 0) {
            auto start = point(e->start_vertex), end = point(e->end_vertex);
            const auto *curve = resolve(e->curve);
            if (start && end && curve) {
                std::optional<std::pair<double, double>> params;
                double sign = e->sense == Sense::Reversed ? -1 : 1;
                if (auto line = SatStraightCurve::from_record(*curve)) {
                    auto length = line->direction.dot(line->direction);
                    params = {{sign * (*start - line->root_point).dot(line->direction) / length,
                               sign * (*end - line->root_point).dot(line->direction) / length}};
                } else if (auto ellipse = SatEllipseCurve::from_record(*curve)) {
                    auto normal = ellipse->normal / ellipse->normal.length();
                    auto minor = normal.cross(ellipse->major_axis);
                    auto angle = [&](Vector3 p) {
                        auto q = p - ellipse->center;
                        return std::atan2(q.dot(minor) / ellipse->ratio, q.dot(ellipse->major_axis));
                    };
                    auto from = sign * angle(*start), to = sign * angle(*end);
                    if (e->start_vertex.value == e->end_vertex.value || to <= from)
                        to += 6.283185307179586476925286766559;
                    params = {{from, to}};
                } else if (auto spline = SatIntCurve::from_record(*curve)) {
                    if (auto decoded = spline->bspline()) {
                        const auto lo = decoded->knots[decoded->degree];
                        const auto hi = decoded->knots[decoded->knots.size() - decoded->degree - 1];
                        for (const auto &token : curve->tokens) {
                            auto name = token.as_ident();
                            if (name == "{")
                                break;
                            if (name == "reversed") {
                                sign = -sign;
                                break;
                            }
                        }
                        if (e->start_vertex.value == e->end_vertex.value)
                            params = sign < 0 ? std::make_pair(-hi, -lo) : std::make_pair(lo, hi);
                        else {
                            auto nearest = [&](Vector3 point) -> std::optional<double> {
                                double a = lo, b = hi, best = lo;
                                for (unsigned iteration = 0; iteration < 8; ++iteration) {
                                    const auto step = (b - a) / 32.;
                                    auto samples = spline->sample_range(a, b, 32);
                                    if (samples.empty())
                                        return {};
                                    std::size_t index = 0;
                                    double minimum = std::numeric_limits<double>::infinity();
                                    for (std::size_t i = 0; i < samples.size(); ++i) {
                                        auto q = point - samples[i];
                                        auto distance = q.x * q.x + q.y * q.y + q.z * q.z;
                                        if (!std::isfinite(distance))
                                            throw std::invalid_argument(
                                                "Non-finite legacy spline projection");
                                        if (distance < minimum) {
                                            minimum = distance;
                                            index = i;
                                        }
                                    }
                                    best = a + step * double(index);
                                    a = std::max(best - step, lo);
                                    b = std::min(best + step, hi);
                                }
                                return best;
                            };
                            auto a = nearest(*start), b = nearest(*end);
                            if (a && b)
                                params = std::make_pair(sign * *a, sign * *b);
                        }
                    }
                }
                if (params) {
                    if (!std::isfinite(params->first) || !std::isfinite(params->second))
                        throw std::invalid_argument("Degenerate legacy ACIS curve parameters");
                    r.tokens[2] = SatToken(params->first);
                    r.tokens[4] = SatToken(params->second);
                }
            }
        }
        if (r.tokens.size() == 8)
            r.tokens.emplace_back(SatString{"unknown"});
        r.raw_text.reset();
    }
    upgraded.header.version = {7, 0, 0};
    *this = std::move(upgraded);
}
} // namespace libopencad::acis
