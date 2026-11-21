// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <libopencad/audit.hpp>
#include <libopencad/dwg_r2000.hpp>
#include <libopencad/dwg_read.hpp>

namespace libopencad::detail {
struct DwgReadData {
    DwgR2000Objects objects;
    bool writable = true;
};

DwgReadData read_dwg_with_stats(std::string_view bytes, DwgReadStatistics &stats,
                               std::optional<std::string_view> password);
DwgReadData recover_dwg_bytes(std::string &bytes, DwgReadStatistics &stats, RepairReport &repair,
                             std::optional<std::string_view> password);
} // namespace libopencad::detail
