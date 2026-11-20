// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <string>
#include <string_view>
namespace libopencad {
// Decompress a named section from an AC18-family or AC21 (R2007) paged container.
std::string read_dwg_paged_section(std::string_view file, std::string_view name);
} // namespace libopencad
