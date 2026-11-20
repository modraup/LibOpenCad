// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/entities/acis/sab.rs at 99d5f488.
#include "libopencad/acis.hpp"
#include "acis_sab_internal.hpp"
#include <algorithm>
#include <cstring>
#include <tuple>
#include <limits>

namespace libopencad::acis {
namespace {
bool suffix(std::string_view s, std::string_view end) {
    return s.size() >= end.size() && s.substr(s.size() - end.size()) == end;
}
bool utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        auto b = std::uint8_t(s[i++]);
        if (b < 0x80)
            continue;
        unsigned n;
        std::uint32_t v, min;
        if (b >= 0xc2 && b <= 0xdf) {
            n = 1;
            v = b & 31;
            min = 0x80;
        } else if (b >= 0xe0 && b <= 0xef) {
            n = 2;
            v = b & 15;
            min = 0x800;
        } else if (b >= 0xf0 && b <= 0xf4) {
            n = 3;
            v = b & 7;
            min = 0x10000;
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
        if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
            return false;
    }
    return true;
}
struct Reader {
    std::string_view data;
    std::size_t at = 0;
    void need(std::size_t n) const {
        if (n > data.size() - at)
            throw SabError("Unexpected end of SAB data", at);
    }
    std::uint8_t peek() const {
        need(1);
        return data[at];
    }
    std::uint8_t byte() {
        auto v = peek();
        ++at;
        return v;
    }
    std::uint64_t bits(std::size_t n) {
        need(n);
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < n; ++i)
            v |= std::uint64_t(static_cast<unsigned char>(data[at++])) << (8 * i);
        return v;
    }
    std::int32_t signed32() {
        auto b = std::uint32_t(bits(4));
        std::int32_t v;
        std::memcpy(&v, &b, 4);
        return v;
    }
    double real() {
        auto b = bits(8);
        double v;
        std::memcpy(&v, &b, 8);
        return v;
    }
    std::string string(std::size_t prefix) {
        auto n = bits(prefix);
        if (n > data.size() - at)
            throw SabError("Truncated SAB string", at);
        std::string s(reinterpret_cast<const char *>(data.data() + at), std::size_t(n));
        if (!utf8(s))
            throw SabError("Invalid SAB UTF-8", at);
        at += std::size_t(n);
        return s;
    }
    std::string tagged_string() {
        auto pos = at;
        auto tag = byte();
        auto n = tag == 7 ? 1 : tag == 8 ? 2 : (tag == 9 || tag == 18) ? 4 : 0;
        if (!n)
            throw SabError("Invalid SAB string tag", pos);
        return string(n);
    }
    double tagged_real() {
        auto pos = at;
        if (byte() != 6)
            throw SabError("Invalid SAB double tag", pos);
        return real();
    }
    SatToken token(bool retain = true) {
        auto pos = at;
        auto tag = byte();
        if (tag == 12)
            return SatToken(SatPointer{signed32()});
        if (tag == 10 || tag == 11)
            return SatToken(tag == 11);
        if (tag == 17)
            return SatToken(SatTerminator{});
        auto start = at;
        std::size_t n = 0, prefix = 0;
        switch (tag) {
        case 2:
            n = 1;
            break;
        case 3:
            n = 2;
            break;
        case 4:
        case 5:
        case 21:
            n = 4;
            break;
        case 6:
        case 23:
            n = 8;
            break;
        case 19:
        case 20:
            n = 24;
            break;
        case 22:
            n = 16;
            break;
        case 7:
        case 13:
        case 14:
            prefix = 1;
            break;
        case 8:
            prefix = 2;
            break;
        case 9:
        case 18:
            prefix = 4;
            break;
        case 15:
        case 16:
            break;
        default:
            throw SabError("Unknown SAB tag " + std::to_string(tag), pos);
        }
        if (prefix)
            n = std::size_t(bits(prefix));
        need(n);
        at += n;
        if (!retain)
            return SatToken(SatTerminator{});
        return SatToken(SabToken{tag, {data.begin() + start, data.begin() + at}});
    }
};
void booleans(SatRecord &r) {
    std::vector<std::size_t> ids;
    for (std::size_t i = 0; i < r.tokens.size(); ++i)
        if (std::holds_alternative<bool>(r.tokens[i].value))
            ids.push_back(i);
    auto set = [&](std::size_t i, const char *t, const char *f, bool ident = false) {
        auto name = std::get<bool>(r.tokens[i].value) ? t : f;
        r.tokens[i] = ident ? SatToken(SatIdent{name}) : SatToken(SatEnum{name});
    };
    const auto base = base_entity_type(r.entity_type);
    if (base == "face" || base == "transform") {
        const auto begin = ids.size() > 3 ? ids.size() - 3 : 0;
        for (std::size_t i = begin; i < ids.size(); ++i) {
            auto role = i - begin;
            if (base == "face")
                set(ids[i],
                    role == 0   ? "forward"
                    : role == 1 ? "single"
                                : "out",
                    role == 0   ? "reversed"
                    : role == 1 ? "double"
                                : "in");
            else
                set(ids[i],
                    role == 0   ? "no_rotate"
                    : role == 1 ? "no_reflect"
                                : "no_shear",
                    role == 0   ? "rotate"
                    : role == 1 ? "reflect"
                                : "shear",
                    true);
        }
    } else if (base == "wire") {
        if (!ids.empty())
            set(ids.back(), "out", "in");
    } else if (base == "edge" || base == "coedge") {
        if (!ids.empty())
            set(ids.front(), "forward", "reversed");
    } else if (r.entity_type == "cone-surface") {
        for (std::size_t i = 0; i < ids.size(); ++i)
            set(ids[i], i == 2 ? "forward" : "I", i == 2 ? "reversed" : "F");
    } else if (suffix(r.entity_type, "-surface")) {
        for (std::size_t i = 0; i < ids.size(); ++i)
            set(ids[i], i == 0 ? "forward_v" : "I", i == 0 ? "reverse_v" : "F");
    } else if (suffix(r.entity_type, "-curve")) {
        const bool sense = r.entity_type != "straight-curve" && r.entity_type != "ellipse-curve";
        for (std::size_t i = 0; i < ids.size(); ++i)
            set(ids[i], sense && i == 0 ? "forward" : "I", sense && i == 0 ? "reversed" : "F");
    }
}
void keywords(SatRecord &r) {
    auto &ts = r.tokens;
    std::size_t depth = 0;
    std::optional<std::size_t> role;
    bool parcur = false;
    auto ident = [&](std::size_t i) {
        return i < ts.size() ? ts[i].as_ident() : std::optional<std::string_view>{};
    };
    for (std::size_t i = 0; i < ts.size(); ++i) {
        auto name = ident(i);
        if (name == "{") {
            ++depth;
            if (depth == 1) {
                parcur = ident(i + 1) == "parcur";
                if (auto n = ident(i + 1); n && suffix(*n, "cur"))
                    role = 0;
            }
            continue;
        }
        if (name == "}") {
            if (depth == 1 && role && i && parcur) {
                auto n = ident(i - 1);
                if (n == "F" || n == "I")
                    ts[i - 1] = SatToken(SatIdent{n == "F" ? "surf1" : "surf2"});
            }
            if (depth)
                --depth;
            if (!depth)
                role.reset();
            continue;
        }
        if ((name == "nubs" || name == "nurbs") && role && *role >= 1) {
            role = 2;
            continue;
        }
        auto raw = std::get_if<SabToken>(&ts[i].value);
        if (depth != 1 || !role || !raw || raw->tag != 21)
            continue;
        auto v = ts[i].as_integer();
        const char *n = nullptr;
        if (v) {
            if (*role == 0 && *v == 0)
                n = "full";
            else if (*role == 2 && *v >= 0 && *v <= 2)
                n = *v == 0 ? "open" : *v == 1 ? "closed" : "periodic";
            else if (*role >= 3 && *v == 0)
                n = "UNEXTENDED";
        }
        if (n)
            ts[i] = SatToken(SatEnum{n});
        ++*role;
    }
    if (r.entity_type != "intcurve-curve" && r.entity_type != "spline-surface" && r.entity_type != "pcurve")
        return;
    auto logical = [](const SatToken &t) -> std::optional<bool> {
        if (auto b = std::get_if<bool>(&t.value))
            return *b;
        if (auto e = std::get_if<SatEnum>(&t.value)) {
            if (e->value == "I" || e->value == "forward_v" || e->value == "forward")
                return true;
            if (e->value == "F" || e->value == "reverse_v" || e->value == "reversed")
                return false;
        }
        return {};
    };
    std::optional<std::tuple<std::string, std::string, std::size_t>> sense;
    if (r.entity_type != "intcurve-curve")
        sense = {{"forward", "reversed", 0}};
    std::vector<std::pair<std::string, std::size_t>> blocks;
    for (std::size_t i = 0; i < ts.size(); ++i) {
        auto n = ident(i);
        if (n == "{") {
            blocks.emplace_back(std::string(ident(i + 1).value_or("")), 0);
            continue;
        }
        if (n == "}") {
            if (!blocks.empty())
                blocks.pop_back();
            continue;
        }
        if (n == "nubs" || n == "nurbs") {
            if (!blocks.empty())
                blocks.back().second = 1;
            continue;
        }
        if (!blocks.empty() && (n == "plane" || n == "sphere" || n == "torus")) {
            sense = {{"forward_v", "reverse_v", 0}};
            continue;
        }
        if (!blocks.empty() && n == "cone") {
            sense = {{"forward", "reversed", 2}};
            continue;
        }
        if (!blocks.empty() && n == "spline") {
            sense = {{"forward", "reversed", 0}};
            continue;
        }
        auto b = logical(ts[i]);
        if (sense && b) {
            auto &[yes, no, skip] = *sense;
            if (skip)
                --skip;
            else {
                ts[i] = SatToken(SatEnum{*b ? yes : no});
                sense.reset();
            }
            continue;
        }
        auto raw = std::get_if<SabToken>(&ts[i].value);
        if (blocks.empty() || !raw || raw->tag != 21)
            continue;
        auto &[block, role] = blocks.back();
        auto v = ts[i].as_integer();
        const char *name = nullptr;
        if (v) {
            if (block == "exactsur" && role == 0 && *v == 0)
                name = "full";
            else if (((block == "exactsur" && (role == 1 || role == 2)) || (block == "exppc" && role == 1)) &&
                     *v >= 0 && *v <= 2)
                name = *v == 0 ? "open" : *v == 1 ? "closed" : "periodic";
            else if (block == "exactsur" && (role == 3 || role == 4) && *v == 0)
                name = "none";
        }
        if (name)
            ts[i] = SatToken(SatEnum{name});
        if (role)
            ++role;
    }
    if (r.entity_type == "pcurve")
        for (auto &t : ts)
            if (auto b = std::get_if<bool>(&t.value))
                t = SatToken(SatEnum{*b ? "I" : "F"});
}
std::optional<bool> boolean(std::string_view s) {
    for (auto n :
         {"forward_v", "I", "forward", "single", "in", "no_rotate", "no_reflect", "no_shear", "surf2"})
        if (s == n)
            return true;
    for (auto n :
         {"reverse_v", "reversed_v", "reversed", "double", "out", "F", "rotate", "reflect", "shear", "surf1"})
        if (s == n)
            return false;
    return {};
}
struct Writer {
    std::vector<std::uint8_t> data;
    void bits(std::uint64_t value, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i)
            data.push_back(std::uint8_t(value >> (8 * i)));
    }
    void real(double v) {
        std::uint64_t bits_;
        std::memcpy(&bits_, &v, 8);
        bits(bits_, 8);
    }
    void tagged_real(double v) {
        data.push_back(6);
        real(v);
    }
    void string(std::string_view s, std::uint8_t tag = 0) {
        if (s.size() > UINT32_MAX)
            throw std::length_error("SAB string too long");
        if (tag) {
            if (s.size() > 255)
                throw std::length_error("SAB class name too long");
            data.push_back(tag);
            bits(s.size(), 1);
        } else {
            auto n = s.size() > 65535 ? 4 : s.size() > 255 ? 2 : 1;
            data.push_back(n == 4 ? 9 : n == 2 ? 8 : 7);
            bits(s.size(), n);
        }
        data.insert(data.end(), s.begin(), s.end());
    }
    void type(std::string_view s) {
        std::size_t start = 0;
        for (;;) {
            auto end = s.find('-', start);
            if (end == s.npos) {
                string(s.substr(start), 13);
                break;
            }
            string(s.substr(start, end - start), 14);
            start = end + 1;
        }
    }
    void token(const SatToken &t, bool doubles) {
        if (auto raw = std::get_if<SabToken>(&t.value)) {
            data.push_back(raw->tag);
            data.insert(data.end(), raw->data.begin(), raw->data.end());
            return;
        }
        if (auto p = t.as_pointer()) {
            data.push_back(12);
            bits(std::uint32_t(p->value), 4);
            return;
        }
        if (auto n = std::get_if<std::int64_t>(&t.value)) {
            if (doubles)
                tagged_real(double(*n));
            else {
                data.push_back(4);
                bits(std::uint32_t(*n), 4);
            }
            return;
        }
        if (auto f = std::get_if<double>(&t.value)) {
            tagged_real(*f);
            return;
        }
        if (auto c = t.coordinates()) {
            data.push_back(19);
            for (std::size_t i = 0; i < c->second; ++i)
                real(c->first[i]);
            return;
        }
        if (auto b = std::get_if<bool>(&t.value)) {
            data.push_back(*b ? 11 : 10);
            return;
        }
        if (std::holds_alternative<SatTerminator>(t.value)) {
            data.push_back(17);
            return;
        }
        auto s = t.as_ident();
        if (!s)
            s = t.as_string();
        if (!s)
            throw std::invalid_argument("Unknown SAB token");
        if (auto b = boolean(*s)) {
            data.push_back(*b ? 11 : 10);
            return;
        }
        if (std::holds_alternative<SatIdent>(t.value) && (*s == "{" || *s == "}")) {
            data.push_back(*s == "{" ? 15 : 16);
            return;
        }
        if (std::holds_alternative<SatEnum>(t.value) &&
            (*s == "full" || *s == "open" || *s == "none" || *s == "closed" || *s == "periodic" ||
             *s == "UNEXTENDED")) {
            data.push_back(21);
            bits(*s == "closed" ? 1 : *s == "periodic" ? 2 : 0, 4);
            return;
        }
        string(*s);
    }
};
std::vector<std::uint8_t> layout(std::string_view t) {
    if (t == "point")
        return {19};
    if (t == "straight-curve")
        return {19, 20};
    if (t == "plane-surface" || t == "cone-surface" || t == "ellipse-curve")
        return {19, 20, 20};
    if (t == "sphere-surface")
        return {19, 0, 20, 20};
    if (t == "torus-surface")
        return {19, 20, 0, 0, 20};
    return {};
}
bool numeric(const SatToken &t) {
    return std::holds_alternative<double>(t.value) || std::holds_alternative<std::int64_t>(t.value);
}
std::pair<SatDocument, std::size_t> read_sab(std::string_view data, bool retain) {
    const auto bytes = data;
    Reader in{data};
    if (bytes.substr(0, 15) == "ACIS BinaryFile")
        in.at = 15;
    else if (bytes.substr(0, 14) == "ASM BinaryFile") {
        in.need(15);
        in.at = 15;
    } else
        throw SabError("Invalid SAB magic", 0);
    SatDocument d;
    d.header.version = SatVersion::from_number(std::uint32_t(in.bits(4)));
    d.header.num_records = std::size_t(in.bits(4));
    d.header.num_bodies = std::size_t(in.bits(4));
    d.header.has_history = (in.bits(4) & 1) != 0;
    d.header.product_id = in.tagged_string();
    d.header.product_version = in.tagged_string();
    d.header.date = in.tagged_string();
    d.header.spatial_resolution = in.tagged_real();
    d.header.normal_tolerance = in.tagged_real();
    d.header.resfit_tolerance.reset();
    if (in.at < data.size() && in.peek() == 6)
        d.header.resfit_tolerance = in.tagged_real();
    bool terminated = false;
    std::size_t records = 0;
    while (in.at < data.size()) {
        SatRecord r;
        if (records >= INT32_MAX)
            throw SabError("SAB record limit", in.at);
        r.index = std::int32_t(records++);
        while (in.peek() == 14) {
            in.byte();
            r.entity_type += in.string(1);
            r.entity_type += '-';
        }
        auto pos = in.at;
        if (in.byte() != 13)
            throw SabError("Expected SAB entity type", pos);
        r.entity_type += in.string(1);
        if (r.entity_type == "End-of-ACIS-data" || r.entity_type == "End-of-ASM-data") {
            terminated = true;
            break;
        }
        auto dash = r.entity_type.find('-');
        if (dash != r.entity_type.npos)
            r.sub_type = r.entity_type.substr(0, dash);
        if (in.at < data.size() && in.peek() == 12) {
            in.byte();
            r.attribute = {in.signed32()};
        }
        if (in.at < data.size() && in.peek() == 4) {
            in.byte();
            r.subtype_id = in.signed32();
        }
        while (in.at < data.size()) {
            if (in.peek() == 17) {
                in.byte();
                break;
            }
            auto token = in.token(retain);
            if (retain)
                r.tokens.push_back(std::move(token));
        }
        if (!retain)
            continue;
        if (d.header.version.major < 7) {
            auto base = base_entity_type(r.entity_type);
            bool normalized = false;
            for (auto t : {"body", "lump", "shell", "subshell", "wire", "face", "loop", "vertex", "coedge",
                           "edge", "point", "transform", "surface", "curve", "pcurve"})
                if (base == t)
                    normalized = true;
            if (normalized) {
                r.tokens.insert(r.tokens.begin(), SatToken(SatPointer{}));
                if (base == "edge" && r.tokens.size() >= 3 && r.tokens[1].as_pointer() &&
                    r.tokens[2].as_pointer()) {
                    r.tokens.insert(r.tokens.begin() + 2, SatToken(0.0));
                    if (r.tokens.size() >= 5)
                        r.tokens.insert(r.tokens.begin() + 4, SatToken(0.0));
                }
            }
        }
        booleans(r);
        keywords(r);
        d.records.push_back(std::move(r));
    }
    if (!retain && !terminated)
        throw SabError("Missing SAB end marker", in.at);
    return {std::move(d), in.at};
}
} // namespace
std::pair<SatDocument, std::size_t> SabReader::read_with_consumed(const std::vector<std::uint8_t> &data) {
    return read_sab({reinterpret_cast<const char *>(data.data()), data.size()}, true);
}
std::size_t detail::sab_extent(std::string_view data) { return read_sab(data, false).second; }
#include "acis_spline_roles.inc"
std::vector<std::uint8_t> SabWriter::write(const SatDocument &document) {
    if (document.header.version.major < 7) {
        auto upgraded = document;
        upgraded.upgrade_to_v700();
        return write(upgraded);
    }
    const auto completed = document.completed_for_restore();
    const auto &d = completed ? *completed : document;
    Writer out;
    const std::string magic = "ACIS BinaryFile";
    out.data.insert(out.data.end(), magic.begin(), magic.end());
    out.bits(d.header.version.number(), 4);
    out.bits(0, 4);
    if (d.header.num_bodies > UINT32_MAX)
        throw std::length_error("SAB body count");
    out.bits(d.header.num_bodies, 4);
    const bool asm_ = d.header.version.number() >= 21800;
    out.bits((d.header.has_history ? 1 : 0) | (asm_ ? 12 : 0), 4);
    out.string(d.header.product_id);
    out.string(d.header.product_version);
    out.string(d.header.date);
    out.tagged_real(d.header.spatial_resolution);
    out.tagged_real(d.header.normal_tolerance);
    if (d.header.resfit_tolerance)
        out.tagged_real(*d.header.resfit_tolerance);
    for (const auto &r : d.records) {
        out.type(r.entity_type);
        out.token(SatToken(r.attribute), false);
        out.data.push_back(4);
        out.bits(std::uint32_t(r.subtype_id), 4);
        auto ts = r.tokens;
        if (r.entity_type == "intcurve-curve" || r.entity_type == "spline-surface" ||
            r.entity_type == "pcurve")
            ts = spline_numeric_roles(r.entity_type, ts);
        const auto base = base_entity_type(r.entity_type);
        if (base == "face") {
            std::vector<std::size_t> ids;
            for (std::size_t i = 0; i < ts.size(); ++i) {
                auto n = ts[i].as_ident();
                if (std::holds_alternative<bool>(ts[i].value) || n == "forward" || n == "reversed" ||
                    n == "single" || n == "double" || n == "in" || n == "out")
                    ids.push_back(i);
            }
            auto start = ids.size() > 3 ? ids.size() - 3 : 0;
            for (std::size_t i = start; i < ids.size(); ++i) {
                auto n = ts[ids[i]].as_ident();
                if (n) {
                    auto role = i - start;
                    ts[ids[i]] = SatToken(role == 0   ? *n == "forward"
                                          : role == 1 ? *n == "single"
                                                      : *n == "out");
                }
            }
        }
        if (base == "wire")
            for (auto &t : ts) {
                auto n = t.as_ident();
                if (n == "out" || n == "in")
                    t = SatToken(n == "out");
            }
        if (r.entity_type == "transform" && std::none_of(ts.begin(), ts.end(), [](const auto &t) {
                return std::holds_alternative<SabToken>(t.value);
            })) {
            std::vector<SatToken> values;
            for (const auto &t : ts)
                if (!t.as_pointer())
                    values.push_back(t);
            for (std::size_t i = 0; i + 2 < std::min<std::size_t>(12, values.size()); i += 3) {
                out.data.push_back(20);
                for (auto j : {0u, 1u, 2u})
                    out.real(values[i + j].as_float().value_or(0));
            }
            if (values.size() > 12)
                out.tagged_real(values[12].as_float().value_or(1));
            for (std::size_t i = 13; i < values.size(); ++i)
                out.token(values[i], false);
            out.data.push_back(17);
            continue;
        }
        if (r.entity_type == "eye_refinement")
            for (std::size_t i = 1; i < ts.size(); ++i) {
                auto n = ts[i - 1].as_string();
                if (!n)
                    n = ts[i - 1].as_ident();
                if (n && (*n == "stol" || *n == "ntol" || *n == "dsil" || *n == "flatness" ||
                          *n == "pixarea" || *n == "hmax" || *n == "gridar"))
                    if (auto v = std::get_if<std::int64_t>(&ts[i].value))
                        ts[i] = SatToken(double(*v));
            }
        auto steps = layout(r.entity_type);
        const bool doubles = !steps.empty() || r.entity_type == "edge" || r.entity_type == "tedge-edge" ||
                             r.entity_type == "tcoedge-coedge";
        std::size_t step = 0, depth = 0;
        bool geometry = false;
        for (std::size_t i = 0; i < ts.size();) {
            const auto &t = ts[i];
            if (auto raw = std::get_if<SabToken>(&t.value)) {
                if (raw->tag == 19 || raw->tag == 20)
                    ++step;
                out.token(t, false);
                ++i;
                continue;
            }
            auto n = t.as_ident();
            if (depth && n && *n != "{" && *n != "}" && !boolean(*n) &&
                std::holds_alternative<SatIdent>(t.value)) {
                if (*n == "plane" || *n == "cone" || *n == "sphere" || *n == "torus") {
                    steps = layout(std::string(*n) + "-surface");
                    step = 0;
                }
                out.type(*n);
                ++i;
                continue;
            }
            if (std::holds_alternative<Vector3>(t.value)) {
                out.token(t, false);
                ++step;
                ++i;
                continue;
            }
            if (numeric(t))
                geometry = true;
            if (geometry && step < steps.size() && steps[step] && i + 2 < ts.size() && numeric(t) &&
                numeric(ts[i + 1]) && numeric(ts[i + 2])) {
                out.data.push_back(steps[step++]);
                out.real(*t.as_float());
                out.real(*ts[i + 1].as_float());
                out.real(*ts[i + 2].as_float());
                i += 3;
                continue;
            }
            out.token(t, doubles && depth == 0);
            if (geometry && step < steps.size() && !steps[step])
                ++step;
            if (n == "{")
                ++depth;
            if (n == "}" && depth)
                --depth;
            ++i;
        }
        out.data.push_back(17);
    }
    out.type(asm_ ? "End-of-ASM-data" : "End-of-ACIS-data");
    return out.data;
}
} // namespace libopencad::acis
