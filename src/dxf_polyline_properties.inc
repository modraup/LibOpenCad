// SPDX-License-Identifier: MPL-2.0
std::int16_t DxfPolyline2D::flags() const { return short_field(heavy_polyline, 70, 0); }
void DxfPolyline2D::set_flags(std::int16_t v) {
    if (v & (8 | 16 | 64))
        throw std::invalid_argument("2D POLYLINE cannot change kind");
    set_short_field(heavy_polyline, 70, v);
}
void DxfPolyline2D::set_closed(bool v) { set_flags(v ? flags() | 1 : flags() & ~1); }
double DxfPolyline2D::elevation() const { return number(heavy_polyline, 30, 0); }
double DxfPolyline2D::thickness() const { return number(heavy_polyline, 39, 0); }
Point3 DxfPolyline2D::normal() const { return point_or(heavy_polyline, 210, {0, 0, 1}); }
std::int16_t DxfPolyline2D::smooth_type() const { return short_field(heavy_polyline, 75, 0); }
void DxfPolyline2D::set_elevation(double v) { set_point(heavy_polyline, 10, {0, 0, v}); }
void DxfPolyline2D::set_thickness(double v) { set_number(heavy_polyline, 39, v); }
void DxfPolyline2D::set_normal(Point3 v) { set_point(heavy_polyline, 210, v); }
void DxfPolyline2D::set_smooth_type(std::int16_t v) { set_short_field(heavy_polyline, 75, v); }
void DxfPolyline2D::set_default_widths(double start, double end) {
    finite(start);
    finite(end);
    auto staged = *this;
    staged.set_number(heavy_polyline, 40, start);
    staged.set_number(heavy_polyline, 41, end);
    *this = std::move(staged);
}
void DxfPolyline2D::set_vertex_metadata(std::size_t index, std::int16_t flags, double tangent,
                                        std::int32_t id) {
    auto v = children_.at(index);
    finite(tangent);
    v.set_short_field(heavy_vertex, 70, flags);
    v.set_number(heavy_vertex, 50, tangent / radians_per_degree);
    auto text = std::to_string(id);
    v.set_field(heavy_vertex, 91, binary_ ? detail::binary_value_from_text(91, text) : text);
    children_[index] = std::move(v);
}
void DxfPolyline3D::set_closed(bool value) {
    auto flags = short_field(polyline3d, 70, 8);
    set_short_field(polyline3d, 70, value ? flags | 1 : flags & ~1);
}
void DxfPolyline3D::set_vertex_flags(std::size_t index, std::int16_t value) {
    children_.at(index).set_short_field(vertex3d, 70, value);
}
void DxfLwPolyline::set_plinegen(bool v) {
    auto flags = short_field(polyline_subclass, 70, 0);
    set_short_field(polyline_subclass, 70, v ? flags | 128 : flags & ~128);
}
void DxfLwPolyline::set_thickness(double v) { set_number(polyline_subclass, 39, v); }
void DxfLwPolyline::set_constant_width(double v) { set_number(polyline_subclass, 43, v); }
void DxfLwPolyline::set_normal(Point3 v) { set_point(polyline_subclass, 210, v); }
void DxfLwPolyline::set_vertex_widths(std::size_t index, double start, double end) {
    auto staged = *this;
    auto values = staged.vertex_fields().at(index);
    auto a = encode_number(start), b = encode_number(end);
    if (values.start_width != missing)
        staged.pairs_[values.start_width].value = a;
    else
        staged.pairs_.insert(staged.pairs_.begin() + values.y + 1, {40, a});
    values = staged.vertex_fields().at(index);
    if (values.end_width != missing)
        staged.pairs_[values.end_width].value = b;
    else
        staged.pairs_.insert(staged.pairs_.begin() + values.y + 1, {41, b});
    *this = std::move(staged);
}
void DxfLwPolyline::set_vertex_id(std::size_t index, std::int32_t id) {
    auto v = vertex_fields().at(index);
    auto text = std::to_string(id);
    auto value = binary_ ? detail::binary_value_from_text(91, text) : text;
    if (v.id != missing)
        pairs_[v.id].value = value;
    else
        pairs_.insert(pairs_.begin() + v.y + 1, {91, value});
}
void DxfLwPolyline::set_vertices(const std::vector<LwVertex> &vertices) {
    if (vertices.size() > INT32_MAX)
        throw std::invalid_argument("Too many polyline vertices");
    MLFields f;
    f.put_i(90, vertices.size());
    for (const auto &v : vertices) {
        f.put_r(10, v.x);
        f.put_r(20, v.y);
        f.put_r(40, v.start_width);
        f.put_r(41, v.end_width);
        f.put_r(42, v.bulge);
        f.put_i(91, v.vertex_id);
    }
    std::vector<Pair> values;
    for (const auto &p : f.values)
        values.push_back({p.code, binary_ ? detail::binary_value_from_text(p.code, p.value) : p.value});
    auto staged = *this;
    std::vector<std::size_t> positions;
    auto end = staged
                   .locate_if(polyline_subclass,
                              [&](std::size_t at, int code) {
                                  if (code == 90 || code == 10 || code == 20 || code == 40 || code == 41 ||
                                      code == 42 || code == 91)
                                      positions.push_back(at);
                                  return false;
                              })
                   .index;
    if (end == std::string_view::npos)
        throw std::invalid_argument("Missing polyline subclass");
    auto at = positions.empty() ? end : positions.front();
    for (auto i = positions.rbegin(); i != positions.rend(); ++i)
        staged.pairs_.erase(staged.pairs_.begin() + *i);
    staged.pairs_.insert(staged.pairs_.begin() + at, values.begin(), values.end());
    *this = std::move(staged);
}
std::string_view DxfVertexSequence::polyline_scope() const {
    if (type() != "POLYLINE")
        throw std::invalid_argument("Expected POLYLINE sequence");
    for (const auto &p : pairs_)
        if (p.code == 100 && (p.value == heavy_polyline || p.value == polyline3d || p.value == polygon_mesh ||
                              p.value == polyface_mesh))
            return p.value;
    auto flags = short_field("", 70, 0);
    return flags & 64 ? polyface_mesh : flags & 16 ? polygon_mesh : flags & 8 ? polyline3d : heavy_polyline;
}
PolylineHeader DxfVertexSequence::polyline_header() const {
    auto s = polyline_scope();
    PolylineHeader v;
    v.flags = short_field(s, 70, 0);
    v.m_count = short_field(s, 71, 0);
    v.n_count = short_field(s, 72, 0);
    v.m_density = short_field(s, 73, 0);
    v.n_density = short_field(s, 74, 0);
    v.smooth_type = short_field(s, 75, 0);
    v.elevation = number(s, 30, 0);
    v.thickness = number(s, 39, 0);
    v.start_width = number(s, 40, 0);
    v.end_width = number(s, 41, 0);
    v.normal = point_or(s, 210, {0, 0, 1});
    return v;
}
void DxfVertexSequence::set_polyline_header(const PolylineHeader &v) {
    auto scope = std::string(polyline_scope());
    auto before = polyline_header();
    auto kind = [](int flags) { return flags & 64 ? 64 : flags & 16 ? 16 : flags & 8 ? 8 : 0; };
    if (kind(v.flags) != kind(before.flags))
        throw std::invalid_argument("POLYLINE class cannot change");
    if (kind(v.flags) == 16 &&
        (v.m_count < 0 || v.n_count < 0 || std::size_t(v.m_count) * v.n_count != children_.size()))
        throw std::invalid_argument("Mesh dimensions must match vertex count");
    if (kind(v.flags) == 64 && (v.m_count != before.m_count || v.n_count != before.n_count))
        throw std::invalid_argument("Polyface counts follow stored vertices and faces");
    auto staged = *this;
    staged.set_short_field(scope, 70, v.flags);
    staged.set_short_field(scope, 71, v.m_count);
    staged.set_short_field(scope, 72, v.n_count);
    staged.set_short_field(scope, 73, v.m_density);
    staged.set_short_field(scope, 74, v.n_density);
    staged.set_short_field(scope, 75, v.smooth_type);
    staged.set_point(scope, 10, {0, 0, v.elevation});
    staged.set_number(scope, 39, v.thickness);
    staged.set_number(scope, 40, v.start_width);
    staged.set_number(scope, 41, v.end_width);
    staged.set_point(scope, 210, v.normal);
    pairs_ = std::move(staged.pairs_);
}
void DxfPolyfaceMesh::set_vertex_position(std::size_t index, Point3 value) {
    set_spatial_vertex(coordinate_records_.at(index), polyface_vertex, value);
}
void DxfPolyfaceMesh::set_vertex_metadata(std::size_t index, const PolylineVertex3D &value) {
    auto &target = children_.at(coordinate_records_.at(index));
    auto staged = target;
    if ((value.flags & 192) != 192)
        throw std::invalid_argument("Polyface coordinate vertex flags must include 64 and 128");
    staged.set_short_field(polyface_vertex, 70, value.flags);
    staged.set_number(polyface_vertex, 42, value.bulge);
    staged.set_number(polyface_vertex, 40, value.start_width);
    staged.set_number(polyface_vertex, 41, value.end_width);
    staged.set_number(polyface_vertex, 50, value.curve_tangent / radians_per_degree);
    auto text = std::to_string(value.id);
    staged.set_field(polyface_vertex, 91, binary_ ? detail::binary_value_from_text(91, text) : text);
    target = std::move(staged);
}
void DxfPolyfaceMesh::set_face_color(std::size_t index, Color value) {
    auto &target = children_.at(face_records_.at(index));
    DxfEntity staged;
    static_cast<DxfRecord &>(staged) = target;
    staged.set_color(value);
    target = std::move(staged);
}
