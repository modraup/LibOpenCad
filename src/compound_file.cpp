// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/compound_file.rs OLE CFB codec.
#include <libopencad/compound_file.hpp>

#include "dxf_code_page.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_set>

namespace libopencad {
namespace {

constexpr std::uint32_t kEndOfChain = 0xFFFFFFFE;
constexpr std::uint32_t kFreeSector = 0xFFFFFFFF;
constexpr std::uint32_t kFatSector = 0xFFFFFFFD;
constexpr std::uint32_t kDifatSector = 0xFFFFFFFC;
constexpr std::uint8_t kCfbMagic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

std::optional<std::uint16_t> read_u16(const std::uint8_t *data, std::size_t size, std::size_t offset) {
    if (offset + 2 > size)
        return std::nullopt;
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}
std::optional<std::uint32_t> read_u32(const std::uint8_t *data, std::size_t size, std::size_t offset) {
    if (offset + 4 > size)
        return std::nullopt;
    return data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24);
}
std::optional<std::uint64_t> read_u64(const std::uint8_t *data, std::size_t size, std::size_t offset) {
    auto lo = read_u32(data, size, offset);
    auto hi = read_u32(data, size, offset + 4);
    if (!lo || !hi)
        return std::nullopt;
    return static_cast<std::uint64_t>(*lo) | (static_cast<std::uint64_t>(*hi) << 32);
}
void write_u16(std::uint8_t *data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}
void write_u32(std::uint8_t *data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    data[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    data[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}
void write_u64(std::uint8_t *data, std::size_t offset, std::uint64_t value) {
    write_u32(data, offset, static_cast<std::uint32_t>(value));
    write_u32(data, offset + 4, static_cast<std::uint32_t>(value >> 32));
}

std::size_t div_ceil(std::size_t n, std::size_t d) { return d ? (n + d - 1) / d : 0; }

struct DirectoryEntry {
    std::string name;
    std::uint8_t kind = 0;
    std::uint32_t left = kFreeSector;
    std::uint32_t right = kFreeSector;
    std::uint32_t child = kFreeSector;
    std::array<std::uint8_t, 16> class_id{};
    std::uint32_t state_bits = 0;
    std::uint64_t creation_time = 0;
    std::uint64_t modified_time = 0;
    std::uint32_t start = 0;
    std::uint64_t size = 0;
};

struct FlatEntry {
    std::string name;
    std::uint8_t kind = 0;
    std::array<std::uint8_t, 16> class_id{};
    std::uint32_t state_bits = 0;
    std::uint64_t creation_time = 0;
    std::uint64_t modified_time = 0;
    std::uint8_t color = 1;
    std::uint32_t left = kFreeSector;
    std::uint32_t right = kFreeSector;
    std::uint32_t child = kFreeSector;
    std::vector<std::uint8_t> stream;
    std::uint32_t start = kEndOfChain;
    std::uint64_t size = 0;
};

std::vector<std::uint8_t> read_chain(std::uint32_t start, const std::vector<std::uint32_t> &allocation,
                                     std::size_t limit,
                                     const std::function<std::optional<std::vector<std::uint8_t>>(std::uint32_t)> &read) {
    std::vector<std::uint8_t> output;
    std::unordered_set<std::uint32_t> visited;
    auto current = start;
    while (current < kDifatSector && visited.size() < limit && visited.insert(current).second) {
        auto bytes = read(current);
        if (!bytes)
            break;
        output.insert(output.end(), bytes->begin(), bytes->end());
        current = current < allocation.size() ? allocation[current] : kEndOfChain;
    }
    return output;
}

std::uint32_t allocate_chain(const std::vector<std::uint8_t> &data, std::size_t sector_size,
                             std::vector<std::vector<std::uint8_t>> &sectors, std::vector<std::uint32_t> &fat) {
    if (data.empty())
        return kEndOfChain;
    auto start = static_cast<std::uint32_t>(sectors.size());
    auto count = div_ceil(data.size(), sector_size);
    for (std::size_t index = 0; index < count; ++index) {
        auto offset = index * sector_size;
        auto end = std::min(offset + sector_size, data.size());
        std::vector<std::uint8_t> sector(sector_size, 0);
        std::copy(data.begin() + static_cast<std::ptrdiff_t>(offset),
                  data.begin() + static_cast<std::ptrdiff_t>(end), sector.begin());
        sectors.push_back(std::move(sector));
        fat.push_back(index + 1 == count ? kEndOfChain : start + static_cast<std::uint32_t>(index) + 1);
    }
    return start;
}

std::pair<std::size_t, std::size_t> allocation_sector_counts(std::size_t non_allocation, std::size_t fat_capacity,
                                                             std::size_t difat_capacity) {
    auto fat_count = std::max<std::size_t>(div_ceil(non_allocation, fat_capacity), 1);
    auto difat_count = div_ceil(fat_count > 109 ? fat_count - 109 : 0, difat_capacity);
    for (;;) {
        auto total = non_allocation + fat_count + difat_count;
        auto next_fat = std::max<std::size_t>(div_ceil(total, fat_capacity), 1);
        auto next_difat = div_ceil(next_fat > 109 ? next_fat - 109 : 0, difat_capacity);
        if (next_fat == fat_count && next_difat == difat_count)
            return {fat_count, difat_count};
        fat_count = next_fat;
        difat_count = next_difat;
    }
}

std::uint32_t build_directory_tree(std::vector<FlatEntry> &flat, const std::vector<std::uint32_t> &entries) {
    auto build = [&](auto &&self, const std::vector<std::uint32_t> &items, std::size_t depth)
        -> std::pair<std::uint32_t, std::size_t> {
        if (items.empty())
            return {kFreeSector, depth ? depth - 1 : 0};
        auto middle = items.size() / 2;
        auto index = items[middle];
        auto left = self(self, std::vector<std::uint32_t>(items.begin(), items.begin() + middle), depth + 1);
        auto right = self(self, std::vector<std::uint32_t>(items.begin() + middle + 1, items.end()), depth + 1);
        flat[index].left = left.first;
        flat[index].right = right.first;
        return {index, std::max(depth, std::max(left.second, right.second))};
    };
    auto color = [&](auto &&self, std::uint32_t index, std::size_t depth, std::size_t deepest) -> void {
        if (index == kFreeSector)
            return;
        auto left = flat[index].left;
        auto right = flat[index].right;
        flat[index].color = (depth == deepest && depth != 0) ? 0 : 1;
        self(self, left, depth + 1, deepest);
        self(self, right, depth + 1, deepest);
    };
    if (entries.empty())
        return kFreeSector;
    auto [root, deepest] = build(build, entries, 0);
    color(color, root, 0, deepest);
    return root;
}

bool ascii_ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

} // namespace

std::vector<BinaryRecord> BinaryRecord::split(const std::uint8_t *data, std::size_t size, std::size_t chunk_size) {
    auto sz = std::max<std::size_t>(chunk_size, 1);
    std::vector<BinaryRecord> out;
    for (std::size_t i = 0; i < size; i += sz) {
        BinaryRecord rec;
        rec.sequence = static_cast<std::uint32_t>(out.size());
        rec.data.assign(data + i, data + std::min(i + sz, size));
        out.push_back(std::move(rec));
    }
    return out;
}

std::vector<std::uint8_t> BinaryRecord::join(const std::vector<BinaryRecord> &records) {
    auto copy = records;
    std::sort(copy.begin(), copy.end(),
              [](const BinaryRecord &a, const BinaryRecord &b) { return a.sequence < b.sequence; });
    std::vector<std::uint8_t> out;
    for (const auto &r : copy)
        out.insert(out.end(), r.data.begin(), r.data.end());
    return out;
}

CompoundStorage CompoundStorage::root() {
    CompoundStorage s;
    s.name = "Root Entry";
    return s;
}

namespace {

std::size_t align4(std::size_t value) { return (value + 3) & ~std::size_t{3}; }

const char *property_page_name(std::uint16_t code_page) {
    switch (code_page) {
    case 65001:
        return nullptr;
    case 932:
        return "CP932";
    case 936:
        return "GBK";
    case 949:
        return "CP949";
    case 950:
        return "BIG5";
    case 874:
        return "CP874";
    case 1250:
        return "CP1250";
    case 1251:
        return "CP1251";
    case 1253:
        return "CP1253";
    case 1254:
        return "CP1254";
    case 1255:
        return "CP1255";
    case 1256:
        return "CP1256";
    case 1257:
        return "CP1257";
    case 1258:
        return "CP1258";
    case 866:
        return "CP866";
    default:
        return "CP1252";
    }
}

std::string decode_property_page(const std::uint8_t *data, std::size_t size, std::uint16_t code_page) {
    if (code_page == 1200) {
        std::u16string units;
        for (std::size_t i = 0; i + 1 < size; i += 2) {
            auto unit = static_cast<char16_t>(data[i] | (data[i + 1] << 8));
            if (unit == 0)
                break;
            units.push_back(unit);
        }
        std::string out;
        for (char16_t unit : units) {
            auto cp = static_cast<char32_t>(unit);
            if (cp < 0x80)
                out.push_back(static_cast<char>(cp));
            else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }
    std::string bytes(reinterpret_cast<const char *>(data), size);
    auto page = property_page_name(code_page);
    if (!page)
        return bytes;
    return detail::from_legacy(bytes, page);
}

std::vector<std::uint8_t> encode_property_page(std::string_view value, std::uint16_t code_page) {
    if (code_page == 65001)
        return std::vector<std::uint8_t>(value.begin(), value.end());
    auto page = property_page_name(code_page);
    auto encoded = detail::to_legacy(std::string(value), page ? page : "CP1252");
    return std::vector<std::uint8_t>(encoded.begin(), encoded.end());
}

std::size_t next_element(const std::uint8_t *data, std::size_t size, std::uint16_t element_type, std::size_t end) {
    auto padded = align4(end);
    auto string = element_type == 30 || element_type == 31;
    bool gap_is_padding = padded <= size;
    if (gap_is_padding) {
        for (std::size_t i = end; i < padded && i < size; ++i)
            if (data[i] != 0)
                gap_is_padding = false;
    }
    if (string && !gap_is_padding)
        return end;
    return padded;
}

CompoundPropertyValue unsupported_value(std::uint32_t variant_type, const std::uint8_t *data, std::size_t size) {
    CompoundPropertyValue out;
    out.kind = CompoundPropertyValue::Kind::Unsupported;
    out.variant_type = variant_type;
    out.blob = BinaryRecord::split(data, size, 4096);
    return out;
}

std::pair<CompoundPropertyValue, std::size_t> decode_scalar(std::uint32_t variant_type, const std::uint8_t *data,
                                                            std::size_t size);

CompoundPropertyValue decode_dictionary(const std::uint8_t *data, std::size_t size) {
    CompoundPropertyValue out;
    out.kind = CompoundPropertyValue::Kind::Dictionary;
    auto count = read_u32(data, size, 0);
    if (!count)
        return out;
    std::size_t offset = 4;
    for (std::uint32_t i = 0; i < std::min(*count, 1000000u); ++i) {
        auto id = read_u32(data, size, offset);
        auto length = read_u32(data, size, offset + 4);
        if (!id || !length)
            break;
        offset += 8;
        auto byte_length = static_cast<std::size_t>(*length);
        if (offset + byte_length > size)
            break;
        CompoundDictionaryEntry entry;
        entry.property_id = *id;
        entry.name.assign(reinterpret_cast<const char *>(data + offset), byte_length);
        while (!entry.name.empty() && entry.name.back() == '\0')
            entry.name.pop_back();
        out.dictionary.push_back(std::move(entry));
        offset = align4(offset + byte_length);
    }
    return out;
}

std::pair<CompoundPropertyValue, std::size_t> decode_scalar(std::uint32_t variant_type, const std::uint8_t *data,
                                                            std::size_t size) {
    auto fail = [&] {
        return std::make_pair(unsupported_value(variant_type, data, size), size);
    };
    CompoundPropertyValue value;
    switch (variant_type) {
    case 0:
        value.kind = CompoundPropertyValue::Kind::Empty;
        return {value, 0};
    case 1:
        value.kind = CompoundPropertyValue::Kind::Null;
        return {value, 0};
    case 2: {
        auto n = read_u16(data, size, 0);
        if (!n)
            return fail();
        value.kind = CompoundPropertyValue::Kind::I16;
        value.signed_value = static_cast<std::int16_t>(*n);
        return {value, 2};
    }
    case 3:
    case 10: {
        auto n = read_u32(data, size, 0);
        if (!n)
            return fail();
        value.kind = CompoundPropertyValue::Kind::I32;
        value.signed_value = static_cast<std::int32_t>(*n);
        return {value, 4};
    }
    case 4: {
        auto n = read_u32(data, size, 0);
        if (!n)
            return fail();
        float f = 0;
        auto bits = *n;
        std::memcpy(&f, &bits, sizeof(f));
        value.kind = CompoundPropertyValue::Kind::F32;
        value.real = static_cast<double>(f);
        return {value, 4};
    }
    case 5:
    case 7: {
        auto n = read_u64(data, size, 0);
        if (!n)
            return fail();
        double d = 0;
        auto bits = *n;
        std::memcpy(&d, &bits, sizeof(d));
        value.kind = CompoundPropertyValue::Kind::F64;
        value.real = d;
        return {value, 8};
    }
    case 11: {
        auto n = read_u16(data, size, 0);
        if (!n)
            return fail();
        value.kind = CompoundPropertyValue::Kind::Bool;
        value.flag = *n != 0;
        return {value, 2};
    }
    case 18: {
        auto n = read_u16(data, size, 0);
        if (!n)
            return fail();
        value.kind = CompoundPropertyValue::Kind::U16;
        value.unsigned_value = *n;
        return {value, 2};
    }
    case 19: {
        auto n = read_u32(data, size, 0);
        if (!n)
            return fail();
        value.kind = CompoundPropertyValue::Kind::U32;
        value.unsigned_value = *n;
        return {value, 4};
    }
    case 20: {
        auto n = read_u64(data, size, 0);
        if (!n)
            return fail();
        value.kind = CompoundPropertyValue::Kind::I64;
        value.signed_value = static_cast<std::int64_t>(*n);
        return {value, 8};
    }
    case 21: {
        auto n = read_u64(data, size, 0);
        if (!n)
            return fail();
        value.kind = CompoundPropertyValue::Kind::U64;
        value.unsigned_value = *n;
        return {value, 8};
    }
    case 30: {
        auto length = read_u32(data, size, 0).value_or(0);
        auto take = std::min(static_cast<std::size_t>(length), size > 4 ? size - 4 : 0);
        if (4 + static_cast<std::size_t>(length) > size && size < 4)
            return fail();
        const std::uint8_t *bytes = data + 4;
        std::size_t n = take;
        if (n && bytes[n - 1] == 0)
            --n;
        value.kind = CompoundPropertyValue::Kind::AnsiString;
        value.text = decode_property_page(bytes, n, 1252);
        value.original_text = value.text;
        value.code_page = 1252;
        value.original_code_page = 1252;
        value.encoded = BinaryRecord::split(bytes, take, 4096);
        return {value, 4 + static_cast<std::size_t>(length)};
    }
    case 31: {
        auto count = read_u32(data, size, 0).value_or(0);
        auto byte_length = static_cast<std::size_t>(count) * 2;
        auto take = std::min(byte_length, size > 4 ? size - 4 : 0);
        if (4 + byte_length > size && size < 4)
            return fail();
        value.kind = CompoundPropertyValue::Kind::UnicodeString;
        value.text = decode_property_page(data + 4, take, 1200);
        return {value, 4 + byte_length};
    }
    case 64: {
        auto n = read_u64(data, size, 0);
        if (!n)
            return fail();
        value.kind = CompoundPropertyValue::Kind::FileTime;
        value.unsigned_value = *n;
        return {value, 8};
    }
    case 65: {
        auto length = read_u32(data, size, 0).value_or(0);
        auto take = std::min(static_cast<std::size_t>(length), size > 4 ? size - 4 : 0);
        value.kind = CompoundPropertyValue::Kind::Blob;
        value.blob = BinaryRecord::split(data + 4, take, 4096);
        return {value, 4 + static_cast<std::size_t>(length)};
    }
    case 71: {
        auto length = read_u32(data, size, 0).value_or(0);
        auto format = read_u32(data, size, 4).value_or(0);
        auto take = std::min(static_cast<std::size_t>(length), size > 4 ? size - 4 : 0);
        std::size_t data_off = 8;
        std::size_t data_len = take > 4 ? take - 4 : 0;
        if (data_off + data_len > size)
            data_len = size > data_off ? size - data_off : 0;
        value.kind = CompoundPropertyValue::Kind::Clipboard;
        value.clipboard_format = format;
        value.blob = BinaryRecord::split(data + data_off, data_len, 4096);
        return {value, 4 + static_cast<std::size_t>(length)};
    }
    case 72: {
        if (size < 16)
            return fail();
        value.kind = CompoundPropertyValue::Kind::ClassId;
        std::memcpy(value.class_id.data(), data, 16);
        return {value, 16};
    }
    default:
        return fail();
    }
}

CompoundPropertyValue decode_property(const std::uint8_t *data, std::size_t size) {
    auto variant = read_u32(data, size, 0);
    if (!variant)
        return unsupported_value(0, data, size);
    if (*variant & 0x1000) {
        CompoundPropertyValue out;
        out.kind = CompoundPropertyValue::Kind::Vector;
        out.element_type = static_cast<std::uint16_t>(*variant & 0x0FFF);
        auto count = read_u32(data, size, 4).value_or(0);
        std::size_t offset = 8;
        for (std::uint32_t i = 0; i < std::min(count, 1000000u); ++i) {
            auto [item, consumed] = decode_scalar(out.element_type, data + offset, offset < size ? size - offset : 0);
            out.values.push_back(std::move(item));
            offset = next_element(data, size, out.element_type, offset + consumed);
        }
        return out;
    }
    return decode_scalar(*variant, data + std::min<std::size_t>(4, size), size > 4 ? size - 4 : 0).first;
}

std::optional<std::uint16_t> property_code_page(const CompoundPropertyValue &value) {
    switch (value.kind) {
    case CompoundPropertyValue::Kind::I16:
        if (value.signed_value >= 0)
            return static_cast<std::uint16_t>(value.signed_value);
        break;
    case CompoundPropertyValue::Kind::I32:
        if (value.signed_value >= 0 && value.signed_value <= 65535)
            return static_cast<std::uint16_t>(value.signed_value);
        break;
    case CompoundPropertyValue::Kind::U16:
        return static_cast<std::uint16_t>(value.unsigned_value);
    case CompoundPropertyValue::Kind::U32:
        if (value.unsigned_value <= 65535)
            return static_cast<std::uint16_t>(value.unsigned_value);
        break;
    default:
        break;
    }
    return std::nullopt;
}

void apply_property_code_page(CompoundPropertyValue &value, std::uint16_t code_page) {
    if (value.kind == CompoundPropertyValue::Kind::AnsiString) {
        auto bytes = BinaryRecord::join(value.encoded);
        if (!bytes.empty() && bytes.back() == 0)
            bytes.pop_back();
        auto decoded = decode_property_page(bytes.data(), bytes.size(), code_page);
        value.text = decoded;
        value.original_text = decoded;
        value.code_page = code_page;
        value.original_code_page = code_page;
    } else if (value.kind == CompoundPropertyValue::Kind::Vector) {
        for (auto &item : value.values)
            apply_property_code_page(item, code_page);
    }
}

void append_u16(std::vector<std::uint8_t> &out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void append_u32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}
void append_u64(std::vector<std::uint8_t> &out, std::uint64_t value) {
    append_u32(out, static_cast<std::uint32_t>(value));
    append_u32(out, static_cast<std::uint32_t>(value >> 32));
}

std::vector<std::uint8_t> encode_dictionary(const std::vector<CompoundDictionaryEntry> &entries) {
    std::vector<std::uint8_t> out;
    append_u32(out, static_cast<std::uint32_t>(entries.size()));
    for (const auto &entry : entries) {
        append_u32(out, entry.property_id);
        std::vector<std::uint8_t> bytes(entry.name.begin(), entry.name.end());
        bytes.push_back(0);
        append_u32(out, static_cast<std::uint32_t>(bytes.size()));
        out.insert(out.end(), bytes.begin(), bytes.end());
        while (out.size() % 4 != 0)
            out.push_back(0);
    }
    return out;
}

std::pair<std::uint32_t, std::vector<std::uint8_t>> encode_scalar(const CompoundPropertyValue &value);

std::pair<std::uint32_t, std::vector<std::uint8_t>> encode_scalar(const CompoundPropertyValue &value) {
    std::vector<std::uint8_t> body;
    switch (value.kind) {
    case CompoundPropertyValue::Kind::Empty:
        return {0, {}};
    case CompoundPropertyValue::Kind::Null:
        return {1, {}};
    case CompoundPropertyValue::Kind::I16:
        append_u16(body, static_cast<std::uint16_t>(value.signed_value));
        return {2, body};
    case CompoundPropertyValue::Kind::I32:
        append_u32(body, static_cast<std::uint32_t>(value.signed_value));
        return {3, body};
    case CompoundPropertyValue::Kind::I64:
        append_u64(body, static_cast<std::uint64_t>(value.signed_value));
        return {20, body};
    case CompoundPropertyValue::Kind::U16:
        append_u16(body, static_cast<std::uint16_t>(value.unsigned_value));
        return {18, body};
    case CompoundPropertyValue::Kind::U32:
        append_u32(body, static_cast<std::uint32_t>(value.unsigned_value));
        return {19, body};
    case CompoundPropertyValue::Kind::U64:
        append_u64(body, value.unsigned_value);
        return {21, body};
    case CompoundPropertyValue::Kind::F32: {
        float f = static_cast<float>(value.real);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        append_u32(body, bits);
        return {4, body};
    }
    case CompoundPropertyValue::Kind::F64: {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value.real, sizeof(bits));
        append_u64(body, bits);
        return {5, body};
    }
    case CompoundPropertyValue::Kind::Bool:
        append_u16(body, value.flag ? 0xFFFFu : 0);
        return {11, body};
    case CompoundPropertyValue::Kind::AnsiString: {
        std::vector<std::uint8_t> bytes;
        if (value.text == value.original_text && value.code_page == value.original_code_page && !value.encoded.empty())
            bytes = BinaryRecord::join(value.encoded);
        else {
            bytes = encode_property_page(value.text, value.code_page);
            bytes.push_back(0);
        }
        if (bytes.empty() || bytes.back() != 0)
            bytes.push_back(0);
        append_u32(body, static_cast<std::uint32_t>(bytes.size()));
        body.insert(body.end(), bytes.begin(), bytes.end());
        return {30, body};
    }
    case CompoundPropertyValue::Kind::UnicodeString: {
        std::u16string units;
        for (std::size_t i = 0; i < value.text.size();) {
            auto b = static_cast<unsigned char>(value.text[i]);
            char32_t cp = 0;
            std::size_t n = 1;
            if (b < 0x80)
                cp = b;
            else if ((b & 0xE0) == 0xC0 && i + 1 < value.text.size()) {
                cp = ((b & 0x1F) << 6) | (static_cast<unsigned char>(value.text[i + 1]) & 0x3F);
                n = 2;
            } else if ((b & 0xF0) == 0xE0 && i + 2 < value.text.size()) {
                cp = ((b & 0x0F) << 12) | ((static_cast<unsigned char>(value.text[i + 1]) & 0x3F) << 6) |
                     (static_cast<unsigned char>(value.text[i + 2]) & 0x3F);
                n = 3;
            } else
                cp = b;
            i += n;
            if (cp > 0xFFFF)
                cp = 0xFFFD;
            units.push_back(static_cast<char16_t>(cp));
        }
        units.push_back(0);
        append_u32(body, static_cast<std::uint32_t>(units.size()));
        for (char16_t unit : units)
            append_u16(body, static_cast<std::uint16_t>(unit));
        return {31, body};
    }
    case CompoundPropertyValue::Kind::FileTime:
        append_u64(body, value.unsigned_value);
        return {64, body};
    case CompoundPropertyValue::Kind::ClassId:
        body.insert(body.end(), value.class_id.begin(), value.class_id.end());
        return {72, body};
    case CompoundPropertyValue::Kind::Blob: {
        auto bytes = BinaryRecord::join(value.blob);
        append_u32(body, static_cast<std::uint32_t>(bytes.size()));
        body.insert(body.end(), bytes.begin(), bytes.end());
        return {65, body};
    }
    case CompoundPropertyValue::Kind::Clipboard: {
        auto bytes = BinaryRecord::join(value.blob);
        append_u32(body, static_cast<std::uint32_t>(bytes.size() + 4));
        append_u32(body, value.clipboard_format);
        body.insert(body.end(), bytes.begin(), bytes.end());
        return {71, body};
    }
    case CompoundPropertyValue::Kind::Vector: {
        append_u32(body, static_cast<std::uint32_t>(value.values.size()));
        for (const auto &item : value.values) {
            auto encoded = encode_scalar(item).second;
            body.insert(body.end(), encoded.begin(), encoded.end());
            while (body.size() % 4 != 0)
                body.push_back(0);
        }
        return {0x1000u | value.element_type, body};
    }
    case CompoundPropertyValue::Kind::Unsupported:
        return {value.variant_type, BinaryRecord::join(value.blob)};
    case CompoundPropertyValue::Kind::Dictionary:
        return {0, encode_dictionary(value.dictionary)};
    }
    return {0, {}};
}

std::vector<std::uint8_t> encode_property(const CompoundPropertyValue &value) {
    auto [variant, body] = encode_scalar(value);
    std::vector<std::uint8_t> out;
    append_u32(out, variant);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<std::uint8_t> encode_property_set(const CompoundPropertySet &set) {
    auto table_size = 8 + set.properties.size() * 8;
    std::vector<std::uint8_t> output(table_size, 0);
    write_u32(output.data(), 4, static_cast<std::uint32_t>(set.properties.size()));
    for (std::size_t index = 0; index < set.properties.size(); ++index) {
        while (output.size() % 4 != 0)
            output.push_back(0);
        auto offset = output.size();
        const auto &property = set.properties[index];
        auto bytes = (property.id == 0 && property.value.kind == CompoundPropertyValue::Kind::Dictionary)
                         ? encode_dictionary(property.value.dictionary)
                         : encode_property(property.value);
        auto table_offset = 8 + index * 8;
        write_u32(output.data(), table_offset, property.id);
        write_u32(output.data(), table_offset + 4, static_cast<std::uint32_t>(offset));
        output.insert(output.end(), bytes.begin(), bytes.end());
    }
    while (output.size() % 4 != 0)
        output.push_back(0);
    write_u32(output.data(), 0, static_cast<std::uint32_t>(output.size()));
    return output;
}

} // namespace

std::optional<CompoundPropertySetStream> CompoundPropertySetStream::decode(const std::uint8_t *data, std::size_t size) {
    if (read_u16(data, size, 0) != 0xFFFE)
        return std::nullopt;
    auto version = read_u16(data, size, 2);
    auto system = read_u32(data, size, 4);
    if (!version || !system || size < 24)
        return std::nullopt;
    auto count = read_u32(data, size, 24);
    if (!count || *count > 64)
        return std::nullopt;
    CompoundPropertySetStream out;
    out.version = *version;
    out.system_identifier = *system;
    std::memcpy(out.class_id.data(), data + 8, 16);
    std::vector<std::pair<std::array<std::uint8_t, 16>, std::size_t>> descriptors;
    for (std::uint32_t index = 0; index < *count; ++index) {
        auto offset = 28 + static_cast<std::size_t>(index) * 20;
        if (offset + 20 > size)
            return std::nullopt;
        std::array<std::uint8_t, 16> format_id{};
        std::memcpy(format_id.data(), data + offset, 16);
        auto set_offset = read_u32(data, size, offset + 16);
        if (!set_offset)
            return std::nullopt;
        descriptors.emplace_back(format_id, *set_offset);
    }
    for (auto [format_id, offset] : descriptors) {
        auto raw_size = read_u32(data, size, offset);
        if (!raw_size)
            return std::nullopt;
        auto end = std::min(offset + static_cast<std::size_t>(*raw_size), size);
        if (end < offset)
            return std::nullopt;
        const std::uint8_t *section = data + offset;
        auto section_size = end - offset;
        auto property_count = read_u32(section, section_size, 4);
        if (!property_count || *property_count > 1000000)
            return std::nullopt;
        std::vector<std::pair<std::uint32_t, std::size_t>> table;
        for (std::uint32_t index = 0; index < *property_count; ++index) {
            auto entry = 8 + static_cast<std::size_t>(index) * 8;
            auto id = read_u32(section, section_size, entry);
            auto property_offset = read_u32(section, section_size, entry + 4);
            if (!id || !property_offset)
                return std::nullopt;
            table.emplace_back(*id, *property_offset);
        }
        std::sort(table.begin(), table.end(),
                  [](const auto &a, const auto &b) { return a.second < b.second; });
        CompoundPropertySet set;
        set.format_id = format_id;
        for (std::size_t index = 0; index < table.size(); ++index) {
            auto [id, property_offset] = table[index];
            auto next = index + 1 < table.size() ? table[index + 1].second : section_size;
            if (property_offset > next || next > section_size)
                return std::nullopt;
            auto bytes = section + property_offset;
            auto nbytes = next - property_offset;
            CompoundProperty property;
            property.id = id;
            property.value = id == 0 ? decode_dictionary(bytes, nbytes) : decode_property(bytes, nbytes);
            set.properties.push_back(std::move(property));
        }
        std::sort(set.properties.begin(), set.properties.end(),
                  [](const auto &a, const auto &b) { return a.id < b.id; });
        std::uint16_t code_page = 1252;
        for (const auto &property : set.properties)
            if (property.id == 1)
                if (auto page = property_code_page(property.value))
                    code_page = *page;
        for (auto &property : set.properties)
            apply_property_code_page(property.value, code_page);
        out.sets.push_back(std::move(set));
    }
    return out;
}

std::vector<std::uint8_t> CompoundPropertySetStream::encode() const {
    auto header_size = 28 + sets.size() * 20;
    std::vector<std::uint8_t> output(header_size, 0);
    write_u16(output.data(), 0, 0xFFFE);
    write_u16(output.data(), 2, version);
    write_u32(output.data(), 4, system_identifier);
    std::memcpy(output.data() + 8, class_id.data(), 16);
    write_u32(output.data(), 24, static_cast<std::uint32_t>(sets.size()));
    for (std::size_t index = 0; index < sets.size(); ++index) {
        auto descriptor = 28 + index * 20;
        std::memcpy(output.data() + descriptor, sets[index].format_id.data(), 16);
        auto set_offset = static_cast<std::uint32_t>(output.size());
        write_u32(output.data(), descriptor + 16, set_offset);
        auto encoded = encode_property_set(sets[index]);
        output.insert(output.end(), encoded.begin(), encoded.end());
    }
    return output;
}

CompoundStreamContent CompoundStreamContent::decode(std::string_view name, const std::uint8_t *data, std::size_t size) {
    CompoundStreamContent out;
    if (ascii_ieq(name, "dir")) {
        if (auto directory = VbaDirectoryStream::decode(data, size)) {
            out.kind = Kind::VbaDirectory;
            out.vba_directory = std::move(*directory);
            return out;
        }
    }
    if ((!name.empty() && static_cast<unsigned char>(name[0]) == 5) || (size >= 2 && data[0] == 0xFE && data[1] == 0xFF)) {
        if (auto properties = CompoundPropertySetStream::decode(data, size)) {
            out.kind = Kind::PropertySet;
            out.properties = std::move(*properties);
            return out;
        }
    }
    out.kind = Kind::Binary;
    out.binary = BinaryRecord::split(data, size, 4096);
    return out;
}

std::vector<std::uint8_t> CompoundStreamContent::encode() const {
    switch (kind) {
    case Kind::PropertySet:
        return properties.encode();
    case Kind::VbaDirectory:
        return vba_directory.encode();
    case Kind::VbaModule:
        return vba_module.encode();
    case Kind::Binary:
        return BinaryRecord::join(binary);
    }
    return {};
}

std::optional<std::pair<CompoundFile, std::size_t>> CompoundFile::decode(const std::uint8_t *data, std::size_t size) {
    if (size < 512 || std::memcmp(data, kCfbMagic, 8) != 0)
        return std::nullopt;
    auto major = read_u16(data, size, 26);
    auto sector_shift = read_u16(data, size, 30);
    auto mini_shift = read_u16(data, size, 32);
    if (!major || !sector_shift || !mini_shift)
        return std::nullopt;
    if (*sector_shift < 9 || *sector_shift > 12 || *mini_shift < 6 || *mini_shift >= *sector_shift)
        return std::nullopt;
    auto sector_size = 1u << *sector_shift;
    auto mini_sector_size = 1u << *mini_shift;
    if (size < sector_size)
        return std::nullopt;
    auto sector = [&](std::uint32_t index) -> std::optional<std::vector<std::uint8_t>> {
        auto offset = (static_cast<std::size_t>(index) + 1) * sector_size;
        if (offset + sector_size > size)
            return std::nullopt;
        return std::vector<std::uint8_t>(data + offset, data + offset + sector_size);
    };
    std::vector<std::uint32_t> difat;
    for (int i = 0; i < 109; ++i) {
        auto v = read_u32(data, size, 76 + i * 4);
        if (v && *v < kDifatSector)
            difat.push_back(*v);
    }
    auto difat_sector = read_u32(data, size, 68).value_or(kEndOfChain);
    auto difat_count = read_u32(data, size, 72).value_or(0);
    for (std::uint32_t n = 0; n < difat_count && n < size / sector_size; ++n) {
        if (difat_sector >= kDifatSector)
            break;
        auto bytes = sector(difat_sector);
        if (!bytes)
            break;
        auto entries = sector_size / 4;
        for (std::size_t i = 0; i + 1 < entries; ++i) {
            auto value = read_u32(bytes->data(), bytes->size(), i * 4);
            if (value && *value != kFreeSector)
                difat.push_back(*value);
        }
        difat_sector = read_u32(bytes->data(), bytes->size(), (entries - 1) * 4).value_or(kEndOfChain);
    }
    auto fat_sector_count = read_u32(data, size, 44).value_or(0);
    std::vector<std::uint32_t> fat;
    for (std::size_t i = 0; i < difat.size() && i < fat_sector_count; ++i) {
        auto bytes = sector(difat[i]);
        if (!bytes)
            return std::nullopt;
        for (std::size_t j = 0; j + 3 < bytes->size(); j += 4)
            fat.push_back(read_u32(bytes->data(), bytes->size(), j).value_or(kFreeSector));
    }
    if (fat.empty())
        return std::nullopt;
    auto regular_chain = [&](std::uint32_t start) {
        return read_chain(start, fat, fat.size() + 1, [&](std::uint32_t index) { return sector(index); });
    };
    auto directory_start = read_u32(data, size, 48);
    if (!directory_start)
        return std::nullopt;
    auto directory_bytes = regular_chain(*directory_start);
    std::vector<DirectoryEntry> directory;
    for (std::size_t at = 0; at + 128 <= directory_bytes.size(); at += 128) {
        const auto *bytes = directory_bytes.data() + at;
        auto name_length = read_u16(bytes, 128, 64).value_or(0);
        std::string name;
        if (name_length >= 2 && name_length <= 64) {
            for (std::size_t i = 0; i + 1 < name_length - 2; i += 2) {
                auto unit = read_u16(bytes, 128, i).value_or(0);
                if (unit < 0x80)
                    name.push_back(static_cast<char>(unit));
                else if (unit < 0x800) {
                    name.push_back(static_cast<char>(0xC0 | (unit >> 6)));
                    name.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
                } else {
                    name.push_back(static_cast<char>(0xE0 | (unit >> 12)));
                    name.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
                    name.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
                }
            }
        }
        DirectoryEntry entry;
        entry.name = std::move(name);
        entry.kind = bytes[66];
        entry.left = read_u32(bytes, 128, 68).value_or(kFreeSector);
        entry.right = read_u32(bytes, 128, 72).value_or(kFreeSector);
        entry.child = read_u32(bytes, 128, 76).value_or(kFreeSector);
        std::memcpy(entry.class_id.data(), bytes + 80, 16);
        entry.state_bits = read_u32(bytes, 128, 96).value_or(0);
        entry.creation_time = read_u64(bytes, 128, 100).value_or(0);
        entry.modified_time = read_u64(bytes, 128, 108).value_or(0);
        entry.start = read_u32(bytes, 128, 116).value_or(0);
        if (*major >= 4)
            entry.size = read_u64(bytes, 128, 120).value_or(0);
        else
            entry.size = read_u32(bytes, 128, 120).value_or(0);
        directory.push_back(std::move(entry));
    }
    auto mini_fat_start = read_u32(data, size, 60).value_or(kEndOfChain);
    auto mini_fat_count = read_u32(data, size, 64).value_or(0);
    auto mini_fat_bytes = regular_chain(mini_fat_start);
    std::vector<std::uint32_t> mini_fat;
    auto mini_fat_take = mini_fat_count * (sector_size / 4);
    for (std::size_t i = 0; i + 3 < mini_fat_bytes.size() && mini_fat.size() < mini_fat_take; i += 4)
        mini_fat.push_back(read_u32(mini_fat_bytes.data(), mini_fat_bytes.size(), i).value_or(kFreeSector));
    auto root_it = std::find_if(directory.begin(), directory.end(), [](const DirectoryEntry &e) { return e.kind == 5; });
    if (root_it == directory.end())
        return std::nullopt;
    auto mini_stream = regular_chain(root_it->start);
    if (mini_stream.size() > root_it->size)
        mini_stream.resize(static_cast<std::size_t>(root_it->size));
    auto mini_cutoff = read_u32(data, size, 56).value_or(4096);

    auto stream_data = [&](const DirectoryEntry &entry) {
        if (entry.size == 0)
            return std::vector<std::uint8_t>{};
        std::vector<std::uint8_t> output;
        if (entry.size < mini_cutoff) {
            output = read_chain(entry.start, mini_fat, mini_fat.size() + 1, [&](std::uint32_t index) {
                auto offset = static_cast<std::size_t>(index) * mini_sector_size;
                if (offset + mini_sector_size > mini_stream.size())
                    return std::optional<std::vector<std::uint8_t>>{};
                return std::optional<std::vector<std::uint8_t>>{
                    std::vector<std::uint8_t>(mini_stream.begin() + offset,
                                              mini_stream.begin() + offset + mini_sector_size)};
            });
        } else {
            output = regular_chain(entry.start);
        }
        if (output.size() > entry.size)
            output.resize(static_cast<std::size_t>(entry.size));
        return output;
    };

    std::function<void(std::uint32_t, std::vector<std::size_t> &, std::unordered_set<std::uint32_t> &)> visit;
    visit = [&](std::uint32_t index, std::vector<std::size_t> &output, std::unordered_set<std::uint32_t> &seen) {
        if (index == kFreeSector || !seen.insert(index).second)
            return;
        if (index >= directory.size())
            return;
        visit(directory[index].left, output, seen);
        output.push_back(index);
        visit(directory[index].right, output, seen);
    };

    std::function<std::optional<CompoundStorage>(std::size_t, bool, std::unordered_set<std::size_t> &)> storage;
    storage = [&](std::size_t index, bool root, std::unordered_set<std::size_t> &visited) -> std::optional<CompoundStorage> {
        if (!visited.insert(index).second)
            return std::nullopt;
        if (index >= directory.size())
            return std::nullopt;
        const auto &entry = directory[index];
        std::vector<std::size_t> child_indices;
        std::unordered_set<std::uint32_t> seen;
        visit(entry.child, child_indices, seen);
        std::optional<VbaDirectoryStream> vba_directory;
        std::vector<VbaModuleDescriptor> module_descriptors;
        if (ascii_ieq(entry.name, "VBA")) {
            for (auto child_index : child_indices) {
                const auto &child = directory[child_index];
                if (child.kind == 2 && ascii_ieq(child.name, "dir")) {
                    auto bytes = stream_data(child);
                    vba_directory = VbaDirectoryStream::decode(bytes.data(), bytes.size());
                    if (vba_directory)
                        module_descriptors = vba_directory->module_descriptors();
                }
            }
        }
        CompoundStorage result;
        result.name = root && entry.name.empty() ? "Root Entry" : entry.name;
        result.class_id = entry.class_id;
        result.state_bits = entry.state_bits;
        result.creation_time = entry.creation_time;
        result.modified_time = entry.modified_time;
        for (auto child_index : child_indices) {
            const auto &child = directory[child_index];
            if (child.kind == 1) {
                if (auto nested = storage(child_index, false, visited))
                    result.entries.emplace_back(std::move(*nested));
            } else if (child.kind == 2) {
                auto bytes = stream_data(child);
                CompoundStream stream;
                stream.name = child.name;
                stream.class_id = child.class_id;
                stream.state_bits = child.state_bits;
                stream.creation_time = child.creation_time;
                stream.modified_time = child.modified_time;
                if (ascii_ieq(child.name, "dir") && vba_directory) {
                    stream.content.kind = CompoundStreamContent::Kind::VbaDirectory;
                    stream.content.vba_directory = *vba_directory;
                } else {
                    auto module = std::find_if(module_descriptors.begin(), module_descriptors.end(),
                                               [&](const VbaModuleDescriptor &m) {
                                                   return ascii_ieq(m.stream_name, child.name);
                                               });
                    if (module != module_descriptors.end()) {
                        if (auto decoded =
                                VbaModuleStream::decode(bytes.data(), bytes.size(), module->source_offset,
                                                        module->code_page)) {
                            stream.content.kind = CompoundStreamContent::Kind::VbaModule;
                            stream.content.vba_module = std::move(*decoded);
                        } else {
                            stream.content = CompoundStreamContent::decode(child.name, bytes.data(), bytes.size());
                        }
                    } else {
                        stream.content = CompoundStreamContent::decode(child.name, bytes.data(), bytes.size());
                    }
                }
                result.entries.emplace_back(std::move(stream));
            }
        }
        return result;
    };

    std::unordered_set<std::size_t> visited;
    auto root_index = static_cast<std::size_t>(root_it - directory.begin());
    auto root = storage(root_index, true, visited);
    if (!root)
        return std::nullopt;
    CompoundFile file;
    file.minor_version = read_u16(data, size, 24).value_or(0x003E);
    file.major_version = *major;
    file.transaction_signature = read_u32(data, size, 52).value_or(0);
    file.root = std::move(*root);
    std::size_t sector_count = 0;
    for (std::size_t i = 0; i < fat.size(); ++i)
        if (fat[i] != kFreeSector)
            sector_count = i + 1;
    auto consumed = std::min(size, sector_size * (1 + sector_count));
    return std::make_pair(std::move(file), consumed);
}

std::vector<std::uint8_t> CompoundFile::encode() const {
    std::vector<FlatEntry> flat;
    auto add_storage = [&](auto &&self, const CompoundStorage &storage, bool root) -> std::uint32_t {
        auto index = static_cast<std::uint32_t>(flat.size());
        FlatEntry entry;
        entry.name = storage.name;
        entry.kind = root ? 5 : 1;
        entry.class_id = storage.class_id;
        entry.state_bits = storage.state_bits;
        entry.creation_time = storage.creation_time;
        entry.modified_time = storage.modified_time;
        flat.push_back(std::move(entry));
        std::vector<std::uint32_t> children;
        for (const auto &item : storage.entries) {
            if (auto *nested = std::get_if<CompoundStorage>(&item)) {
                children.push_back(self(self, *nested, false));
            } else {
                const auto &stream = std::get<CompoundStream>(item);
                auto child = static_cast<std::uint32_t>(flat.size());
                FlatEntry s;
                s.name = stream.name;
                s.kind = 2;
                s.class_id = stream.class_id;
                s.state_bits = stream.state_bits;
                s.creation_time = stream.creation_time;
                s.modified_time = stream.modified_time;
                s.stream = stream.content.encode();
                flat.push_back(std::move(s));
                children.push_back(child);
            }
        }
        std::sort(children.begin(), children.end(), [&](std::uint32_t a, std::uint32_t b) {
            auto utf16_len = [](const std::string &n) { return n.size(); };
            auto la = utf16_len(flat[a].name);
            auto lb = utf16_len(flat[b].name);
            if (la != lb)
                return la < lb;
            auto ua = flat[a].name;
            auto ub = flat[b].name;
            std::transform(ua.begin(), ua.end(), ua.begin(), [](unsigned char c) { return std::toupper(c); });
            std::transform(ub.begin(), ub.end(), ub.begin(), [](unsigned char c) { return std::toupper(c); });
            return ua < ub;
        });
        flat[index].child = build_directory_tree(flat, children);
        return index;
    };
    add_storage(add_storage, root, true);

    constexpr std::size_t sector_size = 512;
    constexpr std::size_t mini_sector_size = 64;
    constexpr std::size_t mini_cutoff = 4096;
    std::vector<std::vector<std::uint8_t>> sectors;
    std::vector<std::uint32_t> fat;
    std::vector<std::uint8_t> mini_stream;
    std::vector<std::uint32_t> mini_fat;
    for (auto &entry : flat) {
        if (entry.kind != 2 || entry.stream.empty()) {
            entry.start = kEndOfChain;
            entry.size = entry.stream.size();
            continue;
        }
        entry.size = entry.stream.size();
        if (entry.stream.size() < mini_cutoff) {
            auto count = div_ceil(entry.stream.size(), mini_sector_size);
            entry.start = static_cast<std::uint32_t>(mini_fat.size());
            for (std::size_t index = 0; index < count; ++index) {
                auto start = index * mini_sector_size;
                auto end = std::min(start + mini_sector_size, entry.stream.size());
                mini_stream.insert(mini_stream.end(), entry.stream.begin() + start, entry.stream.begin() + end);
                mini_stream.resize(div_ceil(mini_stream.size(), mini_sector_size) * mini_sector_size, 0);
                mini_fat.push_back(index + 1 == count ? kEndOfChain : entry.start + static_cast<std::uint32_t>(index) + 1);
            }
        } else {
            entry.start = allocate_chain(entry.stream, sector_size, sectors, fat);
        }
    }
    auto root_start = allocate_chain(mini_stream, sector_size, sectors, fat);
    std::vector<std::uint8_t> mini_fat_bytes;
    for (auto v : mini_fat) {
        mini_fat_bytes.push_back(static_cast<std::uint8_t>(v));
        mini_fat_bytes.push_back(static_cast<std::uint8_t>(v >> 8));
        mini_fat_bytes.push_back(static_cast<std::uint8_t>(v >> 16));
        mini_fat_bytes.push_back(static_cast<std::uint8_t>(v >> 24));
    }
    if (!mini_fat_bytes.empty()) {
        auto entry_count = div_ceil(mini_fat_bytes.size(), sector_size) * (sector_size / 4);
        while (mini_fat.size() < entry_count) {
            mini_fat_bytes.insert(mini_fat_bytes.end(), {0xFF, 0xFF, 0xFF, 0xFF});
            mini_fat.push_back(kFreeSector);
        }
    }
    auto mini_fat_start = allocate_chain(mini_fat_bytes, sector_size, sectors, fat);
    auto mini_fat_sectors = div_ceil(mini_fat_bytes.size(), sector_size);

    auto entry_count = std::max<std::size_t>(flat.size(), 1);
    entry_count = div_ceil(entry_count, 4) * 4;
    std::vector<std::uint8_t> directory_bytes(entry_count * 128, 0);
    for (std::size_t index = 0; index < flat.size(); ++index) {
        auto *bytes = directory_bytes.data() + index * 128;
        const auto &entry = flat[index];
        std::vector<std::uint16_t> name;
        for (unsigned char c : entry.name)
            name.push_back(c);
        if (name.size() > 31)
            name.resize(31);
        name.push_back(0);
        for (std::size_t i = 0; i < name.size(); ++i)
            write_u16(bytes, i * 2, name[i]);
        write_u16(bytes, 64, static_cast<std::uint16_t>(name.size() * 2));
        bytes[66] = entry.kind;
        bytes[67] = entry.color;
        write_u32(bytes, 68, entry.left);
        write_u32(bytes, 72, entry.right);
        write_u32(bytes, 76, entry.child);
        std::memcpy(bytes + 80, entry.class_id.data(), 16);
        write_u32(bytes, 96, entry.state_bits);
        write_u64(bytes, 100, entry.creation_time);
        write_u64(bytes, 108, entry.modified_time);
        auto start = entry.kind == 5 ? root_start : entry.start;
        auto size = entry.kind == 5 ? static_cast<std::uint64_t>(mini_stream.size()) : entry.size;
        write_u32(bytes, 116, start);
        write_u64(bytes, 120, size);
    }
    auto directory_start = allocate_chain(directory_bytes, sector_size, sectors, fat);

    auto non_allocation = sectors.size();
    auto entries_per_fat = sector_size / 4;
    auto entries_per_difat = entries_per_fat - 1;
    auto [fat_count, difat_count] = allocation_sector_counts(non_allocation, entries_per_fat, entries_per_difat);
    auto difat_start = difat_count == 0 ? kEndOfChain : static_cast<std::uint32_t>(sectors.size());
    std::vector<std::uint32_t> difat_sector_ids;
    for (std::size_t i = 0; i < difat_count; ++i) {
        difat_sector_ids.push_back(static_cast<std::uint32_t>(sectors.size()));
        sectors.push_back(std::vector<std::uint8_t>(sector_size, 0));
        fat.push_back(kDifatSector);
    }
    std::vector<std::uint32_t> fat_sector_ids;
    for (std::size_t i = 0; i < fat_count; ++i) {
        fat_sector_ids.push_back(static_cast<std::uint32_t>(sectors.size()));
        sectors.push_back(std::vector<std::uint8_t>(sector_size, 0));
        fat.push_back(kFatSector);
    }
    fat.resize(fat_count * entries_per_fat, kFreeSector);
    for (std::size_t index = 0; index < fat_sector_ids.size(); ++index) {
        auto start = index * entries_per_fat;
        auto *sector = sectors[fat_sector_ids[index]].data();
        for (std::size_t i = 0; i < entries_per_fat; ++i)
            write_u32(sector, i * 4, fat[start + i]);
    }
    std::vector<std::uint32_t> extra_fat;
    if (fat_sector_ids.size() > 109)
        extra_fat.assign(fat_sector_ids.begin() + 109, fat_sector_ids.end());
    for (std::size_t index = 0; index < difat_sector_ids.size(); ++index) {
        auto start = index * entries_per_difat;
        auto *sector = sectors[difat_sector_ids[index]].data();
        auto n = extra_fat.size() > start ? std::min(entries_per_difat, extra_fat.size() - start) : 0;
        for (std::size_t i = 0; i < n; ++i)
            write_u32(sector, i * 4, extra_fat[start + i]);
        for (std::size_t i = n; i < entries_per_difat; ++i)
            write_u32(sector, i * 4, kFreeSector);
        write_u32(sector, entries_per_difat * 4,
                  index + 1 < difat_sector_ids.size() ? difat_sector_ids[index + 1] : kEndOfChain);
    }

    std::vector<std::uint8_t> header(sector_size, 0);
    std::memcpy(header.data(), kCfbMagic, 8);
    write_u16(header.data(), 24, minor_version);
    write_u16(header.data(), 26, 3);
    write_u16(header.data(), 28, 0xFFFE);
    write_u16(header.data(), 30, 9);
    write_u16(header.data(), 32, 6);
    write_u32(header.data(), 40, 0);
    write_u32(header.data(), 44, static_cast<std::uint32_t>(fat_count));
    write_u32(header.data(), 48, directory_start);
    write_u32(header.data(), 52, transaction_signature);
    write_u32(header.data(), 56, static_cast<std::uint32_t>(mini_cutoff));
    write_u32(header.data(), 60, mini_fat_start);
    write_u32(header.data(), 64, static_cast<std::uint32_t>(mini_fat_sectors));
    write_u32(header.data(), 68, difat_start);
    write_u32(header.data(), 72, static_cast<std::uint32_t>(difat_count));
    for (int i = 0; i < 109; ++i)
        write_u32(header.data(), 76 + i * 4, i < static_cast<int>(fat_sector_ids.size()) ? fat_sector_ids[i] : kFreeSector);
    for (const auto &sector : sectors)
        header.insert(header.end(), sector.begin(), sector.end());
    return header;
}

StructuredStoragePayload StructuredStoragePayload::decode(const std::uint8_t *data, std::size_t size) {
    StructuredStoragePayload out;
    std::size_t offset = size;
    for (std::size_t i = 0; i + 8 <= size; ++i) {
        if (std::memcmp(data + i, kCfbMagic, 8) == 0) {
            offset = i;
            break;
        }
    }
    if (offset == size) {
        out.leading_records = BinaryRecord::split(data, size, 4096);
        return out;
    }
    auto decoded = CompoundFile::decode(data + offset, size - offset);
    if (!decoded) {
        out.leading_records = BinaryRecord::split(data, size, 4096);
        return out;
    }
    out.leading_records = BinaryRecord::split(data, offset, 4096);
    out.compound_file = std::move(decoded->first);
    auto rest = offset + decoded->second;
    if (rest < size)
        out.trailing_records = BinaryRecord::split(data + rest, size - rest, 4096);
    return out;
}

std::vector<std::uint8_t> StructuredStoragePayload::encode() const {
    auto output = BinaryRecord::join(leading_records);
    if (compound_file) {
        auto bytes = compound_file->encode();
        output.insert(output.end(), bytes.begin(), bytes.end());
    }
    auto trailing = BinaryRecord::join(trailing_records);
    output.insert(output.end(), trailing.begin(), trailing.end());
    return output;
}

} // namespace libopencad
