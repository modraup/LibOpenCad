// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <map>
#include <string>
#include <string_view>
namespace libopencad::detail {
std::string replace_ac21_sections(std::string_view file,
                                  const std::map<std::string, std::string> &replacements);
std::string replace_ac18_sections(std::string_view file,
                                  const std::map<std::string, std::string> &replacements);
} // namespace libopencad::detail
