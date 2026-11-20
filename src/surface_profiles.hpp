// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "libopencad/surface.hpp"
#include "libopencad/dxf_raw_field.hpp"
namespace libopencad::detail {
enum class SurfaceProfileLayout { Extruded, Lofted, Revolved, Swept };
// Fields contain only the profile region, excluding the loft matrix and options.
SurfaceProfiles decode_surface_profiles(SurfaceProfileLayout, const std::vector<DxfRawField> &, DxfVersion);
std::vector<DxfRawField> encode_surface_profiles(SurfaceProfileLayout, const SurfaceProfiles &, DxfVersion);
SurfaceProfiles decode_loft_profile_metadata(const std::vector<DxfRawField> &fields);
std::vector<DxfRawField> encode_loft_profile_metadata(const SurfaceProfiles &profiles);
} // namespace libopencad::detail
