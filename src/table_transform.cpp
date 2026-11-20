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
        throw std::invalid_argument("Nonfinite ACAD_TABLE transform");
}
} // namespace
void DxfTable::translate(Vector3 offset) {
    auto t = Transform::from_translation(offset);
    validate(t);
    auto e = *this;
    e.set_point("AcDbBlockReference", 10, definition().insertion + offset);
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfTable::apply_transform(const Transform &t) {
    validate(t);
    auto e = *this;
    auto d = definition();
    e.set_point("AcDbBlockReference", 10, t.apply(d.insertion));
    e.set_point("AcDbBlockReference", 210, t.apply_rotation(d.normal).normalize());
    e.transform_table_fields(t.apply_rotation(d.horizontal_direction).normalize(),
                             t.apply_rotation(Vector3::UNIT_X).length());
    e.transform_extended_data(t);
    *this = std::move(e);
}
void DxfTable::apply_mirror(const Transform &t) {
    if (!finite(t) || t.is_identity())
        return;
    apply_transform(t);
}
} // namespace libopencad
