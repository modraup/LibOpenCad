// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace libopencad::detail {
inline constexpr std::string_view binary_signature{"AutoCAD Binary DXF\r\n\x1a\0", 22};
int read_binary_code(std::istream& in, unsigned first, bool compact);
std::string read_binary_value(std::istream& in, int code);
void write_binary_pair(std::ostream& out, bool compact, int code, const std::string& value);
// Encode a canonical ASCII group value for initializing binary defaults.
std::string binary_value_from_text(int code, const std::string& value);
std::string text_value_from_binary(int code, const std::string& value);
bool is_string_value(int code);
double binary_double(const std::string& bytes);
std::string binary_double(double value);
int binary_int32(const std::string& bytes);
std::string binary_int32_bits(std::uint32_t bits);
std::optional<std::uint32_t> as_i32_bits(const std::string& bytes, bool binary);
int binary_short(const std::string& bytes);
std::string binary_short(int value);
// Typed conversion used by the HEADER reader (acadrust DxfCodePair::as_i16).
std::optional<std::int16_t> as_i16(int code, const std::string& bytes, bool binary);
std::string binary_text(std::string_view text);
}
