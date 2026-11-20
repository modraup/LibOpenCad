// SPDX-License-Identifier: MPL-2.0
// acadrust EntityType dispatch and typed extended entity geometry.
#include <libopencad/dxf_document.hpp>
#include <libopencad/extended_geometry.hpp>
#include <cmath>
#include <stdexcept>
namespace libopencad {
namespace {
void validate_light_encoding(const ExtendedEntityDefinition &value) {
    if (const auto *light = std::get_if<LightDefinition>(&value); light && light->photometric) {
        const double z = light->photometric->web_rotation.z;
        if (!std::isfinite(z) || z != std::trunc(z) || z < -32768 || z > 32767)
            throw std::invalid_argument("LIGHT photometric Z is not representable by integer DXF group 63");
    }
}
bool finite(const Transform &t) {
    for (const auto &row : t.matrix.m)
        for (double v : row)
            if (!std::isfinite(v))
                return false;
    return true;
}
} // namespace
BoundingBox3D DxfExtendedEntity::bounding_box() const { return extended_bounding_box(definition()); }
void DxfExtendedEntity::translate(Vector3 offset) {
    if (type() == "SECTIONLINE" || type() == "DRAWINGVIEW") {
        if (!finite(Transform::from_translation(offset)))
            throw std::invalid_argument("Nonfinite translation");
        return; // EntityType dispatch preserves drawing-view anchors and XDATA.
    }
    if (type() != "OLE2FRAME") {
        auto value = translate_extended_definition(definition(), offset);
        validate_light_encoding(value);
        auto staged = *this;
        if (type() != "CAMERA" && type() != "OLEFRAME" && type() != "LAYOUTPRINTCONFIG" &&
            type() != "Format" && type() != "COORDINATION_MODEL")
            staged.set_definition(value);
        staged.transform_extended_data(Transform::from_translation(offset));
        *this = std::move(staged);
        return;
    }
    auto t = Transform::from_translation(offset);
    if (!finite(t))
        throw std::invalid_argument("Nonfinite OLE2FRAME translation");
    auto staged = *this;
    staged.set_point("AcDbOle2Frame", 10,
                     Point3{number("AcDbOle2Frame", 10, 1), number("AcDbOle2Frame", 20, 1),
                            number("AcDbOle2Frame", 30, 0)} +
                         offset);
    staged.set_point("AcDbOle2Frame", 11,
                     Point3{number("AcDbOle2Frame", 11, 0), number("AcDbOle2Frame", 21, 0),
                            number("AcDbOle2Frame", 31, 0)} +
                         offset);
    staged.transform_extended_data(t);
    *this = std::move(staged);
}
void DxfExtendedEntity::apply_transform(const Transform &t) {
    if (type() != "OLE2FRAME") {
        auto value = transform_extended_definition(definition(), t);
        validate_light_encoding(value);
        auto staged = *this;
        if (type() != "CAMERA" && type() != "OLEFRAME" && type() != "LAYOUTPRINTCONFIG" && type() != "Format")
            staged.set_definition(value);
        staged.transform_extended_data(t);
        *this = std::move(staged);
        return;
    }
    if (!finite(t))
        throw std::invalid_argument("Nonfinite OLE2FRAME transform");
    auto staged = *this;
    staged.set_point("AcDbOle2Frame", 10,
                     t.apply(Point3{number("AcDbOle2Frame", 10, 1), number("AcDbOle2Frame", 20, 1),
                                    number("AcDbOle2Frame", 30, 0)}));
    staged.set_point("AcDbOle2Frame", 11,
                     t.apply(Point3{number("AcDbOle2Frame", 11, 0), number("AcDbOle2Frame", 21, 0),
                                    number("AcDbOle2Frame", 31, 0)}));
    staged.transform_extended_data(t);
    *this = std::move(staged);
}
void DxfExtendedEntity::apply_mirror(const Transform &t) {
    if (!finite(t) || t.is_identity())
        return;
    apply_transform(t);
}
} // namespace libopencad
