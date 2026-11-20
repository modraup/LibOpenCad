// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <libopencad/dwg_r2000.hpp>
#include <cstring>
#include <cmath>
#include <stdexcept>
namespace libopencad::detail {
struct MetadataField {
    int code;
    std::size_t offset, size;
};
inline std::uint64_t metadata_uint(std::string_view s, std::size_t p, unsigned n) {
    if (n > 8 || p > s.size() || n > s.size() - p)
        throw std::invalid_argument("Truncated metadata");
    std::uint64_t x = 0;
    for (unsigned i = 0; i < n; ++i)
        x |= std::uint64_t(static_cast<unsigned char>(s[p + i])) << (8 * i);
    return x;
}
inline std::vector<MetadataField> metadata_fields(std::string_view s, bool unicode = true) {
    std::vector<MetadataField> out;
    std::size_t p = 0;
    while (p < s.size()) {
        int c = std::int16_t(metadata_uint(s, p, 2));
        p += 2;
        std::size_t n = 0;
        if (c < 0 || c == 480 || c == 481 || (c >= 330 && c <= 369) || (c >= 390 && c <= 399) || c == 1005)
            n = 8;
        else if ((c >= 0 && c <= 9) || (c >= 100 && c <= 102) || c == 105 || (c >= 300 && c <= 309) ||
                 (c >= 320 && c <= 329) || (c >= 410 && c <= 419) || (c >= 430 && c <= 439) ||
                 (c >= 470 && c <= 479) || c == 999 || (c >= 1000 && c <= 1003))
            // Legacy strings have a code-page byte after their byte count;
            // modern strings instead count UTF-16 code units without that byte.
            n = 2 + (unicode ? 0 : 1) + (unicode ? 2 : 1) * metadata_uint(s, p, 2);
        else if ((c >= 10 && c <= 37) || (c >= 110 && c <= 139) || (c >= 210 && c <= 269) ||
                 (c >= 1010 && c <= 1039) || (c >= 1043 && c <= 1069))
            n = 24;
        else if ((c >= 38 && c <= 59) || (c >= 140 && c <= 169) || (c >= 460 && c <= 469) ||
                 (c >= 1040 && c <= 1042))
            n = 8;
        else if ((c >= 60 && c <= 79) || (c >= 170 && c <= 179) || (c >= 270 && c <= 279) ||
                 (c >= 370 && c <= 409) || c == 1070)
            n = 2;
        else if ((c >= 80 && c <= 99) || (c >= 420 && c <= 429) || (c >= 440 && c <= 459) || c == 1071)
            n = 4;
        else if (c >= 280 && c <= 299)
            n = 1;
        else if ((c >= 310 && c <= 319) || c == 1004)
            n = 1 + metadata_uint(s, p, 1);
        else
            throw std::invalid_argument("Unknown metadata group code");
        if (n > s.size() - p)
            throw std::invalid_argument("Truncated metadata value");
        out.push_back({c, p, n});
        p += n;
    }
    return out;
}
inline MetadataField metadata_field(std::string_view s, int code) {
    for (auto f : metadata_fields(s))
        if (f.code == code)
            return f;
    throw std::invalid_argument("Missing metadata field");
}
inline std::uint64_t metadata_value(std::string_view s, int code) {
    auto f = metadata_field(s, code);
    return metadata_uint(s, f.offset, unsigned(f.size));
}
inline double metadata_double(std::string_view s, int code) {
    auto x = metadata_value(s, code);
    double d;
    std::memcpy(&d, &x, 8);
    if (!std::isfinite(d))
        throw std::invalid_argument("Nonfinite metadata");
    return d;
}
inline void metadata_put(std::string &s, int code, std::uint64_t x, unsigned width) {
    auto f = metadata_field(s, code);
    if (f.size != width)
        throw std::invalid_argument("Metadata type mismatch");
    for (unsigned i = 0; i < width; ++i)
        s[f.offset + i] = char(x >> (8 * i));
}
inline void metadata_put_double(std::string &s, int code, double d) {
    if (!std::isfinite(d))
        throw std::invalid_argument("Nonfinite metadata");
    std::uint64_t x;
    std::memcpy(&x, &d, 8);
    metadata_put(s, code, x, 8);
}
inline bool metadata_has(std::string_view s, int code) {
    for (auto f : metadata_fields(s))
        if (f.code == code)
            return true;
    return false;
}
inline void metadata_append(std::string &s, int code, std::uint64_t x, unsigned width) {
    s += char(code);
    s += char(code >> 8);
    for (unsigned i = 0; i < width; ++i)
        s += char(x >> (8 * i));
}
inline void metadata_set(std::string &s, int code, std::uint64_t x, unsigned width) {
    if (metadata_has(s, code))
        metadata_put(s, code, x, width);
    else
        metadata_append(s, code, x, width);
}
inline void metadata_set_double(std::string &s, int code, double d) {
    if (!std::isfinite(d))
        throw std::invalid_argument("Nonfinite metadata");
    std::uint64_t x;
    std::memcpy(&x, &d, 8);
    metadata_set(s, code, x, 8);
}
// The profile metadata schema uses only shorts, longs and binary chunks.
inline std::vector<DxfRawField> metadata_profile_fields(std::string_view s) {
    std::vector<DxfRawField> out;
    constexpr char digits[] = "0123456789ABCDEF";
    for (auto f : metadata_fields(s)) {
        if (f.code == 310) {
            std::string hex;
            for (std::size_t i = 1; i < f.size; ++i) {
                auto c = static_cast<unsigned char>(s[f.offset + i]);
                hex += digits[c >> 4];
                hex += digits[c & 15];
            }
            out.push_back({f.code, hex});
        } else if (f.code == 70 || (f.code >= 90 && f.code <= 93))
            out.push_back(
                {f.code, std::to_string(std::int32_t(metadata_uint(s, f.offset, unsigned(f.size))))});
    }
    return out;
}
inline std::string metadata_profile_bytes(const std::vector<DxfRawField> &fields) {
    std::string out;
    for (auto &f : fields) {
        if (f.code == 310) {
            if (f.value.size() % 2 || f.value.size() > 510)
                throw std::invalid_argument("Invalid metadata chunk");
            metadata_append(out, 310, f.value.size() / 2, 1);
            auto nibble = [](char c) -> unsigned {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                throw std::invalid_argument("Invalid metadata hex");
            };
            for (std::size_t i = 0; i < f.value.size(); i += 2)
                out += char(16 * nibble(f.value[i]) + nibble(f.value[i + 1]));
        } else if (f.code == 70 || (f.code >= 90 && f.code <= 93)) {
            std::size_t used;
            auto n = std::stoll(f.value, &used);
            if (used != f.value.size() || n < INT32_MIN || n > INT32_MAX)
                throw std::invalid_argument("Invalid metadata integer");
            metadata_append(out, f.code, n, f.code == 70 ? 2 : 4);
        } else
            throw std::invalid_argument("Unknown profile metadata field");
    }
    return out;
}
inline std::uint64_t metadata_reference(const DwgHistoryRecord &r, int code) {
    std::size_t i = 0;
    for (auto f : metadata_fields(r.metadata))
        if (f.code >= 330 && f.code <= 369) {
            if (f.code == code)
                return i < r.reference_values.size() ? r.reference_values[i]
                                                     : metadata_uint(r.metadata, f.offset, 8);
            ++i;
        }
    throw std::invalid_argument("Missing metadata reference");
}
inline void validate_loft_path(const std::vector<DxfRawField> &path) {
    if (path.empty())
        return;
    if (path.size() < 2 || path[0].code != 92 || path[1].code != 93)
        throw std::invalid_argument("Invalid loft metadata path framing");
    std::size_t used = 0;
    auto bits = std::stoll(path[1].value, &used);
    if (used != path[1].value.size() || bits <= 0 || bits > 64 * 1024 * 1024)
        throw std::invalid_argument("Invalid loft metadata path size");
    std::size_t bytes = 0;
    for (std::size_t i = 2; i < path.size(); ++i) {
        if (path[i].code != 310 || path[i].value.size() % 2)
            throw std::invalid_argument("Invalid loft metadata path chunk");
        bytes += path[i].value.size() / 2;
        if (bytes > std::size_t((bits + 7) / 8))
            throw std::invalid_argument("Loft metadata path overflow");
    }
    if (bytes != std::size_t((bits + 7) / 8))
        throw std::invalid_argument("Truncated loft metadata path");
}
inline void validate_loft_metadata(const HistoryLoftSettings &v) {
    validate_loft_path(v.path);
    if (v.normals < 0 || v.normals > 6 || v.start_continuity < 0 || v.start_continuity > 1 ||
        v.end_continuity < 0 || v.end_continuity > 1 || v.start_magnitude < 0 || v.end_magnitude < 0 ||
        v.start_bulge < 0 || v.end_bulge < 0 || v.section_counts.size() > 100000)
        throw std::invalid_argument("Invalid loft metadata");
    for (auto n : {v.start_draft_angle, v.end_draft_angle, v.start_magnitude, v.end_magnitude, v.start_bulge,
                   v.end_bulge})
        if (!std::isfinite(n))
            throw std::invalid_argument("Nonfinite loft metadata");
    for (auto n : v.section_counts)
        if (n <= 0 || n > 100000)
            throw std::invalid_argument("Invalid loft section count");
}
inline std::string surface_history_bytes(std::uint64_t root) {
    std::string s;
    metadata_append(s, 90, 1, 4);
    metadata_append(s, 330, root, 8);
    return s;
}
inline std::string loft_metadata_bytes(const HistoryLoftSettings &v) {
    validate_loft_metadata(v);
    std::string s;
    metadata_append(s, 90, 1, 4);
    metadata_append(s, 70, std::uint16_t(v.normals), 2);
    auto real = [&](int code, double d) {
        std::uint64_t x;
        std::memcpy(&x, &d, 8);
        metadata_append(s, code, x, 8);
    };
    real(40, v.start_draft_angle);
    real(41, v.end_draft_angle);
    real(42, v.start_magnitude);
    real(43, v.end_magnitude);
    metadata_append(s, 71, std::uint16_t(v.start_continuity), 2);
    metadata_append(s, 72, std::uint16_t(v.end_continuity), 2);
    real(44, v.start_bulge);
    real(45, v.end_bulge);
    metadata_append(s, 290, v.closed, 1);
    metadata_append(s, 291, v.surface, 1);
    metadata_append(s, 292, v.align_direction, 1);
    metadata_append(s, 293, v.periodic, 1);
    for (auto n : v.section_counts)
        metadata_append(s, 91, std::uint32_t(n), 4);
    s += metadata_profile_bytes(v.path);
    return s;
}
inline const DwgHistoryRecord *metadata_record(const DwgR2000Objects &o, std::uint64_t owner,
                                               std::uint64_t dict, std::string_view key) {
    if (!dict)
        return nullptr;
    auto check_shared = [&](const auto &records) {
        for (const auto &[h, m] : records)
            if (h != owner && m.properties.extension_dictionary == dict)
                throw std::invalid_argument("Shared metadata dictionary");
    };
    check_shared(o.lines);
    check_shared(o.circles);
    check_shared(o.arcs);
    check_shared(o.texts);
    for (auto &[h, m] : o.modelers)
        if (h != owner && m.properties.extension_dictionary == dict)
            throw std::invalid_argument("Shared metadata dictionary");
    for (auto &[h, r] : o.history_records)
        if (h != owner && r.extension_dictionary == dict)
            throw std::invalid_argument("Shared metadata dictionary");
    for (auto &[h, r] : o.metadata_records)
        if (h != owner && r.extension_dictionary == dict)
            throw std::invalid_argument("Shared metadata dictionary");
    auto i = o.metadata_records.find(dict);
    if (i == o.metadata_records.end() || i->second.owner != owner)
        throw std::invalid_argument("Missing or foreign metadata dictionary");
    for (auto &e : i->second.dictionary_entries)
        if (e.first == key) {
            auto r = o.metadata_records.find(e.second);
            if (r == o.metadata_records.end() || r->second.owner != dict || r->second.type != "XRECORD")
                throw std::invalid_argument("Missing or foreign metadata record");
            for (const auto &[other, d] : o.metadata_records)
                for (const auto &entry : d.dictionary_entries)
                    if (entry.second == e.second && (other != dict || entry.first != key))
                        throw std::invalid_argument("Shared metadata record");
            return &r->second;
        }
    return nullptr;
}
std::string edit_dwg_metadata(std::string_view, const DwgR2000Objects &, std::uint64_t, const std::string &);
std::string edit_dwg_history_slot(std::string_view, const DwgR2000Objects &, std::uint64_t, std::size_t,
                                  std::size_t, std::uint64_t, bool, unsigned = 5);
} // namespace libopencad::detail
