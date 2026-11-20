// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <libopencad/dwg_r2000.hpp>
#include <libopencad/dxf_version.hpp>
#include <set>
namespace libopencad::detail {
DwgBitWriter encode_dwg_history_parameters(const DwgHistoryRecord &, DxfVersion);
void read_dwg_history_parameters(DwgBitReader &, DwgBitReader &, std::size_t, DxfVersion, DwgHistoryRecord &);
void read_dwg_surface_body(DwgBitReader &, std::size_t, DxfVersion, DwgModeler &);
DwgBitWriter encode_dwg_surface(const DwgSurface &, DxfVersion);
void read_dwg_modeler_body(DwgBitReader &main, DwgBitReader &handles, std::size_t main_end,
                           std::uint64_t handle, DxfVersion version, DwgModeler &modeler);
std::map<std::uint64_t, std::vector<std::uint8_t>> decode_dwg_acds(std::string_view,
                                                                   const std::set<std::uint64_t> &);
std::string replace_dwg_acds_body(std::string_view, std::uint64_t, const std::vector<std::uint8_t> &);
std::string insert_dwg_acds_body(std::string_view, std::uint64_t, const std::vector<std::uint8_t> &);
std::string remove_dwg_acds_body(std::string_view, std::uint64_t);
void attach_dwg_modeler_data(std::string_view file, DwgR2000Objects &objects);
} // namespace libopencad::detail
