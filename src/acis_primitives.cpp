// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/entities/acis/primitives.rs at 99d5f488.
#include "libopencad/acis.hpp"
#include <cmath>
namespace libopencad::acis {
namespace {
SatPointer ptr(std::int32_t i) { return {i}; }
} // namespace
SatDocument build_box(Vector3 center, double length, double width, double height) {
    auto sat = SatDocument::new_body();
    const auto body_idx = SatPointer{0};

    const auto hl = length / 2.0;
    const auto hw = width / 2.0;
    const auto hh = height / 2.0;
    const auto cx = center.x, cy = center.y, cz = center.z;

    const auto p0 = sat.add_point(cx - hl, cy - hw, cz - hh);
    const auto p1 = sat.add_point(cx + hl, cy - hw, cz - hh);
    const auto p2 = sat.add_point(cx + hl, cy + hw, cz - hh);
    const auto p3 = sat.add_point(cx - hl, cy + hw, cz - hh);
    const auto p4 = sat.add_point(cx - hl, cy - hw, cz + hh);
    const auto p5 = sat.add_point(cx + hl, cy - hw, cz + hh);
    const auto p6 = sat.add_point(cx + hl, cy + hw, cz + hh);
    const auto p7 = sat.add_point(cx - hl, cy + hw, cz + hh);

    const auto surf_top = sat.add_plane_surface({cx, cy, cz + hh}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0});
    const auto surf_bottom = sat.add_plane_surface({cx, cy, cz - hh}, {0.0, 0.0, -1.0}, {1.0, 0.0, 0.0});
    const auto surf_front = sat.add_plane_surface({cx, cy - hw, cz}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0});
    const auto surf_back = sat.add_plane_surface({cx, cy + hw, cz}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0});
    const auto surf_right = sat.add_plane_surface({cx + hl, cy, cz}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    const auto surf_left = sat.add_plane_surface({cx - hl, cy, cz}, {-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});

    // Bottom edges
    const auto crv_b0 = sat.add_straight_curve({cx - hl, cy - hw, cz - hh}, {1.0, 0.0, 0.0});
    const auto crv_b1 = sat.add_straight_curve({cx + hl, cy - hw, cz - hh}, {0.0, 1.0, 0.0});
    const auto crv_b2 = sat.add_straight_curve({cx + hl, cy + hw, cz - hh}, {-1.0, 0.0, 0.0});
    const auto crv_b3 = sat.add_straight_curve({cx - hl, cy + hw, cz - hh}, {0.0, -1.0, 0.0});
    // Top edges
    const auto crv_t0 = sat.add_straight_curve({cx - hl, cy - hw, cz + hh}, {1.0, 0.0, 0.0});
    const auto crv_t1 = sat.add_straight_curve({cx + hl, cy - hw, cz + hh}, {0.0, 1.0, 0.0});
    const auto crv_t2 = sat.add_straight_curve({cx + hl, cy + hw, cz + hh}, {-1.0, 0.0, 0.0});
    const auto crv_t3 = sat.add_straight_curve({cx - hl, cy + hw, cz + hh}, {0.0, -1.0, 0.0});
    // Vertical edges
    const auto crv_v0 = sat.add_straight_curve({cx - hl, cy - hw, cz - hh}, {0.0, 0.0, 1.0});
    const auto crv_v1 = sat.add_straight_curve({cx + hl, cy - hw, cz - hh}, {0.0, 0.0, 1.0});
    const auto crv_v2 = sat.add_straight_curve({cx + hl, cy + hw, cz - hh}, {0.0, 0.0, 1.0});
    const auto crv_v3 = sat.add_straight_curve({cx - hl, cy + hw, cz - hh}, {0.0, 0.0, 1.0});

    const auto v0 = sat.add_vertex(SatPointer{}, ptr(p0));
    const auto v1 = sat.add_vertex(SatPointer{}, ptr(p1));
    const auto v2 = sat.add_vertex(SatPointer{}, ptr(p2));
    const auto v3 = sat.add_vertex(SatPointer{}, ptr(p3));
    const auto v4 = sat.add_vertex(SatPointer{}, ptr(p4));
    const auto v5 = sat.add_vertex(SatPointer{}, ptr(p5));
    const auto v6 = sat.add_vertex(SatPointer{}, ptr(p6));
    const auto v7 = sat.add_vertex(SatPointer{}, ptr(p7));

    const auto e0 = sat.add_edge(ptr(v0), 0.0, ptr(v1), length, SatPointer{}, ptr(crv_b0), Sense::Forward);
    const auto e1 = sat.add_edge(ptr(v1), 0.0, ptr(v2), width, SatPointer{}, ptr(crv_b1), Sense::Forward);
    const auto e2 = sat.add_edge(ptr(v2), 0.0, ptr(v3), length, SatPointer{}, ptr(crv_b2), Sense::Forward);
    const auto e3 = sat.add_edge(ptr(v3), 0.0, ptr(v0), width, SatPointer{}, ptr(crv_b3), Sense::Forward);
    const auto e4 = sat.add_edge(ptr(v4), 0.0, ptr(v5), length, SatPointer{}, ptr(crv_t0), Sense::Forward);
    const auto e5 = sat.add_edge(ptr(v5), 0.0, ptr(v6), width, SatPointer{}, ptr(crv_t1), Sense::Forward);
    const auto e6 = sat.add_edge(ptr(v6), 0.0, ptr(v7), length, SatPointer{}, ptr(crv_t2), Sense::Forward);
    const auto e7 = sat.add_edge(ptr(v7), 0.0, ptr(v4), width, SatPointer{}, ptr(crv_t3), Sense::Forward);
    const auto e8 = sat.add_edge(ptr(v0), 0.0, ptr(v4), height, SatPointer{}, ptr(crv_v0), Sense::Forward);
    const auto e9 = sat.add_edge(ptr(v1), 0.0, ptr(v5), height, SatPointer{}, ptr(crv_v1), Sense::Forward);
    const auto e10 = sat.add_edge(ptr(v2), 0.0, ptr(v6), height, SatPointer{}, ptr(crv_v2), Sense::Forward);
    const auto e11 = sat.add_edge(ptr(v3), 0.0, ptr(v7), height, SatPointer{}, ptr(crv_v3), Sense::Forward);

    const auto base = static_cast<std::int32_t>(sat.records.size());
    const auto co_base = base;
    const auto loop_base = base + 24;
    const auto face_base = base + 30;
    const auto shell_idx = base + 36;
    const auto lump_idx = base + 37;
    auto co = [=](std::int32_t i) { return co_base + i; };

    // Bottom face
    sat.add_coedge(ptr(co(1)), ptr(co(3)), ptr(co(8)), ptr(e0), Sense::Reversed, ptr(loop_base));
    sat.add_coedge(ptr(co(2)), ptr(co(0)), ptr(co(20)), ptr(e3), Sense::Reversed, ptr(loop_base));
    sat.add_coedge(ptr(co(3)), ptr(co(1)), ptr(co(12)), ptr(e2), Sense::Reversed, ptr(loop_base));
    sat.add_coedge(ptr(co(0)), ptr(co(2)), ptr(co(16)), ptr(e1), Sense::Reversed, ptr(loop_base));
    // Top face
    sat.add_coedge(ptr(co(5)), ptr(co(7)), ptr(co(10)), ptr(e4), Sense::Forward, ptr(loop_base + 1));
    sat.add_coedge(ptr(co(6)), ptr(co(4)), ptr(co(18)), ptr(e5), Sense::Forward, ptr(loop_base + 1));
    sat.add_coedge(ptr(co(7)), ptr(co(5)), ptr(co(14)), ptr(e6), Sense::Forward, ptr(loop_base + 1));
    sat.add_coedge(ptr(co(4)), ptr(co(6)), ptr(co(22)), ptr(e7), Sense::Forward, ptr(loop_base + 1));
    // Front face
    sat.add_coedge(ptr(co(9)), ptr(co(11)), ptr(co(0)), ptr(e0), Sense::Forward, ptr(loop_base + 2));
    sat.add_coedge(ptr(co(10)), ptr(co(8)), ptr(co(19)), ptr(e9), Sense::Forward, ptr(loop_base + 2));
    sat.add_coedge(ptr(co(11)), ptr(co(9)), ptr(co(4)), ptr(e4), Sense::Reversed, ptr(loop_base + 2));
    sat.add_coedge(ptr(co(8)), ptr(co(10)), ptr(co(21)), ptr(e8), Sense::Reversed, ptr(loop_base + 2));
    // Back face
    sat.add_coedge(ptr(co(13)), ptr(co(15)), ptr(co(2)), ptr(e2), Sense::Forward, ptr(loop_base + 3));
    sat.add_coedge(ptr(co(14)), ptr(co(12)), ptr(co(23)), ptr(e11), Sense::Forward, ptr(loop_base + 3));
    sat.add_coedge(ptr(co(15)), ptr(co(13)), ptr(co(6)), ptr(e6), Sense::Reversed, ptr(loop_base + 3));
    sat.add_coedge(ptr(co(12)), ptr(co(14)), ptr(co(17)), ptr(e10), Sense::Reversed, ptr(loop_base + 3));
    // Right face
    sat.add_coedge(ptr(co(17)), ptr(co(19)), ptr(co(3)), ptr(e1), Sense::Forward, ptr(loop_base + 4));
    sat.add_coedge(ptr(co(18)), ptr(co(16)), ptr(co(15)), ptr(e10), Sense::Forward, ptr(loop_base + 4));
    sat.add_coedge(ptr(co(19)), ptr(co(17)), ptr(co(5)), ptr(e5), Sense::Reversed, ptr(loop_base + 4));
    sat.add_coedge(ptr(co(16)), ptr(co(18)), ptr(co(9)), ptr(e9), Sense::Reversed, ptr(loop_base + 4));
    // Left face
    sat.add_coedge(ptr(co(21)), ptr(co(23)), ptr(co(1)), ptr(e3), Sense::Forward, ptr(loop_base + 5));
    sat.add_coedge(ptr(co(22)), ptr(co(20)), ptr(co(11)), ptr(e8), Sense::Forward, ptr(loop_base + 5));
    sat.add_coedge(ptr(co(23)), ptr(co(21)), ptr(co(7)), ptr(e7), Sense::Reversed, ptr(loop_base + 5));
    sat.add_coedge(ptr(co(20)), ptr(co(22)), ptr(co(13)), ptr(e11), Sense::Reversed, ptr(loop_base + 5));

    sat.add_loop(SatPointer{}, ptr(co(0)), ptr(face_base));
    sat.add_loop(SatPointer{}, ptr(co(4)), ptr(face_base + 1));
    sat.add_loop(SatPointer{}, ptr(co(8)), ptr(face_base + 2));
    sat.add_loop(SatPointer{}, ptr(co(12)), ptr(face_base + 3));
    sat.add_loop(SatPointer{}, ptr(co(16)), ptr(face_base + 4));
    sat.add_loop(SatPointer{}, ptr(co(20)), ptr(face_base + 5));

    sat.add_face(ptr(face_base + 1), ptr(loop_base), ptr(shell_idx), ptr(surf_bottom), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 2), ptr(loop_base + 1), ptr(shell_idx), ptr(surf_top), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 3), ptr(loop_base + 2), ptr(shell_idx), ptr(surf_front), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 4), ptr(loop_base + 3), ptr(shell_idx), ptr(surf_back), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 5), ptr(loop_base + 4), ptr(shell_idx), ptr(surf_right), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(SatPointer{}, ptr(loop_base + 5), ptr(shell_idx), ptr(surf_left), Sense::Forward,
                 Sidedness::Single);

    sat.add_shell(ptr(face_base), ptr(lump_idx));
    sat.add_lump(ptr(shell_idx), body_idx);

    if (auto *body_rec = sat.record(0)) {
        body_rec->tokens[1] = SatToken(ptr(lump_idx));
    }
    sat.complete_brep_links();
    return sat;
}

// Build a right triangular prism (wedge) with one corner at `origin`.
//
// The wedge has a right-triangle cross-section in the XY plane:
// - Leg along X with length `length`
// - Leg along Y with length `width`
// - Hypotenuse connecting (length, 0) to (0, width)
//
// Extruded along Z by `height`.
SatDocument build_wedge(Vector3 origin, double length, double width, double height) {
    auto sat = SatDocument::new_body();
    const auto body_idx = SatPointer{0};
    const auto ox = origin.x, oy = origin.y, oz = origin.z;

    const auto hyp_len = std::sqrt(length * length + width * width);
    const auto s = 1.0 / hyp_len; // normalization factor
    const auto nx = width * s;    // hypotenuse normal X component
    const auto ny = length * s;   // hypotenuse normal Y component

    const auto p_a = sat.add_point(ox, oy, oz);
    const auto p_b = sat.add_point(ox + length, oy, oz);
    const auto p_c = sat.add_point(ox, oy + width, oz);
    const auto p_d = sat.add_point(ox, oy, oz + height);
    const auto p_e = sat.add_point(ox + length, oy, oz + height);
    const auto p_f = sat.add_point(ox, oy + width, oz + height);

    const auto surf_bot = sat.add_plane_surface({ox, oy, oz}, {0.0, 0.0, -1.0}, {1.0, 0.0, 0.0});
    const auto surf_top = sat.add_plane_surface({ox, oy, oz + height}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0});
    const auto surf_frt = sat.add_plane_surface({ox, oy, oz}, {0.0, -1.0, 0.0}, {1.0, 0.0, 0.0});
    const auto surf_lft = sat.add_plane_surface({ox, oy, oz}, {-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    const Vector3 hyp_mid = {ox + length / 2.0, oy + width / 2.0, oz};
    const auto surf_hyp = sat.add_plane_surface(hyp_mid, {nx, ny, 0.0}, {0.0, 0.0, 1.0});

    // Hypotenuse edge direction (unit vector from B to C)
    const auto hx = -length / hyp_len;
    const auto hy = width / hyp_len;

    const auto crv0 = sat.add_straight_curve({ox, oy, oz}, {1.0, 0.0, 0.0});
    const auto crv1 = sat.add_straight_curve({ox + length, oy, oz}, {hx, hy, 0.0});
    const auto crv2 = sat.add_straight_curve({ox, oy + width, oz}, {0.0, -1.0, 0.0});
    const auto crv3 = sat.add_straight_curve({ox, oy, oz + height}, {1.0, 0.0, 0.0});
    const auto crv4 = sat.add_straight_curve({ox + length, oy, oz + height}, {hx, hy, 0.0});
    const auto crv5 = sat.add_straight_curve({ox, oy + width, oz + height}, {0.0, -1.0, 0.0});
    const auto crv6 = sat.add_straight_curve({ox, oy, oz}, {0.0, 0.0, 1.0});
    const auto crv7 = sat.add_straight_curve({ox + length, oy, oz}, {0.0, 0.0, 1.0});
    const auto crv8 = sat.add_straight_curve({ox, oy + width, oz}, {0.0, 0.0, 1.0});

    const auto v_a = sat.add_vertex(SatPointer{}, ptr(p_a));
    const auto v_b = sat.add_vertex(SatPointer{}, ptr(p_b));
    const auto v_c = sat.add_vertex(SatPointer{}, ptr(p_c));
    const auto v_d = sat.add_vertex(SatPointer{}, ptr(p_d));
    const auto v_e = sat.add_vertex(SatPointer{}, ptr(p_e));
    const auto v_f = sat.add_vertex(SatPointer{}, ptr(p_f));

    const auto e0 = sat.add_edge(ptr(v_a), 0.0, ptr(v_b), length, SatPointer{}, ptr(crv0), Sense::Forward);
    const auto e1 = sat.add_edge(ptr(v_b), 0.0, ptr(v_c), hyp_len, SatPointer{}, ptr(crv1), Sense::Forward);
    const auto e2 = sat.add_edge(ptr(v_c), 0.0, ptr(v_a), width, SatPointer{}, ptr(crv2), Sense::Forward);
    const auto e3 = sat.add_edge(ptr(v_d), 0.0, ptr(v_e), length, SatPointer{}, ptr(crv3), Sense::Forward);
    const auto e4 = sat.add_edge(ptr(v_e), 0.0, ptr(v_f), hyp_len, SatPointer{}, ptr(crv4), Sense::Forward);
    const auto e5 = sat.add_edge(ptr(v_f), 0.0, ptr(v_d), width, SatPointer{}, ptr(crv5), Sense::Forward);
    const auto e6 = sat.add_edge(ptr(v_a), 0.0, ptr(v_d), height, SatPointer{}, ptr(crv6), Sense::Forward);
    const auto e7 = sat.add_edge(ptr(v_b), 0.0, ptr(v_e), height, SatPointer{}, ptr(crv7), Sense::Forward);
    const auto e8 = sat.add_edge(ptr(v_c), 0.0, ptr(v_f), height, SatPointer{}, ptr(crv8), Sense::Forward);

    const auto base = static_cast<std::int32_t>(sat.records.size());
    auto co = [=](std::int32_t i) { return base + i; };
    const auto loop_base = base + 18;
    const auto face_base = base + 23;
    const auto shell_idx = base + 28;
    const auto lump_idx = base + 29;

    // Bottom: A→C→B
    sat.add_coedge(ptr(co(1)), ptr(co(2)), ptr(co(13)), ptr(e2), Sense::Reversed, ptr(loop_base));
    sat.add_coedge(ptr(co(2)), ptr(co(0)), ptr(co(14)), ptr(e1), Sense::Reversed, ptr(loop_base));
    sat.add_coedge(ptr(co(0)), ptr(co(1)), ptr(co(6)), ptr(e0), Sense::Reversed, ptr(loop_base));
    // Top: D→E→F
    sat.add_coedge(ptr(co(4)), ptr(co(5)), ptr(co(8)), ptr(e3), Sense::Forward, ptr(loop_base + 1));
    sat.add_coedge(ptr(co(5)), ptr(co(3)), ptr(co(16)), ptr(e4), Sense::Forward, ptr(loop_base + 1));
    sat.add_coedge(ptr(co(3)), ptr(co(4)), ptr(co(11)), ptr(e5), Sense::Forward, ptr(loop_base + 1));
    // Front: A→B→E→D
    sat.add_coedge(ptr(co(7)), ptr(co(9)), ptr(co(2)), ptr(e0), Sense::Forward, ptr(loop_base + 2));
    sat.add_coedge(ptr(co(8)), ptr(co(6)), ptr(co(17)), ptr(e7), Sense::Forward, ptr(loop_base + 2));
    sat.add_coedge(ptr(co(9)), ptr(co(7)), ptr(co(3)), ptr(e3), Sense::Reversed, ptr(loop_base + 2));
    sat.add_coedge(ptr(co(6)), ptr(co(8)), ptr(co(10)), ptr(e6), Sense::Reversed, ptr(loop_base + 2));
    // Left: A→D→F→C
    sat.add_coedge(ptr(co(11)), ptr(co(13)), ptr(co(9)), ptr(e6), Sense::Forward, ptr(loop_base + 3));
    sat.add_coedge(ptr(co(12)), ptr(co(10)), ptr(co(5)), ptr(e5), Sense::Reversed, ptr(loop_base + 3));
    sat.add_coedge(ptr(co(13)), ptr(co(11)), ptr(co(15)), ptr(e8), Sense::Reversed, ptr(loop_base + 3));
    sat.add_coedge(ptr(co(10)), ptr(co(12)), ptr(co(0)), ptr(e2), Sense::Forward, ptr(loop_base + 3));
    // Hypotenuse: B→C→F→E
    sat.add_coedge(ptr(co(15)), ptr(co(17)), ptr(co(1)), ptr(e1), Sense::Forward, ptr(loop_base + 4));
    sat.add_coedge(ptr(co(16)), ptr(co(14)), ptr(co(12)), ptr(e8), Sense::Forward, ptr(loop_base + 4));
    sat.add_coedge(ptr(co(17)), ptr(co(15)), ptr(co(4)), ptr(e4), Sense::Reversed, ptr(loop_base + 4));
    sat.add_coedge(ptr(co(14)), ptr(co(16)), ptr(co(7)), ptr(e7), Sense::Reversed, ptr(loop_base + 4));

    sat.add_loop(SatPointer{}, ptr(co(0)), ptr(face_base));
    sat.add_loop(SatPointer{}, ptr(co(3)), ptr(face_base + 1));
    sat.add_loop(SatPointer{}, ptr(co(6)), ptr(face_base + 2));
    sat.add_loop(SatPointer{}, ptr(co(10)), ptr(face_base + 3));
    sat.add_loop(SatPointer{}, ptr(co(14)), ptr(face_base + 4));

    sat.add_face(ptr(face_base + 1), ptr(loop_base), ptr(shell_idx), ptr(surf_bot), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 2), ptr(loop_base + 1), ptr(shell_idx), ptr(surf_top), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 3), ptr(loop_base + 2), ptr(shell_idx), ptr(surf_frt), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 4), ptr(loop_base + 3), ptr(shell_idx), ptr(surf_lft), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(SatPointer{}, ptr(loop_base + 4), ptr(shell_idx), ptr(surf_hyp), Sense::Forward,
                 Sidedness::Single);

    sat.add_shell(ptr(face_base), ptr(lump_idx));
    sat.add_lump(ptr(shell_idx), body_idx);

    if (auto *body_rec = sat.record(0)) {
        body_rec->tokens[1] = SatToken(ptr(lump_idx));
    }
    sat.complete_brep_links();
    return sat;
}

// Build a square-base pyramid with its base centered at `center`.
//
// - `base_size`: side length of the square base
// - `height`: distance from the base plane to the apex along Z
SatDocument build_pyramid(Vector3 center, double base_size, double height) {
    auto sat = SatDocument::new_body();
    const auto body_idx = SatPointer{0};
    const auto cx = center.x, cy = center.y, cz = center.z;

    const auto hb = base_size / 2.0;
    const auto s5 = std::sqrt(1.0 + ((height / hb) * (height / hb))); // slant normalization
    const auto n_horiz = (height / hb) / s5;
    const auto n_vert = 1.0 / s5;
    const auto lat_len = std::sqrt(hb * hb + hb * hb + height * height);
    const auto s6 = lat_len; // edge length from corner to apex

    // Lateral edge direction factors
    const auto inv_lat = 1.0 / lat_len;

    const auto p_a = sat.add_point(cx - hb, cy - hb, cz);
    const auto p_b = sat.add_point(cx + hb, cy - hb, cz);
    const auto p_c = sat.add_point(cx + hb, cy + hb, cz);
    const auto p_d = sat.add_point(cx - hb, cy + hb, cz);
    const auto p_e = sat.add_point(cx, cy, cz + height);

    const auto surf_base = sat.add_plane_surface({cx, cy, cz}, {0.0, 0.0, -1.0}, {1.0, 0.0, 0.0});
    const auto surf_front =
        sat.add_plane_surface({cx, cy - hb, cz}, {0.0, -n_horiz, n_vert}, {1.0, 0.0, 0.0});
    const auto surf_right = sat.add_plane_surface({cx + hb, cy, cz}, {n_horiz, 0.0, n_vert}, {0.0, 1.0, 0.0});
    const auto surf_back = sat.add_plane_surface({cx, cy + hb, cz}, {0.0, n_horiz, n_vert}, {1.0, 0.0, 0.0});
    const auto surf_left = sat.add_plane_surface({cx - hb, cy, cz}, {-n_horiz, 0.0, n_vert}, {0.0, 1.0, 0.0});

    const auto crv0 = sat.add_straight_curve({cx - hb, cy - hb, cz}, {1.0, 0.0, 0.0});
    const auto crv1 = sat.add_straight_curve({cx + hb, cy - hb, cz}, {0.0, 1.0, 0.0});
    const auto crv2 = sat.add_straight_curve({cx + hb, cy + hb, cz}, {-1.0, 0.0, 0.0});
    const auto crv3 = sat.add_straight_curve({cx - hb, cy + hb, cz}, {0.0, -1.0, 0.0});
    const auto crv4 =
        sat.add_straight_curve({cx - hb, cy - hb, cz}, {hb * inv_lat, hb * inv_lat, height * inv_lat});
    const auto crv5 =
        sat.add_straight_curve({cx + hb, cy - hb, cz}, {-hb * inv_lat, hb * inv_lat, height * inv_lat});
    const auto crv6 =
        sat.add_straight_curve({cx + hb, cy + hb, cz}, {-hb * inv_lat, -hb * inv_lat, height * inv_lat});
    const auto crv7 =
        sat.add_straight_curve({cx - hb, cy + hb, cz}, {hb * inv_lat, -hb * inv_lat, height * inv_lat});

    const auto v_a = sat.add_vertex(SatPointer{}, ptr(p_a));
    const auto v_b = sat.add_vertex(SatPointer{}, ptr(p_b));
    const auto v_c = sat.add_vertex(SatPointer{}, ptr(p_c));
    const auto v_d = sat.add_vertex(SatPointer{}, ptr(p_d));
    const auto v_e = sat.add_vertex(SatPointer{}, ptr(p_e));

    const auto e0 = sat.add_edge(ptr(v_a), 0.0, ptr(v_b), base_size, SatPointer{}, ptr(crv0), Sense::Forward);
    const auto e1 = sat.add_edge(ptr(v_b), 0.0, ptr(v_c), base_size, SatPointer{}, ptr(crv1), Sense::Forward);
    const auto e2 = sat.add_edge(ptr(v_c), 0.0, ptr(v_d), base_size, SatPointer{}, ptr(crv2), Sense::Forward);
    const auto e3 = sat.add_edge(ptr(v_d), 0.0, ptr(v_a), base_size, SatPointer{}, ptr(crv3), Sense::Forward);
    const auto e4 = sat.add_edge(ptr(v_a), 0.0, ptr(v_e), s6, SatPointer{}, ptr(crv4), Sense::Forward);
    const auto e5 = sat.add_edge(ptr(v_b), 0.0, ptr(v_e), s6, SatPointer{}, ptr(crv5), Sense::Forward);
    const auto e6 = sat.add_edge(ptr(v_c), 0.0, ptr(v_e), s6, SatPointer{}, ptr(crv6), Sense::Forward);
    const auto e7 = sat.add_edge(ptr(v_d), 0.0, ptr(v_e), s6, SatPointer{}, ptr(crv7), Sense::Forward);

    const auto base = static_cast<std::int32_t>(sat.records.size());
    auto co = [=](std::int32_t i) { return base + i; };
    const auto loop_base = base + 16;
    const auto face_base = base + 21;
    const auto shell_idx = base + 26;
    const auto lump_idx = base + 27;

    // Base: A→D→C→B
    sat.add_coedge(ptr(co(1)), ptr(co(3)), ptr(co(13)), ptr(e3), Sense::Reversed, ptr(loop_base));
    sat.add_coedge(ptr(co(2)), ptr(co(0)), ptr(co(10)), ptr(e2), Sense::Reversed, ptr(loop_base));
    sat.add_coedge(ptr(co(3)), ptr(co(1)), ptr(co(7)), ptr(e1), Sense::Reversed, ptr(loop_base));
    sat.add_coedge(ptr(co(0)), ptr(co(2)), ptr(co(4)), ptr(e0), Sense::Reversed, ptr(loop_base));
    // Front: A→B→E
    sat.add_coedge(ptr(co(5)), ptr(co(6)), ptr(co(3)), ptr(e0), Sense::Forward, ptr(loop_base + 1));
    sat.add_coedge(ptr(co(6)), ptr(co(4)), ptr(co(9)), ptr(e5), Sense::Forward, ptr(loop_base + 1));
    sat.add_coedge(ptr(co(4)), ptr(co(5)), ptr(co(14)), ptr(e4), Sense::Reversed, ptr(loop_base + 1));
    // Right: B→C→E
    sat.add_coedge(ptr(co(8)), ptr(co(9)), ptr(co(2)), ptr(e1), Sense::Forward, ptr(loop_base + 2));
    sat.add_coedge(ptr(co(9)), ptr(co(7)), ptr(co(12)), ptr(e6), Sense::Forward, ptr(loop_base + 2));
    sat.add_coedge(ptr(co(7)), ptr(co(8)), ptr(co(5)), ptr(e5), Sense::Reversed, ptr(loop_base + 2));
    // Back: C→D→E
    sat.add_coedge(ptr(co(11)), ptr(co(12)), ptr(co(1)), ptr(e2), Sense::Forward, ptr(loop_base + 3));
    sat.add_coedge(ptr(co(12)), ptr(co(10)), ptr(co(15)), ptr(e7), Sense::Forward, ptr(loop_base + 3));
    sat.add_coedge(ptr(co(10)), ptr(co(11)), ptr(co(8)), ptr(e6), Sense::Reversed, ptr(loop_base + 3));
    // Left: D→A→E
    sat.add_coedge(ptr(co(14)), ptr(co(15)), ptr(co(0)), ptr(e3), Sense::Forward, ptr(loop_base + 4));
    sat.add_coedge(ptr(co(15)), ptr(co(13)), ptr(co(6)), ptr(e4), Sense::Forward, ptr(loop_base + 4));
    sat.add_coedge(ptr(co(13)), ptr(co(14)), ptr(co(11)), ptr(e7), Sense::Reversed, ptr(loop_base + 4));

    sat.add_loop(SatPointer{}, ptr(co(0)), ptr(face_base));
    sat.add_loop(SatPointer{}, ptr(co(4)), ptr(face_base + 1));
    sat.add_loop(SatPointer{}, ptr(co(7)), ptr(face_base + 2));
    sat.add_loop(SatPointer{}, ptr(co(10)), ptr(face_base + 3));
    sat.add_loop(SatPointer{}, ptr(co(13)), ptr(face_base + 4));

    sat.add_face(ptr(face_base + 1), ptr(loop_base), ptr(shell_idx), ptr(surf_base), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 2), ptr(loop_base + 1), ptr(shell_idx), ptr(surf_front), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 3), ptr(loop_base + 2), ptr(shell_idx), ptr(surf_right), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 4), ptr(loop_base + 3), ptr(shell_idx), ptr(surf_back), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(SatPointer{}, ptr(loop_base + 4), ptr(shell_idx), ptr(surf_left), Sense::Forward,
                 Sidedness::Single);

    sat.add_shell(ptr(face_base), ptr(lump_idx));
    sat.add_lump(ptr(shell_idx), body_idx);

    if (auto *body_rec = sat.record(0)) {
        body_rec->tokens[1] = SatToken(ptr(lump_idx));
    }
    sat.complete_brep_links();
    return sat;
}

// Build a cylinder centered at `center` with the given `radius` and `height`.
//
// The cylinder axis is along Z. The base sits at `center.z` and the top at
// `center.z + height`.
SatDocument build_cylinder(Vector3 center, double radius, double height) {
    auto sat = SatDocument::new_body();
    const auto body_idx = SatPointer{0};
    const auto cx = center.x, cy = center.y, cz = center.z;
    const auto tau = 6.283185307179586476925286766559;

    const auto p0 = sat.add_point(cx + radius, cy, cz);
    const auto p1 = sat.add_point(cx + radius, cy, cz + height);

    const auto surf_bot = sat.add_plane_surface({cx, cy, cz}, {0.0, 0.0, -1.0}, {1.0, 0.0, 0.0});
    const auto surf_top = sat.add_plane_surface({cx, cy, cz + height}, {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0});
    const auto surf_cyl =
        sat.add_cone_surface({cx, cy, cz}, {0.0, 0.0, 1.0}, {radius, 0.0, 0.0}, 1.0, 1.0, 0.0);

    const auto crv_bot = sat.add_ellipse_curve({cx, cy, cz}, {0.0, 0.0, -1.0}, {radius, 0.0, 0.0}, 1.0);
    const auto crv_top =
        sat.add_ellipse_curve({cx, cy, cz + height}, {0.0, 0.0, 1.0}, {radius, 0.0, 0.0}, 1.0);

    const auto v0 = sat.add_vertex(SatPointer{}, ptr(p0));
    const auto v1 = sat.add_vertex(SatPointer{}, ptr(p1));

    const auto e_bot = sat.add_edge(ptr(v0), 0.0, ptr(v0), tau, SatPointer{}, ptr(crv_bot), Sense::Forward);
    const auto e_top = sat.add_edge(ptr(v1), 0.0, ptr(v1), tau, SatPointer{}, ptr(crv_top), Sense::Forward);

    // Cylindrical face uses two separate loops (one per boundary circle),
    // matching the ACIS convention — no seam edge needed.
    const auto base = static_cast<std::int32_t>(sat.records.size());
    auto co = [=](std::int32_t i) { return base + i; };
    const auto loop_base = base + 4;
    const auto face_base = base + 8;
    const auto shell_idx = base + 11;
    const auto lump_idx = base + 12;

    // co0: bottom circle on bottom-cap face (single-coedge loop)
    sat.add_coedge(ptr(co(0)), ptr(co(0)), ptr(co(2)), ptr(e_bot), Sense::Forward, ptr(loop_base));
    // co1: top circle on top-cap face (single-coedge loop)
    sat.add_coedge(ptr(co(1)), ptr(co(1)), ptr(co(3)), ptr(e_top), Sense::Forward, ptr(loop_base + 1));
    // co2: bottom circle boundary on cylindrical face (single-coedge loop)
    sat.add_coedge(ptr(co(2)), ptr(co(2)), ptr(co(0)), ptr(e_bot), Sense::Reversed, ptr(loop_base + 2));
    // co3: top circle boundary on cylindrical face (single-coedge loop)
    sat.add_coedge(ptr(co(3)), ptr(co(3)), ptr(co(1)), ptr(e_top), Sense::Reversed, ptr(loop_base + 3));

    // Bottom cap loop, top cap loop, cylindrical face loop (bottom), cylindrical face loop (top)
    sat.add_loop(SatPointer{}, ptr(co(0)), ptr(face_base));
    sat.add_loop(SatPointer{}, ptr(co(1)), ptr(face_base + 1));
    sat.add_loop(ptr(loop_base + 3), ptr(co(2)), ptr(face_base + 2)); // next_loop = top boundary loop
    sat.add_loop(SatPointer{}, ptr(co(3)), ptr(face_base + 2));       // second loop on same cyl face

    sat.add_face(ptr(face_base + 1), ptr(loop_base), ptr(shell_idx), ptr(surf_bot), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(ptr(face_base + 2), ptr(loop_base + 1), ptr(shell_idx), ptr(surf_top), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(SatPointer{}, ptr(loop_base + 2), ptr(shell_idx), ptr(surf_cyl), Sense::Forward,
                 Sidedness::Single);

    sat.add_shell(ptr(face_base), ptr(lump_idx));
    sat.add_lump(ptr(shell_idx), body_idx);

    if (auto *body_rec = sat.record(0)) {
        body_rec->tokens[1] = SatToken(ptr(lump_idx));
    }
    sat.complete_brep_links();
    return sat;
}

// Build a cone with its base centered at `center`.
//
// - `radius`: radius of the circular base
// - `height`: distance from the base to the apex along Z
//
// The apex is at `center.z + height`.
// The cone face has two loops: the base circle boundary and a degenerate
// (singularity) edge at the apex.
SatDocument build_cone(Vector3 center, double radius, double height) {
    auto sat = SatDocument::new_body();
    const auto body_idx = SatPointer{0};
    const auto cx = center.x, cy = center.y, cz = center.z;
    const auto tau = 6.283185307179586476925286766559;
    const auto hyp = std::sqrt(radius * radius + height * height);
    const auto sin_half = -radius / hyp;
    const auto cos_half = height / hyp;

    const auto p_base = sat.add_point(cx + radius, cy, cz);
    const auto p_apex = sat.add_point(cx, cy, cz + height);

    const auto surf_base = sat.add_plane_surface({cx, cy, cz}, {0.0, 0.0, -1.0}, {1.0, 0.0, 0.0});
    const auto surf_cone =
        sat.add_cone_surface({cx, cy, cz}, {0.0, 0.0, 1.0}, {radius, 0.0, 0.0}, 1.0, cos_half, sin_half);

    const auto crv_base = sat.add_ellipse_curve({cx, cy, cz}, {0.0, 0.0, -1.0}, {radius, 0.0, 0.0}, 1.0);

    const auto v0 = sat.add_vertex(SatPointer{}, ptr(p_base));
    const auto v_apex = sat.add_vertex(SatPointer{}, ptr(p_apex));

    const auto e_base = sat.add_edge(ptr(v0), 0.0, ptr(v0), tau, SatPointer{}, ptr(crv_base), Sense::Forward);
    // Singularity (degenerate) edge at the apex — zero-length, no curve
    const auto e_apex =
        sat.add_edge(ptr(v_apex), 1.0, ptr(v_apex), 0.0, SatPointer{}, SatPointer{}, Sense::Forward);

    const auto base = static_cast<std::int32_t>(sat.records.size());
    auto co = [=](std::int32_t i) { return base + i; };
    const auto loop_base = base + 3;
    const auto face_base = base + 6;
    const auto shell_idx = base + 8;
    const auto lump_idx = base + 9;

    // co0: base circle on the base-cap face
    sat.add_coedge(ptr(co(0)), ptr(co(0)), ptr(co(1)), ptr(e_base), Sense::Reversed, ptr(loop_base));
    // co1: base circle boundary on the cone face
    sat.add_coedge(ptr(co(1)), ptr(co(1)), ptr(co(0)), ptr(e_base), Sense::Forward, ptr(loop_base + 1));
    // co2: singularity (apex) on the cone face — partner=$-1 (no partner for degenerate edge)
    sat.add_coedge(ptr(co(2)), ptr(co(2)), SatPointer{}, ptr(e_apex), Sense::Reversed, ptr(loop_base + 2));

    // Base cap loop
    sat.add_loop(SatPointer{}, ptr(co(0)), ptr(face_base));
    // Cone face: two loops — base circle boundary + apex singularity
    sat.add_loop(ptr(loop_base + 2), ptr(co(1)), ptr(face_base + 1)); // next_loop = singularity loop
    sat.add_loop(SatPointer{}, ptr(co(2)), ptr(face_base + 1));       // singularity loop on same cone face

    sat.add_face(ptr(face_base + 1), ptr(loop_base), ptr(shell_idx), ptr(surf_base), Sense::Forward,
                 Sidedness::Single);
    sat.add_face(SatPointer{}, ptr(loop_base + 1), ptr(shell_idx), ptr(surf_cone), Sense::Forward,
                 Sidedness::Single);

    sat.add_shell(ptr(face_base), ptr(lump_idx));
    sat.add_lump(ptr(shell_idx), body_idx);

    if (auto *body_rec = sat.record(0)) {
        body_rec->tokens[1] = SatToken(ptr(lump_idx));
    }
    sat.complete_brep_links();
    return sat;
}

// Build a sphere centered at `center` with the given `radius`.
//
// Produces a single-face closed surface (no loop entity).
SatDocument build_sphere(Vector3 center, double radius) {
    auto sat = SatDocument::new_body();
    const auto body_idx = SatPointer{0};

    const auto surf = sat.add_sphere_surface(center, radius, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0});

    const auto base = static_cast<std::int32_t>(sat.records.size());
    const auto face_idx = base;
    const auto shell_idx = base + 1;
    const auto lump_idx = base + 2;

    sat.add_face(SatPointer{}, SatPointer{}, ptr(shell_idx), ptr(surf), Sense::Forward, Sidedness::Single);
    sat.add_shell(ptr(face_idx), ptr(lump_idx));
    sat.add_lump(ptr(shell_idx), body_idx);

    if (auto *body_rec = sat.record(0)) {
        body_rec->tokens[1] = SatToken(ptr(lump_idx));
    }
    sat.complete_brep_links();
    return sat;
}

// Build a torus centered at `center` with the given radii.
//
// - `major_radius`: distance from the center to the tube center
// - `minor_radius`: radius of the tube
//
// The torus axis is along Z. Produces a single-face closed surface (no loop
// entity).
SatDocument build_torus(Vector3 center, double major_radius, double minor_radius) {
    auto sat = SatDocument::new_body();
    const auto body_idx = SatPointer{0};

    const auto surf =
        sat.add_torus_surface(center, {0.0, 0.0, 1.0}, major_radius, minor_radius, {1.0, 0.0, 0.0});

    const auto base = static_cast<std::int32_t>(sat.records.size());
    const auto face_idx = base;
    const auto shell_idx = base + 1;
    const auto lump_idx = base + 2;

    sat.add_face(SatPointer{}, SatPointer{}, ptr(shell_idx), ptr(surf), Sense::Forward, Sidedness::Single);
    sat.add_shell(ptr(face_idx), ptr(lump_idx));
    sat.add_lump(ptr(shell_idx), body_idx);

    if (auto *body_rec = sat.record(0)) {
        body_rec->tokens[1] = SatToken(ptr(lump_idx));
    }
    sat.complete_brep_links();
    return sat;
}

} // namespace libopencad::acis
