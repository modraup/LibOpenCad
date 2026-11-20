// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstddef>
#include <string_view>
namespace libopencad::acis::detail {
// Scan a complete SAB through its typed end record without allocating token payloads.
// Throws SabError on invalid framing, truncation or a missing end record.
std::size_t sab_extent(std::string_view data);
} // namespace libopencad::acis::detail
