// SPDX-License-Identifier: MPL-2.0
// Port of acadrust entities/modification/{translate,transform,mirror/{text,blocks}}.
#include <libopencad/dxf_document.hpp>
#include <libopencad/mtext_format.hpp>
#include "dxf_code_page.hpp"
#include <cmath>
#include <stdexcept>
namespace libopencad {
namespace {
constexpr double pi = 3.1415926535897932384626433832795, tau = 2 * pi;
bool finite_matrix(const Transform &t) {
    for (const auto &row : t.matrix.m)
        for (auto x : row)
            if (!std::isfinite(x))
                return false;
    return true;
}
void validate_matrix(const Transform &t) {
    if (!finite_matrix(t))
        throw std::invalid_argument("Nonfinite text/INSERT transform");
}
bool reflected(const Transform &t) { return t.matrix.to_matrix3().determinant() < 0; }
double scale_x(const Transform &t) { return t.apply_rotation(Vector3::UNIT_X).length(); }
double wrap(double a) {
    auto r = std::fmod(a, tau);
    return r < 0 ? r + tau : r;
}
Vector3 normal_transform(const Transform &t, Vector3 n) {
    if (auto inverse = t.matrix.to_matrix3().inverse()) {
        auto p = inverse->transpose() * n;
        auto len = p.length();
        if (len >= 1e-10)
            return p * (1. / len);
    }
    return n;
}
void translate_text(TextDefinition &d, Vector3 offset) {
    auto o = Matrix3::arbitrary_axis(d.normal).transpose() * offset;
    d.insertion = d.insertion + o;
    if (d.alignment_point)
        *d.alignment_point = *d.alignment_point + o;
}
void transform_text(TextDefinition &d, const Transform &t) {
    auto old = Matrix3::arbitrary_axis(d.normal);
    d.normal = t.apply_rotation(d.normal).normalize();
    auto inverse = Matrix3::arbitrary_axis(d.normal).transpose();
    d.insertion = inverse * t.apply(old * d.insertion);
    if (d.alignment_point)
        *d.alignment_point = inverse * t.apply(old * (*d.alignment_point));
    d.height *= scale_x(t);
    auto direction = inverse * t.apply_rotation(old * Vector3{std::cos(d.rotation), std::sin(d.rotation), 0});
    if (direction.x != 0 || direction.y != 0)
        d.rotation = std::atan2(direction.y, direction.x);
}
double estimated_width(const std::string &text) {
    double result = 0;
    for (auto c : detail::utf8_scalars(text)) {
        auto within = [&](char32_t a, char32_t b) { return a <= c && c <= b; };
        result += within(0x1100, 0x115f) || within(0x2e80, 0xa4cf) || within(0xac00, 0xd7a3) ||
                          within(0xf900, 0xfaff) || within(0xfe30, 0xfe4f) || within(0xff00, 0xff60) ||
                          within(0xffe0, 0xffe6)
                      ? 1.
                      : .6;
    }
    return result;
}
void mirror_text(TextDefinition &d, const Transform &t, const MirrorOptions &o) {
    auto rotation = d.rotation;
    auto width = o.text_width && std::isfinite(*o.text_width) && *o.text_width >= 0
                     ? *o.text_width
                     : estimated_width(d.text) * d.height * std::abs(d.width_factor);
    std::optional<Point3> unused;
    if (d.horizontal_alignment == 0 && d.vertical_alignment == 0) {
        unused = d.alignment_point;
        d.alignment_point.reset();
    }
    transform_text(d, t);
    if (reflected(t)) {
        if (o.mirror_text) {
            d.rotation = wrap(d.rotation + pi);
            d.generation_flags ^= 2;
        } else {
            bool reverse = std::cos(rotation) * std::cos(d.rotation) < 0;
            if (reverse)
                d.rotation += pi;
            d.rotation = wrap(d.rotation);
            Vector3 x{std::cos(d.rotation), std::sin(d.rotation), 0}, y{-x.y, x.x, 0};
            double sx = (d.generation_flags & 2) ? -1. : 1., sy = (d.generation_flags & 4) ? -1. : 1.;
            if (reverse) {
                if (d.horizontal_alignment == 3 || d.horizontal_alignment == 5) {
                    if (d.alignment_point)
                        std::swap(d.insertion, *d.alignment_point);
                } else {
                    d.insertion = d.insertion - x * (width * sx);
                    if (d.horizontal_alignment == 2 && d.alignment_point)
                        *d.alignment_point = *d.alignment_point + x * (width * sx);
                }
            } else {
                double vertical = d.vertical_alignment == 3                                  ? -1.
                                  : d.vertical_alignment == 2 || d.horizontal_alignment == 4 ? 0.
                                                                                             : 1.;
                auto offset = y * (-d.height * sy * vertical);
                d.insertion = d.insertion + offset;
                if (d.alignment_point)
                    *d.alignment_point = *d.alignment_point + offset;
            }
        }
    }
    if (unused)
        d.alignment_point = unused;
}
Vector3 mtext_direction(const MTextDefinition &d) {
    if (d.x_direction) {
        auto p = *d.x_direction;
        auto delta = wrap(std::atan2(p.y, p.x) - d.rotation + pi) - pi;
        if (std::hypot(p.x, p.y) <= 1e-12 || std::abs(delta) < 1e-12)
            return p;
    }
    return {std::cos(d.rotation), std::sin(d.rotation), 0};
}
void transform_mtext(MTextDefinition &d, const Transform &t) {
    auto direction = mtext_direction(d);
    d.insertion = t.apply(d.insertion);
    auto factor = scale_x(t);
    d.height *= factor;
    d.rectangle_width *= factor;
    if (d.rectangle_height)
        *d.rectangle_height *= factor;
    mtext::scale_inline_heights(d.text, factor);
    d.normal = t.apply_rotation(d.normal).normalize();
    direction = t.apply_rotation(direction);
    if (direction.length() > 1e-12)
        direction = direction.normalize();
    d.x_direction = direction;
    d.rotation = std::abs(direction.x) > 1e-12 || std::abs(direction.y) > 1e-12
                     ? std::atan2(direction.y, direction.x)
                     : 0.;
}
void mirror_mtext(MTextDefinition &d, const Transform &t, const MirrorOptions &o) {
    auto old = Matrix3::arbitrary_axis(d.normal).transpose() * mtext_direction(d);
    transform_mtext(d, t);
    auto direction = *d.x_direction;
    d.rotation = std::atan2(direction.y, direction.x);
    if (reflected(t)) {
        if (o.mirror_text)
            d.normal = -d.normal;
        else {
            auto x = Matrix3::arbitrary_axis(d.normal).transpose() * direction;
            bool reverse = old.x * x.x < 0;
            if (reverse)
                direction = -direction;
            d.x_direction = direction;
            d.rotation = std::atan2(direction.y, direction.x);
            int row = (d.attachment - 1) / 3, col = (d.attachment - 1) % 3;
            d.attachment = (reverse ? row : 2 - row) * 3 + (reverse ? 2 - col : col) + 1;
        }
    }
}
void translate_attribute(AttributeDefinition &d, Vector3 offset) {
    if (!d.text.alignment_point)
        d.text.alignment_point = Point3{};
    translate_text(d.text, offset);
    if (d.embedded_mtext)
        d.embedded_mtext->insertion = d.embedded_mtext->insertion + offset;
}
void transform_attribute(AttributeDefinition &d, const Transform &t) {
    if (!d.text.alignment_point)
        d.text.alignment_point = Point3{};
    transform_text(d.text, t);
    if (d.embedded_mtext)
        transform_mtext(*d.embedded_mtext, t);
}
void mirror_attribute(AttributeDefinition &d, const Transform &t, const MirrorOptions &o) {
    auto a = d.text.alignment_point.value_or(Point3{});
    bool unused = d.text.horizontal_alignment == 0 && d.text.vertical_alignment == 0;
    if (unused)
        d.text.alignment_point.reset();
    else if (!d.text.alignment_point)
        d.text.alignment_point = a;
    mirror_text(d.text, t, o);
    if (!d.text.alignment_point)
        d.text.alignment_point = a;
    if (d.embedded_mtext)
        mirror_mtext(*d.embedded_mtext, t, o);
}
void transform_insert(InsertDefinition &d, const Transform &t) {
    auto normal = normal_transform(t, d.normal);
    auto old = Matrix3::arbitrary_axis(d.normal);
    auto inverse = Matrix3::arbitrary_axis(normal).transpose();
    auto position = inverse * t.apply(old * d.insertion);
    auto frame = old * Matrix3::rotation_z(d.rotation);
    auto linear = t.matrix.to_matrix3();
    auto v = inverse * (linear * (frame * Vector3::UNIT_X));
    auto rotation = std::atan2(v.y, v.x);
    auto local = Matrix3::rotation_z(rotation).transpose() * inverse;
    auto scales = local * (linear * (frame * d.scale));
    auto spacing = [&](double &s, Vector3 axis, bool x) {
        if (s == 0)
            return;
        auto world = linear * (frame * (axis * s));
        auto p = local * world;
        s = std::copysign(world.length(), x ? p.x : p.y);
    };
    spacing(d.column_spacing, Vector3::UNIT_X, true);
    spacing(d.row_spacing, Vector3::UNIT_Y, false);
    auto clamp = [](double x) { return std::abs(x) < 1e-12 ? 1e-12 : x; };
    d.normal = normal;
    d.insertion = position;
    d.rotation = rotation;
    d.scale = {clamp(scales.x), clamp(scales.y), clamp(scales.z)};
}
} // namespace
void DxfText::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate_matrix(t);
    auto e = *this;
    auto d = definition();
    translate_text(d, offset);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfText::apply_transform(const Transform &t) {
    validate_matrix(t);
    auto e = *this;
    auto d = definition();
    transform_text(d, t);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfText::apply_mirror(const Transform &t, const MirrorOptions &o) {
    if (!finite_matrix(t) || t.is_identity())
        return;
    auto e = *this;
    auto d = definition();
    mirror_text(d, t, o);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfMText::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate_matrix(t);
    auto e = *this;
    auto d = definition();
    d.insertion = d.insertion + offset;
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfMText::apply_transform(const Transform &t) {
    validate_matrix(t);
    auto e = *this;
    auto d = definition();
    transform_mtext(d, t);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfMText::apply_mirror(const Transform &t, const MirrorOptions &o) {
    if (!finite_matrix(t) || t.is_identity())
        return;
    auto e = *this;
    auto d = definition();
    mirror_mtext(d, t, o);
    e.set_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfAttribute::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate_matrix(t);
    auto e = *this;
    auto d = attribute_definition();
    translate_attribute(d, offset);
    e.set_attribute_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfAttribute::apply_transform(const Transform &t) {
    validate_matrix(t);
    auto e = *this;
    auto d = attribute_definition();
    transform_attribute(d, t);
    e.set_attribute_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfAttribute::apply_mirror(const Transform &t, const MirrorOptions &o) {
    if (!finite_matrix(t) || t.is_identity())
        return;
    auto e = *this;
    auto d = attribute_definition();
    mirror_attribute(d, t, o);
    e.set_attribute_definition(d);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfInsert::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate_matrix(t);
    auto e = *this;
    auto d = definition();
    auto axis = Matrix3::arbitrary_axis(d.normal);
    d.insertion = axis.transpose() * (axis * d.insertion + offset);
    e.set_definition(d);
    for (std::size_t i = 0; i < attribute_count(); ++i) {
        auto a = attribute(i);
        auto v = a.attribute_definition();
        translate_attribute(v, offset);
        a.set_attribute_definition(v);
        e.set_attribute(i, a);
    }
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfInsert::apply_transform(const Transform &t) {
    validate_matrix(t);
    auto e = *this;
    auto d = definition();
    transform_insert(d, t);
    e.set_definition(d);
    for (std::size_t i = 0; i < attribute_count(); ++i) {
        auto a = attribute(i);
        auto v = a.attribute_definition();
        transform_attribute(v, t);
        a.set_attribute_definition(v);
        e.set_attribute(i, a);
    }
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfInsert::apply_mirror(const Transform &t, const MirrorOptions &o) {
    if (!finite_matrix(t) || t.is_identity())
        return;
    auto e = *this;
    auto d = definition();
    auto old = Matrix3::arbitrary_axis(d.normal) * Matrix3::rotation_z(d.rotation);
    auto col = t.apply_rotation(old * Vector3{d.column_spacing, 0, 0}),
         row = t.apply_rotation(old * Vector3{0, d.row_spacing, 0});
    transform_insert(d, t);
    if (reflected(t)) {
        d.rotation = wrap(d.rotation + pi);
        d.scale.x = -d.scale.x;
        d.scale.y = -d.scale.y;
    }
    auto frame = (Matrix3::arbitrary_axis(d.normal) * Matrix3::rotation_z(d.rotation)).transpose();
    d.column_spacing = (frame * col).x;
    d.row_spacing = (frame * row).y;
    e.set_definition(d);
    for (std::size_t i = 0; i < attribute_count(); ++i) {
        auto a = attribute(i);
        auto v = a.attribute_definition();
        mirror_attribute(v, t, o);
        a.set_attribute_definition(v);
        e.set_attribute(i, a);
    }
    e.transform_extended_data(t);
    *this = std::move(e);
}
Transform DxfInsert::get_transform() const {
    auto d = definition();
    return Transform::from_matrix(Matrix4::from_matrix3(Matrix3::arbitrary_axis(d.normal)) *
                                  Matrix4::translation(d.insertion.x, d.insertion.y, d.insertion.z) *
                                  Matrix4::rotation_z(d.rotation) *
                                  Matrix4::scaling(d.scale.x, d.scale.y, d.scale.z));
}
std::vector<Point3> DxfInsert::array_points() const {
    auto d = definition();
    std::vector<Point3> p;
    if (std::size_t(d.rows) * d.columns > 1000000)
        throw std::length_error("INSERT array exceeds 1,000,000 cells");
    for (std::size_t r = 0; r < d.rows; ++r)
        for (std::size_t c = 0; c < d.columns; ++c) {
            auto x = c * d.column_spacing, y = r * d.row_spacing;
            p.push_back(d.insertion + Vector3{x * std::cos(d.rotation) - y * std::sin(d.rotation),
                                              x * std::sin(d.rotation) + y * std::cos(d.rotation), 0});
        }
    return p;
}
} // namespace libopencad
