#include "dwg_metadata_internal.hpp"
// SPDX-License-Identifier: MPL-2.0
#include <optional>
// acadrust bit_reader/bit_writer, file_header_ac15 and handle_reader.
#include "dwg_edit_internal.hpp"
#include "dwg_modeler_internal.hpp"
#include "dwg_read_internal.hpp"
#include "dwg_sat_internal.hpp"
#include "dwg_sections_internal.hpp"
#include "dxf_code_page.hpp"
#include "arch_internal.hpp"
#include <libopencad/line_weight.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include <libopencad/dwg_r2000.hpp>
#include <libopencad/dwg_sections.hpp>
#include <libopencad/dxf_version.hpp>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
namespace libopencad {
namespace {
void require(bool condition, const char *reason) {
    if (!condition)
        throw std::invalid_argument(reason);
}
void read_diagnostic(DwgReadStatistics &stats, DwgReadDiagnostic diagnostic, bool error = true) {
    if (error)
        ++stats.recovered_errors;
    constexpr std::size_t limit = 1000; // acadrust MAX_READ_DIAGNOSTICS
    if (stats.diagnostics.size() < limit - 1)
        stats.diagnostics.push_back(std::move(diagnostic));
    else if (stats.diagnostics.size() == limit - 1)
        stats.diagnostics.push_back({"diagnostic-limit-reached", "Additional DWG diagnostics were omitted", {},
                                     DwgReadStage::RecordStream, {}, {}, {}, {}});
}
std::uint16_t crc16(std::string_view data) {
    std::uint16_t crc = 0xc0c1;
    for (unsigned char c : data) {
        crc ^= c;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    return crc;
}
std::uint64_t compact_integer(DwgBitReader &r, bool signed_magnitude, bool &negative) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
        auto byte = r.bits(8);
        bool last = byte < 128;
        auto part = byte & ((last && signed_magnitude) ? 63 : 127);
        require(part <= (UINT64_MAX >> shift), "DWG modular integer overflow");
        value |= part << shift;
        if (last) {
            negative = signed_magnitude && (byte & 64);
            return value;
        }
    }
    throw std::invalid_argument("DWG modular integer too long");
}
std::map<std::uint64_t, std::size_t> read_offsets(std::string_view map, std::size_t object_size,
                                                DwgReadStatistics *stats = nullptr) {
    std::map<std::uint64_t, std::size_t> offsets;
    const auto length = map.size();
    while (map.size() >= 4) {
        DwgBitReader chunk(map);
        auto size = chunk.bits(16);
        require(size >= 2 && size <= 2048 && size <= map.size() - 2, "Invalid DWG handle chunk size");
        chunk.seek(size * 8);
        if (chunk.bits(16) != crc16(map.substr(0, size))) {
            if (stats)
                read_diagnostic(*stats, {"checksum-mismatch", "DWG handle map CRC mismatch", "AcDb:Handles",
                                         DwgReadStage::Handles, length - map.size() + size,
                                         DwgOffsetBasis::HandleSectionByte, {}, {}}, false);
            require(stats && stats->recovery_mode, "DWG handle map CRC mismatch");
        }
        if (size == 2) {
            require(map.size() == 4, "Trailing DWG handle map bytes");
            return offsets;
        }
        auto payload = map.substr(2, size - 2);
        DwgBitReader pairs(payload);
        std::uint64_t h = 0, offset = 0;
        while (pairs.position() < payload.size() * 8) {
            bool negative = false;
            auto delta = compact_integer(pairs, false, negative);
            require(delta <= UINT64_MAX - h, "DWG handle overflow");
            h += delta;
            auto distance = compact_integer(pairs, true, negative);
            require(negative ? distance <= offset : distance <= UINT64_MAX - offset,
                    "DWG object offset overflow");
            offset = negative ? offset - distance : offset + distance;
            require(stats || (h && offset < object_size), "Invalid DWG object location");
            const auto location = offset > SIZE_MAX ? SIZE_MAX : static_cast<std::size_t>(offset);
            require(offsets.emplace(h, location).second, "Duplicate DWG object handle");
        }
        map.remove_prefix(size + 2);
    }
    throw std::invalid_argument("Missing DWG map terminator");
}
} // namespace
DwgBitReader::DwgBitReader(std::string_view bytes) : data_(bytes) {
    require(bytes.size() <= SIZE_MAX / 8, "DWG stream too large");
    limit_ = bytes.size() * 8;
}
DwgBitReader::DwgBitReader(std::string_view bytes, std::size_t limit_bits) : DwgBitReader(bytes) {
    require(limit_bits <= limit_, "DWG bit limit outside stream");
    limit_ = limit_bits;
}
void DwgBitReader::seek(std::size_t bit) {
    require(bit <= limit_, "DWG seek outside stream");
    pos_ = bit;
}
std::uint64_t DwgBitReader::bits(unsigned width) {
    require(width <= 64 && width <= limit_ - pos_, "Truncated DWG bit field");
    std::uint64_t result = 0;
    for (unsigned i = 0; i < width; ++i, ++pos_)
        result = (result << 1) | ((static_cast<unsigned char>(data_[pos_ / 8]) >> (7 - pos_ % 8)) & 1);
    return result;
}
std::uint64_t DwgBitReader::little_endian(unsigned n) {
    require(n <= 8, "DWG scalar too wide");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < n; ++i)
        value |= bits(8) << (8 * i);
    return value;
}
std::int16_t DwgBitReader::bit_short() {
    switch (bits(2)) {
    case 0: {
        auto v = little_endian(2);
        return int(v ^ 32768) - 32768;
    }
    case 1:
        return bits(8);
    case 2:
        return 0;
    default:
        return 256;
    }
}
std::int32_t DwgBitReader::bit_long() {
    switch (bits(2)) {
    case 0:
        return static_cast<std::int64_t>(little_endian(4) ^ 0x80000000u) - 0x80000000LL;
    case 1:
        return bits(8);
    case 2:
        return 0;
    default:
        throw std::invalid_argument("Reserved BL selector");
    }
}
namespace {
double as_double(std::uint64_t raw) {
    double v;
    static_assert(sizeof(v) == 8 && std::numeric_limits<double>::is_iec559);
    std::memcpy(&v, &raw, 8);
    return v;
}
} // namespace
double DwgBitReader::raw_double() { return as_double(little_endian(8)); }
double DwgBitReader::default_double(double base) {
    auto selector = bits(2);
    if (!selector)
        return base;
    if (selector == 3)
        return raw_double();
    std::uint64_t raw;
    std::memcpy(&raw, &base, 8);
    if (selector == 2)
        raw = (raw & 0xffff000000000000ULL) | (little_endian(2) << 32);
    return as_double((raw & 0xffffffff00000000ULL) | little_endian(4));
}
double DwgBitReader::bit_double() {
    switch (bits(2)) {
    case 0:
        return raw_double();
    case 1:
        return 1;
    case 2:
        return 0;
    default:
        throw std::invalid_argument("Reserved BD selector");
    }
}
std::uint32_t DwgBitReader::modular_short() {
    auto low = little_endian(2);
    if (low < 32768)
        return low;
    auto high = little_endian(2);
    require(high < 32768, "DWG modular short overflow");
    return (low & 32767) | (high << 15);
}
DwgHandleReference DwgBitReader::handle(std::uint64_t base) {
    auto code = static_cast<unsigned>(bits(4));
    auto n = bits(4);
    require(n <= 8, "DWG handle too wide");
    if (code == 6 || code == 8) {
        require(!n, "Unexpected DWG unit-offset payload");
        return {code, code == 6 ? base + 1 : base - 1};
    }
    require(code <= 5 || code == 10 || code == 12, "Unknown DWG handle code");
    auto value = bits(8 * n);
    return {code, code == 10 ? base + value : code == 12 ? base - value : value};
}
void DwgBitWriter::bits(unsigned width, std::uint64_t value) {
    require(width <= 64 && width <= SIZE_MAX - pos_ && (width == 64 || (value >> width) == 0),
            "Invalid DWG bit field");
    for (unsigned i = width; i > 0; --i, ++pos_) {
        if (!(pos_ % 8))
            data_.push_back(0);
        data_.back() = static_cast<char>(static_cast<unsigned char>(data_.back()) |
                                         (((value >> (i - 1)) & 1) << (7 - pos_ % 8)));
    }
}
void DwgBitWriter::little_endian(unsigned n, std::uint64_t v) {
    require(n <= 8 && (n == 8 || (v >> (8 * n)) == 0), "Invalid DWG scalar");
    for (unsigned i = 0; i < n; ++i) {
        bits(8, v & 255);
        v >>= 8;
    }
}
void DwgBitWriter::bit_short(std::int16_t v) {
    if (!v || v == 256)
        bits(2, v ? 3 : 2);
    else if (v > 0 && v < 256) {
        bits(2, 1);
        bits(8, v);
    } else {
        bits(2, 0);
        little_endian(2, static_cast<std::uint16_t>(v));
    }
}
void DwgBitWriter::bit_long(std::int32_t v) {
    if (!v)
        bits(2, 2);
    else if (v > 0 && v < 256) {
        bits(2, 1);
        bits(8, v);
    } else {
        bits(2, 0);
        little_endian(4, static_cast<std::uint32_t>(v));
    }
}
std::int64_t DwgBitReader::bit_long_long() {
    auto n = bits(3);
    require(n <= 8, "DWG BLL too wide");
    return static_cast<std::int64_t>(little_endian(static_cast<unsigned>(n)));
}
void DwgBitWriter::bit_long_long(std::int64_t v) {
    auto u = static_cast<std::uint64_t>(v);
    unsigned n = 0;
    for (auto x = u; x; x >>= 8)
        ++n;
    require(n <= 8, "DWG BLL too wide");
    bits(3, n);
    little_endian(n, n ? u : 0);
}
void DwgBitWriter::bit_double(double v) {
    if (v == 0 || v == 1)
        bits(2, v == 0 ? 2 : 1);
    else {
        bits(2, 0);
        std::uint64_t raw;
        std::memcpy(&raw, &v, 8);
        little_endian(8, raw);
    }
}
void DwgBitWriter::modular_short(std::uint32_t v) {
    require(v < (1u << 30), "DWG modular short overflow");
    little_endian(2, v < 32768 ? v : 32768 | (v & 32767));
    if (v >= 32768)
        little_endian(2, v >> 15);
}
void DwgBitWriter::handle_payload(unsigned code, std::uint64_t v) {
    unsigned n = 0;
    for (auto x = v; x; x >>= 8)
        ++n;
    bits(8, code * 16 + n);
    bits(8 * n, v);
}
void DwgBitWriter::absolute_handle(unsigned code, std::uint64_t v) {
    require(code <= 5, "Invalid absolute handle code");
    handle_payload(code, v);
}
void DwgBitWriter::relative_handle(std::uint64_t base, std::uint64_t h) {
    if (h == base + 1)
        bits(8, 0x60);
    else if (h == base - 1)
        bits(8, 0x80);
    else if (h >= base)
        handle_payload(10, h - base);
    else
        handle_payload(12, base - h);
}
DwgR2000Index read_dwg_r2000_index(std::string_view file) {
    require(file.substr(0, 6) == "AC1015", "Expected R2000 DWG");
    return read_dwg_linear_index(file);
}
namespace {
DwgR2000Index read_linear_directory(std::string_view file) {
    auto sig = file.substr(0, 6);
    require(sig == "AC1012" || sig == "AC1014" || sig == "AC1015", "Expected R13, R14 or R2000 DWG");
    DwgBitReader r(file);
    r.seek(0x15 * 8);
    auto count = r.little_endian(4);
    require(count > 0 && count <= 6, "Invalid R2000 locator count");
    DwgR2000Index index;
    for (unsigned i = 0; i < count; ++i) {
        auto id = r.bits(8);
        auto offset = r.little_endian(4);
        auto size = r.little_endian(4);
        require(offset <= file.size() && size <= file.size() - offset, "DWG section outside file");
        require(index.sections
                    .emplace(id, DwgSectionLocation{static_cast<std::uint32_t>(offset),
                                                    static_cast<std::uint32_t>(size)})
                    .second,
                "Duplicate DWG section");
    }
    auto end = r.position() / 8;
    require(r.little_endian(2) == crc16(file.substr(0, end)), "DWG locator CRC mismatch");
    constexpr unsigned char sentinel[] = {0x95, 0xa0, 0x4e, 0x28, 0x99, 0x82, 0x1a, 0xe5,
                                          0x5e, 0x41, 0xe0, 0x5f, 0x9d, 0x3a, 0x4d, 0};
    for (auto byte : sentinel)
        require(r.bits(8) == byte, "DWG locator sentinel mismatch");
    return index;
}
DwgR2000Index read_linear_index(std::string_view file) {
    auto index = read_linear_directory(file);
    require(index.sections.count(2), "Missing DWG handle map");
    auto section = index.sections.at(2);
    index.object_offsets = read_offsets(file.substr(section.offset, section.size), file.size());
    return index;
}
} // namespace
DwgR2000Index read_dwg_linear_index(std::string_view file) { return read_linear_index(file); }
namespace {
void skip_entity_prefix(DwgBitReader &main, std::size_t end, bool graphic, unsigned version,
                        std::vector<DwgEedItem> *eed = nullptr,
                        std::vector<std::uint8_t> *graphics = nullptr, std::size_t *graphics_bit = nullptr) {
    auto skip = [&](std::uint64_t n) {
        require(main.position() <= end && n <= (end - main.position()) / 8,
                "DWG entity data exceeds main stream");
        main.seek(main.position() + n * 8);
    };
    for (auto size = main.bit_short(); size; size = main.bit_short()) {
        require(size > 0, "Negative DWG extended data size");
        auto app = main.handle();
        require(main.position() <= end && std::uint64_t(size) <= (end - main.position()) / 8,
                "DWG entity data exceeds main stream");
        if (eed) {
            std::string payload(static_cast<std::size_t>(size), '\0');
            for (int i = 0; i < size; ++i)
                payload[static_cast<std::size_t>(i)] = static_cast<char>(main.bits(8));
            eed->push_back({app.value, std::move(payload)});
        } else
            skip(size);
    }
    if (graphic && main.bits(1)) {
        auto n = version >= 1024 ? main.bits(3) : 4;
        auto size = main.little_endian(static_cast<unsigned>(n));
        if (graphics_bit)
            *graphics_bit = main.position();
        if (graphics) {
            require(main.position() <= end && size <= (end - main.position()) / 8,
                    "DWG entity data exceeds main stream");
            graphics->resize(static_cast<std::size_t>(size));
            for (std::size_t i = 0; i < graphics->size(); ++i)
                (*graphics)[i] = static_cast<std::uint8_t>(main.bits(8));
        } else
            skip(size);
    }
}
bool basic_geometry(int type) { return (type >= 17 && type <= 19) || type == 28; }
bool is_lwpolyline(int type, const std::vector<std::string> &classes) {
    return type == 77 || (type >= 500 && std::size_t(type - 500) < classes.size() &&
                          classes[type - 500] == "LWPOLYLINE");
}
DwgR2000Record read_record(std::string_view bytes, std::size_t offset, std::uint64_t handle,
                           unsigned version, const std::vector<std::string> &classes = {}) {
    bool early = version < 1015;
    DwgBitReader prefix(bytes);
    auto size = prefix.modular_short();
    bool negative = false;
    auto handle_bits = version >= 1024 ? compact_integer(prefix, false, negative) : 0;
    require(handle_bits <= size * std::uint64_t(8), "Invalid DWG handle stream length");
    auto prefix_size = prefix.position() / 8;
    require(size <= bytes.size() - prefix_size && bytes.size() - prefix_size - size >= 2,
            "DWG object extends beyond record boundary");
    auto payload_end = prefix_size + size;
    DwgBitReader checksum(bytes.substr(payload_end, 2));
    require(checksum.little_endian(2) == crc16(bytes.substr(0, payload_end)), "DWG object CRC mismatch");
    DwgBitReader body(bytes.substr(prefix_size, size));
    std::int16_t type;
    if (version >= 1024) {
        auto tag = body.bits(2);
        type = tag < 2 ? body.bits(8) + tag * 0x1f0 : body.little_endian(2);
    } else
        type = body.bit_short();
    auto type_end = body.position();
    auto split = version >= 1024 ? size * std::uint64_t(8) - handle_bits
                 : early         ? std::uint64_t(0)
                                 : body.little_endian(4);
    auto id = body.handle();
    require(id.code == 0 && id.value == handle, "DWG object handle disagrees with map");
    auto id_end = body.position();
    auto split_field = type_end;
    const bool entity_split = basic_geometry(type) || type == 1 || type == 7 || type == 8 || (type >= 20 && type <= 26) || type == 31 || type == 32 || type == 44 ||
                              (type >= 37 && type <= 39) || is_lwpolyline(type, classes);
    if (early && entity_split) {
        skip_entity_prefix(body, size * std::size_t(8), true, version);
        split_field = body.position();
        split = body.little_endian(4);
    }
    // In R13/R14 the split follows type-specific data. Leave it unknown for opaque records.
    if (!early || entity_split)
        require(split >= body.position() && split <= std::size_t(size) * 8,
                "Invalid DWG object stream boundary");
    return {offset, offset + prefix_size, size, type, type_end, id_end, std::size_t(split), 0, split_field};
}

struct OwnershipBits {
    std::size_t mode{}, links_flag{}, links_begin{}, links_end{};
    bool implicit_links{};
};
DwgR2000EntityProperties read_entity_properties(DwgBitReader &main, DwgBitReader &handles, std::uint64_t self,
                                                std::size_t main_end, unsigned version,
                                                DwgR2000Record &r, OwnershipBits *ownership = nullptr,
                                                std::vector<DwgEedItem> *eed = nullptr,
                                                std::vector<std::uint8_t> *graphics = nullptr,
                                                std::size_t *graphics_bit = nullptr) {
    bool early = version < 1015;
    skip_entity_prefix(main, main_end, true, version, eed, graphics, graphics_bit);
    if (early)
        main.little_endian(4);
    DwgR2000EntityProperties p;
    auto reference = [&] { return handles.handle(self).value; };
    if (ownership)
        ownership->mode = main.position();
    p.mode = main.bits(2);
    require(p.mode <= 2, "Reserved DWG entity mode");
    if (!p.mode)
        p.owner = reference();
    auto count = main.bit_long();
    require(count >= 0, "Negative DWG reactor count");
    for (int i = 0; i < count; ++i)
        p.reactors.push_back(reference());
    if (version < 1018 || !main.bits(1))
        p.extension_dictionary = reference();
    if (version >= 1027) {
        r.ds_data_bit=main.position();
        p.has_ds_data = main.bits(1);
    }
    if (early) {
        r.layer_bit = handles.position();
        p.layer = reference();
        r.linetype_flag_bit = main.position();
        r.linetype_bit = handles.position();
        if (!main.bits(1)) {
            p.linetype_flags = 3;
            p.linetype = reference();
        }
    }
    if (version < 1018) {
        auto pos = main.position(), begin = handles.position();
        bool implicit = main.bits(1);
        if (!implicit) {
            p.previous = reference();
            p.next = reference();
        } else if (ownership) {
            // Public properties expose stored references. Only chain traversal
            // expands the implicit sequential handles.
            p.previous = self - 1;
            p.next = self + 1;
        }
        if (ownership) {
            ownership->links_flag = pos;
            ownership->links_begin = begin;
            ownership->links_end = handles.position();
            ownership->implicit_links = implicit;
        }
    }
    r.color_bit = main.position();
    r.color_handle_bit = handles.position();
    auto color = static_cast<std::uint16_t>(main.bit_short());
    if (version >= 1018) {
        p.color = Color::from_index(color & 0x1fff);
        if ((color & 0xc000) == 0x8000) {
            auto packed = static_cast<std::uint32_t>(main.bit_long());
            switch (packed >> 24) {
            case 0xc0:
                p.color = Color::by_layer();
                break;
            case 0xc1:
                p.color = Color::by_block();
                break;
            case 0xc3:
                p.color = Color::from_index(packed & 255);
                break;
            case 0xc8:
                p.color = Color::none();
                break;
            default:
                p.color = Color::from_true_color_value(packed);
            }
        }
        if (color & 0x2000)
            p.transparency = main.bit_long();
        if (color & 0x4000)
            p.color_book = reference();
    } else
        p.color = Color::from_index(static_cast<std::int16_t>(color));
    r.linetype_scale_bit = main.position();
    p.linetype_scale = main.bit_double();
    if (early) {
        r.visibility_bit = main.position();
        p.invisible = (main.bit_short() & 1) != 0;
        return p;
    }
    r.layer_bit = handles.position();
    p.layer = reference();
    r.linetype_flag_bit = main.position();
    r.linetype_bit = handles.position();
    p.linetype_flags = main.bits(2);
    if (p.linetype_flags == 3)
        p.linetype = reference();
    if (version >= 1021) {
        p.material_flags = main.bits(2);
        p.shadow_flags = main.bits(8);
        if (p.material_flags == 3)
            p.material = reference();
    }
    p.plotstyle_flags = main.bits(2);
    if (p.plotstyle_flags == 3)
        p.plotstyle = reference();
    if (version >= 1024) {
        for (auto &style : p.visual_styles)
            if (main.bits(1))
                style = reference();
    }
    r.visibility_bit = main.position();
    p.invisible = (main.bit_short() & 1) != 0;
    p.lineweight_index = main.bits(8);
    return p;
}

Vector3 read_vector(DwgBitReader &r) { return {r.bit_double(), r.bit_double(), r.bit_double()}; }

void read_endpoints(DwgBitReader &main, DwgR2000Line &line, bool early) {
    if (early) {
        line.start = read_vector(main);
        line.end = read_vector(main);
        return;
    }
    bool flat = main.bits(1);
    auto pair = [&](double &start, double &end) {
        start = main.raw_double();
        end = main.default_double(start);
    };
    pair(line.start.x, line.end.x);
    pair(line.start.y, line.end.y);
    if (!flat)
        pair(line.start.z, line.end.z);
}

void read_geometry(std::string_view body, DwgR2000Record &r, std::uint64_t h, unsigned version,
                   DwgR2000Objects &result) {
    bool early = version < 1015;
    auto main_end = r.main_bits;
    DwgBitReader handles(body);
    if (version >= 1021) {
        require(main_end > r.handle_end_bit, "Missing DWG string stream flag");
        handles.seek(--main_end);
        if (handles.bits(1)) {
            auto trailer = [&]() {
                require(main_end >= 16, "Invalid DWG string trailer");
                main_end -= 16;
                handles.seek(main_end);
                return handles.little_endian(2);
            };
            auto size = trailer();
            if (size & 0x8000)
                size = (size & 32767) + (trailer() << 15);
            require(size <= main_end, "Invalid DWG string length");
            main_end -= size;
        }
    }
    DwgBitReader main(body, main_end);
    main.seek(r.handle_end_bit);
    handles.seek(r.main_bits);
    auto props = read_entity_properties(main, handles, h, main_end, version, r);
    r.geometry_start = main.position();
    auto extrusion = [&](auto &entity) {
        entity.thickness = !early && main.bits(1) ? 0 : main.bit_double();
        entity.normal = !early && main.bits(1) ? Vector3::UNIT_Z : read_vector(main);
    };
    if ((r.type >= 37 && r.type <= 39)||r.type>=500) {
        DwgModeler modeler;
        if (r.type >= 500) {
            modeler.surface = DwgSurface{};
            modeler.surface->type = result.class_names.at(r.type - 500);
        }
        modeler.properties = props;
        modeler.type = r.type;
        detail::read_dwg_modeler_body(main, handles, main_end, h, static_cast<DxfVersion>(version), modeler);
        if (modeler.surface)
            detail::read_dwg_surface_body(main, main_end, static_cast<DxfVersion>(version), modeler);
        if (!modeler.surface && version >= 1021 && (r.type == 38 || version < 1027))
            modeler.history = handles.handle(h).value;
        require(main.position() == main_end && body.size() * 8 - handles.position() < 8,
                "Unexpected DWG modeler tail");
        r.geometry_end = main.position();
        result.modelers.emplace(h, std::move(modeler));
        return;
    }
    if (r.type == 28) {
        DwgFace face;
        face.properties = props;
        bool flags = true;
        if (early) {
            for (auto &corner : face.corners) corner = read_vector(main);
        } else {
            flags = !main.bits(1);
            const bool flat = main.bits(1);
            face.corners[0] = {main.raw_double(), main.raw_double(), flat ? 0 : main.raw_double()};
            for (std::size_t i = 1; i < 4; ++i) {
                const auto &p = face.corners[i-1];
                face.corners[i] = {main.default_double(p.x), main.default_double(p.y), main.default_double(p.z)};
            }
        }
        if (flags) face.invisible_edges = static_cast<std::uint8_t>(main.bit_short());
        require(main.position() == main_end, "Unexpected DWG 3DFACE data");
        r.geometry_end = main.position();
        result.faces.emplace(h, std::move(face));
        return;
    }
    if (r.type == 19) {
        DwgR2000Line line;
        line.properties = std::move(props);
        read_endpoints(main, line, early);
        extrusion(line);
        require(main.position() == main_end && body.size() * 8 - handles.position() < 8,
                "Unexpected trailing DWG geometry fields");
        result.lines.emplace(h, std::move(line));
    } else {
        DwgArc arc;
        DwgCircle &circle = arc;
        circle.properties = std::move(props);
        circle.center = read_vector(main);
        r.geometry_bit = main.position();
        circle.radius = main.bit_double();
        extrusion(circle);
        if (r.type == 17) {
            arc.start_angle = main.bit_double();
            arc.end_angle = main.bit_double();
        }
        require(main.position() == main_end && body.size() * 8 - handles.position() < 8,
                "Unexpected trailing DWG geometry fields");
        if (r.type == 17)
            result.arcs.emplace(h, std::move(arc));
        else
            result.circles.emplace(h, std::move(circle));
    }
    r.geometry_end = main.position();
}
} // namespace

DwgR2000Objects read_dwg_r2000_objects(std::string_view file) {
    require(file.substr(0, 6) == "AC1015", "Expected R2000 DWG");
    return read_dwg_linear_objects(file);
}

namespace {
// DWG stores pre-Unicode names using a numeric code page identifier.
const char *drawing_code_page(std::string_view file, unsigned version) {
    static constexpr const char *names[] = {
        "ANSI_1252", "ASCII",     "ISO8859-1", "ISO8859-2", "ISO8859-3", "ISO8859-4", "ISO8859-5",
        "ISO8859-6", "ISO8859-7", "ISO8859-8", "ISO8859-9", "DOS437",    "DOS850",    "DOS852",
        "DOS855",    "DOS857",    "DOS860",    "DOS861",    "DOS863",    "DOS864",    "DOS865",
        "DOS869",    "ANSI_932",  "MAC-ROMAN", "BIG5",      "KOREAN",    "JOHAB",     "DOS866",
        "ANSI_1250", "ANSI_1251", "ANSI_1252", "GB2312",    "ANSI_1253", "ANSI_1254", "ANSI_1255",
        "ANSI_1256", "ANSI_1257", "ANSI_874",  "ANSI_932",  "GB2312",    "KOREAN",    "BIG5",
        "JOHAB",     "UTF-8",     "ANSI_1258"};
    DwgBitReader r(file);
    r.seek((version == 1009 ? 0x3f9 : 0x13) * 8);
    auto page = r.little_endian(2);
    return detail::legacy_page(file.substr(0, 6), page < std::size(names) ? names[page] : names[0]);
}
bool named_table(int type) {
    switch (type) {
    case 49:
    case 51:
    case 53:
    case 57:
    case 61:
    case 63:
    case 65:
    case 67:
    case 69:
        return true;
    default:
        return false;
    }
}
std::string symbol_text(DwgBitReader &r, bool wide, const char *page) {
    auto n = r.bit_short();
    require(n >= 0, "Negative DWG string size");
    if (!wide) {
        std::string text;
        for (int i = 0; i < n; i++)
            text.push_back(static_cast<char>(r.bits(8)));
        while(!text.empty() && text.back()==0)text.pop_back();
        return detail::from_legacy(text, page);
    }
    std::u32string text;
    unsigned lead = 0;
    for (int i = 0; i < n; i++) {
        auto unit = r.little_endian(2);
        if (lead && unit >= 0xdc00 && unit <= 0xdfff) {
            text.push_back(0x10000 + ((lead - 0xd800) << 10) + (unit - 0xdc00));
            lead = 0;
            continue;
        }
        if (lead)
            text.push_back(0xfffd);
        lead = unit >= 0xd800 && unit <= 0xdbff ? unit : 0;
        if (!lead && unit)
            text.push_back(unit >= 0xdc00 && unit <= 0xdfff ? 0xfffd : unit);
    }
    if (lead)
        text.push_back(0xfffd);
    return detail::utf8_string(text);
}
#include "dwg_text_read.inc"
struct BlockBits {
    std::size_t split{}, split_field{}, count{}, after_count{}, begin{}, end{};
    bool external{};
};
template<class ReadText>
Color read_packed_cm_color(DwgBitReader &main, ReadText read_text) {
    main.bit_short(); // Modern CMC carries its color method in the packed word.
    auto rgb = static_cast<std::uint32_t>(main.bit_long());
    Color color = rgb == 0xc0000000u     ? Color::by_layer()
                  : rgb == 0xc8000000u   ? Color::none()
                  : (rgb & 0x01000000u)  ? Color::from_index(static_cast<std::int16_t>(rgb & 255))
                                         : Color::from_true_color_value(rgb);
    auto flags = main.bits(8);
    if (flags & 1)
        (void)read_text();
    if (flags & 2)
        (void)read_text();
    return color;
}
DwgSymbolRecord read_symbol(std::string_view body, DwgR2000Record &r, std::uint64_t h, unsigned v,
                            const char *page, BlockBits *positions = nullptr,
                            DwgR2000Objects *catalog = nullptr) {
    DwgBitReader main(body);
    main.seek(r.handle_end_bit);
    skip_entity_prefix(main, body.size() * 8, false, v);
    if (v < 1015) {
        r.split_field_bit = main.position();
        r.main_bits = main.little_endian(4);
    }
    require(r.main_bits >= main.position() && r.main_bits <= body.size() * 8,
            "Invalid DWG symbol stream split");
    DwgBitReader refs(body);
    refs.seek(r.main_bits);
    DwgSymbolRecord symbol{};
    symbol.type = r.type;
    symbol.owner = refs.handle(h).value;
    auto count = main.bit_long();
    require(count >= 0 && std::size_t(count) <= (body.size() * 8 - r.main_bits) / 8,
            "Invalid DWG symbol reactor count");
    for (int i = 0; i < count; i++)
        refs.handle(h);
    if (v < 1018 || !main.bits(1))
        symbol.extension_dictionary = refs.handle(h).value;
    if (v >= 1027)
        main.bits(1);
    if (catalog && r.type == 48) {
        auto count = main.bit_long();
        require(count >= 0 && std::size_t(count) <= (body.size() * 8 - refs.position()) / 8,
                "Invalid DWG block control size");
        for (int i = 0; i < count; i++)
            refs.handle(h);
        auto model = refs.handle(h).value;
        auto paper = refs.handle(h).value;
        catalog->model_space = model;
        catalog->paper_space = paper;
        return symbol;
    }
    DwgBitReader strings(body, r.main_bits);
    bool have_text = false;
    if (v >= 1021) {
        require(r.main_bits >= 1, "Missing DWG symbol string flag");
        auto length = r.main_bits - 1;
        strings.seek(length);
        have_text = strings.bits(1);
        if (have_text) {
            require(length >= 16, "Invalid DWG text stream");
            length -= 16;
            strings.seek(length);
            auto size = strings.little_endian(2);
            if (size & 0x8000) {
                require(length >= 16, "Invalid DWG extended string length");
                length -= 16;
                strings.seek(length);
                size = (size & 0x7fff) + (strings.little_endian(2) << 15);
            }
            require(size <= length, "Invalid DWG string offset");
            strings = DwgBitReader(body, length);
            strings.seek(length - size);
        }
    }
    auto text = [&]() {
        return v >= 1021 ? (!have_text ? std::string() : symbol_text(strings, true, page))
                         : symbol_text(main, false, page);
    };
    symbol.name = text();
    if (r.type == 51) {
        if (catalog) {
            if (v < 1021) main.bits(1);
            main.bit_short(); // xref flags
            if (v < 1021) main.bits(1);
            r.layer_flags_bit = main.position();
            if (v < 1015) main.bits(4); // individual layer flags before R2000
            else main.bit_short(); // packed flags and line weight
            catalog->layer_colors[symbol.name] = v < 1018 ? Color::from_index(main.bit_short())
                                                          : read_packed_cm_color(main, text);
        }
        refs.handle(h); // xref
        if (v >= 1015) refs.handle(h); // plot style
        if (v >= 1021) refs.handle(h); // material
        r.linetype_bit = refs.position();
        symbol.linetype = refs.handle(h).value;
    }
    if (r.type == 57 && catalog) {
        if (v < 1021) main.bits(1);
        main.bit_short();
        if (v < 1021) main.bits(1);
        text(); // description
        main.bit_double(); // total pattern length
        main.bits(8); // alignment
        const unsigned count = main.bits(8);
        std::vector<double> segments;
        bool simple = true;
        for (unsigned i = 0; i < count; ++i) {
            segments.push_back(main.bit_double());
            main.bit_short(); // shape number
            main.raw_double(); main.raw_double(); // x/y offset
            main.bit_double(); main.bit_double(); // scale/rotation
            if (main.bit_short() != 0) simple = false;
        }
        if (simple) catalog->simple_linetypes.emplace(symbol.name, std::move(segments));
    }
    if (r.type != 49)
        return symbol;
    if (v < 1021)
        main.bits(1);
    main.bit_short();
    if (v < 1021)
        main.bits(1);
    main.bits(2);
    auto flags = main.bits(2);
    if (v >= 1015)
        main.bits(1);
    auto count_pos = main.position();
    auto members = v >= 1018 && !flags ? main.bit_long() : 0;
    if (positions)
        *positions = {r.main_bits, r.split_field_bit, count_pos, main.position(), 0, 0, flags != 0};
    require(members >= 0 && std::size_t(members) <= (body.size() * 8 - r.main_bits) / 8,
            "Invalid DWG block member count");
    symbol.base_point = read_vector(main);
    symbol.external_block = flags != 0;
    text();
    std::size_t inserts = 0;
    if (v >= 1015) {
        while (main.bits(8))
            inserts++;
        text();
        auto preview = main.bit_long();
        require(preview >= 0 && main.position() <= r.main_bits &&
                    std::size_t(preview) <= (r.main_bits - main.position()) / 8,
                "Invalid DWG block preview");
        main.seek(main.position() + std::size_t(preview) * 8);
    }
    refs.handle(h);
    symbol.begin = refs.handle(h).value;
    if (positions)
        positions->begin = refs.position();
    if (v < 1018 && !flags) {
        symbol.first = refs.handle(h).value;
        symbol.last = refs.handle(h).value;
    }
    for (int i = 0; i < members; i++)
        symbol.members.push_back(refs.handle(h).value);
    if (positions)
        positions->end = refs.position();
    symbol.end = refs.handle(h).value;
    for (std::size_t i = 0; i < inserts; i++)
        symbol.inserts.push_back(refs.handle(h).value);
    if (v >= 1015)
        symbol.layout = refs.handle(h).value;
    return symbol;
}

#include "dwg_classes.inc"
#include "dwg_history_read.inc"
#include "dwg_dynamic.inc"

DwgLwPolyline read_lwpolyline(std::string_view body, DwgR2000Record &record,
                              std::uint64_t handle, unsigned version, const char *page) {
    auto stream = open_dynamic(body, record, handle, version, page);
    auto &main = stream.main;
    DwgLwPolyline poly;
    poly.properties = read_entity_properties(main, stream.refs, handle, stream.end, version, record);
    const auto flags = main.bit_short();
    poly.plinegen = (flags & 0x100) != 0;
    poly.closed = (flags & 0x200) != 0;
    if (flags & 4) poly.constant_width = stream.real();
    if (flags & 8) poly.elevation = stream.real();
    if (flags & 2) poly.thickness = stream.real();
    if (flags & 1) poly.normal = stream.vec();

    const auto count = main.bit_long();
    const auto bulges = flags & 0x10 ? main.bit_long() : 0;
    const auto ids = flags & 0x400 ? main.bit_long() : 0;
    const auto widths = flags & 0x20 ? main.bit_long() : 0;
    require(count >= 0 && bulges >= 0 && bulges <= count && ids >= 0 && ids <= count &&
            widths >= 0 && widths <= count, "Invalid LWPOLYLINE array counts");
    // The first XY pair is raw. R2000+ encodes later points relative to the preceding vertex.
    const auto point_bits = count ? 128ULL + (count - 1ULL) * (version < 1015 ? 128 : 4) : 0;
    require(point_bits + 2ULL * bulges + 2ULL * ids + 4ULL * widths <= main.remaining(),
            "LWPOLYLINE arrays exceed the entity stream");
    poly.vertices.resize(count);
    for (int i = 0; i < count; ++i) {
        auto &v = poly.vertices[i];
        if (i == 0 || version < 1015) {
            v.x = main.raw_double();
            v.y = main.raw_double();
        } else {
            const auto &previous = poly.vertices[i - 1];
            v.x = main.default_double(previous.x);
            v.y = main.default_double(previous.y);
        }
    }
    for (int i = 0; i < bulges; ++i) poly.vertices[i].bulge = stream.real();
    for (int i = 0; i < ids; ++i) poly.vertices[i].vertex_id = main.bit_long();
    for (int i = 0; i < widths; ++i) {
        poly.vertices[i].start_width = stream.real();
        poly.vertices[i].end_width = stream.real();
    }
    return poly;
}

#include "dwg_associative.inc"
#include "dwg_arch_read.inc"
#include "dwg_class_object.inc"
std::string recovery_record_type(std::int16_t type, const std::vector<std::string> &classes) {
    if (type >= 500 && std::size_t(type - 500) < classes.size() && !classes[type - 500].empty())
        return classes[type - 500];
    switch (type) {
    case 1: return "TEXT";
    case 7: return "INSERT";
    case 8: return "MINSERT";
    case 17: return "ARC";
    case 18: return "CIRCLE";
    case 19: return "LINE";
    case 48: return "BLOCK_CONTROL";
    case 49: return "BLOCK_RECORD";
    case 51: return "LAYER";
    case 53: return "STYLE";
    case 57: return "LTYPE";
    default: return "DWG_OBJ_" + std::to_string(type);
    }
}
DwgR2000Objects decode_objects(std::string_view file, const DwgR2000Index &index, unsigned version,
                               const char *page, const std::vector<std::string> &classes = {},
                               DwgReadStatistics *stats = nullptr) {
    std::set<std::size_t> boundaries{file.size()};
    std::set<std::uint64_t> invalid_locations;
    const auto failsafe = stats && stats->recovery_mode;
    const auto offset_basis = version <= 1015 ? DwgOffsetBasis::FileByte : DwgOffsetBasis::ObjectSectionByte;
    auto skip_record = [&](std::uint64_t handle, std::size_t offset, const char *code,
                           std::string message, std::string type = {}) {
        if (!stats)
            throw std::invalid_argument(message);
        ++stats->skipped_source_records;
        std::ostringstream detail;
        detail << "DWG record at handle 0x" << std::uppercase << std::hex << handle << ": " << message;
        auto description = detail.str();
        read_diagnostic(*stats, {code, description, "AcDb:AcDbObjects", DwgReadStage::RecordStream,
                                 offset, offset_basis, handle, std::move(type)});
        if (!failsafe)
            throw DwgReadError(std::move(description), std::move(*stats));
    };
    for (auto [id, loc] : index.sections)
        if (loc.size)
            boundaries.insert(loc.offset);
    for (auto [handle, offset] : index.object_offsets) {
        const char *problem = nullptr;
        if (!handle || offset >= file.size())
            problem = "Invalid DWG object location";
        else if (version <= 1015 && offset < 0x19 + 9 * index.sections.size() + 18)
            problem = "Object overlaps DWG directory";
        for (auto [id, loc] : index.sections)
            if (offset >= loc.offset && offset - loc.offset < loc.size)
                problem = "Object overlaps DWG section";
        // A shared offset may still identify one intrinsic handle in failsafe mode.
        if (!problem && !boundaries.insert(offset).second && !failsafe)
            problem = "Duplicate DWG object offset";
        if (problem) {
            invalid_locations.insert(handle);
            skip_record(handle, offset, "record-catalog-failed", problem);
        }
    }
    DwgR2000Objects result;
    result.class_names = classes;
    for (auto [handle, offset] : index.object_offsets) {
        if (invalid_locations.count(handle))
            continue;
        auto end = *boundaries.upper_bound(offset);
        DwgR2000Record record{};
        bool have_frame = false;
        try {
            record = read_record(file.substr(offset, end - offset), offset, handle, version, classes);
            have_frame = true;
            auto body = file.substr(record.body_offset, record.body_size);
            if (basic_geometry(record.type))
                read_geometry(body, record, handle, version, result);
            else if (record.type == 1)
                read_text_record(body, record, handle, version, page, result);
            else if (record.type == 48)
                read_symbol(body, record, handle, version, page, nullptr, &result);
            else if (named_table(record.type))
                result.symbols.emplace(handle, read_symbol(body, record, handle, version, page, nullptr, &result));
        } catch (const std::invalid_argument &error) {
            if (!stats)
                throw;
            skip_record(handle, offset, have_frame ? "record-decode-failed" : "record-catalog-failed", error.what(),
                        have_frame ? recovery_record_type(record.type, classes) : std::string{});
            continue;
        }
        // Fixed type catalogue from object_reader/common.rs. Codes 500+ are drawing classes.
        const auto type = record.type;
        if (type < 500 && !((type >= 1 && type <= 8) || (type >= 10 && type <= 53) ||
                            (type >= 56 && type <= 57) || (type >= 60 && type <= 82) || type >= 498))
            result.unknown_fixed_types.push_back(handle);
        if ((record.type >= 37 && record.type <= 39) ||
            (record.type >= 500 && std::size_t(record.type - 500) < classes.size() &&
             is_surface_class(classes[record.type - 500]))) {
            try {
                read_geometry(file.substr(record.body_offset, record.body_size), record, handle, version,
                              result);
            } catch (const std::exception &e) {
                result.modeler_errors[handle] = e.what();
            }
        }
        if(record.type>=500&&std::size_t(record.type-500)<classes.size()&&history_class(classes[record.type-500])){
            try{result.history_records.emplace(handle,read_history_record(file.substr(record.body_offset,record.body_size),record,handle,version,page,classes[record.type-500]));}
            catch(const std::exception& e){result.history_errors[handle]=e.what();}
        }
        if (record.type >= 500 && std::size_t(record.type - 500) < classes.size() &&
            is_associative_object_name(classes[record.type - 500])) {
            try {
                auto body = file.substr(record.body_offset, record.body_size);
                result.associative_records.emplace(
                    handle, read_associative_record(body, record, handle, version, page,
                                                    classes[record.type - 500]));
            } catch (const std::exception &e) {
                result.associative_errors[handle] = e.what();
            }
        }
        if (record.type >= 500 && std::size_t(record.type - 500) < classes.size() &&
            is_class_object_name(classes[record.type - 500])) {
            try {
                auto body = file.substr(record.body_offset, record.body_size);
                result.class_objects.emplace(handle,
                                             read_class_object_record(body, record, handle, version, page,
                                                                      classes[record.type - 500]));
            } catch (const std::exception &e) {
                result.class_object_errors[handle] = e.what();
            }
        }
        auto dynamic_name = [&]() -> std::string {
            if (record.type == 132)
                return "BLOCKVISIBILITYPARAMETER";
            if (record.type == 133)
                return "ACDB_BLOCKREPRESENTATION_DATA";
            if (record.type >= 500 && std::size_t(record.type - 500) < classes.size())
                return classes[record.type - 500];
            return {};
        }();
        if (!dynamic_name.empty() && is_dynamic_object_name(dynamic_name)) {
            try {
                auto body = file.substr(record.body_offset, record.body_size);
                result.dynamic_records.emplace(
                    handle, read_dynamic_record(body, record, handle, version, page, dynamic_name,
                                                is_dynamic_entity_name(dynamic_name)));
            } catch (const std::exception &e) {
                result.dynamic_errors[handle] = e.what();
            }
        }
        if (record.type >= 500 && std::size_t(record.type - 500) < classes.size() &&
            is_architecture_name(classes[record.type - 500]) &&
            !is_dynamic_object_name(classes[record.type - 500]) &&
            !is_associative_object_name(classes[record.type - 500])) {
            try {
                auto body = file.substr(record.body_offset, record.body_size);
                decode_architecture_record(result, body, record, handle, version, page,
                                           classes[record.type - 500]);
            } catch (const std::exception &e) {
                result.arch_errors[handle] = e.what();
            }
        }
        if ((record.type >= 20 && record.type <= 26) || record.type == 31 || record.type == 32) {
            try {
                auto r = open_dynamic(file.substr(record.body_offset, record.body_size), record, handle, version, page);
                auto properties = read_entity_properties(r.main, r.refs, handle, r.end, version, record);
                if (record.type == 31 || record.type == 32) {
                    DwgSolid solid;
                    solid.properties = properties;
                    if (version < 1015 || !r.main.bits(1)) solid.thickness = r.real();
                    double z = r.real();
                    for (auto &p : solid.corners) { auto xy = r.raw2(); p = {xy.x,xy.y,z}; }
                    if (version < 1015 || !r.main.bits(1)) solid.normal = r.vec();
                    result.solids.emplace(handle, std::move(solid));
                } else {
                    DwgDimension dim;
                    dim.properties = properties;
                    auto &d = dim.definition;
                    if (version >= 1024) r.main.bits(8);
                    d.normal = r.vec();
                    auto middle = r.raw2();
                    d.text_middle_point = {middle.x, middle.y, r.real()};
                    d.text_user_positioned = !(r.main.bits(8) & 1);
                    d.text_override = r.str();
                    d.text_rotation = r.real();
                    d.horizontal_direction = r.real();
                    dim.insertion_scale = r.vec();
                    d.insertion_rotation = r.real();
                    if (version >= 1015) {
                        d.attachment_point = r.main.bit_short();
                        d.line_spacing_style = r.main.bit_short();
                        d.line_spacing_factor = r.real();
                        auto measurement = r.real();
                        if (measurement >= 0) d.actual_measurement = measurement;
                    }
                    if (version >= 1021) {
                        r.main.bits(1);
                        d.flip_arrow1 = r.main.bits(1);
                        d.flip_arrow2 = r.main.bits(1);
                    }
                    auto insertion = r.raw2();
                    d.insertion_point = {insertion.x, insertion.y, 0};
                    dim.style = r.href();
                    dim.graphics_block = r.href();
                    switch (record.type) {
                    case 20: {
                        d.definition_point = r.vec();
                        d.geometry = OrdinateDimension{r.vec(), r.vec(), r.main.bits(8) == 1};
                        break;
                    }
                    case 21: case 22: {
                        auto first = r.vec(), second = r.vec();
                        d.definition_point = r.vec();
                        auto oblique = r.real();
                        if (record.type == 21) d.geometry = LinearDimension{first, second, r.real(), oblique};
                        else d.geometry = AlignedDimension{first, second, oblique};
                        break;
                    }
                    case 23:
                        d.definition_point = r.vec();
                        d.geometry = Angular3PointDimension{r.vec(), r.vec(), r.vec()};
                        break;
                    case 24: {
                        auto arc = r.raw2();
                        d.geometry = Angular2LineDimension{r.vec(), r.vec(), r.vec(), {arc.x,arc.y,d.text_middle_point.z}};
                        d.definition_point = r.vec();
                        break;
                    }
                    case 25: case 26: {
                        auto first = r.vec(), second = r.vec();
                        if (record.type == 25) { d.definition_point = first; d.geometry = RadiusDimension{second,r.real()}; }
                        else { d.definition_point = second; d.geometry = DiameterDimension{first,r.real()}; }
                        break;
                    }
                    }
                    result.dimensions.emplace(handle, std::move(dim));
                }
            } catch (const std::exception &e) { result.dimension_errors.emplace(handle,e.what()); }
        }
        if (is_lwpolyline(record.type, classes)) {
            try {
                auto body = file.substr(record.body_offset, record.body_size);
                result.lwpolylines.emplace(handle, read_lwpolyline(body, record, handle, version, page));
            } catch (const std::invalid_argument &error) {
                result.lwpolyline_errors.emplace(handle, error.what());
            }
        }
        if (record.type == 44) {
            try {
                auto stream = open_dynamic(file.substr(record.body_offset, record.body_size), record, handle, version, page);
                DwgMText text;
                text.properties = read_entity_properties(stream.main, stream.refs, handle, stream.end, version, record);
                text.insertion = stream.vec();
                text.normal = stream.vec();
                text.x_direction = stream.vec();
                text.width = stream.real();
                if (version >= 1021) text.rectangle_height = stream.real();
                text.height = stream.real();
                text.attachment = stream.main.bit_short();
                text.drawing_direction = stream.main.bit_short();
                text.extents_height = stream.real();
                text.extents_width = stream.real();
                text.value = stream.str();
                text.style = stream.href();
                if (version >= 1015) {
                    text.line_spacing_style = stream.main.bit_short();
                    text.line_spacing_factor = stream.real();
                }
                result.mtexts.emplace(handle, std::move(text));
            } catch (const std::exception &e) {
                result.mtext_errors.emplace(handle, e.what());
            }
        }
        if (record.type == 7 || record.type == 8) {
            try {
                auto body = file.substr(record.body_offset, record.body_size);
                std::vector<DwgEedItem> eed;
                result.inserts.emplace(handle, read_insert_record(body, record, handle, version,
                                                                  record.type == 8, &eed));
                if (!eed.empty())
                    result.eed[handle] = std::move(eed);
            } catch (const std::exception &e) {
                result.insert_errors[handle] = e.what();
            }
        }
        if (version >= 1018 && (record.type == 42 || record.type == 79)) {
            try {
                result.metadata_records.emplace(
                    handle,
                    read_history_record(file.substr(record.body_offset, record.body_size), record, handle,
                                        version, page, record.type == 42 ? "DICTIONARY" : "XRECORD"));
            } catch (const std::exception &e) {
                result.metadata_errors[handle] = e.what();
            }
        }
        if (record.type == 49) {
            try {
                auto body = file.substr(record.body_offset, record.body_size);
                DwgBitReader prefix(body);
                prefix.seek(record.handle_end_bit);
                std::vector<DwgEedItem> eed;
                skip_entity_prefix(prefix, body.size() * 8, false, version, &eed);
                if (!eed.empty())
                    result.eed[handle] = std::move(eed);
            } catch (const std::exception &) {
            }
        }
        result.records.emplace(handle, record);
        if (stats)
            ++stats->decoded_source_records;
    }
    for (auto &[h, m] : result.modelers)
        if (m.properties.mode)
            m.properties.owner = m.properties.mode == 1 ? result.paper_space : result.model_space;
    for (auto &[h, i] : result.inserts)
        if (i.properties.mode)
            i.properties.owner = i.properties.mode == 1 ? result.paper_space : result.model_space;
    return result;
}
} // namespace
DwgR2000Objects read_dwg_linear_objects(std::string_view file) {
    auto version = unsigned(dxf_version_from_string(file.substr(0, 6)));
    auto index = read_dwg_linear_index(file);
    auto page = drawing_code_page(file, version);
    std::vector<DwgClassRecord> classes;
    std::string error;
    try {
        auto loc = index.sections.at(1);
        classes = read_class_records(file.substr(loc.offset, loc.size), version,
                                     static_cast<unsigned char>(file[11]), page);
    } catch (const std::exception &e) {
        error = e.what();
    }
    std::vector<std::string> names;
    for (auto &c : classes)
        names.push_back(c.dxf_name);
    auto result = decode_objects(file, index, version, page, names);
    result.classes = std::move(classes);
    result.classes_error = error;
    return result;
}
namespace {
DwgR2000Objects read_r12_geometry(std::string_view file) {
    DwgBitReader header(file);
    header.seek(0x11 * 8);
    auto count = header.little_endian(2);
    require(count == 204 || count == 205, "Unsupported AC1009 header layout");
    std::size_t header_size = count == 204 ? 0x6bb : 0x6bd;
    header.seek(header_size * 8);
    require(header.little_endian(2) == crc16(file.substr(0, header_size)), "AC1009 header CRC mismatch");
    constexpr unsigned char begin[] = {0xc4, 0x6e, 0x68, 0x54, 0xf8, 0x6e, 0x33, 0x30,
                                       0x63, 0x3e, 0xc1, 0x85, 0x2a, 0xdc, 0x94, 0x01};
    for (auto c : begin)
        require(header.bits(8) == c, "AC1009 entity begin sentinel mismatch");
    header.seek(0x14 * 8);
    auto start = header.little_endian(4), end = header.little_endian(4);
    auto block_start = header.little_endian(4);
    auto blocks = header.little_endian(4) & 0x3fffffff;
    auto extra_start = header.little_endian(4);
    auto extras = header.little_endian(4) & 0x3fffffff;
    require(block_start <= file.size() && blocks <= file.size() - block_start && extra_start <= file.size() &&
                extras <= file.size() - extra_start,
            "Invalid AC1009 block or extra bounds");
    std::map<std::size_t, std::string> block_names;
    require(start >= header_size + 18 && start <= end && end <= file.size() && file.size() - end >= 16,
            "Invalid AC1009 entity bounds");
    header.seek(end * 8);
    for (auto c : begin)
        require(header.bits(8) == (c ^ 255), "AC1009 entity end sentinel mismatch");
    DwgR2000Objects result;
    constexpr unsigned locators[] = {0x2c, 0x36, 0x40, 0x4a, 0x54, 0x3ef, 0x500, 0x512, 0x522};
    constexpr int kinds[] = {49, 51, 53, 57, 61, 63, 65, 67, 69};
    auto page = drawing_code_page(file, 1009);
    for (unsigned i = 0; i < std::size(locators); i++) {
        header.seek(locators[i] * 8);
        auto size = header.little_endian(2), rows = header.little_endian(2);
        header.little_endian(2);
        auto at = header.little_endian(4);
        if (!rows)
            continue;
        require(size >= 35 && at <= file.size() && rows <= (file.size() - at) / size,
                "Invalid AC1009 table extent");
        for (std::size_t j = 0; j < rows; j++, at += size) {
            auto body = file.substr(at, size - 2);
            DwgBitReader check(file.substr(at + size - 2, 2));
            require(check.little_endian(2) == crc16(body), "AC1009 table CRC mismatch");
            auto name = body.substr(1, 32);
            name = name.substr(0, name.find(char(0)));
            if (i == 0 && !(static_cast<unsigned char>(body.front()) & 128)) {
                DwgBitReader row(body);
                row.seek(35 * 8);
                auto address = row.little_endian(4) & 0x3fffffff;
                auto block_name = detail::from_legacy(name, page);
                block_names.emplace(block_start + address, block_name);
                auto &symbol = result.legacy_blocks[block_name];
                symbol.type = 49;
                symbol.name = block_name;
                symbol.external_block = (static_cast<unsigned char>(body.front()) & (4 | 8 | 16)) != 0;
            }
            // Deleted rows still occupy a slot. Never resolve their old name as a live layer or linetype.
            bool deleted=(static_cast<unsigned char>(body.front())&128)!=0;
            const auto live_name = deleted ? std::string{} : detail::from_legacy(name, page);
            result.legacy_symbol_names[kinds[i]].push_back(live_name);
            if (kinds[i] == 51 && !deleted) {
                DwgBitReader row(body);
                row.seek(35 * 8); // flags, fixed name and xref index
                const int value = static_cast<int>(row.little_endian(2));
                result.layer_colors[live_name] = Color::from_index(value < 32768 ? value : value - 65536);
                if (body.size() >= 39) result.legacy_layer_linetypes[live_name] = row.little_endian(2);
            } else if (kinds[i] == 57 && !deleted && body.size() >= 93) {
                const auto n = static_cast<unsigned char>(body[84]);
                if (n <= (body.size()-93)/8) {
                    DwgBitReader row(body);
                    row.seek(93*8);
                    auto &segments = result.simple_linetypes[live_name];
                    for (unsigned k=0; k<n; ++k) segments.push_back(row.raw_double());
                }
            }
        }
    }
    struct Stream {
        std::size_t begin, end;
    };
    Stream streams[] = {
        {start, end}, {block_start, block_start + blocks}, {extra_start, extra_start + extras}};
    struct Row {
        unsigned type, flags, section;
        std::size_t size;
        bool visited = false;
    };
    std::map<std::size_t, Row> rows;
    for (unsigned section = 0; section < 3; section++) {
        auto stream = streams[section];
        for (auto at = stream.begin; at < stream.end;) {
            DwgBitReader r(file.substr(at, stream.end - at));
            unsigned type = r.bits(8), flags = r.bits(8);
            auto size = r.little_endian(2);
            bool jump = (type & 127) == 18;
            require(size >= (jump ? 10 : 6) && size <= stream.end - at, "Invalid AC1009 record size");
            auto crc_at = jump ? 8 : size - 2;
            r.seek(crc_at * 8);
            require(r.little_endian(2) == crc16(file.substr(at, crc_at)), "AC1009 entity CRC mismatch");
            require(rows.emplace(at, Row{type, flags, section, size}).second, "Overlapping AC1009 streams");
            at += size;
        }
    }
    for (unsigned root = 0; root < 3; root++)
        for (auto seed = rows.lower_bound(streams[root].begin);
             seed != rows.end() && seed->first < streams[root].end; ++seed) {
            if (seed->second.visited)
                continue;
            std::optional<std::string> block;
            std::uint64_t sequence = 0;
            auto at = seed->first;
            for (;;) {
                auto it = rows.find(at);
                require(it != rows.end() && !it->second.visited, "Invalid AC1009 JUMP target or cycle");
                it->second.visited = true;
                const auto &row = it->second;
                auto type = row.type, flags = row.flags;
                auto offset = at;
                auto data = file.substr(at, row.size - 2);
                auto section = row.section;
                at += row.size;
                if ((type & 127) == 18) {
                    DwgBitReader target(data);
                    target.seek(32);
                    auto raw = target.little_endian(4);
                    auto flag = raw >> 24;
                    require(flag == 0 || flag == 64 || flag == 128, "Invalid AC1009 JUMP tag");
                    section = flag / 64;
                    auto dest = raw & 0xffffff;
                    if (!flag && raw >= start && raw <= end)
                        dest = raw - start;
                    else if (!flag && blocks && raw >= block_start && raw <= block_start + blocks) {
                        section = 1;
                        dest = raw - block_start;
                    }
                    require(!(section == 1 && root != 1) && !(section == 0 && root == 1),
                            "AC1009 JUMP crosses block ownership");
                    require(dest <= streams[section].end - streams[section].begin,
                            "Invalid AC1009 JUMP offset");
                    at = streams[section].begin + dest;
                } else if (!(type & 128)) {
                    if (type == 12) {
                        require(root == 1 && !block, "Invalid AC1009 BLOCK nesting");
                        auto name = block_names.find(offset);
                        require(name != block_names.end(), "Missing AC1009 BLOCK table entry");
                        block = name->second;
                    } else if (type == 13) {
                        if (block)
                            result.legacy_block_ends[*block] = offset;
                        require(block.has_value(), "Orphan AC1009 ENDBLK");
                        block.reset();
                    } else if (root == 1)
                        require(block.has_value(), "AC1009 entity outside block");
                    if (type != 13) {
                        DwgBitReader r(data);
                        r.seek(32);
                        DwgR2000EntityProperties props;
                        props.legacy_layer_index = r.little_endian(2);
                        auto options = r.little_endian(2);
                        auto extra = flags & 64 ? r.bits(8) : 0;
                        props.mode = block ? 0 : (extra & 1) ? 1 : 2;
                        if (block)
                            props.legacy_block_name = *block;
                        if (extra & 2) {
                            auto length = r.little_endian(2);
                            r.seek(r.position() + length * 8);
                        }
                        auto color_bit = r.position();
                        if (flags & 1)
                            props.color = Color::from_index(r.bits(8));
                        auto linetype_bit = r.position();
                        if (flags & 2)
                            props.legacy_linetype_index = r.little_endian(2);
                        bool is3d = type == 1 || type == 2 || type == 21 || type == 22;
                        auto elevation_bit = r.position();
                        double elevation = (flags & 4) && !is3d ? r.raw_double() : 0;
                        auto thickness_bit = r.position();
                        double thickness = flags & 8 ? r.raw_double() : 0;
                        require(type == 12 || (flags & 32), "AC1009 entity lacks stored handle");
                        auto width = (flags & 32) ? r.bits(8) : 0;
                        require(width <= 8, "Oversized AC1009 handle");
                        auto h = width ? r.bits(width * 8) : 0;
                        if (extra & 4)
                            r.little_endian(2);
                        if (type == 12) {
                            result.legacy_blocks.at(*block).base_point = {r.raw_double(), r.raw_double(), elevation};
                        } else {
                            require(h && result.records
                                             .emplace(h, DwgR2000Record{std::size_t(offset), std::size_t(offset),
                                                                        data.size(),
                                                                        static_cast<std::int16_t>(type), 0, 0, 0})
                                             .second,
                                    "Duplicate or null AC1009 handle");
                            result.legacy_entity_properties.emplace(h, props);
                            if (sequence) {
                                auto parent_type = result.records.at(sequence).type;
                                if (type == 17 || type == (parent_type == 19 ? 20 : 16))
                                    result.legacy_sequences.at(sequence).push_back(h);
                                else sequence = 0;
                                if (type == 17) sequence = 0;
                            }
                            if (type == 19 || (type == 14 && (flags & 128))) {
                                sequence = h;
                                result.legacy_sequences[h] = {h};
                            }
                            auto &record = result.records.at(h);
                            record.color_bit = color_bit;
                            record.linetype_bit = linetype_bit;
                            record.elevation_bit = elevation_bit;
                            record.thickness_bit = thickness_bit;
                            record.geometry_start = r.position();
                            auto point = [&](bool z, double fallback) {
                                return Point3{r.raw_double(), r.raw_double(), z ? r.raw_double() : fallback};
                            };
                            auto normal = [&]() { return options & 1 ? point(true, 0) : Vector3::UNIT_Z; };
                            if (type == 9 || type == 11 || type == 23) {
                                try {
                                    if (type == 9 || type == 11) {
                                        DwgSolid solid;
                                        solid.properties = props;
                                        solid.thickness = thickness;
                                        for (auto &corner : solid.corners) corner = point(false,elevation);
                                        solid.normal = normal();
                                        result.solids.emplace(h,std::move(solid));
                                    } else if (type == 23) {
                                        DwgDimension dim;
                                        dim.properties = props;
                                        auto &d = dim.definition;
                                        auto slot = r.little_endian(2);
                                        const auto &blocks = result.legacy_symbol_names[49];
                                        if (slot < blocks.size()) d.block_name = blocks[slot];
                                        d.definition_point = point(true,0);
                                        d.text_middle_point = point(false,elevation);
                                        if (options & 1) d.insertion_point = point(false,elevation);
                                        unsigned flags = options & 2 ? r.bits(8) : 0;
                                        d.text_user_positioned = (flags & 128) != 0;
                                        if (options & 4) {
                                            auto n = r.little_endian(2);
                                            std::string label;
                                            for (std::size_t i=0;i<n;++i) label.push_back(char(r.bits(8)));
                                            while (!label.empty() && label.back() == 0) label.pop_back();
                                            d.text_override = detail::from_legacy(label,page);
                                        }
                                        Point3 a{}, b{}, c{}, arc{};
                                        if (options & 8) a = point(true,elevation);
                                        if (options & 16) b = point(true,elevation);
                                        if (options & 32) c = point(true,elevation);
                                        if (options & 64) arc = point(false,elevation);
                                        double leader = options & 128 ? r.raw_double() : 0;
                                        double rotation = options & 256 ? r.raw_double() : 0;
                                        double oblique = (options & 512) && !(flags & 7) ? r.raw_double() : 0;
                                        if (options & 1024) d.text_rotation = r.raw_double();
                                        if (options & 0x4000) d.normal = point(true,0);
                                        if (options & 0x8000) {
                                            auto style = r.little_endian(2);
                                            const auto &names = result.legacy_symbol_names[69];
                                            if (style < names.size()) d.style_name = names[style];
                                        }
                                        switch (flags & 7) {
                                        case 0: d.geometry = LinearDimension{a,b,rotation,oblique}; break;
                                        case 1: d.geometry = AlignedDimension{a,b,0}; break;
                                        case 2: d.geometry = Angular2LineDimension{a,b,c,arc}; break;
                                        case 3: d.geometry = DiameterDimension{c,leader}; break;
                                        case 4: d.geometry = RadiusDimension{c,leader}; break;
                                        case 5: d.geometry = Angular3PointDimension{a,b,c}; break;
                                        case 6: d.geometry = OrdinateDimension{a,b,bool(flags & 64)}; break;
                                        }
                                        result.dimensions.emplace(h,std::move(dim));
                                    }
                                } catch (const std::exception &e) {
                                    result.dimension_errors.emplace(h, e.what());
                                }
                            } else if (type == 14) {
                                auto slot = r.little_endian(2);
                                const auto &names = result.legacy_symbol_names[49];
                                if (slot >= names.size() || names[slot].empty()) {
                                    result.insert_errors[h] = "Missing AC1009 INSERT block";
                                } else {
                                    auto &insert = result.inserts[h];
                                    insert.properties = props;
                                    insert.legacy_block_name = names[slot];
                                    insert.has_attributes = (flags & 128) != 0;
                                    insert.insertion = point(false, elevation);
                                    if (options & 1) insert.scale.x = r.raw_double();
                                    if (options & 2) insert.scale.y = r.raw_double();
                                    if (options & 4) insert.rotation = r.raw_double();
                                    if (options & 8) insert.scale.z = r.raw_double();
                                    if (options & 16) insert.columns = r.little_endian(2);
                                    if (options & 32) insert.rows = r.little_endian(2);
                                    if (options & 64) insert.column_spacing = r.raw_double();
                                    if (options & 128) insert.row_spacing = r.raw_double();
                                    if (options & 256) insert.normal = point(true, 0);
                                    insert.minsert = insert.columns > 1 || insert.rows > 1;
                                }
                            } else if (type == 1 || type == 21) {
                                auto &line = result.lines[h];
                                line.properties = props;
                                line.start = point(!(flags & 4), 0);
                                line.end = point(!(flags & 4), 0);
                                line.thickness = thickness;
                                line.normal = normal();
                            } else if (type == 3 || type == 8) {
                                DwgCircle &c =
                                    type == 3 ? result.circles[h] : static_cast<DwgCircle &>(result.arcs[h]);
                                c.properties = props;
                                c.center = point(false, elevation);
                                result.records.at(h).geometry_bit = r.position();
                                c.radius = r.raw_double();
                                c.thickness = thickness;
                                if (type == 8) {
                                    auto &arc = result.arcs.at(h);
                                    arc.start_angle = r.raw_double();
                                    arc.end_angle = r.raw_double();
                                }
                                c.normal = normal();
                            }
                            if (type == 7) {
                                auto &t = result.texts[h];
                                t.flags = options;
                                t.properties = props;
                                t.thickness = thickness;
                                t.insertion = point(false, elevation);
                                record.position_end = r.position();
                                t.height = r.raw_double();
                                record.text_start = r.position();
                                auto length = r.little_endian(2);
                                std::string text;
                                for (std::size_t i = 0; i < length; ++i)
                                    text.push_back(char(r.bits(8)));
                                while (!text.empty() && text.back() == 0)
                                    text.pop_back();
                                t.value = detail::from_legacy(text, drawing_code_page(file, 1009));
                                record.text_end = r.position();
                                if (options & 1)
                                    t.rotation = r.raw_double();
                                if (options & 2)
                                    t.width_factor = r.raw_double();
                                if (options & 4)
                                    t.oblique_angle = r.raw_double();
                                if (options & 8)
                                    t.style = r.bits(8);
                                if (options & 16)
                                    t.generation = r.bits(8);
                                if (options & 32)
                                    t.horizontal = r.bits(8);
                                t.has_alignment = options & 64;
                                if (t.has_alignment)
                                    t.alignment = point(false, elevation);
                                if (options & 128)
                                    t.normal = point(true, 0);
                                if (options & 256)
                                    t.vertical = r.bits(8);
                            }
                            if (type == 1 || type == 21 || type == 3 || type == 8 || type == 7)
                                require(r.position() == data.size() * 8, "Unexpected AC1009 geometry fields");
                        }
                    }
                }
                if (at == streams[section].end)
                    break;
                require(rows.count(at) && rows.at(at).section == section, "Invalid AC1009 record boundary");
            }
            require(!block, "Missing AC1009 ENDBLK");
        }
    return result;
}
} // namespace
DwgR2000Objects read_dwg_objects(std::string_view file) {
    auto version = unsigned(dxf_version_from_string(file.substr(0, 6)));
    if (version == 1009)
        return read_r12_geometry(file);
    if (version <= 1015)
        return read_dwg_linear_objects(file);
    detail::DwgPagedReader reader(file);
    reader.authenticate(std::nullopt);
    auto objects = reader.read("AcDb:AcDbObjects");
    auto handles = reader.read("AcDb:Handles");
    DwgR2000Index index;
    index.object_offsets = read_offsets(handles, objects.size());
    std::vector<DwgClassRecord> records;
    std::vector<std::string> classes;
    std::string classes_error;
    try {
        records = read_class_records(reader.read("AcDb:Classes"), version,
                                     static_cast<unsigned char>(file[11]), drawing_code_page(file, version));
    } catch (const std::exception &e) {
        classes_error = e.what();
    }
    for (auto &c : records)
        classes.push_back(c.dxf_name);
    auto result = decode_objects(objects, index, version, drawing_code_page(file, version), classes);
    result.classes = std::move(records);
    result.classes_error = classes_error;
    if (version >= 1027)
        detail::attach_dwg_modeler_data(reader, result);
    return result;
}
namespace {
void set_integer(std::string &s, std::size_t offset, std::uint64_t v, unsigned n) {
    require(offset <= s.size() && n <= s.size() - offset, "Invalid DWG write offset");
    for (unsigned i = 0; i < n; i++) {
        s[offset + i] = static_cast<char>(v);
        v >>= 8;
    }
    require(!v, "DWG field overflow");
}
std::size_t get_u32(std::string_view s, std::size_t at) {
    DwgBitReader r(s.substr(at));
    return r.little_endian(4);
}
void set_crc(std::string &s, std::size_t start, std::size_t end) {
    set_integer(s, end, crc16(std::string_view(s).substr(start, end - start)), 2);
}
void copy_bits(DwgBitWriter &writer, DwgBitReader &reader, std::size_t n) {
    while (n) {
        auto width = static_cast<unsigned>(std::min<std::size_t>(64, n));
        writer.bits(width, reader.bits(width));
        n -= width;
    }
}
std::string changed_radius_record(std::string_view file, const DwgR2000Record &r, double radius,
                                  unsigned version) {
    auto body = file.substr(r.body_offset, r.body_size);
    DwgBitReader old(body);
    old.seek(r.geometry_bit);
    old.bit_double();
    auto after = old.position();
    DwgBitWriter field;
    field.bit_double(radius);
    auto split = r.main_bits - (after - r.geometry_bit) + field.position();
    DwgBitWriter new_body;
    old.seek(0);
    if (version < 1024) {
        copy_bits(new_body, old, r.split_field_bit);
        new_body.little_endian(4, split);
        old.seek(r.split_field_bit + 32);
    }
    copy_bits(new_body, old, r.geometry_bit - old.position());
    new_body.bit_double(radius);
    old.seek(after);
    // Every handle occupies whole bytes. Padding starts after the last reference.
    auto pad = (8 - r.main_bits % 8) % 8;
    copy_bits(new_body, old, body.size() * 8 - pad - after);
    DwgBitWriter prefix;
    require(new_body.bytes().size() < (1u << 30), "DWG record too large");
    prefix.modular_short(new_body.bytes().size());
    if (version >= 1024) {
        auto length = new_body.bytes().size() * 8 - split;
        while (length >= 128) {
            prefix.bits(8, 128 | (length & 127));
            length >>= 7;
        }
        prefix.bits(8, length);
    }
    auto result = prefix.bytes() + new_body.bytes();
    result.resize(result.size() + 2);
    set_crc(result, 0, result.size() - 2);
    return result;
}
void modular_delta(std::string &out, std::uint64_t value, bool signed_delta, bool negative = false) {
    while (value >= (signed_delta ? 64u : 128u)) {
        out.push_back(static_cast<char>(128 | (value & 127)));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value | (negative ? 64 : 0)));
}
std::string write_map(const std::map<std::uint64_t, std::size_t> &offsets) {
    std::string output, chunk(2, 0);
    auto finish = [&]() {
        chunk[0] = static_cast<char>(chunk.size() >> 8);
        chunk[1] = static_cast<char>(chunk.size());
        auto crc = crc16(chunk);
        output += chunk;
        output.push_back(static_cast<char>(crc >> 8));
        output.push_back(static_cast<char>(crc));
        chunk.assign(2, 0);
    };
    std::uint64_t last = 0;
    std::size_t at = 0;
    for (const auto &item : offsets) {
        auto h = item.first;
        auto offset = item.second;
        auto entry = [&]() {
            std::string s;
            modular_delta(s, h - last, false);
            modular_delta(s, offset >= at ? offset - at : at - offset, true, offset < at);
            return s;
        };
        auto pair = entry();
        if (chunk.size() + pair.size() > 2032) {
            finish();
            last = 0;
            at = 0;
            pair = entry();
        }
        chunk += pair;
        last = h;
        at = offset;
    }
    if (chunk.size() > 2)
        finish();
    finish();
    return output;
}
} // namespace
namespace {
template <class Encode>
std::string replace_geometry_record(std::string_view input, const DwgR2000Record &r, Encode encode) {
    unsigned version = unsigned(dxf_version_from_string(input.substr(0, 6)));
    if (version >= 1018) {

        auto bytes = read_dwg_paged_section(input, "AcDb:AcDbObjects");
        auto offsets = read_offsets(read_dwg_paged_section(input, "AcDb:Handles"), bytes.size());
        auto record = encode(bytes);
        auto size = r.body_offset + r.body_size + 2 - r.offset;
        for (auto &[h, pos] : offsets)
            if (pos >= r.offset + size)
                pos = pos - size + record.size();
        bytes.replace(r.offset, size, record);
        auto replace = version == 1021 ? detail::replace_ac21_sections : detail::replace_ac18_sections;
        return replace(input, {{"AcDb:AcDbObjects", std::move(bytes)}, {"AcDb:Handles", write_map(offsets)}});
    }
    auto idx = read_dwg_linear_index(input);
    auto changed = encode(input);
    auto old_length = r.body_offset + r.body_size + 2 - r.offset;
    std::string file(input);
    if (changed.size() == old_length) {
        file.replace(r.offset, old_length, changed);
        return file;
    }
    auto map = idx.sections.at(2);
    require(r.offset + old_length <= map.offset, "DWG object located after map");
    auto relocate = [&](std::size_t offset) {
        return offset >= r.offset + old_length ? offset - old_length + changed.size() : offset;
    };
    for (auto &[h, offset] : idx.object_offsets) {
        require(offset < map.offset, "DWG objects after handle map");
        offset = relocate(offset);
    }
    auto new_map = write_map(idx.object_offsets);
    auto target = [&](std::size_t pos) {
        auto p = relocate(pos);
        return pos >= map.offset + map.size ? p - map.size + new_map.size() : p;
    };
    file.replace(r.offset, old_length, changed);
    file.replace(relocate(map.offset), map.size, new_map);
    for (std::size_t i = 0; i < idx.sections.size(); i++) {
        auto at = 25 + 9 * i;
        auto id = static_cast<unsigned char>(file[at]);
        set_integer(file, at + 1, target(idx.sections.at(id).offset), 4);
        if (id == 2)
            set_integer(file, at + 5, new_map.size(), 4);
    }
    auto preview = get_u32(input, 13);
    set_integer(file, 13, target(preview), 4);
    if (preview && preview < input.size() && target(preview) != preview) {
        auto container = input.substr(preview);
        require(container.size() >= 37, "Truncated DWG preview");
        auto n = static_cast<unsigned char>(container[20]);
        require(21 + n * 9u <= container.size() - 16, "Invalid DWG preview descriptors");
        for (unsigned i = 0; i < n; i++) {
            auto field = 22 + i * 9;
            auto pos = get_u32(container, field);
            set_integer(file, target(preview) + field, target(pos), 4);
        }
    }
    set_crc(file, 0, 25 + 9 * idx.sections.size());
    return file;
}
} // namespace
std::string set_dwg_radius(std::string_view input, std::uint64_t handle, double radius) {
    require(std::isfinite(radius) && radius > 0, "Expected a positive finite radius");
    auto objects = read_dwg_objects(input);
    require(objects.arcs.count(handle) || objects.circles.count(handle), "DWG handle is not a circle or arc");
    auto r = objects.records.at(handle);
    if (input.substr(2, 4) == "1009") {
        std::string file(input);
        std::uint64_t raw;
        std::memcpy(&raw, &radius, 8);
        set_integer(file, r.body_offset + r.geometry_bit / 8, raw, 8);
        set_crc(file, r.offset, r.body_offset + r.body_size);
        return file;
    }
    return replace_geometry_record(input, r, [&](std::string_view bytes) {
        return changed_radius_record(bytes, r, radius, unsigned(dxf_version_from_string(input.substr(0, 6))));
    });
}
namespace {
#include "dwg_defaults.inc"
template <std::size_t N> std::string_view byte_view(const unsigned char (&array)[N]) {
    return {reinterpret_cast<const char *>(array), N};
}
std::string_view standard_drawing(DxfVersion v) {
    switch (v) {
    case DxfVersion::AC1009:
        return byte_view(blank_AC1009);
    case DxfVersion::AC1012:
        return byte_view(blank_AC1012);
    case DxfVersion::AC1014:
        return byte_view(blank_AC1014);
    case DxfVersion::AC1015:
        return byte_view(blank_AC1015);
    case DxfVersion::AC1018:
        return byte_view(blank_AC1018);
    case DxfVersion::AC1021:
        return byte_view(blank_AC1021);
    case DxfVersion::AC1024:
        return byte_view(blank_AC1024);
    case DxfVersion::AC1027:
        return byte_view(blank_AC1027);
    case DxfVersion::AC1032:
        return byte_view(blank_AC1032);
    default:
        throw std::invalid_argument("Unknown DWG version");
    }
}
void raw_double(DwgBitWriter &w, double v) {
    std::uint64_t raw;
    std::memcpy(&raw, &v, 8);
    w.little_endian(8, raw);
}
void vector_bits(DwgBitWriter &w, Vector3 p) {
    w.bit_double(p.x);
    w.bit_double(p.y);
    w.bit_double(p.z);
}
void append_bits(DwgBitWriter &out, const DwgBitWriter &w) {
    DwgBitReader r(w.bytes());
    copy_bits(out, r, w.position());
}
std::string pack_record(unsigned version, const DwgBitWriter &main, std::size_t split_field,
                        const DwgBitWriter &handles) {
    auto split = main.position();
    DwgBitWriter body;
    if (version < 1024) {
        DwgBitReader src(main.bytes());
        copy_bits(body, src, split_field);
        body.little_endian(4, split);
        src.seek(split_field + 32);
        copy_bits(body, src, split - src.position());
    } else
        append_bits(body, main);
    append_bits(body, handles);
    require(body.bytes().size() < (1u << 30), "DWG record too large");
    DwgBitWriter prefix;
    prefix.modular_short(body.bytes().size());
    std::string record = prefix.bytes();
    if (version >= 1024)
        modular_delta(record, body.bytes().size() * 8 - split, false);
    record += body.bytes();
    record.resize(record.size() + 2);
    set_crc(record, 0, record.size() - 2);
    return record;
}
struct NewEntity {
    std::uint64_t h, prev{}, next{};
    const DwgPrimitive *primitive;
    std::uint64_t owner{}, insert_block{};
    unsigned marker_type{};
    std::string_view marker_name{};
};
void encode_geometry(DwgBitWriter &main, unsigned v, const DwgPrimitive &e) {
    if (auto f = std::get_if<DwgFaceGeometry>(&e.geometry)) {
        if (v < 1015) {
            for (auto p : f->corners) vector_bits(main, p);
        } else {
            auto prev = f->corners.front();
            main.bits(1, f->invisible_edges == 0);
            main.bits(1, prev.z == 0);
            raw_double(main, prev.x); raw_double(main, prev.y);
            if (prev.z != 0) raw_double(main, prev.z);
            auto delta = [&](double a, double b) {
                main.bits(2, a == b ? 0 : 3);
                if (a != b) raw_double(main, b);
            };
            for (std::size_t i = 1; i < 4; ++i) {
                auto p = f->corners[i];
                delta(prev.x, p.x); delta(prev.y, p.y); delta(prev.z, p.z);
                prev = p;
            }
        }
        if (v < 1015 || f->invisible_edges) main.bit_short(f->invisible_edges);
        return;
    }
    if (auto ray = std::get_if<DwgRayGeometry>(&e.geometry)) {
        vector_bits(main, ray->origin); vector_bits(main, ray->direction);
        return;
    }
    if (auto p = std::get_if<DwgPointGeometry>(&e.geometry)) {
        vector_bits(main, p->location);
        if (v >= 1015) main.bits(1, 1); else main.bit_double(0);
        if (v >= 1015) main.bits(1, 1); else vector_bits(main, Vector3::UNIT_Z);
        main.bit_double(0);
        return;
    }
    if (auto l = std::get_if<DwgLineGeometry>(&e.geometry)) {
        if (v < 1015) {
            vector_bits(main, l->start);
            vector_bits(main, l->end);
        } else {
            bool flat = l->start.z == 0 && l->end.z == 0;
            main.bits(1, flat);
            auto pair = [&](double a, double b) {
                raw_double(main, a);
                if (a == b)
                    main.bits(2, 0);
                else {
                    main.bits(2, 3);
                    raw_double(main, b);
                }
            };
            pair(l->start.x, l->end.x);
            pair(l->start.y, l->end.y);
            if (!flat)
                pair(l->start.z, l->end.z);
        }
    } else if (auto c = std::get_if<DwgCircleGeometry>(&e.geometry)) {
        vector_bits(main, c->center);
        main.bit_double(c->radius);
    } else {
        auto &a = std::get<DwgArcGeometry>(e.geometry);
        vector_bits(main, a.center);
        main.bit_double(a.radius);
    }
    if (v >= 1015)
        main.bits(1, e.thickness == 0);
    if (v < 1015 || e.thickness != 0)
        main.bit_double(e.thickness);
    if (v >= 1015)
        main.bits(1, e.normal == Vector3::UNIT_Z);
    if (v < 1015 || e.normal != Vector3::UNIT_Z)
        vector_bits(main, e.normal);
    if (auto a = std::get_if<DwgArcGeometry>(&e.geometry)) {
        main.bit_double(a->start_angle);
        main.bit_double(a->end_angle);
    }
}
void symbol_string(DwgBitWriter &, DwgBitWriter &, unsigned, std::string_view, const char *);
std::string finish_symbol(DwgBitWriter &, const DwgBitWriter &, const DwgBitWriter &, unsigned, std::size_t);
std::string new_record(unsigned v, const NewEntity &entry, std::uint64_t style, const char *page,
                       unsigned modeler_type = 0, std::uint64_t layer = 16,
                       const DwgSurface *surface = nullptr, unsigned lt_mode = 0, std::uint64_t lt_handle = 0) {
    auto &e = *entry.primitive;
    DwgBitWriter main, handles;
    auto text = std::get_if<DwgTextGeometry>(&e.geometry);
    unsigned type = modeler_type ? modeler_type : text ? 1 : 19 - e.geometry.index();
    if (std::holds_alternative<DwgFaceGeometry>(e.geometry)) type = 28;
    if (std::holds_alternative<DwgPointGeometry>(e.geometry)) type = 27;
    if (auto ray = std::get_if<DwgRayGeometry>(&e.geometry)) type = ray->infinite ? 41 : 40;
    if (std::holds_alternative<InsertDefinition>(e.geometry)) type = 7;
    if (entry.marker_type) type = entry.marker_type;
    if (v >= 1024) {
        if (type < 256) {
            main.bits(2, 0);
            main.bits(8, type);
        } else if (type >= 496 && type < 752) {
            main.bits(2, 1);
            main.bits(8, type - 496);
        } else {
            require(type <= 32767, "Invalid modeler class number");
            main.bits(2, 2);
            main.little_endian(2, type);
        }
    } else
        main.bit_short(type);
    auto split = main.position();
    if (v >= 1015 && v < 1024)
        main.little_endian(4, 0);
    main.absolute_handle(0, entry.h);
    main.bit_short(0);
    main.bits(1, 0);
    if (v < 1015) {
        split = main.position();
        main.little_endian(4, 0);
    }
    main.bits(2, entry.owner ? 0 : e.paper_space ? 1 : 2);
    if (entry.owner) handles.absolute_handle(4, entry.owner);
    main.bit_long(0);
    if (v >= 1018)
        main.bits(1, 1);
    else
        handles.absolute_handle(3, 0);
    if (v >= 1027)
        main.bits(1, 0);
    if (v < 1015) {
        handles.absolute_handle(5, layer);
        main.bits(1, 1);
    }
    if (v < 1018) {
        main.bits(1, 0);
        handles.absolute_handle(4, entry.prev);
        handles.absolute_handle(4, entry.next);
    }
    auto rgb = e.color.to_true_color_value();
    if (v >= 1018 && rgb) {
        main.bit_short(0x8000);
        main.bit_long(static_cast<std::int32_t>(0xc2000000u | *rgb));
    } else main.bit_short(e.color.approximate_index());
    main.bit_double(e.linetype_scale);
    if (v >= 1015) {
        handles.absolute_handle(5, layer);
        main.bits(2, lt_mode);
        if (lt_mode == 3) handles.absolute_handle(5, lt_handle);
        if (v >= 1021) {
            main.bits(2, 0);
            main.bits(8, 0);
        }
        main.bits(2, 0);
        if (v >= 1024)
            main.bits(3, 0);
    }
    main.bit_short(0);
    if (v >= 1015)
        main.bits(8, 29);
    if (entry.marker_type == 4) {
        DwgBitWriter strings;
        symbol_string(main, strings, v, entry.marker_name, page);
        return finish_symbol(main, strings, handles, v, split);
    }
    if (entry.marker_type == 5) {
        main.bits(1, 0);
        return pack_record(v, main, split, handles);
    }
    if (auto insert = std::get_if<InsertDefinition>(&e.geometry)) {
        vector_bits(main, insert->insertion);
        main.bits(2, 0);
        raw_double(main, insert->scale.x);
        for (double scale : {insert->scale.y, insert->scale.z}) {
            main.bits(2, 3);
            raw_double(main, scale);
        }
        main.bit_double(insert->rotation);
        vector_bits(main, insert->normal);
        main.bits(1, 0); // No attached attributes.
        handles.absolute_handle(5, entry.insert_block);
        main.bits(1, 0); // No string stream.
        return pack_record(v, main, split, handles);
    }
    if (text) {
        DwgBitWriter strings;
        if (v < 1015) {
            main.bit_double(text->insertion.z);
            raw_double(main, text->insertion.x);
            raw_double(main, text->insertion.y);
            raw_double(main, 0);
            raw_double(main, 0);
            vector_bits(main, e.normal);
            main.bit_double(e.thickness);
            main.bit_double(0); // oblique
            main.bit_double(0); // rotation
            main.bit_double(text->height);
            main.bit_double(1); // width
        } else {
            main.bits(8, text->insertion.z == 0 ? 255 : 254);
            if (text->insertion.z != 0)
                raw_double(main, text->insertion.z);
            raw_double(main, text->insertion.x);
            raw_double(main, text->insertion.y);
            main.bits(1, e.normal == Vector3::UNIT_Z);
            if (e.normal != Vector3::UNIT_Z)
                vector_bits(main, e.normal);
            main.bits(1, e.thickness == 0);
            if (e.thickness != 0)
                main.bit_double(e.thickness);
            raw_double(main, text->height);
        }
        symbol_string(main, strings, v, text->value, page);
        if (v < 1015) {
            main.bit_short(0);
            main.bit_short(0);
            main.bit_short(0);
        }
        handles.absolute_handle(5, style);
        return finish_symbol(main, strings, handles, v, split);
    }
    if (modeler_type) {
        if (v < 1027)
            main.bits(1, 1); // Empty inline primary body.
        main.bits(1, 0);     // No visualization cache.
        if (v < 1027)
            main.bits(1, 1); // No secondary body.
        if (v >= 1021)
            main.bit_long(0); // Materials.
        if (v >= 1027) {
            main.bits(1, 0);
            main.bit_long(0);
            main.bit_short(0);
            main.bit_short(0);
            for (int i = 0; i < 8; ++i)
                main.bits(8, 0);
            main.bit_long(0);
        }
        if (surface)
            append_bits(main, detail::encode_dwg_surface(*surface, static_cast<DxfVersion>(v)));
        else if (v >= 1021 && (type == 38 || v < 1027))
            handles.absolute_handle(5, 0);
    } else
        encode_geometry(main, v, e);
    if (v >= 1021)
        main.bits(1, 0);
    return pack_record(v, main, split, handles);
}
// Replace the ownership list in one of the canonical empty block records.
std::string populate_block(unsigned v, std::string_view body, DwgR2000Record record, std::uint64_t self,
                           const std::vector<std::uint64_t> &members) {
    DwgBitReader src(body), refs(body);
    src.seek(record.handle_end_bit);
    skip_entity_prefix(src, body.size() * 8, false, v);
    if (v < 1015) {
        record.split_field_bit = src.position();
        record.main_bits = src.little_endian(4);
    }
    refs.seek(record.main_bits);
    refs.handle(self);
    require(src.bit_long() == 0, "Unexpected default block reactors");
    if (v < 1018 || !src.bits(1))
        refs.handle(self);
    if (v >= 1027)
        src.bits(1);
    if (v < 1021) {
        auto n = src.bit_short();
        require(n >= 0, "Invalid default block name");
        src.seek(src.position() + n * 8);
        src.bits(1);
        src.bit_short();
        src.bits(1);
    } else
        src.bit_short();
    src.bits(4);
    if (v >= 1015)
        src.bits(1);
    DwgBitWriter main, handles;
    DwgBitReader original(body);
    if (v >= 1018) {
        auto count_at = src.position();
        require(src.bit_long() == 0, "Default block is populated");
        copy_bits(main, original, count_at);
        main.bit_long(static_cast<std::int32_t>(members.size()));
        original.seek(src.position());
    }
    copy_bits(main, original, record.main_bits - original.position());
    refs.handle(self);
    refs.handle(self); // NULL and BLOCK.
    original.seek(record.main_bits);
    copy_bits(handles, original, refs.position() - record.main_bits);
    if (v < 1018) {
        require(!refs.handle(self).value && !refs.handle(self).value, "Unexpected default block links");
        handles.absolute_handle(4, members.front());
        handles.absolute_handle(4, members.back());
    } else
        for (auto h : members)
            handles.absolute_handle(3, h);
    original.seek(refs.position());
    auto end = body.size() * 8 - (8 - record.main_bits % 8) % 8;
    copy_bits(handles, original, end - original.position());
    return pack_record(v, main, record.split_field_bit, handles);
}
std::string pack_r12(const NewEntity &entry) {
    auto &e = *entry.primitive;
    auto line = std::get_if<DwgLineGeometry>(&e.geometry);
    auto circle = std::get_if<DwgCircleGeometry>(&e.geometry);
    auto arc = std::get_if<DwgArcGeometry>(&e.geometry);
    auto text = std::get_if<DwgTextGeometry>(&e.geometry);
    auto elevation = line ? 0 : circle ? circle->center.z : arc ? arc->center.z : text->insertion.z;
    unsigned flags = 32 | (e.paper_space ? 64 : 0) | (e.thickness != 0 ? 8 : 0) | (elevation != 0 ? 4 : 0);
    DwgBitWriter w;
    w.bits(8, line ? 1 : circle ? 3 : arc ? 8 : 7);
    w.bits(8, flags);
    w.little_endian(2, 0);
    w.little_endian(2, 0);
    w.little_endian(2, e.normal != Vector3::UNIT_Z ? (text ? 128 : 1) : 0);
    if (e.paper_space)
        w.bits(8, 1);
    if (elevation != 0)
        raw_double(w, elevation);
    if (e.thickness != 0)
        raw_double(w, e.thickness);
    DwgBitWriter h;
    h.absolute_handle(0, entry.h);
    w.bits(8, h.bytes().size() - 1);
    w.bits((h.bytes().size() - 1) * 8, entry.h);
    auto coords = [&](Point3 p, bool z) {
        raw_double(w, p.x);
        raw_double(w, p.y);
        if (z)
            raw_double(w, p.z);
    };
    if (text) {
        coords(text->insertion, false);
        raw_double(w, text->height);
        auto encoded = detail::to_legacy(text->value, "CP1252");
        require(encoded.size() <= 32767, "DWG TEXT too long");
        w.little_endian(2, encoded.size());
        for (unsigned char c : encoded) w.bits(8, c);
    } else if (line) {
        coords(line->start, true);
        coords(line->end, true);
    } else {
        coords(circle ? circle->center : arc->center, false);
        raw_double(w, circle ? circle->radius : arc->radius);
        if (arc) {
            raw_double(w, arc->start_angle);
            raw_double(w, arc->end_angle);
        }
    }
    if (e.normal != Vector3::UNIT_Z)
        coords(e.normal, true);
    std::string bytes = w.bytes();
    bytes.resize(bytes.size() + 2);
    set_integer(bytes, 2, bytes.size(), 2);
    set_crc(bytes, 0, bytes.size() - 2);
    return bytes;
}
std::string create_r12(std::string_view defaults, const std::vector<NewEntity> &entries) {
    std::string entities;
    for (auto &e : entries)
        entities += pack_r12(e);
    auto at = get_u32(defaults, 20);
    require(get_u32(defaults, 24) == at, "Expected empty AC1009 defaults");
    std::string out(defaults);
    out.insert(at, entities);
    auto relocate = [&](std::size_t pos) { set_integer(out, pos, get_u32(out, pos) + entities.size(), 4); };
    for (auto pos : {24, 28, 36})
        relocate(pos);
    for (auto pos : {0x2c, 0x36, 0x40, 0x4a, 0x54, 0x3ef, 0x500, 0x512, 0x522, 0x69f})
        relocate(pos + 6);
    set_crc(out, 0, at - 18);
    auto aux = out.size() - 170;
    for (auto pos : {24, 28, 32})
        relocate(aux + pos);
    for (unsigned i = 0; i < 10; i++)
        relocate(aux + 54 + i * 10);
    relocate(aux + 148);
    set_crc(out, aux + 16, aux + 152);
    return out;
}
void check_geometry(const DwgPrimitive &e) {
    auto finite = [](Point3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
    require(finite(e.normal) && e.normal != Vector3::ZERO && std::isfinite(e.thickness), "Invalid extrusion");
    if (auto i = std::get_if<InsertDefinition>(&e.geometry)) {
        require(!i->block_name.empty() && finite(i->insertion) && finite(i->normal) && i->normal.length() > 0 &&
                finite(i->scale) && i->scale.x && i->scale.y && i->scale.z && std::isfinite(i->rotation) &&
                i->columns == 1 && i->rows == 1, "Invalid DWG INSERT");
        return;
    }
    if (auto face = std::get_if<DwgFaceGeometry>(&e.geometry)) {
        for (auto p : face->corners) require(finite(p), "Invalid 3DFACE");
        return;
    }
    if (auto p = std::get_if<DwgPointGeometry>(&e.geometry)) { require(finite(p->location), "Invalid POINT"); return; }
    if (auto r = std::get_if<DwgRayGeometry>(&e.geometry)) {
        require(finite(r->origin) && finite(r->direction) && r->direction != Vector3::ZERO, "Invalid ray direction");
        return;
    }
    if (auto l = std::get_if<DwgLineGeometry>(&e.geometry)) {
        require(finite(l->start) && finite(l->end), "Invalid LINE");
        return;
    }
    if (auto c = std::get_if<DwgCircleGeometry>(&e.geometry)) {
        require(finite(c->center) && std::isfinite(c->radius) && c->radius > 0, "Invalid CIRCLE");
        return;
    }
    if (auto t = std::get_if<DwgTextGeometry>(&e.geometry)) {
        require(finite(t->insertion) && std::isfinite(t->height) && t->height > 0, "Invalid TEXT");
        detail::utf8_scalars(t->value);
        require(t->value.find(char(0)) == std::string::npos, "NUL in DWG TEXT");
        return;
    }
    auto &a = std::get<DwgArcGeometry>(e.geometry);
    require(finite(a.center) && std::isfinite(a.radius) && a.radius > 0 && std::isfinite(a.start_angle) &&
                std::isfinite(a.end_angle),
            "Invalid ARC");
}
} // namespace

std::string detail::edit_dwg_history_slot(std::string_view file, const DwgR2000Objects &objects,
                                          std::uint64_t h, std::size_t begin, std::size_t end,
                                          std::uint64_t value, bool reference, unsigned handle_code) {
    const auto &r = objects.records.at(h);
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, refs;
        if (reference) {
            copy_bits(main, old, r.main_bits);
            copy_bits(refs, old, begin - old.position());
            refs.absolute_handle(handle_code, value);
            old.seek(end);
        } else {
            copy_bits(main, old, begin);
            main.bit_long(static_cast<std::int32_t>(value));
            old.seek(end);
            copy_bits(main, old, r.main_bits - old.position());
        }
        copy_bits(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8 - old.position());
        return pack_record(v, main, r.split_field_bit, refs);
    });
}
std::string detail::edit_dwg_metadata(std::string_view file, const DwgR2000Objects &objects, std::uint64_t h,
                                      const std::string &value) {
    const auto &r = objects.records.at(h);
    const auto &history = objects.metadata_records.at(h);
    auto v = dxf_version_from_string(file.substr(0, 6));
    DwgBitWriter data;
    require(value.size() <= INT32_MAX, "Metadata too large");
    data.bit_long(value.size());
    for (unsigned char c : value)
        data.bits(8, c);
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body), fresh(data.bytes(), data.position());
        DwgBitWriter main, refs;
        copy_bits(main, old, history.parameters_begin_bit);
        copy_bits(main, fresh, data.position());
        old.seek(history.parameters_end_bit);
        copy_bits(main, old, r.main_bits - old.position());
        copy_bits(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8 - old.position());
        return pack_record(unsigned(v), main, r.split_field_bit, refs);
    });
}
std::string detail::edit_dwg_history_parameters(std::string_view file, const DwgR2000Objects &objects,
                                                std::uint64_t h, const DwgHistoryRecord &value) {
    const auto &r = objects.records.at(h);
    const auto &history = objects.history_records.at(h);
    auto v = dxf_version_from_string(file.substr(0, 6));
    auto data = encode_dwg_history_parameters(value, v);
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body), fresh(data.bytes(), data.position());
        DwgBitWriter main, refs;
        copy_bits(main, old, history.parameters_begin_bit);
        copy_bits(main, fresh, data.position());
        old.seek(history.parameters_end_bit);
        copy_bits(main, old, r.main_bits - old.position());
        copy_bits(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8 - old.position());
        return pack_record(unsigned(v), main, r.split_field_bit, refs);
    });
}
std::string detail::edit_dwg_surface(std::string_view file, const DwgR2000Objects &objects, std::uint64_t h,
                                     const DwgSurface &value) {
    const auto &r = objects.records.at(h);
    const auto &surface = *objects.modelers.at(h).surface;
    auto v = dxf_version_from_string(file.substr(0, 6));
    auto data = encode_dwg_surface(value, v);
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body), fresh(data.bytes(), data.position());
        DwgBitWriter main, refs;
        copy_bits(main, old, surface.parameters_begin_bit);
        copy_bits(main, fresh, data.position());
        old.seek(surface.parameters_end_bit);
        copy_bits(main, old, r.main_bits - old.position());
        copy_bits(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8 - old.position());
        return pack_record(unsigned(v), main, r.split_field_bit, refs);
    });
}
std::string detail::clear_dwg_modeler(std::string_view file, const DwgR2000Objects &objects,
                                      std::uint64_t h) {
    const auto &r = objects.records.at(h);
    const auto &m = objects.modelers.at(h);
    if (m.acis_empty)
        return std::string(file);
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    std::string datastore;
    if (v >= 1027)
        datastore = remove_dwg_acds_body(read_dwg_paged_section(file, "AcDb:AcDsPrototype_1b"), h);
    auto changed = replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, refs;
        if (v >= 1027) {
            copy_bits(main, old, r.ds_data_bit);
            old.bits(1);
            main.bits(1, 0);
            copy_bits(main, old, m.materials_begin_bit - old.position());
        } else {
            copy_bits(main, old, r.geometry_start);
            main.bits(1, 1);
            old.seek(m.payload_end_bit);
            copy_bits(main, old, m.materials_begin_bit - old.position());
        }
        if (v >= 1021)
            main.bit_long(0);
        old.seek(m.materials_end_bit);
        copy_bits(main, old, r.main_bits - old.position());
        copy_bits(refs, old, m.material_handles_begin - old.position());
        old.seek(m.material_handles_end);
        copy_bits(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8 - old.position());
        return pack_record(v, main, r.split_field_bit, refs);
    });
    if (v >= 1027)
        changed = replace_ac18_sections(changed, {{"AcDb:AcDsPrototype_1b", datastore}});
    return changed;
}
std::string detail::edit_dwg_modeler(std::string_view file, const DwgR2000Objects &objects, std::uint64_t h,
                                     const acis::SatDocument &input) {
    const auto &m = objects.modelers.at(h);
    const auto &r = objects.records.at(h);
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    if (v >= 1027) {
        std::string section;
        try {
            section = read_dwg_paged_section(file, "AcDb:AcDsPrototype_1b");
        } catch (const std::invalid_argument &e) {
            if (!m.acis_empty || std::string_view(e.what()) != "DWG section not found")
                throw;
        }
        auto sab = acis::SabWriter::write(input);
        auto changed =
            m.acis_empty ? insert_dwg_acds_body(section, h, sab) : replace_dwg_acds_body(section, h, sab);
        std::string source(file);
        if (m.acis_empty)
            source = replace_geometry_record(file, r, [&](std::string_view bytes) {
                auto body = bytes.substr(r.body_offset, r.body_size);
                DwgBitReader old(body);
                DwgBitWriter main, refs;
                require(r.ds_data_bit < r.geometry_start, "Missing modeler DS flag");
                copy_bits(main, old, r.ds_data_bit);
                old.bits(1);
                main.bits(1, 1);
                copy_bits(main, old, r.main_bits - old.position());
                copy_bits(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8 - old.position());
                return pack_record(v, main, r.split_field_bit, refs);
            });
        return replace_ac18_sections(source, {{"AcDb:AcDsPrototype_1b", std::move(changed)}});
    }
    DwgBitWriter payload;
    bool binary = m.acis_empty ? v >= 1018 : m.acis_version == 2;
    if (m.acis_empty) {
        payload.bits(1, 0);
        payload.bits(1, !binary);
        payload.bit_short(binary ? 2 : 1);
    }
    if (binary) {
        for (auto b : acis::SabWriter::write(input))
            payload.bits(8, b);
    } else {
        auto text = detail::dwg_sat_from_document(input, v);
        for (std::size_t i = 0; i < text.size(); i += 4096) {
            auto n = std::min<std::size_t>(4096, text.size() - i);
            payload.bit_long(static_cast<std::int32_t>(n));
            for (std::size_t j = 0; j < n; ++j)
                payload.bits(8, static_cast<unsigned char>(text[i + j]));
        }
        payload.bit_long(0);
    }
    auto begin = m.acis_empty ? r.geometry_start : m.payload_begin_bit;
    auto end = m.acis_empty ? r.geometry_start + 1 : m.payload_end_bit;
    require(end > begin, "Missing inline modeler payload bounds");
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, refs;
        copy_bits(main, old, begin);
        DwgBitReader fresh(payload.bytes(), payload.position());
        copy_bits(main, fresh, payload.position());
        old.seek(end);
        copy_bits(main, old, r.main_bits - end);
        auto refs_end = body.size() * 8 - (8 - r.main_bits % 8) % 8;
        copy_bits(refs, old, refs_end - r.main_bits);
        return pack_record(v, main, r.split_field_bit, refs);
    });
}
std::string detail::edit_dwg_primitive(std::string_view file, const DwgR2000Objects &objects,
                                       std::uint64_t handle, const DwgPrimitive &e, std::uint64_t layer) {
    check_geometry(e);
    auto r = objects.records.at(handle);
    auto version = unsigned(dxf_version_from_string(file.substr(0, 6)));
    if (version == 1009) {
        std::string out(file);
        require(layer < objects.legacy_symbol_names.at(51).size(), "Unknown legacy layer slot");
        set_integer(out, r.offset + 4, layer, 2);
        auto flags = static_cast<unsigned char>(out[r.offset + 1]);
        auto write_double = [&](std::size_t at, double value) {
            std::uint64_t bits;
            std::memcpy(&bits, &value, 8);
            set_integer(out, at, bits, 8);
        };
        std::size_t at = r.offset + r.geometry_start / 8;
        auto scalar = [&](double value) {
            write_double(at, value);
            at += 8;
        };
        auto point = [&](Point3 p, bool xyz) {
            scalar(p.x);
            scalar(p.y);
            if (xyz)
                scalar(p.z);
        };
        if (auto line = std::get_if<DwgLineGeometry>(&e.geometry)) {
            require(!(flags & 4) || (line->start.z == 0 && line->end.z == 0),
                    "R12 planar LINE has no Z fields");
            point(line->start, !(flags & 4));
            point(line->end, !(flags & 4));
        } else {
            auto circle = std::get_if<DwgCircleGeometry>(&e.geometry);
            auto arc = std::get_if<DwgArcGeometry>(&e.geometry);
            auto center = circle ? circle->center : arc->center;
            require(flags & 4 || center.z == 0, "R12 entity has no elevation field");
            if (flags & 4)
                write_double(r.offset + r.elevation_bit / 8, center.z);
            point(center, false);
            scalar(circle ? circle->radius : arc->radius);
            if (arc) {
                scalar(arc->start_angle);
                scalar(arc->end_angle);
            }
        }
        require(flags & 8 || e.thickness == 0, "R12 entity has no thickness field");
        if (flags & 8)
            write_double(r.offset + r.thickness_bit / 8, e.thickness);
        if (out[r.offset + 6] & 1)
            point(e.normal, true);
        else
            require(e.normal == Vector3::UNIT_Z, "R12 entity has no normal field");
        require(at == r.offset + r.body_size, "Invalid R12 geometry boundary");
        set_crc(out, r.offset, at);
        return out;
    }
    require(objects.symbols.at(layer).type == 51, "Invalid DWG layer");
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, handles;
        copy_bits(main, old, r.geometry_start);
        encode_geometry(main, version, e);
        old.seek(r.geometry_end);
        copy_bits(main, old, r.main_bits - r.geometry_end);
        copy_bits(handles, old, r.layer_bit - r.main_bits);
        auto ref = old.handle(handle);
        if (ref.value == layer) {
            auto end = old.position();
            old.seek(r.layer_bit);
            copy_bits(handles, old, end - r.layer_bit);
        } else
            handles.absolute_handle(5, layer);
        auto padding = (8 - r.main_bits % 8) % 8;
        copy_bits(handles, old, body.size() * 8 - padding - old.position());
        return pack_record(version, main, r.split_field_bit, handles);
    });
}
namespace {
struct OwnershipRecord {
    DwgR2000Record record;
    DwgR2000EntityProperties props;
    OwnershipBits bits;
};
OwnershipRecord ownership_record(std::string_view body, DwgR2000Record r, std::uint64_t h, unsigned v) {
    DwgBitReader main(body), refs(body);
    main.seek(r.handle_end_bit);
    if (v < 1015) {
        skip_entity_prefix(main, body.size() * 8, true, v);
        r.split_field_bit = main.position();
        r.main_bits = main.little_endian(4);
        main.seek(r.handle_end_bit);
    }
    refs.seek(r.main_bits);
    OwnershipRecord result{r, {}, {}};
    result.props = read_entity_properties(main, refs, h, r.main_bits, v, result.record, &result.bits);
    return result;
}
void copy_until(DwgBitWriter &out, DwgBitReader &in, std::size_t end) {
    require(end >= in.position(), "Invalid DWG bit field order");
    copy_bits(out, in, end - in.position());
}
std::string with_owner(std::string_view body, const OwnershipRecord &entry, std::uint64_t h, unsigned v,
                       unsigned mode, std::uint64_t owner, std::uint64_t prev, std::uint64_t next) {
    auto &r = entry.record;
    auto &bits = entry.bits;
    DwgBitReader old(body);
    DwgBitWriter main, refs;
    copy_until(main, old, bits.mode);
    old.bits(2);
    main.bits(2, mode);
    if (v < 1018) {
        copy_until(main, old, bits.links_flag);
        old.bits(1);
        main.bits(1, 0);
    }
    copy_until(main, old, r.main_bits);
    if (!entry.props.mode)
        old.handle(h);
    if (!mode)
        refs.absolute_handle(4, owner);
    if (v < 1018) {
        copy_until(refs, old, bits.links_begin);
        old.seek(bits.links_end);
        refs.absolute_handle(4, prev);
        refs.absolute_handle(4, next);
    }
    copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
    return pack_record(v, main, r.split_field_bit, refs);
}
std::string with_members(std::string_view body, const BlockBits &b, unsigned v,
                         const std::vector<std::uint64_t> &members) {
    require(!b.external, "Cannot move entities into or out of an xref");
    DwgBitWriter main, refs;
    DwgBitReader old(body);
    if (v >= 1018) {
        copy_until(main, old, b.count);
        old.seek(b.after_count);
        main.bit_long(members.size());
    }
    copy_until(main, old, b.split);
    copy_until(refs, old, b.begin);
    old.seek(b.end);
    if (v < 1018) {
        refs.absolute_handle(4, members.empty() ? 0 : members.front());
        refs.absolute_handle(4, members.empty() ? 0 : members.back());
    } else
        for (auto h : members)
            refs.absolute_handle(3, h);
    copy_until(refs, old, body.size() * 8 - (8 - b.split % 8) % 8);
    return pack_record(v, main, b.split_field, refs);
}
} // namespace
std::string detail::move_dwg_entity(std::string_view file, const DwgR2000Objects &objects, std::uint64_t h,
                                    std::uint64_t dest) {
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    auto owner_of = [&](const DwgR2000EntityProperties &p) {
        if (!p.mode)
            return p.owner;
        return p.mode == 1 ? objects.paper_space : objects.model_space;
    };
    auto paged = v >= 1018;
    std::string section;
    if (paged)
        section = read_dwg_paged_section(file, "AcDb:AcDbObjects");
    std::string_view bytes = paged ? std::string_view(section) : file;
    auto body = [&](std::uint64_t id) {
        auto &r = objects.records.at(id);
        return bytes.substr(r.body_offset, r.body_size);
    };
    auto type = objects.records.at(h).type;
    require(type == 1 || type == 7 || type == 8 || type == 15 || type == 16 ||
                type == 17 || type == 18 || type == 19 || type == 29 || type == 30 || objects.modelers.count(h),
            "Unsupported DWG entity move (child records must move with their parent)");
    auto props = ownership_record(body(h), objects.records.at(h), h, v).props;
    auto src = owner_of(props);
    if (src == dest) return std::string(file);
    std::map<std::uint64_t, OwnershipRecord> entities;
    auto entity = [&](std::uint64_t id) -> OwnershipRecord & {
        if (!entities.count(id))
            entities.emplace(id, ownership_record(body(id), objects.records.at(id), id, v));
        return entities.at(id);
    };
    std::map<std::uint64_t, std::string> edits;
    for (auto block : {src, dest}) {
        BlockBits positions;
        auto record = objects.records.at(block);
        auto s = read_symbol(body(block), record, block, v, drawing_code_page(file, v),
                             &positions);
        require(s.type == 49 && !positions.external, "Invalid destination or source block");
        auto members = s.members;
        if (!paged) {
            std::set<std::uint64_t> visited;
            require(bool(s.first) == bool(s.last), "Invalid DWG block endpoints");
            for (auto id = s.first; id;) {
                require(visited.insert(id).second, "DWG member chain cycle");
                auto &ent = entity(id);
                require(owner_of(ent.props) == block, "DWG member chain crosses owners");
                members.push_back(id);
                if (id == s.last)
                    break;
                id = ent.props.next;
                require(id != 0, "Incomplete DWG block chain");
            }
        }
        auto it = std::find(members.begin(), members.end(), h);
        if (block == src) {
            require(it != members.end() && std::count(members.begin(), members.end(), h) == 1,
                    "Missing or duplicate source member");
            members.erase(it);
        } else {
            require(it == members.end(), "Duplicate destination member");
            members.push_back(h);
        }
        edits[block] = with_members(body(block), positions, v, members);
        for (std::size_t i = 0; i < members.size(); i++) {
            auto id = members[i];
            if (paged && id != h)
                continue;
            auto &ent = entity(id);
            auto prev = i ? members[i - 1] : 0, next = i + 1 < members.size() ? members[i + 1] : 0;
            if (id != h && prev == ent.props.previous && next == ent.props.next)
                continue;
            auto mode = id != h                        ? ent.props.mode
                        : block == objects.model_space ? 2u
                        : block == objects.paper_space ? 1u
                                                       : 0u;
            edits[id] = with_owner(body(id), ent, id, v, mode, block, prev, next);
        }
    }
    if (paged) {
        // Original map entries are offsets in the decompressed section. Rebuild
        // the section and its map together, including gaps and opaque records.
        std::map<std::size_t, std::uint64_t> order;
        for (auto &[id, r] : objects.records)
            order.emplace(r.offset, id);
        std::map<std::uint64_t, std::size_t> offsets;
        std::string updated;
        std::size_t cursor = 0;
        for (auto [pos, id] : order) {
            updated.append(bytes.substr(cursor, pos - cursor));
            offsets[id] = updated.size();
            auto &r = objects.records.at(id);
            cursor = r.body_offset + r.body_size + 2;
            auto edit = edits.find(id);
            if (edit != edits.end())
                updated += edit->second;
            else
                updated.append(bytes.substr(pos, cursor - pos));
        }
        updated.append(bytes.substr(cursor));
        auto replace = v == 1021 ? replace_ac21_sections : replace_ac18_sections;
        return replace(file,
                       {{"AcDb:AcDbObjects", std::move(updated)}, {"AcDb:Handles", write_map(offsets)}});
    }
    // Encode all changes against the source. Offset relocation must use the
    // current map after each variable-length replacement.
    std::string out(file);
    for (const auto &edit : edits) {
        auto r = read_dwg_objects(out).records.at(edit.first);
        out = replace_geometry_record(out, r, [&](std::string_view) { return edit.second; });
    }
    return out;
}

namespace {
std::string legacy_jump(std::size_t length, std::uint32_t target) {
    require(length >= 10, "Short AC1009 JUMP slot");
    std::string j(length, 0);
    j[0] = 18;
    set_integer(j, 2, length, 2);
    set_integer(j, 4, target, 4);
    set_crc(j, 0, 8);
    return j;
}
// Append one change stream without moving any existing entity addresses.
std::string append_legacy_extra(std::string_view file, std::string out, std::string live,
                                std::size_t redirect,
                                std::optional<std::pair<std::size_t, std::size_t>> sequence_end = {}) {
    auto extra = get_u32(file, 36), size = get_u32(file, 40) & 0x3fffffff;
    auto insert = extra + size;
    auto block = get_u32(file, 28), end_block = block + (get_u32(file, 32) & 0x3fffffff);
    require(get_u32(file, 24) <= extra && end_block <= extra, "Unsupported AC1009 stream order");
    constexpr unsigned offsets[] = {0x2c, 0x36, 0x40, 0x4a, 0x54, 0x3ef, 0x500, 0x512, 0x522, 0x69f};
    for (auto start : offsets) {
        auto table = get_u32(file, start + 6);
        auto sizes = get_u32(file, start);
        require(table >= insert || table + std::uint64_t(sizes & 65535) * (sizes >> 16) <= extra,
                "AC1009 table overlaps extra stream");
    }
    // A JUMP to the old end means stop. Move that target to the new end.
    // If a path used to fall off the last record, terminate it before the appended data.
    std::vector<std::size_t> end_jumps;
    bool falls_through = false;
    for (auto stream : {std::make_pair(get_u32(file, 20), get_u32(file, 24)),
                        std::make_pair(block, end_block), std::make_pair(extra, insert)}) {
        for (auto pos = stream.first; pos < stream.second;) {
            auto n = get_u32(file, pos) >> 16;
            bool jump = (out[pos] & 127) == 18;
            if (jump && get_u32(out, pos + 4) == (0x80000000u | size))
                end_jumps.push_back(pos);
            if (stream.first == extra && pos + n == insert)
                falls_through = !jump && pos != redirect;
            pos += n;
        }
    }
    auto entry = size + (falls_through ? 10 : 0);
    if (sequence_end) {
        auto [record, field] = *sequence_end;
        // SEQEND stores its parent's stream address, not its object handle.
        set_integer(live, field, 0x80000000u | entry, 4);
        auto length = get_u32(live, record) >> 16;
        set_crc(live, record, record + length - 2);
    }
    auto new_end = entry + live.size();
    require(new_end <= 0xffffff && insert <= file.size() - 170, "AC1009 extra stream too large");
    auto stop = 0x80000000u | new_end;
    for (auto pos : end_jumps) {
        set_integer(out, pos + 4, stop, 4);
        set_crc(out, pos, pos + 8);
    }
    // The appended chain ends with a ten-byte return JUMP.
    if (get_u32(live, live.size() - 6) == (0x80000000u | size))
        live.replace(live.size() - 10, 10, legacy_jump(10, stop));
    if (redirect) {
        auto length = get_u32(file, redirect) >> 16;
        out.replace(redirect, length, legacy_jump(length, 0x80000000u | entry));
    }
    if (falls_through)
        live.insert(0, legacy_jump(10, stop));
    out.insert(insert, live);
    set_integer(out, 40, (get_u32(file, 40) & 0xc0000000u) | new_end, 4);
    auto aux = out.size() - 170;
    require(get_u32(file, file.size() - 22) == file.size() - 170, "Unsupported AC1009 auxiliary header");
    for (unsigned i = 0; i < std::size(offsets); i++) {
        auto field = offsets[i] + 6;
        auto pos = get_u32(file, field);
        if (pos >= insert) {
            set_integer(out, field, pos + live.size(), 4);
            set_integer(out, aux + 54 + i * 10, pos + live.size(), 4);
        }
    }
    set_integer(out, aux + 148, aux, 4);
    set_crc(out, aux + 16, aux + 152);
    set_crc(out, 0, get_u32(file, 20) - 18);
    return out;
}

std::string replace_legacy_entity(std::string_view file, const DwgR2000Record &r, std::string live) {
    set_integer(live, 2, live.size(), 2);
    set_crc(live, 0, live.size() - 2);
    std::string out(file);
    auto old_size = r.body_size + 2;
    if (live.size() == old_size) {
        out.replace(r.offset, old_size, live);
        return out;
    }
    auto block = get_u32(file, 28), extra = get_u32(file, 36);
    auto next = r.offset + old_size;
    std::uint32_t tag = 0;
    if (r.offset >= extra && r.offset < extra + (get_u32(file, 40) & 0x3fffffff)) {
        tag = 0x80000000u;
        next -= extra;
    } else if (r.offset >= block && r.offset < block + (get_u32(file, 32) & 0x3fffffff)) {
        tag = 0x40000000u;
        next -= block;
    }
    require(next <= 0xffffff, "AC1009 JUMP offset overflow");
    live += legacy_jump(10, tag | next);
    return append_legacy_extra(file, std::move(out), std::move(live), r.offset);
}
} // namespace

// Keep existing section-relative addresses. Tombstone the old entity and
// append the live record to EXTRA. For a named block, splice the new record
// immediately before ENDBLK using the native change-stream JUMP encoding.
std::string detail::move_legacy_entity(std::string_view file, const DwgR2000Objects &objects, std::uint64_t h,
                                       std::string_view name) {
    const auto &p = objects.legacy_entity_properties.at(h);
    auto type = objects.records.at(h).type;
    require(type == 1 || type == 3 || type == 7 || type == 8 || type == 14 || type == 19 || type == 21,
            "Unsupported AC1009 move (child records must move with their parent)");
    std::vector<std::uint64_t> group{h};
    if (objects.legacy_sequences.count(h)) {
        group = objects.legacy_sequences.at(h);
        require(group.size() >= 2 && objects.records.at(group.back()).type == 17,
                "Incomplete AC1009 compound sequence");
    }
    auto key = symbol_name_key(name);
    bool model = key == U"*MODEL_SPACE", paper = key == U"*PAPER_SPACE";
    std::size_t marker = 0;
    if (!model && !paper) {
        for (auto &[n, offset] : objects.legacy_block_ends)
            if (symbol_name_key(n) == key) {
                require(!marker, "Ambiguous AC1009 block");
                marker = offset;
            }
        require(marker, "Missing AC1009 block");
    }
    if ((model && p.mode == 2) || (paper && p.mode == 1) ||
        (!p.mode && symbol_name_key(p.legacy_block_name) == key))
        return std::string(file);
    auto extra = get_u32(file, 36), size = get_u32(file, 40) & 0x3fffffff, insert = extra + size;
    auto block = get_u32(file, 28), main_length = get_u32(file, 24) - get_u32(file, 20);
    require(get_u32(file, 24) <= 0xffffff && size <= 0xffffff, "AC1009 JUMP offset overflow");
    std::string out(file);
    std::string live;
    std::optional<std::pair<std::size_t, std::size_t>> sequence_end;
    for (auto id : group) {
        const auto &r = objects.records.at(id);
        const auto &child = objects.legacy_entity_properties.at(id);
        require(child.mode == p.mode && child.legacy_block_name == p.legacy_block_name,
                "AC1009 sequence crosses ownership");
        std::string record(file.substr(r.offset, r.body_size + 2));
        if (static_cast<unsigned char>(record[1]) & 64)
            record[8] = char((record[8] & ~1) | (paper ? 1 : 0));
        else if (paper) {
            record[1] |= 64;
            record.insert(8, 1, 1);
        }
        set_integer(record, 2, record.size(), 2);
        set_crc(record, 0, record.size() - 2);
        out[r.offset] |= static_cast<char>(128);
        set_crc(out, r.offset, r.offset + r.body_size);
        if (r.type == 17) {
            auto field = r.geometry_start / 8 + (record.size() - r.body_size - 2);
            require(field + 4 == record.size() - 2, "Invalid AC1009 SEQEND owner field");
            sequence_end = std::make_pair(live.size(), live.size() + field);
        }
        live += record;
    }
    if (marker) {
        auto length = static_cast<unsigned char>(file[marker + 2]) +
                      (static_cast<unsigned char>(file[marker + 3]) << 8);
        auto next = marker + length;
        live.append(file.substr(marker, length));
        if (marker >= extra && marker < insert) {
            require(next < insert, "Unterminated AC1009 block continuation");
            live += legacy_jump(10, 0x80000000u | (next - extra));
        } else {
            require(next >= block && next - block <= 0xffffff, "AC1009 block offset overflow");
            live += legacy_jump(10, 0x40000000u | (next - block));
        }
    } else
        live += legacy_jump(10, main_length + get_u32(file, 20)); // absolute main end
    return append_legacy_extra(file, std::move(out), std::move(live), marker, sequence_end);
}

std::string detail::set_dwg_entity_display(std::string_view file, const DwgR2000Objects &objects,
                                           std::uint64_t entity, std::optional<Transparency> transparency,
                                           std::optional<double> scale, std::optional<bool> invisible,
                                           std::optional<std::uint8_t> weight) {
    auto version = unsigned(dxf_version_from_string(file.substr(0, 6)));
    const auto &r = objects.records.at(entity);
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, refs;
        if (transparency) {
            copy_until(main, old, r.color_bit);
            auto word = static_cast<std::uint16_t>(old.bit_short());
            const bool explicit_value = *transparency != Transparency::by_layer();
            main.bit_short((word & ~0x2000) | (explicit_value ? 0x2000 : 0));
            if ((word & 0xc000) == 0x8000) {
                // Retain the packed RGB encoding, including its method byte.
                auto start = old.position();
                old.bit_long();
                auto end = old.position();
                old.seek(start);
                copy_until(main, old, end);
            }
            if (word & 0x2000)
                old.bit_long();
            if (explicit_value)
                main.bit_long(transparency->to_alpha_value());
        }
        if (scale) {
            copy_until(main, old, r.linetype_scale_bit);
            old.bit_double();
            main.bit_double(*scale);
        }
        if (invisible) {
            copy_until(main, old, r.visibility_bit);
            auto flags = old.bit_short();
            main.bit_short((flags & ~1) | (*invisible ? 1 : 0));
        }
        if (weight) {
            // The lineweight byte is the last common field before geometry.
            copy_until(main, old, r.geometry_start - 8);
            old.bits(8);
            main.bits(8, *weight);
        }
        copy_until(main, old, r.main_bits);
        copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
        return pack_record(version, main, r.split_field_bit, refs);
    });
}

std::string detail::set_dwg_entity_color(std::string_view file, const DwgR2000Objects &objects,
                                         std::uint64_t entity, Color color) {
    const auto version = unsigned(dxf_version_from_string(file.substr(0, 6)));
    const auto &r = objects.records.at(entity);
    if (version == 1009) {
        std::string live(file.substr(r.offset, r.body_size + 2));
        auto pos = r.color_bit / 8;
        if (live[1] & 1) live.erase(pos, 1);
        if (color == Color::by_layer()) {
            live[1] &= ~1;
        } else {
            auto index = color.approximate_index();
            require(index >= 0 && index <= 255, "Invalid AC1009 entity color");
            live.insert(pos, 1, static_cast<char>(index));
            live[1] |= 1;
        }
        return replace_legacy_entity(file, r, std::move(live));
    }
    return replace_geometry_record(file, r, [&](std::string_view bytes) {
        auto body = bytes.substr(r.body_offset, r.body_size);
        DwgBitReader old(body);
        DwgBitWriter main, refs;
        copy_until(main, old, r.color_bit);
        auto word = static_cast<std::uint16_t>(old.bit_short());
        bool book = version >= 1018 && (word & 0x4000);
        if (version >= 1018) {
            if ((word & 0xc000) == 0x8000) old.bit_long();
            auto rgb = color.to_true_color_value();
            // The following optional transparency field is retained verbatim.
            main.bit_short((word & 0x2000) | (rgb ? 0x8000 : color.approximate_index()));
            if (rgb) main.bit_long(static_cast<std::int32_t>(0xc2000000u | *rgb));
        } else {
            main.bit_short(color.approximate_index());
        }
        copy_until(main, old, r.main_bits);
        if (book) {
            copy_until(refs, old, r.color_handle_bit);
            old.handle(entity);
        }
        copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
        return pack_record(version, main, r.split_field_bit, refs);
    });
}

#include "dwg_layer.inc"

double DwgDocument::drawing_linetype_scale() const {
    const auto file = source_bytes();
    const auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    double scale = 1;
    try {
        if (v == 1009) {
            DwgBitReader reader(file);
            // Fixed header variable in AC1009, immediately after DRAGMODE.
            reader.seek(0x132 * 8);
            scale = reader.raw_double();
        } else {
            std::string storage;
            std::string_view header;
            if (v >= 1018) {
                storage = read_dwg_paged_section(file, "AcDb:Header");
                header = storage;
            } else {
                const auto loc = read_linear_directory(file).sections.at(0);
                header = file.substr(loc.offset, loc.size);
            }
            const auto data = dwg_header_payload(header, v);
            DwgBitReader reader(data);
            reader.seek(variable_position(data, v, HeaderVariable::LinetypeScale));
            scale = reader.bit_double();
        }
    } catch (const std::invalid_argument &) {
        return 1; // A recovered drawing may have geometry without a usable header.
    } catch (const std::out_of_range &) {
        return 1;
    }
    return std::isfinite(scale) && scale > 0 ? scale : 1;
}

#include "dwg_lifecycle.inc"
#include "dwg_read.inc"
#include "dwg_recover.inc"
#include "dwg_text_write.inc"
struct NewBlock {
    std::uint64_t header;
    std::vector<std::uint64_t> members, references;
};

static std::string encode_block_header(unsigned v, const NewBlock &block, std::string_view name,
                                       std::uint64_t control, const char *page) {
    DwgBitWriter main, refs, strings;
    const auto split = start_symbol(main, refs, v, 49, block.header, control);
    symbol_name(main, strings, v, name, page);
    main.bits(5, 0); // Named block, no attributes or XREF.
    main.bit_long(block.members.size());
    vector_bits(main, Point3{});
    symbol_string(main, strings, v, "", page);
    for (std::size_t i = 0; i < block.references.size(); ++i) main.bits(8, 1);
    main.bits(8, 0);
    symbol_string(main, strings, v, "", page);
    main.bit_long(0); // No preview.
    main.bit_short(0); // Inherit drawing units.
    main.bits(1, 1); // Explodable.
    main.bits(8, 0); // Allow nonuniform scaling.
    refs.absolute_handle(5, 0);
    refs.absolute_handle(3, block.header + 1);
    for (auto h : block.members) refs.absolute_handle(3, h);
    refs.absolute_handle(3, block.header + 2);
    for (auto h : block.references) refs.absolute_handle(4, h);
    refs.absolute_handle(5, 0);
    return finish_symbol(main, strings, refs, v, split);
}

// Change only the display flags and CMC in the template’s built-in layer record.
static std::string style_template_layer(std::string_view body, const DwgR2000Record &r,
                                        const DwgLayerDefinition &layer, unsigned version, std::uint64_t linetype) {
    DwgBitReader old(body);
    DwgBitWriter main, refs;
    copy_until(main, old, r.layer_flags_bit);
    const auto flags = old.bit_short();
    main.bit_short((flags & ~2) | (layer.off ? 2 : 0));
    read_packed_cm_color(old, [] { return std::string{}; });
    write_layer_cmc(main, layer.color);
    copy_until(main, old, r.main_bits);
    copy_until(refs, old, r.linetype_bit);
    old.handle(16);
    refs.absolute_handle(5, linetype);
    copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
    return pack_record(version, main, r.split_field_bit, refs);
}

static std::string create_from_defaults(DxfVersion version, std::string_view defaults,
                                        const std::vector<DwgPrimitive> &entities,
                                        const std::vector<std::string> &block_names = {},
                                        const std::vector<DwgLayerDefinition> &layer_defs = {},
                                        const std::map<std::string, std::vector<double>> &patterns = {}) {
    auto v = unsigned(version);
    if (entities.empty() && block_names.empty() && layer_defs.empty() && patterns.empty())
        return std::string(defaults);
    std::vector<NewEntity> entries;
    std::vector<std::uint64_t> space[2];
    // Default documents reserve all handles below 0x100 and use HANDSEED 0x10000000.
    require(entities.size() <= 0x10000000 - 0x100, "Too many DWG entities");
    require(block_names.empty() || v == 1032, "Named DWG blocks require R2018");
    require(block_names.size() <= (0x10000000 - 0x100 - entities.size()) / 3, "Too many DWG blocks");
    std::map<std::string, NewBlock> blocks;
    std::set<std::u32string> names;
    std::vector<std::uint64_t> headers;
    for (const auto &name : block_names) {
        require(!name.empty() && name.front() != '*' && names.insert(detail::symbol_name_key(name)).second,
                "Invalid or duplicate block name");
        auto header = 0x100 + entities.size() + 3 * blocks.size();
        blocks.emplace(name, NewBlock{header, {}, {}});
        headers.push_back(header);
    }
    std::map<std::string, std::uint64_t> layers{{"0", 16}};
    std::vector<std::uint64_t> layer_ids;
    std::set<std::u32string> layer_names;
    const DwgLayerDefinition *zero = nullptr;
    auto next_layer = 0x100 + entities.size() + 3 * blocks.size();
    require(layer_defs.size() <= 0x10000000 - next_layer, "Too many DWG layers");
    for (const auto &layer : layer_defs) {
        require(!layer.name.empty() && layer_names.insert(detail::symbol_name_key(layer.name)).second &&
                layer.color.rgb().has_value(), "Invalid or duplicate DWG layer");
        if (layer.name == "0") zero = &layer;
        else {
            layers.emplace(layer.name, next_layer);
            layer_ids.push_back(next_layer++);
        }
    }
    std::string seeded;
    for (auto &e : entities) {
        check_geometry(e);
        require(v != 1009 || (e.geometry.index() < 4 && e.layer == "0" && e.color == Color::by_layer()),
                "Selected primitive or style requires R13 or newer");
        if (!layers.count(e.layer)) {
            std::uint64_t id = 0;
            seeded = detail::add_dwg_layer(defaults, read_dwg_objects(defaults), e.layer, 7, id);
            defaults = seeded;
            layers.emplace(e.layer, id);
        }
        auto h = 0x100 + entries.size();
        auto owner = blocks.find(e.block_name);
        require(e.block_name.empty() || (owner != blocks.end() && !e.paper_space), "Unknown block owner");
        std::uint64_t target = 0;
        if (auto i = std::get_if<InsertDefinition>(&e.geometry)) {
            auto it = blocks.find(i->block_name);
            require(it != blocks.end(), "Unknown INSERT block");
            it->second.references.push_back(h);
            target = it->second.header;
        }
        auto &members = owner == blocks.end() ? space[e.paper_space] : owner->second.members;
        auto prev = members.empty() ? 0 : members.back();
        if (prev)
            entries.at(prev - 0x100).next = h;
        members.push_back(h);
        entries.push_back({h, prev, 0, &e, owner == blocks.end() ? 0 : owner->second.header, target});
    }
    if (v == 1009)
        return create_r12(defaults, entries);
    auto original = read_dwg_objects(defaults);
    std::map<std::u32string,std::uint64_t> linetypes;
    for (const auto &[h,symbol] : original.symbols)
        if (symbol.type == 57) linetypes.emplace(detail::symbol_name_key(symbol.name), h);
    require(patterns.size() <= 0x10000000 - next_layer, "Too many DWG linetypes");
    std::vector<std::uint64_t> type_ids;
    for (const auto &[name,segments] : patterns) {
        require(!name.empty() && segments.size() <= 255 &&
                linetypes.emplace(detail::symbol_name_key(name), next_layer).second, "Invalid or duplicate DWG linetype");
        type_ids.push_back(next_layer++);
    }
    const auto type_handle = [&](const std::string &name) {
        auto it = linetypes.find(detail::symbol_name_key(name));
        require(it != linetypes.end(), "Unknown DWG linetype");
        return it->second;
    };
    auto paged = v >= 1018;
    std::string storage;
    std::string_view source = defaults;
    DwgR2000Index idx;
    std::size_t start = 0, end = 0;
    if (paged) {
        storage = read_dwg_paged_section(defaults, "AcDb:AcDbObjects");
        source = storage;
        end = source.size();
    } else {
        idx = read_dwg_linear_index(defaults);
        end = idx.sections.at(2).offset;
    }
    std::map<std::size_t, std::uint64_t> order;
    for (auto &[h, r] : original.records)
        order.emplace(r.offset, h);
    if (!paged)
        start = order.begin()->first;
    std::string bytes;
    std::size_t cursor = start;
    std::map<std::uint64_t, std::size_t> offsets;
    std::uint64_t block_control = 0, layer_control = 0, type_control = 0;
    for (auto [offset, h] : order) {
        auto r = original.records.at(h);
        bytes.append(source.substr(cursor, offset - cursor));
        offsets[h] = start + bytes.size();
        auto length = r.body_offset + r.body_size + 2 - r.offset;
        if ((h == 21 || h == 24) && !space[h == 24].empty()) {
            require(r.type == 49, "Invalid default block record");
            bytes += populate_block(v, source.substr(r.body_offset, r.body_size), r, h, space[h == 24]);
        } else if (r.type == 56 && !type_ids.empty()) {
            type_control = h;
            bytes += extend_symbol_control(source.substr(r.body_offset, r.body_size), r, v, h, type_ids);
        } else if (r.type == 50 && !layer_ids.empty()) {
            layer_control = h;
            bytes += extend_symbol_control(source.substr(r.body_offset, r.body_size), r, v, h, layer_ids);
        } else if (h == layers.at("0") && zero) {
            bytes += style_template_layer(source.substr(r.body_offset, r.body_size), r, *zero, v, type_handle(zero->linetype));
        } else if (r.type == 48 && !blocks.empty()) {
            block_control = h;
            bytes += extend_symbol_control(source.substr(r.body_offset, r.body_size), r, v, h, headers);
        } else
            bytes.append(source.substr(offset, length));
        cursor = offset + length;
    }
    std::uint64_t style = 0;
    for (const auto &[id, symbol] : original.symbols)
        if (symbol.type == 53 && detail::symbol_name_key(symbol.name) == U"STANDARD") style = id;
    require(style, "Missing default text style");
    const auto page = drawing_code_page(defaults, v);
    for (const auto &e : entries) {
        require(offsets.emplace(e.h, start + bytes.size()).second, "Default DWG handle conflict");
        const auto &name = e.primitive->linetype;
        const auto key = detail::symbol_name_key(name);
        const unsigned mode = key.empty() || key == U"BYLAYER" ? 0 : key == U"BYBLOCK" ? 1 : key == U"CONTINUOUS" ? 2 : 3;
        require(!mode || v >= 1015, "Explicit linetype requires R2000 or newer");
        require(std::isfinite(e.primitive->linetype_scale) && e.primitive->linetype_scale > 0, "Invalid linetype scale");
        bytes += new_record(v, e, style, page, 0, layers.at(e.primitive->layer), nullptr, mode,
                            mode == 3 ? type_handle(name) : 0);
    }
    if (!patterns.empty()) require(type_control, "Missing DWG linetype control");
    for (const auto &[name,segments] : patterns) {
        double length = 0;
        for (const auto dash : segments) length += std::abs(dash);
        require(std::isfinite(length), "Invalid DWG linetype pattern");
        auto h = type_handle(name);
        offsets.emplace(h,start + bytes.size());
        bytes += encode_linetype(v,h,type_control,name,"",segments,length,page);
    }
    if (!layer_ids.empty()) {
        require(layer_control, "Invalid drawing template layer table");
        for (const auto &layer : layer_defs) {
            if (layer.name == "0") continue;
            auto h = layers.at(layer.name);
            offsets.emplace(h, start + bytes.size());
            bytes += encode_layer(v, h, layer_control, type_handle(layer.linetype), layer.name, page, layer.color, 31, layer.off);
        }
    }
    if (!blocks.empty()) require(block_control, "Missing block table control");
    const DwgPrimitive marker{DwgLineGeometry{}};
    for (const auto &[name, block] : blocks) {
        auto id = block.header;
        offsets.emplace(id, start + bytes.size());
        bytes += encode_block_header(v, block, name, block_control, page);
        for (unsigned type : {4u, 5u}) {
            auto h = id + type - 3;
            offsets.emplace(h, start + bytes.size());
            bytes += new_record(v, {h, 0, 0, &marker, id, 0, type, name}, style, page);
        }
    }
    bytes.append(source.substr(cursor, end - cursor));
    auto map = write_map(offsets);
    if (paged) {
        auto free_space = read_dwg_paged_section(defaults, "AcDb:ObjFreeSpace");
        set_integer(free_space, 4, offsets.size(), 4);
        auto replace = v == 1021 ? detail::replace_ac21_sections : detail::replace_ac18_sections;
        return replace(defaults, {{"AcDb:AcDbObjects", std::move(bytes)},
                                  {"AcDb:Handles", std::move(map)},
                                  {"AcDb:ObjFreeSpace", std::move(free_space)}});
    }
    auto old_map = idx.sections.at(2);
    auto old_tail = old_map.offset + old_map.size;
    std::string out(defaults.substr(0, start));
    out += bytes;
    auto new_map = out.size();
    out += map;
    auto tail = out.size();
    out += defaults.substr(old_tail);
    auto relocate = [&](std::size_t pos) { return pos >= old_tail ? tail + (pos - old_tail) : pos; };
    for (std::size_t i = 0; i < idx.sections.size(); i++) {
        auto field = 25 + i * 9;
        unsigned id = static_cast<unsigned char>(out[field]);
        auto loc = idx.sections.at(id);
        set_integer(out, field + 1, id == 2 ? new_map : relocate(loc.offset), 4);
        if (id == 2)
            set_integer(out, field + 5, map.size(), 4);
        if (id == 3)
            set_integer(out, relocate(loc.offset) + 4, offsets.size(), 4);
    }
    set_integer(out, 13, relocate(get_u32(defaults, 13)), 4);
    set_crc(out, 0, 25 + 9 * idx.sections.size());
    return out;
}

std::string create_dwg(DxfVersion version, const std::vector<DwgPrimitive> &entities) {
    return create_from_defaults(version, standard_drawing(version), entities);
}

std::string create_dwg(DwgTemplate drawing_template, const std::vector<DwgPrimitive> &entities) {
    return create_dwg(drawing_template, entities, {});
}

std::string create_dwg(DwgTemplate drawing_template, const std::vector<DwgPrimitive> &entities,
                       const std::vector<std::string> &block_names) {
    return create_dwg(drawing_template, entities, block_names, {});
}

std::string create_dwg(DwgTemplate drawing_template, const std::vector<DwgPrimitive> &entities,
                       const std::vector<std::string> &block_names, const std::vector<DwgLayerDefinition> &layers) {
    return create_dwg(drawing_template, entities, block_names, layers, {});
}

std::string create_dwg(DwgTemplate drawing_template, const std::vector<DwgPrimitive> &entities,
                       const std::vector<std::string> &block_names, const std::vector<DwgLayerDefinition> &layers,
                       const std::map<std::string, std::vector<double>> &linetypes) {
    require(drawing_template == DwgTemplate::R2018Inches, "Unknown DWG drawing template");
    return create_from_defaults(DxfVersion::AC1032, byte_view(blank_AC1032_inches), entities, block_names, layers, linetypes);
}

} // namespace libopencad
