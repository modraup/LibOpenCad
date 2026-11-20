// SPDX-License-Identifier: MPL-2.0
// acadrust entities/modification/explode/modeler.rs.
namespace {
template <class T> std::optional<T> modeler_view(const acis::SatDocument &d, acis::SatPointer p) {
    auto r = d.resolve(p);
    return r ? T::from_record(*r) : std::nullopt;
}
std::optional<Vector3> modeler_vertex(const acis::SatDocument &d, acis::SatPointer p) {
    auto v = modeler_view<acis::SatVertex>(d, p);
    auto point = v ? modeler_view<acis::SatPoint>(d, v->point) : std::nullopt;
    if (!point)
        return {};
    auto q = point->position;
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z))
        return {};
    return q;
}
std::vector<Vector3> modeler_face_points(const acis::SatDocument &d, const acis::SatFace &face) {
    std::vector<Vector3> points;
    auto loop = modeler_view<acis::SatLoop>(d, face.first_loop);
    if (!loop)
        return points;
    auto p = loop->first_coedge;
    std::set<std::int32_t> seen;
    while (!p.is_null() && seen.insert(p.value).second) {
        auto c = modeler_view<acis::SatCoedge>(d, p);
        if (!c)
            break;
        auto e = modeler_view<acis::SatEdge>(d, c->edge);
        if (!e)
            break;
        auto point = modeler_vertex(d, e->start_vertex);
        if (!point)
            break;
        points.push_back(*point);
        p = c->next;
    }
    return points;
}
} // namespace
std::vector<ExplodedModelerPart> DxfDocument::explode_modeler(std::uint64_t handle) const {
    const auto found = handle_index_.find(handle);
    if (!handle || found == handle_index_.end() || found->second.size() != 1)
        throw std::invalid_argument("Modeler explode requires a unique entity handle");
    const auto loc = found->second.front();
    if (loc.vertex || loc.seqend)
        throw std::invalid_argument("Modeler explode requires an entity");
    const auto *source = std::get_if<DxfModelerEntity>(&records_[loc.record]);
    if (!source)
        throw std::invalid_argument("Entity is not a modeler");
    std::vector<ExplodedModelerPart> result;
    const auto kind = source->kind();
    if (kind >= ModelerKind::Surface && kind != ModelerKind::PlaneSurface)
        return result;
    // Rust's face-only solid/body/plane path calls parse_sat(), unlike REGION's
    // parse() path. It deliberately produces no face wires for SAB-only data.
    if (kind != ModelerKind::Region && modeler_payload_record(handle))
        return result;
    auto geometry = modeler_geometry(handle);
    if (!geometry)
        return result;
    const auto &d = *geometry;
    auto common = [&](const char *type, const char *subclass) {
        auto e = make_entity(type, subclass, {});
        e.set_layer(source->layer());
        e.set_color(source->color());
        e.set_linetype(source->linetype());
        e.set_line_weight(source->line_weight());
        e.set_invisible(source->is_invisible());
        e.set_display_properties(source->display_properties());
        if (!source->owner_handle().empty())
            e.set_field("", 330, source->owner_handle());
        e.set_short_field("AcDbEntity", 67, source->short_field("AcDbEntity", 67, 0));
        return e;
    };
    auto line = [&](Vector3 a, Vector3 b) {
        DxfLine e;
        static_cast<DxfEntity &>(e) = common("LINE", "AcDbLine");
        e.set_start(a);
        e.set_end(b);
        result.emplace_back(std::move(e));
    };
    auto spline_entity = [&](const SplineDefinition &data) {
        DxfSpline e;
        static_cast<DxfEntity &>(e) = common("SPLINE", "AcDbSpline");
        if (data.control_points.empty() && !data.fit_points.empty()) {
            e.set_short_field("AcDbSpline", 70, data.flags);
            e.set_short_field("AcDbSpline", 71, data.degree);
            e.set_short_field("AcDbSpline", 72, 0);
            e.set_short_field("AcDbSpline", 73, 0);
            e.set_short_field("AcDbSpline", 74, int(data.fit_points.size()));
            e.set_point("AcDbSpline", 210, data.normal);
            e.set_point("AcDbSpline", 12, data.begin_tangent);
            e.set_point("AcDbSpline", 13, data.end_tangent);
            e.set_number("AcDbSpline", 42, data.knot_tolerance);
            e.set_number("AcDbSpline", 43, data.control_tolerance);
            e.set_number("AcDbSpline", 44, data.fit_tolerance);
            for (auto p : data.fit_points) {
                e.pairs_.push_back({11, e.encode_number(p.x)});
                e.pairs_.push_back({21, e.encode_number(p.y)});
                e.pairs_.push_back({31, e.encode_number(p.z)});
            }
        } else
            e.set_definition(data);
        return e;
    };
    auto edge_entity = [&](const acis::SatCoedge &coedge, const acis::SatEdge &edge) {
        auto start = modeler_vertex(d, edge.start_vertex), end = modeler_vertex(d, edge.end_vertex);
        if (!start || !end)
            return;
        bool reversed = coedge.sense == acis::Sense::Reversed;
        if (reversed)
            std::swap(start, end);
        const auto *record = d.resolve(edge.curve);
        if (!record)
            return;
        if (acis::SatStraightCurve::from_record(*record)) {
            line(*start, *end);
            return;
        }
        if (auto curve = acis::SatEllipseCurve::from_record(*record)) {
            const auto normal = curve->normal.normalize();
            if (std::abs(curve->ratio - 1) <= 1e-9 &&
                std::abs(edge.end_param - edge.start_param) >= 6.2831853071795864769 - 1e-8) {
                DxfCircle e;
                static_cast<DxfEntity &>(e) = common("CIRCLE", "AcDbCircle");
                e.set_center(curve->center);
                e.set_radius(curve->major_axis.length());
                e.set_normal(normal);
                result.emplace_back(std::move(e));
            } else {
                DxfEllipse e;
                static_cast<DxfEntity &>(e) = common("ELLIPSE", "AcDbEllipse");
                e.set_center(curve->center);
                e.set_major_axis(curve->major_axis);
                e.set_minor_axis_ratio(curve->ratio);
                e.set_normal(normal);
                e.set_start_parameter(reversed ? edge.end_param : edge.start_param);
                e.set_end_parameter(reversed ? edge.start_param : edge.end_param);
                result.emplace_back(std::move(e));
            }
            return;
        }
        if (auto curve = acis::SatIntCurve::from_record(*record)) {
            auto c = curve->bspline_in(d);
            if (!c || c->controls.size() < 2 || c->knots.size() < 2)
                return;
            auto from = c->knots[c->degree], to = c->knots[c->knots.size() - c->degree - 1];
            bool against = curve->sense() == acis::Sense::Reversed;
            auto a = against ? -edge.end_param : edge.start_param,
                 b = against ? -edge.start_param : edge.end_param;
            reversed = reversed != against;
            SplineDefinition data;
            if (std::abs(a - from) > 1e-9 || std::abs(b - to) > 1e-9) {
                data.fit_points = curve->sample_range(a, b, 32);
                if (reversed)
                    std::reverse(data.fit_points.begin(), data.fit_points.end());
                if (data.fit_points.size() < 2)
                    return;
            } else {
                if (c->degree > INT16_MAX)
                    throw std::invalid_argument("Spline degree exceeds DXF limit");
                data.degree = std::int16_t(c->degree);
                data.knots = c->knots;
                for (auto p : c->controls) {
                    double w = std::abs(p[3]) > 1e-12 ? p[3] : 1;
                    data.control_points.push_back({p[0] / w, p[1] / w, p[2] / w});
                    data.weights.push_back(p[3]);
                    if (std::abs(p[3] - 1) > 1e-12)
                        data.flags |= 4;
                }
                if (reversed) {
                    std::reverse(data.control_points.begin(), data.control_points.end());
                    std::reverse(data.weights.begin(), data.weights.end());
                    std::reverse(data.knots.begin(), data.knots.end());
                    const auto sum = data.knots.front() + data.knots.back();
                    for (auto &k : data.knots)
                        k = sum - k;
                }
            }
            result.emplace_back(spline_entity(data));
        }
    };
    auto faces = d.records_of_type("face");
    if (kind == ModelerKind::Region) {
        for (auto record : faces) {
            auto face = acis::SatFace::from_record(*record);
            if (!face)
                continue;
            auto lp = face->first_loop;
            std::set<std::int32_t> loops;
            while (!lp.is_null() && loops.insert(lp.value).second) {
                auto l = modeler_view<acis::SatLoop>(d, lp);
                if (!l)
                    break;
                auto cp = l->first_coedge;
                std::set<std::int32_t> coedges;
                while (!cp.is_null() && coedges.insert(cp.value).second) {
                    auto c = modeler_view<acis::SatCoedge>(d, cp);
                    if (!c)
                        break;
                    if (auto e = modeler_view<acis::SatEdge>(d, c->edge))
                        edge_entity(*c, *e);
                    cp = c->next;
                }
                lp = l->next_loop;
            }
        }
        if (!result.empty()) {
            if (!d.records_of_type("transform").empty()) {
                auto p = d.placement();
                Matrix4 matrix;
                for (std::size_t i = 0; i < 3; ++i) {
                    for (std::size_t j = 0; j < 3; ++j)
                        matrix.m[i][j] = p.scale * p.matrix[j][i];
                    matrix.m[i][3] = i == 0 ? p.translation.x : i == 1 ? p.translation.y : p.translation.z;
                }
                auto transform = Transform::from_matrix(matrix);
                for (auto &part : result)
                    std::visit(
                        [&](auto &e) {
                            using T = std::decay_t<decltype(e)>;
                            if constexpr (std::is_same_v<T, DxfSpline>) {
                                bool fit_only = e.definition().control_points.empty();
                                e.apply_transform(transform);
                                if (fit_only) {
                                    auto data = e.definition();
                                    data.knots.clear();
                                    data.control_points.clear();
                                    data.weights.clear();
                                    e = spline_entity(data);
                                }
                            } else if constexpr (!std::is_same_v<T, ModelerWireFace>)
                                e.apply_transform(transform);
                        },
                        part);
            }
            return result;
        }
    }
    for (auto record : faces) {
        auto face = acis::SatFace::from_record(*record);
        if (!face)
            continue;
        auto points = modeler_face_points(d, *face);
        if (points.empty())
            continue;
        auto surface = d.resolve(face->surface);
        bool planar = !surface || surface->entity_type.find("plane") != std::string::npos;
        if (kind == ModelerKind::Region) {
            if (!planar)
                continue;
            for (std::size_t i = 1; i < points.size(); ++i)
                line(points[i - 1], points[i]);
            if (points.size() > 2)
                line(points.back(), points.front());
        } else {
            ModelerWireFace part;
            part.common = common(planar ? "REGION" : "SURFACE", "AcDbModelerGeometry");
            part.planar = planar;
            part.points = std::move(points);
            result.emplace_back(std::move(part));
        }
    }
    return result;
}
