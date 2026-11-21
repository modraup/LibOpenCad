// SPDX-License-Identifier: MPL-2.0
// DWG password derivation follows acadrust io/dwg/security.rs and RFC 1321.
#include "dwg_security.hpp"
#include "dxf_code_page.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace libopencad::detail {
namespace {
std::uint32_t read_u32(std::string_view bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        throw std::invalid_argument("Truncated AcDb:Security section");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    return value;
}

std::string password_utf16le(std::string_view password) {
    struct Mapping {
        char32_t source;
        char32_t upper[3];
    };
    // Supplement the single-character table with expansions and non-ASCII-to-ASCII mappings.
    static constexpr Mapping full_uppercase[] = {
#include "dwg_password_uppercase.inc"
    };
    std::string text;
    auto unit = [&](char32_t code) {
        text.push_back(static_cast<char>(code & 0xff));
        text.push_back(static_cast<char>(code >> 8));
    };
    auto scalar = [&](char32_t code) {
        if (code <= 0xffff) {
            unit(code);
        } else {
            code -= 0x10000;
            unit(0xd800 + (code >> 10));
            unit(0xdc00 + (code & 0x3ff));
        }
    };
    for (auto code : symbol_name_key(password)) {
        const auto found = std::lower_bound(std::begin(full_uppercase), std::end(full_uppercase), code,
                                            [](const Mapping &entry, char32_t c) { return entry.source < c; });
        if (found != std::end(full_uppercase) && found->source == code) {
            for (auto upper : found->upper)
                if (upper)
                    scalar(upper);
        } else {
            scalar(code);
        }
    }
    return text;
}

std::array<std::uint8_t, 16> md5(std::string message) {
    static constexpr std::uint32_t constants[] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static constexpr unsigned shifts[] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
    std::array<std::uint32_t, 4> state{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    const auto bit_count = std::uint64_t(message.size()) * 8;
    message.push_back(static_cast<char>(0x80));
    message.append((56 + 64 - message.size() % 64) % 64, '\0');
    for (unsigned i = 0; i < 8; ++i)
        message.push_back(static_cast<char>(bit_count >> (8 * i)));

    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::uint32_t words[16];
        for (unsigned i = 0; i < 16; ++i)
            words[i] = read_u32(message, offset + 4 * i);
        auto [a, b, c, d] = state;
        for (unsigned i = 0; i < 64; ++i) {
            std::uint32_t f;
            unsigned word;
            if (i < 16) {
                f = (b & c) | (~b & d);
                word = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                word = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                word = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                word = (7 * i) % 16;
            }
            const auto sum = a + f + constants[i] + words[word];
            const auto shift = shifts[(i / 16) * 4 + i % 4];
            a = d;
            d = c;
            c = b;
            b += (sum << shift) | (sum >> (32 - shift));
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
    }
    std::array<std::uint8_t, 16> digest;
    for (unsigned i = 0; i < digest.size(); ++i)
        digest[i] = static_cast<std::uint8_t>(state[i / 4] >> (8 * (i % 4)));
    return digest;
}
} // namespace

DwgCipherKey verify_dwg_password(std::string_view security, std::optional<std::string_view> password) {
    if (read_u32(security, 8) != 0xabcdabcd)
        throw std::invalid_argument("Invalid AcDb:Security section magic");
    const auto name_size = read_u32(security, 16);
    if (name_size > security.size() - 20)
        throw std::invalid_argument("Provider name exceeds security section length");
    const auto provider_end = std::size_t{20} + name_size;
    const auto algorithm = read_u32(security, provider_end);
    const auto key_size = read_u32(security, provider_end + 4) / 8;
    const auto verifier_size = read_u32(security, provider_end + 8);
    const auto verifier_offset = provider_end + 12;
    constexpr std::string_view expected = "SamirBajajSamirB";
    if (verifier_size != expected.size() || verifier_size > security.size() - verifier_offset)
        throw std::invalid_argument("Invalid AcDb:Security password verifier");
    if (!password)
        throw std::invalid_argument("Drawing is password protected; please provide a password");
    if (algorithm != 0x6801 || !key_size || key_size > 16)
        throw std::invalid_argument("Unsupported DWG password encryption algorithm or key length");

    DwgCipherKey key{md5(password_utf16le(*password)), key_size};
    std::string verifier(security.substr(verifier_offset, verifier_size));
    decrypt_dwg_page(verifier, key);
    unsigned difference = 0;
    for (std::size_t i = 0; i < verifier.size(); ++i)
        difference |= static_cast<unsigned char>(verifier[i]) ^ static_cast<unsigned char>(expected[i]);
    if (difference)
        throw std::invalid_argument("Invalid password for encrypted DWG drawing");
    return key;
}

void decrypt_dwg_page(std::string &page, const DwgCipherKey &key) {
    std::array<std::uint8_t, 256> state;
    std::iota(state.begin(), state.end(), 0u);
    unsigned j = 0;
    for (unsigned i = 0; i < state.size(); ++i) {
        j = (j + state[i] + key.bytes[i % key.size]) & 0xff;
        std::swap(state[i], state[j]);
    }
    unsigned i = 0;
    j = 0;
    for (char &byte : page) {
        i = (i + 1) & 0xff;
        j = (j + state[i]) & 0xff;
        std::swap(state[i], state[j]);
        byte = static_cast<char>(static_cast<unsigned char>(byte) ^ state[(state[i] + state[j]) & 0xff]);
    }
}
} // namespace libopencad::detail
