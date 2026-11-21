// SPDX-License-Identifier: MPL-2.0
// acadrust dwg_reader, checksum and decompressor_ac18.
#include "dwg_sections_internal.hpp"
#include "dwg_security.hpp"
#include <algorithm>
#include <cstdint>
#include <libopencad/dwg_sections.hpp>
#include <map>
#include <set>
#include <stdexcept>
#include <variant>
#include <vector>
namespace libopencad {
namespace {
using View = std::string_view;
void check(bool valid, const char *what) {
    if (!valid)
        throw std::invalid_argument(what);
}
View slice(View s, std::size_t offset, std::size_t count) {
    check(offset <= s.size() && count <= s.size() - offset, "Truncated DWG page");
    return s.substr(offset, count);
}
std::uint64_t number(View s, std::size_t pos, unsigned n = 4) {
    auto bytes = slice(s, pos, n);
    std::uint64_t v = 0;
    for (unsigned i = 0; i < n; i++)
        v |= std::uint64_t(static_cast<unsigned char>(bytes[i])) << (8 * i);
    return v;
}
std::uint32_t adler(std::uint32_t seed, View s) {
    unsigned a = seed & 65535, b = seed >> 16;
    for (unsigned char c : s) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    return a | (b << 16);
}
std::uint32_t crc32(View s) {
    std::uint32_t crc = ~0u;
    for (unsigned char c : s) {
        crc ^= c;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}
std::string inflate(View src, std::size_t limit) {
    std::size_t pos = 0;
    std::string out;
    auto byte = [&]() -> unsigned {
        check(pos < src.size(), "Truncated AC18 LZ77 stream");
        return static_cast<unsigned char>(src[pos++]);
    };
    auto extended = [&](std::size_t base) {
        unsigned n = byte();
        while (!n) {
            check(base <= limit, "Invalid AC18 run");
            base += 255;
            n = byte();
        }
        return base + n;
    };
    auto literals = [&](std::size_t n) {
        check(n <= limit - out.size(), "AC18 literals exceed page");
        out.append(slice(src, pos, n));
        pos += n;
    };
    while (pos < src.size()) {
        unsigned op = byte();
        if (op == 0x11)
            break;
        if (op < 16) {
            literals(op ? op + 3 : extended(18));
            continue;
        }
        std::size_t length, distance;
        unsigned trail;
        if (op >= 64) {
            length = (op >> 4) - 1;
            distance = 1 + ((op >> 2) & 3) + (byte() << 2);
            trail = op & 3;
        } else {
            bool far = op < 32;
            unsigned low = op & (far ? 7 : 31);
            length = low ? low + 2 : extended(far ? 9 : 33);
            unsigned first = byte();
            distance = (first >> 2) + (byte() << 6) + (far ? 0x4000 + ((op & 8) << 11) : 1);
            trail = first & 3;
        }
        check(distance && distance <= out.size() && length <= limit - out.size(),
              "Invalid AC18 back-reference");
        for (std::size_t i = 0; i < length; i++)
            out.push_back(out[out.size() - distance]);
        literals(trail);
    }
    return out;
}
struct PhysicalPage {
    std::size_t offset, size;
};
std::string system_page(View file, std::size_t address, std::uint32_t type) {
    std::string header(slice(file, address, 20));
    auto size = number(header, 4);
    check(number(header, 0) == type && size && size <= 16 * 1024 * 1024, "Invalid DWG system page");
    auto data = slice(file, address + 20, number(header, 8));
    auto stored = number(header, 16);
    header.replace(16, 4, 4, 0);
    check(stored == adler(adler(0, header), data), "DWG system page checksum mismatch");
    auto encoding = number(header, 12);
    check(encoding == 1 || encoding == 2, "Unknown DWG system page encoding");
    auto result = encoding == 2 ? inflate(data, size) : std::string(data);
    check(result.size() == size, "Truncated DWG system page payload");
    return result;
}
// Source-byte permutation for the last 0..31 literals in an AC21 run.
constexpr unsigned char literal_order[32][31] = {
    {},
    {0},
    {1, 0},
    {2, 1, 0},
    {0, 1, 2, 3},
    {4, 0, 1, 2, 3},
    {5, 1, 2, 3, 4, 0},
    {6, 5, 1, 2, 3, 4, 0},
    {0, 1, 2, 3, 4, 5, 6, 7},
    {8, 0, 1, 2, 3, 4, 5, 6, 7},
    {9, 1, 2, 3, 4, 5, 6, 7, 8, 0},
    {10, 9, 1, 2, 3, 4, 5, 6, 7, 8, 0},
    {8, 9, 10, 11, 0, 1, 2, 3, 4, 5, 6, 7},
    {12, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5, 6, 7},
    {13, 9, 10, 11, 12, 1, 2, 3, 4, 5, 6, 7, 8, 0},
    {14, 13, 9, 10, 11, 12, 1, 2, 3, 4, 5, 6, 7, 8, 0},
    {8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {9, 10, 11, 12, 13, 14, 15, 16, 8, 0, 1, 2, 3, 4, 5, 6, 7},
    {17, 9, 10, 11, 12, 13, 14, 15, 16, 1, 2, 3, 4, 5, 6, 7, 8, 0},
    {18, 17, 16, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {16, 17, 18, 19, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {20, 16, 17, 18, 19, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {21, 20, 16, 17, 18, 19, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {22, 21, 20, 16, 17, 18, 19, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {17, 18, 19, 20, 21, 22, 23, 24, 16, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {25, 17, 18, 19, 20, 21, 22, 23, 24, 16, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {26, 25, 17, 18, 19, 20, 21, 22, 23, 24, 16, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {24, 25, 26, 27, 16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7},
    {28, 24, 25, 26, 27, 16, 17, 18, 19, 20, 21, 22, 23, 8, 9,
     10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7},
    {29, 28, 24, 25, 26, 27, 16, 17, 18, 19, 20, 21, 22, 23, 8,
     9,  10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7},
    {30, 26, 27, 28, 29, 18, 19, 20, 21, 22, 23, 24, 25, 10, 11, 12,
     13, 14, 15, 16, 17, 2,  3,  4,  5,  6,  7,  8,  9,  1,  0},
};
std::string inflate21(View source, std::size_t expected) {
    std::string result;
    std::size_t pos = 0;
    auto byte = [&]() -> unsigned {
        check(pos < source.size(), "Truncated AC21 LZ77 input");
        return static_cast<unsigned char>(source[pos++]);
    };
    unsigned op = byte();
    std::size_t literals = 0;
    if ((op & 0xf0) == 0x20) {
        byte();
        byte();
        literals = byte() & 7;
    }
    while (pos < source.size() && result.size() < expected) {
        if (!literals) {
            literals = op + 8;
            if (literals == 23) {
                unsigned extra = byte();
                literals += extra;
                if (extra == 255) {
                    do {
                        extra = byte();
                        extra += byte() << 8;
                        literals += extra;
                    } while (extra == 65535 && literals <= expected);
                }
            }
        }
        check(literals <= expected - result.size(), "AC21 literal run exceeds page");
        auto bytes = slice(source, pos, literals);
        pos += literals;
        while (bytes.size() >= 32) {
            for (int part = 3; part >= 0; --part)
                result.append(bytes.substr(part * 8, 8));
            bytes.remove_prefix(32);
        }
        for (std::size_t i = 0; i < bytes.size(); ++i)
            result.push_back(bytes[literal_order[bytes.size()][i]]);
        if (result.size() == expected || pos == source.size())
            break;
        op = byte();
        for (;;) {
            auto kind = op >> 4;
            std::size_t length, distance;
            if (kind <= 1) {
                length = (op & 15) + (kind ? 3 : 19);
                distance = byte();
                op = byte();
                if (!kind)
                    length += (op >> 3) & 16;
                distance += 1 + ((op & (kind ? 248 : 120)) << 5);
            } else if (kind == 2) {
                distance = byte();
                distance += byte() << 8;
                length = op & 7;
                if (op & 8) {
                    ++distance;
                    length += 256 + (byte() << 3);
                    op = byte();
                    length += (op & 248) << 8;
                } else {
                    op = byte();
                    length += op & 248;
                }
            } else {
                distance = (op & 15) + 1;
                length = kind;
                op = byte();
                distance += (op & 248) << 1;
            }
            check(distance && distance <= result.size() && length <= expected - result.size(),
                  "Invalid AC21 back-reference");
            for (std::size_t i = 0; i < length; i++)
                result.push_back(result[result.size() - distance]);
            literals = op & 7;
            if (literals || pos == source.size() || result.size() == expected)
                break;
            op = byte();
            if (op < 16)
                break;
            if ((op >> 4) == 15)
                op &= 15;
        }
    }
    check(result.size() == expected, "Incomplete AC21 page");
    return result;
}
std::string encode_rs(View data, unsigned k);
std::string deinterleave(View bytes, std::size_t count, std::size_t factor, unsigned block) {
    auto physical = slice(bytes, 0, factor * 255);
    std::string result(factor * block, 0);
    for (std::size_t i = 0; i < result.size(); i++)
        result[i] = physical[(i % block) * factor + i / block];
    check(encode_rs(result, block) == physical, "AC21 Reed-Solomon parity mismatch");
    check(count <= result.size(), "Invalid AC21 interleave factor");
    result.resize(count);
    return result;
}
std::uint64_t crc21(View data, bool normal, std::uint64_t seed = 0);
std::string reordered_crc_input(View data);
std::string page21(View file, std::size_t offset, std::size_t compressed, std::size_t size,
                   std::size_t repeat, unsigned block, std::uint64_t expected_crc, std::uint64_t seed,
                   const detail::DwgCipherKey *key = nullptr) {
    check(compressed && compressed <= 16 * 1024 * 1024 && size <= 16 * 1024 * 1024 && repeat > 0,
          "Invalid AC21 page size");
    auto aligned = (compressed + 7) & ~std::size_t(7);
    check(repeat <= 100000000 / aligned, "Invalid AC21 repetition factor");
    auto factor = (aligned * repeat + block - 1) / block;
    auto stored = deinterleave(slice(file, offset, factor * 255), compressed, factor, block);
    if (key)
        detail::decrypt_dwg_page(stored, *key);
    else
        check(crc21(stored, false, seed) == expected_crc, "AC21 compressed page CRC mismatch");
    return compressed == size ? stored : inflate21(stored, size);
}
struct Directory21 {
    std::string header, metadata, catalog;
    std::map<std::uint64_t, PhysicalPage> pages;
};
Directory21 directory21(View file) {
    std::string h = deinterleave(slice(file, 128, 1024), 717, 3, 239);
    auto len = number(h, 24);
    auto stored = slice(h, 32, (len & 0x80000000u) ? 0x100000000ULL - len : len);
    check(crc21(stored, true) == number(h, 16, 8), "AC21 header payload CRC mismatch");
    auto metadata = (len & 0x80000000u) ? std::string(stored) : inflate21(stored, 272);
    check(metadata.size() == 272, "Invalid AC21 metadata size");
    auto masked = metadata;
    masked.replace(264, 8, 8, 0);
    check(crc21(masked, true) == number(metadata, 264, 8), "AC21 metadata CRC mismatch");
    auto field = [&](int word) { return number(metadata, word * 8, 8); };
    check(file.size() >= 1152 && field(7) <= file.size() - 1152, "Invalid AC21 page map address");
    auto map = page21(file, 1152 + field(7), field(10), field(11), field(3), 239, field(2), field(30));
    check(crc21(map, false, field(30)) == field(16), "AC21 page directory CRC mismatch");
    std::map<std::uint64_t, PhysicalPage> locs;
    std::size_t address = 1152;
    for (std::size_t i = 0; i < map.size(); i += 16) {
        auto size = number(map, i, 8), id = number(map, i + 8, 8);
        if (!size && !id)
            break;
        check(size <= file.size() - address, "AC21 physical page outside file");
        locs.emplace(id, PhysicalPage{address, std::size_t(size)});
        address += size;
    }
    auto loc = [&](std::uint64_t id) {
        auto it = locs.find(id);
        check(it != locs.end(), "Missing AC21 page");
        return it->second;
    };
    auto directory =
        page21(file, loc(field(24)).offset, field(22), field(25), field(27), 239, field(26), field(30));
    check(crc21(directory, false, field(30)) == field(21), "AC21 section directory CRC mismatch");
    return {h, metadata, directory, locs};
}
struct SectionView {
    View desc, entries;
    bool encrypted;
};
constexpr View security_section = "AcDb:Security";

bool section_name_matches(View stored, View name, bool wide) {
    if (!wide)
        return stored.substr(0, stored.find('\0')) == name;
    if (stored.size() % 2 || stored.size() / 2 < name.size())
        return false;
    for (std::size_t i = 0; i < stored.size() / 2; ++i)
        if (number(stored, i * 2, 2) != (i < name.size() ? static_cast<unsigned char>(name[i]) : 0))
            return false;
    return true;
}

std::optional<SectionView> find_section(View catalog, bool ac21, View name, bool *any_encrypted = nullptr) {
    std::optional<SectionView> result;
    std::size_t pos = ac21 ? 0 : 20;
    const auto count = ac21 ? 0 : number(catalog, 0);
    const std::size_t descriptor_size = ac21 ? 64 : 96;
    const std::size_t page_size = ac21 ? 56 : 16;
    for (std::size_t i = 0; ac21 ? pos < catalog.size() : i < count; ++i) {
        const auto desc = slice(catalog, pos, descriptor_size);
        pos += descriptor_size;
        const auto stored_name = ac21 ? slice(catalog, pos, number(desc, 32, 8)) : desc.substr(32, 64);
        if (ac21)
            pos += stored_name.size();
        const auto pages = ac21 ? number(desc, 56, 8) : number(desc, 8);
        check(pages <= (catalog.size() - pos) / page_size, "Truncated DWG page descriptors");
        const auto entries = slice(catalog, pos, pages * page_size);
        pos += entries.size();
        const bool encrypted = (ac21 ? number(desc, 16, 8) : number(desc, 28)) != 0;
        if (any_encrypted && encrypted)
            *any_encrypted = true;
        if (!section_name_matches(stored_name, name, ac21))
            continue;
        check(!result, "Duplicate DWG security section");
        result = SectionView{desc, entries, encrypted};
        if (!any_encrypted)
            break;
    }
    return result;
}

bool protected_catalog(View catalog, bool ac21) {
    bool any_encrypted = false;
    const auto security = find_section(catalog, ac21, security_section, &any_encrypted);
    return security.has_value() || any_encrypted;
}

std::string read21(View file, const Directory21 &directory, const SectionView &section,
                   const detail::DwgCipherKey *key) {
    const auto total = number(section.desc, 0, 8);
    check(total <= 1024ULL * 1024 * 1024, "Oversized AC21 section");
    std::string result;
    for (std::size_t pos = 0; pos < section.entries.size(); pos += 56) {
        const auto record = slice(section.entries, pos, 56);
        const auto page = directory.pages.find(number(record, 16, 8));
        check(page != directory.pages.end(), "Missing AC21 page");
        const auto physical = page->second;
        const auto size = number(record, 24, 8), comp = number(record, 32, 8);
        check(size <= 4 * 1024 * 1024 && result.size() <= total, "Invalid AC21 logical page");
        std::string bytes;
        if (number(section.desc, 48, 8) == 1) {
            bytes = slice(slice(file, physical.offset, physical.size), 0, size);
            if (key)
                detail::decrypt_dwg_page(bytes, *key);
        } else {
            bytes = page21(slice(file, physical.offset, physical.size), 0, comp, size, 1, 251,
                           number(record, 48, 8), number(directory.metadata, 240, 8), key);
        }
        check(number(record, 0, 8) == result.size(), "Invalid AC21 page offset");
        if (!key)
            check(adler(static_cast<std::uint32_t>(size * 0x343fd + 0x269ec3), reordered_crc_input(bytes)) ==
                      number(record, 40, 8),
                  "AC21 data page checksum mismatch");
        result.append(bytes, 0, std::min<std::size_t>(size, total - result.size()));
    }
    check(result.size() == total, "Incomplete AC21 section");
    return result;
}

struct Directory18 {
    std::string inner, catalog;
    std::map<unsigned, PhysicalPage> pages;
};
Directory18 directory18(View file) {
    auto sig = file.substr(0, 6);
    check(sig == "AC1018" || sig == "AC1024" || sig == "AC1027" || sig == "AC1032",
          "Expected AC18-family DWG");
    std::string inner(slice(file, 128, 108));
    std::uint32_t seed = 1;
    for (char &c : inner) {
        seed = seed * 0x343fd + 0x269ec3;
        c ^= static_cast<char>(seed >> 16);
    }
    check(inner.compare(0, 12, "AcFssFcAJMB\0", 12) == 0, "Invalid DWG inner header");
    auto crc = number(inner, 104);
    inner.replace(104, 4, 4, 0);
    check(crc == crc32(inner), "DWG inner header CRC mismatch");
    auto map_offset = number(inner, 84, 8);
    check(file.size() >= 256 && map_offset < file.size() - 256, "Invalid DWG page map location");
    auto mapping = system_page(file, 256 + map_offset, 0x41630e3b);
    std::map<unsigned, PhysicalPage> pages;
    std::size_t address = 256;
    for (std::size_t i = 0; i < mapping.size(); i += 8) {
        auto id = number(mapping, i), size = number(mapping, i + 4);
        if (!size || size > INT32_MAX)
            continue;
        if (id && id <= INT32_MAX)
            check(pages.emplace(id, PhysicalPage{address, std::size_t(size)}).second,
                  "Duplicate DWG page id");
        check(size <= SIZE_MAX - address, "DWG page address overflow");
        address += size;
    }
    auto catalog_page = pages.find(number(inner, 92));
    check(catalog_page != pages.end(), "Missing DWG section map");
    auto catalog = system_page(file, catalog_page->second.offset, 0x4163003b);
    return {inner, catalog, pages};
}
std::string read18(View file, const Directory18 &directory, const SectionView &section,
                   const detail::DwgCipherKey *key) {
    const auto total = number(section.desc, 0, 8), capacity = number(section.desc, 12);
    check(capacity <= 4 * 1024 * 1024 && total <= 1024ULL * 1024 * 1024, "DWG section size exceeds limit");
    std::string out;
    for (std::size_t pos = 0; pos < section.entries.size(); pos += 16) {
        auto page = directory.pages.find(number(section.entries, pos));
        check(page != directory.pages.end(), "Missing DWG data page");
        const auto offset = page->second.offset;
        const auto physical = slice(file, offset, page->second.size);
        std::string hdr(slice(physical, 0, 32));
        auto mask = 0x4164536bu ^ static_cast<std::uint32_t>(offset);
        for (unsigned k = 0; k < 32; ++k)
            hdr[k] ^= static_cast<char>(mask >> (8 * (k % 4)));
        const auto dst = number(hdr, 16, 8), len = number(hdr, 8);
        check(dst == number(section.entries, pos + 8, 8) && len == number(section.entries, pos + 4),
              "Inconsistent DWG data page descriptor");
        auto data = slice(physical, 32, len);
        std::string decrypted;
        // The pin skips page checksums for protected data because the coverage is unspecified.
        if (key) {
            decrypted = data;
            detail::decrypt_dwg_page(decrypted, *key);
            data = decrypted;
        } else {
            const auto payload_crc = number(hdr, 28), header_crc = number(hdr, 24);
            hdr.replace(24, 4, 4, 0);
            check(adler(0, data) == payload_crc && adler(payload_crc, hdr) == header_crc,
                  "DWG data page checksum mismatch");
        }
        auto contents = number(section.desc, 20) == 2 ? inflate(data, capacity) : std::string(data);
        check(dst <= total && dst >= out.size(), "Invalid DWG data page placement");
        out.resize(dst, 0);
        out.append(contents, 0, std::min<std::size_t>(contents.size(), total - dst));
    }
    check(total == 0 || total <= out.size() + capacity, "Invalid DWG section extent");
    out.resize(total, 0);
    return out;
}
} // namespace

struct detail::DwgPagedReader::Impl {
    using Directory = std::variant<Directory18, Directory21>;
    explicit Impl(View bytes)
        : file(bytes), directory(bytes.substr(0, 6) == "AC1021" ? Directory(directory21(bytes))
                                                              : Directory(directory18(bytes))) {}

    bool is_ac21() const { return std::holds_alternative<Directory21>(directory); }
    View catalog() const {
        return std::visit([](const auto &value) -> View { return value.catalog; }, directory);
    }
    std::string extract(const SectionView &section) const {
        check(!section.encrypted || key.has_value(), "Encrypted DWG section requires a verified password");
        const auto *cipher = section.encrypted ? &*key : nullptr;
        if (const auto *ac21 = std::get_if<Directory21>(&directory))
            return read21(file, *ac21, section, cipher);
        return read18(file, std::get<Directory18>(directory), section, cipher);
    }

    View file;
    Directory directory;
    std::optional<DwgCipherKey> key;
    bool protected_file = false;
};

detail::DwgPagedReader::DwgPagedReader(View file) : impl_(std::make_unique<Impl>(file)) {}
detail::DwgPagedReader::~DwgPagedReader() = default;

void detail::DwgPagedReader::authenticate(std::optional<View> password) {
    impl_->key.reset();
    bool any_encrypted = false;
    auto security = find_section(impl_->catalog(), impl_->is_ac21(), security_section, &any_encrypted);
    impl_->protected_file = any_encrypted || security.has_value();
    if (!impl_->protected_file)
        return;
    check(security && !security->encrypted, "Missing or encrypted AcDb:Security section");
    impl_->key = verify_dwg_password(impl_->extract(*security), password);
}

bool detail::DwgPagedReader::password_protected() const { return impl_->protected_file; }

std::string detail::DwgPagedReader::read(View name) const {
    auto section = find_section(impl_->catalog(), impl_->is_ac21(), name);
    check(section.has_value(), "DWG section not found");
    return impl_->extract(*section);
}

std::string read_dwg_paged_section(View file, View name) {
    return detail::DwgPagedReader(file).read(name);
}
namespace {
void put(std::string &s, std::size_t pos, std::uint64_t value, unsigned width = 4) {
    check(pos <= s.size() && width <= s.size() - pos, "DWG integer location out of bounds");
    for (unsigned i = 0; i < width; i++) {
        s[pos + i] = static_cast<char>(value);
        value >>= 8;
    }
    check(!value, "DWG integer field overflow");
}
void append_int(std::string &s, std::uint64_t v, unsigned width = 4) {
    auto pos = s.size();
    s.resize(pos + width);
    put(s, pos, v, width);
}
void mask_page(std::string &s, std::size_t offset) {
    auto mask = 0x4164536bu ^ static_cast<std::uint32_t>(offset);
    for (unsigned i = 0; i < 32; i++)
        s[i] ^= static_cast<char>(mask >> (8 * (i % 4)));
}
void align_page(std::string &s) { s.resize((s.size() + 31) & ~std::size_t(31), 0); }
std::string plain_system_page(unsigned type, View payload) {
    std::string h(20, 0);
    put(h, 0, type);
    put(h, 4, payload.size());
    put(h, 8, payload.size());
    put(h, 12, 1);
    put(h, 16, adler(adler(0, h), payload));
    h.append(payload);
    align_page(h);
    return h;
}
void relocate_preview(std::string &data, std::size_t from, std::size_t to) {
    if (from == to || data.size() < 37)
        return;
    auto count = number(data, 20, 1);
    check(21 + count * 9 + 16 <= data.size(), "Invalid preview descriptor table");
    for (std::size_t i = 0; i < count; i++) {
        auto field = 22 + i * 9;
        auto start = number(data, field), size = number(data, field + 4);
        check(start >= from && start - from <= data.size() && size <= data.size() - (start - from),
              "Invalid DWG preview image location");
        put(data, field, to + (start - from));
    }
}
} // namespace
std::string detail::replace_ac18_sections(View file, const std::map<std::string, std::string> &replacements) {
    auto directory = directory18(file);
    auto &catalog = directory.catalog;
    check(!number(file, 24) && !protected_catalog(catalog, false), "Secured DWG rewrite unsupported");
    std::string out(slice(file, 0, 256));
    auto count = number(catalog, 0);
    // The modeler writer may create the datastore on its first nonempty body.
    // Other missing replacement names still reject, preventing silent renames.
    if (replacements.count("AcDb:AcDsPrototype_1b") && file.substr(0, 6) >= "AC1027") {
        std::size_t scan = 20;
        std::uint64_t maximum = 0;
        bool present = false;
        for (std::size_t i = 0; i < count; ++i) {
            auto d = slice(catalog, scan, 96);
            scan += 96;
            auto n = number(d, 8);
            check(n <= (catalog.size() - scan) / 16, "Invalid AC18 page count");
            scan += n * 16;
            auto name = d.substr(32, 64);
            name = name.substr(0, name.find('\0'));
            present |= name == "AcDb:AcDsPrototype_1b";
            maximum = std::max(maximum, number(d, 24));
        }
        if (!present) {
            check(maximum < INT32_MAX, "DWG section id exhausted");
            std::string d(96, 0);
            put(d, 12, 0x7400);
            put(d, 16, 1);
            put(d, 20, 1);
            put(d, 24, maximum + 1);
            std::string name = "AcDb:AcDsPrototype_1b";
            d.replace(32, name.size(), name);
            catalog.insert(scan, d);
            put(catalog, 0, ++count);
            put(catalog, 16, count);
        }
    }
    std::string new_catalog(slice(catalog, 0, 20)), physical_map;
    unsigned page_id = 0;
    std::set<std::string> written;
    auto append_page = [&](View bytes) {
        append_int(physical_map, ++page_id);
        append_int(physical_map, bytes.size());
        out.append(bytes);
    };
    std::size_t pos = 20;
    for (std::size_t i = 0; i < count; i++) {
        std::string desc(slice(catalog, pos, 96));
        pos += 96;
        auto pages = number(desc, 8);
        check(pages <= (catalog.size() - pos) / 16, "Invalid AC18 section page count");
        auto entries = slice(catalog, pos, pages * 16);
        pos += entries.size();
        auto capacity = number(desc, 12);
        auto name = desc.substr(32, 64);
        name.resize(name.find('\0') == name.npos ? name.size() : name.find('\0'));
        auto replace = replacements.find(name);
        auto section_id = number(desc, 24);
        check(!number(desc, 28), "Encrypted DWG rewrite unsupported");
        std::string page_entries;
        auto register_page = [&](std::string &header, View bytes, std::size_t dst) {
            put(header, 8, bytes.size());
            auto padded = (32 + bytes.size() + 31) & ~std::size_t(31);
            put(header, 12, padded);
            put(header, 16, dst, 8);
            put(header, 24, 0);
            auto sum = adler(0, bytes);
            put(header, 28, sum);
            put(header, 24, adler(sum, header));
            mask_page(header, out.size());
            std::string page = header;
            page.append(bytes);
            align_page(page);
            append_page(page);
            append_int(page_entries, page_id);
            append_int(page_entries, bytes.size());
            append_int(page_entries, dst, 8);
        };
        if (replace != replacements.end()) {
            check(capacity && capacity <= 4 * 1024 * 1024 && written.insert(name).second,
                  "Invalid replaced section");
            auto &data = replace->second;
            put(desc, 0, data.size(), 8);
            put(desc, 20, 1);
            for (std::size_t off = 0; off < data.size(); off += capacity) {
                auto bytes = View(data).substr(off, capacity);
                std::string header(32, 0);
                put(header, 0, 0x4163043b);
                put(header, 4, section_id);
                register_page(header, bytes, off);
            }
        } else {
            for (std::size_t j = 0; j < pages; j++) {
                auto page = directory.pages.find(number(entries, j * 16));
                check(page != directory.pages.end(), "Missing original DWG page");
                auto location = page->second;
                auto original = slice(file, location.offset, location.size);
                std::string header(slice(original, 0, 32));
                mask_page(header, location.offset);
                auto bytes = slice(original, 32, number(header, 8));
                auto dst = number(header, 16, 8);
                check(dst == number(entries, j * 16 + 8, 8) && bytes.size() == number(entries, j * 16 + 4),
                      "Inconsistent original DWG data page");
                auto crc = number(header, 24), sum = number(header, 28);
                put(header, 24, 0);
                check(sum == adler(0, bytes) && crc == adler(sum, header),
                      "Invalid original DWG page checksum");
                if (!j) {
                    for (auto field : {13u, 32u, 36u, 44u})
                        if (number(file, field) == location.offset + 32)
                            put(out, field, out.size() + 32);
                }
                if (name == "AcDb:Preview" && !j && number(file, 13) == location.offset + 32) {
                    check(pages == 1 && number(desc, 20) != 2, "Unsupported split or compressed DWG preview");
                    std::string preview(bytes);
                    relocate_preview(preview, location.offset + 32, out.size() + 32);
                    register_page(header, preview, dst);
                } else
                    register_page(header, bytes, dst);
            }
        }
        put(desc, 8, page_entries.size() / 16);
        new_catalog += desc;
        new_catalog += page_entries;
    }
    check(written.size() == replacements.size(), "Missing replacement DWG section");
    append_page(plain_system_page(0x4163003b, new_catalog));
    auto map_address = out.size();
    append_int(physical_map, page_id + 1);
    append_int(physical_map, (physical_map.size() + 4 + 20 + 31) & ~std::size_t(31));
    out += plain_system_page(0x41630e3b, physical_map);
    auto &inner = directory.inner;
    put(inner, 40, page_id + 1);
    put(inner, 44, out.size() - 256, 8);
    put(inner, 52, out.size(), 8);
    for (auto gap : {24, 28, 32, 60, 100})
        put(inner, gap, 0);
    for (auto field : {64, 80, 96})
        put(inner, field, page_id + 1);
    put(inner, 84, map_address - 256, 8);
    put(inner, 92, page_id);
    put(inner, 104, crc32(inner));
    std::uint32_t seed = 1;
    for (char &c : inner) {
        seed = seed * 0x343fd + 0x269ec3;
        c ^= static_cast<char>(seed >> 16);
    }
    out.replace(128, inner.size(), inner);
    out += inner;
    return out;
}
namespace {
std::string reordered_crc_input(View data) {
    std::string out;
    out.reserve(data.size());
    while (data.size() >= 8) {
        for (int i = 6; i >= 0; i -= 2)
            out.append(data.substr(i, 2));
        data.remove_prefix(8);
    }
    if (data.size() >= 4) {
        out.append(data.substr(2, 2));
        out.append(data.substr(0, 2));
        data.remove_prefix(4);
    }
    out.append(data);
    return out;
}
std::uint64_t crc21(View data, bool normal, std::uint64_t seed) {
    std::uint64_t v = (seed + data.size()) * 0x343fd + 0x269ec3;
    v = normal ? ~(v * 0x1000343fdULL + data.size() + 0x269ec3) : ~(v | ((v * 0x343fd + 0x269ec3) << 32));
    for (unsigned char b : reordered_crc_input(data)) {
        if (normal) {
            v ^= std::uint64_t(b) << 56;
            for (int i = 0; i < 8; i++)
                v = (v << 1) ^ ((v >> 63) ? 0x42f0e1eba9ea3693ULL : 0);
        } else {
            v ^= b;
            for (int i = 0; i < 8; i++)
                v = (v >> 1) ^ ((v & 1) ? 0x95ac9329ac4bc9b5ULL : 0);
        }
    }
    return normal ? ~v : v;
}
// Systematic RS(255,k) over GF(256), with k consecutive roots starting at exponent k.
std::string encode_rs(View data, unsigned k) {
    auto count = (data.size() + k - 1) / k;
    unsigned exp[510]{}, log[256]{};
    unsigned v = 1;
    for (unsigned i = 0; i < 255; i++) {
        exp[i] = exp[i + 255] = v;
        log[v] = i;
        v <<= 1;
        if (v & 256)
            v ^= (k == 239 ? 0x169 : 0x11d);
    }
    auto mul = [&](unsigned a, unsigned b) { return a && b ? exp[log[a] + log[b]] : 0u; };
    std::vector<unsigned> gen{1};
    for (unsigned i = 0; i < 255 - k; i++) {
        gen.push_back(0);
        for (std::size_t j = gen.size() - 1; j; j--)
            gen[j] ^= mul(gen[j - 1], exp[k + i]);
    }
    std::string out(count * 255, 0);
    for (std::size_t n = 0; n < count; n++) {
        unsigned parity[16]{};
        for (unsigned i = 0; i < k; i++) {
            unsigned c = n * k + i < data.size() ? static_cast<unsigned char>(data[n * k + i]) : 0;
            out[i * count + n] = static_cast<char>(c);
            unsigned feedback = c ^ parity[0];
            for (unsigned j = 0; j < 255 - k; j++)
                parity[j] = (j + 1 < 255 - k ? parity[j + 1] : 0) ^ mul(gen[j + 1], feedback);
        }
        for (unsigned j = 0; j < 255 - k; j++)
            out[(k + j) * count + n] = static_cast<char>(parity[j]);
    }
    return out;
}
std::string system21(View data) {
    std::string one(data);
    one.resize((one.size() + 7) & ~std::size_t(7), 0);
    auto out = encode_rs(one + one, 239);
    align_page(out);
    return out;
}
} // namespace
std::string detail::replace_ac21_sections(View file, const std::map<std::string, std::string> &changes) {
    auto header = directory21(file);
    auto &meta = header.metadata;
    auto &catalog = header.catalog;
    check(!number(file, 24) && !protected_catalog(catalog, true), "Secured AC21 rewrite unsupported");
    auto crc_seed = number(meta, 30 * 8, 8);
    std::string out(slice(file, 0, 1152)), map, new_catalog;
    unsigned id = 0, found = 0;
    auto add = [&](View payload) {
        append_int(map, payload.size(), 8);
        append_int(map, ++id, 8);
        out.append(payload);
    };
    for (std::size_t pos = 0; pos < catalog.size();) {
        std::string desc(slice(catalog, pos, 64));
        pos += 64;
        auto name = slice(catalog, pos, number(desc, 32, 8));
        pos += name.size();
        std::string key;
        for (std::size_t i = 0; i < name.size(); i += 2) {
            auto ch = number(name, i, 2);
            if (!ch)
                break;
            key += ch <= 127 ? static_cast<char>(ch) : '?';
        }
        auto n = number(desc, 56, 8);
        check(n <= (catalog.size() - pos) / 56, "Invalid AC21 page count");
        auto entries = slice(catalog, pos, n * 56);
        pos += entries.size();
        std::string new_entries;
        auto change = changes.find(key);
        if (change == changes.end()) {
            for (std::size_t i = 0; i < n; i++) {
                std::string row(slice(entries, i * 56, 56));
                auto page = header.pages.at(number(row, 16, 8));
                auto raw = slice(file, page.offset, page.size);
                for (auto at : {13u, 32u, 36u, 44u})
                    if (number(file, at) == page.offset)
                        put(out, at, out.size());
                if (key == "AcDb:Preview" && page.offset != out.size() && number(row, 24, 8) > 37)
                    throw std::invalid_argument("Relocation of an AC21 image preview is unavailable");
                add(raw);
                put(row, 16, id, 8);
                new_entries += row;
            }
        } else {
            found++;
            auto &data = change->second;
            auto cap = number(desc, 8, 8);
            check(cap && cap <= 4 * 1024 * 1024, "Invalid AC21 page capacity");
            put(desc, 0, data.size(), 8);
            put(desc, 48, 4, 8);
            for (std::size_t offset = 0; offset < data.size(); offset += cap) {
                auto bytes = View(data).substr(offset, cap);
                std::string padded(bytes);
                padded.resize((padded.size() + 7) & ~std::size_t(7), 0);
                auto physical = encode_rs(padded, 251);
                align_page(physical);
                add(physical);
                auto sum = adler(static_cast<std::uint32_t>(bytes.size() * 0x343fd + 0x269ec3),
                                 reordered_crc_input(bytes));
                for (auto value : {std::uint64_t(offset), std::uint64_t(physical.size()), std::uint64_t(id),
                                   std::uint64_t(bytes.size()), std::uint64_t(bytes.size()),
                                   std::uint64_t(sum), crc21(bytes, false, crc_seed)})
                    append_int(new_entries, value, 8);
            }
        }
        put(desc, 56, new_entries.size() / 56, 8);
        new_catalog += desc;
        new_catalog.append(name);
        new_catalog += new_entries;
    }
    check(found == changes.size(), "Missing AC21 replacement section");
    auto field = [&](int word, std::uint64_t value) { put(meta, word * 8, value, 8); };
    auto directory = system21(new_catalog);
    add(directory);
    field(24, id);
    add(directory);
    field(23, id);
    field(21, crc21(new_catalog, false, crc_seed));
    field(26, number(meta, 21 * 8, 8));
    field(22, new_catalog.size());
    field(25, new_catalog.size());
    field(27, 2);
    auto map_size = map.size() + 48;
    auto encoded_size =
        (((((map_size + 7) & ~std::size_t(7)) * 2 + 238) / 239 * 255 + 31) & ~std::size_t(31));
    for (int i = 0; i < 2; i++) {
        append_int(map, encoded_size, 8);
        append_int(map, ++id, 8);
    }
    map.resize(map.size() + 16, 0);
    auto page = system21(map);
    check(page.size() == encoded_size, "Invalid AC21 page map encoding");
    field(7, out.size() - 1152);
    field(8, id - 1);
    out += page;
    field(5, out.size() - 1152);
    field(6, id);
    out += page;
    field(2, crc21(map, false, crc_seed));
    field(16, number(meta, 16, 8));
    field(3, 2);
    field(10, map.size());
    field(11, map.size());
    field(12, id);
    field(13, id);
    field(1, out.size() + 1024);
    field(9, out.size() - 1152);
    field(33, 0);
    field(33, crc21(meta, true));
    std::string payload = header.header.substr(0, 32);
    put(payload, 16, crc21(meta, true), 8);
    put(payload, 24, static_cast<std::uint32_t>(-272));
    put(payload, 28, 0);
    payload += meta;
    std::string repeated = payload + payload;
    repeated.resize(717, 0);
    auto rs = encode_rs(repeated, 239);
    std::string new_header(slice(file, 128, 1024));
    new_header.replace(0, rs.size(), rs);
    out.replace(128, 1024, new_header);
    out += new_header;
    return out;
}
} // namespace libopencad
