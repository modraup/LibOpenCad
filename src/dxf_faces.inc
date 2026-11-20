// SPDX-License-Identifier: MPL-2.0
// acadrust entities/{solid,face3d}.rs and DXF section_reader/section_writer.
// Included inside namespace libopencad in dxf_document.cpp.
namespace {
int corner_code(std::size_t index) {
    if (index >= 4)
        throw std::out_of_range("DXF corner/edge index must be in 0..3");
    return 10 + static_cast<int>(index);
}
void validate_corners(const std::vector<Point3> &corners) {
    if (corners.size() != 3 && corners.size() != 4)
        throw std::invalid_argument("Expected three or four corners");
    for (const auto &p : corners) {
        finite(p.x);
        finite(p.y);
        finite(p.z);
    }
}
} // namespace
Point3 DxfSolid::corner(std::size_t index) const {
    return point_or("AcDbTrace", corner_code(index), index == 3 ? corner(2) : Point3{});
}
void DxfSolid::set_corner(std::size_t index, Point3 value) {
    set_point("AcDbTrace", corner_code(index), value);
}
bool DxfSolid::is_triangle() const { return (corner(2) - corner(3)).length() < 1e-10; }
std::vector<Point3> DxfSolid::corners() const {
    std::vector<Point3> result{corner(0), corner(1), corner(2)};
    if (!is_triangle())
        result.push_back(corner(3));
    return result;
}
std::vector<Point3> DxfSolid::boundary_corners() const {
    if (is_triangle())
        return corners();
    return {corner(0), corner(1), corner(3), corner(2)};
}
Point3 DxfSolid::normal() const { return point_or("AcDbTrace", 210, {0, 0, 1}); }
void DxfSolid::set_normal(Point3 value) { set_point("AcDbTrace", 210, value); }
double DxfSolid::thickness() const { return number("AcDbTrace", 39, 0); }
void DxfSolid::set_thickness(double value) { set_number("AcDbTrace", 39, value); }
Point3 DxfFace3D::corner(std::size_t index) const {
    return point_or("AcDbFace", corner_code(index), index == 3 ? corner(2) : Point3{});
}
void DxfFace3D::set_corner(std::size_t index, Point3 value) {
    set_point("AcDbFace", corner_code(index), value);
}
bool DxfFace3D::is_triangle() const {
    for (std::size_t i = 0; i < 4; ++i)
        if ((corner(i) - corner((i + 1) % 4)).length() < 1e-10)
            return true;
    return false;
}
std::vector<Point3> DxfFace3D::corners() const {
    std::vector<Point3> result;
    for (std::size_t i = 0; i < 4; ++i)
        if (result.empty() || (corner(i) - result.back()).length() >= 1e-10)
            result.push_back(corner(i));
    if (result.size() > 1 && (result.front() - result.back()).length() < 1e-10)
        result.pop_back();
    return result;
}
double DxfFace3D::area() const {
    const auto points = corners();
    double result = 0;
    for (std::size_t i = 1; i + 1 < points.size(); ++i)
        result += (points[i] - points[0]).cross(points[i + 1] - points[0]).length() * 0.5;
    return result;
}
std::uint8_t DxfFace3D::invisible_edges() const {
    return static_cast<std::uint8_t>(short_field("AcDbFace", 70, 0) & 15);
}
void DxfFace3D::set_invisible_edges(std::uint8_t flags) {
    if (flags > 15)
        throw std::invalid_argument("Invalid 3DFACE invisible-edge flags");
    set_short_field("AcDbFace", 70, (short_field("AcDbFace", 70, 0) & ~15) | flags);
}
bool DxfFace3D::is_edge_invisible(std::size_t index) const {
    corner_code(index);
    return (invisible_edges() & (1u << index)) != 0;
}
void DxfFace3D::set_edge_invisible(std::size_t index, bool invisible) {
    corner_code(index);
    const auto mask = 1u << index;
    set_invisible_edges(
        static_cast<std::uint8_t>(invisible ? (invisible_edges() | mask) : (invisible_edges() & ~mask)));
}
std::uint64_t DxfDocument::insert_solid(const std::vector<Point3> &corners, bool trace,
                                        const EntityOptions &options) {
    validate_corners(corners);
    DxfSolid entity;
    static_cast<DxfEntity &>(entity) = make_entity(trace ? "TRACE" : "SOLID", "AcDbTrace", options);
    for (std::size_t i = 0; i < 4; ++i)
        entity.set_corner(i, corners[std::min(i, corners.size() - 1)]);
    return insert_entity(std::move(entity), options);
}
std::uint64_t DxfDocument::add_solid(const std::vector<Point3> &corners, const EntityOptions &options) {
    return insert_solid(corners, false, options);
}
std::uint64_t DxfDocument::add_trace(const std::vector<Point3> &corners, const EntityOptions &options) {
    return insert_solid(corners, true, options);
}
std::uint64_t DxfDocument::add_face3d(const std::vector<Point3> &corners, std::uint8_t flags,
                                      const EntityOptions &options) {
    validate_corners(corners);
    DxfFace3D entity;
    static_cast<DxfEntity &>(entity) = make_entity("3DFACE", "AcDbFace", options);
    for (std::size_t i = 0; i < 4; ++i)
        entity.set_corner(i, corners[std::min(i, corners.size() - 1)]);
    entity.set_invisible_edges(flags);
    return insert_entity(std::move(entity), options);
}
