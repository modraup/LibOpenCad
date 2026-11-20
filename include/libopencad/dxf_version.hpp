// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/types/mod.rs (DxfVersion).
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace libopencad {

enum class DxfVersion : std::uint16_t {
    Unknown = 0,
    AC1009 = 1009,
    AC1012 = 1012,
    AC1014 = 1014,
    AC1015 = 1015,
    AC1018 = 1018,
    AC1021 = 1021,
    AC1024 = 1024,
    AC1027 = 1027,
    AC1032 = 1032
};

namespace detail {
struct VersionName {
    DxfVersion version;
    std::string_view name;
};
inline constexpr VersionName dxf_versions[] = {
    {DxfVersion::AC1009, "AC1009"}, {DxfVersion::AC1012, "AC1012"}, {DxfVersion::AC1014, "AC1014"},
    {DxfVersion::AC1015, "AC1015"}, {DxfVersion::AC1018, "AC1018"}, {DxfVersion::AC1021, "AC1021"},
    {DxfVersion::AC1024, "AC1024"}, {DxfVersion::AC1027, "AC1027"}, {DxfVersion::AC1032, "AC1032"}};
} // namespace detail

constexpr DxfVersion dxf_version_from_string(std::string_view name) noexcept {
    if (name.size() == 6 && (name.substr(0, 2) == "AC" || name.substr(0, 2) == "AD")) {
        for (const auto &entry : detail::dxf_versions)
            if (entry.name.substr(2) == name.substr(2))
                return entry.version;
    }
    return DxfVersion::Unknown;
}

constexpr std::string_view dxf_version_string(DxfVersion version) noexcept {
    for (const auto &entry : detail::dxf_versions)
        if (entry.version == version)
            return entry.name;
    return "UNKNOWN";
}

// Predicts the pinned Rust writer's effective version without mutating a document.
// Planning is separate from conversion: an R12 plan requires its dedicated writer.
enum class DxfOutputPolicy { PreserveVersion, UpstreamCompatible };
struct DxfOutputPlan {
    DxfVersion source, requested, effective;
    bool binary, explicit_target, requires_r12_writer;
    bool changes_version() const noexcept { return source != effective; }
};
inline DxfOutputPlan dxf_output_plan(DxfVersion source, bool binary,
                                     std::optional<DxfVersion> target = std::nullopt,
                                     DxfOutputPolicy policy = DxfOutputPolicy::PreserveVersion) {
    auto known = [](DxfVersion v) { return dxf_version_string(v) != "UNKNOWN"; };
    if (!known(source) || (target && !known(*target)))
        throw std::invalid_argument("Cannot plan output for an unknown DXF version");
    auto requested = target.value_or(source);
    DxfOutputPlan plan{source, requested, requested, binary, target.has_value(), false};
    if (policy == DxfOutputPolicy::UpstreamCompatible) {
        if (target == DxfVersion::AC1009) {
            if (binary)
                throw std::invalid_argument("Pinned upstream supports explicit R12 output as ASCII only");
            plan.requires_r12_writer = true;
        } else if (binary && requested < DxfVersion::AC1021) {
            plan.effective = DxfVersion::AC1024;
        } else if (requested == DxfVersion::AC1009) {
            plan.effective = DxfVersion::AC1012;
        }
    } else if (target == DxfVersion::AC1009 && source != DxfVersion::AC1009) {
        plan.requires_r12_writer = true;
    }
    return plan;
}

} // namespace libopencad
