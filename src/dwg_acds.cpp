// SPDX-License-Identifier: MPL-2.0
// AcDs record/blob association, ported from acadrust io/dwg/dwg_reader.rs.
#include "dwg_modeler_internal.hpp"
#include "acis_sab_internal.hpp"
#include <libopencad/acis.hpp>
#include <libopencad/dwg_sections.hpp>
#include <algorithm>
#include <set>
#include <optional>
#include <limits>
namespace libopencad::detail {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr auto missing = std::string_view::npos;
constexpr std::string_view data_marker = "\xAC\xD5_data_";
constexpr std::string_view page_marker = "\xAC\xD5"
                                         "blob01";
std::optional<std::uint64_t> word(std::string_view b, std::size_t p, std::size_t n = 4) {
    if (p > b.size() || n > b.size() - p)
        return {};
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < n; ++i)
        v |= std::uint64_t(static_cast<unsigned char>(b[p + i])) << (8 * i);
    return v;
}
std::size_t magic(std::string_view b, std::size_t from = 0) {
    return std::min(b.find("ACIS BinaryFile", from), b.find("ASM BinaryFile", from));
}
std::size_t stop(std::string_view b, std::size_t from, std::size_t bound) {
    bound = std::min(bound, b.size());
    if (from > bound)
        return missing;
    try {
        return from + acis::detail::sab_extent(b.substr(from, bound - from));
    } catch (const acis::SabError &) {
        return missing;
    }
}

Bytes bytes(std::string_view b) { return {b.begin(), b.end()}; }
std::vector<std::pair<std::size_t, std::size_t>> ranges(std::string_view b) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t p = 0;
    while ((p = magic(b, p)) != missing) {
        auto end = stop(b, p, b.size());
        if (end == missing) {
            ++p;
            continue;
        }
        out.emplace_back(p, end);
        p = end;
    }
    return out;
}
struct AcdsSegment {
    std::size_t start, size;
};
// Indexed containers are authoritative: never discover segments inside SAB strings,
// schema bytes, unindexed trailers or page payloads by searching for marker bytes.
std::vector<AcdsSegment> read_segments(std::string_view b) {
    std::vector<AcdsSegment> segments;
    if (b.substr(0, 4) == "jard") {
        auto require = [](bool ok, const char *why) {
            if (!ok)
                throw std::invalid_argument(why);
        };
        auto get = [&](std::size_t at, unsigned n = 4) {
            auto v = word(b, at, n);
            require(v.has_value(), "Truncated AcDs directory");
            return *v;
        };
        auto header = get(4), index = get(24), count = get(32);
        require(header >= 56 && header <= b.size() && index >= header && index <= b.size() &&
                    b.size() - index >= 48 && b.substr(index, 8) == std::string_view("\xAC\xD5segidx"),
                "Invalid AcDs directory");
        auto index_size = get(index + 16, 8);
        require(index_size >= 48 && index_size <= b.size() - index && count <= (index_size - 48) / 12,
                "Invalid AcDs directory length");
        bool self = false;
        for (std::size_t i = 0; i < count; ++i) {
            auto row = index + 48 + 12 * i, at = get(row, 8), size = get(row + 8);
            if (!at && !size)
                continue;
            require(at >= header && at <= b.size() && size >= 48 && size <= b.size() - at &&
                        b.substr(at, 2) == std::string_view("\xAC\xD5") && get(at + 16, 8) == size,
                    "Invalid AcDs segment bounds");
            if (at == index && size == index_size)
                self = true;
            segments.push_back({std::size_t(at), std::size_t(size)});
        }
        require(self, "AcDs directory missing its own segment");
        std::sort(segments.begin(), segments.end(), [](auto a, auto c) { return a.start < c.start; });
        for (std::size_t i = 1; i < segments.size(); ++i)
            require(segments[i - 1].start + segments[i - 1].size <= segments[i].start,
                    "Overlapping AcDs segments");
        return segments;
    }
    // Legacy unindexed storage: walk whole data/page segments and typed SAB bodies.
    // In particular, skip an entire page payload even when it contains only part of
    // a SAB token (and cannot independently be parsed as a complete SAB).
    std::size_t scan = 0;
    while (scan < b.size()) {
        auto seg = std::min(b.find(data_marker, scan), b.find(page_marker, scan));
        auto body = magic(b, scan);
        if (body < seg) {
            auto end = stop(b, body, b.size());
            if (end == missing)
                break; // Do not reinterpret bytes inside a damaged raw body as segments.
            scan = end;
            continue;
        }
        if (seg == missing)
            break;
        auto size = word(b, seg + 16, 8);
        if (!size || *size < 48) {
            scan = seg + 8;
            continue;
        }
        auto length = std::min<std::uint64_t>(*size, b.size() - seg);
        if (b.substr(seg, 8) == page_marker && *size == 48) {
            // Some legacy pages declare only the fixed segment header. Only
            // that representation uses the payload size to locate the next page.
            // A fully sized segment must not grow into its neighbor on corruption.
            auto payload = word(b, seg + 72, 8);
            if (b.size() - seg >= 80 && payload && *payload <= b.size() - seg - 80)
                length = std::max<std::uint64_t>(length, 80 + *payload);
        }
        segments.push_back({seg, std::size_t(length)});
        scan = seg + length;
    }
    return segments;
}
template <class Segment>
std::map<std::uint64_t, std::size_t> page_index(std::string_view b, const std::vector<Segment> &segments) {
    std::map<std::uint64_t, std::size_t> pages;
    std::set<std::uint64_t> seen, duplicate;
    for (auto seg : segments) {
        if (b.substr(seg.start, 8) != page_marker)
            continue;
        auto id = word(b, seg.start + 8), size = word(b, seg.start + 72, 8);
        if (!id)
            continue;
        // A damaged duplicate is still a conflicting declaration of this ID.
        // Detect it before validating the payload, independent of physical order.
        if (!seen.insert(*id).second)
            duplicate.insert(*id);
        if (word(b, seg.start + 12) == 1 && size && seg.size >= 80 && *size <= seg.size - 80)
            pages.emplace(*id, seg.start);
    }
    for (auto id : duplicate)
        pages.erase(id);
    return pages;
}
std::optional<std::pair<std::size_t, Bytes>> resolve(std::string_view b, std::string_view region,
                                                     const std::map<std::uint64_t, std::size_t> &pages) {
    auto total = word(region, 4, 8), count = word(region, 12), refsize = word(region, 16);
    if (!total || !count || !refsize || !*count || *total > b.size() || *count > region.size() / 8 ||
        *refsize != 32 + 8 * (*count) || region.size() < 36 + 8 * (*count))
        return {};
    std::string joined;
    std::vector<std::pair<std::size_t, std::size_t>> starts;
    for (std::size_t i = 0; i < *count; ++i) {
        auto id = word(region, 36 + 8 * i), size = word(region, 40 + 8 * i);
        auto page = pages.find(*id);
        if (page == pages.end())
            return {};
        auto p = page->second;
        if (word(b, p + 48, 8) != total || word(b, p + 56, 8) != joined.size() ||
            word(b, p + 64) != i || word(b, p + 68) != count ||
            word(b, p + 72, 8) != size || p > b.size() || b.size() - p < 80 || *size > b.size() - p - 80 ||
            *size > *total - joined.size())
            return {};
        starts.emplace_back(joined.size(), p + 80);
        joined.append(b.substr(p + 80, *size));
    }
    if (joined.size() != *total)
        return {};
    auto start = magic(joined);
    if (start != 0 && start != 4)
        return {};
    auto bound = joined.size();
    if (start == 4) {
        auto length = word(joined, 0);
        if (!length || *length > joined.size() - 4)
            return {};
        bound = 4 + *length;
    }
    auto end = stop(joined, start, bound);
    if (end == missing)
        return {};
    auto it = std::upper_bound(starts.begin(), starts.end(),
                               std::make_pair(start, std::numeric_limits<std::size_t>::max()));
    --it;
    return std::make_pair(it->second + start - it->first,
                          bytes(std::string_view(joined).substr(start, end - start)));
}
} // namespace
// Kept separate from the container reader so malformed, reordered and paged
// datastore fixtures exercise the association logic directly.
std::map<std::uint64_t, Bytes> decode_dwg_acds(std::string_view b, const std::set<std::uint64_t> &wanted) {
    std::map<std::uint64_t, Bytes> out;
    std::set<std::uint64_t> ambiguous;
    std::vector<std::pair<std::size_t, std::size_t>> rejected_ranges;
    auto reject = [&](std::uint64_t h, std::size_t begin, std::size_t end) {
        out.erase(h);
        ambiguous.insert(h);
        rejected_ranges.emplace_back(begin, end);
    };
    auto assign = [&](std::uint64_t h, Bytes body) {
        if (ambiguous.count(h))
            return;
        auto it = out.find(h);
        if (it == out.end())
            out.emplace(h, std::move(body));
        else if (it->second != body) {
            out.erase(it);
            ambiguous.insert(h);
        }
    };
    std::set<std::size_t> claimed;
    std::vector<std::uint64_t> orphan;
    auto segments = read_segments(b);
    auto pages = page_index(b, segments);
    bool table = false;
    std::vector<std::pair<std::size_t, std::size_t>> all;
    for (auto segment : segments) {
        auto seg = segment.start, end = seg + segment.size;
        if (segment.size < 48 || b.substr(seg, 8) != data_marker)
            continue;
        for (auto [start, stop_at] : ranges(b.substr(seg + 48, segment.size - 48)))
            all.emplace_back(seg + 48 + start, seg + 48 + stop_at);
        if (segment.size < 68 || word(b, seg + 48) != 0x14)
            continue;
        std::vector<std::pair<std::uint64_t, std::size_t>> records;
        auto p = seg + 48;
        while (p <= end && end - p >= 20 && word(b, p) == 0x14) {
            records.emplace_back(*word(b, p + 8, 8), *word(b, p + 16));
            p += 20;
        }
        // Even one row is an explicit owner/offset binding. Positional fallback
        // is only appropriate when no record table could be parsed.
        if (records.empty())
            continue;
        table = true;
        auto base = p;
        auto units = word(b, seg + 36);
        if (units && *units <= (end - seg) / 16 && seg + *units * 16 >= p && seg + *units * 16 < end)
            base = seg + *units * 16;
        for (std::size_t i = 0; i < records.size(); ++i) {
            auto [h, offset] = records[i];
            auto next = i + 1 < records.size() ? records[i + 1].second : end - base;
            if (offset >= end - base || next <= offset) {
                if (wanted.count(h))
                    orphan.push_back(h);
                continue;
            }
            auto begin = base + offset;
            auto limit = base + std::min(next, end - base);
            auto region = b.substr(begin, limit - begin);
            auto mp = magic(region);
            if (mp != 0 && mp != 4) {
                auto result = resolve(b, region, pages);
                if (result) {
                    claimed.insert(result->first);
                    assign(h, std::move(result->second));
                } else {
                    // A recognizable but inconsistent page reference is corruption,
                    // not permission to guess its owner from an unrelated blob.
                    auto pages_count = word(region, 12), ref_size = word(region, 16);
                    bool page_reference =
                        pages_count && ref_size && *pages_count && *pages_count <= region.size() / 8 &&
                        *ref_size == 32 + 8 * (*pages_count) && region.size() >= 36 + 8 * (*pages_count);
                    if (page_reference || mp != missing)
                        reject(h, begin, limit);
                    else if (wanted.count(h))
                        orphan.push_back(h);
                }
                continue;
            }
            auto start = begin + mp;
            // Modern inline records carry a length before the SAB. Never let a
            // corrupt body consume bytes from its neighbor or trailing padding.
            auto length = word(region, 0);
            auto bound = limit;
            if (mp == 4 && length) {
                if (*length > region.size() - 4) {
                    reject(h, begin, limit);
                    continue;
                }
                bound = start + *length;
            }
            auto stop_at = stop(b, start, bound);
            if (stop_at != missing) {
                claimed.insert(start);
                assign(h, bytes(b.substr(start, stop_at - start)));
            } else {
                // An explicitly framed but invalid body is not an anonymous
                // recovery candidate for a different owner (including nested SABs).
                reject(h, begin, limit);
            }
        }
    }
    std::sort(rejected_ranges.begin(), rejected_ranges.end());
    std::vector<std::pair<std::size_t, std::size_t>> excluded;
    for (auto range : rejected_ranges) {
        if (!excluded.empty() && range.first <= excluded.back().second)
            excluded.back().second = std::max(excluded.back().second, range.second);
        else
            excluded.push_back(range);
    }
    if (segments.empty() && b.substr(0, 4) != "jard") {
        // A raw legacy stream is a sequence of complete bodies, not permission
        // to search inside a damaged body's strings for replacement geometry.
        std::size_t at = 0;
        while (at < b.size()) {
            auto start = magic(b, at);
            auto bound = b.size();
            if (start == at + 4) {
                auto length = word(b, at);
                if (!length || *length > b.size() - start) {
                    all.clear();
                    break;
                }
                bound = start + *length;
            } else if (start != at) {
                all.clear();
                break;
            }
            auto end = stop(b, start, bound);
            if (end == missing) {
                all.clear();
                break;
            }
            all.emplace_back(start, end);
            at = start == at + 4 ? bound : end;
        }
    }
    std::vector<Bytes> pool;
    for (auto [start, end] : all) {
        auto blocked = std::lower_bound(excluded.begin(), excluded.end(), start,
                                        [](auto range, auto at) { return range.second <= at; });
        if (!claimed.count(start) && (blocked == excluded.end() || blocked->first >= end))
            pool.push_back(bytes(b.substr(start, end - start)));
    }
    if (table) {
        if (pool.size() == orphan.size())
            for (std::size_t i = 0; i < pool.size(); ++i)
                assign(orphan[i], std::move(pool[i]));
    } else if (b.substr(0, 4) != "jard" && pool.size() == wanted.size()) {
        auto it = wanted.begin();
        for (auto &blob : pool)
            assign(*it++, std::move(blob));
    }
    return out;
}
std::string replace_dwg_acds_body(std::string_view b, std::uint64_t h, const Bytes &replacement) {
    auto require = [](bool ok, const char *why) {
        if (!ok)
            throw std::invalid_argument(why);
    };
    auto get = [&](std::size_t p, std::size_t n = 4) {
        auto v = word(b, p, n);
        require(v.has_value(), "Truncated AcDs index");
        return *v;
    };
    require(b.substr(0, 4) == "jard" && get(4) >= 56 && get(4) <= b.size(), "Unsupported AcDs header");
    auto index = get(24), count = get(32);
    require(index <= b.size() && b.size() - index >= 48 && count <= (b.size() - index - 48) / 12,
            "Invalid AcDs segment index");
    require(b.substr(index, 8) == std::string_view("\xAC\xD5segidx"), "Unsupported AcDs segment index");
    struct Segment {
        std::size_t row, start, size;
    };
    std::vector<Segment> segments;
    for (std::size_t i = 0; i < count; ++i) {
        auto p = index + 48 + i * 12;
        auto at = get(p), size = get(p + 8);
        if (!at && !size)
            continue;
        require(get(p + 4) == 0 && at >= get(4) && at <= b.size() && size >= 48 && size <= b.size() - at,
                "Invalid AcDs segment bounds");
        require(get(at + 16, 8) == size, "AcDs segment size mismatch");
        segments.push_back({p, std::size_t(at), std::size_t(size)});
    }
    auto sorted = segments;
    std::sort(sorted.begin(), sorted.end(), [](auto a, auto c) { return a.start < c.start; });
    for (std::size_t i = 1; i < sorted.size(); ++i)
        require(sorted[i - 1].start + sorted[i - 1].size <= sorted[i].start, "Overlapping AcDs segments");
    auto put = [](std::string &out, std::size_t p, std::uint64_t v, std::size_t n = 4) {
        if (n < 8 && v > 0xffffffffu)
            throw std::length_error("AcDs index exceeds 32-bit range");
        for (std::size_t i = 0; i < n; ++i)
            out.at(p + i) = char(v >> (8 * i));
    };
    auto pages = page_index(b, segments);
    std::optional<Segment> selected;
    std::string changed;
    for (auto seg : segments) {
        if (b.substr(seg.start, 8) != data_marker || get(seg.start + 12) != 0)
            continue;
        auto end = seg.start + seg.size, p = seg.start + 48;
        std::vector<std::size_t> records;
        while (p <= end && end - p >= 20 && get(p) == 0x14) {
            records.push_back(p);
            p += 20;
        }
        if (records.empty())
            continue;
        auto units = get(seg.start + 36);
        require(units <= seg.size / 16, "Invalid AcDs blob alignment");
        auto base = seg.start + units * 16;
        if (base < p || base >= end)
            continue; // Legacy interleaved storage is read-only.
        for (std::size_t i = 0; i < records.size(); ++i) {
            auto row = records[i];
            if (get(row + 8, 8) != h)
                continue;
            require(!selected, "Duplicate AcDs owner");
            auto offset = get(row + 16);
            require(offset < end - base, "AcDs record has no local data or page reference");
            auto start = base + offset;
            auto limit = i + 1 < records.size() ? base + get(records[i + 1] + 16) : end;
            require(limit > start && limit <= end && limit - start >= 4, "Invalid AcDs body range");
            auto length = get(start);
            std::size_t old_size;
            if (length <= limit - start - 4 && magic(b.substr(start + 4, length)) == 0)
                old_size = 4 + length;
            else {
                auto region = b.substr(start, limit - start);
                require(resolve(b, region, pages).has_value(), "Invalid or unsupported AcDs page reference");
                old_size = 36 + 8 * (*word(region, 12));
            }
            // Promote a verified external body to inline storage. Keep its old
            // pages verbatim: they may be shared by records outside this schema.
            changed = std::string(b.substr(seg.start, seg.size));
            auto local = start - seg.start;
            std::string record(4, 0);
            record.append(reinterpret_cast<const char *>(replacement.data()), replacement.size());
            put(record, 0, replacement.size());
            changed.replace(local, old_size, record);
            for (std::size_t j = i + 1; j < records.size(); ++j) {
                auto old = get(records[j] + 16);
                require(old >= offset + old_size, "Overlapping AcDs bodies");
                put(changed, records[j] + 16 - seg.start, old - old_size + 4 + replacement.size());
            }
            changed.resize((changed.size() + 15) / 16 * 16, char(0x70));
            put(changed, 16, changed.size(), 8);
            selected = seg;
        }
    }
    require(selected.has_value(), "No editable indexed AcDs body for this handle");
    auto seg = *selected;
    auto relocate = [&](std::size_t at) {
        return at >= seg.start + seg.size ? at - seg.size + changed.size() : at;
    };
    std::string out(b);
    out.replace(seg.start, seg.size, changed);
    put(out, 24, relocate(index));
    put(out, 52, out.size());
    for (auto s : segments) {
        auto row = relocate(s.row);
        put(out, row, relocate(s.start));
        if (s.start == seg.start)
            put(out, row + 8, changed.size());
    }
    return out;
}
#include "dwg_acds_create.inc"
void attach_dwg_modeler_data(std::string_view file, DwgR2000Objects &objects) {
    std::set<std::uint64_t> wanted;
    for (auto &[h, m] : objects.modelers)
        if (m.properties.has_ds_data)
            wanted.insert(h);
    if (wanted.empty())
        return;
    try {
        auto section = read_dwg_paged_section(file, "AcDb:AcDsPrototype_1b");
        auto blobs = decode_dwg_acds(section, wanted);
        for (auto h : wanted) {
            auto it = blobs.find(h);
            if (it == blobs.end()) {
                objects.modeler_errors[h] = "Missing or ambiguous AcDs geometry";
                continue;
            }
            auto &g = objects.modelers.at(h).data.geometry;
            g.sab = std::move(it->second);
            g.sat.clear();
            g.is_binary = true;
        }
    } catch (const std::exception &e) {
        for (auto h : wanted)
            objects.modeler_errors[h] = std::string("AcDs: ") + e.what();
    }
}
} // namespace libopencad::detail
