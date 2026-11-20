// SPDX-License-Identifier: MPL-2.0
// AcDs schema/index authoring from acadrust io/dwg/dwg_writer.rs.
namespace {
#include "dwg_acds_schema.inc"
void acds_require(bool b, const char *message) {
    if (!b)
        throw std::invalid_argument(message);
}
std::uint64_t acds_get(std::string_view s, std::size_t p, unsigned n = 4) {
    auto v = word(s, p, n);
    acds_require(v.has_value(), "Truncated AcDs table");
    return *v;
}
void acds_put(std::string &s, std::size_t p, std::uint64_t n, unsigned width = 4) {
    acds_require(p <= s.size() && width <= s.size() - p, "AcDs field bounds");
    for (unsigned i = 0; i < width; ++i) {
        s[p + i] = char(n);
        n >>= 8;
    }
    if (n)
        throw std::length_error("AcDs field overflow");
}
void acds_append(std::string &s, std::uint64_t v, unsigned width = 4) {
    auto p = s.size();
    s.resize(p + width);
    acds_put(s, p, v, width);
}
std::size_t acds_align(std::size_t n) {
    if (n > UINT32_MAX - 15)
        throw std::length_error("AcDs segment exceeds 32-bit range");
    return (n + 15) & ~std::size_t(15);
}
std::string acds_segment(std::string_view name, std::uint32_t id, std::size_t size) {
    std::string s(acds_align(size), char(0x70));
    s.replace(0, 48, 48, 0);
    s[0] = char(0xac);
    s[1] = char(0xd5);
    s.replace(2, 6, name);
    acds_put(s, 8, id);
    acds_put(s, 16, s.size(), 8);
    acds_put(s, 24, 1);
    s.replace(40, 8, "UUUUUUUU");
    return s;
}
std::string acds_datidx(std::size_t n) {
    auto s = acds_segment("datidx", 4, std::max<std::size_t>(128, 56 + 12 * n));
    acds_put(s, 48, n);
    acds_put(s, 52, 0);
    for (std::size_t i = 0; i < n; ++i) {
        acds_put(s, 56 + i * 12, 2);
        acds_put(s, 60 + i * 12, i * 20);
        acds_put(s, 64 + i * 12, 1);
    }
    return s;
}
std::string acds_search(const std::vector<std::uint64_t> &handles) {
    std::string content;
    acds_append(content, 2);
    acds_append(content, 1);
    acds_append(content, handles.size());
    for (std::size_t i = 0; i < handles.size(); ++i)
        acds_append(content, std::uint64_t(i) << 32, 8);
    for (auto v : {0u, 1u, 0u})
        acds_append(content, v);
    acds_append(content, handles.size());
    std::map<std::uint64_t, std::size_t> sorted;
    for (std::size_t i = 0; i < handles.size(); ++i)
        acds_require(sorted.emplace(handles[i], i).second, "Duplicate AcDs owner");
    for (auto [h, i] : sorted) {
        acds_append(content, h, 8);
        acds_append(content, 1, 8);
        acds_append(content, i, 8);
    }
    for (auto v : {0u, 0u, 0u, 1u, 0u, 0u})
        acds_append(content, v);
    auto s = acds_segment("search", 7, std::max<std::size_t>(192, 48 + content.size()));
    s.replace(48, content.size(), content);
    return s;
}
struct AcdsRecord {
    std::uint64_t handle;
    std::string bytes;
};
std::string acds_data(const std::vector<AcdsRecord> &rows) {
    auto base = 48 + acds_align(rows.size() * 20);
    std::size_t size = base;
    for (auto &r : rows) {
        if (r.bytes.size() > UINT32_MAX - size)
            throw std::length_error("AcDs body data exceeds limit");
        size += r.bytes.size();
    }
    auto s = acds_segment("_data_", 2, size);
    acds_put(s, 36, base / 16);
    s.replace(48, base - 48, base - 48, char(0x62));
    std::size_t offset = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        auto p = 48 + i * 20;
        acds_put(s, p, 20);
        acds_put(s, p + 4, 1);
        acds_put(s, p + 8, rows[i].handle, 8);
        acds_put(s, p + 16, offset);
        s.replace(base + offset, rows[i].bytes.size(), rows[i].bytes);
        offset += rows[i].bytes.size();
    }
    return s;
}
template <std::size_t N> std::string acds_template(const unsigned char (&a)[N]) {
    return {reinterpret_cast<const char *>(a), N};
}
struct AcdsCatalog {
    std::string header;
    std::map<std::size_t, std::string> parts;
    std::vector<std::size_t> order;
    static AcdsCatalog read(std::string_view s) {
        acds_require(s.substr(0, 4) == "jard" && acds_get(s, 4) == 128 && acds_get(s, 36) == 6 &&
                         acds_get(s, 40) == 4 && acds_get(s, 44) == 7,
                     "Unsupported AcDs catalog schema");
        auto idx = acds_get(s, 24), n = acds_get(s, 32);
        acds_require(idx <= s.size() && s.size() - idx >= 48 && n <= (s.size() - idx - 48) / 12,
                     "Invalid AcDs catalog index");
        acds_require(s.substr(idx, 8) == std::string_view("\xAC\xD5segidx") && acds_get(s, idx + 8) == 1,
                     "Invalid AcDs index segment");
        AcdsCatalog c;
        c.header = std::string(s.substr(0, 128));
        std::map<std::size_t, std::size_t> physical;
        for (std::size_t i = 0; i < n; ++i) {
            auto p = idx + 48 + i * 12, at = acds_get(s, p), len = acds_get(s, p + 8);
            if (!at && !len)
                continue;
            acds_require(i && acds_get(s, p + 4) == 0 && at >= 128 && at <= s.size() && len >= 48 &&
                             len <= s.size() - at,
                         "Invalid AcDs segment bounds");
            acds_require(acds_get(s, at + 8) == i && acds_get(s, at + 16, 8) == len &&
                             physical.emplace(at, i).second,
                         "Invalid AcDs segment identity");
            c.parts[i] = std::string(s.substr(at, len));
        }
        std::size_t cursor = 128;
        for (auto [at, id] : physical) {
            acds_require(at == cursor, "Unsupported AcDs segment gaps/overlap");
            cursor += c.parts.at(id).size();
            c.order.push_back(id);
        }
        acds_require(cursor == s.size(), "Unknown AcDs trailing data");
        acds_require(c.parts.count(1) && c.parts.count(2) && c.parts.count(3) && c.parts.count(4) &&
                         c.parts.count(5) && c.parts.count(6) && c.parts.count(7),
                     "Incomplete AcDs schema");
        acds_require(physical.count(idx) && physical.at(idx) == 1 && c.parts.at(1).size() >= 48 + n * 12,
                     "AcDs index does not match its own directory entry");
        acds_require(c.parts.at(5) == acds_template(ACDS_SCHDAT_TEMPLATE) &&
                         c.parts.at(6) == acds_template(ACDS_SCHIDX_TEMPLATE),
                     "Unsupported AcDs schema definitions");
        return c;
    }
    static AcdsCatalog fresh() {
        AcdsCatalog c;
        c.header = std::string(128, 0);
        c.header.replace(0, 4, "jard");
        acds_put(c.header, 4, 128);
        acds_put(c.header, 8, 2);
        acds_put(c.header, 12, 2);
        acds_put(c.header, 20, 1);
        acds_put(c.header, 32, 8);
        acds_put(c.header, 36, 6);
        acds_put(c.header, 40, 4);
        acds_put(c.header, 44, 7);
        c.parts[1] = acds_segment("segidx", 1, 192);
        c.parts[1].replace(48, 96, 96, 0);
        c.parts[3] = acds_segment("_data_", 3, 64);
        acds_put(c.parts[3], 36, 4);
        c.parts[3].replace(48, 16, 16, char(0x62));
        c.parts[5] = acds_template(ACDS_SCHDAT_TEMPLATE);
        c.parts[6] = acds_template(ACDS_SCHIDX_TEMPLATE);
        c.order = {2, 3, 4, 5, 6, 7, 1};
        return c;
    }
    std::string write() {
        std::string s = header;
        std::map<std::size_t, std::size_t> offsets;
        for (auto id : order) {
            offsets[id] = s.size();
            s += parts.at(id);
        }
        auto index = offsets.at(1);
        acds_put(s, 24, index);
        acds_put(s, 52, s.size());
        for (auto [id, at] : offsets) {
            auto p = index + 48 + 12 * id;
            acds_put(s, p, at);
            acds_put(s, p + 4, 0);
            acds_put(s, p + 8, parts.at(id).size());
        }
        return s;
    }
};
std::vector<AcdsRecord> acds_records(const AcdsCatalog &c) {
    const auto &s = c.parts.at(2);
    acds_require(s.substr(0, 8) == data_marker && acds_get(s, 12) == 0, "Unsupported AcDs data segment");
    auto base = acds_get(s, 36) * 16;
    acds_require(base >= 48 && base <= s.size(), "Invalid AcDs data alignment");
    std::vector<std::pair<std::uint64_t, std::size_t>> refs;
    for (std::size_t p = 48; p + 20 <= base && acds_get(s, p) == 20; p += 20) {
        acds_require(acds_get(s, p + 4) == 1, "Unknown AcDs record schema");
        refs.emplace_back(acds_get(s, p + 8, 8), acds_get(s, p + 16));
    }
    acds_require(base == 48 + acds_align(refs.size() * 20), "Unknown AcDs table data");
    std::vector<AcdsRecord> out;
    std::vector<std::uint64_t> handles;
    for (std::size_t i = 0; i < refs.size(); ++i) {
        auto start = refs[i].second;
        auto end = i + 1 < refs.size() ? refs[i + 1].second : s.size() - base;
        acds_require((i || !start) && start < end && end <= s.size() - base, "Invalid AcDs record span");
        out.push_back({refs[i].first, s.substr(base + start, end - start)});
        handles.push_back(refs[i].first);
    }
    acds_require(c.parts.at(4) == acds_datidx(refs.size()) && c.parts.at(7) == acds_search(handles),
                 "Unsupported or inconsistent AcDs lookup indices");
    return out;
}
} // namespace
std::string insert_dwg_acds_body(std::string_view source, std::uint64_t h, const Bytes &data) {
    acds_require(h && h <= UINT32_MAX, "AcDs handle exceeds upstream's 32-bit reader range");
    auto catalog = source.empty() ? AcdsCatalog::fresh() : AcdsCatalog::read(source);
    auto records = source.empty() ? std::vector<AcdsRecord>{} : acds_records(catalog);
    for (auto &r : records)
        acds_require(r.handle != h, "AcDs already contains this handle");
    std::string body;
    acds_append(body, data.size());
    body.append(reinterpret_cast<const char *>(data.data()), data.size());
    records.push_back({h, std::move(body)});
    std::vector<std::uint64_t> handles;
    for (auto &r : records)
        handles.push_back(r.handle);
    catalog.parts[2] = acds_data(records);
    catalog.parts[4] = acds_datidx(records.size());
    catalog.parts[7] = acds_search(handles);
    return catalog.write();
}

std::string remove_dwg_acds_body(std::string_view source, std::uint64_t h) {
    auto catalog = AcdsCatalog::read(source);
    auto rows = acds_records(catalog);
    auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &row) { return row.handle == h; });
    acds_require(it != rows.end(), "Missing AcDs owner");
    rows.erase(it);
    std::vector<std::uint64_t> handles;
    for (auto &r : rows)
        handles.push_back(r.handle);
    catalog.parts[2] = acds_data(rows);
    catalog.parts[4] = acds_datidx(rows.size());
    catalog.parts[7] = acds_search(handles);
    return catalog.write();
}
