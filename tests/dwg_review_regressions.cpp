// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include <libopencad/dwg_sections.hpp>
#include "../src/dwg_metadata_internal.hpp"
#include "../src/dwg_modeler_internal.hpp"
#include "../src/dwg_sections_internal.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace libopencad;
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
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
static DwgDocument read(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return DwgDocument::read(in);
}
static void save(const DwgDocument &d, const std::string &path) {
    std::ofstream out(path, std::ios::binary);
    d.write(out);
}
template <class F> static void rejects(DwgDocument &d, F f) {
    auto before = bytes(d);
    bool failed = false;
    try {
        f();
    } catch (const std::invalid_argument &) {
        failed = true;
    }
    check(failed && bytes(d) == before, "Invalid mutation must reject atomically");
}
static void acds(const std::string &prefix) {
    auto shape = acis::build_box({}, 1, 2, 3);
    auto sab = acis::SabWriter::write(shape);
    // A single indexed row may refer to multiple external pages as well.
    std::string paged(120, 0);
    auto put = [&](std::size_t at, std::uint64_t n, unsigned width = 4) {
        for (unsigned i = 0; i < width; ++i)
            paged.at(at + i) = char(n >> (8 * i));
    };
    paged.replace(0, 8, "\xAC\xD5_data_");
    put(16, paged.size(), 8);
    put(48, 20);
    put(56, 100);
    put(72, sab.size(), 8);
    put(80, 2);
    put(84, 48);
    put(104, 7);
    put(108, sab.size() / 2);
    put(112, 8);
    put(116, sab.size() - sab.size() / 2);
    for (unsigned i = 0; i < 2; ++i) {
        auto at = paged.size(), from = i ? sab.size() / 2 : 0, n = i ? sab.size() - from : sab.size() / 2;
        paged.resize(at + 80);
        paged.replace(at, 8,
                      "\xAC\xD5"
                      "blob01");
        put(at + 8, 7 + i);
        put(at + 12, 1);
        put(at + 16, 48, 8);
        put(at + 48, sab.size(), 8);
        put(at + 56, from, 8);
        put(at + 64, i);
        put(at + 68, 2);
        put(at + 72, n, 8);
        paged.append(sab.begin() + from, sab.begin() + from + n);
    }
    check(detail::decode_dwg_acds(paged, {100}).at(100) == sab, "Single-row paged AcDs lost its owner");
    check(!detail::decode_dwg_acds(paged, {200}).count(200), "Single-row paged AcDs assigned to wrong owner");
    put(116, sab.size());
    check(!detail::decode_dwg_acds(paged, {100}).count(100), "Corrupt single-row page chain accepted");
    for (auto version : {DxfVersion::AC1027, DxfVersion::AC1032}) {
        auto name = std::string(dxf_version_string(version));
        auto d = parse(create_dwg(version, {}));
        auto h = d.add_modeler(ModelerKind::Solid3D, shape);
        auto original = bytes(d);
        auto section = detail::insert_dwg_acds_body({}, h, sab);
        auto one = detail::decode_dwg_acds(section, {h});
        check(one.count(h) && one.at(h) == sab, "Single indexed AcDs body lost");
        auto wrong = detail::insert_dwg_acds_body({}, 1000, sab);
        auto decoded = detail::decode_dwg_acds(wrong, {h});
        check(!decoded.count(h), "Single AcDs row assigned to a different owner");
        auto damaged = parse(detail::replace_ac18_sections(original, {{"AcDb:AcDsPrototype_1b", wrong}}));
        check(damaged.objects().modeler_errors.count(h), "Missing explicit AcDs owner needs diagnostic");
        auto raw = bytes(damaged);
        check(bytes(parse(raw)) == raw, "Corrupt body must retain opaque save bytes");
        rejects(damaged, [&] { damaged.copy_modeler(h); });
        auto segment = section.find("\xAC\xD5_data_");
        auto units = detail::metadata_uint(section, segment + 36, 4);
        auto at = segment + 16 * units;
        for (auto length : {0u, std::uint32_t(sab.size() - 1), UINT32_MAX}) {
            auto corrupt = section;
            for (unsigned i = 0; i < 4; ++i)
                corrupt.at(at + i) = char(length >> (8 * i));
            check(!detail::decode_dwg_acds(corrupt, {h}).count(h), "Single AcDs length ignored");
            auto bad = parse(detail::replace_ac18_sections(original, {{"AcDb:AcDsPrototype_1b", corrupt}}));
            check(bad.objects().modeler_errors.count(h), "Invalid single-body length needs diagnostic");
            rejects(bad, [&] { bad.set_modeler_geometry(h, shape); });
        }
        // The no-table legacy fallback remains available, but only without an explicit table.
        std::string unindexed(sab.begin(), sab.end());
        check(detail::decode_dwg_acds(unindexed, {h}).at(h) == sab, "Legacy no-table fallback lost");
        check(detail::decode_dwg_acds(unindexed, {h, h + 1}).empty(), "Ambiguous legacy fallback accepted");
        auto copy = d.copy_modeler(h);
        d.remove_modeler(h);
        d = parse(bytes(d));
        check(d.modeler(copy).data.geometry.sab == sab, "Single remaining body lost after deletion");
        save(d, prefix + "-" + name + ".dwg");
        std::ofstream ids(prefix + "-" + name + ".ids");
        ids << copy;
    }
}
static void legacy(const std::string &root, const std::string &prefix) {
    for (std::string version : {"AC1018", "AC1021", "AC1032"}) {
        auto d = read(root + "/tests/data/dwg/review/" + version + ".dwg");
        std::uint64_t h = 0;
        for (const auto &[id, m] : d.objects().modelers)
            if (m.type == 38 && m.properties.extension_dictionary) {
                h = id;
                break;
            }
        check(h != 0, "Missing string XRECORD fixture");
        auto xr = detail::metadata_record(d.objects(), h, d.modeler(h).properties.extension_dictionary,
                                          "REVIEW_STRING");
        check(xr != nullptr, "Missing fixture metadata");
        auto raw = xr->metadata;
        auto xr_id = xr->handle;
        auto unicode = version != "AC1018";
        auto fields = detail::metadata_fields(raw, unicode);
        std::vector<int> codes;
        for (auto f : fields)
            codes.push_back(f.code);
        check(codes == std::vector<int>({1, 3, 5, 105, 320, 329, 70, 330}),
              "XRECORD fields shifted after string");
        auto truncated = raw.substr(0, fields[0].offset + fields[0].size - 1);
        bool failed = false;
        try {
            (void)detail::metadata_fields(truncated, unicode);
        } catch (const std::invalid_argument &) {
            failed = true;
        }
        check(failed, "Truncated XRECORD string accepted");
        std::map<std::uint64_t, std::uint64_t> remapping;
        auto copy = d.copy_modeler(h, 0, &remapping);
        auto cloned = d.objects().metadata_records.at(remapping.at(xr_id));
        check(cloned.reference_values.at(0) == copy, "Copied XRECORD object ID not remapped");
        auto unrelated = d.add_modeler(ModelerKind::Solid3D, acis::build_sphere({}, 2));
        d.remove_modeler(unrelated);
        check(d.objects().metadata_records.at(xr_id).metadata == raw,
              "Copy/delete changed original string data");
        d = parse(bytes(d));
        check(d.objects().metadata_records.at(xr_id).metadata == raw, "Reload changed original XRECORD");
        save(d, prefix + "-" + version + ".dwg");
        std::ofstream ids(prefix + "-" + version + ".ids");
        ids << h << ' ' << copy;
    }
}
static void references(const std::string &root, const std::string &prefix) {
    for (std::string version : {"AC1021", "AC1024", "AC1027", "AC1032"}) {
        auto d = read(root + "/tests/data/dwg/history/" + version + ".dwg.metadata.dwg");
        auto fresh = d.add_modeler(ModelerKind::Solid3D, acis::build_box({}, 1, 2, 3));
        std::uint64_t surface = 0, record = 0, target = 0;
        for (const auto &[h, m] : d.objects().modelers)
            if (m.surface && m.properties.extension_dictionary) {
                auto x = detail::metadata_record(d.objects(), h, m.properties.extension_dictionary,
                                                 "CADCODEC_SURFACE_HISTORY_V1");
                if (x) {
                    surface = h;
                    record = x->handle;
                    target = detail::metadata_reference(*x, 330);
                    break;
                }
            }
        check(surface && record && target && target != fresh, "Missing reference fixture");
        auto original = d.objects().metadata_records.at(record);
        auto raw = original.metadata;
        detail::metadata_put(raw, 330, fresh, 8);
        auto stale = parse(detail::edit_dwg_metadata(bytes(d), d.objects(), record, raw));
        check(stale.modeler_history_graph(surface)->root == target, "Resolved reference lost precedence");
        save(stale, prefix + "-" + version + ".before.dwg");
        stale.remove_modeler(fresh);
        stale = parse(bytes(stale));
        check(!stale.objects().records.count(fresh), "Unreferenced modeler was not deleted");
        check(stale.modeler_history_graph(surface)->root == target, "Delete changed surviving history");
        check(stale.objects().metadata_records.at(record).metadata == raw &&
                  stale.objects().metadata_records.at(record).reference_values == original.reference_values,
              "Delete changed raw or effective XRECORD data");
        save(stale, prefix + "-" + version + ".after.dwg");
        // Reverse mismatch: a real stream reference must still prevent deletion.
        auto slot = original.reference_slots.at(0);
        auto live = parse(detail::edit_dwg_history_slot(bytes(d), d.objects(), record, slot.first,
                                                        slot.second, fresh, true, 4));
        rejects(live, [&] { live.remove_modeler(fresh); });
        // Only 330-369 consume stream slots. An extra repeated code without a slot uses raw data.
        auto fallback = original.metadata;
        detail::metadata_append(fallback, 390, target, 8);
        detail::metadata_append(fallback, 330, fresh, 8);
        auto no_slot = parse(detail::edit_dwg_metadata(bytes(d), d.objects(), record, fallback));
        rejects(no_slot, [&] { no_slot.remove_modeler(fresh); });
        // A raw reference outside 330-369 is authoritative even with a stream slot present.
        fallback = original.metadata;
        detail::metadata_append(fallback, 390, fresh, 8);
        auto raw_pointer = parse(detail::edit_dwg_metadata(bytes(d), d.objects(), record, fallback));
        rejects(raw_pointer, [&] { raw_pointer.remove_modeler(fresh); });
        std::ofstream ids(prefix + "-" + version + ".ids");
        ids << surface << ' ' << target << ' ' << fresh;
    }
}
int main(int argc, char **argv) try {
    if (argc != 4)
        return 2;
    std::string mode = argv[1];
    if (mode == "acds")
        acds(argv[3]);
    else if (mode == "legacy")
        legacy(argv[2], argv[3]);
    else if (mode == "references")
        references(argv[2], argv[3]);
    else
        return 2;
    std::cout << mode << " DWG review regressions passed\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
