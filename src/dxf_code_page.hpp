// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <string_view>

namespace libopencad::detail {
// Returns a static iconv name, or nullptr for drawings using UTF-8.
const char* legacy_page(std::string_view version, std::string_view declaration);
// acadrust tables::normalize_name, using the pinned Unicode mapping.
std::u32string symbol_name_key(std::string_view utf8);
std::string from_legacy(std::string_view bytes, const char* page);
std::string to_legacy(std::string_view utf8, const char* page);
}
