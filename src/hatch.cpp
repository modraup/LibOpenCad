// SPDX-License-Identifier: MPL-2.0
// acadrust entities/hatch.rs: HatchPattern::update and Hatch pattern setters.
#include <cmath>
#include <libopencad/hatch.hpp>
#include <limits>

namespace libopencad {
namespace {
Vector2 rotate_point_2d(Vector2 point, Vector2 center, double angle) {
    const auto c = std::cos(angle), s = std::sin(angle);
    const auto d = point - center;
    return {center.x + d.x * c - d.y * s, center.y + d.x * s + d.y * c};
}
} // namespace
void HatchDefinition::update_pattern(Vector2 base_point, double angle, double scale) {
    for (auto &line : pattern_lines) {
        line.angle += angle;
        line.base_point =
            base_point + (rotate_point_2d(line.base_point, base_point, angle) - base_point) * scale;
        line.offset = rotate_point_2d(line.offset, Vector2::ZERO, angle) * scale;
        for (auto &dash : line.dash_lengths)
            dash *= scale;
    }
}
void HatchDefinition::set_pattern_angle(double angle) {
    update_pattern(Vector2::ZERO, angle - pattern_angle, 1.0);
    pattern_angle = angle;
}
void HatchDefinition::set_pattern_scale(double scale) {
    const auto ratio =
        std::abs(pattern_scale) > std::numeric_limits<double>::epsilon() ? scale / pattern_scale : scale;
    update_pattern(Vector2::ZERO, 0.0, ratio);
    pattern_scale = scale;
}
} // namespace libopencad
