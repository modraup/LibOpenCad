// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace libopencad {
struct LwVertex {
    double x, y;
    double bulge; // Outgoing segment, including last-to-first when closed.
    double start_width, end_width;
    int vertex_id;
};
} // namespace libopencad
