// SPDX-License-Identifier: MPL-2.0
std::uint64_t DwgDocument::ensure_civil() {
    *this = DwgDocument(detail::ensure_dwg_civil(source_bytes(), objects_));
    for (auto &[h, obj] : objects_.arch_objects)
        if (obj.dxf_name == "AECC_TREE_NODE")
            return h;
    throw std::invalid_argument("civil root was not created");
}
std::vector<CogoPoint> DwgDocument::cogo_points() const {
    for (auto &[h, obj] : objects_.arch_objects) {
        (void)h;
        if (obj.dxf_name == "AECC_POINT_NODE")
            return libopencad::cogo_points(obj);
    }
    return {};
}
std::uint64_t DwgDocument::add_cogo_point(std::optional<std::uint32_t> number, Vector3 position,
                                         std::optional<std::string> description, std::optional<std::uint64_t> style,
                                         std::optional<std::string> layer) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_cogo_point(source_bytes(), objects_, number, position, description, style, layer, id));
    return id;
}
void DwgDocument::set_cogo_point(std::uint64_t point, std::uint32_t number, Vector3 position,
                                std::optional<std::string> description) {
    *this = DwgDocument(detail::set_dwg_cogo_point(source_bytes(), objects_, point, number, position, description));
}
void DwgDocument::remove_cogo_point(std::uint64_t point) {
    *this = DwgDocument(detail::remove_dwg_cogo_point(source_bytes(), objects_, point));
}
std::uint64_t DwgDocument::add_point_group(std::string_view name, std::optional<std::string> description,
                                          const std::vector<std::uint32_t> &numbers) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_point_group(source_bytes(), objects_, name, description, numbers, id));
    return id;
}
void DwgDocument::set_point_group(std::uint64_t group, std::string_view name, std::optional<std::string> description,
                                 const std::vector<std::uint32_t> &numbers) {
    *this = DwgDocument(detail::set_dwg_point_group(source_bytes(), objects_, group, name, description, numbers));
}
std::uint64_t DwgDocument::add_point_group_query(std::string_view name, std::optional<std::string> description,
                                                const PointQuery &query) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_point_group_query(source_bytes(), objects_, name, description, query, id));
    return id;
}
void DwgDocument::set_point_group_query(std::uint64_t group, std::string_view name,
                                       std::optional<std::string> description, const PointQuery &query) {
    *this = DwgDocument(detail::set_dwg_point_group_query(source_bytes(), objects_, group, name, description, query));
}
std::vector<std::uint32_t> DwgDocument::point_group_numbers(std::uint64_t group) const {
    auto obj = architecture_object(group);
    return obj ? libopencad::point_group_numbers(*obj) : std::vector<std::uint32_t>{};
}
std::optional<std::uint32_t> DwgDocument::cogo_point_number(std::uint64_t point) const {
    auto entity = architecture_entity(point);
    return entity ? libopencad::cogo_point_number(*entity) : std::nullopt;
}
std::uint64_t DwgDocument::add_alignment(std::string_view name, const std::vector<Vector2> &vertices,
                                        std::optional<std::string> layer) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_alignment(source_bytes(), objects_, name, vertices, layer, id));
    return id;
}
std::uint64_t DwgDocument::add_alignment_with_curves(std::string_view name, const std::vector<Vector2> &vertices,
                                                    const std::vector<VertexCurve> &curves,
                                                    std::optional<std::string> layer) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_alignment_with_curves(source_bytes(), objects_, name, vertices, curves, layer, id));
    return id;
}
std::optional<std::string> DwgDocument::alignment_name(std::uint64_t handle) const {
    auto entity = architecture_entity(handle);
    return entity ? libopencad::alignment_name(*entity) : std::nullopt;
}
std::optional<double> DwgDocument::alignment_start_station(std::uint64_t handle) const {
    auto entity = architecture_entity(handle);
    return entity ? libopencad::alignment_start_station(*entity) : std::nullopt;
}
std::vector<std::vector<Vector2>> DwgDocument::alignment_polylines(std::uint64_t handle) const {
    auto entity = architecture_entity(handle);
    return entity ? libopencad::alignment_polylines(*entity) : std::vector<std::vector<Vector2>>{};
}
std::optional<std::vector<AlignmentElement>> DwgDocument::alignment_elements(std::uint64_t handle) const {
    auto entity = architecture_entity(handle);
    return entity ? libopencad::alignment_elements(*entity) : std::nullopt;
}
std::vector<std::pair<AlignmentPiece, std::vector<Vector2>>> DwgDocument::alignment_pieces(std::uint64_t handle) const {
    auto entity = architecture_entity(handle);
    return entity ? libopencad::alignment_pieces(*entity) : std::vector<std::pair<AlignmentPiece, std::vector<Vector2>>>{};
}
std::uint64_t DwgDocument::add_profile(std::uint64_t alignment, std::string_view name, const std::vector<Vector2> &pvis,
                                      const std::vector<double> &curves, std::optional<std::string> layer) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_profile(source_bytes(), objects_, alignment, name, pvis, curves, layer, id));
    return id;
}
std::uint64_t DwgDocument::add_profile_with_curves(std::uint64_t alignment, std::string_view name,
                                                  const std::vector<Vector2> &pvis,
                                                  const std::vector<VerticalCurve> &curves,
                                                  std::optional<std::string> layer) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_profile_with_curves(source_bytes(), objects_, alignment, name, pvis, curves, layer, id));
    return id;
}
std::optional<std::string> DwgDocument::profile_name(std::uint64_t handle) const {
    auto entity = architecture_entity(handle);
    return entity ? libopencad::profile_name(*entity) : std::nullopt;
}
std::vector<std::pair<ProfilePiece, std::vector<Vector2>>> DwgDocument::profile_pieces(std::uint64_t handle) const {
    auto entity = architecture_entity(handle);
    return entity ? libopencad::profile_pieces(*entity)
                  : std::vector<std::pair<ProfilePiece, std::vector<Vector2>>>{};
}
std::uint64_t DwgDocument::add_tin_surface(std::string_view name, const std::vector<Vector3> &points,
                                          std::optional<std::string> layer) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_tin_surface(source_bytes(), objects_, name, points, layer, id));
    return id;
}
std::optional<TinSurface> DwgDocument::tin_surface(std::uint64_t handle) const {
    auto entity = architecture_entity(handle);
    return entity ? libopencad::tin_surface(*entity) : std::nullopt;
}
