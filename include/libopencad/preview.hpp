// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/document.rs Preview and src/io/dwg/preview.rs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace libopencad {

enum class PreviewFormat { Bmp, Wmf, Png };

struct Preview {
    PreviewFormat format = PreviewFormat::Bmp;
    std::vector<std::uint8_t> data;
    bool operator==(const Preview &other) const {
        return format == other.format && data == other.data;
    }
    bool operator!=(const Preview &other) const { return !(*this == other); }
};

std::size_t dwg_preview_container_len(std::size_t overall_size);
std::optional<std::size_t> dwg_preview_overall_size(std::string_view bytes);
std::optional<Preview> parse_dwg_preview(std::string_view bytes, std::uint64_t base);
std::string build_dwg_preview(const Preview *preview, std::uint64_t base);
std::string empty_dwg_preview();

} // namespace libopencad
