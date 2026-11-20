// SPDX-License-Identifier: MPL-2.0
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <libopencad/dwg_r2000.hpp>
#include <stdexcept>
using namespace libopencad;
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("AC1015 fixture mismatch");
}
template <typename F> void rejects(F test, std::string_view reason = {}) {
    try {
        test();
    } catch (const std::invalid_argument &e) {
        if (!reason.empty() && reason != e.what())
            throw;
        return;
    }
    throw std::runtime_error("Accepted corrupt DWG data");
}
// Change a field without invalidating its record CRC, to exercise structural diagnostics.
void patch_field(std::string &file, const DwgR2000Record &record, std::size_t bit,
                 const DwgBitWriter &field) {
    for (std::size_t i = 0; i < field.position(); ++i) {
        auto pos = record.body_offset + (bit + i) / 8;
        unsigned mask = 0x80 >> ((bit + i) % 8);
        bool set = static_cast<unsigned char>(field.bytes()[i / 8]) & (0x80 >> (i % 8));
        file.at(pos) = static_cast<char>(set ? file.at(pos) | mask : file.at(pos) & ~mask);
    }
    unsigned crc = 0xc0c1;
    auto end = record.body_offset + record.body_size;
    for (auto pos = record.offset; pos < end; ++pos) {
        crc ^= static_cast<unsigned char>(file[pos]);
        for (int i = 0; i < 8; ++i)
            crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    file.at(end) = static_cast<char>(crc & 255);
    file.at(end + 1) = static_cast<char>(crc >> 8);
}
int main(int argc, char **argv) try {
    bool line_fixture = argc == 4 && std::string_view(argv[3]) == "--lines";
    check(argc == 3 || line_fixture || (argc == 4 && std::string_view(argv[3]) == "--external"));
    std::ifstream input(argv[1], std::ios::binary);
    std::string file{std::istreambuf_iterator<char>(input), {}};
    auto idx = read_dwg_r2000_index(file);
    std::ofstream out(argv[2], std::ios::binary);
    for (auto [id, loc] : idx.sections)
        out << "S " << id << " " << loc.offset << " " << loc.size << "\n";
    if (argc == 3)
        check(idx.object_offsets.size() >= 900 && idx.sections.at(2).size > 2048);
    auto objects = read_dwg_r2000_objects(file);
    check(objects.records.size() == idx.object_offsets.size());
    if (argc == 3)
        check(objects.lines.size() == 900);
    if (line_fixture) {
        check(objects.lines.size() == 3);
        const auto &paper = objects.lines.at(50);
        check(paper.properties.mode == 1 && paper.properties.invisible &&
              paper.properties.color.index() == 5);
        check(paper.start == Point3{-5, 7, 0} && paper.end == Point3{-5, -2, 0});
        check(paper.normal == Vector3::UNIT_Y && paper.thickness == -1.25 &&
              paper.properties.linetype_scale == 2.5);
        const auto &model = objects.lines.at(51);
        check(model.properties.mode == 2 && model.start == Point3{1, -2, 3});
        check(model.end == Point3{1.0000000001, -2.001, -4} && model.normal == Vector3::UNIT_Z);
        const auto &child = objects.lines.at(55);
        check(child.properties.mode == 0 && child.properties.owner == 52);
        check(child.properties.reactors == std::vector<std::uint64_t>{50});
        check(child.properties.lineweight_index == 11 && child.properties.linetype_flags == 1);
    }
    for (const auto &[h, rec] : objects.records) {
        DwgBitReader object(std::string_view(file).substr(rec.body_offset, rec.body_size));
        out << "O " << h << " " << rec.offset << " " << rec.body_offset << " " << rec.body_size << " "
            << rec.type << " " << rec.type_end_bit << " " << rec.main_bits << " " << rec.handle_end_bit;
        object.seek(rec.main_bits);
        if (rec.body_size * 8 - rec.main_bits >= 8) {
            auto ref = object.handle(h);
            out << " " << ref.value << " " << object.position();
        }
        out << "\n";
        if (!objects.lines.count(h))
            continue;
        const auto &line = objects.lines.at(h);
        const auto &p = line.properties;
        out << "L " << h << " " << p.mode << " " << p.owner << " " << p.layer << " " << p.extension_dictionary
            << " " << p.previous << " " << p.next << " " << p.color.index().value() << " "
            << p.lineweight_index << " " << p.invisible << " " << p.linetype_flags << " " << p.linetype << " "
            << p.plotstyle_flags << " " << p.plotstyle << " " << p.reactors.size();
        for (auto reactor : p.reactors)
            out << " " << reactor;
        for (double value :
             {p.linetype_scale, line.start.x, line.start.y, line.start.z, line.end.x, line.end.y, line.end.z,
              line.thickness, line.normal.x, line.normal.y, line.normal.z}) {
            std::uint64_t bits;
            std::memcpy(&bits, &value, 8);
            out << " " << bits;
        }
        out << "\n";
    }
    if (!objects.records.empty()) {
        const auto &r = objects.records.begin()->second;
        auto dirty = file;
        dirty.at(0x0c) ^= 1;
        rejects([&] { read_dwg_r2000_index(dirty); }, "DWG locator CRC mismatch");
        dirty = file;
        dirty[r.body_offset + r.body_size] ^= 1;
        rejects([&] { read_dwg_r2000_objects(dirty); }, "DWG object CRC mismatch");
        dirty = file;
        dirty.replace(r.offset, 4, 4, static_cast<char>(0xff));
        rejects([&] { read_dwg_r2000_objects(dirty); });
    }
    if (line_fixture) {
        auto r = objects.records.at(50);
        for (auto split : {std::size_t(0), r.body_size * 8 + 1, r.main_bits - 1}) {
            auto dirty = file;
            DwgBitWriter field;
            field.little_endian(4, split);
            patch_field(dirty, r, r.type_end_bit, field);
            rejects([&] { read_dwg_r2000_objects(dirty); });
        }
        auto dirty = file;
        DwgBitWriter unknown;
        unknown.bit_short(90);
        check(unknown.position() == r.type_end_bit);
        patch_field(dirty, r, 0, unknown);
        const auto opaque = read_dwg_r2000_objects(dirty);
        check(std::find(opaque.unknown_fixed_types.begin(), opaque.unknown_fixed_types.end(), 50) !=
              opaque.unknown_fixed_types.end());
        check(opaque.records.at(50).offset == r.offset && opaque.records.at(50).type == 90);
        dirty = file;
        DwgBitWriter wrong_handle;
        wrong_handle.absolute_handle(0, 51);
        patch_field(dirty, r, r.type_end_bit + 32, wrong_handle);
        rejects([&] { read_dwg_r2000_objects(dirty); }, "DWG object handle disagrees with map");
        dirty = file;
        dirty[r.offset] = static_cast<char>(0xff);
        dirty[r.offset + 1] = 127;
        rejects([&] { read_dwg_r2000_objects(dirty); }, "DWG object extends beyond record boundary");
    }
    DwgBitWriter w;
    w.bits(1, 1);
    for (int x : {0, 256, 255, -1234})
        w.bit_short(x);
    for (int x : {0, 100, -123456})
        w.bit_long(x);
    for (double x : {0., 1., -3.25})
        w.bit_double(x);
    for (int x : {101, 99, 0x123456, 3, 100})
        w.relative_handle(100, x);
    out << "W " << w.position() << " " << std::hex << std::setfill('0');
    for (unsigned char b : w.bytes())
        out << std::setw(2) << unsigned(b);
    out << "\n";
    out.close();
    check(bool(out));
    DwgBitReader r(w.bytes());
    check(r.bits(1) == 1);
    for (int x : {0, 256, 255, -1234})
        check(r.bit_short() == x);
    for (int x : {0, 100, -123456})
        check(r.bit_long() == x);
    for (double x : {0., 1., -3.25})
        check(r.bit_double() == x);
    for (unsigned x : {101, 99, 0x123456, 3, 100})
        check(r.handle(100).value == x);
    check(r.position() == w.position());
    DwgBitWriter widths;
    for (unsigned x : {0u, 32767u, 32768u, 0x3fffffffu})
        widths.modular_short(x);
    widths.absolute_handle(5, UINT64_MAX);
    DwgBitReader wr(widths.bytes());
    for (unsigned x : {0u, 32767u, 32768u, 0x3fffffffu})
        check(wr.modular_short() == x);
    auto abs = wr.handle();
    check(abs.code == 5 && abs.value == UINT64_MAX);
    rejects([&] { wr.bits(1); });
    rejects([&] { widths.modular_short(1u << 30); });
    for (auto pos : {std::size_t(25 + 9 * idx.sections.size()), std::size_t(27 + 9 * idx.sections.size()),
                     std::size_t(idx.sections.at(2).offset) + 3,
                     std::size_t(idx.sections.at(2).offset + idx.sections.at(2).size - 1)}) {
        auto dirty = file;
        dirty.at(pos) ^= 1;
        rejects([&] { read_dwg_r2000_index(dirty); });
    }
    rejects([&] { read_dwg_r2000_index(std::string_view(file).substr(0, 30)); });
    rejects([] { DwgBitReader("\x09").handle(); });
    std::cout << idx.object_offsets.size() << " mapped objects\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
