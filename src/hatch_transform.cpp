// SPDX-License-Identifier: MPL-2.0
// Port of acadrust modification/{translate,transform,mirror/hatch}.rs and hatch_origin.rs.
#include <libopencad/dxf_document.hpp>
#include <cmath>
#include <stdexcept>
namespace libopencad {
namespace {
constexpr double pi = 3.1415926535897932384626433832795, tau = 2 * pi;
double wrap(double a) {
    auto r = std::fmod(a, tau);
    return r < 0 ? r + tau : r;
}
bool finite(const Transform &t) {
    for (const auto &row : t.matrix.m)
        for (auto n : row)
            if (!std::isfinite(n))
                return false;
    return true;
}
void validate(const Transform &t) {
    if (!finite(t))
        throw std::invalid_argument("Nonfinite hatch transform");
}
std::optional<std::size_t> origin_index(const ExtendedDataRecord &r) {
    std::size_t depth = 0;
    for (std::size_t i = 0; i < r.values.size(); ++i) {
        const auto &v = r.values[i];
        if (v.code == 1002) {
            auto &s = std::get<std::string>(v.value);
            if (s == "{")
                ++depth;
            else if (s == "}" && depth)
                --depth;
        } else if (v.code == 1010 && depth == 0)
            return i;
    }
    return {};
}
Vector2 transform_hatch(HatchDefinition &d, const Transform &t, Vector2 origin) {
    auto old = d.normal;
    double elevation = d.elevation;
    auto normal = t.apply_rotation(old).normalize();
    auto ocs = Matrix3::arbitrary_axis(old), inv = Matrix3::arbitrary_axis(normal).transpose();
    auto point = [&](Vector2 p) {
        auto v = inv * t.apply(ocs * Vector3{p.x, p.y, elevation});
        return Vector2{v.x, v.y};
    };
    auto direction = [&](Vector2 p) {
        auto v = inv * t.apply_rotation(ocs * Vector3{p.x, p.y, 0});
        return Vector2{v.x, v.y};
    };
    auto angle = [&](double a) {
        auto v = direction({std::cos(a), std::sin(a)});
        return std::atan2(v.y, v.x);
    };
    auto x = t.apply_rotation(ocs * Vector3::UNIT_X), y = t.apply_rotation(ocs * Vector3::UNIT_Y);
    double sx = x.length(), sy = y.length();
    auto nx = inv * x, ny = inv * y;
    bool uniform = std::abs(sx - sy) < 1e-10 && std::abs(x.dot(y)) < 1e-10,
         flip = nx.x * ny.y - nx.y * ny.x < 0;
    for (auto &path : d.paths)
        for (auto &edge : path.edges) {
            if (auto a = std::get_if<HatchArcEdge>(&edge)) {
                auto sweep = a->end_angle - a->start_angle;
                auto true_start = a->counter_clockwise ? a->start_angle : -a->start_angle;
                bool ccw = a->counter_clockwise != flip;
                auto center = point(a->center);
                if (uniform) {
                    auto start = angle(true_start);
                    if (start < 0)
                        start += tau;
                    start = wrap(ccw ? start : -start);
                    a->center = center;
                    a->radius *= sx;
                    a->counter_clockwise = ccw;
                    a->start_angle = start;
                    a->end_angle = start + sweep;
                } else {
                    auto major3 = inv * (x * a->radius);
                    Vector2 major{major3.x, major3.y};
                    auto ratio = sy / sx, start = wrap(ccw ? true_start : -true_start);
                    if (ratio > 1) {
                        auto len = major.length() * ratio;
                        major = Vector2{-major.y, major.x}.normalize() * len;
                        ratio = 1 / ratio;
                        start = wrap(start + (ccw ? -pi / 2 : pi / 2));
                    }
                    edge = HatchEllipseEdge{center, major, ratio, start, start + sweep, ccw};
                }
            } else if (auto a = std::get_if<HatchEllipseEdge>(&edge)) {
                auto minor = Vector2{-a->major_axis_endpoint.y, a->major_axis_endpoint.x}.normalize() *
                             (a->major_axis_endpoint.length() * a->minor_axis_ratio);
                auto major = direction(a->major_axis_endpoint);
                minor = direction(minor);
                auto ml = major.length(), nl = minor.length(), sweep = a->end_angle - a->start_angle;
                a->center = point(a->center);
                if (flip) {
                    a->counter_clockwise = !a->counter_clockwise;
                    a->start_angle = wrap(-a->start_angle);
                }
                if (nl > ml + 1e-12) {
                    a->major_axis_endpoint = minor;
                    a->minor_axis_ratio = ml / nl;
                    a->start_angle = wrap(a->start_angle + (a->counter_clockwise ? -pi / 2 : pi / 2));
                } else {
                    a->major_axis_endpoint = major;
                    a->minor_axis_ratio = ml > 1e-12 ? nl / ml : 1;
                }
                a->end_angle = a->start_angle + sweep;
            } else if (auto a = std::get_if<HatchLineEdge>(&edge)) {
                a->start = point(a->start);
                a->end = point(a->end);
            } else if (auto a = std::get_if<HatchSplineEdge>(&edge)) {
                for (auto &p : a->control_points) {
                    auto q = point({p.x, p.y});
                    p.x = q.x;
                    p.y = q.y;
                }
                for (auto &p : a->fit_points)
                    p = point(p);
                a->start_tangent = direction(a->start_tangent);
                a->end_tangent = direction(a->end_tangent);
            } else if (auto a = std::get_if<HatchPolylineEdge>(&edge)) {
                for (auto &p : a->vertices) {
                    auto q = point({p.x, p.y});
                    p.x = q.x;
                    p.y = q.y;
                    if (flip)
                        p.z = -p.z;
                }
            }
        }
    for (auto &p : d.seed_points)
        p = point(p);
    d.pattern_angle = angle(d.pattern_angle);
    d.pattern_scale *= sx;
    auto new_origin = point(origin);
    for (auto &line : d.pattern_lines) {
        line.angle = angle(line.angle);
        line.base_point = point(line.base_point);
        line.offset = direction(line.offset);
        for (auto &dash : line.dash_lengths)
            dash *= sx;
    }
    if (d.gradient.enabled)
        d.gradient.angle = angle(d.gradient.angle);
    d.normal = normal;
    d.elevation = (inv * t.apply(ocs * Vector3{0, 0, elevation})).z;
    return new_origin;
}
} // namespace
std::optional<Vector2> DxfHatch::stored_pattern_origin() const {
    for (auto &r : extended_data())
        if (r.application_name == "ACAD") {
            if (auto i = origin_index(r)) {
                auto p = std::get<Point3>(r.values[*i].value);
                if (std::isfinite(p.x) && std::isfinite(p.y))
                    return Vector2{p.x, p.y};
            }
            return {};
        }
    return {};
}
Vector2 DxfHatch::pattern_origin() const { return stored_pattern_origin().value_or(Vector2{}); }
bool DxfHatch::record_pattern_origin(Vector2 p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y))
        return false;
    ExtendedDataRecord r{"ACAD", {}};
    for (auto &v : extended_data())
        if (v.application_name == "ACAD") {
            r = v;
            break;
        }
    XDataValue v{1010, Point3{p.x, p.y, 0}};
    if (auto i = origin_index(r))
        r.values[*i] = v;
    else
        r.values.push_back(v);
    upsert_extended_data(r);
    return true;
}
bool DxfHatch::set_pattern_origin(Vector2 p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y))
        return false;
    auto delta = p - pattern_origin();
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y))
        return false;
    auto d = definition();
    for (auto &l : d.pattern_lines) {
        l.base_point = l.base_point + delta;
        if (!std::isfinite(l.base_point.x) || !std::isfinite(l.base_point.y))
            return false;
    }
    auto e = *this;
    e.set_definition(d);
    e.record_pattern_origin(p);
    *this = std::move(e);
    return true;
}
void DxfHatch::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    auto d = definition();
    auto v = Matrix3::arbitrary_axis(d.normal).transpose() * offset;
    Vector2 delta{v.x, v.y};
    if (!e.set_pattern_origin(pattern_origin() + delta))
        throw std::invalid_argument("Overflowing hatch origin");
    d = e.definition();
    d.elevation += v.z;
    for (auto &path : d.paths)
        for (auto &edge : path.edges)
            std::visit(
                [&](auto &a) {
                    using T = std::decay_t<decltype(a)>;
                    if constexpr (std::is_same_v<T, HatchLineEdge>) {
                        a.start = a.start + delta;
                        a.end = a.end + delta;
                    } else if constexpr (std::is_same_v<T, HatchArcEdge> ||
                                         std::is_same_v<T, HatchEllipseEdge>)
                        a.center = a.center + delta;
                    else if constexpr (std::is_same_v<T, HatchSplineEdge>) {
                        for (auto &p : a.control_points) {
                            p.x += v.x;
                            p.y += v.y;
                        }
                        for (auto &p : a.fit_points)
                            p = p + delta;
                    } else {
                        for (auto &p : a.vertices) {
                            p.x += v.x;
                            p.y += v.y;
                        }
                    }
                },
                edge);
    for (auto &p : d.seed_points)
        p = p + delta;
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfHatch::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto d = definition();
    auto origin = transform_hatch(d, t, pattern_origin());
    e.set_definition(d);
    if (!e.record_pattern_origin(origin))
        throw std::invalid_argument("Overflowing hatch origin");
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfHatch::apply_mirror(const Transform &t, const MirrorOptions &options) {
    if (!finite(t) || t.is_identity())
        return;
    auto e = *this;
    auto old = definition(), d = old;
    auto before = pattern_origin(), after = transform_hatch(d, t, before);
    bool reflecting = t.matrix.to_matrix3().determinant() < 0;
    bool preserve = reflecting && !options.mirror_hatch;
    if (reflecting)
        d.pattern_angle = old.pattern_angle;
    if (preserve) {
        d.pattern_lines = old.pattern_lines;
        d.pattern_scale = old.pattern_scale;
        d.gradient.angle = old.gradient.angle;
    }
    e.set_definition(d);
    if (!preserve && !(before.length() < 1e-12 && after.length() < 1e-12)) {
        if (!e.record_pattern_origin(after))
            throw std::invalid_argument("Overflowing hatch origin");
    }
    e.transform_extended_data(t);
    *this = std::move(e);
}
} // namespace libopencad
