// SPDX-License-Identifier: MPL-2.0
// acadrust 0c3ae80d entities/modification/explode and io/dxf/writer/r12.rs.
#pragma once
#include <libopencad/dxf_document.hpp>
namespace libopencad::detail {
std::vector<Point3> approximate_spline(const SplineDefinition &spline, std::size_t limit);
std::vector<Point3> approximate_ellipse(const DxfEllipse &ellipse);
struct ExplodedText {
    TextDefinition text;
    std::optional<Color> color;
};
// Matches upstream approximate font metrics, wrapping and column distribution.
std::vector<ExplodedText> approximate_mtext(const MTextDefinition &mtext, std::size_t limit);
} // namespace libopencad::detail
namespace libopencad::detail {
struct HatchPrimitive {
    enum class Kind { Line, Point, Solid } kind;
    std::array<Point3, 4> points{};
};
struct HatchApproximation {
    std::vector<HatchPrimitive> primitives;
    bool boundary_only = false;
};
HatchApproximation approximate_hatch(const HatchDefinition &hatch, std::size_t limit);
} // namespace libopencad::detail
