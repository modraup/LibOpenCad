// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include <libopencad/dxf_document.hpp>
#include "../src/dwg_modeler_internal.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace libopencad;
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static std::string hex(std::uint64_t h) {
    std::ostringstream out;
    out << std::hex << h;
    return out.str();
}
template <class Document> static Document reload(const Document &d) {
    std::ostringstream out;
    d.write(out);
    std::istringstream in(out.str());
    return Document::read(in);
}
static void sat() {
    for (auto version : {700u, 21800u}) {
        for (std::string value : {"  hi  ", "a\nb\r\nc", "a#point $-1 -1 0 0 0", "End-of-ACIS-data",
                                  "End-of-ASM-data", "@3 foo #", "中文", "", " # "}) {
            auto d = acis::SatDocument::new_body();
            d.header.version = acis::SatVersion::from_number(version);
            acis::SatRecord record;
            record.entity_type = "test-attrib";
            record.tokens.emplace_back(acis::SatString{value});
            d.add_record(record);
            d.add_record(record);
            auto parsed = acis::SatDocument::parse(d.to_sat_string());
            check(parsed.records.size() == d.records.size(), "SAT string changed record boundaries");
            for (std::size_t i = 1; i < parsed.records.size(); ++i)
                check(parsed.records[i].tokens.at(0).as_string() == value, "SAT counted string changed");
        }
    }
    for (auto value : {"@99999999999999999999999999 x", "@-1 x", "@20 x", "@3foo"}) {
        bool rejected = false;
        try {
            acis::SatDocument::parse(std::string("700 0 0 0\n1 x 1 y 1 z\n1 1e-6\n") + "test-attrib $-1 -1 " +
                                     value + " #");
        } catch (const acis::SatParseError &) {
            rejected = true;
        }
        check(rejected, "Malformed SAT string length accepted");
    }
}
static void acds() {
    for (auto version : {DxfVersion::AC1018, DxfVersion::AC1027, DxfVersion::AC1032}) {
        std::istringstream in(create_dwg(version, {}));
        auto d = DwgDocument::read(in);
        auto shape = acis::build_box({}, 1, 2, 3);
        acis::SatRecord record;
        record.entity_type = "test-attrib";
        for (std::string value : {"prefix End-of-ACIS-data suffix", "ACIS BinaryFile ASM BinaryFile",
                                  "\x0e\x03"
                                  "End\x0e\x02of\x0e\x04"
                                  "ACIS\x0d\x04"
                                  "data"})
            record.tokens.emplace_back(acis::SatString{value});
        // This is a string token, not an AcDs page header. All bytes must survive.
        std::vector<std::uint8_t> frame(84);
        frame[0] = 80;
        const std::string marker = "\xAC\xD5"
                                   "blob01";
        std::copy(marker.begin(), marker.end(), frame.begin() + 4);
        frame[16] = 1;
        frame[20] = 48;
        std::fill(frame.begin() + 44, frame.begin() + 52, 'U');
        record.tokens.emplace_back(acis::SabToken{9, frame});
        shape.add_record(record);
        auto h = d.add_modeler(ModelerKind::Solid3D, shape);
        auto neighbor = d.add_modeler(ModelerKind::Solid3D, acis::build_sphere({}, 2));
        auto expected = acis::SabWriter::write(shape);
        auto other = d.modeler(neighbor).data.geometry.sab;
        std::string legacy(expected.begin(), expected.end());
        legacy.append(other.begin(), other.end());
        auto recovered = detail::decode_dwg_acds(legacy, {100, 200});
        check(recovered.size() == 2 && recovered.at(100) == expected && recovered.at(200) == other,
              "SAB payload signature split legacy AcDs bodies");
        auto indexed = detail::insert_dwg_acds_body({}, 100, expected);
        indexed = detail::insert_dwg_acds_body(indexed, 200, other);
        const auto segment = indexed.find("\xAC\xD5_data_");
        auto word = [&](std::size_t offset) {
            std::uint32_t v = 0;
            for (unsigned i = 0; i < 4; ++i)
                v |= std::uint32_t(static_cast<unsigned char>(indexed.at(offset + i))) << (8 * i);
            return v;
        };
        auto body = segment + 16 * word(segment + 36);
        for (auto length : {std::uint32_t(expected.size() - 3), UINT32_MAX}) {
            auto damaged = indexed;
            for (unsigned i = 0; i < 4; ++i)
                damaged.at(body + i) = char(length >> (8 * i));
            auto isolated = detail::decode_dwg_acds(damaged, {100, 200});
            check(!isolated.count(100) && isolated.at(200) == other,
                  "Invalid AcDs length crossed an indexed body boundary");
        }
        auto copy = d.copy_modeler(h);
        d = reload(d);
        for (auto id : {h, copy}) {
            check(d.modeler(id).data.geometry.sab == expected, "AcDs truncated a counted SAB string");
            check(d.modeler_geometry(id)->records.size() == shape.records.size(),
                  "AcDs changed record count");
        }
        d.remove_modeler(h);
        d = reload(d);
        check(d.modeler(copy).data.geometry.sab == expected, "Delete damaged copied AcDs body");
        check(d.modeler(neighbor).data.geometry.sab == other, "AcDs changed neighboring body");
    }
}
static void loft(const std::string &root) {
    for (std::string version : {"AC1021", "AC1024", "AC1027", "AC1032"}) {
        std::ifstream in(root + "/tests/data/dwg/history/" + version + ".dwg.metadata.dwg", std::ios::binary);
        auto d = DwgDocument::read(in);
        std::uint64_t h = 0;
        for (const auto &[id, r] : d.objects().history_records)
            if (r.operation && r.operation->base.kind == SolidHistoryKind::Loft && r.extension_dictionary)
                h = id;
        check(h != 0, "Missing loft fixture");
        auto settings = d.history_loft_settings(h);
        SolidHistoryOperation op(SolidHistoryKind::Loft);
        HistoryProfileGeometry profiles;
        ProfileLine line;
        line.end = {1, 2, 3};
        profiles.metadata_path = line;
        set_history_profiles(op, profiles, DxfVersion::AC1032);
        settings.path = std::get<HistoryLoft>(op.parameters).settings->path;
        d.set_history_loft_settings(h, settings);
        check(d.history_record(h).profiles.path.has_value(), "Loft path was not installed");
        auto before = d.history_record(h).profiles;
        settings.path.clear();
        d.set_history_loft_settings(h, settings);
        d = reload(d);
        check(d.history_loft_settings(h).path.empty(), "Loft metadata path was not cleared");
        auto after = d.history_record(h).profiles;
        check(!after.path, "Loft native path was not cleared");
        check(before.sections.size() == after.sections.size() && before.guides.size() == after.guides.size(),
              "Clearing loft path changed other profiles");
        auto same = [&](const auto &a, const auto &b) {
            for (std::size_t i = 0; i < a.size(); ++i) {
                auto x = encode_embedded_profile(a[i], dxf_version_from_string(version));
                auto y = encode_embedded_profile(b[i], dxf_version_from_string(version));
                check(x.type_code == y.type_code && x.bit_count == y.bit_count && x.bytes == y.bytes,
                      "Clearing loft path changed section or guide geometry");
            }
        };
        same(before.sections, after.sections);
        same(before.guides, after.guides);
    }
}
static void expand() {
    for (bool binary : {false, true}) {
        for (auto normal : {Vector3::UNIT_X, Vector3::UNIT_Y, Vector3::UNIT_Z}) {
            for (double mirror : {1., -1.}) {
                auto d = DxfDocument::create(binary);
                d.add_symbol(SymbolTable::BlockRecord, "B");
                EntityOptions options;
                options.block_name = "B";
                auto h = d.add_circle({1, 2, 3}, 1, options);
                d.for_each<DxfCircle>([&](auto &c) {
                    if (c.handle() == hex(h)) {
                        c.set_normal(normal);
                        c.set_thickness(2);
                    }
                });
                InsertDefinition insert;
                insert.block_name = "B";
                insert.insertion = {7, 8, 9};
                insert.scale = {mirror * (3 - normal.x), 3 - normal.y, 3 - normal.z};
                auto id = d.add_insert(insert);
                auto copies = d.expand_insert_into(hex(id), {});
                check(copies.size() == 1, "Wrong expansion count");
                d = reload(d);
                std::size_t count = 0;
                d.for_each<DxfCircle>([&](auto &c) {
                    ++count;
                    if (c.handle() == hex(copies[0])) {
                        check(std::abs(c.radius() - 3) < 1e-12,
                              "Expanded circle used scale outside its plane");
                        check(std::abs(c.thickness() - 4) < 1e-12,
                              "Expanded thickness lost normal-axis scale");
                        auto world = Matrix3::arbitrary_axis(c.normal()) * c.center();
                        auto expected = Matrix3::arbitrary_axis(normal) * Vector3{1, 2, 3};
                        expected = Vector3{expected.x * insert.scale.x, expected.y * insert.scale.y,
                                           expected.z * insert.scale.z} +
                                   insert.insertion;
                        check((world - expected).length() < 1e-12, "Expanded OCS center changed");
                        check(std::abs(std::abs(c.normal().dot(normal)) - 1) < 1e-12,
                              "Expanded circle plane changed");
                    }
                    if (c.handle() == hex(h))
                        check(c.radius() == 1 && c.thickness() == 2 && c.normal() == normal,
                              "Expansion modified source circle");
                });
                check(count == 2, "Expanded circle or source missing after save");
            }
        }
    }
}

int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::string test = argv[1];
    if (test == "sat")
        sat();
    else if (test == "acds")
        acds();
    else if (test == "loft")
        loft(argv[2]);
    else if (test == "expand")
        expand();
    else
        return 2;
    std::cout << test << " integrity passed\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
