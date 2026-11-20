// SPDX-License-Identifier: MPL-2.0
// acadrust entities/acis/writer.rs: spline/pcurve builders and bs3_tokens.
#include <libopencad/acis.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace libopencad::acis {
namespace {
using Tokens = std::vector<SatToken>;
SatToken id(std::string n) { return SatToken(SatIdent{std::move(n)}); }
SatToken en(std::string n) { return SatToken(SatEnum{std::move(n)}); }
SatRecord spline_record(std::string name) {
    SatRecord r;
    r.entity_type = std::move(name);
    r.tokens.emplace_back(SatPointer{});
    return r;
}
void finite(double n) {
    if (!std::isfinite(n))
        throw std::invalid_argument("Non-finite ACIS spline builder value");
}
std::size_t axis_count(std::int32_t degree, const KnotRuns &knots) {
    if (degree < 0 || knots.size() < 2)
        throw std::invalid_argument("Invalid spline degree or knot runs");
    std::size_t sum = 0;
    double last = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < knots.size(); ++i) {
        auto [k, m] = knots[i];
        finite(k);
        if (k < last || m < 0 || ((i == 0 || i + 1 == knots.size()) && m < 1) ||
            std::size_t(m) > (1u << 20) - sum)
            throw std::invalid_argument("Invalid spline knot multiplicity or ordering");
        sum += std::size_t(m);
        last = k;
    }
    if (sum <= std::size_t(degree) + 1 || sum - degree - 1 <= std::size_t(degree))
        throw std::invalid_argument("Insufficient spline controls");
    return sum - degree - 1;
}
void runs(Tokens &t, const KnotRuns &knots) {
    for (std::size_t i = 0; i < knots.size(); ++i) {
        t.emplace_back(knots[i].first);
        t.emplace_back(std::int64_t(knots[i].second - (i == 0 || i + 1 == knots.size())));
    }
}
void weights(const std::vector<double> &values) {
    for (double w : values)
        finite(w);
}
void point(Tokens &t, Vector3 v) {
    finite(v.x);
    finite(v.y);
    finite(v.z);
    t.emplace_back(v.x);
    t.emplace_back(v.y);
    t.emplace_back(v.z);
}
void append(Tokens &to, const Tokens &from) { to.insert(to.end(), from.begin(), from.end()); }
void infinite(Tokens &t, std::size_t n) {
    while (n--)
        t.push_back(id("I"));
}
Tokens net_tokens(const SplineNet &n) {
    auto u = axis_count(n.degree_u, n.u_knots), v = axis_count(n.degree_v, n.v_knots);
    if (u > std::numeric_limits<std::size_t>::max() / v || u * v != n.control.size())
        throw std::invalid_argument("Spline control net dimensions disagree");
    finite(n.fit_tolerance);
    weights(n.weights);
    Tokens t{en("full"), id(n.rational ? "nurbs" : "nubs"), SatToken(std::int64_t(n.degree_u)),
             SatToken(std::int64_t(n.degree_v))};
    if (n.rational)
        t.push_back(id("both"));
    append(t, {en(n.u_closed ? "closed" : "open"), en(n.v_closed ? "closed" : "open"), en("none"), en("none"),
               SatToken(std::int64_t(n.u_knots.size())), SatToken(std::int64_t(n.v_knots.size()))});
    runs(t, n.u_knots);
    runs(t, n.v_knots);
    for (std::size_t i = 0; i < n.control.size(); ++i) {
        point(t, n.control[i]);
        if (n.rational)
            t.emplace_back(i < n.weights.size() ? n.weights[i] : 1.);
    }
    t.emplace_back(n.fit_tolerance);
    for (unsigned i = 0; i < 6; ++i)
        t.emplace_back(std::int64_t(0));
    return t;
}
} // namespace
std::int32_t SatDocument::add_spline_curve(bool rational, std::int32_t degree, bool closed,
                                           const KnotRuns &knots, const std::vector<Vector3> &control,
                                           const std::vector<double> &w, double tolerance) {
    if (axis_count(degree, knots) != control.size())
        throw std::invalid_argument("Spline control count disagrees with knots");
    weights(w);
    finite(tolerance);
    auto r = spline_record("intcurve-curve");
    auto &t = r.tokens;
    append(t, {id("forward"), id("{"), id("exactcur"), en("full"), id(rational ? "nurbs" : "nubs"),
               SatToken(std::int64_t(degree)), en(closed ? "closed" : "open"),
               SatToken(std::int64_t(knots.size()))});
    runs(t, knots);
    for (std::size_t i = 0; i < control.size(); ++i) {
        point(t, control[i]);
        if (rational)
            t.emplace_back(i < w.size() ? w[i] : 1.);
    }
    t.emplace_back(tolerance);
    append(t, {id("null_surface"), id("null_surface"), id("nullbs"), id("nullbs"), id("I"), id("I"),
               SatToken(std::int64_t(0)), SatToken(std::int64_t(0)), SatToken(std::int64_t(0)), id("F"),
               SatToken(1.), id("F"), SatToken(0.), id("}"), id("I"), id("I")});
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_spline_surface(bool reverse_v, const SplineNet &n) {
    auto payload = net_tokens(n);
    auto r = spline_record("spline-surface");
    append(r.tokens, {id(reverse_v ? "reversed" : "forward"), id("{"), id("exactsur")});
    append(r.tokens, payload);
    infinite(r.tokens, 4);
    r.tokens.push_back(id("}"));
    infinite(r.tokens, 4);
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_offset_spline_surface(bool reverse_v, bool base_reverse_v,
                                                    const SplineNet &base, double distance,
                                                    const SplineNet &approximation) {
    finite(distance);
    auto a = net_tokens(base), b = net_tokens(approximation);
    auto r = spline_record("spline-surface");
    auto &t = r.tokens;
    append(t, {id(reverse_v ? "reversed" : "forward"), id("{"), id("offsur"), id("spline"),
               id(base_reverse_v ? "reversed" : "forward"), id("{"), id("exactsur")});
    append(t, a);
    for (double n : {1., 0., 1., 0.}) {
        t.push_back(id("F"));
        t.emplace_back(n);
    }
    t.push_back(id("}"));
    infinite(t, 4);
    t.emplace_back(distance);
    append(t, {id("forward"), id("forward")});
    append(t, b);
    t.push_back(id("}"));
    infinite(t, 4);
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_pcurve(bool rational, std::int32_t degree, bool closed, const KnotRuns &knots,
                                     const std::vector<Vector2> &control, const std::vector<double> &w,
                                     double tolerance, std::int32_t support, Vector2 offsets) {
    if (axis_count(degree, knots) != control.size())
        throw std::invalid_argument("Pcurve control count disagrees with knots");
    weights(w);
    finite(tolerance);
    finite(offsets.x);
    finite(offsets.y);
    const auto *s = record(support);
    if (!s)
        throw std::invalid_argument("Missing pcurve support surface");
    Tokens embedded;
    if (s->entity_type == "spline-surface") {
        std::optional<std::size_t> start, end;
        for (std::size_t i = 0; i < s->tokens.size(); ++i) {
            if (s->tokens[i].as_ident() == "{" && !start)
                start = i;
            if (s->tokens[i].as_ident() == "}")
                end = i;
        }
        if (!start || !end || *end <= *start || s->tokens.size() < 2)
            throw std::invalid_argument("Malformed spline support subtype");
        std::int64_t index = 0;
        if (*start + 2 < s->tokens.size() && s->tokens[*start + 1].as_ident() == "ref") {
            auto n = s->tokens[*start + 2].as_integer();
            if (!n || *n < 0)
                throw std::invalid_argument("Invalid support subtype reference");
            index = *n;
        } else {
            for (const auto &rec : records) {
                if (&rec == s)
                    break;
                for (std::size_t i = 0; i + 1 < rec.tokens.size(); ++i)
                    if (rec.tokens[i].as_ident() == "{" && rec.tokens[i + 1].as_ident() != "ref")
                        ++index;
            }
        }
        embedded = {id("spline"), s->tokens[1], id("{"), id("ref"), SatToken(index), id("}")};
        embedded.insert(embedded.end(), s->tokens.begin() + *end + 1, s->tokens.end());
    } else if (s->entity_type == "plane-surface" || s->entity_type == "cone-surface" ||
               s->entity_type == "sphere-surface" || s->entity_type == "torus-surface") {
        embedded.push_back(id(s->entity_type.substr(0, s->entity_type.size() - 8)));
        if (s->tokens.empty())
            throw std::invalid_argument("Incomplete analytic support");
        embedded.insert(embedded.end(), s->tokens.begin() + 1, s->tokens.end());
        std::size_t keywords = 0;
        for (std::size_t i = 1; i < s->tokens.size(); ++i)
            if (!s->tokens[i].as_pointer() && !s->tokens[i].as_float() && !s->tokens[i].coordinates())
                ++keywords;
        if ((s->entity_type == "cone-surface" && keywords <= 2) ||
            (s->entity_type != "cone-surface" && keywords == 0)) {
            embedded.push_back(id(s->entity_type == "cone-surface" ? "forward" : "forward_v"));
            infinite(embedded, 4);
        }
    } else
        throw std::invalid_argument("Unsupported pcurve support surface");
    auto r = ::libopencad::acis::SatRecord{};
    r.entity_type = "pcurve";
    auto &t = r.tokens;
    t = {SatToken(SatPointer{}),
         SatToken(std::int64_t(0)),
         id("forward"),
         id("{"),
         id("exppc"),
         id(rational ? "nurbs" : "nubs"),
         SatToken(std::int64_t(degree)),
         en(closed ? "closed" : "open"),
         SatToken(std::int64_t(knots.size()))};
    runs(t, knots);
    for (std::size_t i = 0; i < control.size(); ++i) {
        finite(control[i].x);
        finite(control[i].y);
        t.emplace_back(control[i].x);
        t.emplace_back(control[i].y);
        if (rational)
            t.emplace_back(i < w.size() ? w[i] : 1.);
    }
    t.emplace_back(tolerance);
    append(t, embedded);
    t.push_back(id("}"));
    t.emplace_back(offsets.x);
    t.emplace_back(offsets.y);
    return add_record(std::move(r));
}
} // namespace libopencad::acis
