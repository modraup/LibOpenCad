// SPDX-License-Identifier: MPL-2.0
#include "libopencad/embedded_profile.hpp"
#include "libopencad/solid_history.hpp"
#include "surface_profiles.hpp"
#include <charconv>
#include <limits>
#include <stdexcept>
#include <sstream>
namespace libopencad {
namespace {
using Fields = std::vector<DxfRawField>;
const HistoryProfiles &profiles(const SolidHistoryOperation &op) {
    return std::visit(
        [](const auto &v) -> const HistoryProfiles & {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, HistorySweep> || std::is_same_v<T, HistoryLoft> ||
                          std::is_same_v<T, HistoryRevolve>)
                return v.profiles;
            else
                throw std::invalid_argument("Operation has no embedded profiles");
        },
        op.parameters);
}
std::int32_t integer(const std::string &s) {
    std::int32_t v;
    auto first = s.find_first_not_of(" \t\r\n");
    auto last = s.find_last_not_of(" \t\r\n");
    if (first == std::string::npos)
        throw std::invalid_argument("Empty profile integer");
    if (s[first] == '+')
        ++first;
    auto p = std::from_chars(s.data() + first, s.data() + last + 1, v);
    if (p.ec != std::errc{} || p.ptr != s.data() + last + 1)
        throw std::invalid_argument("Invalid profile integer");
    return v;
}
std::string cipher(std::string s) {
    for (char &x : s) {
        auto c = static_cast<unsigned char>(x);
        if (c >= 128 || c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        x = char(c >= 64 && c <= 95 ? 159 - c : c ^ 0x5f);
    }
    return s;
}
std::string clean_sat(std::string s) {
    std::istringstream in(s);
    std::string line, out;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("End-of-ACIS-data", 0) == 0 || line.rfind("End-of-ASM-data", 0) == 0)
            break;
        out += line + '\n';
    }
    if (out.find_first_not_of(" \t\r\n") == std::string::npos)
        out.clear();
    return out;
}
std::vector<std::string> wrap_sat(const std::string &line) {
    std::vector<std::string> pieces;
    std::size_t start = 0, index = 0;
    std::optional<std::size_t> split;
    while (index < line.size()) {
        if (line[index] == '@' && (index == 0 || line[index - 1] == ' ')) {
            std::size_t at = index + 1, count = 0;
            while (at < line.size() && line[at] >= '0' && line[at] <= '9') {
                if (count > line.size())
                    throw std::invalid_argument("Invalid SAT string length");
                count = count * 10 + std::size_t(line[at++] - '0');
            }
            index = std::min(line.size(), at + 1 + count);
            continue;
        }
        if (line[index] == ' ')
            split = index;
        if (index - start >= 240 && split && *split > start) {
            pieces.push_back(line.substr(start, *split - start));
            start = *split;
            split.reset();
        }
        ++index;
    }
    pieces.push_back(line.substr(start));
    return pieces;
}
int nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    throw std::invalid_argument("Invalid profile hex chunk");
}
struct Cursor {
    const Fields &f;
    std::size_t i = 0;
    DxfVersion version;
    std::int32_t take(int code) {
        if (i == f.size() || f[i].code != code)
            throw std::invalid_argument("Missing profile framing field");
        return integer(f[i++].value);
    }
    std::size_t count(int code) {
        auto n = take(code);
        if (n < 0 || std::size_t(n) > f.size() - i || n > 1000000)
            throw std::invalid_argument("Invalid profile list count");
        return std::size_t(n);
    }
    std::optional<EmbeddedProfile> entity(int typecode, int sizecode, bool body = false,
                                          bool surface = false) {
        auto type = take(typecode);
        if (!type) {
            if (surface && take(sizecode) != 0)
                throw std::invalid_argument("Nonzero length for empty surface profile");
            return std::nullopt;
        }
        if (body && (surface ? (i < f.size() && f[i].code == 70) : (type == 15 || type == 16))) {
            if (take(70) != 1)
                throw std::invalid_argument("Unsupported SAT profile version");
            std::string sat;
            while (i < f.size() && (f[i].code == 1 || f[i].code == 3)) {
                if (f[i].code == 1 && !sat.empty())
                    sat += '\n';
                sat += f[i++].value;
            }
            if (sat.empty())
                return std::nullopt;
            return ProfileBody{type, clean_sat(cipher(sat))};
        }
        auto bits = take(sizecode);
        if (bits < 0)
            throw std::invalid_argument("Negative profile bit count");
        std::string bytes;
        while (i < f.size() && f[i].code == 310) {
            auto &s = f[i++].value;
            if (s.size() % 2)
                throw std::invalid_argument("Odd profile hex chunk");
            for (std::size_t j = 0; j < s.size(); j += 2)
                bytes += char((nibble(s[j]) << 4) | nibble(s[j + 1]));
        }
        return decode_embedded_profile({type, std::size_t(bits), bytes}, version);
    }
    void finish() {
        if (i != f.size())
            throw std::invalid_argument("Unexpected embedded profile fields");
    }
};
void emit(Fields &f, int typecode, int sizecode, const std::optional<EmbeddedProfile> &v, DxfVersion version,
          bool body = false, bool exact = false, bool surface = false) {
    if (!v) {
        f.push_back({typecode, "0"});
        if (surface)
            f.push_back({sizecode, "0"});
        return;
    }
    if (auto sat = std::get_if<ProfileBody>(&*v)) {
        if (!body || (surface ? sat->type_code <= 0 : (sat->type_code != 15 && sat->type_code != 16)))
            throw std::invalid_argument(surface ? "Invalid SAT surface profile type"
                                                : "SAT wire profiles require sweep/extrusion type 15 or 16");
        f.push_back({typecode, std::to_string(sat->type_code)});
        f.push_back({70, "1"});
        auto text = clean_sat(sat->sat);
        if (text.empty())
            throw std::invalid_argument("Empty SAT wire profile");
        text += "End-of-ACIS-data\n";
        std::istringstream probe(text);
        std::string line;
        bool wrap = false;
        while (std::getline(probe, line))
            wrap |= line.size() > 255;
        std::istringstream in(text);
        while (std::getline(in, line)) {
            for (auto &piece : (wrap ? wrap_sat(line) : std::vector<std::string>{line})) {
                auto encoded = cipher(piece);
                if (encoded.empty())
                    f.push_back({1, ""});
                for (std::size_t i = 0; i < encoded.size();) {
                    auto n = std::min<std::size_t>(2049, encoded.size() - i);
                    while (i + n < encoded.size() &&
                           (static_cast<unsigned char>(encoded[i + n]) & 0xc0) == 0x80)
                        --n;
                    if (!n)
                        throw std::invalid_argument("Invalid SAT UTF-8");
                    f.push_back({i ? 3 : 1, encoded.substr(i, n)});
                    i += n;
                }
            }
        }
        return;
    }
    auto e = encode_embedded_profile(*v, version);
    // Raw unknown layouts retain their exact meaningful length, not invented padding.
    auto bits = (exact || std::holds_alternative<ProfileOpaque>(*v)) ? e.bit_count : e.bytes.size() * 8;
    if (bits > std::size_t(std::numeric_limits<std::int32_t>::max()))
        throw std::length_error("Profile body too large");
    f.push_back({typecode, std::to_string(e.type_code)});
    f.push_back({sizecode, std::to_string(bits)});
    constexpr char digits[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < e.bytes.size(); i += 127) {
        std::string s;
        for (std::size_t j = i; j < std::min(i + 127, e.bytes.size()); ++j) {
            auto b = static_cast<unsigned char>(e.bytes[j]);
            s += digits[b >> 4];
            s += digits[b & 15];
        }
        f.push_back({310, std::move(s)});
    }
}
void list(Fields &f, int countcode, int type, int size, const std::vector<EmbeddedProfile> &vs,
          DxfVersion version) {
    if (vs.size() > 1000000)
        throw std::length_error("Too many profiles");
    f.push_back({countcode, std::to_string(vs.size())});
    for (auto &v : vs)
        emit(f, type, size, v, version);
}
} // namespace
HistoryProfileGeometry read_history_profiles(const SolidHistoryOperation &op, DxfVersion version) {
    auto &p = profiles(op);
    HistoryProfileGeometry g;
    if (!p.fields.empty()) {
        if (p.source_version != dxf_version_string(version))
            throw std::invalid_argument("Profile source version mismatch");
        Cursor c{p.fields, 0, version};
        if (std::holds_alternative<HistoryLoft>(op.parameters)) {
            auto n = c.count(92);
            for (std::size_t i = 0; i < n; ++i) {
                auto v = c.entity(93, 94);
                if (!v)
                    throw std::invalid_argument("Empty loft section");
                g.sections.push_back(*v);
            }
            n = c.count(95);
            for (std::size_t i = 0; i < n; ++i) {
                auto v = c.entity(96, 97);
                if (!v)
                    throw std::invalid_argument("Empty loft guide");
                g.guides.push_back(*v);
            }
            g.path = c.entity(98, 99);
        } else {
            auto sweep = std::holds_alternative<HistorySweep>(op.parameters);
            g.sweep = c.entity(92, 90, sweep);
            if (sweep)
                g.path = c.entity(93, 90, true);
        }
        c.finish();
    }
    if (auto loft = std::get_if<HistoryLoft>(&op.parameters);
        loft && loft->settings && !loft->settings->path.empty()) {
        Cursor c{loft->settings->path, 0, DxfVersion::AC1032};
        g.metadata_path = c.entity(92, 93);
        c.finish();
    }
    return g;
}
void set_history_profiles(SolidHistoryOperation &op, const HistoryProfileGeometry &g, DxfVersion version) {
    (void)profiles(op);
    if (version < DxfVersion::AC1021 || dxf_version_string(version) == "UNKNOWN")
        throw std::invalid_argument("History profiles require R2007+");
    auto next = op;
    HistoryProfiles p;
    p.source_version = dxf_version_string(version);
    if (auto loft = std::get_if<HistoryLoft>(&next.parameters)) {
        if (g.sweep)
            throw std::invalid_argument("Loft has sections rather than a sweep profile");
        list(p.fields, 92, 93, 94, g.sections, version);
        list(p.fields, 95, 96, 97, g.guides, version);
        emit(p.fields, 98, 99, g.metadata_path ? g.metadata_path : g.path, version);
        if (g.metadata_path) {
            if (!loft->settings)
                loft->settings = HistoryLoftSettings{};
            loft->settings->path.clear();
            emit(loft->settings->path, 92, 93, g.metadata_path, DxfVersion::AC1032, false, true);
        } else if (loft->settings)
            loft->settings->path.clear();
        loft->profiles = std::move(p);
    } else {
        if (!g.sections.empty() || !g.guides.empty() || g.metadata_path)
            throw std::invalid_argument("Sections/guides/metadata path require loft");
        auto sweep = std::get_if<HistorySweep>(&next.parameters);
        if (!sweep && g.path)
            throw std::invalid_argument("Revolve has no path");
        emit(p.fields, 92, 90, g.sweep, version, sweep != nullptr);
        if (sweep) {
            emit(p.fields, 93, 90, g.path, version, true);
            sweep->profiles = std::move(p);
        } else
            std::get<HistoryRevolve>(next.parameters).profiles = std::move(p);
    }
    op = std::move(next);
}

namespace detail {
SurfaceProfiles decode_surface_profiles(SurfaceProfileLayout kind, const Fields &fields, DxfVersion version) {
    if (version < DxfVersion::AC1021 || dxf_version_string(version) == "UNKNOWN")
        throw std::invalid_argument("Surface profiles require R2007+");
    SurfaceProfiles out;
    Cursor c{fields, 0, version};
    if (kind == SurfaceProfileLayout::Lofted) {
        int group = 90;
        bool path = false;
        while (c.i < fields.size()) {
            int code = fields[c.i].code;
            if (code < group || code < 90 || code > 92 || path)
                throw std::invalid_argument("Invalid loft profile order");
            group = code;
            auto entity = c.entity(code, 90, true, true);
            if (!entity)
                throw std::invalid_argument("Empty loft profile record");
            if (code == 90)
                out.sections.push_back(*entity);
            else if (code == 91)
                out.guides.push_back(*entity);
            else {
                out.path = *entity;
                path = true;
            }
            if (out.sections.size() + out.guides.size() > 1000000)
                throw std::length_error("Too many surface profiles");
        }
    } else {
        if (fields.empty())
            return out;
        if (kind == SurfaceProfileLayout::Revolved && fields.size() == 2 && fields[0].code == 90 &&
            fields[1].code == 90) {
            out.revolve_class_version = c.take(90);
            out.revolve_entity_id = c.take(90);
            if (out.revolve_entity_id < 0)
                throw std::invalid_argument("Negative revolved legacy entity id");
        } else {
            if (kind == SurfaceProfileLayout::Swept && fields.size() >= 3 && fields[0].code == 90 &&
                fields[1].code == 90 && (fields[2].code == 90 || fields[2].code == 70))
                out.swept_legacy_class_version = c.take(90);
            out.profile = c.entity(90, 90, true, true);
            if (kind == SurfaceProfileLayout::Swept)
                out.path = c.entity(c.i < fields.size() && fields[c.i].code == 90 ? 90 : 91, 90, true, true);
        }
    }
    c.finish();
    return out;
}
Fields encode_surface_profiles(SurfaceProfileLayout kind, const SurfaceProfiles &value, DxfVersion version) {
    if (version < DxfVersion::AC1021 || dxf_version_string(version) == "UNKNOWN")
        throw std::invalid_argument("Surface profiles require R2007+");
    Fields fields;
    if (kind != SurfaceProfileLayout::Revolved && (value.revolve_class_version || value.revolve_entity_id))
        throw std::invalid_argument("Legacy revolve fields require a revolved surface");
    if (kind != SurfaceProfileLayout::Swept && value.swept_legacy_class_version)
        throw std::invalid_argument("Legacy sweep version requires a swept surface");
    if (kind == SurfaceProfileLayout::Lofted) {
        if (value.profile)
            throw std::invalid_argument("Loft uses sections rather than a single profile");
        if (value.sections.size() > 1000000 || value.guides.size() > 1000000 - value.sections.size())
            throw std::length_error("Too many surface profiles");
        for (auto &e : value.sections)
            emit(fields, 90, 90, e, version, true, true, true);
        for (auto &e : value.guides)
            emit(fields, 91, 90, e, version, true, true, true);
        if (value.path)
            emit(fields, 92, 90, value.path, version, true, true, true);
    } else {
        if (!value.sections.empty() || !value.guides.empty())
            throw std::invalid_argument("Sections and guides require loft");
        if (kind != SurfaceProfileLayout::Swept && value.path)
            throw std::invalid_argument("This surface has no path input");
        if (kind == SurfaceProfileLayout::Revolved && !value.profile) {
            if (value.revolve_entity_id < 0)
                throw std::invalid_argument("Negative revolved legacy entity id");
            fields = {{90, std::to_string(value.revolve_class_version)},
                      {90, std::to_string(value.revolve_entity_id)}};
        } else {
            if (value.swept_legacy_class_version)
                fields.push_back({90, std::to_string(*value.swept_legacy_class_version)});
            emit(fields, 90, 90, value.profile, version, true, false, true);
        }
        if (kind == SurfaceProfileLayout::Swept)
            emit(fields, 91, 90, value.path, version, true, false, true);
    }
    return fields;
}
SurfaceProfiles decode_loft_profile_metadata(const Fields &fields) {
    Cursor c{fields, 0, DxfVersion::AC1032};
    if (c.take(90) != 1)
        throw std::invalid_argument("Unsupported loft fallback schema");
    SurfaceProfiles result;
    while (c.i < fields.size()) {
        auto role = c.take(70);
        if (c.i + 1 >= fields.size() || fields[c.i + 1].code != 92)
            throw std::invalid_argument("Incomplete loft fallback framing");
        auto bits = integer(fields[c.i + 1].value);
        if (bits <= 0 || bits > 64 * 1024 * 1024)
            throw std::invalid_argument("Invalid loft fallback size");
        std::size_t bytes = 0;
        for (auto at = c.i + 2; at < fields.size() && fields[at].code == 310; ++at) {
            if (fields[at].value.size() % 2)
                throw std::invalid_argument("Odd loft fallback chunk");
            bytes += fields[at].value.size() / 2;
        }
        if (bytes != (std::size_t(bits) + 7) / 8)
            throw std::invalid_argument("Loft fallback length mismatch");
        auto value = c.entity(91, 92);
        if (!value)
            throw std::invalid_argument("Empty loft fallback profile");
        if (role == 0)
            result.sections.push_back(*value);
        else if (role == 1)
            result.guides.push_back(*value);
        else if (role == 2 && !result.path)
            result.path = *value;
        else
            throw std::invalid_argument("Invalid loft fallback role");
    }
    return result;
}
Fields encode_loft_profile_metadata(const SurfaceProfiles &profiles) {
    Fields result{{90, "1"}};
    auto add = [&](int role, const EmbeddedProfile &v) {
        // The Rust fallback is always R2018. A SAT body has no bitstream and
        // an opaque older payload cannot safely be transcoded. Native inputs
        // remain authoritative; disable the complete optional fallback then.
        if (std::holds_alternative<ProfileBody>(v))
            return false;
        if (auto p = std::get_if<ProfileOpaque>(&v); p && p->source_version != DxfVersion::AC1032)
            return false;
        result.push_back({70, std::to_string(role)});
        emit(result, 91, 92, v, DxfVersion::AC1032, false, true);
        return true;
    };
    for (auto &v : profiles.sections)
        if (!add(0, v))
            return {{90, "1"}};
    for (auto &v : profiles.guides)
        if (!add(1, v))
            return {{90, "1"}};
    if (profiles.path && !add(2, *profiles.path))
        return {{90, "1"}};
    return result;
}
} // namespace detail
} // namespace libopencad
