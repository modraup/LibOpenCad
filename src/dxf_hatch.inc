// SPDX-License-Identifier: MPL-2.0
// acadrust entities/hatch.rs and DXF section_reader/section_writer.
namespace {
double hatch_ellipse_angle(double ratio, double value, bool to_parameter) {
    if (!(ratio > 0) || !std::isfinite(ratio) || !std::isfinite(value))
        return value;
    const double angle = std::atan2(std::sin(value) * (to_parameter ? 1 / ratio : ratio), std::cos(value));
    return angle + std::round((value - angle) / (2 * pi)) * (2 * pi);
}
} // namespace
HatchDefinition DxfHatch::definition() const {
    HatchDefinition v;
    v.is_mpolygon = type() == "MPOLYGON";
    const auto scope = v.is_mpolygon ? "AcDbMPolygon" : "AcDbHatch";
    std::vector<std::size_t> positions;
    locate_if(scope, [&](std::size_t at, int) {
        positions.push_back(at);
        return false;
    });
    auto code = [&](std::size_t at) { return at < positions.size() ? pairs_[positions[at]].code : -1; };
    auto real = [&](std::size_t at) { return number_at(positions.at(at)); };
    auto int_value = [&](std::size_t at) {
        const auto &p = pairs_[positions.at(at)];
        return integer(binary_ ? detail::text_value_from_binary(p.code, p.value) : p.value);
    };
    auto text = [&](std::size_t at) { return decode_string(pairs_[positions.at(at)].value); };
    std::size_t cursor = 0;
    // Bounded prefix readers deliberately stop before the first unrelated code, as Rust does.
    auto prefix = [&](std::size_t maximum, auto consume) {
        for (std::size_t n = 0; n < maximum && cursor < positions.size(); ++n) {
            if (!consume(code(cursor), cursor))
                break;
            ++cursor;
        }
    };
    auto points = [&](int count, int xcode, bool extra, double extra_default) {
        std::vector<Point3> result;
        for (int n = 0; n < count && code(cursor) == xcode; ++n) {
            Point3 p{real(cursor++), 0, extra_default};
            if (code(cursor) != xcode + 10)
                break;
            p.y = real(cursor++);
            if (extra && code(cursor) == 42)
                p.z = real(cursor++);
            result.push_back(p);
        }
        return result;
    };
    HatchPath path;
    bool boundary = false, elevation_x = false, elevation_y = false;
    auto flush = [&] {
        if (!path.edges.empty()) {
            v.paths.push_back(std::move(path));
            path = {};
        }
    };
    while (cursor < positions.size()) {
        const auto at = cursor++;
        const auto c = code(at);
        if (c == 2)
            v.pattern_name = text(at);
        else if (c == 10 && !boundary)
            elevation_x = true;
        else if (c == 20 && !boundary)
            elevation_y = true;
        else if (c == 30 && !boundary)
            v.elevation = real(at);
        else if (c == 62 && v.is_mpolygon)
            v.mpolygon_hatch_color = Color::from_index(static_cast<std::int16_t>(int_value(at)));
        else if (c == 11 && v.is_mpolygon)
            v.mpolygon_x_direction.x = real(at);
        else if (c == 21 && v.is_mpolygon)
            v.mpolygon_x_direction.y = real(at);
        else if (c == 70)
            v.solid = int_value(at) != 0;
        else if (c == 71)
            v.associative = int_value(at) != 0;
        else if (c == 75) {
            const auto n = int_value(at);
            v.style = n >= 0 && n <= 2 ? static_cast<std::int16_t>(n) : 0;
        } else if (c == 76) {
            const auto n = int_value(at);
            v.pattern_type = n >= 0 && n <= 2 ? static_cast<std::int16_t>(n) : 1;
        } else if (c == 52)
            v.pattern_angle = real(at) * radians_per_degree;
        else if (c == 41)
            v.pattern_scale = real(at);
        else if (c == 77)
            v.double_pattern = int_value(at) != 0;
        else if (c == 47)
            v.pixel_size = real(at);
        else if (c == 92) {
            flush();
            boundary = true;
            path.flags = static_cast<std::uint32_t>(int_value(at));
            if (path.flags & 2) {
                HatchPolylineEdge poly;
                bool bulge = false;
                int count = 0;
                prefix(3, [&](int k, std::size_t i) {
                    if (k == 72)
                        bulge = int_value(i) != 0;
                    else if (k == 73)
                        poly.closed = int_value(i) != 0;
                    else if (k == 93)
                        count = int_value(i);
                    else
                        return false;
                    return true;
                });
                poly.vertices = points(count, 10, bulge, 0);
                path.edges.push_back(std::move(poly));
            }
        } else if (c == 72) {
            const auto type = int_value(at);
            if (type == 1) {
                HatchLineEdge edge;
                prefix(4, [&](int k, std::size_t i) {
                    switch (k) {
                    case 10:
                        edge.start.x = real(i);
                        break;
                    case 20:
                        edge.start.y = real(i);
                        break;
                    case 11:
                        edge.end.x = real(i);
                        break;
                    case 21:
                        edge.end.y = real(i);
                        break;
                    default:
                        return false;
                    }
                    return true;
                });
                path.edges.push_back(edge);
            } else if (type == 2 || type == 3) {
                HatchEllipseEdge ellipse;
                HatchArcEdge arc;
                double start = 0, end = type == 3 ? 2 * pi : 0;
                prefix(type == 2 ? 6 : 8, [&](int k, std::size_t i) {
                    switch (k) {
                    case 10:
                        arc.center.x = ellipse.center.x = real(i);
                        break;
                    case 20:
                        arc.center.y = ellipse.center.y = real(i);
                        break;
                    case 11:
                        if (type != 3)
                            return false;
                        ellipse.major_axis_endpoint.x = real(i);
                        break;
                    case 21:
                        if (type != 3)
                            return false;
                        ellipse.major_axis_endpoint.y = real(i);
                        break;
                    case 40:
                        arc.radius = ellipse.minor_axis_ratio = real(i);
                        break;
                    case 50:
                        start = real(i) * radians_per_degree;
                        break;
                    case 51:
                        end = real(i) * radians_per_degree;
                        break;
                    case 73:
                        arc.counter_clockwise = ellipse.counter_clockwise = int_value(i) != 0;
                        break;
                    default:
                        return false;
                    }
                    return true;
                });
                if (type == 2) {
                    arc.start_angle = start;
                    arc.end_angle = end;
                    path.edges.push_back(arc);
                } else {
                    ellipse.start_angle = hatch_ellipse_angle(ellipse.minor_axis_ratio, start, true);
                    ellipse.end_angle = hatch_ellipse_angle(ellipse.minor_axis_ratio, end, true);
                    path.edges.push_back(ellipse);
                }
            } else if (type == 4) {
                HatchSplineEdge edge;
                int knots = 0, controls = 0, fit = 0;
                prefix(5, [&](int k, std::size_t i) {
                    switch (k) {
                    case 94:
                        edge.degree = int_value(i);
                        break;
                    case 73:
                        edge.rational = int_value(i) != 0;
                        break;
                    case 74:
                        edge.periodic = int_value(i) != 0;
                        break;
                    case 95:
                        knots = int_value(i);
                        break;
                    case 96:
                        controls = int_value(i);
                        break;
                    default:
                        return false;
                    }
                    return true;
                });
                for (int n = 0; n < knots && code(cursor) == 40; ++n)
                    edge.knots.push_back(real(cursor++));
                edge.control_points = points(controls, 10, true, 1);
                if (version_ >= DxfVersion::AC1024 && code(cursor) == 97)
                    fit = int_value(cursor++);
                for (auto p : points(fit, 11, false, 0))
                    edge.fit_points.push_back({p.x, p.y});
                prefix(4, [&](int k, std::size_t i) {
                    switch (k) {
                    case 12:
                        edge.start_tangent.x = real(i);
                        break;
                    case 22:
                        edge.start_tangent.y = real(i);
                        break;
                    case 13:
                        edge.end_tangent.x = real(i);
                        break;
                    case 23:
                        edge.end_tangent.y = real(i);
                        break;
                    default:
                        return false;
                    }
                    return true;
                });
                path.edges.push_back(std::move(edge));
            }
        } else if (c == 97) {
            const auto count = int_value(at);
            for (int n = 0; n < count && code(cursor) == 330; ++n)
                path.boundary_handles.push_back(handle_value(pairs_[positions[cursor++]].value));
        } else if (c == 78) {
            const auto count = int_value(at);
            for (int n = 0; n < count && cursor < positions.size(); ++n) {
                HatchPatternLine line;
                int dashes = 0;
                prefix(6, [&](int k, std::size_t i) {
                    switch (k) {
                    case 53:
                        line.angle = real(i) * radians_per_degree;
                        break;
                    case 43:
                        line.base_point.x = real(i);
                        break;
                    case 44:
                        line.base_point.y = real(i);
                        break;
                    case 45:
                        line.offset.x = real(i);
                        break;
                    case 46:
                        line.offset.y = real(i);
                        break;
                    case 79:
                        dashes = int_value(i);
                        break;
                    default:
                        return false;
                    }
                    return true;
                });
                for (int j = 0; j < dashes && code(cursor) == 49; ++j)
                    line.dash_lengths.push_back(real(cursor++));
                v.pattern_lines.push_back(std::move(line));
                if (code(cursor) != 53)
                    break;
            }
        } else if (c == 98) {
            const auto count = int_value(at);
            for (int n = 0; n < count && code(cursor) == 10; ++n) {
                Vector2 point{real(cursor++), 0};
                if (code(cursor) == 20)
                    point.y = real(cursor++);
                v.seed_points.push_back(point);
            }
        } else if (c == 450) {
            auto &g = v.gradient;
            g.enabled = int_value(at) != 0;
            prefix(positions.size(), [&](int k, std::size_t i) {
                switch (k) {
                case 451:
                    g.reserved = int_value(i);
                    break;
                case 452:
                    g.single_color = int_value(i) != 0;
                    break;
                case 453:
                    break;
                case 460:
                    g.angle = real(i);
                    break;
                case 461:
                    g.shift = real(i);
                    break;
                case 462:
                    g.color_tint = real(i);
                    break;
                case 463:
                    g.colors.push_back({real(i), {}});
                    break;
                case 63:
                    if (!g.colors.empty() && g.colors.back().color.kind() != Color::Kind::Rgb)
                        g.colors.back().color = Color::from_index(static_cast<std::int16_t>(int_value(i)));
                    break;
                case 421:
                    if (!g.colors.empty())
                        g.colors.back().color =
                            Color::from_true_color_value(static_cast<std::uint32_t>(int_value(i)));
                    break;
                case 470:
                    g.name = text(i);
                    break;
                default:
                    return false;
                }
                return true;
            });
        }
    }
    flush();
    if (!elevation_x || !elevation_y)
        v.elevation = 0;
    v.normal = point_or(scope, 210, {0, 0, 1});
    if (solid_pattern_parameters_) {
        v.pattern_angle = solid_pattern_parameters_->x;
        v.pattern_scale = solid_pattern_parameters_->y;
    }
    return v;
}
void DxfHatch::set_definition(const HatchDefinition &v) {
    const bool mpolygon = type() == "MPOLYGON";
    if (v.is_mpolygon != mpolygon)
        throw std::invalid_argument("Hatch entity kind cannot be changed");
    if (v.pattern_type < 0 || v.pattern_type > 2 || v.style < 0 || v.style > 2)
        throw std::invalid_argument("Invalid HATCH pattern or style");
    std::vector<Pair> out;
    auto number = [&](int code, double value) { out.push_back({code, encode_number(value)}); };
    auto integer_value = [&](int code, std::int64_t value) {
        const auto text = std::to_string(value);
        const auto bytes = detail::binary_value_from_text(code, text);
        out.push_back({code, binary_ ? bytes : text});
    };
    auto string = [&](int code, const std::string &value) { out.push_back({code, encode_string(value)}); };
    auto point2 = [&](int code, Vector2 p) {
        number(code, p.x);
        number(code + 10, p.y);
    };
    auto point3 = [&](int code, Point3 p) {
        number(code, p.x);
        number(code + 10, p.y);
        number(code + 20, p.z);
    };
    auto count = [&](int code, std::size_t size) {
        if (size > INT32_MAX)
            throw std::invalid_argument("HATCH count overflow");
        integer_value(code, static_cast<std::int64_t>(size));
    };
    if (mpolygon)
        integer_value(75, v.style);
    point3(10, {0, 0, v.elevation});
    point3(210, v.normal);
    string(2, v.pattern_name);
    integer_value(70, v.solid);
    bool has_boundary = false;
    for (const auto &path : v.paths)
        for (auto h : path.boundary_handles)
            if (h)
                has_boundary = true;
    integer_value(71, v.associative && has_boundary);
    count(91, v.paths.size());
    for (const auto &path : v.paths) {
        out.push_back({92, binary_ ? detail::binary_int32_bits(path.flags)
                                   : std::to_string(static_cast<std::int32_t>(path.flags))});
        const bool polyline = (path.flags & 2) != 0;
        if (polyline && !path.edges.empty() &&
            (path.edges.size() != 1 || !std::holds_alternative<HatchPolylineEdge>(path.edges.front())))
            throw std::invalid_argument("Polyline hatch path requires exactly one polyline edge");
        if (!polyline)
            count(93, path.edges.size());
        else if (path.edges.empty()) {
            integer_value(72, 0);
            integer_value(73, 0);
            count(93, 0);
        }
        for (const auto &edge : path.edges)
            std::visit(
                [&](const auto &e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, HatchPolylineEdge>) {
                        if (!polyline)
                            throw std::invalid_argument("Polyline edge requires path polyline flag");
                        const bool bulge = std::any_of(e.vertices.begin(), e.vertices.end(),
                                                       [](auto p) { return std::abs(p.z) > 1e-10; });
                        integer_value(72, bulge);
                        integer_value(73, e.closed);
                        count(93, e.vertices.size());
                        for (auto p : e.vertices) {
                            finite(p.z);
                            point2(10, {p.x, p.y});
                            if (bulge)
                                number(42, p.z);
                        }
                    } else if constexpr (std::is_same_v<T, HatchLineEdge>) {
                        integer_value(72, 1);
                        point2(10, e.start);
                        point2(11, e.end);
                    } else if constexpr (std::is_same_v<T, HatchArcEdge>) {
                        integer_value(72, 2);
                        point2(10, e.center);
                        number(40, e.radius);
                        number(50, e.start_angle / radians_per_degree);
                        number(51, e.end_angle / radians_per_degree);
                        integer_value(73, e.counter_clockwise);
                    } else if constexpr (std::is_same_v<T, HatchEllipseEdge>) {
                        integer_value(72, 3);
                        point2(10, e.center);
                        point2(11, e.major_axis_endpoint);
                        number(40, e.minor_axis_ratio);
                        number(50, hatch_ellipse_angle(e.minor_axis_ratio, e.start_angle, false) /
                                       radians_per_degree);
                        number(51, hatch_ellipse_angle(e.minor_axis_ratio, e.end_angle, false) /
                                       radians_per_degree);
                        integer_value(73, e.counter_clockwise);
                    } else if constexpr (std::is_same_v<T, HatchSplineEdge>) {
                        integer_value(72, 4);
                        integer_value(94, e.degree);
                        integer_value(73, e.rational);
                        integer_value(74, e.periodic);
                        count(95, e.knots.size());
                        count(96, e.control_points.size());
                        for (auto knot : e.knots)
                            number(40, knot);
                        for (auto p : e.control_points) {
                            finite(p.z);
                            point2(10, {p.x, p.y});
                            if (e.rational)
                                number(42, p.z);
                        }
                        if (version_ >= DxfVersion::AC1024) {
                            count(97, e.fit_points.size());
                            for (auto p : e.fit_points)
                                point2(11, p);
                            if (!e.fit_points.empty()) {
                                point2(12, e.start_tangent);
                                point2(13, e.end_tangent);
                            }
                        }
                    }
                },
                edge);
        const auto n = static_cast<std::size_t>(std::count_if(
            path.boundary_handles.begin(), path.boundary_handles.end(), [](auto h) { return h != 0; }));
        count(97, n);
        for (auto h : path.boundary_handles)
            if (h)
                out.push_back({330, hex_handle(h)});
    }
    integer_value(75, v.style);
    integer_value(76, v.pattern_type);
    if (!v.solid) {
        number(52, v.pattern_angle / radians_per_degree);
        number(41, v.pattern_scale);
        integer_value(77, v.double_pattern);
        count(78, v.pattern_lines.size());
        for (const auto &line : v.pattern_lines) {
            number(53, line.angle / radians_per_degree);
            number(43, line.base_point.x);
            number(44, line.base_point.y);
            number(45, line.offset.x);
            number(46, line.offset.y);
            count(79, line.dash_lengths.size());
            for (auto dash : line.dash_lengths)
                number(49, dash);
        }
    }
    if (mpolygon) {
        integer_value(62,
                      v.mpolygon_hatch_color.index().value_or(v.mpolygon_hatch_color.approximate_index()));
        point2(11, v.mpolygon_x_direction);
        integer_value(99, 0);
    } else {
        if (std::any_of(v.paths.begin(), v.paths.end(),
                        [](const auto &path) { return (path.flags & 4) != 0; }))
            number(47, v.pixel_size);
        count(98, v.seed_points.size());
        for (auto seed : v.seed_points)
            point2(10, seed);
    }
    if (v.gradient.enabled) {
        const auto &g = v.gradient;
        integer_value(450, 1);
        integer_value(451, g.reserved);
        number(460, g.angle);
        number(461, g.shift);
        integer_value(452, g.single_color);
        number(462, g.color_tint);
        count(453, g.colors.size());
        for (const auto &color : g.colors) {
            number(463, color.value);
            integer_value(63, color.color.index().value_or(color.color.approximate_index()));
            if (auto rgb = color.color.to_true_color_value())
                integer_value(421, *rgb);
        }
        string(470, g.name);
    }
    DxfHatch staged = *this;
    std::vector<std::size_t> positions;
    static const std::unordered_set<int> modeled{
        2,  10, 20, 30, 11, 21, 12, 22,  13,  23,  210, 220, 230, 40,  41,  42,  43,  44, 45, 46,
        47, 49, 50, 51, 52, 53, 62, 63,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79, 91, 92,
        93, 94, 95, 96, 97, 98, 99, 330, 421, 450, 451, 452, 453, 460, 461, 462, 463, 470};
    const auto end = staged
                         .locate_if(mpolygon ? "AcDbMPolygon" : "AcDbHatch",
                                    [&](std::size_t at, int code) {
                                        if (modeled.count(code))
                                            positions.push_back(at);
                                        return false;
                                    })
                         .index;
    if (end == std::string_view::npos)
        throw std::invalid_argument("Missing HATCH subclass");
    const auto insertion = positions.empty() ? end : positions.front();
    for (auto at = positions.rbegin(); at != positions.rend(); ++at)
        staged.pairs_.erase(staged.pairs_.begin() + *at);
    staged.pairs_.insert(staged.pairs_.begin() + insertion, out.begin(), out.end());
    finite(v.pattern_angle);
    finite(v.pattern_scale);
    staged.solid_pattern_parameters_ =
        v.solid ? std::optional<Vector2>{{v.pattern_angle, v.pattern_scale}} : std::nullopt;
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_hatch(const HatchDefinition &v, const EntityOptions &options) {
    if (version_ < DxfVersion::AC1024)
        throw std::invalid_argument("Full HATCH construction requires R2010 or later");
    for (const auto &path : v.paths)
        for (auto h : path.boundary_handles)
            if (h && records_with_handle(hex_handle(h)).size() != 1)
                throw std::invalid_argument("Missing or ambiguous hatch boundary");
    DxfHatch entity;
    static_cast<DxfEntity &>(entity) = make_entity(v.is_mpolygon ? "MPOLYGON" : "HATCH",
                                                   v.is_mpolygon ? "AcDbMPolygon" : "AcDbHatch", options);
    entity.set_definition(v);
    return insert_entity(std::move(entity), options);
}
