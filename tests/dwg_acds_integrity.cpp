// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include "../src/dwg_metadata_internal.hpp"
#include "../src/dwg_modeler_internal.hpp"
#include "../src/dwg_sections_internal.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace libopencad;
using Bytes = std::vector<std::uint8_t>;
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static std::uint64_t get(const std::string &s, std::size_t at, unsigned n = 4) {
    return detail::metadata_uint(s, at, n);
}
static void put(std::string &s, std::size_t at, std::uint64_t v, unsigned n = 4) {
    for (unsigned i = 0; i < n; ++i)
        s.at(at + i) = char(v >> (8 * i));
}
static std::string bytes(const DwgDocument &d) {
    std::ostringstream out;
    d.write(out);
    return out.str();
}
static DwgDocument parse(const std::string &s) {
    std::istringstream in(s);
    return DwgDocument::read(in);
}
static DwgDocument attach(const DwgDocument &d, const std::string &section) {
    return parse(detail::replace_ac18_sections(bytes(d), {{"AcDb:AcDsPrototype_1b", section}}));
}
static std::string data_segment(const std::string &s) {
    auto p = s.find("\xAC\xD5_data_");
    return s.substr(p, get(s, p + 16, 8));
}
// Generic directory fixture: permits independent data segments, unlike the authoring schema.
static std::string catalog(std::vector<std::string> parts) {
    std::string s(128, 0);
    s.replace(0, 4, "jard");
    put(s, 4, 128);
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        put(parts[i], 8, i + 2);
        starts.push_back(s.size());
        s += parts[i];
    }
    auto index = s.size(), count = parts.size() + 2;
    s.resize(index + 48 + count * 12, 0);
    s.replace(index, 8, "\xAC\xD5segidx");
    put(s, index + 8, 1);
    put(s, index + 16, s.size() - index, 8);
    put(s, 24, index);
    put(s, 32, count);
    put(s, index + 60, index);
    put(s, index + 68, s.size() - index);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        put(s, index + 48 + (i + 2) * 12, starts[i]);
        put(s, index + 56 + (i + 2) * 12, parts[i].size());
    }
    put(s, 52, s.size());
    return s;
}
static std::vector<std::string> paged(std::uint64_t h, const Bytes &sab) {
    auto table = data_segment(detail::insert_dwg_acds_body({}, h, sab));
    auto base = get(table, 36) * 16;
    table.resize(base + 52, 0);
    table.replace(base, 52, 52, 0);
    put(table, 16, table.size(), 8);
    put(table, base + 4, sab.size(), 8);
    put(table, base + 12, 2);
    put(table, base + 16, 48);
    // Split before the opaque string, so the second page contains nested markers
    // without a complete outer SAB header. Marker scanning is unsafe on either page.
    const std::size_t cut = 64;
    check(sab.size() > cut, "Short paged test body");
    std::vector<std::string> parts{table};
    for (unsigned i = 0; i < 2; ++i) {
        auto from = i ? cut : 0, n = i ? sab.size() - cut : cut;
        put(parts[0], base + 36 + 8 * i, 3 + i);
        put(parts[0], base + 40 + 8 * i, n);
        std::string page(80, 0);
        page.replace(0, 8,
                     "\xAC\xD5"
                     "blob01");
        put(page, 8, 3 + i);
        put(page, 12, 1);
        put(page, 16, 80 + n, 8);
        put(page, 48, sab.size(), 8);
        put(page, 56, from, 8);
        put(page, 64, i);
        put(page, 68, 2);
        put(page, 72, n, 8);
        page.append(sab.begin() + from, sab.begin() + from + n);
        parts.push_back(page);
    }
    return parts;
}
static std::string legacy(const std::vector<std::string> &parts) {
    std::string s;
    for (const auto &p : parts)
        s += p;
    return s;
}
static acis::SatDocument with_payload(const std::string &payload) {
    auto shape = acis::build_box({}, 1, 2, 3);
    acis::SabToken token;
    token.tag = 9;
    for (unsigned i = 0; i < 4; ++i)
        token.data.push_back(std::uint8_t(payload.size() >> (8 * i)));
    token.data.insert(token.data.end(), payload.begin(), payload.end());
    acis::SatRecord rec;
    rec.entity_type = "test-attrib";
    rec.tokens.emplace_back(token);
    shape.add_record(rec);
    return shape;
}
static void nested(DxfVersion v) {
    auto d = parse(create_dwg(v, {}));
    auto shape = acis::build_box({}, 1, 2, 3);
    auto h = d.add_modeler(ModelerKind::Solid3D, shape);
    auto sphere = acis::SabWriter::write(acis::build_sphere({}, 7));
    shape = with_payload(detail::insert_dwg_acds_body({}, h, sphere));
    auto sab = acis::SabWriter::write(shape);
    check(acis::SabReader::read(sab).records.size() == shape.records.size(), "Invalid SAB fixture");
    d.set_modeler_geometry(h, shape);
    check(d.modeler(h).data.geometry.sab == sab, "Nested table replaced outer geometry");
    d = parse(bytes(d));
    check(d.modeler(h).data.geometry.sab == sab, "Save lost nested SAB bytes");
    auto section = detail::insert_dwg_acds_body({}, h, sab);
    check(detail::decode_dwg_acds(data_segment(section), {h}).at(h) == sab,
          "Legacy segment scanned nested table");
    std::string raw(sab.begin(), sab.end());
    check(detail::decode_dwg_acds(raw, {h}).at(h) == sab, "Raw SAB scanned nested table");
    // A segment-like trailer is not indexed and must not become authoritative.
    section += data_segment(detail::insert_dwg_acds_body({}, h, sphere));
    check(detail::decode_dwg_acds(section, {h}).at(h) == sab, "Unindexed trailer replaced geometry");
    auto parts = paged(h, sab);
    for (bool indexed : {false, true}) {
        auto s = indexed ? catalog(parts) : legacy(parts);
        check(detail::decode_dwg_acds(s, {h}).at(h) == sab, "Paged SAB scanned nested table");
        if (indexed) {
            auto loaded = attach(d, s);
            check(loaded.modeler(h).data.geometry.sab == sab, "Indexed nested page read failed");
            loaded.set_modeler_geometry(h, shape);
            check(parse(bytes(loaded)).modeler(h).data.geometry.sab == sab,
                  "Paged nested geometry replacement failed");
        }
    }
    // An opaque inline string can contain a complete blob page with the same ID
    // as a real later page. It must never shadow that page in the page lookup.
    auto actual = paged(h + 1, sphere);
    auto false_page = actual[1];
    put(false_page, 48, sphere.size() + 1, 8);
    auto carrier = acis::SabWriter::write(with_payload(false_page));
    auto carrier_table = data_segment(detail::insert_dwg_acds_body({}, h, carrier));
    actual.insert(actual.begin() + 1, carrier_table);
    // Catalog assigns IDs from physical order; keep real references aligned.
    auto ref_base = get(actual[0], 36) * 16;
    put(actual[0], ref_base + 36, 4);
    put(actual[0], ref_base + 44, 5);
    put(actual[2], 8, 4);
    put(actual[3], 8, 5);
    // Rebuild the opaque fake page with the real page's updated ID.
    put(false_page, 8, 4);
    carrier = acis::SabWriter::write(with_payload(false_page));
    actual[1] = data_segment(detail::insert_dwg_acds_body({}, h, carrier));
    for (bool indexed : {false, true}) {
        auto s = indexed ? catalog(actual) : legacy(actual);
        auto decoded = detail::decode_dwg_acds(s, {h, h + 1});
        check(decoded.at(h) == carrier && decoded.at(h + 1) == sphere, "SAB string shadowed real blob page");
    }
    // An indexed container with corrupt directory bounds must fail closed, never scan payloads.
    auto index = get(section, 24);
    put(section, index + 16, 48, 8);
    auto broken = attach(d, section);
    check(broken.objects().modeler_errors.count(h), "Corrupt segment index accepted");
    check(bytes(parse(bytes(broken))) == bytes(broken), "Corrupt index opaque save changed");
    auto clean = detail::insert_dwg_acds_body({}, h, sab);
    for (int mutation = 0; mutation < 4; ++mutation) {
        auto bad = clean;
        auto directory = get(bad, 24), first = directory + 48 + 2 * 12;
        if (mutation == 0)
            put(bad, 32, UINT32_MAX);
        if (mutation == 1)
            put(bad, first, bad.size() - 1, 8);
        if (mutation == 2) {
            // A second entry naming the same physical data overlaps the first.
            put(bad, first + 12, get(bad, first, 8), 8);
            put(bad, first + 20, get(bad, first + 8));
        }
        if (mutation == 3)
            bad.resize(directory + 50);
        auto rejected = attach(d, bad);
        check(rejected.objects().modeler_errors.count(h), "Malformed directory used scan fallback");
    }
}
static void wide(DxfVersion v, const std::string &root) {
    auto name = std::string(dxf_version_string(v));
    std::ifstream in(root + "/tests/data/dwg/review/" + name + "-wide.dwg", std::ios::binary);
    auto d = DwgDocument::read(in);
    constexpr std::uint64_t low = 0x10000000, high = 0x110000000;
    check(d.objects().modeler_errors.empty(), "64-bit AcDs owner lost");
    for (auto h : {low, high}) {
        std::ifstream expected(root + "/tests/data/dwg/review/wide-" + (h == low ? "box" : "sphere") + ".sab",
                               std::ios::binary);
        check(bool(expected), "Missing Rust SAB oracle");
        Bytes sab((std::istreambuf_iterator<char>(expected)), std::istreambuf_iterator<char>());
        check(d.modeler(h).data.geometry.sab == sab, "Wide owner body differs from Rust writer input");
        auto g = d.modeler_geometry(h);
        check(g.has_value(), "Missing wide fixture geometry");
        bool sphere = false;
        for (const auto &r : g->records)
            sphere |= r.entity_type == "sphere-surface";
        check(sphere == (h == high), "Distinct 64-bit owners aliased");
    }
    auto saved = parse(bytes(d));
    for (auto h : {low, high})
        check(saved.modeler(h).data.geometry.sab == d.modeler(h).data.geometry.sab,
              "Save changed wide-owner geometry");
    auto sab = acis::SabWriter::write(acis::build_box({}, 1, 2, 3));
    auto section = detail::insert_dwg_acds_body({}, low, sab);
    auto at = section.find("\xAC\xD5_data_");
    put(section, at + 56, high, 8);
    auto out = detail::decode_dwg_acds(section, {low, high});
    check(!out.count(low) && out.at(high) == sab, "Single wide owner assigned to low half");
}
static void duplicate(DxfVersion v) {
    auto shape = acis::build_box({}, 1, 2, 3);
    auto box = acis::SabWriter::write(shape);
    auto sphere = acis::SabWriter::write(acis::build_sphere({}, 7));
    auto d = parse(create_dwg(v, {}));
    auto h = d.add_modeler(ModelerKind::Solid3D, shape);
    auto neighbor = d.add_modeler(ModelerKind::Solid3D, acis::build_sphere({}, 7));
    auto section = detail::insert_dwg_acds_body({}, h, box);
    section = detail::insert_dwg_acds_body(section, h + 100, sphere);
    section = detail::insert_dwg_acds_body(section, neighbor, sphere);
    auto at = section.find("\xAC\xD5_data_");
    put(section, at + 76, h, 8);
    auto out = detail::decode_dwg_acds(section, {h, neighbor});
    check(!out.count(h) && out.at(neighbor) == sphere, "Duplicate owner silently overwrote geometry");
    auto broken = attach(d, section);
    check(broken.objects().modeler_errors.count(h) && broken.modeler(neighbor).data.geometry.sab == sphere,
          "Duplicate diagnostic affected neighbor");
    auto before = bytes(broken);
    bool refused = false;
    try {
        broken.copy_modeler(h);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused && bytes(broken) == before, "Ambiguous body mutation was not atomic");
    check(bytes(parse(before)) == before, "Duplicate owner opaque save changed");
    auto a = data_segment(detail::insert_dwg_acds_body({}, h, box));
    auto b = data_segment(detail::insert_dwg_acds_body({}, h, sphere));
    auto n = data_segment(detail::insert_dwg_acds_body({}, neighbor, sphere));
    auto page_parts = paged(h, sphere);
    page_parts.push_back(a);
    page_parts.push_back(n);
    for (bool indexed : {false, true}) {
        auto s = indexed ? catalog(page_parts) : legacy(page_parts);
        out = detail::decode_dwg_acds(s, {h, neighbor});
        check(!out.count(h) && out.at(neighbor) == sphere, "Paged/inline duplicate owner accepted");
    }
    for (bool indexed : {false, true}) {
        // A third matching body must not resurrect an already ambiguous owner.
        auto s = indexed ? catalog({a, b, a, n}) : a + b + a + n;
        out = detail::decode_dwg_acds(s, {h, neighbor});
        check(!out.count(h) && out.at(neighbor) == sphere, "Cross-segment conflict lost");
        s = indexed ? catalog({a, a, n}) : a + a + n;
        out = detail::decode_dwg_acds(s, {h, neighbor});
        check(out.at(h) == box && out.at(neighbor) == sphere, "Identical duplicate rejected");
    }
}
static void recovery(DxfVersion v) {
    auto shape = acis::build_box({}, 1, 2, 3);
    auto box = acis::SabWriter::write(shape);
    auto sphere = acis::SabWriter::write(acis::build_sphere({}, 7));
    auto d = parse(create_dwg(v, {}));
    auto h = d.add_modeler(ModelerKind::Solid3D, shape);
    auto n = d.add_modeler(ModelerKind::Solid3D, acis::build_sphere({}, 7));
    auto healthy = d.add_modeler(ModelerKind::Solid3D, shape);
    auto section = detail::insert_dwg_acds_body({}, h, box);
    section = detail::insert_dwg_acds_body(section, n, sphere);
    section = detail::insert_dwg_acds_body(section, healthy, box);
    auto seg = section.find("\xAC\xD5_data_");
    auto base = seg + get(section, seg + 36) * 16;
    auto second = base + get(section, seg + 84);
    for (auto length : {0u, std::uint32_t(box.size() - 1), UINT32_MAX}) {
        auto bad = section;
        put(bad, base, length);
        bad.replace(second + 4, 14, 14, '?');
        for (bool indexed : {false, true}) {
            auto s = indexed ? bad : data_segment(bad);
            auto out = detail::decode_dwg_acds(s, {h, n, healthy});
            check(!out.count(h) && !out.count(n) && out.at(healthy) == box,
                  "Rejected body leaked to another recovery owner");
            out = detail::decode_dwg_acds(s, {n, healthy});
            check(!out.count(n) && out.at(healthy) == box, "Unrequested corrupt owner leaked its body");
        }
        auto broken = attach(d, bad);
        check(broken.objects().modeler_errors.count(h) && broken.objects().modeler_errors.count(n),
              "Both corrupted owners need diagnostics");
        check(broken.modeler(healthy).data.geometry.sab == box, "Healthy neighbor changed");
        auto original = bytes(broken);
        for (auto id : {h, n}) {
            bool rejected = false;
            try {
                broken.set_modeler_geometry(id, shape);
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            check(rejected && bytes(broken) == original, "Damaged owner mutation was not atomic");
        }
        check(bytes(parse(original)) == original, "Opaque damaged drawing changed on save");
    }
    auto nested_shape = with_payload(detail::insert_dwg_acds_body({}, h, sphere));
    auto nested_sab = acis::SabWriter::write(nested_shape);
    auto nested = detail::insert_dwg_acds_body({}, h, nested_sab);
    auto nested_seg = nested.find("\xAC\xD5_data_");
    auto body = nested_seg + get(nested, nested_seg + 36) * 16;
    nested.replace(body + 4, 14, 14, '?');
    check(!detail::decode_dwg_acds(nested, {h}).count(h), "Damaged outer SAB adopted an inner body");
    std::string raw(nested_sab.begin(), nested_sab.end());
    // Keep the outer magic but corrupt its first typed header token.
    raw.at(31) = char(255);
    check(detail::decode_dwg_acds(raw, {h}).empty(), "Raw legacy fallback searched inside corrupt SAB");
    auto frames = paged(h, nested_sab);
    frames[1].replace(80, 14, 14, '?');
    check(!detail::decode_dwg_acds(catalog(frames), {h}).count(h), "Broken paged SAB adopted inner body");
    // A valid row must not resurrect a known-corrupt duplicate owner.
    auto a = data_segment(detail::insert_dwg_acds_body({}, h, box));
    auto bad = a;
    put(bad, get(bad, 36) * 16, 0);
    for (auto parts : {std::vector<std::string>{a, bad}, std::vector<std::string>{bad, a}})
        check(!detail::decode_dwg_acds(catalog(parts), {h}).count(h), "Corrupt duplicate owner accepted");
}
static void page_integrity(DxfVersion v) {
    auto shape = acis::build_box({}, 1, 2, 3);
    auto box = acis::SabWriter::write(shape);
    auto sphere = acis::SabWriter::write(acis::build_sphere({}, 7));
    auto d = parse(create_dwg(v, {}));
    auto h = d.add_modeler(ModelerKind::Solid3D, shape);
    auto neighbor = d.add_modeler(ModelerKind::Solid3D, acis::build_sphere({}, 7));
    auto original = paged(h, box);
    original.push_back(data_segment(detail::insert_dwg_acds_body({}, neighbor, sphere)));
    auto verify = [&](const std::vector<std::string> &parts, bool duplicate) {
        for (bool indexed : {false, true}) {
            auto section = indexed ? catalog(parts) : legacy(parts);
            if (duplicate && indexed) {
                // catalog assigns physical IDs; deliberately repeat the first page ID.
                const auto first = section.find("\xAC\xD5" "blob01");
                const auto last = section.rfind("\xAC\xD5" "blob01");
                put(section, last + 8, get(section, first + 8));
            }
            auto decoded = detail::decode_dwg_acds(section, {h, neighbor});
            check(!decoded.count(h), "Inconsistent or ambiguous AcDs page chain accepted");
            check(decoded.at(neighbor) == sphere, "Bad page chain changed adjacent model");
            if (indexed) {
                bool replacement_refused = false;
                try {
                    detail::replace_dwg_acds_body(section, h, box);
                } catch (const std::invalid_argument &) {
                    replacement_refused = true;
                }
                check(replacement_refused, "Replacement bypassed page-chain validation");
                auto damaged = attach(d, section);
                check(damaged.objects().modeler_errors.count(h), "Missing page-chain diagnostic");
                check(damaged.modeler(neighbor).data.geometry.sab == sphere, "Adjacent geometry lost");
                auto before = bytes(damaged);
                check(bytes(parse(before)) == before, "Opaque page-chain save changed");
                bool refused = false;
                try {
                    damaged.set_modeler_geometry(h, shape);
                } catch (const std::invalid_argument &) {
                    refused = true;
                }
                check(refused && bytes(damaged) == before, "Invalid page edit must reject atomically");
            }
        }
    };
    // Every framing field must agree with the reference and cumulative payload position.
    for (auto page : {1u, 2u}) {
        for (auto field : {48u, 56u, 64u, 68u, 72u}) {
            auto parts = original;
            unsigned width = field == 64 || field == 68 ? 4 : 8;
            put(parts[page], field, get(parts[page], field, width) + 1, width);
            verify(parts, false);
        }
    }
    for (bool invalid_first : {false, true})
        for (bool invalid_size : {false, true}) {
            auto parts = original;
            parts.push_back(parts[1]);
            // An invalid duplicate still makes the reference ID ambiguous.
            auto &bad = invalid_first ? parts[1] : parts.back();
            if (invalid_size)
                put(bad, 72, bad.size(), 8);
            else
                put(bad, 12, 0);
            verify(parts, true);
        }
    for (bool indexed : {false, true}) {
        auto good = indexed ? catalog(original) : legacy(original);
        auto decoded = detail::decode_dwg_acds(good, {h, neighbor});
        check(decoded.at(h) == box && decoded.at(neighbor) == sphere, "Valid paged geometry changed");
        if (indexed) {
            auto updated = detail::replace_dwg_acds_body(good, h, sphere);
            auto reloaded = detail::decode_dwg_acds(updated, {h, neighbor});
            check(reloaded.at(h) == sphere && reloaded.at(neighbor) == sphere,
                  "Valid page promotion changed adjacent model");
        }
    }
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    for (auto v : {DxfVersion::AC1027, DxfVersion::AC1032}) {
        std::string mode = argv[1];
        if (mode == "nested")
            nested(v);
        else if (mode == "wide")
            wide(v, argv[2]);
        else if (mode == "duplicate")
            duplicate(v);
        else if (mode == "recovery")
            recovery(v);
        else if (mode == "pages")
            page_integrity(v);
        else
            return 2;
    }
    std::cout << argv[1] << " AcDs integrity passed\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
