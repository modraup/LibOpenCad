// SPDX-License-Identifier: MPL-2.0
// Port of acadrust modification/{translate,transform}.rs, raster_image.rs and wipeout.rs.
#include <libopencad/dxf_document.hpp>
#include <cmath>
#include <stdexcept>
namespace libopencad {
namespace {
bool finite(const Transform &t) {
    for (const auto &row : t.matrix.m)
        for (auto n : row)
            if (!std::isfinite(n))
                return false;
    return true;
}
void validate(const Transform &t) {
    if (!finite(t))
        throw std::invalid_argument("Nonfinite IMAGE/WIPEOUT transform");
}
} // namespace
void DxfRasterImage::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    auto d = definition();
    d.insertion = d.insertion + offset;
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfRasterImage::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto d = definition();
    d.insertion = t.apply(d.insertion);
    d.u_vector = t.apply_rotation(d.u_vector);
    d.v_vector = t.apply_rotation(d.v_vector);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfRasterImage::apply_mirror(const Transform &t) {
    if (!finite(t) || t.is_identity())
        return;
    apply_transform(t);
}
std::array<Point3, 4> DxfRasterImage::corners() const {
    auto d = definition();
    auto u = d.u_vector * d.size.x, v = d.v_vector * d.size.y;
    return {d.insertion, d.insertion + u, d.insertion + u + v, d.insertion + v};
}
BoundingBox3D DxfRasterImage::bounding_box() const {
    auto p = corners();
    auto b = BoundingBox3D::from_point(p[0]);
    for (std::size_t i = 1; i < p.size(); ++i)
        b.expand_to_include(p[i]);
    return b;
}
std::vector<Point3> DxfRasterImage::wipeout_world_boundary_vertices() const {
    if (!is_wipeout())
        throw std::invalid_argument("Wipeout boundary mapping requires WIPEOUT");
    auto d = definition();
    std::vector<Point3> points;
    points.reserve(d.boundary.size());
    for (auto p : d.boundary)
        points.push_back(d.insertion + d.u_vector * (p.x + d.size.x * .5) +
                         d.v_vector * (d.size.y * .5 - p.y));
    return points;
}
} // namespace libopencad
