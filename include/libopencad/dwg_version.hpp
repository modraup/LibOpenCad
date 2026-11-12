// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/io/dwg/dwg_version.rs, DwgVersion::from_version_string.
#pragma once

#include <optional>
#include <string_view>

namespace libopencad {

// Stream encoding families; several drawing releases share one family.
enum class DwgVersion { AC9, AC12, AC15, AC18, AC21, AC24 };

constexpr std::optional<DwgVersion> from_version_string(std::string_view header) noexcept {
    if (header.size() != 6 || (header.substr(0, 2) != "AC" && header.substr(0, 2) != "AD")) {
        return std::nullopt;
    }
    const auto code = header.substr(2);
    if (code == "1009") return DwgVersion::AC9;
    if (code == "1012" || code == "1014") return DwgVersion::AC12;
    if (code == "1015") return DwgVersion::AC15;
    if (code == "1018") return DwgVersion::AC18;
    if (code == "1021") return DwgVersion::AC21;
    if (code == "1024" || code == "1027" || code == "1032") return DwgVersion::AC24;
    return std::nullopt;
}

} // namespace libopencad
