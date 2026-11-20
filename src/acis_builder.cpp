// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/entities/acis/writer.rs builder helpers at 99d5f488.
#include "libopencad/acis.hpp"
#include <cmath>
namespace libopencad::acis {
namespace {
SatToken p(SatPointer v = {}) { return SatToken(v); }
SatToken id(std::string value) { return SatToken(SatIdent{std::move(value)}); }
SatToken sense(Sense s) { return SatToken(SatEnum{s == Sense::Forward ? "forward" : "reversed"}); }
SatRecord rec(std::string name) {
    SatRecord r;
    r.entity_type = std::move(name);
    r.tokens = {p()};
    return r;
}
void vector(SatRecord &r, Vector3 v) {
    for (auto f : {v.x, v.y, v.z})
        r.tokens.emplace_back(f);
}
void tail(SatRecord &r, const char *s, std::size_t n) {
    r.tokens.push_back(id(s));
    while (n--)
        r.tokens.push_back(id("I"));
}
} // namespace
SatDocument SatDocument::new_body() {
    SatDocument d;
    d.header.num_bodies = 1;
    auto r = rec("body");
    r.tokens = {p(), p(), p(), p()};
    d.add_record(std::move(r));
    return d;
}
std::int32_t SatDocument::add_point(double x, double y, double z) {
    auto r = rec("point");
    vector(r, {x, y, z});
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_plane_surface(Vector3 root, Vector3 normal, Vector3 u) {
    auto r = rec("plane-surface");
    vector(r, root);
    vector(r, normal);
    vector(r, u);
    tail(r, "forward_v", 4);
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_straight_curve(Vector3 root, Vector3 direction) {
    auto r = rec("straight-curve");
    vector(r, root);
    vector(r, direction);
    r.tokens.push_back(id("I"));
    r.tokens.push_back(id("I"));
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_cone_surface(Vector3 center, Vector3 axis, Vector3 major, double ratio,
                                           double cosine, double sine) {
    auto r = rec("cone-surface");
    vector(r, center);
    vector(r, axis);
    vector(r, major);
    r.tokens.emplace_back(ratio);
    r.tokens.push_back(id("I"));
    r.tokens.push_back(id("I"));
    r.tokens.emplace_back(sine);
    r.tokens.emplace_back(cosine);
    r.tokens.emplace_back(major.length());
    tail(r, "forward", 4);
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_sphere_surface(Vector3 center, double radius, Vector3 u, Vector3 pole) {
    auto r = rec("sphere-surface");
    vector(r, center);
    r.tokens.emplace_back(radius);
    vector(r, u);
    vector(r, pole);
    tail(r, "forward_v", 4);
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_torus_surface(Vector3 center, Vector3 normal, double major, double minor,
                                            Vector3 u) {
    auto r = rec("torus-surface");
    vector(r, center);
    vector(r, normal);
    r.tokens.emplace_back(major);
    r.tokens.emplace_back(minor);
    vector(r, u);
    tail(r, "forward_v", 4);
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_ellipse_curve(Vector3 center, Vector3 normal, Vector3 major, double ratio) {
    auto r = rec("ellipse-curve");
    vector(r, center);
    vector(r, normal);
    vector(r, major);
    r.tokens.emplace_back(ratio);
    r.tokens.push_back(id("I"));
    r.tokens.push_back(id("I"));
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_vertex(SatPointer edge, SatPointer point) {
    auto r = rec("vertex");
    r.tokens = {p(), p(edge), p(point)};
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_edge(SatPointer sv, double sp, SatPointer ev, double ep, SatPointer co,
                                   SatPointer curve, Sense s) {
    auto r = rec("edge");
    r.tokens = {p(),      p(sv),        SatToken(sp),
                p(ev),    SatToken(ep), p(co),
                p(curve), sense(s),     SatToken(SatString{"unknown"})};
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_coedge(SatPointer next, SatPointer prev, SatPointer partner, SatPointer edge,
                                     Sense s, SatPointer loop) {
    auto r = rec("coedge");
    r.tokens = {p(), p(next), p(prev), p(partner), p(edge), sense(s), p(loop), p()};
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_loop(SatPointer next, SatPointer co, SatPointer face) {
    auto r = rec("loop");
    r.tokens = {p(), p(next), p(co), p(face)};
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_face(SatPointer next, SatPointer loop, SatPointer shell, SatPointer surface,
                                   Sense s, Sidedness side) {
    auto r = rec("face");
    r.tokens = {
        p(), p(next),    p(loop),  p(shell),
        p(), p(surface), sense(s), SatToken(SatEnum{side == Sidedness::Single ? "single" : "double"})};
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_shell(SatPointer face, SatPointer lump) {
    auto r = rec("shell");
    r.tokens = {p(), p(), p(), p(face), p(), p(lump)};
    return add_record(std::move(r));
}
std::int32_t SatDocument::add_lump(SatPointer shell, SatPointer body) {
    auto r = rec("lump");
    r.tokens = {p(), p(), p(shell), p(body)};
    return add_record(std::move(r));
}
} // namespace libopencad::acis
