// SPDX-License-Identifier: MPL-2.0
// Port of acadrust entities/modification/{translate,transform,mirror} and annotation bounds.
#include <libopencad/dxf_document.hpp>
#include <libopencad/mtext_format.hpp>
#include <cmath>
#include <stdexcept>
namespace libopencad {
namespace {
bool finite(const Transform &t) {
    for (const auto &row : t.matrix.m)
        for (double v : row)
            if (!std::isfinite(v))
                return false;
    return true;
}
void validate(const Transform &t) {
    if (!finite(t))
        throw std::invalid_argument("Nonfinite annotation transform");
}
void rotate_angle(double &a, const Transform &t) {
    auto d = t.apply_rotation({std::cos(a), std::sin(a), 0});
    if (d.x != 0 || d.y != 0)
        a = std::atan2(d.y, d.x);
}
void move(LeaderDefinition &d, Vector3 v) {
    for (auto &p : d.vertices)
        p = p + v;
}
void move(ToleranceDefinition &d, Vector3 v) { d.insertion = d.insertion + v; }
void move(MLineDefinition &d, Vector3 v) {
    d.start_point = d.start_point + v;
    for (auto &p : d.vertices)
        p.position = p.position + v;
}
template <class Fn> void anchors(MultiLeaderDefinition &d, Fn fn) {
    auto &c = d.context;
    fn(c.content_base_point);
    fn(c.text_location);
    fn(c.block_content_location);
    fn(c.base_point);
    for (auto &r : c.leader_roots) {
        fn(r.connection_point);
        for (auto &b : r.break_points) {
            fn(b.start);
            fn(b.end);
        }
        for (auto &l : r.lines) {
            for (auto &p : l.points)
                fn(p);
            // Rust transforms only a legacy break cache. Its DXF writer retains
            // the complete per-segment break_infos unchanged.
        }
    }
}
void move(MultiLeaderDefinition &d, Vector3 v) {
    anchors(d, [&](auto &p) { p = p + v; });
}
void transform(LeaderDefinition &d, const Transform &t) {
    for (auto &p : d.vertices)
        p = t.apply(p);
    d.block_offset = t.apply_rotation(d.block_offset);
    d.annotation_offset = t.apply_rotation(d.annotation_offset);
    double s = t.apply_rotation(Vector3::UNIT_X).length();
    d.text_height *= s;
    d.text_width *= s;
    d.horizontal_direction = t.apply_rotation(d.horizontal_direction).normalize();
    d.normal = t.apply_rotation(d.normal).normalize();
}
void transform(ToleranceDefinition &d, const Transform &t) {
    d.insertion = t.apply(d.insertion);
    d.direction = t.apply_rotation(d.direction).normalize();
    d.normal = t.apply_rotation(d.normal).normalize();
    double s = t.apply_rotation(Vector3::UNIT_X).length();
    d.text_height *= s;
    d.dimension_gap *= s;
}
void transform(MLineDefinition &d, const Transform &t) {
    d.start_point = t.apply(d.start_point);
    d.normal = t.apply_rotation(d.normal).normalize();
    for (auto &v : d.vertices) {
        v.position = t.apply(v.position);
        v.direction = t.apply_rotation(v.direction).normalize();
        v.miter = t.apply_rotation(v.miter).normalize();
    }
}
void transform(MultiLeaderDefinition &d, const Transform &t) {
    anchors(d, [&](auto &p) { p = t.apply(p); });
    auto &c = d.context;
    for (auto p : {&c.text_normal, &c.text_direction, &c.base_direction, &c.base_vertical})
        *p = t.apply_rotation(*p).normalize();
    double s = t.apply_rotation(Vector3::UNIT_X).length();
    for (auto &r : c.leader_roots) {
        r.direction = t.apply_rotation(r.direction).normalize();
        r.landing_distance *= s;
    }
    d.arrowhead_size *= s;
    d.text_height *= s;
    d.dogleg_length *= s;
    c.arrowhead_size *= s;
    c.text_height *= s;
    mtext::scale_inline_heights(c.text_string, s);
    c.landing_gap *= s;
    c.text_width *= s;
    c.text_boundary_height *= s;
    c.block_content_scale = c.block_content_scale * s;
    rotate_angle(c.text_rotation, t);
    rotate_angle(c.block_rotation, t);
    rotate_angle(d.block_rotation, t);
}
template <class Definition> void mirror(Definition &d, const Transform &t) { transform(d, t); }
void mirror(LeaderDefinition &d, const Transform &t) {
    transform(d, t);
    d.origin = t.apply(d.origin);
}
void mirror(MLineDefinition &d, const Transform &t) {
    transform(d, t);
    if (t.matrix.to_matrix3().determinant() < 0)
        d.scale_factor = -d.scale_factor;
}
} // namespace
#define ANNOTATION_OPERATIONS(Type)                                                                          \
    void Type::translate(Vector3 offset) {                                                                   \
        auto t = Transform::from_translation(offset);                                                        \
        validate(t);                                                                                         \
        auto e = *this;                                                                                      \
        auto d = e.definition();                                                                             \
        move(d, offset);                                                                                     \
        e.set_definition(d);                                                                                 \
        e.transform_extended_data(t);                                                                        \
        *this = std::move(e);                                                                                \
    }                                                                                                        \
    void Type::apply_transform(const Transform &t) {                                                         \
        validate(t);                                                                                         \
        auto e = *this;                                                                                      \
        auto d = e.definition();                                                                             \
        transform(d, t);                                                                                     \
        e.set_definition(d);                                                                                 \
        e.transform_extended_data(t);                                                                        \
        *this = std::move(e);                                                                                \
    }                                                                                                        \
    void Type::apply_mirror(const Transform &t) {                                                            \
        if (!finite(t) || t.is_identity())                                                                   \
            return;                                                                                          \
        auto e = *this;                                                                                      \
        auto d = e.definition();                                                                             \
        mirror(d, t);                                                                                        \
        e.set_definition(d);                                                                                 \
        e.transform_extended_data(t);                                                                        \
        *this = std::move(e);                                                                                \
    }
ANNOTATION_OPERATIONS(DxfLeader)
ANNOTATION_OPERATIONS(DxfMultiLeader)
ANNOTATION_OPERATIONS(DxfMLine)
ANNOTATION_OPERATIONS(DxfTolerance)
#undef ANNOTATION_OPERATIONS
BoundingBox3D DxfLeader::bounding_box() const {
    return BoundingBox3D::from_points(definition().vertices).value_or(BoundingBox3D{});
}
double DxfLeader::length() const {
    auto d = definition();
    double n = 0;
    for (std::size_t i = 1; i < d.vertices.size(); ++i)
        n += (d.vertices[i] - d.vertices[i - 1]).length();
    return n;
}
std::optional<BoundingBox3D> DxfMLine::bounding_box() const {
    std::vector<Point3> p;
    for (auto &v : definition().vertices)
        p.push_back(v.position);
    return BoundingBox3D::from_points(p);
}
double DxfMLine::length() const {
    auto d = definition();
    double n = 0;
    for (std::size_t i = 1; i < d.vertices.size(); ++i)
        n += (d.vertices[i].position - d.vertices[i - 1].position).length();
    if ((d.flags & 2) && d.vertices.size() > 1)
        n += (d.vertices.front().position - d.vertices.back().position).length();
    return n;
}
BoundingBox3D DxfMultiLeader::bounding_box() const {
    auto d = definition();
    std::vector<Point3> p{d.context.content_base_point};
    for (auto &r : d.context.leader_roots) {
        p.push_back(r.connection_point);
        for (auto &l : r.lines)
            p.insert(p.end(), l.points.begin(), l.points.end());
    }
    return *BoundingBox3D::from_points(p);
}
std::size_t DxfTolerance::line_count() const {
    auto text = definition().text;
    std::size_t n = 1;
    for (auto p = text.find("^J"); p != std::string::npos; p = text.find("^J", p + 2))
        ++n;
    return n;
}
BoundingBox3D DxfTolerance::bounding_box() const {
    auto d = definition();
    auto width = d.text.size() * d.text_height * .6, height = d.text_height * line_count();
    return {{d.insertion.x - width * .1, d.insertion.y - height * .5, d.insertion.z},
            {d.insertion.x + width, d.insertion.y + height * .5, d.insertion.z}};
}
} // namespace libopencad
