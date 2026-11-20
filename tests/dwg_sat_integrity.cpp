// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include <libopencad/embedded_profile.hpp>
#include "../src/dwg_sat_internal.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace libopencad;
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
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
template <class F> static void rejects(F f) {
    bool refused = false;
    try {
        f();
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "Expected explicit refusal of lossy SAT input");
}
static acis::SatDocument shape(const std::string &value, unsigned version = 21800) {
    auto d = acis::build_box({}, 1, 2, 3);
    d.header.version = acis::SatVersion::from_number(version);
    acis::SatRecord label;
    label.entity_type = "string-attrib";
    label.tokens.emplace_back(acis::SatString{value});
    d.add_record(label);
    acis::SatRecord after;
    after.entity_type = "point";
    after.tokens.emplace_back(Vector3{8, 9, 10});
    d.add_record(after);
    return d;
}
static void verify(const acis::SatDocument &d, const std::string &label) {
    check(d.records.size() == 87 && d.records.back().entity_type == "point", "SAT records truncated");
    auto &r = d.records[d.records.size() - 2];
    check(r.entity_type == "string-attrib" && r.tokens.size() == 1 && r.tokens[0].as_string() == label,
          "SAT string bytes changed");
}
static std::vector<std::string> labels() {
    return {"left\nEnd-of-inside\nright",
            "a\nEnd-of-ACIS-data\n# @9 fake\r\nb",
            "a\nEnd-of-ASM-data\nb",
            "",
            "  trailing  ",
            "a\rb\r\nc\nd",
            "@5 abcde #",
            std::string(4081, 'x') + "\nEnd-of-ACIS-data\n" + std::string(4100, 'y')};
}
static void framing(const std::string &prefix) {
    for (auto v : {DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015}) {
        auto d = parse(create_dwg(v, {}));
        auto h = d.add_modeler(ModelerKind::Solid3D, acis::build_box({}, 1, 2, 3));
        auto neighbor = d.add_modeler(ModelerKind::Solid3D, acis::build_sphere({}, 7));
        auto neighbor_before = d.modeler(neighbor).data.geometry.sat;
        for (auto label : labels()) {
            auto input = shape(label);
            verify(acis::SatDocument::parse(input.to_sat_string()), label);
            d.set_modeler_geometry(h, input);
            verify(*d.modeler_geometry(h), label);
            d = parse(bytes(d));
            verify(*d.modeler_geometry(h), label);
            check(d.modeler(neighbor).data.geometry.sat == neighbor_before, "SAT edit changed neighbor");
        }
        auto copy = d.copy_modeler(h);
        d.remove_modeler(h);
        verify(*parse(bytes(d)).modeler_geometry(copy), labels().back());
        d.set_modeler_geometry(copy, shape("review ASCII label"));
        std::ofstream ids(prefix + "-" + std::string(dxf_version_string(v)) + ".ids");
        ids << copy << ' ' << neighbor;
        std::ofstream out(prefix + "-" + std::string(dxf_version_string(v)) + ".dwg", std::ios::binary);
        d.write(out);
    }
    for (auto malformed : {"@999 x", "@18446744073709551616 x", "@3x", "@-1 x"})
        rejects(
            [&] { detail::dwg_sat_transport(std::string("21800 0 0 0\npoint $-1 -1 ") + malformed, true); });
    // Each printable ASCII byte and allowed whitespace must survive the cipher twice.
    std::string ascii = "\t\r\n";
    for (int c = 32; c <= 126; ++c)
        ascii += char(c);
    check(detail::dwg_sat_cipher(detail::dwg_sat_cipher(ascii)) == ascii, "SAT cipher is not symmetric");
}
static void encoding(const std::string &prefix) {
    for (auto v : {DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015}) {
        auto d = parse(create_dwg(v, {}));
        auto h = d.add_modeler(ModelerKind::Solid3D, acis::build_box({}, 1, 2, 3));
        auto original = bytes(d);
        for (std::string label : std::vector<std::string>{"中文名称", std::string(1, '\x7f'),
                                                          std::string(1, '\xff'), std::string(1, '\0')}) {
            rejects([&] { d.set_modeler_geometry(h, shape(label)); });
            rejects([&] { d.add_modeler(ModelerKind::Solid3D, shape(label)); });
            check(bytes(d) == original, "Failed SAT mutation changed drawing/handle seed");
        }
        // The automatic SAT 700 -> 400 downgrade must not erase unknown string fields.
        rejects([&] { d.set_modeler_geometry(h, shape("left\nEnd-of-inside\nright", 700)); });
        auto header = acis::build_box({}, 1, 2, 3);
        header.header.product_id = "a\nb";
        rejects([&] { d.set_modeler_geometry(h, header); });
        check(bytes(d) == original, "Rejected downgrade/header mutation changed drawing");
    }
    for (auto v : {DxfVersion::AC1018, DxfVersion::AC1021, DxfVersion::AC1024, DxfVersion::AC1027,
                   DxfVersion::AC1032}) {
        auto d = parse(create_dwg(v, {}));
        auto input = shape("中文名称\nEnd-of-ACIS-data\n结尾");
        auto h = d.add_modeler(ModelerKind::Solid3D, input);
        verify(*parse(bytes(d)).modeler_geometry(h), "中文名称\nEnd-of-ACIS-data\n结尾");
        d.set_modeler_geometry(h, shape("中文名称"));
        std::ofstream ids(prefix + "-" + std::string(dxf_version_string(v)) + ".ids");
        ids << h;
        std::ofstream out(prefix + "-" + std::string(dxf_version_string(v)) + ".dwg", std::ios::binary);
        d.write(out);
    }
    {
        auto d = parse(create_dwg(DxfVersion::AC1015, {}));
        auto h = d.add_modeler(ModelerKind::Solid3D, acis::build_box({}, 1, 2, 3));
        auto source = bytes(d);
        auto r = d.objects().records.at(h);
        DwgBitReader bits(std::string_view(source).substr(r.body_offset, r.body_size));
        bits.seek(d.modeler(h).payload_begin_bit);
        check(bits.bit_long() > 0, "Missing SAT block");
        auto at = r.body_offset * 8 + bits.position();
        for (unsigned i = 0; i < 8; ++i)
            source[(at + i) / 8] =
                char(static_cast<unsigned char>(source[(at + i) / 8]) | (1u << (7 - (at + i) % 8)));
        auto end = r.body_offset + r.body_size;
        std::uint16_t crc = 0xc0c1;
        for (std::size_t i = r.offset; i < end; ++i) {
            crc ^= static_cast<unsigned char>(source[i]);
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
        }
        source[end] = char(crc);
        source[end + 1] = char(crc >> 8);
        auto bad = parse(source);
        check(bad.objects().modeler_errors.count(h), "Non-ASCII SAT read lacks diagnostic");
        check(bytes(bad) == source, "Unsupported SAT opaque save changed");
    }
    for (int c = 0; c < 256; ++c)
        if (c < 32 ? c != 9 && c != 10 && c != 13 : c > 126)
            rejects([&] { detail::dwg_sat_cipher(std::string(1, char(c))); });
}
static void embedded() {
    for (auto v : {DxfVersion::AC1021, DxfVersion::AC1024, DxfVersion::AC1027, DxfVersion::AC1032}) {
        for (auto label : labels()) {
            ProfileRegion region;
            region.geometry.sat = shape(label).to_sat_string();
            region.extra = ProfileAcis{};
            region.extra->sat = shape(label).to_sat_string();
            auto encoded = encode_embedded_profile(region, v);
            auto decoded = decode_embedded_profile(encoded, v);
            check(decoded && std::holds_alternative<ProfileRegion>(*decoded), "REGION became opaque");
            auto &result = std::get<ProfileRegion>(*decoded);
            auto primary = result.geometry.is_binary ? acis::SabReader::read(result.geometry.sab)
                                                     : acis::SatDocument::parse(result.geometry.sat);
            verify(primary, label);
            check(result.extra.has_value(), "Extra SAT lost");
            verify(acis::SatDocument::parse(result.extra->sat), label);
        }
        ProfileRegion region;
        region.geometry.sat = acis::build_box({}, 1, 2, 3).to_sat_string();
        region.extra = ProfileAcis{};
        region.extra->sat = shape("中文名称").to_sat_string();
        rejects([&] { encode_embedded_profile(region, v); });
    }
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::string mode = argv[1];
    if (mode == "framing")
        framing(argv[2]);
    else if (mode == "encoding")
        encoding(argv[2]);
    else if (mode == "embedded")
        embedded();
    else
        return 2;
    std::cout << mode << " DWG SAT integrity passed\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
