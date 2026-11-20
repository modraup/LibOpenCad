// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/io/dwg/preview.rs.
#include <libopencad/preview.hpp>

#include <array>
#include <cstring>

namespace libopencad {
namespace {
constexpr std::uint8_t kCodeHeader = 1, kCodeBmp = 2, kCodeWmf = 3, kCodePng = 6;
constexpr std::size_t kHeaderLen = 80;
constexpr std::array<unsigned char, 16> kStart{
    0x1F, 0x25, 0x6D, 0x07, 0xD4, 0x36, 0x28, 0x28, 0x9D, 0x57, 0xCA, 0x3F, 0x9D, 0x44, 0x10, 0x2B};
constexpr std::array<unsigned char, 16> kEnd{
    0xE0, 0xDA, 0x92, 0xF8, 0x2B, 0xC9, 0xD7, 0xD7, 0x62, 0xA8, 0x35, 0xC0, 0x62, 0xBB, 0xEF, 0xD4};

std::uint32_t le32(std::string_view bytes, std::size_t at) {
    return static_cast<unsigned char>(bytes[at]) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3])) << 24);
}

void append_le32(std::string &out, std::uint32_t value) {
    out.push_back(static_cast<char>(value));
    out.push_back(static_cast<char>(value >> 8));
    out.push_back(static_cast<char>(value >> 16));
    out.push_back(static_cast<char>(value >> 24));
}

void push_descriptor(std::string &out, std::uint8_t code, std::uint64_t start, std::uint32_t size) {
    out.push_back(static_cast<char>(code));
    append_le32(out, static_cast<std::uint32_t>(start));
    append_le32(out, size);
}

bool starts_with_sentinel(std::string_view bytes) {
    if (bytes.size() < 16)
        return false;
    return std::memcmp(bytes.data(), kStart.data(), 16) == 0;
}
} // namespace

std::size_t dwg_preview_container_len(std::size_t overall_size) { return 16 + 4 + overall_size + 16; }

std::optional<std::size_t> dwg_preview_overall_size(std::string_view bytes) {
    if (bytes.size() < 20 || !starts_with_sentinel(bytes))
        return std::nullopt;
    return le32(bytes, 16);
}

std::optional<Preview> parse_dwg_preview(std::string_view bytes, std::uint64_t base) {
    if (bytes.size() < 21 || !starts_with_sentinel(bytes))
        return std::nullopt;
    const auto count = static_cast<unsigned char>(bytes[20]);
    std::size_t off = 21;
    for (unsigned i = 0; i < count; ++i) {
        if (off + 9 > bytes.size())
            break;
        const auto code = static_cast<unsigned char>(bytes[off]);
        const auto start = le32(bytes, off + 1);
        const auto size = static_cast<std::size_t>(le32(bytes, off + 5));
        off += 9;
        PreviewFormat format = PreviewFormat::Bmp;
        if (code == kCodeBmp)
            format = PreviewFormat::Bmp;
        else if (code == kCodeWmf)
            format = PreviewFormat::Wmf;
        else if (code == kCodePng)
            format = PreviewFormat::Png;
        else
            continue;
        if (start < base)
            continue;
        const auto rel = static_cast<std::size_t>(start - base);
        if (size == 0 || rel > bytes.size() || size > bytes.size() - rel)
            continue;
        Preview preview;
        preview.format = format;
        preview.data.assign(reinterpret_cast<const std::uint8_t *>(bytes.data() + rel),
                            reinterpret_cast<const std::uint8_t *>(bytes.data() + rel + size));
        return preview;
    }
    return std::nullopt;
}

std::string empty_dwg_preview() {
    std::string out;
    out.append(reinterpret_cast<const char *>(kStart.data()), kStart.size());
    append_le32(out, 1);
    out.push_back(0);
    out.append(reinterpret_cast<const char *>(kEnd.data()), kEnd.size());
    return out;
}

std::string build_dwg_preview(const Preview *preview, std::uint64_t base) {
    if (!preview || preview->data.empty())
        return empty_dwg_preview();
    std::uint8_t code = kCodePng;
    bool with_header = false;
    if (preview->format == PreviewFormat::Bmp) {
        code = kCodeBmp;
        with_header = true;
    } else if (preview->format == PreviewFormat::Wmf)
        code = kCodeWmf;
    const std::size_t count = with_header ? 2 : 1;
    const std::size_t descriptors_len = 9 * count;
    const std::size_t data_rel = 16 + 4 + 1 + descriptors_len;
    const std::size_t header_bytes = with_header ? kHeaderLen : 0;
    const auto overall =
        static_cast<std::uint32_t>(1 + descriptors_len + header_bytes + preview->data.size());
    std::string out;
    out.reserve(dwg_preview_container_len(overall));
    out.append(reinterpret_cast<const char *>(kStart.data()), kStart.size());
    append_le32(out, overall);
    out.push_back(static_cast<char>(count));
    if (with_header) {
        const auto hstart = base + data_rel;
        push_descriptor(out, kCodeHeader, hstart, static_cast<std::uint32_t>(kHeaderLen));
        push_descriptor(out, code, hstart + kHeaderLen, static_cast<std::uint32_t>(preview->data.size()));
        out.append(kHeaderLen, '\0');
        out.append(reinterpret_cast<const char *>(preview->data.data()),
                   static_cast<std::size_t>(preview->data.size()));
    } else {
        push_descriptor(out, code, base + data_rel, static_cast<std::uint32_t>(preview->data.size()));
        out.append(reinterpret_cast<const char *>(preview->data.data()),
                   static_cast<std::size_t>(preview->data.size()));
    }
    out.append(reinterpret_cast<const char *>(kEnd.data()), kEnd.size());
    return out;
}
} // namespace libopencad
