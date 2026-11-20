// SPDX-License-Identifier: MPL-2.0
#include "dxf_approximation.hpp"
#include "dxf_code_page.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <libopencad/mtext_format.hpp>
#include <libopencad/transform.hpp>
#include <stdexcept>
namespace libopencad::detail {
namespace {
void finite(Point3 p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        throw std::invalid_argument("Nonfinite approximation geometry");
}
} // namespace
std::vector<Point3> approximate_spline(const SplineDefinition &s, std::size_t limit) {
    std::vector<Point3> points;
    const auto n = s.control_points.size();
    auto append = [&](Point3 p) {
        finite(p);
        if (points.size() >= limit)
            throw std::length_error("Spline approximation exceeds output budget");
        points.push_back(p);
    };
    if (n < 2) {
        for (auto p : s.fit_points)
            append(p);
    } else {
        const auto degree = s.degree < 0 ? 3u : static_cast<unsigned>(s.degree);
        const auto p = std::clamp<std::size_t>(degree, 1, n - 1);
        // Bound both result allocation and quadratic de Boor work for hostile inputs.
        if (n > limit || p > 256)
            throw std::length_error("Spline approximation complexity limit exceeded");
        auto knots = s.knots;
        bool valid = knots.size() == n + p + 1;
        for (std::size_t i = 0; i < knots.size(); ++i)
            valid &= std::isfinite(knots[i]) && (!i || knots[i] >= knots[i - 1]);
        if (!valid) {
            knots.resize(n + p + 1);
            for (std::size_t i = 0; i < knots.size(); ++i)
                knots[i] = i <= p ? 0 : i >= n ? 1 : double(i - p) / double(n - p);
        }
        bool rational =
            s.weights.size() == n &&
            std::any_of(s.weights.begin(), s.weights.end(), [](double w) { return std::abs(w - 1) > 1e-12; });
        std::vector<std::array<double, 4>> hom;
        for (std::size_t i = 0; i < n; ++i) {
            finite(s.control_points[i]);
            if (rational && !std::isfinite(s.weights[i]))
                throw std::invalid_argument("Nonfinite spline weight");
            auto w = rational && s.weights[i] > 0 ? s.weights[i] : 1.;
            const auto c = s.control_points[i];
            hom.push_back({c.x * w, c.y * w, c.z * w, w});
        }
        std::size_t work = 0;
        for (std::size_t k = p; k < n; ++k) {
            const auto u0 = knots[k], u1 = knots[k + 1];
            if (u1 - u0 <= 1e-15)
                continue;
            for (unsigned step = points.empty() ? 0 : 1; step <= 16; ++step) {
                work += p * (p + 1) * 2;
                if (work > 20000000)
                    throw std::length_error("Spline evaluation work budget exceeded");
                const auto u = u0 + (u1 - u0) * step / 16.;
                std::vector<std::array<double, 4>> d(hom.begin() + k - p, hom.begin() + k + 1);
                for (std::size_t r = 1; r <= p; ++r)
                    for (std::size_t j = p; j >= r; --j) {
                        const auto i = j + k - p;
                        const auto denom = knots[i + p + 1 - r] - knots[i];
                        const auto alpha = std::abs(denom) < 1e-15 ? 0 : (u - knots[i]) / denom;
                        for (unsigned c = 0; c < 4; ++c)
                            d[j][c] = (1 - alpha) * d[j - 1][c] + alpha * d[j][c];
                    }
                auto w = std::abs(d[p][3]) < 1e-15 ? 1 : d[p][3];
                append({d[p][0] / w, d[p][1] / w, d[p][2] / w});
            }
        }
        if (points.empty())
            for (auto c : s.control_points)
                append(c);
    }
    if ((s.flags & 1) && points.size() > 2 && (n < 2 || points.front() != points.back()))
        append(points.front());
    return points;
}
std::vector<Point3> approximate_ellipse(const DxfEllipse &e) {
    const auto center = e.center(), major = e.major_axis(), normal = e.normal();
    finite(center);
    finite(major);
    finite(normal);
    const auto minor = normal.normalize().cross(major) * e.minor_axis_ratio();
    const auto t0 = e.start_parameter();
    auto t1 = e.end_parameter();
    if (!std::isfinite(t0) || !std::isfinite(t1) || !std::isfinite(e.minor_axis_ratio()))
        throw std::invalid_argument("Nonfinite ellipse parameter");
    constexpr double tau = 6.283185307179586476925286766559;
    // Avoid the upstream unbounded addition loop for large negative sweeps.
    if (!std::isfinite(t1 - t0))
        throw std::invalid_argument("Ellipse parameter sweep overflows");
    if (t1 <= t0)
        t1 = t0 + tau - std::fmod(t0 - t1, tau);
    if (t1 <= t0 || !std::isfinite(t1))
        throw std::invalid_argument("Ellipse parameter resolution exhausted");
    const auto steps = static_cast<unsigned>(std::clamp(std::ceil((t1 - t0) / tau * 96), 8., 512.));
    std::vector<Point3> result;
    for (unsigned i = 0; i <= steps; ++i) {
        auto t = t0 + (t1 - t0) * i / steps;
        auto p = center + major * std::cos(t) + minor * std::sin(t);
        finite(p);
        result.push_back(p);
    }
    return result;
}
std::vector<ExplodedText> approximate_mtext(const MTextDefinition &m, std::size_t limit) {
    const auto formatted = mtext::parse(m.text, false);
    const auto h = m.height > 0 ? m.height : 1.;
    const auto spacing = h * 5. / 3. * (m.line_spacing_factor > 0 ? m.line_spacing_factor : 1.);
    auto normal = m.normal.length_squared() > 1e-20 ? m.normal.normalize() : Vector3::UNIT_Z;
    auto raw = m.x_direction.value_or(Point3{std::cos(m.rotation), std::sin(m.rotation), 0});
    auto projected = raw - normal * raw.dot(normal);
    auto x = projected.length_squared() > 1e-20
                 ? projected.normalize()
                 : (Matrix3::arbitrary_axis(normal) * Vector3::UNIT_X).normalize();
    auto y = normal.cross(x).normalize();
    auto width = std::max(m.rectangle_width + m.columns.gutter, 0.);
    auto layout = [&](std::size_t count, std::size_t i) {
        auto cols = m.columns.type ? static_cast<std::size_t>(std::max(m.columns.count, 1)) : 1;
        auto per = std::max<std::size_t>((count + cols - 1) / cols, 1);
        auto col = std::min(i / per, cols - 1);
        if (m.columns.flow_reversed && cols > 1)
            col = cols - 1 - col;
        return std::pair<std::size_t, std::size_t>{col, i % per};
    };
    std::vector<ExplodedText> out;
    auto add = [&](ExplodedText t) {
        finite(t.text.insertion);
        if (out.size() >= limit)
            throw std::length_error("MTEXT decomposition exceeds output budget");
        out.push_back(std::move(t));
    };
    bool rich = std::any_of(formatted.paragraphs.begin(), formatted.paragraphs.end(), [](const auto &p) {
        return p.spans.size() > 1 ||
               std::any_of(p.spans.begin(), p.spans.end(), [](const auto &s) { return !s.plain(); });
    });
    if (rich) {
        for (std::size_t i = 0; i < formatted.paragraphs.size(); ++i) {
            auto [col, row] = layout(formatted.paragraphs.size(), i);
            double advance = 0;
            for (const auto &span : formatted.paragraphs[i].spans) {
                auto value = span.stacking ? span.stacking->plain_text() : span.text;
                if (value.empty())
                    continue;
                ExplodedText t;
                auto &v = t.text;
                v.text = value;
                v.style = m.style;
                v.normal = m.normal;
                auto &p = span.properties;
                v.height =
                    p.height ? std::max(p.height->relative ? h * p.height->value : p.height->value, 1e-9) : h;
                v.width_factor = std::max(p.width_factor.value_or(1), 1e-9);
                v.rotation = m.rotation + p.oblique_angle.value_or(0) * std::acos(-1.) / 180.;
                v.insertion =
                    m.insertion + x * (width * col) + y * (-double(row) * spacing - h) + x * advance;
                if (p.font && !p.font->name.empty())
                    v.style = p.font->name;
                t.color = p.color;
                if (t.color && t.color->kind() == Color::Kind::None)
                    t.color.reset();
                add(t);
                advance += utf8_scalars(span.text).size() * v.height * .6 * v.width_factor;
            }
        }
    } else {
        auto plain = mtext::parse(m.text, true).plain_text();
        std::vector<std::string> lines;
        std::size_t pos = 0;
        do {
            auto end = plain.find('\n', pos);
            auto paragraph = plain.substr(pos, end == std::string::npos ? end : end - pos);
            if (m.rectangle_width <= 0 || h * .6 <= 0)
                lines.push_back(paragraph);
            else {
                const auto maximum = std::max(std::floor(m.rectangle_width / (h * .6)), 1.);
                std::string line;
                std::size_t word_start = 0;
                do {
                    auto word_end = paragraph.find(' ', word_start);
                    auto word = paragraph.substr(
                        word_start, word_end == std::string::npos ? word_end : word_end - word_start);
                    auto candidate = line.empty() ? word : line + " " + word;
                    if (utf8_scalars(candidate).size() > maximum && !line.empty()) {
                        lines.push_back(line);
                        line = word;
                    } else
                        line = candidate;
                    if (word_end == std::string::npos)
                        break;
                    word_start = word_end + 1;
                } while (true);
                lines.push_back(line);
            }
            if (lines.size() > limit)
                throw std::length_error("MTEXT line budget exceeded");
            if (end == std::string::npos)
                break;
            pos = end + 1;
        } while (true);
        const auto total = h + spacing * (lines.empty() ? 0 : lines.size() - 1);
        const int column = ((m.attachment - 1) % 3 + 3) % 3, row = std::clamp((m.attachment - 1) / 3, 0, 2);
        const auto first = -h + (row == 0 ? 0 : row == 1 ? total / 2 : total);
        auto rotation = m.x_direction && std::abs(m.x_direction->x) + std::abs(m.x_direction->y) > 1e-12
                            ? std::atan2(m.x_direction->y, m.x_direction->x)
                            : m.rotation;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto scalars = utf8_scalars(lines[i]);
            const bool blank = std::all_of(scalars.begin(), scalars.end(), [](char32_t c) {
                return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
                       (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f ||
                       c == 0x205f || c == 0x3000;
            });
            if (blank)
                continue;
            auto [col, r] = layout(lines.size(), i);
            ExplodedText t;
            auto &v = t.text;
            v.text = lines[i];
            v.style = m.style;
            v.height = h;
            v.rotation = rotation;
            v.normal = m.normal;
            v.horizontal_alignment = column;
            v.insertion = m.insertion + x * (width * col) + y * (first - spacing * r);
            if (column)
                v.alignment_point = v.insertion;
            add(std::move(t));
        }
    }
    return out;
}
} // namespace libopencad::detail
