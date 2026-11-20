// SPDX-License-Identifier: MPL-2.0
// acadrust entities/modification/explode/{mod,primitives,meshes}.rs.
// Included inside namespace libopencad in dxf_document.cpp.
std::vector<ExplodedPrimitive> DxfDocument::explode_entity(std::string_view handle) const {
    const auto identity = handle_value(handle);
    const auto found = handle_index_.find(identity);
    if (!identity || found == handle_index_.end() || found->second.size() != 1)
        throw std::invalid_argument("Explode requires a unique existing entity handle");
    const auto location = found->second.front();
    if (location.vertex || location.seqend ||
        std::find(entity_records_.begin(), entity_records_.end(), location.record) == entity_records_.end())
        throw std::invalid_argument("Explode requires a top-level entity");
    std::vector<ExplodedPrimitive> parts;
    auto common = [&](const DxfRecord &source, std::string type, std::string subclass) {
        DxfEntity properties;
        static_cast<DxfRecord &>(properties) = source;
        auto output = make_entity(std::move(type), std::move(subclass), {});
        output.set_layer(properties.layer());
        output.set_linetype(properties.linetype());
        output.set_color(properties.color());
        output.set_line_weight(properties.line_weight());
        output.set_invisible(properties.is_invisible());
        output.set_display_properties(properties.display_properties());
        if (!source.owner_handle().empty())
            output.set_field("", 330, source.owner_handle());
        output.set_short_field("AcDbEntity", 67, source.short_field("AcDbEntity", 67, 0));
        return output;
    };
    auto line = [&](const DxfRecord &source, Point3 a, Point3 b, double thickness, Vector3 normal) {
        DxfLine e;
        static_cast<DxfEntity &>(e) = common(source, "LINE", "AcDbLine");
        e.set_start(a);
        e.set_end(b);
        e.set_thickness(thickness);
        e.set_normal(normal);
        parts.emplace_back(std::move(e));
    };
    auto point = [&](const DxfRecord &source, Point3 p, double thickness, Vector3 normal) {
        DxfPoint e;
        static_cast<DxfEntity &>(e) = common(source, "POINT", "AcDbPoint");
        e.set_location(p);
        e.set_thickness(thickness);
        e.set_normal(normal);
        parts.emplace_back(std::move(e));
    };
    auto face = [&](const DxfRecord &source, const std::vector<Point3> &points, std::uint8_t hidden) {
        DxfFace3D e;
        static_cast<DxfEntity &>(e) = common(source, "3DFACE", "AcDbFace");
        for (std::size_t i = 0; i < 4; ++i)
            e.set_corner(i, points[std::min(i, points.size() - 1)]);
        e.set_invisible_edges(hidden);
        parts.emplace_back(std::move(e));
    };
    auto segment = [&](const DxfRecord &source, Point3 a, Point3 b, double bulge, double thickness,
                       Vector3 normal) {
        if (std::abs(bulge) < 1e-10) {
            auto o = Matrix3::arbitrary_axis(normal);
            line(source, o * a, o * b, thickness, normal);
            return;
        }
        auto dx = b.x - a.x, dy = b.y - a.y, d = std::sqrt(dx * dx + dy * dy);
        if (d < 1e-10) {
            point(source, a, thickness, normal);
            return;
        }
        auto radius = d * (1 + bulge * bulge) / (4 * std::abs(bulge));
        auto offset = d * (1 - bulge * bulge) / (4 * bulge);
        Point3 c{(a.x + b.x) / 2 - dy / d * offset, (a.y + b.y) / 2 + dx / d * offset, a.z};
        auto sa = std::atan2(a.y - c.y, a.x - c.x), ea = std::atan2(b.y - c.y, b.x - c.x);
        if (bulge < 0)
            std::swap(sa, ea);
        auto normalize = [](double a) {
            constexpr double tau = 6.2831853071795864769;
            auto r = std::fmod(a, tau);
            return r < 0 ? r + tau : r;
        };
        DxfArc e;
        static_cast<DxfEntity &>(e) = common(source, "ARC", "AcDbCircle");
        if (version_ != DxfVersion::AC1009)
            e.pairs_.push_back({100, "AcDbArc"});
        e.set_center(c);
        e.set_radius(std::abs(radius));
        e.set_start_angle_radians(normalize(sa));
        e.set_end_angle_radians(normalize(ea));
        e.set_thickness(thickness);
        e.set_normal(normal);
        parts.emplace_back(std::move(e));
    };
    std::visit(
        [&](const auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, DxfLwPolyline>) {
                auto vs = e.vertices();
                auto n = vs.size();
                if (n < 2)
                    return;
                auto count = e.is_closed() ? n : n - 1;
                for (std::size_t i = 0; i < count; ++i) {
                    auto a = vs[i], b = vs[(i + 1) % n];
                    segment(e, {a.x, a.y, e.elevation()}, {b.x, b.y, e.elevation()}, a.bulge, e.thickness(),
                            e.normal());
                }
            } else if constexpr (std::is_same_v<T, DxfPolyline2D>) {
                auto n = e.vertex_count();
                if (n < 2)
                    return;
                auto count = e.is_closed() ? n : n - 1;
                for (std::size_t i = 0; i < count; ++i) {
                    auto a = e.vertex(i), b = e.vertex((i + 1) % n);
                    if (e.elevation() != 0) {
                        a.location.z = e.elevation();
                        b.location.z = e.elevation();
                    }
                    segment(e, a.location, b.location, a.bulge, e.thickness(), e.normal());
                }
            } else if constexpr (std::is_same_v<T, DxfPolyline3D>) {
                auto n = e.vertex_count();
                if (n < 2)
                    return;
                auto count = e.is_closed() ? n : n - 1;
                for (std::size_t i = 0; i < count; ++i)
                    line(e, e.vertex(i).position, e.vertex((i + 1) % n).position, 0, Vector3::UNIT_Z);
            } else if constexpr (std::is_same_v<T, DxfPolygonMesh>) {
                int m = e.m_vertex_count(), n = e.n_vertex_count();
                if (m < 2 || n < 2 || std::size_t(m) > e.vertex_count() / std::size_t(n))
                    return;
                auto h = e.polyline_header();
                int mc = (h.flags & 1) ? m : m - 1, nc = (h.flags & 32) ? n : n - 1;
                auto get = [&](int i, int j) { return e.vertex(std::size_t(i % m) * n + j % n).position; };
                for (int i = 0; i < mc; ++i)
                    for (int j = 0; j < nc; ++j)
                        face(e, {get(i, j), get(i, j + 1), get(i + 1, j + 1), get(i + 1, j)}, 0);
            } else if constexpr (std::is_same_v<T, DxfMesh>) {
                auto mesh = e.definition();
                for (const auto &f : mesh.faces) {
                    if (f.vertices.size() < 3 || f.vertices.size() > 4)
                        continue;
                    std::vector<Point3> points;
                    for (auto i : f.vertices) {
                        if (i >= mesh.vertices.size())
                            break;
                        points.push_back(mesh.vertices[i]);
                    }
                    if (points.size() == f.vertices.size())
                        face(e, points, 0);
                }
            } else if constexpr (std::is_same_v<T, DxfPolyfaceMesh>) {
                for (std::size_t i = 0; i < e.face_count(); ++i) {
                    const auto f = e.face(i);
                    std::vector<Point3> points;
                    bool valid = true;
                    for (int j = 0; j < (f.is_triangle() ? 3 : 4); ++j) {
                        auto index = std::abs(f.indices[j]);
                        if (!index)
                            continue;
                        if (std::size_t(index) > e.vertex_count()) {
                            valid = false;
                            break;
                        }
                        points.push_back(e.vertex(index - 1).position);
                    }
                    if (!valid || points.empty())
                        continue;
                    // Face records have independent common properties in upstream's native EXPLODE.
                    const auto *source = &e.children_[e.face_records_[i]];
                    if (points.size() == 1)
                        point(*source, points[0], 0, Vector3::UNIT_Z);
                    else if (points.size() == 2)
                        line(*source, points[0], points[1], 0, Vector3::UNIT_Z);
                    else {
                        std::uint8_t hidden = 0;
                        for (std::size_t j = 0; j < points.size(); ++j)
                            if (f.indices[j] < 0)
                                hidden |= 1u << j;
                        face(*source, points, hidden);
                    }
                }
            } else if constexpr (std::is_same_v<T, DxfLine> || std::is_same_v<T, DxfCircle> ||
                                 std::is_same_v<T, DxfArc> || std::is_same_v<T, DxfEllipse> ||
                                 std::is_same_v<T, DxfPoint> || std::is_same_v<T, DxfRay> ||
                                 std::is_same_v<T, DxfXLine> || std::is_same_v<T, DxfSolid> ||
                                 std::is_same_v<T, DxfFace3D> || std::is_same_v<T, DxfSpline>) {
                return;
            } else
                throw std::invalid_argument("Entity type is not supported by native EXPLODE yet");
        },
        records_[location.record]);
    return parts;
}
std::vector<std::uint64_t> DxfDocument::explode_entity_into(std::string_view handle,
                                                            const EntityOptions &target, bool remove_source) {
    auto parts = explode_entity(handle);
    if (parts.empty())
        return {};
    DxfDocument staged = *this;
    std::vector<std::uint64_t> handles;
    handles.reserve(parts.size());
    for (auto &part : parts)
        std::visit([&](auto &e) { handles.push_back(staged.insert_entity(Record{std::move(e)}, target)); },
                   part);
    if (remove_source && !staged.remove_entity(handle))
        throw std::invalid_argument("Explode source disappeared");
    *this = std::move(staged);
    return handles;
}
