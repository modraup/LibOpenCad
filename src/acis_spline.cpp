// SPDX-License-Identifier: MPL-2.0
// Port of acadrust entities/acis/types.rs: interpolated/UV curves and spline surfaces.
#include <libopencad/acis.hpp>
#include "acis_spline_limits.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
namespace libopencad::acis {
namespace {
using Tokens = std::vector<SatToken>;
struct DecodeFailure {};
constexpr std::size_t allocation_limit = 1u << 20;
template <class F> auto attempt(F f) -> std::optional<decltype(f())> {
    try {
        return f();
    } catch (const DecodeFailure &) {
        return {};
    }
}
const SatToken &token(const Tokens &t, std::size_t i) {
    if (i >= t.size())
        throw DecodeFailure{};
    return t[i];
}
std::string_view id(const Tokens &t, std::size_t i) {
    return i < t.size() ? t[i].as_ident().value_or("") : "";
}
double number(const Tokens &t, std::size_t i) {
    auto v = token(t, i).as_float();
    if (!v)
        throw DecodeFailure{};
    return *v;
}
std::int64_t integer(const Tokens &t, std::size_t i) {
    auto v = token(t, i).as_integer();
    if (!v)
        throw DecodeFailure{};
    return *v;
}
std::size_t count(const Tokens &t, std::size_t i) {
    auto n = integer(t, i);
    if (n < 0 || std::uint64_t(n) > allocation_limit)
        throw DecodeFailure{};
    return std::size_t(n);
}
std::size_t spline_degree(const Tokens &t, std::size_t i) {
    const auto degree = count(t, i);
    if (degree > max_acis_spline_degree)
        throw DecodeFailure{};
    return degree;
}
bool spline(std::string_view s) { return s == "nubs" || s == "nurbs"; }
bool surface(std::string_view s) {
    return s == "null_surface" || s == "spline" || s == "plane" || s == "cone" || s == "sphere" ||
           s == "torus";
}
template <class F> std::size_t find(const Tokens &t, std::size_t begin, F predicate) {
    for (auto i = begin; i < t.size(); ++i)
        if (predicate(id(t, i)))
            return i;
    throw DecodeFailure{};
}
std::size_t named(const Tokens &t, std::string_view name, std::size_t begin = 0) {
    return find(t, begin, [&](auto s) { return s == name; });
}
std::size_t close_block(const Tokens &t, std::size_t open) {
    if (id(t, open) != "{")
        throw DecodeFailure{};
    std::size_t depth = 0;
    for (auto i = open; i < t.size(); ++i) {
        if (id(t, i) == "{")
            ++depth;
        if (id(t, i) == "}") {
            if (!depth)
                throw DecodeFailure{};
            if (!--depth)
                return i;
        }
    }
    throw DecodeFailure{};
}
Tokens slice(const Tokens &t, std::size_t begin, std::size_t end) {
    if (begin > end || end > t.size())
        throw DecodeFailure{};
    return {t.begin() + begin, t.begin() + end};
}
std::optional<std::size_t> reference(const Tokens &t) {
    return attempt([&] {
        auto p = named(t, "{");
        if (id(t, p + 1) != "ref" || id(t, p + 3) != "}")
            throw DecodeFailure{};
        return count(t, p + 2);
    });
}
Tokens definition(const SatDocument &d, const Tokens &t, std::size_t open) {
    if (id(t, open + 1) == "ref") {
        auto result = d.subtype_tokens(count(t, open + 2));
        if (!result)
            throw DecodeFailure{};
        return *result;
    }
    return slice(t, open + 1, close_block(t, open));
}
Tokens scalar(const Tokens &t) {
    Tokens out;
    for (const auto &x : t) {
        if (auto c = x.coordinates()) {
            for (std::size_t i = 0; i < c->second; ++i)
                out.emplace_back(c->first[i]);
        } else
            out.push_back(x);
    }
    return out;
}
bool closed(const Tokens &t) {
    auto p = attempt([&] { return find(t, 0, spline); });
    if (!p || *p + 2 >= t.size())
        return false;
    auto v = token(t, *p + 2).as_integer();
    return id(t, *p + 2) == "closed" || id(t, *p + 2) == "periodic" || (v && *v != 0);
}
std::vector<double> knots(const Tokens &t, std::size_t &pos, std::size_t n, std::size_t degree) {
    if (n > (t.size() - std::min(pos, t.size())) / 2)
        throw DecodeFailure{};
    std::vector<double> out;
    for (std::size_t k = 0; k < n; ++k) {
        const auto value = number(t, pos);
        const auto multiplicity = count(t, pos + 1);
        if (multiplicity > degree + 1 || multiplicity > allocation_limit - out.size())
            throw DecodeFailure{};
        out.insert(out.end(), multiplicity, value);
        pos += 2;
    }
    return out;
}
void pad(std::vector<double> &k) {
    auto first = k.front(), last = k.back();
    k.insert(k.begin(), first);
    k.push_back(last);
}
SatBSplineCurve curve_at(const Tokens &t, std::size_t start, bool uv) {
    bool rational = id(t, start) == "nurbs";
    auto degree = spline_degree(t, start + 1);
    auto n = count(t, start + 3);
    auto p = start + 4;
    auto k = knots(t, p, n, degree);
    if (k.size() < 2 || degree >= k.size())
        throw DecodeFailure{};
    auto stride = std::size_t((uv ? 2 : 3) + (rational ? 1 : 0));
    std::size_t available = 0;
    for (auto i = p; i < t.size() && t[i].as_float(); ++i)
        ++available;
    available /= stride;
    auto controls = k.size() - degree - 1;
    if (available >= controls + 2) {
        pad(k);
        controls += 2;
    }
    if (controls <= degree || controls > available)
        throw DecodeFailure{};
    SatBSplineCurve out{degree, std::move(k), {}};
    out.controls.reserve(controls);
    for (std::size_t i = 0; i < controls; ++i) {
        auto x = number(t, p), y = number(t, p + 1), z = uv ? 0 : number(t, p + 2),
             w = rational ? number(t, p + stride - 1) : 1;
        out.controls.push_back({x * w, y * w, z * w, w});
        p += stride;
    }
    return out;
}
SatBSplineCurve curve(const Tokens &t) {
    auto i = find(t, 0, [](auto s) { return spline(s) || surface(s); });
    if (!spline(id(t, i)))
        throw DecodeFailure{};
    return curve_at(t, i, false);
}
std::array<double, 4> de_boor(const SatBSplineCurve &c, double t) {
    auto n = c.controls.size(), degree = c.degree, k = degree;
    while (k + 1 < c.knots.size() && c.knots[k + 1] <= t && k + 1 <= n)
        ++k;
    k = std::max(std::min(k, n - 1), degree);
    std::vector<std::array<double, 4>> d;
    for (std::size_t j = 0; j <= degree; ++j)
        d.push_back(c.controls[std::min(k - degree + j, n - 1)]);
    for (std::size_t r = 1; r <= degree; ++r)
        for (std::size_t j = degree;; --j) {
            auto i = k - degree + j;
            auto denom = c.knots[i + degree - r + 1] - c.knots[i];
            auto a = std::abs(denom) > 1e-12 ? (t - c.knots[i]) / denom : 0.;
            for (std::size_t x = 0; x < 4; ++x)
                d[j][x] = (1 - a) * d[j - 1][x] + a * d[j][x];
            if (j == r)
                break;
        }
    return d[degree];
}
std::vector<Vector3> samples(const SatBSplineCurve &c, double start, double end, std::size_t segments) {
    auto a = std::max(std::fmin(start, end), c.knots[c.degree]),
         b = std::min(std::fmax(start, end), c.knots[c.knots.size() - c.degree - 1]);
    if (!(b > a))
        return {};
    segments = std::max<std::size_t>(1, segments);
    if (segments >= allocation_limit)
        throw std::length_error("ACIS spline sample limit exceeded");
    // De Boor is quadratic in degree; bound CPU work as well as allocation.
    constexpr std::size_t work_limit = 1u << 26;
    const auto order = c.degree + 1;
    if (order > work_limit / order || segments + 1 > work_limit / order / order)
        throw std::length_error("ACIS spline evaluation work limit exceeded");
    std::vector<Vector3> out;
    out.reserve(segments + 1);
    for (std::size_t s = 0; s <= segments; ++s) {
        auto p = de_boor(c, a + (b - a) * (double(s) / double(segments)));
        auto w = std::abs(p[3]) > 1e-12 ? p[3] : 1.;
        out.push_back({p[0] / w, p[1] / w, p[2] / w});
    }
    return out;
}
std::pair<double, double> offsets(const SatRecord &r) {
    std::vector<double> v;
    for (auto i = r.tokens.rbegin(); i != r.tokens.rend() && v.size() < 2; ++i)
        if (auto x = i->as_float())
            v.push_back(*x);
    return v.size() == 2 ? std::make_pair(v[1], v[0]) : std::make_pair(0., 0.);
}
std::pair<SatBSplineCurve, bool> referenced_curve(const SatRecord &r, const SatDocument &d) {
    std::int64_t selector = 1;
    for (const auto &t : r.tokens)
        if (auto i = t.as_integer()) {
            selector = *i;
            break;
        }
    auto ptr = r.nth_pointer(1);
    const auto *target = ptr ? d.resolve(*ptr) : nullptr;
    if (!target)
        throw DecodeFailure{};
    auto t = target->tokens;
    if (!attempt([&] { return named(t, "par_int_cur"); }))
        if (auto ref = reference(t)) {
            auto v = d.subtype_tokens(*ref);
            if (!v)
                throw DecodeFailure{};
            t = *v;
        }
    std::size_t depth = 0;
    std::optional<std::size_t> level;
    std::vector<std::size_t> slots;
    for (std::size_t i = 0; i < t.size(); ++i) {
        auto s = id(t, i);
        if (s == "{")
            ++depth;
        else if (s == "}") {
            if (depth)
                --depth;
        } else if (spline(s) || s == "nullbs") {
            if (!level)
                level = depth;
            if (*level == depth)
                slots.push_back(i);
        }
    }
    auto support = selector < 0 ? std::uint64_t(-(selector + 1)) + 1 : std::uint64_t(selector);
    support = std::max<std::uint64_t>(support, 1);
    if (support >= slots.size() || id(t, slots[support]) == "nullbs")
        throw DecodeFailure{};
    return {curve_at(t, slots[support], true), (selector < 0) != (target->token_sense(1) == Sense::Reversed)};
}
std::pair<SatBSplineCurve, bool> pcurve(const SatRecord &r, const SatDocument &d) {
    if (auto direct = attempt([&] { return curve_at(r.tokens, find(r.tokens, 0, spline), true); }))
        return {*direct, false};
    return referenced_curve(r, d);
}
SatBSplineSurface surface_net(const Tokens &t) {
    std::optional<std::size_t> last;
    for (std::size_t i = 0; i < t.size(); ++i)
        if (spline(id(t, i)))
            last = i;
    if (!last)
        throw DecodeFailure{};
    auto s = *last;
    bool rational = id(t, s) == "nurbs";
    auto du = spline_degree(t, s + 1), dv = spline_degree(t, s + 2), f = s + 3;
    if (rational && (id(t, f) == "u" || id(t, f) == "v" || id(t, f) == "both" || id(t, f) == "none"))
        ++f;
    auto string_at = [&](auto i) -> std::optional<std::string> {
        auto text = id(t, i);
        return text.empty() ? std::nullopt : std::optional<std::string>(text);
    };
    SatBSplineSurface out;
    out.rational = rational;
    out.degree_u = du;
    out.degree_v = dv;
    out.u_closure = string_at(f);
    out.v_closure = string_at(f + 1);
    out.u_singularity = string_at(f + 2);
    out.v_singularity = string_at(f + 3);
    auto nu = count(t, f + 4), nv = count(t, f + 5), pos = f + 6;
    out.u_knots = knots(t, pos, nu, du);
    out.v_knots = knots(t, pos, nv, dv);
    if (out.u_knots.size() < 2 || out.v_knots.size() < 2 || du >= out.u_knots.size() ||
        dv >= out.v_knots.size())
        throw DecodeFailure{};
    std::vector<double> values;
    for (auto i = pos; i < t.size() && id(t, i) != "}"; ++i) {
        if (auto c = t[i].coordinates()) {
            for (std::size_t j = 0; j < c->second; ++j)
                values.push_back(c->first[j]);
        } else if (auto x = t[i].as_float())
            values.push_back(*x);
        else if (auto n = t[i].as_integer())
            values.push_back(double(*n));
    }
    auto bu = out.u_knots.size() - du - 1, bv = out.v_knots.size() - dv - 1,
         stride = std::size_t(rational ? 4 : 3);
    std::optional<std::tuple<std::size_t, std::size_t, bool, bool, std::size_t>> best;
    for (bool cu : {false, true})
        for (bool cv : {false, true}) {
            auto u = bu + 2 * cu, v = bv + 2 * cv;
            if (u <= du || v <= dv || u > allocation_limit / v)
                continue;
            auto needed = u * v * stride;
            if (needed > values.size())
                continue;
            auto rest = values.size() - needed;
            if (!best || rest < std::get<4>(*best))
                best = std::make_tuple(u, v, cu, cv, rest);
        }
    if (!best)
        throw DecodeFailure{};
    auto [u, v, cu, cv, rest] = *best;
    (void)rest;
    out.control_count_u = u;
    out.control_count_v = v;
    if (cu)
        pad(out.u_knots);
    if (cv)
        pad(out.v_knots);
    for (std::size_t i = 0; i < u * v * stride; i += stride) {
        auto w = rational ? values[i + 3] : 1.;
        out.control_points.push_back({values[i] * w, values[i + 1] * w, values[i + 2] * w, w});
    }
    if (values.size() > u * v * stride)
        out.fit_tolerance = values[u * v * stride];
    return out;
}
std::array<double, 3> read3(const Tokens &t, std::size_t p) {
    return {number(t, p), number(t, p + 1), number(t, p + 2)};
}
template <class T> bool finite(const T &v) {
    return std::all_of(v.begin(), v.end(), [](double n) { return std::isfinite(n); });
}
std::array<double, 4> bounds(const Tokens &t, std::size_t p) {
    if (integer(t, p) != 2)
        throw DecodeFailure{};
    std::array<double, 4> v;
    for (std::size_t i = 0; i < 4; ++i) {
        if (token(t, p + 1 + 2 * i).as_float())
            throw DecodeFailure{};
        v[i] = number(t, p + 2 + 2 * i);
    }
    if (!finite(v))
        throw DecodeFailure{};
    return v;
}
std::size_t skip_range(const Tokens &t, std::size_t p, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (id(t, p).empty())
            throw DecodeFailure{};
        p += id(t, p) == "I" ? 1 : 2;
    }
    return p;
}
std::optional<bool> against(const SatBSplineCurve &c, double low, double high) {
    auto first = c.knots[c.degree], last = c.knots[c.knots.size() - c.degree - 1],
         tolerance = 1e-6 * std::max(std::abs(last - first), 1.);
    auto within = [&](double a, double b) { return low >= a - tolerance && high <= b + tolerance; };
    if (within(first, last))
        return false;
    if (within(-last, -first))
        return true;
    return {};
}
void reverse(SatBSplineCurve &c) {
    std::reverse(c.knots.begin(), c.knots.end());
    for (auto &k : c.knots)
        k = -k;
    std::reverse(c.controls.begin(), c.controls.end());
}
SatBSplineSurface ruled(const SatBSplineCurve &c, const std::array<double, 4> &range,
                        const std::array<double, 3> &root, const std::array<double, 3> &direction,
                        const std::array<double, 3> &origin, bool closed_profile) {
    SatBSplineSurface out;
    out.degree_u = c.degree;
    out.degree_v = 1;
    out.u_closure = closed_profile ? "closed" : "open";
    out.v_closure = "open";
    out.u_knots = c.knots;
    out.v_knots = {range[2], range[2], range[3], range[3]};
    out.control_count_u = c.controls.size();
    out.control_count_v = 2;
    out.fit_tolerance = 0.;
    for (auto v : {range[2], range[3]})
        for (auto p : c.controls) {
            out.rational |= p[3] != 1.;
            for (std::size_t j = 0; j < 3; ++j)
                p[j] += p[3] * (root[j] + direction[j] * v - origin[j]);
            out.control_points.push_back(p);
        }
    return out;
}
SatBSplineSurface extrusion(const SatDocument &d, const Tokens &source) {
    auto t = scalar(source);
    auto open = named(t, "{", named(t, "cyl_spl_sur"));
    auto close = close_block(t, open);
    auto profile = scalar(definition(d, t, open));
    auto c = curve(profile);
    auto p = skip_range(t, close + 1, 2);
    auto dir = read3(t, p);
    read3(t, p + 3);
    auto b = bounds(t, p + 6);
    if (!finite(dir) || b[2] >= b[3])
        throw DecodeFailure{};
    // Preserve the upstream multiplication order for homogeneous offsets.
    auto out = ruled(c, b, {0, 0, 0}, dir, {0, 0, 0}, closed(profile));
    out.control_points.clear();
    for (auto v : {b[2], b[3]})
        for (auto x : c.controls) {
            for (std::size_t j = 0; j < 3; ++j)
                x[j] += x[3] * dir[j] * v;
            out.control_points.push_back(x);
        }
    return out;
}
SatBSplineSurface linear_sum(const Tokens &source) {
    auto t = scalar(source);
    auto open = named(t, "{", named(t, "intcurve", named(t, "sum_spl_sur") + 1) + 1);
    auto close = close_block(t, open);
    auto c = curve(slice(t, open + 1, close));
    auto straight = named(t, "straight", close + 1);
    auto root = read3(t, straight + 1), dir = read3(t, straight + 4);
    if (token(t, straight + 7).as_float() || token(t, straight + 8).as_float())
        throw DecodeFailure{};
    auto origin = read3(t, straight + 9);
    auto b = bounds(t, straight + 12);
    if (!finite(root) || !finite(dir) || !finite(origin) || b[0] >= b[1] || b[2] >= b[3])
        throw DecodeFailure{};
    if (against(c, b[0], b[1]) == true)
        reverse(c);
    return ruled(c, b, root, dir, origin, false);
}
SatBSplineSurface revolution(const SatDocument &d, const Tokens &source) {
    auto t = scalar(source);
    auto curve_start = find(t, named(t, "rot_spl_sur") + 1, [](auto s) { return !s.empty(); });
    if (id(t, curve_start) != "intcurve" || id(t, curve_start + 1) == "T" ||
        id(t, curve_start + 1) == "reversed")
        throw DecodeFailure{};
    auto open = id(t, curve_start + 1) == "{" ? curve_start + 1 : curve_start + 2;
    if (id(t, open) != "{")
        throw DecodeFailure{};
    auto close = close_block(t, open);
    auto profile = scalar(definition(d, t, open));
    auto c = curve(profile);
    auto p = skip_range(t, close + 1, 2);
    auto root = read3(t, p), axis = read3(t, p + 3);
    auto b = bounds(t, p + 6);
    auto length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]), sweep = b[3] - b[2];
    constexpr double pi = 3.14159265358979323846;
    if (!finite(root) || !finite(axis) || length < 1e-12 || !(sweep > 0 && sweep <= 2 * pi + 1e-9))
        throw DecodeFailure{};
    for (auto &a : axis)
        a /= length;
    auto reversed = against(c, b[0], b[1]);
    if (!reversed)
        throw DecodeFailure{};
    if (*reversed)
        reverse(c);
    auto spans = std::size_t(std::max(std::ceil(sweep / (pi / 2) - 1e-9), 1.));
    auto step = sweep / double(spans), weight = std::cos(step / 2);
    SatBSplineSurface out;
    out.rational = true;
    out.degree_u = c.degree;
    out.degree_v = 2;
    out.u_closure = closed(profile) ? "closed" : "open";
    out.v_closure = std::abs(sweep - 2 * pi) < 1e-9 ? "closed" : "open";
    out.u_knots = c.knots;
    out.v_knots.assign(3, b[2]);
    for (std::size_t span = 1; span < spans; ++span) {
        auto knot = b[2] + sweep * double(span) / double(spans);
        out.v_knots.insert(out.v_knots.end(), 2, knot);
    }
    out.v_knots.insert(out.v_knots.end(), 3, b[3]);
    std::vector<std::array<double, 3>> rows{{b[2], 1, 1}};
    for (std::size_t span = 0; span < spans; ++span) {
        auto start = b[2] + step * double(span);
        rows.push_back({start + step / 2, 1 / weight, weight});
        rows.push_back({start + step, 1, 1});
    }
    out.control_count_u = c.controls.size();
    out.control_count_v = rows.size();
    out.fit_tolerance = 0.;
    for (auto row : rows) {
        auto si = std::sin(row[0]), co = std::cos(row[0]);
        for (auto cp : c.controls) {
            std::array<double, 3> point, x, y;
            for (std::size_t j = 0; j < 3; ++j)
                point[j] = cp[j] / cp[3] - root[j];
            auto along = point[0] * axis[0] + point[1] * axis[1] + point[2] * axis[2];
            for (std::size_t j = 0; j < 3; ++j)
                x[j] = point[j] - axis[j] * along;
            y = {axis[1] * x[2] - axis[2] * x[1], axis[2] * x[0] - axis[0] * x[2],
                 axis[0] * x[1] - axis[1] * x[0]};
            auto w = cp[3] * row[2];
            std::array<double, 4> h;
            for (std::size_t j = 0; j < 3; ++j)
                h[j] = (root[j] + axis[j] * along + row[1] * (x[j] * co + y[j] * si)) * w;
            h[3] = w;
            out.control_points.push_back(h);
        }
    }
    return out;
}
SatBSplineSurface decode_surface(const SatDocument &d, const Tokens &t) {
    auto first = attempt([&] {
        return find(t, 0, [](auto s) {
            return !s.empty() && s != "{" && s != "forward" && s != "reversed" && s != "forward_v" &&
                   s != "reversed_v";
        });
    });
    auto name = first ? id(t, *first) : "";
    if (name == "sum_spl_sur") {
        if (auto v = attempt([&] { return linear_sum(t); }))
            return *v;
    } else if (name == "cyl_spl_sur") {
        if (auto v = attempt([&] { return extrusion(d, t); }))
            return *v;
        auto close = close_block(t, named(t, "{", named(t, "cyl_spl_sur")));
        std::optional<std::size_t> last;
        for (std::size_t i = 0; i < t.size(); ++i)
            if (spline(id(t, i)))
                last = i;
        if (!last || *last <= close)
            throw DecodeFailure{};
    } else if (name == "rot_spl_sur") {
        if (auto v = attempt([&] { return revolution(d, t); }))
            return *v;
    }
    return surface_net(t);
}
bool family(std::string_view name, std::string_view base) {
    return name == base || (name.size() > base.size() && name[name.size() - base.size() - 1] == '-' &&
                            name.substr(name.size() - base.size()) == base);
}
} // namespace
std::optional<Tokens> SatDocument::subtype_tokens(std::size_t requested) const {
    return attempt([&] {
        std::size_t index = 0;
        for (const auto &r : records)
            for (std::size_t i = 0; i < r.tokens.size(); ++i)
                if (id(r.tokens, i) == "{" && id(r.tokens, i + 1) != "ref") {
                    if (index++ == requested)
                        return slice(r.tokens, i + 1, close_block(r.tokens, i));
                }
        throw DecodeFailure{};
    });
}
std::optional<SatIntCurve> SatIntCurve::from_record(const SatRecord &r) {
    for (auto name : {"intcurve-curve", "bs3-curve", "exactcur-curve"})
        if (family(r.entity_type, name))
            return SatIntCurve(r);
    return {};
}
Sense SatIntCurve::sense() const { return record_.token_sense(1); }
std::optional<SatBSplineCurve> SatIntCurve::bspline() const {
    return attempt([&] { return curve(record_.tokens); });
}
std::optional<SatBSplineCurve> SatIntCurve::bspline_in(const SatDocument &d) const {
    if (auto v = bspline())
        return v;
    auto r = reference(record_.tokens);
    auto t = r ? d.subtype_tokens(*r) : std::nullopt;
    return t ? attempt([&] { return curve(*t); }) : std::nullopt;
}
bool SatIntCurve::is_closed_in(const SatDocument &d) const {
    auto t = record_.tokens;
    if (auto r = reference(t)) {
        auto v = d.subtype_tokens(*r);
        if (!v)
            return false;
        t = *v;
    }
    return closed(t);
}
std::vector<Vector3> SatIntCurve::sample(std::size_t segments) const {
    auto c = bspline();
    if (!c)
        return {};
    return samples(*c, c->knots[c->degree], c->knots[c->knots.size() - c->degree - 1], segments);
}
std::vector<Vector3> SatIntCurve::sample_range(double a, double b, std::size_t segments) const {
    auto c = bspline();
    return c ? samples(*c, a, b, segments) : std::vector<Vector3>{};
}
std::optional<SatCurveSupport> SatIntCurve::support_in(const SatDocument &d) const {
    return attempt([&] {
        auto t = definition(d, record_.tokens, named(record_.tokens, "{"));
        std::size_t depth = 0;
        std::optional<std::size_t> s;
        for (std::size_t i = 1; i < t.size(); ++i) {
            auto name = id(t, i);
            if (name == "{")
                ++depth;
            else if (name == "}") {
                if (!depth)
                    throw DecodeFailure{};
                --depth;
            } else if (!depth && surface(name)) {
                s = i;
                break;
            }
        }
        if (!s || id(t, *s) != "spline")
            throw DecodeFailure{};
        auto open = named(t, "{", *s + 1), close = close_block(t, open);
        auto net = decode_surface(d, definition(d, t, open));
        double offset = 0;
        if (id(t, 0) == "off_surf_int_cur") {
            if (t.size() < 3)
                throw DecodeFailure{};
            for (std::size_t i = t.size() - 3; i < t.size(); ++i)
                number(t, i);
            offset = number(t, t.size() - 3);
        }
        depth = 0;
        for (auto i = close + 1; i < t.size(); ++i) {
            auto name = id(t, i);
            if (name == "{")
                ++depth;
            else if (name == "}") {
                if (!depth)
                    throw DecodeFailure{};
                --depth;
            } else if (!depth && spline(name)) {
                auto c = curve_at(t, i, true);
                SatBSplinePCurve p{c.degree, c.knots, {}};
                for (auto v : c.controls)
                    p.controls.push_back({v[0], v[1], v[3]});
                return SatCurveSupport{net, p, offset};
            } else if (!depth && name == "nullbs")
                throw DecodeFailure{};
        }
        throw DecodeFailure{};
    });
}
std::optional<SatPCurve> SatPCurve::from_record(const SatRecord &r) {
    return r.entity_type == "pcurve" ? std::optional<SatPCurve>(SatPCurve(r)) : std::nullopt;
}
Sense SatPCurve::sense() const {
    if (record_.tokens.size() > 2) {
        const auto &v = record_.tokens[2].value;
        if (auto b = std::get_if<bool>(&v))
            return *b ? Sense::Forward : Sense::Reversed;
        if (auto b = std::get_if<SabToken>(&v)) {
            if (b->tag == 0x0a)
                return Sense::Reversed;
            if (b->tag == 0x0b)
                return Sense::Forward;
        }
    }
    return record_.token_sense(2);
}
std::optional<SatBSplinePCurve> SatPCurve::bspline_in(const SatDocument &d) const {
    return attempt([&] {
        auto [c, rev] = pcurve(record_, d);
        auto [u, v] = offsets(record_);
        SatBSplinePCurve out{c.degree, c.knots, {}};
        for (auto p : c.controls)
            out.controls.push_back({p[0] + u * p[3], p[1] + v * p[3], p[3]});
        if (rev) {
            std::reverse(out.controls.begin(), out.controls.end());
            auto sum = out.knots[out.degree] + out.knots[out.controls.size()];
            std::reverse(out.knots.begin(), out.knots.end());
            for (auto &k : out.knots)
                k = sum - k;
        }
        return out;
    });
}
std::vector<Vector2> SatPCurve::sample(std::size_t segments) const {
    auto c = attempt([&] { return curve_at(record_.tokens, find(record_.tokens, 0, spline), true); });
    if (!c)
        return {};
    auto points = samples(*c, c->knots[c->degree], c->knots[c->knots.size() - c->degree - 1], segments);
    auto [u, v] = offsets(record_);
    std::vector<Vector2> out;
    for (auto p : points)
        out.push_back({p.x + u, p.y + v});
    return out;
}
std::vector<Vector2> SatPCurve::sample_in(const SatDocument &d, std::size_t segments) const {
    auto direct = sample(segments);
    if (!direct.empty())
        return direct;
    auto c = attempt([&] { return referenced_curve(record_, d); });
    if (!c)
        return {};
    auto points = samples(c->first, c->first.knots[c->first.degree],
                          c->first.knots[c->first.knots.size() - c->first.degree - 1], segments);
    if (c->second)
        std::reverse(points.begin(), points.end());
    auto [u, v] = offsets(record_);
    std::vector<Vector2> out;
    for (auto p : points)
        out.push_back({p.x + u, p.y + v});
    return out;
}
std::optional<SatSplineSurface> SatSplineSurface::from_record(const SatRecord &r) {
    if (family(r.entity_type, "meshsurf-surface"))
        return SatSplineSurface(r, SatSplineSurfaceKind::Mesh);
    if (family(r.entity_type, "bs3-surface"))
        return SatSplineSurface(r, SatSplineSurfaceKind::Bs3);
    if (family(r.entity_type, "spline-surface"))
        return SatSplineSurface(r, SatSplineSurfaceKind::Spline);
    return {};
}
Sense SatSplineSurface::sense() const { return record_.token_sense(1); }
std::optional<Tokens> SatSplineSurface::definition_tokens(const SatDocument &d) const {
    auto r = reference(record_.tokens);
    return r ? d.subtype_tokens(*r) : std::optional<Tokens>(record_.tokens);
}
std::optional<SatBSplineSurface> SatSplineSurface::bspline(const SatDocument &d) const {
    auto t = definition_tokens(d);
    return t ? attempt([&] { return decode_surface(d, *t); }) : std::nullopt;
}
std::optional<std::pair<SatBSplineSurface, double>> SatSplineSurface::offset_in(const SatDocument &d) const {
    return attempt([&] {
        auto t = definition_tokens(d);
        if (!t)
            throw DecodeFailure{};
        auto name = find(*t, 0, [](auto n) { return n == "off_spl_sur" || n == "offsur"; });
        auto open = named(*t, "{", name);
        auto net = decode_surface(d, definition(d, *t, open));
        auto p = skip_range(*t, close_block(*t, open) + 1, 4);
        auto distance = number(*t, p);
        if (!std::isfinite(distance))
            throw DecodeFailure{};
        return std::make_pair(net, distance);
    });
}
} // namespace libopencad::acis
