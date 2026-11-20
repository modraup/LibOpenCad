// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include "../src/dwg_modeler_internal.hpp"
#include "../src/dwg_sections_internal.hpp"
#include <libopencad/dwg_sections.hpp>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <cstring>
using namespace libopencad;
static void check(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
static void number(std::ostream &o, double d) {
    std::uint64_t bits;
    std::memcpy(&bits, &d, 8);
    o << ' ' << bits;
}
static void point(std::ostream &o, Vector3 p) {
    number(o, p.x);
    number(o, p.y);
    number(o, p.z);
}
static void sat(const ProfileAcis &p, const std::string &file) {
    if (p.sab.empty() && p.sat.empty()) {
        std::filesystem::remove(file);
        return;
    }
    auto d = p.is_binary ? acis::SabReader::read(p.sab) : acis::SatDocument::parse(p.sat);
    std::ofstream o(file);
    o << d.to_sat_string();
}
static void wire(std::ostream &o, const ProfileWire &w) {
    o << ' ' << unsigned(w.type) << ' ' << w.selection_marker << ' ' << w.color << ' ' << w.acis_index << ' '
      << w.has_transform << ' ' << w.rotation << ' ' << w.reflection << ' ' << w.shear;
    for (auto p : w.points)
        point(o, p);
    for (auto p : {w.x_axis, w.y_axis, w.z_axis, w.translation, Vector3{w.scale, w.scale, w.scale}})
        point(o, p);
}
static void acds_tests() {
    using namespace libopencad::detail;
    auto a = acis::SabWriter::write(acis::build_box({1, 2, 3}, 4, 5, 6));
    auto b = acis::SabWriter::write(acis::build_sphere({7, 8, 9}, 3));
    auto str = [](const auto &v) { return std::string(v.begin(), v.end()); };
    std::string section(88, 0);
    section.replace(0, 8, "\xAC\xD5_data_");
    auto put = [&](std::size_t p, std::uint64_t v, int n = 4) {
        for (int i = 0; i < n; ++i)
            section[p + i] = char(v >> (8 * i));
    };
    put(48, 0x14);
    put(56, 200);
    put(64, 0);
    put(68, 0x14);
    put(76, 100);
    put(84, a.size());
    section += str(a) + str(b);
    put(16, section.size(), 8);
    auto out = decode_dwg_acds(section, {100, 200});
    check(out.at(200) == a && out.at(100) == b, "AcDs must use explicit owners, not handle order");
    put(84, 0xFFFFFFFFu);
    out = decode_dwg_acds(section, {100, 200});
    check(out.at(100) == b, "Orphan AcDs record must use only the unclaimed body");
    check(decode_dwg_acds(str(a) + str(b), {100}).empty(), "Ambiguous legacy blob count");
    for (std::size_t n = 0; n < 88; ++n)
        (void)decode_dwg_acds(std::string_view(section).substr(0, n), {100, 200});
}
static void malformed(const DwgDocument &doc) {
    std::ostringstream encoded;
    doc.write(encoded);
    auto bytes = encoded.str();
    if (bytes.substr(0, 6) != "AC1015")
        return;
    auto [h, m] = *doc.objects().modelers.begin();
    auto r = doc.objects().records.at(h);
    auto bit = r.body_offset * 8 + m.payload_begin_bit - 8; // BS tag 01 followed by the version byte.
    for (unsigned i = 0; i < 8; ++i) {
        auto p = bit + i;
        auto mask = unsigned(1) << (7 - p % 8);
        auto c = static_cast<unsigned char>(bytes[p / 8]);
        bytes[p / 8] = char((c & ~mask) | (((3u >> (7 - i)) & 1) ? mask : 0));
    }
    auto end = r.body_offset + r.body_size;
    std::uint16_t crc = 0xc0c1;
    for (std::size_t i = r.offset; i < end; ++i) {
        crc ^= static_cast<unsigned char>(bytes[i]);
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    bytes[end] = char(crc);
    bytes[end + 1] = char(crc >> 8);
    std::istringstream source(bytes);
    auto damaged = DwgDocument::read(source);
    check(damaged.objects().modeler_errors.size() == 1 && damaged.objects().modeler_errors.count(h),
          "Malformed modeler must be isolated to its own handle");
    check(damaged.objects().modelers.size() == 8, "Neighbor modelers survive a malformed body");
    bool rejected = false;
    try {
        (void)damaged.modeler(h);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "Typed access must expose parse errors");
    std::ostringstream saved;
    damaged.write(saved);
    check(saved.str() == bytes, "Opaque preservation after typed decode failure");
}
static void paged_acds() {
    using namespace libopencad::detail;
    auto a = acis::SabWriter::write(acis::build_box({1, 2, 3}, 4, 5, 6));
    auto b = acis::SabWriter::write(acis::build_sphere({7, 8, 9}, 3));
    std::string s(88, 0);
    s.replace(0, 8, "\xAC\xD5_data_");
    auto put = [&](std::size_t p, std::uint64_t n, int len = 4) {
        for (int i = 0; i < len; ++i)
            s.at(p + i) = char(n >> (8 * i));
    };
    put(48, 20);
    put(56, 200);
    put(68, 20);
    put(76, 100);
    put(84, a.size());
    s.append(a.begin(), a.end());
    auto ref = s.size();
    s.resize(ref + 52);
    put(ref + 4, b.size(), 8);
    put(ref + 12, 2);
    put(ref + 16, 48);
    put(ref + 36, 7);
    put(ref + 40, b.size() / 2);
    put(ref + 44, 8);
    put(ref + 48, b.size() - b.size() / 2);
    put(16, s.size(), 8);
    for (int i = 0; i < 2; ++i) {
        auto start = s.size(), from = i ? b.size() / 2 : 0, len = i ? b.size() - b.size() / 2 : b.size() / 2;
        s.resize(start + 80);
        s.replace(start, 8,
                  "\xAC\xD5"
                  "blob01");
        put(start + 8, 7 + i);
        put(start + 12, 1);
        put(start + 16, 48, 8);
        put(start + 48, b.size(), 8);
        put(start + 56, from, 8);
        put(start + 64, i);
        put(start + 68, 2);
        put(start + 72, len, 8);
        s.append(b.begin() + from, b.begin() + from + len);
    }
    auto result = decode_dwg_acds(s, {100, 200});
    check(result.at(100) == b && result.at(200) == a,
          "Two-page blob references retain explicit owner association");
    put(ref + 48, b.size());
    result = decode_dwg_acds(s, {100, 200});
    check(!result.count(100), "Mismatched AcDs page sizes must reject the paged body");
}
// Construct indexed external-page storage from the upstream contiguous fixture.
static void paged_drawing(const DwgDocument &doc, const std::string &prefix) {
    std::ostringstream encoded;
    doc.write(encoded);
    auto file = encoded.str();
    if (file.substr(0, 6) < "AC1027")
        return;
    using namespace libopencad::detail;
    auto data = read_dwg_paged_section(file, "AcDb:AcDsPrototype_1b");
    auto get = [](const std::string &s, std::size_t p, int n = 4) {
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i)
            v |= std::uint64_t(static_cast<unsigned char>(s.at(p + i))) << (8 * i);
        return v;
    };
    auto put = [](std::string &s, std::size_t p, std::uint64_t v, int n = 4) {
        for (int i = 0; i < n; ++i)
            s.at(p + i) = char(v >> (8 * i));
    };
    auto index = get(data, 24), count = get(data, 32);
    std::vector<std::string> parts(count + 2);
    for (std::size_t i = 1; i < count; ++i) {
        auto row = index + 48 + i * 12;
        parts[i] = data.substr(get(data, row), get(data, row + 8));
    }
    auto h = doc.objects().modelers.begin()->first;
    auto &table = parts[2];
    auto base = get(table, 36) * 16;
    std::vector<std::size_t> rows;
    for (std::size_t row = 48; row + 20 <= base && get(table, row) == 20; row += 20)
        rows.push_back(row);
    auto row = *std::find_if(rows.begin(), rows.end(), [&](auto p) { return get(table, p + 8, 8) == h; });
    auto at = base + get(table, row + 16), old_size = 4 + get(table, at);
    auto body = table.substr(at + 4, old_size - 4);
    auto half = body.size() / 2;
    std::string ref(52, 0);
    put(ref, 4, body.size(), 8);
    put(ref, 12, 2);
    put(ref, 16, 48);
    put(ref, 36, count);
    put(ref, 40, half);
    put(ref, 44, count + 1);
    put(ref, 48, body.size() - half);
    table.replace(at, old_size, ref);
    for (auto p : rows)
        if (p > row)
            put(table, p + 16, get(table, p + 16) - old_size + ref.size());
    table.resize((table.size() + 15) / 16 * 16, char(0x70));
    put(table, 16, table.size(), 8);
    for (std::size_t i = 0; i < 2; ++i) {
        auto from = i ? half : 0, len = i ? body.size() - half : half;
        std::string page((80 + len + 15) / 16 * 16, 0);
        page.replace(0, 8,
                     "\xAC\xD5"
                     "blob01");
        put(page, 8, count + i);
        put(page, 12, 1);
        put(page, 16, page.size(), 8);
        put(page, 24, 1);
        page.replace(40, 8, "UUUUUUUU");
        put(page, 48, body.size(), 8);
        put(page, 56, from, 8);
        put(page, 64, i);
        put(page, 68, 2);
        put(page, 72, len, 8);
        page.replace(80, len, body.substr(from, len));
        parts[count + i] = std::move(page);
    }
    parts[1].resize((48 + (count + 2) * 12 + 15) / 16 * 16, 0);
    put(parts[1], 16, parts[1].size(), 8);
    std::string result = data.substr(0, 128);
    std::vector<std::size_t> offsets(count + 2);
    for (std::size_t i = 2; i < count + 2; ++i) {
        offsets[i] = result.size();
        result += parts[i];
    }
    offsets[1] = result.size();
    for (std::size_t i = 1; i < count + 2; ++i) {
        put(parts[1], 48 + i * 12, offsets[i]);
        put(parts[1], 52 + i * 12, 0);
        put(parts[1], 56 + i * 12, parts[i].size());
    }
    result += parts[1];
    put(result, 24, offsets[1]);
    put(result, 32, count + 2);
    put(result, 52, result.size());
    auto paged_file = replace_ac18_sections(file, {{"AcDb:AcDsPrototype_1b", result}});
    std::istringstream in(paged_file);
    auto paged = DwgDocument::read(in);
    check(paged.modeler(h).data.geometry.sab == doc.modeler(h).data.geometry.sab,
          "Indexed paged drawing reads original geometry");
    {
        std::ofstream o(prefix + ".paged.dwg", std::ios::binary);
        paged.write(o);
    }
    paged.set_modeler_geometry(h, acis::build_torus({9, -4, 7}, 8, 2));
    {
        std::ofstream o(prefix + ".paged-edited.dwg", std::ios::binary);
        paged.write(o);
    }
    std::ostringstream out;
    paged.write(out);
    auto after = read_dwg_paged_section(out.str(), "AcDb:AcDsPrototype_1b");
    auto new_index = get(after, 24);
    for (std::size_t i = count; i < count + 2; ++i) {
        auto p = new_index + 48 + i * 12;
        check(after.substr(get(after, p), get(after, p + 8)) == parts[i],
              "Old external pages must remain verbatim");
    }
}
int main(int argc, char **argv) {
    try {
        acds_tests();
        paged_acds();
        if (argc != 3)
            throw std::runtime_error("input.dwg prefix");
        std::ifstream in(argv[1], std::ios::binary);
        auto d = DwgDocument::read(in);
        malformed(d);
        std::string prefix = argv[2];
        paged_drawing(d, prefix);
        for (auto &[h, e] : d.objects().modeler_errors)
            std::cerr << h << ": " << e << '\n';
        check(d.objects().modeler_errors.empty(), "Modeler decode failures");
        check(d.objects().modelers.size() == 9, "Nine modelers expected");
        std::ofstream o(prefix + ".txt");
        for (auto &[h, m] : d.objects().modelers) {
            auto &a = m.data;
            auto &r = a.revision;
            o << h << ' ' << m.type << ' ' << m.properties.owner << ' ' << m.history << ' '
              << (m.acis_version ? m.acis_version : 1) << ' ' << a.geometry.is_binary << ' '
              << a.wireframe_present << ' ' << a.point_present << ' ' << a.isoline_present << ' '
              << m.acis_empty_bit << ' ' << a.isolines << ' ' << a.wires.size() << ' ' << a.silhouettes.size()
              << ' ' << a.materials.size() << ' ' << r.has_guid;
            point(o, d.modeler_reference_point(h));
            o << ' ' << r.major << ' ' << r.minor1 << ' ' << r.minor2 << ' ' << r.end_marker;
            for (auto n : r.bytes)
                o << ' ' << unsigned(n);
            for (auto &mat : a.materials)
                o << ' ' << mat.array_index << ' ' << mat.absolute_reference << ' ' << mat.handle;
            for (auto &w : a.wires)
                wire(o, w);
            for (auto &s : a.silhouettes)
                for (auto &w : s.wires)
                    wire(o, w);
            for (auto &s : a.silhouettes) {
                o << ' ' << s.viewport_id << ' ' << s.perspective << ' ' << s.has_wires;
                point(o, s.target);
                point(o, s.direction);
                point(o, s.up);
            }
            o << '\n';
            auto file = prefix + "." + std::to_string(h);
            sat(a.geometry, file + ".sat");
            if (a.extra)
                sat(*a.extra, file + ".extra.sat");
            check(d.modeler_geometry(h).has_value() == (!a.geometry.sab.empty() || !a.geometry.sat.empty()),
                  "Geometry getter");
        }
        std::ofstream saved(prefix + ".dwg", std::ios::binary);
        d.write(saved);
        saved.close();
        auto edited = d;
        std::ostringstream source_bytes;
        d.write(source_bytes);
        for (auto &[h, m] : d.objects().modelers)
            if (h % 2 == 0) {
                auto g = acis::build_torus({9, -4, 7}, 8, 2);
                edited.set_modeler_geometry(h, g);
            }
        {
            std::ofstream out(prefix + ".edited.dwg", std::ios::binary);
            edited.write(out);
        }
        auto cleared=d;
        for(auto& [h,m]:d.objects().modelers)if(h%2==0){cleared.clear_modeler_geometry(h);check(!cleared.modeler_geometry(h),"Clear primary geometry");std::ostringstream once;cleared.write(once);cleared.clear_modeler_geometry(h);std::ostringstream twice;cleared.write(twice);check(once.str()==twice.str(),"Clear is idempotent");}
        {std::ofstream out(prefix+".cleared.dwg",std::ios::binary);cleared.write(out);}
        auto before = std::ostringstream{};
        edited.write(before);
        bool rejected = false;
        try {
            edited.set_modeler_geometry(d.objects().modelers.begin()->first, acis::SatDocument{});
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "Empty replacement rejected");
        std::ostringstream after;
        edited.write(after);
        check(before.str() == after.str(), "Failed mutation must be atomic");
        std::cout << "Verified DWG modeler streams and AcDs handle association\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
