// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <libopencad/transform.hpp>
#include <limits>
using namespace libopencad;
inline Transform operation(int step) {
    switch (step) {
    case 1:
        return Transform::from_translation({3, -4, 5});
    case 2:
        return Transform::from_rotation_with_origin({1, 2, 3}, 0.7, {4, -3, 2});
    case 3:
        return Transform::from_scale(2);
    case 4:
        return Transform::from_scaling({2, 0.5, 1.5});
    case 5:
        return Transform::from_mirror_x();
    case 6:
        return Transform::from_mirror_plane({2, -1, 3}, {1, 2, -1});
    case 7:
        return Transform::from_mirror_line({1, 1, 5}, {1, 1, -3});
    case 8:
        return Transform::from_rotation({0, 1, 0}, 0.7)
            .then(Transform::from_scaling({-2, 1, 3}))
            .then(Transform::from_translation({3, -4, 5}));
    case 9:
        return Transform::from_scale(0);
    case 11: {
        auto t = Transform::identity();
        t.matrix.m[0][0] = std::numeric_limits<double>::infinity();
        return t;
    }
    case 12:
        return Transform::from_scaling({1, 0, 1});
    case 13: {
        auto m = Matrix4::identity();
        m.m[0][1] = .75;
        m.m[2][0] = -.2;
        return Transform::from_matrix(m);
    }
    default:
        return Transform::identity();
    }
}
