// SPDX-License-Identifier: MPL-2.0
// acadrust 0c3ae80d io/dxf/writer/r12.rs hatch_loops/hatch_entities.
#include "dxf_approximation.hpp"
#include <algorithm>
#include <cmath>
#include <libopencad/transform.hpp>
#include <limits>
#include <stdexcept>
namespace libopencad::detail {
namespace {
constexpr double tau = 6.283185307179586476925286766559;
double sweep(double start, double end) {
    if (!std::isfinite(start) || !std::isfinite(end))
        throw std::invalid_argument("Nonfinite hatch parameter");
    const auto delta = end - start;
    if (!std::isfinite(delta))
        throw std::invalid_argument("Hatch parameter sweep overflows");
    return delta <= 0 ? tau - std::fmod(-delta, tau) : delta;
}
} // namespace
HatchApproximation approximate_hatch(const HatchDefinition &h, std::size_t limit) {
    std::vector<std::pair<Vector2, Vector2>> edges;
    std::size_t samples = 0;
    for (const auto &path : h.paths) {
        std::vector<Vector2> pts;
        auto push = [&](Vector2 p) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y))
                throw std::invalid_argument("Nonfinite hatch sample");
            if (pts.empty() || std::abs(pts.back().x - p.x) > 1e-12 || std::abs(pts.back().y - p.y) > 1e-12) {
                if (++samples > limit)
                    throw std::length_error("Hatch sampling budget exceeded");
                pts.push_back(p);
            }
        };
        for (const auto &edge : path.edges)
            std::visit(
                [&](const auto &e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, HatchLineEdge>) {
                        push(e.start);
                        push(e.end);
                    } else if constexpr (std::is_same_v<T, HatchArcEdge> ||
                                         std::is_same_v<T, HatchEllipseEdge>) {
                        auto delta = sweep(e.start_angle, e.end_angle);
                        auto steps = static_cast<unsigned>(std::clamp(std::ceil(delta / tau * 96), 2., 512.));
                        for (unsigned i = 0; i <= steps; ++i) {
                            auto t = (e.start_angle + delta * i / steps) * (e.counter_clockwise ? 1 : -1);
                            if constexpr (std::is_same_v<T, HatchArcEdge>)
                                push(e.center + Vector2{std::cos(t), std::sin(t)} * e.radius);
                            else {
                                auto minor = Vector2{-e.major_axis_endpoint.y, e.major_axis_endpoint.x} *
                                             e.minor_axis_ratio;
                                push(e.center + e.major_axis_endpoint * std::cos(t) + minor * std::sin(t));
                            }
                        }
                    } else if constexpr (std::is_same_v<T, HatchSplineEdge>) {
                        if (e.degree > 32767)
                            throw std::length_error("Hatch spline degree exceeds budget");
                        SplineDefinition spline;
                        spline.degree = static_cast<std::int16_t>(e.degree);
                        spline.knots = e.knots;
                        for (auto c : e.control_points) {
                            spline.control_points.push_back({c.x, c.y, 0});
                            if (e.rational)
                                spline.weights.push_back(c.z);
                        }
                        for (auto f : e.fit_points)
                            spline.fit_points.push_back({f.x, f.y, 0});
                        for (auto p : approximate_spline(spline, limit))
                            push({p.x, p.y});
                    } else {
                        const auto n = e.vertices.size(), count = e.closed ? n : n ? n - 1 : 0;
                        for (std::size_t i = 0; i < n; ++i) {
                            auto a = e.vertices[i];
                            push({a.x, a.y});
                            if (i >= count || std::abs(a.z) <= 1e-12)
                                continue;
                            auto b = e.vertices[(i + 1) % n];
                            const auto theta = 4 * std::atan(a.z), dx = b.x - a.x, dy = b.y - a.y,
                                       chord = std::sqrt(dx * dx + dy * dy);
                            if (chord < 1e-12)
                                continue;
                            auto r = chord / (2 * std::sin(theta / 2)), height = r * std::cos(theta / 2);
                            Vector2 center{(a.x + b.x) / 2 - height * dy / chord,
                                           (a.y + b.y) / 2 + height * dx / chord};
                            auto start = std::atan2(a.y - center.y, a.x - center.x);
                            auto steps = static_cast<unsigned>(
                                std::clamp(std::ceil(std::abs(theta) / tau * 96), 2., 256.));
                            for (unsigned j = 1; j < steps; ++j) {
                                auto t = start + theta * j / steps;
                                push(center + Vector2{std::cos(t), std::sin(t)} * std::abs(r));
                            }
                        }
                    }
                },
                edge);
        if (pts.size() > 1 && pts.front() == pts.back())
            pts.pop_back();
        if (pts.size() >= 3)
            for (std::size_t i = 0; i < pts.size(); ++i)
                edges.emplace_back(pts[i], pts[(i + 1) % pts.size()]);
    }
    if (edges.empty())
        throw std::invalid_argument("Hatch has no polygonal boundary");
    HatchApproximation result;
    auto &out = result.primitives;
    auto append = [&](HatchPrimitive p) {
        if (out.size() >= limit)
            throw std::length_error("Hatch output budget exceeded");
        for (auto v : p.points)
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                throw std::invalid_argument("Nonfinite hatch output");
        out.push_back(p);
    };
    std::size_t work = 0;
    auto tick = [&]() {
        if (++work > 20000000)
            throw std::length_error("Hatch intersection work budget exceeded");
    };
    std::string name = h.pattern_name;
    for (auto &c : name)
        if (c >= 'a' && c <= 'z')
            c -= 32;
    if (h.solid || (h.pattern_lines.empty() && name == "SOLID")) {
        std::vector<double> ys;
        for (auto e : edges)
            ys.push_back(e.first.y);
        std::sort(ys.begin(), ys.end());
        ys.erase(std::unique(ys.begin(), ys.end(), [](auto a, auto b) { return std::abs(a - b) < 1e-12; }),
                 ys.end());
        for (std::size_t i = 1; i < ys.size(); ++i) {
            auto y0 = ys[i - 1], y1 = ys[i], ym = (y0 + y1) / 2;
            std::vector<std::array<double, 3>> xs;
            for (const auto &edge : edges) {
                const auto a = edge.first, b = edge.second;
                tick();
                if ((a.y <= ym) != (b.y <= ym)) {
                    auto at = [&](double y) { return a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y); };
                    const std::array<double, 3> intersection{at(ym), at(y0), at(y1)};
                    for (auto v : intersection)
                        if (!std::isfinite(v))
                            throw std::invalid_argument("Hatch strip intersection overflows");
                    xs.push_back(intersection);
                }
            }
            std::sort(xs.begin(), xs.end(), [](auto a, auto b) { return a[0] < b[0]; });
            for (std::size_t j = 1; j < xs.size(); j += 2) {
                auto l = xs[j - 1], r = xs[j];
                append({HatchPrimitive::Kind::Solid,
                        {{{l[1], y0, h.elevation},
                          {r[1], y0, h.elevation},
                          {l[2], y1, h.elevation},
                          {r[2], y1, h.elevation}}}});
            }
        }
    } else {
        auto normal = h.normal.normalize();
        auto basis = Matrix3::arbitrary_axis(normal);
        auto wcs = [&](Vector2 p) {
            return (normal - Vector3::UNIT_Z).length() < 1e-12 ? Point3{p.x, p.y, h.elevation}
                                                               : basis * Point3{p.x, p.y, h.elevation};
        };
        auto line = [&](Vector2 a, Vector2 b) {
            append({HatchPrimitive::Kind::Line, {{wcs(a), wcs(b), {}, {}}}});
        };
        bool dense = false;
        for (const auto &family : h.pattern_lines) {
            const Vector2 dir{std::cos(family.angle), std::sin(family.angle)};
            auto spacing = dir.cross(family.offset);
            if (std::abs(spacing) < 1e-9)
                continue;
            double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
            for (auto e : edges) {
                auto p = (e.first - family.base_point).cross(dir) * -1;
                lo = std::min(lo, p);
                hi = std::max(hi, p);
            }
            auto k0 = std::floor(std::min(lo / spacing, hi / spacing)),
                 k1 = std::ceil(std::max(lo / spacing, hi / spacing));
            if (!std::isfinite(k0) || !std::isfinite(k1))
                throw std::invalid_argument("Nonfinite hatch line index");
            if (k1 - k0 > 200000) {
                dense = true;
                break;
            }
            // Integers above this precision cannot be enumerated by adding one.
            if (std::abs(k0) > 9e15 || std::abs(k1) > 9e15)
                throw std::length_error("Hatch index precision limit exceeded");
            double period = 0;
            for (auto dash : family.dash_lengths) {
                if (!std::isfinite(dash))
                    throw std::invalid_argument("Nonfinite hatch dash");
                period += std::abs(dash);
            }
            for (auto k = k0; k <= k1 && !dense; ++k) {
                auto q = family.base_point + family.offset * k;
                std::vector<double> ts;
                for (const auto &edge : edges) {
                    const auto a = edge.first, b = edge.second;
                    tick();
                    auto e = b - a;
                    auto den = dir.cross(e);
                    if (std::abs(den) < 1e-15)
                        continue;
                    auto w = a - q;
                    auto s = w.cross(dir) / den;
                    if (s < 0 || s >= 1)
                        continue;
                    const auto t = w.cross(e) / den;
                    if (!std::isfinite(s) || !std::isfinite(t))
                        throw std::invalid_argument("Hatch line intersection overflows");
                    ts.push_back(t);
                }
                std::sort(ts.begin(), ts.end());
                auto at = [&](double t) { return q + dir * t; };
                for (std::size_t i = 1; i < ts.size() && !dense; i += 2) {
                    auto t0 = ts[i - 1], t1 = ts[i];
                    if (family.dash_lengths.empty() || period <= 1e-12)
                        line(at(t0), at(t1));
                    else {
                        double t = std::floor(t0 / period) * period;
                        bool stop = false;
                        while (!stop && !dense) {
                            const auto before = t;
                            for (auto dash : family.dash_lengths) {
                                tick();
                                auto end = t + std::abs(dash);
                                if (t >= t1) {
                                    stop = true;
                                    break;
                                }
                                if (dash >= 0 && end > t0) {
                                    if (dash == 0)
                                        append({HatchPrimitive::Kind::Point, {{wcs(at(t)), {}, {}, {}}}});
                                    else
                                        line(at(std::max(t, t0)), at(std::min(end, t1)));
                                }
                                t = end;
                            }
                            if (!stop && t <= before)
                                throw std::invalid_argument("Hatch dash makes no numerical progress");
                            dense = out.size() > 200000;
                        }
                    }
                    dense = out.size() > 200000;
                }
            }
            if (dense)
                break;
        }
        if (dense || out.empty()) {
            result.boundary_only = true;
            out.clear();
            for (auto [a, b] : edges)
                line(a, b);
        }
    }
    if (out.empty())
        throw std::invalid_argument("Hatch produced no geometry");
    return result;
}
} // namespace libopencad::detail
