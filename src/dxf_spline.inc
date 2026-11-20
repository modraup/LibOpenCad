// SPDX-License-Identifier: MPL-2.0
// acadrust entities/{spline,helix}.rs and the DXF spline/helix subclass I/O.
namespace {
// Match the upstream writer's conversion of compact periodic cubic control nets.
SplineDefinition clamp_periodic_cubic(SplineDefinition value) {
    const auto n = value.control_points.size();
    if (!(value.flags & 2) || value.degree != 3 || n < 3 || value.knots.size() != n + 1)
        return value;
    const auto weight = [&](std::size_t i) { return i < value.weights.size() ? value.weights[i] : 1.; };
    const auto combine = [&](std::initializer_list<std::pair<std::size_t, double>> terms) {
        const auto common = weight(terms.begin()->first);
        Point3 point{};
        if (!(value.flags & 4) || std::all_of(terms.begin(), terms.end(),
                                              [&](const auto &t) { return weight(t.first) == common; })) {
            for (auto [i, factor] : terms)
                point = point + value.control_points[i] * factor;
            return std::make_pair(point, common);
        }
        double scale = 0;
        for (auto [i, factor] : terms) {
            (void)factor;
            scale = std::max(scale, weight(i));
        }
        double sum = 0;
        for (auto [i, factor] : terms) {
            const auto contribution = factor * (weight(i) / scale);
            sum += contribution;
            point = point + value.control_points[i] * contribution;
        }
        return std::make_pair(point * (1. / sum), sum * scale);
    };
    const auto k = (n - 2) % n, previous = (k + n - 1) % n, next = (k + 1) % n;
    const auto first = combine({{previous, 1. / 6.}, {k, 2. / 3.}, {next, 1. / 6.}});
    std::vector<std::pair<Point3, double>> controls{first, combine({{k, 2. / 3.}, {next, 1. / 3.}})};
    for (std::size_t i = 0; i < n - 1; ++i) {
        const auto index = (next + i) % n;
        controls.emplace_back(value.control_points[index], weight(index));
    }
    controls.push_back(combine({{previous, 1. / 3.}, {k, 2. / 3.}}));
    controls.push_back(first);
    std::vector<double> knots(4, value.knots.front());
    knots.insert(knots.end(), value.knots.begin() + 1, value.knots.end() - 1);
    knots.insert(knots.end(), 4, value.knots.back());
    value.knots = std::move(knots);
    value.control_points.clear();
    value.weights.clear();
    for (const auto &[point, weight] : controls) {
        value.control_points.push_back(point);
        if (value.flags & 4)
            value.weights.push_back(weight);
    }
    return value;
}
// Cubic B-spline basis and its derivatives on the closed final knot span.
double fit_basis(const std::vector<double> &k, std::size_t i, int degree, double u, int derivative) {
    if (!degree)
        return (k[i] <= u && u < k[i + 1]) ||
               (u == k.back() && k[i] < u && k[i + 1] == u);
    const auto left = k[i + degree] - k[i], right = k[i + degree + 1] - k[i + 1];
    if (derivative)
        return degree * ((left ? fit_basis(k, i, degree - 1, u, derivative - 1) / left : 0.) -
                         (right ? fit_basis(k, i + 1, degree - 1, u, derivative - 1) / right : 0.));
    return (left ? (u - k[i]) / left * fit_basis(k, i, degree - 1, u, 0) : 0.) +
           (right ? (k[i + degree + 1] - u) / right * fit_basis(k, i + 1, degree - 1, u, 0) : 0.);
}

void interpolate_fit_cubic(SplineDefinition &s) {
    const auto &points = s.fit_points;
    if (!points.empty() && s.control_points.empty()) {
        s.flags |= 1024;
        if (!(s.flags & 0x1e0)) s.flags |= 32;
    }
    if (s.degree != 3 || points.size() < 2 || !s.control_points.empty())
        return;
    const auto m = points.size() + 2;
    const int method = s.flags & 0x1e0;
    std::vector<double> params{0.};
    double length = 0.;
    // The final-column knot cap is checked after coincident points, which keep the source form.
    for (std::size_t i = 1; i < points.size(); ++i) {
        const double d = points[i].distance(points[i - 1]);
        if (d < 1e-12)
            return;
        length += d;
        params.push_back(params.back() + (method == 64 ? std::sqrt(d) : method == 128 ? 1. : d));
    }
    if (m + 4 > INT16_MAX)
        throw std::overflow_error("DXF cubic knot count exceeds signed group count");
    std::vector<double> knots(3, 0.);
    knots.insert(knots.end(), params.begin(), params.end());
    knots.insert(knots.end(), 3, params.back());

    // Only four adjacent basis functions can contribute to any input row.
    std::vector<std::map<std::size_t, double>> rows(m);
    std::vector<Point3> values(m);
    rows.front()[0] = rows.back()[m - 1] = 1.;
    values.front() = points.front();
    values.back() = points.back();
    const double speed = length / params.back();
    for (std::size_t i = 0; i < points.size(); ++i) {
        int derivative = 0;
        values[i + 1] = points[i];
        if (i == 0 || i + 1 == points.size()) {
            auto tangent = i ? s.end_tangent : s.begin_tangent;
            derivative = tangent == Point3{} ? 2 : 1;
            values[i + 1] = tangent.normalize() * speed;
        }
        const auto span = std::min(i, points.size() - 2);
        for (auto j = span; j < span + 4; ++j) {
            const auto b = fit_basis(knots, j, 3, params[i], derivative);
            if (b) rows[i + 1][j] = b;
        }
    }
    for (std::size_t col = 0; col < m; ++col) {
        auto pivot = col;
        for (auto r = col + 1; r < std::min(m, col + 3); ++r)
            if (std::abs(rows[r][col]) >= std::abs(rows[pivot][col]))
                pivot = r;
        if (std::abs(rows[pivot][col]) < 1e-14)
            return;
        std::swap(rows[col], rows[pivot]);
        std::swap(values[col], values[pivot]);
        for (auto r = col + 1; r < std::min(m, col + 3); ++r) {
            const double factor = rows[r][col] / rows[col][col];
            rows[r].erase(col);
            if (!factor) continue;
            for (const auto &[k, a] : rows[col])
                if (k > col) rows[r][k] -= factor * a;
            values[r] = values[r] - values[col] * factor;
        }
    }
    for (std::size_t r = m; r--;) {
        for (const auto &[k, a] : rows[r])
            if (k > r) values[r] = values[r] - values[k] * a;
        values[r] = values[r] / rows[r][r];
    }
    s.control_points = std::move(values);
    s.knots = std::move(knots);
}
} // namespace
SplineDefinition DxfSpline::definition() const {
    SplineDefinition result;
    result.degree = static_cast<std::int16_t>(short_field("AcDbSpline", 71, 3));
    result.flags = static_cast<std::int16_t>(short_field("AcDbSpline", 70, 0));
    result.normal = point_or("AcDbSpline", 210, {0, 0, 1});
    result.begin_tangent = point_or("AcDbSpline", 12, {});
    result.end_tangent = point_or("AcDbSpline", 13, {});
    result.knot_tolerance = number("AcDbSpline", 42, 0);
    result.control_tolerance = number("AcDbSpline", 43, 0);
    result.fit_tolerance = number("AcDbSpline", 44, 0);
    struct Accumulator {
        Point3 point{};
        bool x = false, y = false;
        void flush(std::vector<Point3> &points) {
            if (x && y)
                points.push_back(point);
            point = {};
            x = y = false;
        }
    } control, fit;
    locate_if("AcDbSpline", [&](std::size_t i, int code) {
        if (code == 40)
            result.knots.push_back(number_at(i));
        if (code == 41)
            result.weights.push_back(number_at(i));
        if (code == 10) {
            control.flush(result.control_points);
            control.point.x = number_at(i);
            control.x = true;
        } else if (code == 20) {
            control.point.y = number_at(i);
            control.y = true;
        } else if (code == 30)
            control.point.z = number_at(i);
        if (code == 11) {
            fit.flush(result.fit_points);
            fit.point.x = number_at(i);
            fit.x = true;
        } else if (code == 21) {
            fit.point.y = number_at(i);
            fit.y = true;
        } else if (code == 31)
            fit.point.z = number_at(i);
        return false;
    });
    control.flush(result.control_points);
    fit.flush(result.fit_points);
    return result;
}
void DxfSpline::set_definition(const SplineDefinition &requested) {
    // Validate before arithmetic so NaN inputs cannot disappear during normalization.
    for (const auto &p : requested.control_points) {
        finite(p.x);
        finite(p.y);
        finite(p.z);
    }
    for (double weight : requested.weights)
        finite(weight);
    for (double knot : requested.knots)
        finite(knot);
    if (requested.weights.size() > requested.control_points.size())
        throw std::invalid_argument("More spline weights than control points");
    auto value = clamp_periodic_cubic(requested);
    interpolate_fit_cubic(value);
    if (value.degree < 1)
        throw std::invalid_argument("Spline degree must be positive");
    for (auto size :
         {value.knots.size(), value.control_points.size(), value.fit_points.size(), value.weights.size()})
        if (size > INT16_MAX)
            throw std::overflow_error("DXF spline array exceeds signed group count");
    if (value.weights.size() > value.control_points.size())
        throw std::invalid_argument("More spline weights than control points");
    std::vector<Pair> replacement;
    auto real = [&](int code, double number) { replacement.push_back({code, encode_number(number)}); };
    auto integer = [&](int code, int number) {
        replacement.push_back({code, binary_ ? detail::binary_short(number) : std::to_string(number)});
    };
    auto point = [&](int code, Point3 p) {
        real(code, p.x);
        real(code + 10, p.y);
        real(code + 20, p.z);
    };
    point(210, value.normal);
    integer(70, value.flags);
    integer(71, value.degree);
    integer(72, static_cast<int>(value.knots.size()));
    integer(73, static_cast<int>(value.control_points.size()));
    integer(74, static_cast<int>(value.fit_points.size()));
    real(42, value.knot_tolerance);
    real(43, value.control_tolerance);
    real(44, value.fit_tolerance);
    point(12, value.begin_tangent);
    point(13, value.end_tangent);
    for (double knot : value.knots)
        real(40, knot);
    for (std::size_t i = 0; i < value.control_points.size(); ++i) {
        point(10, value.control_points[i]);
        if (i < value.weights.size())
            real(41, value.weights[i]);
        else if (value.flags & 4)
            real(41, 1);
    }
    for (auto p : value.fit_points)
        point(11, p);
    const std::set<int> codes{10, 20, 30, 11, 21, 31, 12, 22, 32, 13,  23,  33, 40,
                              41, 42, 43, 44, 70, 71, 72, 73, 74, 210, 220, 230};
    std::vector<std::size_t> old;
    const auto end = locate_if("AcDbSpline", [&](std::size_t i, int code) {
        if (codes.count(code))
            old.push_back(i);
        return false;
    });
    if (end.index == std::string_view::npos)
        throw std::invalid_argument("Missing AcDbSpline subclass");
    const auto insertion = old.empty() ? end.index : old.front();
    std::vector<Pair> updated;
    std::size_t remove = 0;
    for (std::size_t i = 0; i <= pairs_.size(); ++i) {
        if (i == insertion)
            updated.insert(updated.end(), replacement.begin(), replacement.end());
        if (i == pairs_.size())
            break;
        if (remove < old.size() && old[remove] == i)
            ++remove;
        else
            updated.push_back(pairs_[i]);
    }
    pairs_.swap(updated);
}
HelixParameters DxfHelix::parameters() const {
    HelixParameters result;
    auto integer = [&](int code, int fallback) {
        const auto p = locate("AcDbHelix", code);
        if (!p.found)
            return fallback;
        const auto &raw = pairs_[p.index].value;
        return libopencad::integer(binary_ ? detail::text_value_from_binary(code, raw) : raw);
    };
    result.major_version = integer(90, 29);
    result.maintenance_version = integer(91, 0);
    result.axis_base_point = point_or("AcDbHelix", 10, {});
    result.start_point = point_or("AcDbHelix", 11, {});
    result.axis_vector = point_or("AcDbHelix", 12, {0, 0, 1});
    result.radius = number("AcDbHelix", 40, 1);
    result.turns = number("AcDbHelix", 41, 3);
    result.turn_height = number("AcDbHelix", 42, 1);
    result.handedness = integer(290, 1) != 0;
    const auto constraint = integer(280, 0);
    result.constraint = constraint == 1   ? HelixConstraint::Turns
                        : constraint == 2 ? HelixConstraint::Height
                                          : HelixConstraint::TurnHeight;
    return result;
}
void DxfHelix::set_parameters(const HelixParameters &value) {
    if (static_cast<unsigned>(value.constraint) > 2)
        throw std::invalid_argument("Invalid helix constraint");
    DxfHelix staged = *this;
    auto integer = [&](int code, std::int32_t number) {
        const auto text = std::to_string(number);
        staged.set_field("AcDbHelix", code, binary_ ? detail::binary_value_from_text(code, text) : text);
    };
    integer(90, value.major_version);
    integer(91, value.maintenance_version);
    staged.set_point("AcDbHelix", 10, value.axis_base_point);
    staged.set_point("AcDbHelix", 11, value.start_point);
    staged.set_point("AcDbHelix", 12, value.axis_vector);
    staged.set_number("AcDbHelix", 40, value.radius);
    staged.set_number("AcDbHelix", 41, value.turns);
    staged.set_number("AcDbHelix", 42, value.turn_height);
    integer(290, value.handedness ? 1 : 0);
    integer(280, static_cast<int>(value.constraint));
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_spline(const SplineDefinition &definition, const EntityOptions &options) {
    if (version_ == DxfVersion::AC1009)
        throw std::invalid_argument("SPLINE requires R13 or later");
    DxfSpline entity;
    static_cast<DxfEntity &>(entity) = make_entity("SPLINE", "AcDbSpline", options);
    entity.set_definition(definition);
    return insert_entity(std::move(entity), options);
}
std::uint64_t DxfDocument::add_helix(const SplineDefinition &definition, const HelixParameters &parameters,
                                     const EntityOptions &options) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("HELIX requires R2007 or later");
    DxfHelix entity;
    static_cast<DxfEntity &>(entity) = make_entity("HELIX", "AcDbSpline", options);
    entity.pairs_.push_back({100, "AcDbHelix"});
    entity.set_definition(definition);
    entity.set_parameters(parameters);
    return insert_entity(std::move(entity), options);
}
