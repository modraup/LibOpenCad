// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/entities/acis/{parser,writer,types}.rs; SAB text rules at 52034a67.
#include "libopencad/acis.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <iomanip>
#include <sstream>

namespace libopencad::acis {
namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
std::string_view trim(std::string_view s) {
    while (!s.empty() && space(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && space(s.back()))
        s.remove_suffix(1);
    return s;
}
template <class T> std::optional<T> integer(std::string_view s) {
    if (s.empty())
        return {};
    if (s.front() == '+')
        s.remove_prefix(1);
    T n{};
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
        return {};
    return n;
}
std::optional<double> real(std::string_view s) {
    if (s.empty())
        return {};
    if (s.front() == '+')
        s.remove_prefix(1);
    double n;
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
        return {};
    return n;
}
struct Cursor {
    std::string_view text;
    std::size_t at = 0;
    void skip() {
        while (at < text.size() && space(text[at]))
            ++at;
    }
    std::string_view raw() {
        skip();
        auto start = at;
        while (at < text.size() && !space(text[at]))
            ++at;
        return text.substr(start, at - start);
    }
    std::string_view peek() const {
        auto copy = *this;
        return copy.raw();
    }
    std::optional<std::string> counted(bool modern, bool truncate = true) {
        skip();
        if (modern) {
            if (at >= text.size() || text[at] != '@')
                return {};
            ++at;
        }
        auto start = at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9')
            ++at;
        auto n = integer<std::size_t>(text.substr(start, at - start));
        if (!n)
            return {};
        if (modern && (at == text.size() || !space(text[at])))
            return {};
        if (at < text.size() && space(text[at]))
            ++at;
        if (*n > text.size() - at) {
            if (!truncate)
                return {};
            auto s = std::string(trim(text.substr(at)));
            at = text.size();
            return s;
        }
        auto s = std::string(text.substr(at, *n));
        at += *n;
        return s;
    }
    std::optional<SatToken> next(bool modern) {
        skip();
        if (at >= text.size())
            return {};
        if (text[at] == '@' && modern) {
            auto s = counted(true, false);
            if (!s)
                throw SatParseError("Invalid or truncated SAT counted string");
            return SatToken(SatString{std::move(*s)});
        }
        if (text[at] == '$') {
            ++at;
            auto s = raw();
            if (s.empty())
                return {};
            return SatToken(SatPointer{integer<std::int32_t>(s).value_or(-1)});
        }
        if (text[at] == '#') {
            ++at;
            return SatToken(SatTerminator{});
        }
        auto s = raw();
        if (auto i = integer<std::int64_t>(s)) {
            if (*i == 0 && s.front() == '-')
                return SatToken(-0.0);
            return SatToken(*i);
        }
        if (auto f = real(s))
            return SatToken(*f);
        auto lower = std::string(s);
        for (auto &c : lower)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        if (lower == "true" || s == "T")
            return SatToken(true);
        if (lower == "false" || s == "F")
            return SatToken(false);
        for (auto e : {"forward", "reversed", "single", "double", "in", "out", "unknown"})
            if (lower == e)
                return SatToken(SatEnum{lower});
        return SatToken(SatIdent{std::string(s)});
    }
};
bool negative_index(std::string_view s) {
    return s.size() > 1 && s.front() == '-' &&
           std::all_of(s.begin() + 1, s.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool starts_record(std::string_view s) {
    s = trim(s);
    if (s.empty() || s.substr(0, 7) == "End-of-")
        return true;
    Cursor c{s};
    auto id = c.raw();
    if (negative_index(id))
        id = c.raw();
    if (id.empty() || !((id.front() >= 'a' && id.front() <= 'z') || (id.front() >= 'A' && id.front() <= 'Z')))
        return false;
    for (auto ch : id)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
              ch == '_'))
            return false;
    auto slot = c.raw();
    return !slot.empty() && (slot.front() == '$' || integer<std::int64_t>(slot).has_value());
}
bool normalized_type(std::string_view t) {
    t = base_entity_type(t);
    for (auto s : {"body", "lump", "shell", "subshell", "wire", "face", "loop", "vertex", "coedge", "edge",
                   "point", "transform", "surface", "curve", "pcurve"})
        if (s == t)
            return true;
    return false;
}
SatRecord parse_record(std::string_view text, SatVersion version, std::int32_t &next) {
    Cursor c{text};
    auto head = c.peek();
    SatRecord r;
    if (negative_index(head)) {
        c.raw();
        auto n = integer<std::uint32_t>(head.substr(1));
        if (!n || *n >= INT32_MAX)
            throw SatParseError("SAT record index overflow");
        r.index = static_cast<std::int32_t>(*n);
        next = r.index + 1;
    } else {
        if (next == INT32_MAX)
            throw SatParseError("SAT record index overflow");
        r.index = next++;
    }
    r.entity_type = std::string(c.raw());
    if (r.entity_type.empty())
        throw SatParseError("Missing SAT entity type");
    if (auto a = c.next(version.major >= 7)) {
        if (auto p = a->as_pointer())
            r.attribute = *p;
        else if (auto n = std::get_if<std::int64_t>(&a->value))
            r.attribute = {static_cast<std::int32_t>(*n)};
    }
    if (version.major >= 7) {
        auto s = c.peek();
        if (integer<std::int64_t>(s)) {
            auto t = c.next(true);
            if (auto n = std::get_if<std::int64_t>(&t->value))
                r.subtype_id = static_cast<std::int32_t>(*n);
        }
    }
    for (;;) {
        std::optional<SatToken> t;
        if (version.major < 7 && r.entity_type == "eye_refinement" && r.tokens.size() % 2 == 0) {
            auto saved = c.at;
            auto s = c.raw();
            auto n = integer<std::size_t>(s);
            if (n && c.at < c.text.size() && c.text[c.at] == ' ' && *n <= c.text.size() - c.at - 1) {
                t = SatToken(SatString{std::string(c.text.substr(c.at + 1, *n))});
                c.at += 1 + *n;
            } else
                c.at = saved;
        }
        if (!t)
            t = c.next(version.major >= 7);
        if (!t || std::holds_alternative<SatTerminator>(t->value))
            break;
        r.tokens.push_back(std::move(*t));
    }
    if (version.major >= 7 && r.entity_type == "transform" &&
        (r.tokens.empty() || !r.tokens.front().as_pointer()))
        r.tokens.insert(r.tokens.begin(), SatToken(SatPointer{}));
    if (version.major < 7 && normalized_type(r.entity_type)) {
        r.tokens.insert(r.tokens.begin(), SatToken(SatPointer{}));
        if (base_entity_type(r.entity_type) == "edge" && r.tokens.size() >= 3 && r.tokens[1].as_pointer() &&
            r.tokens[2].as_pointer()) {
            r.tokens.insert(r.tokens.begin() + 2, SatToken(0.0));
            if (r.tokens.size() >= 5)
                r.tokens.insert(r.tokens.begin() + 4, SatToken(0.0));
        }
    }
    r.raw_text = std::string(text);
    return r;
}
std::string floating(double v) {
    if (v == 0)
        return std::signbit(v) ? "-0" : "0";
    char buf[128];
    const auto sci = std::abs(v) < 1e-3 || std::abs(v) >= 1e15;
    auto r = std::to_chars(buf, buf + sizeof(buf), v,
                           sci ? std::chars_format::scientific : std::chars_format::fixed);
    if (r.ec != std::errc{})
        throw std::runtime_error("Cannot format SAT number");
    std::string s(buf, r.ptr);
    const auto at = s.find('e');
    if (at != s.npos) {
        auto exp = integer<int>(std::string_view(s).substr(at + 1)).value_or(0);
        auto digits = std::to_string(std::abs(exp));
        while (digits.size() < 3)
            digits.insert(digits.begin(), '0');
        s = s.substr(0, at) + (exp < 0 ? "e-" : "e+") + digits;
    }
    return s;
}
std::string token_text(const SatToken &t, bool modern) {
    if (auto raw = std::get_if<SabToken>(&t.value)) {
        if (!t.coordinates()) {
            if (raw->tag == 5 || raw->tag == 6)
                if (auto f = t.as_float())
                    return floating(*f);
            if (auto n = t.as_integer())
                return std::to_string(*n);
            if (auto f = t.as_float())
                return floating(*f);
            if (auto s = t.as_string()) {
                if (raw->tag == 18) {
                    auto text = *s;
                    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                        text.remove_suffix(1);
                    return std::string(text);
                }
                return (raw->tag == 13 || raw->tag == 14)
                           ? std::string(*s)
                           : "@" + std::to_string(s->size()) + " " + std::string(*s);
            }
        }
    }
    if (auto p = t.as_pointer())
        return "$" + std::to_string(p->value);
    if (auto c = t.coordinates()) {
        std::string s;
        for (std::size_t i = 0; i < c->second; ++i) {
            if (i)
                s += ' ';
            s += floating(c->first[i]);
        }
        return s;
    }
    if (auto s = t.as_string())
        return (modern ? "@" : "") + std::to_string(s->size()) + " " + std::string(*s);
    if (auto s = t.as_ident())
        return std::string(*s);
    if (auto n = std::get_if<std::int64_t>(&t.value))
        return std::to_string(*n);
    if (auto f = t.as_float())
        return floating(*f);
    if (auto b = std::get_if<bool>(&t.value))
        return *b ? "T" : "F";
    if (std::holds_alternative<SatTerminator>(t.value))
        return "#";
    if (auto raw = std::get_if<SabToken>(&t.value)) {
        std::ostringstream out;
        out << "<sab:" << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
            << unsigned(raw->tag) << ":" << std::dec << raw->data.size() << ">";
        return out.str();
    }
    throw std::invalid_argument("SAB token has no SAT text representation");
}
} // namespace
SatDocument SatDocument::parse(std::string_view text) {
    text = trim(text);
    if (text.empty())
        throw SatParseError("Empty SAT input");
    std::size_t at = 0;
    auto line = [&]() {
        auto start = at;
        auto end = text.find('\n', at);
        if (end == text.npos)
            end = text.size();
        at = std::min(end + 1, text.size());
        auto s = text.substr(start, end - start);
        if (!s.empty() && s.back() == '\r')
            s.remove_suffix(1);
        return s;
    };
    Cursor h{line()};
    auto version = integer<std::uint32_t>(h.raw());
    if (!version)
        throw SatParseError("Invalid SAT version");
    SatDocument d;
    d.header.version = SatVersion::from_number(*version);
    d.header.num_records = integer<std::size_t>(h.raw()).value_or(0);
    d.header.num_bodies = integer<std::size_t>(h.raw()).value_or(0);
    d.header.has_history = integer<std::uint32_t>(h.raw()).value_or(0) != 0;
    const auto next_end = text.find('\n', at);
    auto peek = text.substr(at, next_end == text.npos ? text.size() - at : next_end - at);
    if (at < text.size() && peek.find('$') == peek.npos) {
        Cursor product{line()};
        const bool modern = trim(product.text).substr(0, 1) == "@";
        d.header.product_id = product.counted(modern).value_or("");
        d.header.product_version = product.counted(modern).value_or("");
        auto date = product.counted(modern);
        d.header.date = date ? *date : std::string(trim(product.text.substr(product.at)));
        if (at >= text.size())
            throw SatParseError("Missing SAT tolerances");
        Cursor tol{line()};
        d.header.spatial_resolution = real(tol.raw()).value_or(1e-6);
        d.header.normal_tolerance = real(tol.raw()).value_or(1e-6);
        d.header.resfit_tolerance = real(tol.raw());
    } else {
        d.header.product_id.clear();
        d.header.product_version.clear();
        d.header.date.clear();
        d.header.spatial_resolution = 1e-6;
        d.header.normal_tolerance = 1e-10;
        d.header.resfit_tolerance.reset();
    }
    std::vector<std::string> fragments;
    const bool modern = d.header.version.major >= 7;
    if (modern) {
        // DXF may split ordinary tokens across lines. Join those transport
        // breaks, but counted payloads own their bytes, including CR/LF and '#'.
        std::string fragment;
        while (at < text.size()) {
            const auto ch = text[at];
            if (ch == '@' && (fragment.empty() || space(fragment.back()))) {
                Cursor c{text.substr(at)};
                auto payload = c.counted(true, false);
                if (!payload)
                    throw SatParseError("Invalid or truncated SAT counted string");
                fragment.append(text.substr(at, c.at));
                at += c.at;
            } else {
                ++at;
                if (ch == '#') {
                    fragments.push_back(std::move(fragment));
                    fragment.clear();
                } else if (ch != '\r' && ch != '\n')
                    fragment += ch;
            }
        }
        fragments.push_back(std::move(fragment));
    } else {
        // Pre-v700 strings have no '@' prefix. Retain the legacy record-start
        // heuristic for labels such as STEP Id# and eye_refinement strings.
        std::string data(text.substr(at));
        data.erase(std::remove_if(data.begin(), data.end(), [](char c) { return c == '\r' || c == '\n'; }),
                   data.end());
        std::size_t start = 0;
        for (;;) {
            auto end = data.find('#', start);
            if (end == data.npos)
                end = data.size();
            std::string_view piece(data.data() + start, end - start);
            if (fragments.empty() || starts_record(piece))
                fragments.emplace_back(piece);
            else {
                fragments.back() += '#';
                fragments.back() += piece;
            }
            if (end == data.size())
                break;
            start = end + 1;
        }
    }
    std::int32_t next = 0;
    for (const auto &fragment : fragments) {
        std::string_view s(fragment);
        while (!s.empty() && space(s.front()))
            s.remove_prefix(1);
        if (s.empty())
            continue;
        if (modern) {
            Cursor c{s};
            auto type = c.raw();
            if (type == "End-of-ACIS-data" || type == "End-of-ASM-data")
                break;
            d.records.push_back(parse_record(s, d.header.version, next));
        } else {
            const auto stop = std::min(s.find("End-of-ACIS-data"), s.find("End-of-ASM-data"));
            if (stop == 0)
                break;
            if (stop != s.npos)
                s = trim(s.substr(0, stop));
            if (!s.empty())
                d.records.push_back(parse_record(s, d.header.version, next));
            if (stop != s.npos)
                break;
        }
    }
    d.header.num_records = d.records.size();
    return d;
}
std::string SatDocument::to_sat_string() const {
    const auto completed = completed_for_restore();
    const auto &d = completed ? *completed : *this;
    const auto major = d.header.version.major;
    const auto count = major == 7 ? 0 : major > 7 ? d.records.size() : d.header.num_records;
    std::string out = std::to_string(d.header.version.number()) + " " + std::to_string(count) + " " +
                      std::to_string(d.header.num_bodies) + " " + (d.header.has_history ? "1\n" : "0\n");
    for (const auto *s : {&d.header.product_id, &d.header.product_version, &d.header.date}) {
        if (s != &d.header.product_id)
            out += ' ';
        out += std::to_string(s->size()) + " " + *s;
    }
    out += '\n';
    out += floating(d.header.spatial_resolution) + " " + floating(d.header.normal_tolerance);
    if (d.header.resfit_tolerance)
        out += " " + floating(*d.header.resfit_tolerance);
    out += '\n';
    for (const auto &r : d.records) {
        out += r.entity_type + " $" + std::to_string(r.attribute.value);
        if (major >= 7)
            out += " " + std::to_string(r.subtype_id);
        std::size_t start = 0;
        if (r.entity_type == "transform")
            while (start < r.tokens.size() && r.tokens[start].as_pointer())
                ++start;
        for (std::size_t i = start; i < r.tokens.size(); ++i) {
            if (r.entity_type != "transform" && major < 7 && normalized_type(r.entity_type) && i == 0)
                continue;
            if (major < 7 && base_entity_type(r.entity_type) == "edge" && (i == 2 || i == 4 || i >= 8))
                continue;
            out += ' ';
            out += token_text(r.tokens[i], major >= 7);
        }
        out += " #\n";
    }
    out += "End-of-ACIS-data\n";
    return out;
}
} // namespace libopencad::acis
