// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/classes/civil/create/alignment.rs (free arcs and SCS)
// and the write-path subset of pieces_with_curvature (fixed lines, free
// arcs and free spiral-curve-spirals between two lines).
constexpr std::int32_t ALIGNMENT_ARC = 258;
constexpr std::int32_t ALIGNMENT_SCS = 260;
constexpr std::int32_t ALIGNMENT_BETWEEN_LINES = 29;
constexpr std::int32_t ALIGNMENT_SCS_BETWEEN = 0;
constexpr std::int32_t ALIGNMENT_SPIRAL_DEFINITION = 256;
constexpr std::int32_t ALIGNMENT_ARC_EDGE = 0x010F0000;
constexpr double ALIGNMENT_STEP = 1.0;
constexpr double ALIGNMENT_PI = 3.14159265358979323846;
constexpr double ALIGNMENT_TAU = 2.0 * ALIGNMENT_PI;

struct AlignmentPathPoint {
    Vector2 point{};
    std::optional<double> bulge;
};
struct AlignmentProfileRef {
    std::uint64_t handle = 0;
    std::array<std::uint8_t, 16> id{};
};
struct AlignmentWalkNote {
    Vector2 first{}, last{};
    AlignmentPiece kind = AlignmentPiece::Line;
    double curvature = 0;
};
struct AlignmentState {
    Vector2 point{};
    double heading = 0, curvature = 0;
};
struct AlignmentPendingLine {
    Vector2 start{}, direction{}, end{};
};

double alignment_heading(Vector2 v) { return std::atan2(v.y, v.x); }
Vector2 alignment_direction(double h) { return {std::cos(h), std::sin(h)}; }
double alignment_normalize(double angle) {
    auto x = std::fmod(angle + ALIGNMENT_PI, ALIGNMENT_TAU);
    if (x < 0)
        x += ALIGNMENT_TAU;
    return x - ALIGNMENT_PI;
}
std::optional<Vector2> alignment_unit(Vector2 v) {
    auto length = v.length();
    if (length <= 1e-12)
        return std::nullopt;
    return v / length;
}
std::optional<Vector2> alignment_intersect(Vector2 p, Vector2 u, Vector2 q, Vector2 v) {
    auto den = u.cross(v);
    if (std::abs(den) < 1e-12)
        return std::nullopt;
    return p + u * ((q - p).cross(v) / den);
}
bool alignment_same(Vector2 a, Vector2 b) {
    return a.distance(b) <= 1e-9 * (1.0 + std::max(std::abs(a.x), std::abs(a.y)));
}
void alignment_made(std::vector<AlignmentWalkNote> &notes, const std::vector<Vector2> &points, AlignmentPiece kind,
                    double curvature) {
    if (points.empty())
        return;
    notes.push_back({points.front(), points.back(), kind, curvature});
}
void alignment_shift_notes(std::vector<AlignmentWalkNote> &notes, const std::vector<Vector2> &points, Vector2 shift) {
    auto has = [&](Vector2 p) {
        for (auto &q : points)
            if (p.distance(q) < 1e-12)
                return true;
        return false;
    };
    for (auto &n : notes)
        if (has(n.first) && has(n.last)) {
            n.first = n.first + shift;
            n.last = n.last + shift;
        }
}
std::pair<std::vector<Vector2>, double> alignment_walk_points(Vector2 start, double h, double k0, double k1,
                                                             double length) {
    auto n = static_cast<int>(std::ceil(length / ALIGNMENT_STEP));
    n = std::max(2, std::min(n, 20000));
    auto ds = length / static_cast<double>(n);
    std::vector<Vector2> points{start};
    auto p = start;
    auto heading_at = [&](double s) { return h + k0 * s + (k1 - k0) * s * s / (2.0 * length); };
    for (int i = 0; i < n; ++i) {
        auto s0 = static_cast<double>(i) * ds, s1 = static_cast<double>(i + 1) * ds;
        auto half = (heading_at(s1) - heading_at(s0)) / 2.0;
        auto chord = std::abs(half) < 1e-12 ? ds : ds * std::sin(half) / half;
        p = p + alignment_direction(heading_at((s0 + s1) / 2.0)) * chord;
        points.push_back(p);
    }
    return {points, h + k0 * length + (k1 - k0) * length / 2.0};
}
std::pair<std::vector<Vector2>, double> alignment_walk(std::vector<AlignmentWalkNote> &notes, Vector2 start, double h,
                                                      double k0, double k1, double length) {
    auto walked = alignment_walk_points(start, h, k0, k1, length);
    AlignmentPiece kind = std::abs(k1 - k0) > 1e-12 ? AlignmentPiece::Spiral
                         : std::abs(k0) > 1e-12     ? AlignmentPiece::Curve
                                                    : AlignmentPiece::Line;
    alignment_made(notes, walked.first, kind, kind == AlignmentPiece::Curve ? k0 : 0);
    return walked;
}
std::pair<std::vector<Vector2>, AlignmentState> alignment_pieces_from(std::vector<AlignmentWalkNote> &notes,
                                                                     AlignmentState state,
                                                                     const std::vector<std::array<double, 3>> &parts) {
    std::vector<Vector2> points{state.point};
    auto h = state.heading, k = state.curvature;
    for (auto &part : parts) {
        if (part[2] > 1e-9) {
            auto walked = alignment_walk(notes, points.back(), h, part[0], part[1], part[2]);
            h = walked.second;
            auto skip = points.size() == 1 ? 1u : 0u;
            points.insert(points.end(), walked.first.begin() + static_cast<std::ptrdiff_t>(skip), walked.first.end());
            k = part[1];
        }
    }
    return {points, AlignmentState{points.back(), h, k}};
}
std::optional<std::pair<Vector2, Vector2>> alignment_line_carrier(const AlignmentElement *element) {
    if (!element || element->kind.tag != AlignmentElementKind::Tag::FixedLine)
        return std::nullopt;
    auto u = alignment_unit(element->kind.end - element->kind.start);
    if (!u)
        return std::nullopt;
    return std::pair<Vector2, Vector2>{element->kind.start, *u};
}
std::optional<std::pair<std::vector<Vector2>, AlignmentState>>
alignment_fitted_curve(std::vector<Vector2> &points, std::optional<AlignmentPendingLine> &pending,
                       std::optional<AlignmentState> state, const AlignmentElement *next, double spiral_in,
                       double radius, double spiral_out, std::vector<AlignmentWalkNote> &notes) {
    auto carrier = alignment_line_carrier(next);
    if (!carrier)
        return std::nullopt;
    auto b = carrier->first, v = carrier->second;
    if (pending) {
        auto u = pending->direction;
        auto pi = alignment_intersect(pending->start, u, b, v);
        if (!pi)
            return std::nullopt;
        auto delta = alignment_normalize(alignment_heading(v) - alignment_heading(u));
        auto k = (delta > 0 ? 1.0 : delta < 0 ? -1.0 : 0.0) / radius;
        auto arc = radius * (std::abs(delta) - (spiral_in + spiral_out) / (2.0 * radius));
        if (arc < -1e-9)
            return std::nullopt;
        AlignmentState origin{{0, 0}, alignment_heading(u), 0};
        auto solved = alignment_pieces_from(notes, origin,
                                            {{{0, k, spiral_in}, {k, k, std::max(arc, 0.0)}, {k, 0, spiral_out}}});
        auto slide = (*pi + solved.second.point - b).cross(v) / u.cross(v);
        auto ts = *pi - u * slide;
        auto line = *pending;
        pending.reset();
        points.push_back(line.start);
        points.push_back(ts);
        alignment_shift_notes(notes, solved.first, ts);
        std::vector<Vector2> q;
        q.reserve(solved.first.size());
        for (auto &p : solved.first)
            q.push_back(ts + p);
        return std::pair<std::vector<Vector2>, AlignmentState>{q, AlignmentState{q.back(), solved.second.heading,
                                                                                solved.second.curvature}};
    }
    if (!state)
        return std::nullopt;
    auto turn = alignment_normalize(alignment_heading(v) - state->heading);
    for (double sign : {1.0, -1.0}) {
        auto k = sign / radius;
        auto spirals = (state->curvature + k) * spiral_in / 2.0 + k * spiral_out / 2.0;
        auto arc = (turn - spirals) / k;
        if (arc < -1e-9)
            continue;
        auto solved = alignment_pieces_from(
            notes, *state, {{{state->curvature, k, spiral_in}, {k, k, std::max(arc, 0.0)}, {k, 0, spiral_out}}});
        if (std::abs((solved.second.point - b).cross(v)) < 0.01)
            return solved;
    }
    return std::nullopt;
}
void alignment_solve_run(const std::vector<const AlignmentElement *> &run, std::vector<std::vector<Vector2>> &out,
                         std::vector<AlignmentWalkNote> &notes) {
    std::vector<Vector2> points;
    std::optional<AlignmentState> state;
    std::optional<AlignmentPendingLine> pending;
    std::size_t index = 0;
    while (index < run.size()) {
        auto *element = run[index];
        auto *next = index + 1 < run.size() ? run[index + 1] : nullptr;
        ++index;
        if (element->kind.tag == AlignmentElementKind::Tag::FixedLine) {
            auto u = alignment_unit(element->kind.end - element->kind.start);
            if (!u)
                continue;
            if (pending) {
                Vector2 joint = pending->end;
                if (pending->end.distance(element->kind.start) > 0.01) {
                    if (auto x = alignment_intersect(pending->start, pending->direction, element->kind.start, *u))
                        if (x->distance(pending->end) < 1e4)
                            joint = *x;
                }
                points.push_back(pending->start);
                points.push_back(joint);
                state = AlignmentState{joint, alignment_heading(*u), 0};
                pending.reset();
            }
            auto from = state ? state->point : element->kind.start;
            pending = AlignmentPendingLine{from, *u, element->kind.end};
            state = AlignmentState{from, alignment_heading(*u), 0};
            if (!next) {
                points.push_back(from);
                points.push_back(element->kind.end);
                pending.reset();
            }
            continue;
        }
        std::optional<std::pair<std::vector<Vector2>, AlignmentState>> solved;
        if (element->kind.tag == AlignmentElementKind::Tag::FreeArc)
            solved = alignment_fitted_curve(points, pending, state, next, 0, element->kind.radius, 0, notes);
        else if (element->kind.tag == AlignmentElementKind::Tag::FreeSpiralCurveSpiral)
            solved = alignment_fitted_curve(points, pending, state, next, element->kind.spiral_in, element->kind.radius,
                                            element->kind.spiral_out, notes);
        if (solved) {
            auto &q = solved->first;
            if (!points.empty() && !q.empty() && points.back().distance(q.front()) >= 1e-9)
                points.push_back(q.front());
            points.insert(points.end(), q.begin(), q.end());
            state = solved->second;
        } else {
            if (pending) {
                points.push_back(pending->start);
                points.push_back(pending->end);
                pending.reset();
            }
            out.push_back(std::move(points));
            points.clear();
            state.reset();
        }
    }
    if (pending) {
        points.push_back(pending->start);
        points.push_back(pending->end);
    }
    out.push_back(std::move(points));
}
std::vector<std::vector<const AlignmentElement *>> alignment_chains(const std::vector<AlignmentElement> &elements) {
    auto by_id = [&](std::int16_t id) -> const AlignmentElement * {
        for (auto &e : elements)
            if (e.id == id)
                return &e;
        return nullptr;
    };
    std::vector<std::int16_t> seen;
    std::vector<std::vector<const AlignmentElement *>> out;
    for (auto &first : elements) {
        if (first.previous != 0 && by_id(first.previous))
            continue;
        std::vector<const AlignmentElement *> run;
        auto *current = &first;
        while (current) {
            bool twice = false;
            for (auto id : seen)
                if (id == current->id)
                    twice = true;
            if (twice)
                break;
            seen.push_back(current->id);
            run.push_back(current);
            current = by_id(current->next);
        }
        out.push_back(std::move(run));
    }
    return out;
}
std::vector<std::vector<Vector2>> alignment_raw_polylines(const std::vector<AlignmentElement> &elements,
                                                          std::vector<AlignmentWalkNote> &notes) {
    std::vector<std::vector<Vector2>> out;
    for (auto &run : alignment_chains(elements))
        alignment_solve_run(run, out, notes);
    out.erase(std::remove_if(out.begin(), out.end(), [](const std::vector<Vector2> &p) { return p.size() <= 1; }),
              out.end());
    return out;
}
std::vector<std::pair<std::size_t, std::size_t>> alignment_split_points(const std::vector<Vector2> &raw) {
    std::vector<std::size_t> joins{0};
    for (std::size_t i = 1; i < raw.size(); ++i)
        if (raw[i].distance(raw[i - 1]) < 1e-9)
            joins.push_back(i);
    joins.push_back(raw.size());
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (std::size_t i = 1; i < joins.size(); ++i)
        if (joins[i] > joins[i - 1] + 1)
            out.push_back({joins[i - 1], joins[i]});
    return out;
}
std::vector<std::tuple<AlignmentPiece, double, std::vector<Vector2>>>
alignment_pieces_with_curvature(const std::vector<AlignmentElement> &elements) {
    std::vector<AlignmentWalkNote> notes;
    auto raw = alignment_raw_polylines(elements, notes);
    std::vector<std::tuple<AlignmentPiece, double, std::vector<Vector2>>> out;
    for (auto &run : raw) {
        for (auto [a, b] : alignment_split_points(run)) {
            std::vector<Vector2> points(run.begin() + static_cast<std::ptrdiff_t>(a),
                                       run.begin() + static_cast<std::ptrdiff_t>(b));
            auto first = points.front(), last = points.back();
            auto before_last = points[points.size() >= 2 ? points.size() - 2 : 0];
            AlignmentPiece kind = AlignmentPiece::Line;
            double k = 0;
            for (auto &n : notes)
                if (alignment_same(n.first, first) && (alignment_same(n.last, last) || alignment_same(n.last, before_last))) {
                    kind = n.kind;
                    k = n.curvature;
                    break;
                }
            out.push_back({kind, k, std::move(points)});
        }
    }
    return out;
}
std::vector<std::pair<AlignmentPiece, std::vector<Vector2>>>
alignment_pieces_by_kind(const std::vector<AlignmentElement> &elements) {
    std::vector<std::pair<AlignmentPiece, std::vector<Vector2>>> out;
    for (auto &[kind, k, points] : alignment_pieces_with_curvature(elements)) {
        (void)k;
        out.push_back({kind, std::move(points)});
    }
    return out;
}
std::vector<std::vector<Vector2>> alignment_solved_polylines(const std::vector<AlignmentElement> &elements) {
    std::vector<AlignmentWalkNote> notes;
    auto out = alignment_raw_polylines(elements, notes);
    for (auto &points : out)
        points.erase(std::unique(points.begin(), points.end(),
                                 [](Vector2 a, Vector2 b) { return a.distance(b) < 1e-9; }),
                     points.end());
    return out;
}
std::vector<AlignmentElement> alignment_build_elements(const std::vector<Vector2> &vertices,
                                                       const std::vector<VertexCurve> &curves) {
    auto n = vertices.size() - 1;
    auto next = static_cast<std::int16_t>(n);
    std::vector<std::optional<std::int16_t>> ids(curves.size());
    for (std::size_t j = 0; j < curves.size(); ++j)
        if (curves[j].kind != VertexCurve::Kind::None)
            ids[j] = ++next;
    std::vector<AlignmentElement> out;
    for (std::size_t i = 0; i < n; ++i) {
        auto id = static_cast<std::int16_t>(i + 1);
        std::int16_t previous = 0, nxt = 0;
        if (i != 0)
            previous = ids[i - 1].value_or(id - 1);
        if (i + 1 != n)
            nxt = ids[i].value_or(id + 1);
        out.push_back({id, previous, nxt, AlignmentElementKind::fixed_line(vertices[i], vertices[i + 1])});
    }
    for (std::size_t j = 0; j < curves.size(); ++j) {
        if (!ids[j])
            continue;
        AlignmentElementKind kind;
        if (curves[j].kind == VertexCurve::Kind::Arc)
            kind = AlignmentElementKind::free_arc(curves[j].radius);
        else
            kind = AlignmentElementKind::free_scs(curves[j].spiral_in, curves[j].radius, curves[j].spiral_out);
        out.push_back({*ids[j], static_cast<std::int16_t>(j + 1), static_cast<std::int16_t>(j + 2), kind});
    }
    return out;
}
bool alignment_curve_valid(const VertexCurve &c) {
    auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    if (c.kind == VertexCurve::Kind::None)
        return true;
    if (c.kind == VertexCurve::Kind::Arc)
        return positive(c.radius);
    return positive(c.spiral_in) && positive(c.radius) && positive(c.spiral_out);
}
std::optional<std::vector<AlignmentPathPoint>> alignment_solve_path(const std::vector<Vector2> &vertices,
                                                                    const std::vector<VertexCurve> &curves,
                                                                    const std::vector<AlignmentElement> &elements) {
    for (std::size_t j = 0; j < curves.size(); ++j) {
        auto u = vertices[j + 1] - vertices[j], v = vertices[j + 2] - vertices[j + 1];
        auto turn = std::atan2(u.cross(v), u.dot(v));
        if (!alignment_curve_valid(curves[j]))
            return std::nullopt;
        if (curves[j].kind != VertexCurve::Kind::None &&
            (std::abs(turn) <= 1e-9 || std::abs(turn) >= ALIGNMENT_PI - 1e-9))
            return std::nullopt;
    }
    auto pieces = alignment_pieces_with_curvature(elements);
    std::size_t curve_pieces = 0;
    for (auto &c : curves) {
        if (c.kind == VertexCurve::Kind::Arc)
            ++curve_pieces;
        else if (c.kind == VertexCurve::Kind::SpiralCurveSpiral)
            curve_pieces += 3;
    }
    double scale = 1;
    for (auto &p : vertices)
        scale = std::max(scale, std::max(std::abs(p.x), std::abs(p.y)));
    std::vector<AlignmentPathPoint> path;
    std::size_t line = 0;
    for (auto &[kind, k, points] : pieces) {
        if (points.empty())
            return std::nullopt;
        auto first = points.front(), last = points.back();
        if (kind == AlignmentPiece::Line) {
            if (line + 1 >= vertices.size())
                return std::nullopt;
            auto along = vertices[line + 1] - vertices[line];
            if (along.dot(last - first) < -1e-9 * scale * along.length())
                return std::nullopt;
            ++line;
            if (first.distance(last) > 1e-12 * scale)
                path.push_back({first, std::nullopt});
        } else if (kind == AlignmentPiece::Curve) {
            auto sweep = 2.0 * std::asin(std::min(first.distance(last) * std::abs(k) / 2.0, 1.0)) *
                         (k > 0 ? 1.0 : k < 0 ? -1.0 : 0.0);
            path.push_back({first, std::tan(sweep / 4.0)});
        } else {
            for (std::size_t i = 0; i + 1 < points.size(); ++i)
                path.push_back({points[i], std::nullopt});
        }
    }
    if (pieces.size() != vertices.size() - 1 + curve_pieces || line != vertices.size() - 1)
        return std::nullopt;
    path.push_back({vertices.back(), std::nullopt});
    return path;
}
void write_alignment_curve_head(ArchWriter &w, std::int32_t kind, std::int32_t constraint) {
    w.main.bit_long(kind);
    w.bs(2);
    w.main.bits(1, 0);
    w.main.bits(1, 0);
    w.bd(-1);
    w.bd(-1);
    w.bs(1);
    w.bd(-1);
    w.main.bit_long(constraint);
}
void write_alignment_ids(ArchWriter &w, const AlignmentElement &e) {
    w.bs(e.id);
    w.bs(e.previous);
    w.bs(e.next);
}
std::string encode_alignment_entity(int type, std::uint64_t id, std::uint64_t layer, std::string_view name,
                                    const std::vector<AlignmentElement> &elements, std::uint64_t style,
                                    std::uint64_t companion, const std::vector<AlignmentProfileRef> &profiles,
                                    const char *page) {
    ArchWriter w;
    w.v = 1032;
    w.page = page;
    auto split = arch_entity_common(w, type, id, layer, {});
    for (auto v : {80, 0, 80, 80, 0, 0x10025F04})
        w.main.bit_long(v);
    write_bit_run(w.main, "010101");
    write_byte(w.main, 1);
    w.main.bit_long(ALIGNMENT_STAMP);
    w.main.bit_long(2);
    w.main.bit_long(0);
    w.main.bit_long(0);
    bl_byte(w.main, 0);
    write_bit_run(w.main, std::string(65, '0'));
    w.bd(2.25);
    write_bit_run(w.main, "0111010");
    w.main.bit_long(1);
    w.bd(0);
    w.bd(0);
    write_bit_run(w.main, "0000011");
    w.bd(0);
    w.main.bits(1, 0);
    w.main.bit_long(1);
    w.main.bit_long(static_cast<std::int32_t>(elements.size()));
    std::size_t curves = 0;
    for (auto &e : elements) {
        if (e.kind.tag == AlignmentElementKind::Tag::FixedLine) {
            w.main.bit_long(ALIGNMENT_LINE);
            w.bs(2);
            w.main.bits(1, 0);
            w.bs(0);
            write_alignment_ids(w, e);
            for (auto p : {e.kind.start, e.kind.end}) {
                raw_double(w.main, p.x);
                raw_double(w.main, p.y);
            }
        } else if (e.kind.tag == AlignmentElementKind::Tag::FreeArc) {
            ++curves;
            write_alignment_curve_head(w, ALIGNMENT_ARC, ALIGNMENT_BETWEEN_LINES);
            w.main.bits(1, 0);
            write_alignment_ids(w, e);
            w.bd(e.kind.radius);
        } else {
            ++curves;
            write_alignment_curve_head(w, ALIGNMENT_SCS, ALIGNMENT_SCS_BETWEEN);
            write_alignment_ids(w, e);
            w.main.bit_long(ALIGNMENT_SPIRAL_DEFINITION);
            w.bd(e.kind.spiral_in);
            w.bd(e.kind.radius);
            w.bd(e.kind.spiral_out);
        }
    }
    auto first_end = 0.0;
    auto pieces = alignment_pieces_with_curvature(elements);
    if (!pieces.empty()) {
        auto &pts = std::get<2>(pieces.front());
        for (std::size_t i = 1; i < pts.size(); ++i)
            first_end += pts[i - 1].distance(pts[i]);
    }
    auto count = elements.size() > 1 ? static_cast<std::int32_t>(elements.size()) : 0;
    auto n = static_cast<std::int32_t>(profiles.size());
    for (auto v : {n, 0, 0, n})
        w.main.bit_long(v);
    for (auto it = profiles.rbegin(); it != profiles.rend(); ++it) {
        for (auto b : it->id)
            write_byte(w.main, b);
        w.main.bits(1, 1);
        for (auto v : {32763, 0, 0, 0})
            w.main.bit_long(v);
        w.bd(first_end);
        for (auto v : {1, 0, count})
            w.main.bit_long(v);
        for (auto v : {0.0, 0.0, first_end, 0.0, 0.0, 0.0})
            raw_double(w.main, v);
    }
    for (auto v : {0, 1, 0})
        w.main.bit_long(v);
    w.bd(100);
    for (auto v : {0, 0, 1, 2, 2, 2, 0, 3})
        w.main.bit_long(v);
    w.main.bits(1, 0);
    w.bs(CIVIL_VERSION);
    w.href(SOFT_POINTER, 0);
    w.href(HARD_POINTER, companion);
    w.href(HARD_POINTER, companion);
    w.href(SOFT_POINTER, 0);
    w.href(HARD_POINTER, style);
    w.href(HARD_POINTER, 0);
    for (auto &p : profiles)
        w.href(SOFT_POINTER, p.handle);
    w.str("AeccImpAlignment");
    w.str("");
    w.str("");
    w.str(name);
    for (std::size_t i = 0; i < 5 + 3 * curves + 2 * profiles.size(); ++i)
        w.str("");
    return finish_arch(w, split);
}
std::optional<std::vector<AlignmentProfileRef>> read_written_alignment_profiles(DwgBitReader &main,
                                                                               const std::vector<std::uint64_t> &handles) {
    auto n = main.bit_long();
    if (n < 0 || n > 1000000 || main.bit_long() != 0 || main.bit_long() != 0 || main.bit_long() != n)
        return std::nullopt;
    if (handles.size() != static_cast<std::size_t>(n))
        return std::nullopt;
    std::vector<std::array<std::uint8_t, 16>> ids(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        for (auto &b : ids[static_cast<std::size_t>(i)])
            b = static_cast<std::uint8_t>(main.bits(8));
        if (!main.bits(1))
            return std::nullopt;
        if (main.bit_long() != 32763)
            return std::nullopt;
        for (int k = 0; k < 3; ++k)
            main.bit_long();
        main.bit_double();
        main.bit_long();
        main.bit_long();
        main.bit_long();
        for (int k = 0; k < 6; ++k)
            main.raw_double();
    }
    std::vector<AlignmentProfileRef> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        out.push_back({handles[static_cast<std::size_t>(i)], ids[static_cast<std::size_t>(n - 1 - i)]});
    return out;
}
std::optional<AlignmentElement> read_written_alignment_element(DwgBitReader &main) {
    auto type = main.bit_long();
    AlignmentElement e;
    if (type == ALIGNMENT_LINE) {
        if (main.bit_short() != 2)
            return std::nullopt;
        main.bits(1);
        if (main.bit_short() != 0)
            return std::nullopt;
        e.id = main.bit_short();
        e.previous = main.bit_short();
        e.next = main.bit_short();
        Vector2 start{main.raw_double(), main.raw_double()}, end{main.raw_double(), main.raw_double()};
        e.kind = AlignmentElementKind::fixed_line(start, end);
        return e;
    }
    if (type == ALIGNMENT_ARC) {
        if (main.bit_short() != 2)
            return std::nullopt;
        main.bits(1);
        main.bits(1);
        main.bit_double();
        main.bit_double();
        main.bit_short();
        main.bit_double();
        main.bit_long();
        main.bits(1);
        e.id = main.bit_short();
        e.previous = main.bit_short();
        e.next = main.bit_short();
        e.kind = AlignmentElementKind::free_arc(main.bit_double());
        return e;
    }
    if (type == ALIGNMENT_SCS) {
        if (main.bit_short() != 2)
            return std::nullopt;
        main.bits(1);
        main.bits(1);
        main.bit_double();
        main.bit_double();
        main.bit_short();
        main.bit_double();
        main.bit_long();
        e.id = main.bit_short();
        e.previous = main.bit_short();
        e.next = main.bit_short();
        main.bit_long();
        auto in = main.bit_double(), radius = main.bit_double(), out = main.bit_double();
        e.kind = AlignmentElementKind::free_scs(in, radius, out);
        return e;
    }
    return std::nullopt;
}
std::optional<std::vector<AlignmentElement>> read_written_alignment_elements(DwgBitReader &main) {
    auto n = main.bit_long();
    if (n < 1 || n > 1000000)
        return std::nullopt;
    std::vector<AlignmentElement> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        auto e = read_written_alignment_element(main);
        if (!e)
            return std::nullopt;
        out.push_back(*e);
    }
    return out;
}
