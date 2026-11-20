// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/classes/civil COGO points, number-list and query point
// groups, siteless alignments of fixed lines, free arcs and SCS, layout
// profiles, and TIN surfaces.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <libopencad/arch.hpp>
#include <libopencad/vector.hpp>

namespace libopencad {

struct CogoPoint {
    std::uint32_t number = 0;
    Vector3 position{};
    std::uint64_t style = 0;
    std::uint64_t label_style = 0;
    std::optional<std::string> description;
};

enum class Comparison { Equal, Greater, Less, GreaterOrEqual, LessOrEqual };

struct PointQuery {
    enum class Kind { Numbers, Elevation, RawDescription };
    Kind kind = Kind::Numbers;
    std::vector<std::uint32_t> numbers;
    Comparison comparison = Comparison::Equal;
    double elevation = 0;
    std::vector<std::string> patterns;
    static PointQuery from_numbers(std::vector<std::uint32_t> list) {
        PointQuery q;
        q.kind = Kind::Numbers;
        q.numbers = std::move(list);
        return q;
    }
    static PointQuery from_elevation(Comparison comparison, double value) {
        PointQuery q;
        q.kind = Kind::Elevation;
        q.comparison = comparison;
        q.elevation = value;
        return q;
    }
    static PointQuery from_raw_description(std::vector<std::string> list) {
        PointQuery q;
        q.kind = Kind::RawDescription;
        q.patterns = std::move(list);
        return q;
    }
};

struct TinVertex {
    Vector3 position{};
    std::int32_t flags = 0;
    std::int32_t triangle = 0;
};

struct TinTriangle {
    std::array<std::uint32_t, 3> vertices{};
    std::array<std::int32_t, 3> neighbors{};
    std::int32_t flags = 0;
    bool is_visible() const { return (flags & 1) == 0; }
};

struct TinSurface {
    std::vector<TinVertex> vertices;
    std::vector<TinTriangle> triangles;
    std::array<std::uint32_t, 4> corners{};
    std::vector<std::array<Vector3, 3>> visible_triangles() const {
        std::vector<std::array<Vector3, 3>> out;
        for (auto &tri : triangles)
            if (tri.is_visible())
                out.push_back({vertices[tri.vertices[0]].position, vertices[tri.vertices[1]].position,
                               vertices[tri.vertices[2]].position});
        return out;
    }
};

struct VertexCurve {
    enum class Kind { None, Arc, SpiralCurveSpiral };
    Kind kind = Kind::None;
    double radius = 0, spiral_in = 0, spiral_out = 0;
    static VertexCurve none() { return {}; }
    static VertexCurve arc(double r) {
        VertexCurve c;
        c.kind = Kind::Arc;
        c.radius = r;
        return c;
    }
    static VertexCurve spiral_curve_spiral(double spiral_in, double radius, double spiral_out) {
        VertexCurve c;
        c.kind = Kind::SpiralCurveSpiral;
        c.spiral_in = spiral_in;
        c.radius = radius;
        c.spiral_out = spiral_out;
        return c;
    }
};

enum class AlignmentPiece { Line, Curve, Spiral };

struct AlignmentElementKind {
    enum class Tag { FixedLine, FreeArc, FreeSpiralCurveSpiral };
    Tag tag = Tag::FixedLine;
    Vector2 start{}, end{};
    double radius = 0, spiral_in = 0, spiral_out = 0;
    static AlignmentElementKind fixed_line(Vector2 start, Vector2 end) {
        AlignmentElementKind k;
        k.start = start;
        k.end = end;
        return k;
    }
    static AlignmentElementKind free_arc(double radius) {
        AlignmentElementKind k;
        k.tag = Tag::FreeArc;
        k.radius = radius;
        return k;
    }
    static AlignmentElementKind free_scs(double spiral_in, double radius, double spiral_out) {
        AlignmentElementKind k;
        k.tag = Tag::FreeSpiralCurveSpiral;
        k.spiral_in = spiral_in;
        k.radius = radius;
        k.spiral_out = spiral_out;
        return k;
    }
    bool operator==(const AlignmentElementKind &o) const {
        if (tag != o.tag)
            return false;
        if (tag == Tag::FixedLine)
            return start == o.start && end == o.end;
        if (tag == Tag::FreeArc)
            return radius == o.radius;
        return spiral_in == o.spiral_in && radius == o.radius && spiral_out == o.spiral_out;
    }
};

struct AlignmentElement {
    std::int16_t id = 0, previous = 0, next = 0;
    AlignmentElementKind kind{};
};

std::vector<CogoPoint> cogo_points(const ArchObject &database);
std::vector<std::uint32_t> point_group_numbers(const ArchObject &group);
std::optional<std::uint32_t> cogo_point_number(const ArchEntityData &entity);
std::optional<std::string> alignment_name(const ArchEntityData &entity);
std::optional<double> alignment_start_station(const ArchEntityData &entity);
std::vector<std::vector<Vector2>> alignment_polylines(const ArchEntityData &entity);
std::optional<std::vector<AlignmentElement>> alignment_elements(const ArchEntityData &entity);
std::vector<std::pair<AlignmentPiece, std::vector<Vector2>>> alignment_pieces(const ArchEntityData &entity);

struct VerticalCurve {
    enum class Kind { None, Parabola, Asymmetric, Circular };
    Kind kind = Kind::None;
    double length = 0, before = 0, after = 0, radius = 0;
    static VerticalCurve none() { return {}; }
    static VerticalCurve parabola(double length) {
        VerticalCurve c;
        c.kind = Kind::Parabola;
        c.length = length;
        return c;
    }
    static VerticalCurve asymmetric(double before, double after) {
        VerticalCurve c;
        c.kind = Kind::Asymmetric;
        c.before = before;
        c.after = after;
        return c;
    }
    static VerticalCurve circular(double radius) {
        VerticalCurve c;
        c.kind = Kind::Circular;
        c.radius = radius;
        return c;
    }
};

enum class ProfilePiece { Tangent, Parabola, AsymmetricParabola, Circular };

std::optional<std::string> profile_name(const ArchEntityData &entity);
std::vector<std::pair<ProfilePiece, std::vector<Vector2>>> profile_pieces(const ArchEntityData &entity);
std::optional<TinSurface> tin_surface(const ArchEntityData &entity);

} // namespace libopencad
