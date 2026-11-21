// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace libopencad::detail {
struct DwgCipherKey {
    std::array<std::uint8_t, 16> bytes;
    std::size_t size;
};

DwgCipherKey verify_dwg_password(std::string_view security, std::optional<std::string_view> password);
void decrypt_dwg_page(std::string &page, const DwgCipherKey &key);
} // namespace libopencad::detail
