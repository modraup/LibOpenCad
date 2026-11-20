// SPDX-License-Identifier: MPL-2.0
// Port of acadrust modification/{translate,transform,mirror/{text,blocks}} and underlay.rs.
#include <libopencad/dxf_document.hpp>
#include <cmath>
#include <stdexcept>
namespace libopencad {
namespace {
constexpr double pi = 3.1415926535897932384626433832795, tau = 2 * pi;
bool finite(const Transform &t) {
    for (const auto &row : t.matrix.m)
        for (auto v : row)
            if (!std::isfinite(v))
                return false;
    return true;
}
void validate(const Transform &t) {
    if (!finite(t))
        throw std::invalid_argument("Nonfinite SHAPE/UNDERLAY transform");
}
double angle(double rotation, Vector3 old, const Transform &t) {
    auto ocs = Matrix3::arbitrary_axis(old);
    auto normal = t.apply_rotation(old).normalize();
    auto v = Matrix3::arbitrary_axis(normal).transpose() *
             t.apply_rotation(ocs * Vector3{std::cos(rotation), std::sin(rotation), 0});
    return v.x != 0 || v.y != 0 ? std::atan2(v.y, v.x) : rotation;
}
void transform(ShapeDefinition &d, const Transform &t) {
    auto old = d.normal;
    d.normal = t.apply_rotation(old).normalize();
    d.insertion =
        Matrix3::arbitrary_axis(d.normal).transpose() * t.apply(Matrix3::arbitrary_axis(old) * d.insertion);
    d.size *= t.apply_rotation(Vector3::UNIT_X).length();
    d.rotation = angle(d.rotation, old, t);
}
void transform(UnderlayDefinition &d, const Transform &t) {
    auto old = d.normal;
    d.insertion = t.apply(d.insertion);
    d.normal = t.apply_rotation(old).normalize();
    d.scale = d.scale * t.apply_rotation(Vector3::UNIT_X).length();
    d.rotation = angle(d.rotation, old, t);
}
} // namespace
void DxfShape::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    auto d = definition();
    d.insertion = d.insertion + Matrix3::arbitrary_axis(d.normal).transpose() * offset;
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfShape::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto d = definition();
    transform(d, t);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfShape::apply_mirror(const Transform &t) {
    if (!finite(t) || t.is_identity())
        return;
    auto e = *this;
    auto d = definition();
    transform(d, t);
    if (t.matrix.to_matrix3().determinant() < 0) {
        auto a = std::fmod(d.rotation + pi, tau);
        d.rotation = a < 0 ? a + tau : a;
        auto cosine = std::cos(d.oblique_angle);
        if (std::abs(cosine) > 1e-12) {
            d.size *= std::abs(cosine);
            d.relative_x_scale = -std::abs(d.relative_x_scale) / std::abs(cosine);
        } else
            d.relative_x_scale = -d.relative_x_scale;
        d.oblique_angle = -d.oblique_angle;
    }
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfUnderlay::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    auto d = definition();
    d.insertion = d.insertion + offset;
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfUnderlay::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto d = definition();
    transform(d, t);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfUnderlay::apply_mirror(const Transform &t) {
    if (!finite(t) || t.is_identity())
        return;
    auto e = *this;
    auto d = definition();
    auto frame = Matrix3::arbitrary_axis(d.normal) * Matrix3::rotation_z(d.rotation);
    auto x = t.apply_rotation(frame * Vector3{d.scale.x, 0, 0}),
         y = t.apply_rotation(frame * Vector3{0, d.scale.y, 0}),
         z = t.apply_rotation(frame * Vector3{0, 0, d.scale.z});
    transform(d, t);
    auto local = (Matrix3::arbitrary_axis(d.normal) * Matrix3::rotation_z(d.rotation)).transpose();
    d.scale = {(local * x).x, (local * y).y, (local * z).z};
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
std::vector<Point3> DxfUnderlay::world_clip_boundary() const {
    auto d = definition();
    auto ocs = Matrix3::arbitrary_axis(d.normal);
    double c = std::cos(d.rotation), s = std::sin(d.rotation);
    std::vector<Point3> result;
    result.reserve(d.boundary.size());
    for (auto p : d.boundary) {
        double x = p.x * d.scale.x, y = p.y * d.scale.y;
        result.push_back(d.insertion + ocs * Vector3{x * c - y * s, x * s + y * c, 0});
    }
    return result;
}
} // namespace libopencad
