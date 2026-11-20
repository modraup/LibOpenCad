// SPDX-License-Identifier: MPL-2.0
// Port of acadrust entities/modification/{translate,transform}.rs and mirror fallback dispatch.
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
        throw std::invalid_argument("Nonfinite VIEWPORT transform");
}
} // namespace
void DxfViewport::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    e.set_point("AcDbViewport", 10, definition().center + offset);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfViewport::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto d = definition();
    auto scale = t.apply_rotation(Vector3::UNIT_X).length();
    e.set_point("AcDbViewport", 10, t.apply(d.center));
    e.set_number("AcDbViewport", 40, d.width * scale);
    e.set_number("AcDbViewport", 41, d.height * scale);
    e.set_point("AcDbViewport", 16, t.apply_rotation(d.view_direction).normalize());
    e.set_point("AcDbViewport", 111, t.apply_rotation(d.ucs_x_axis).normalize());
    e.set_point("AcDbViewport", 112, t.apply_rotation(d.ucs_y_axis).normalize());
    e.set_point("AcDbViewport", 110, t.apply(d.ucs_origin));
    e.set_point("AcDbViewport", 17, t.apply(d.view_target));
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfViewport::apply_mirror(const Transform &t) {
    if (!finite(t) || t.is_identity())
        return;
    apply_transform(t);
}
} // namespace libopencad
