// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace libopencad::detail {
// Returns a static iconv name, or nullptr for drawings using UTF-8.
void valid_symbol_name(std::string_view name);
bool known_code_page(std::string_view declaration);
const char* legacy_page(std::string_view version, std::string_view declaration);
// acadrust tables::normalize_name, using the pinned Unicode mapping.
std::u32string utf8_scalars(std::string_view utf8);
std::string utf8_string(std::u32string_view scalars);
std::u32string symbol_name_key(std::string_view utf8);
enum class LegacyTextError { InvalidBytes, MalformedEscape, UnpairedSurrogate };
void recover_legacy_text(std::string& bytes, const char* page,
                         const std::function<void(LegacyTextError, std::size_t)>& report);
std::string decode_undeclared_text(std::string_view bytes, bool binary);
std::string from_legacy(std::string_view bytes, const char* page);
std::string to_legacy(std::string_view utf8, const char* page);
}
