// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/vba.rs container and directory codecs.
#include <libopencad/vba.hpp>

#include <algorithm>
#include <cstring>

namespace libopencad {
namespace {

std::optional<std::uint16_t> read_u16(const std::uint8_t *data, std::size_t size, std::size_t offset) {
    if (offset + 2 > size)
        return std::nullopt;
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::optional<std::uint32_t> read_u32(const std::uint8_t *data, std::size_t size, std::size_t offset) {
    if (offset + 4 > size)
        return std::nullopt;
    return static_cast<std::uint32_t>(data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) |
                                      (data[offset + 3] << 24));
}

void append_u16(std::vector<std::uint8_t> &out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void append_u32(std::vector<std::uint8_t> &out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 24));
}

void append_windows_1252(std::string &out, std::uint8_t byte) {
    static constexpr char32_t high[32] = {
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
    };
    char32_t cp = byte;
    if (byte >= 0x80 && byte <= 0x9F)
        cp = high[byte - 0x80];
    if (cp <= 0x7F)
        out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string decode_code_page(const std::uint8_t *data, std::size_t size, std::uint16_t code_page) {
    if (code_page == 65001)
        return std::string(reinterpret_cast<const char *>(data), size);
    std::string out;
    out.reserve(size);
    for (std::size_t i = 0; i < size; ++i)
        append_windows_1252(out, data[i]);
    return out;
}

std::vector<std::uint8_t> encode_code_page(std::string_view value, std::uint16_t) {
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

std::string decode_utf16(const std::uint8_t *data, std::size_t size) {
    std::u16string units;
    for (std::size_t i = 0; i + 1 < size; i += 2) {
        auto unit = static_cast<char16_t>(data[i] | (data[i + 1] << 8));
        if (unit == 0)
            break;
        units.push_back(unit);
    }
    std::string out;
    for (char16_t u : units) {
        if (u < 0x80)
            out.push_back(static_cast<char>(u));
        else if (u < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (u >> 6)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (u >> 12)));
            out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        }
    }
    return out;
}

bool is_ansi_record(std::uint16_t id) {
    return id == 0x0004 || id == 0x0005 || id == 0x0006 || id == 0x000C || id == 0x0016 || id == 0x0019 ||
           id == 0x001A || id == 0x001C || id == 0x003D;
}
bool is_unicode_record(std::uint16_t id) {
    return id == 0x0032 || id == 0x003C || id == 0x003E || id == 0x0040 || id == 0x0047 || id == 0x0048;
}
bool is_u16_record(std::uint16_t id) { return id == 0x0003 || id == 0x000F || id == 0x0013 || id == 0x002C; }
bool is_u32_record(std::uint16_t id) {
    return id == 0x0001 || id == 0x0002 || id == 0x0007 || id == 0x0008 || id == 0x0014 || id == 0x001E ||
           id == 0x0031;
}
bool is_marker_record(std::uint16_t id) {
    return id == 0x0010 || id == 0x0021 || id == 0x0022 || id == 0x0025 || id == 0x0028 || id == 0x002B;
}

std::optional<std::vector<std::uint8_t>> decompress_chunk(const std::uint8_t *data, std::size_t size) {
    std::vector<std::uint8_t> output;
    std::size_t offset = 0;
    while (offset < size && output.size() < 4096) {
        auto flags = data[offset++];
        for (int bit = 0; bit < 8; ++bit) {
            if (offset >= size || output.size() >= 4096)
                break;
            if ((flags & (1 << bit)) == 0) {
                output.push_back(data[offset++]);
                continue;
            }
            auto token = read_u16(data, size, offset);
            if (!token)
                return std::nullopt;
            offset += 2;
            auto current = output.size();
            int offset_bits = 4;
            while ((std::size_t{1} << offset_bits) < current)
                ++offset_bits;
            auto length_mask = 0xFFFFu >> offset_bits;
            auto length = (*token & length_mask) + 3;
            auto distance = (*token >> (16 - offset_bits)) + 1;
            if (static_cast<std::size_t>(distance) > output.size())
                return std::nullopt;
            for (std::size_t n = 0; n < length && output.size() < 4096; ++n)
                output.push_back(output[output.size() - distance]);
        }
    }
    return output;
}

struct RawRecord {
    std::uint16_t id = 0;
    std::vector<std::uint8_t> data;
};

std::optional<std::vector<RawRecord>> raw_directory_records(const std::uint8_t *data, std::size_t size) {
    std::vector<RawRecord> records;
    std::size_t offset = 0;
    while (offset < size) {
        bool all_zero = true;
        for (std::size_t i = offset; i < size; ++i)
            if (data[i] != 0) {
                all_zero = false;
                break;
            }
        if (all_zero)
            break;
        auto id = read_u16(data, size, offset);
        auto rec_size = read_u32(data, size, offset + 2);
        if (!id || !rec_size)
            return std::nullopt;
        offset += 6;
        auto actual = *id == 0x0009 ? *rec_size + 2 : *rec_size;
        if (offset + actual > size)
            return std::nullopt;
        RawRecord rec;
        rec.id = *id;
        rec.data.assign(data + offset, data + offset + actual);
        records.push_back(std::move(rec));
        offset += actual;
    }
    return records;
}

VbaDirectoryRecord decode_directory_record(RawRecord record, std::uint16_t code_page) {
    VbaDirectoryRecord out;
    out.id = record.id;
    if (record.id == 0x0009 && record.data.size() >= 6) {
        out.value.kind = VbaDirectoryValue::Kind::ProjectVersion;
        out.value.major = read_u32(record.data.data(), record.data.size(), 0).value_or(0);
        out.value.minor = read_u16(record.data.data(), record.data.size(), 4).value_or(0);
    } else if (is_ansi_record(record.id)) {
        auto encoded = record.data;
        auto bytes = encoded;
        if (!bytes.empty() && bytes.back() == 0)
            bytes.pop_back();
        out.value.kind = VbaDirectoryValue::Kind::AnsiString;
        out.value.value = decode_code_page(bytes.data(), bytes.size(), code_page);
        out.value.original_value = out.value.value;
        out.value.original_code_page = code_page;
        out.value.encoded = std::move(encoded);
    } else if (is_unicode_record(record.id)) {
        out.value.kind = VbaDirectoryValue::Kind::UnicodeString;
        out.value.value = decode_utf16(record.data.data(), record.data.size());
        out.value.original_value = out.value.value;
        out.value.encoded = record.data;
    } else if (is_u16_record(record.id) && record.data.size() == 2) {
        out.value.kind = VbaDirectoryValue::Kind::U16;
        out.value.u16 = read_u16(record.data.data(), record.data.size(), 0).value_or(0);
    } else if (is_u32_record(record.id) && record.data.size() == 4) {
        out.value.kind = VbaDirectoryValue::Kind::U32;
        out.value.u32 = read_u32(record.data.data(), record.data.size(), 0).value_or(0);
    } else if (is_marker_record(record.id) && record.data.empty()) {
        out.value.kind = VbaDirectoryValue::Kind::Marker;
    } else {
        out.value.kind = VbaDirectoryValue::Kind::Binary;
        out.value.encoded = record.data;
    }
    return out;
}

std::vector<std::uint8_t> encode_directory_value(const VbaDirectoryValue &value, std::uint16_t code_page) {
    switch (value.kind) {
    case VbaDirectoryValue::Kind::Marker:
        return {};
    case VbaDirectoryValue::Kind::U16: {
        std::vector<std::uint8_t> out;
        append_u16(out, value.u16);
        return out;
    }
    case VbaDirectoryValue::Kind::U32: {
        std::vector<std::uint8_t> out;
        append_u32(out, value.u32);
        return out;
    }
    case VbaDirectoryValue::Kind::ProjectVersion: {
        std::vector<std::uint8_t> out;
        append_u32(out, value.major);
        append_u16(out, value.minor);
        return out;
    }
    case VbaDirectoryValue::Kind::AnsiString:
        if (value.value == value.original_value && code_page == value.original_code_page)
            return value.encoded;
        {
            auto out = encode_code_page(value.value, code_page);
            out.push_back(0);
            return out;
        }
    case VbaDirectoryValue::Kind::UnicodeString:
        if (value.value == value.original_value)
            return value.encoded;
        {
            std::vector<std::uint8_t> out;
            for (unsigned char c : value.value) {
                append_u16(out, c);
            }
            append_u16(out, 0);
            return out;
        }
    case VbaDirectoryValue::Kind::Binary:
        return value.encoded;
    }
    return {};
}

bool same_records(const std::vector<VbaDirectoryRecord> &a, const std::vector<VbaDirectoryRecord> &b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id || a[i].value.kind != b[i].value.kind)
            return false;
        if (a[i].value.value != b[i].value.value || a[i].value.u16 != b[i].value.u16 ||
            a[i].value.u32 != b[i].value.u32)
            return false;
    }
    return true;
}

} // namespace

std::optional<std::vector<std::uint8_t>> vba_decompress_container(const std::uint8_t *data, std::size_t size) {
    if (size == 0 || data[0] != 1)
        return std::nullopt;
    std::size_t offset = 1;
    std::vector<std::uint8_t> output;
    while (offset < size) {
        auto chunk_start = offset;
        auto header = read_u16(data, size, offset);
        if (!header)
            return std::nullopt;
        offset += 2;
        if (((*header >> 12) & 0x7) != 0x3)
            return std::nullopt;
        auto chunk_size = (*header & 0x0FFF) + 3;
        if (chunk_size < 3 || chunk_size > 4098)
            return std::nullopt;
        if (chunk_start + chunk_size > size)
            return std::nullopt;
        const auto *chunk = data + offset;
        auto chunk_len = chunk_start + chunk_size - offset;
        if ((*header & 0x8000) == 0)
            output.insert(output.end(), chunk, chunk + chunk_len);
        else {
            auto decoded = decompress_chunk(chunk, chunk_len);
            if (!decoded)
                return std::nullopt;
            output.insert(output.end(), decoded->begin(), decoded->end());
        }
        offset = chunk_start + chunk_size;
    }
    return output;
}

std::vector<std::uint8_t> vba_compress_container(const std::uint8_t *data, std::size_t size) {
    std::vector<std::uint8_t> output{1};
    for (std::size_t i = 0; i < size; i += 4096) {
        auto n = std::min<std::size_t>(4096, size - i);
        auto header = static_cast<std::uint16_t>(0x3000 | (n ? n - 1 : 0));
        append_u16(output, header);
        output.insert(output.end(), data + i, data + i + n);
    }
    return output;
}

std::optional<VbaDirectoryStream> VbaDirectoryStream::decode(const std::uint8_t *data, std::size_t size) {
    auto decompressed = vba_decompress_container(data, size);
    if (!decompressed)
        return std::nullopt;
    auto raw = raw_directory_records(decompressed->data(), decompressed->size());
    if (!raw)
        return std::nullopt;
    std::uint16_t code_page = 1252;
    for (const auto &record : *raw) {
        if (record.id == 0x0003)
            code_page = read_u16(record.data.data(), record.data.size(), 0).value_or(1252);
    }
    VbaDirectoryStream out;
    out.code_page = code_page;
    out.original_code_page = code_page;
    for (auto &record : *raw)
        out.records.push_back(decode_directory_record(std::move(record), code_page));
    out.original_records = out.records;
    out.compressed.assign(data, data + size);
    return out;
}

std::vector<std::uint8_t> VbaDirectoryStream::encode() const {
    if (code_page == original_code_page && same_records(records, original_records))
        return compressed;
    std::vector<std::uint8_t> bytes;
    for (const auto &record : records) {
        append_u16(bytes, record.id);
        if (record.value.kind == VbaDirectoryValue::Kind::ProjectVersion) {
            append_u32(bytes, 4);
            append_u32(bytes, record.value.major);
            append_u16(bytes, record.value.minor);
        } else {
            auto data = encode_directory_value(record.value, code_page);
            append_u32(bytes, static_cast<std::uint32_t>(data.size()));
            bytes.insert(bytes.end(), data.begin(), data.end());
        }
    }
    return vba_compress_container(bytes.data(), bytes.size());
}

std::vector<VbaModuleDescriptor> VbaDirectoryStream::module_descriptors() const {
    std::vector<VbaModuleDescriptor> output;
    std::string name, stream_name;
    std::uint32_t source_offset = 0;
    auto directory_string = [](const VbaDirectoryValue &value) -> std::optional<std::string> {
        if (value.kind == VbaDirectoryValue::Kind::AnsiString ||
            value.kind == VbaDirectoryValue::Kind::UnicodeString)
            return value.value;
        return std::nullopt;
    };
    for (const auto &record : records) {
        switch (record.id) {
        case 0x0019:
        case 0x0047:
            if (auto v = directory_string(record.value))
                name = *v;
            break;
        case 0x001A:
        case 0x0032:
            if (auto v = directory_string(record.value))
                stream_name = *v;
            break;
        case 0x0031:
            if (record.value.kind == VbaDirectoryValue::Kind::U32)
                source_offset = record.value.u32;
            break;
        case 0x002B:
            if (!stream_name.empty()) {
                VbaModuleDescriptor d;
                d.name = name;
                d.stream_name = stream_name;
                d.source_offset = source_offset;
                d.code_page = code_page;
                output.push_back(std::move(d));
            }
            name.clear();
            stream_name.clear();
            source_offset = 0;
            break;
        default:
            break;
        }
    }
    return output;
}

std::optional<VbaModuleStream> VbaModuleStream::decode(const std::uint8_t *data, std::size_t size,
                                                       std::uint32_t source_offset, std::uint16_t code_page) {
    if (source_offset > size)
        return std::nullopt;
    auto source_bytes = vba_decompress_container(data + source_offset, size - source_offset);
    if (!source_bytes)
        return std::nullopt;
    VbaModuleStream out;
    out.source_offset = source_offset;
    out.code_page = code_page;
    out.performance_cache.assign(data, data + source_offset);
    out.source = decode_code_page(source_bytes->data(), source_bytes->size(), code_page);
    out.original_source_offset = source_offset;
    out.original_code_page = code_page;
    out.original_source = out.source;
    out.compressed_source.assign(data + source_offset, data + size);
    return out;
}

std::vector<std::uint8_t> VbaModuleStream::encode() const {
    std::vector<std::uint8_t> output = performance_cache;
    auto source_off = static_cast<std::size_t>(source_offset);
    if (output.size() < source_off)
        output.resize(source_off, 0);
    else if (output.size() > source_off)
        output.resize(source_off);
    if (source == original_source && code_page == original_code_page &&
        source_offset == original_source_offset)
        output.insert(output.end(), compressed_source.begin(), compressed_source.end());
    else {
        auto encoded = encode_code_page(source, code_page);
        auto compressed = vba_compress_container(encoded.data(), encoded.size());
        output.insert(output.end(), compressed.begin(), compressed.end());
    }
    return output;
}

} // namespace libopencad
