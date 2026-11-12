// SPDX-License-Identifier: MPL-2.0
// acadrust: io/dxf/group_code_value.rs, reader/binary_reader.rs, writer/binary_writer.rs
#include "dxf_binary.hpp"

#include <charconv>
#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <locale>
#include <sstream>
#include <cmath>
#include <ostream>
#include <stdexcept>

namespace libopencad::detail {
namespace {
enum class Layout { string, chunk, boolean, int16, int32, int64, real };

Layout layout(int code) {
    const auto in = [code](int low, int high) { return low <= code && code <= high; };
    if (in(310, 319) || code == 1004) return Layout::chunk;
    if (in(290, 299)) return Layout::boolean;
    // Byte groups 280–289 also occupy two bytes in binary DXF.
    if (in(60, 79) || in(170, 179) || in(270, 289) || in(370, 389) ||
        in(400, 409) || in(1060, 1070)) return Layout::int16;
    if (in(90, 99) || in(420, 429) || in(440, 459) || code == 1071) return Layout::int32;
    if (in(160, 169)) return Layout::int64;
    if (in(10, 59) || in(110, 149) || in(210, 239) ||
        in(460, 469) || in(1010, 1059)) return Layout::real;
    return Layout::string; // Strings, handles and upstream's fallback.
}

unsigned byte(std::istream& in) {
    const int c = in.get();
    if (c == std::char_traits<char>::eof()) throw std::runtime_error("Truncated binary DXF");
    return static_cast<unsigned>(c);
}

std::string fixed_bytes(std::istream& in, std::size_t size) {
    std::string result(size, '\0');
    if (!in.read(result.data(), size)) throw std::runtime_error("Truncated binary DXF value");
    return result;
}
std::optional<std::int64_t> ascii_integer(const std::string& bytes) {
    std::int64_t value{};
    const auto begin = bytes.find_first_not_of(" \t\r\n\v\f");
    if (begin == bytes.npos) return std::nullopt;
    const char* first = bytes.data() + begin;
    const char* last = bytes.data() + bytes.find_last_not_of(" \t\r\n\v\f") + 1;
    if (*first == '+') {
        ++first;
        if (first == last || *first == '-') return std::nullopt;
    }
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last) return std::nullopt;
    return value;
}
} // namespace

int read_binary_code(std::istream& in, unsigned first, bool compact) {
    if (compact && first != 255) return static_cast<int>(first);
    if (compact) first = byte(in);
    return static_cast<int>(first | (byte(in) << 8));
}

std::string read_binary_value(std::istream& in, int code) {
    switch (layout(code)) {
        case Layout::boolean: return fixed_bytes(in, 1);
        case Layout::int16: return fixed_bytes(in, 2);
        case Layout::int32: return fixed_bytes(in, 4);
        case Layout::int64:
        case Layout::real: return fixed_bytes(in, 8);
        case Layout::chunk: return fixed_bytes(in, byte(in));
        case Layout::string: {
            std::string result;
            if (!std::getline(in, result, '\0') || in.eof())
                throw std::runtime_error("Unterminated binary DXF string");
            return result;
        }
    }
    throw std::logic_error("Unknown binary DXF layout");
}

void write_binary_pair(std::ostream& out, bool compact, int code, const std::string& value) {
    if (compact && code >= 255) out.put(static_cast<char>(255));
    out.put(static_cast<char>(code & 255));
    if (!compact || code >= 255) out.put(static_cast<char>(code >> 8));
    const auto kind = layout(code);
    if (kind == Layout::chunk) out.put(static_cast<char>(value.size()));
    out.write(value.data(), value.size());
    if (kind == Layout::string) out.put('\0');
}

std::string binary_value_from_text(int code, const std::string& text) {
    const auto kind = layout(code);
    if (kind == Layout::string) return text;
    if (kind == Layout::real) {
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        double value;
        if (!(in >> value) || !(in >> std::ws).eof() || !std::isfinite(value))
            throw std::invalid_argument("Invalid DXF default real");
        return binary_double(value);
    }
    if (kind == Layout::chunk) {
        if (text.size() % 2 || text.size() > 510)
            throw std::invalid_argument("Invalid DXF default binary chunk");
        std::string bytes;
        for (std::size_t i = 0; i < text.size(); i += 2) {
            unsigned value;
            const auto parsed = std::from_chars(text.data() + i, text.data() + i + 2, value, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + i + 2)
                throw std::invalid_argument("Invalid DXF default hex byte");
            bytes.push_back(static_cast<char>(value));
        }
        return bytes;
    }
    const auto value = ascii_integer(text);
    if (!value) throw std::invalid_argument("Invalid DXF default integer");
    if (kind == Layout::boolean) {
        if (*value != 0 && *value != 1) throw std::invalid_argument("Invalid DXF default boolean");
        return std::string(1, static_cast<char>(*value));
    }
    if (kind == Layout::int16) {
        if (*value < INT16_MIN || *value > INT16_MAX) throw std::out_of_range("DXF default int16");
        return binary_short(static_cast<int>(*value));
    }
    if (kind == Layout::int32) {
        if (*value < INT32_MIN || *value > INT32_MAX) throw std::out_of_range("DXF default int32");
        return binary_int32_bits(static_cast<std::uint32_t>(*value));
    }
    std::string bytes(8, '\0');
    const auto bits = static_cast<std::uint64_t>(*value);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>((bits >> (8 * i)) & 255);
    return bytes;
}

bool is_string_value(int code) { return layout(code) == Layout::string; }

std::string text_value_from_binary(int code, const std::string& bytes) {
    const auto kind = layout(code);
    if (kind == Layout::string) return bytes;
    if (kind == Layout::chunk) {
        std::string result;
        constexpr char digits[] = "0123456789ABCDEF";
        for (unsigned char b : bytes) { result += digits[b >> 4]; result += digits[b & 15]; }
        return result;
    }
    if (kind == Layout::real) {
        const double value = binary_double(bytes);
        if (!std::isfinite(value)) throw std::invalid_argument("Non-finite binary DXF real");
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out.precision(std::numeric_limits<double>::max_digits10);
        out << value;
        return out.str();
    }
    if (kind == Layout::int16) return std::to_string(binary_short(bytes));
    if (kind == Layout::int32) return std::to_string(binary_int32(bytes));
    if (kind == Layout::boolean) {
        const auto value = static_cast<unsigned char>(bytes.at(0));
        if (value > 1) throw std::invalid_argument("Invalid binary DXF boolean");
        return std::to_string(value);
    }
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < 8; ++i) bits |= std::uint64_t(static_cast<unsigned char>(bytes.at(i))) << (8 * i);
    // Avoid implementation-defined unsigned-to-signed overflow.
    if (bits <= INT64_MAX) return std::to_string(bits);
    return "-" + std::to_string(~bits + 1);
}

static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
              "Binary DXF requires IEEE 754 binary64");
double binary_double(const std::string& bytes) {
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < 8; ++i)
        bits |= std::uint64_t(static_cast<unsigned char>(bytes.at(i))) << (8 * i);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string binary_double(double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    std::string bytes(8, '\0');
    for (std::size_t i = 0; i < 8; ++i) bytes[i] = static_cast<char>((bits >> (8 * i)) & 255);
    return bytes;
}

int binary_int32(const std::string& bytes) {
    std::int64_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= std::int64_t(static_cast<unsigned char>(bytes.at(i))) << (8 * i);
    if (value >= 0x80000000LL) value -= 0x100000000LL;
    return static_cast<int>(value);
}

std::string binary_int32_bits(std::uint32_t bits) {
    std::string result(4, '\0');
    for (std::size_t i = 0; i < 4; ++i)
        result[i] = static_cast<char>((bits >> (8 * i)) & 255);
    return result;
}

std::optional<std::uint32_t> as_i32_bits(const std::string& bytes, bool binary) {
    if (binary) return static_cast<std::uint32_t>(binary_int32(bytes));
    const auto value = ascii_integer(bytes);
    if (!value || *value < INT32_MIN || *value > UINT32_MAX) return std::nullopt;
    return static_cast<std::uint32_t>(*value);
}

int binary_short(const std::string& bytes) {
    int n = static_cast<unsigned char>(bytes.at(0)) | (static_cast<unsigned char>(bytes.at(1)) << 8);
    return n < 0x8000 ? n : n - 0x10000;
}

std::string binary_short(int value) {
    const auto bits = static_cast<std::uint16_t>(value);
    return {static_cast<char>(bits & 255), static_cast<char>(bits >> 8)};
}

std::optional<std::int16_t> as_i16(int code, const std::string& bytes, bool binary) {
    const auto kind = layout(code);
    if (kind != Layout::boolean && kind != Layout::int16 && kind != Layout::int32 && kind != Layout::int64)
        return std::nullopt;
    std::int64_t value = 0;
    if (binary) {
        if (kind == Layout::boolean) return bytes.at(0) != 0;
        // Sign-extend from the most significant byte, then consume the little-endian payload.
        value = static_cast<unsigned char>(bytes.back());
        if (value >= 128) value -= 256;
        for (std::size_t i = bytes.size() - 1; i > 0; --i)
            value = value * 256 + static_cast<unsigned char>(bytes[i - 1]);
    } else {
        const auto parsed = ascii_integer(bytes);
        if (!parsed) return std::nullopt;
        value = *parsed;
        if (kind == Layout::boolean) {
            if (value < INT32_MIN || value > INT32_MAX) return std::nullopt;
            return value != 0;
        }
    }
    if (value < INT16_MIN || value > INT16_MAX) return std::nullopt;
    return static_cast<std::int16_t>(value);
}

std::string binary_text(std::string_view text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\0') continue;
        if (text[i] == '\r' || text[i] == '\n') {
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            result += "\\P";
        } else result += text[i];
    }
    return result;
}
} // namespace libopencad::detail
