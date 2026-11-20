Vector3 dehomog(const std::array<double, 4> &p) {
    auto w = p[3] == 0.0 ? 1.0 : p[3];
    return {p[0] / w, p[1] / w, p[2] / w};
}

std::pair<std::vector<std::array<double, 4>>, std::vector<std::array<double, 4>>>
bezier_split(const std::vector<std::array<double, 4>> &cp, double t) {
    auto work = cp;
    auto n = work.size();
    std::vector<std::array<double, 4>> left = {work[0]}, right = {work[n - 1]};
    for (std::size_t r = 1; r < n; ++r) {
        for (std::size_t j = 0; j < n - r; ++j)
            for (int k = 0; k < 4; ++k)
                work[j][k] = (1.0 - t) * work[j][k] + t * work[j + 1][k];
        left.push_back(work[0]);
        right.push_back(work[n - 1 - r]);
    }
    std::reverse(right.begin(), right.end());
    return {left, right};
}

std::vector<CountPiece> spline_beziers(std::size_t degree, const std::vector<double> &knots,
                                      const std::vector<Vector3> &points, const std::vector<double> &weights) {
    auto p = std::max(degree, std::size_t{1});
    if (points.size() <= p || knots.size() != points.size() + p + 1)
        return {};
    auto w_at = [&](std::size_t i) {
        return i < weights.size() && weights[i] > 0.0 ? weights[i] : 1.0;
    };
    std::vector<std::array<double, 4>> cp;
    for (std::size_t i = 0; i < points.size(); ++i) {
        auto w = w_at(i);
        cp.push_back({points[i].x * w, points[i].y * w, points[i].z * w, w});
    }
    auto kv = knots;
    auto a = kv[p], b = kv[cp.size()];
    std::vector<double> distinct;
    for (auto u : kv)
        if (u >= a && u <= b)
            distinct.push_back(u);
    distinct.erase(std::unique(distinct.begin(), distinct.end(),
                               [](double x, double y) { return std::abs(x - y) < 1e-14; }),
                   distinct.end());
    for (auto u : distinct) {
        for (;;) {
            std::size_t mult = 0;
            for (auto k : kv)
                if (std::abs(k - u) < 1e-14)
                    ++mult;
            if (mult >= p)
                break;
            std::size_t k = p;
            for (std::size_t i = 0; i < kv.size(); ++i)
                if (kv[i] <= u + 1e-14)
                    k = i;
            k = std::min(k, cp.size() - 1);
            k = std::max(k, p);
            std::vector<std::array<double, 4>> next;
            next.reserve(cp.size() + 1);
            for (std::size_t i = 0; i <= cp.size(); ++i) {
                if (i <= k - p)
                    next.push_back(cp[i]);
                else if (i > k)
                    next.push_back(cp[i - 1]);
                else {
                    auto den = kv[i + p] - kv[i];
                    auto al = std::abs(den) < 1e-15 ? 0.0 : (u - kv[i]) / den;
                    std::array<double, 4> q{};
                    for (int c = 0; c < 4; ++c)
                        q[c] = al * cp[i][c] + (1.0 - al) * cp[i - 1][c];
                    next.push_back(q);
                }
            }
            kv.insert(kv.begin() + static_cast<std::ptrdiff_t>(k + 1), u);
            cp = std::move(next);
        }
    }
    std::vector<CountPiece> out;
    for (std::size_t k = p; k < cp.size(); ++k) {
        if (kv[k + 1] - kv[k] > 1e-14 && kv[k] >= a - 1e-14 && kv[k + 1] <= b + 1e-14) {
            CountPiece piece;
            piece.kind = CountPiece::Kind::Bezier;
            piece.bezier.assign(cp.begin() + static_cast<std::ptrdiff_t>(k - p),
                                cp.begin() + static_cast<std::ptrdiff_t>(k + 1));
            out.push_back(std::move(piece));
        }
    }
    return out;
}

std::vector<CountPiece> hatch_pieces(const HatchDefinition &h) {
    const double tau = 2 * std::acos(-1.0);
    auto m = Matrix3::arbitrary_axis(h.normal);
    auto z = h.elevation;
    auto at = [&](double x, double y) { return m * Vector3{x, y, z}; };
    auto span = [&](double a0, double a1) {
        auto s = std::fmod(a1 - a0, tau);
        if (s < 0)
            s += tau;
        if (s < 1e-12)
            return tau;
        return s;
    };
    std::vector<CountPiece> out;
    for (const auto &path : h.paths) {
        for (const auto &edge : path.edges) {
            std::visit(
                [&](const auto &e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, HatchLineEdge>) {
                        out.push_back(CountPiece::segment(at(e.start.x, e.start.y), at(e.end.x, e.end.y)));
                    } else if constexpr (std::is_same_v<T, HatchPolylineEdge>) {
                        std::vector<std::array<double, 3>> v;
                        for (auto q : e.vertices)
                            v.push_back({q.x, q.y, q.z});
                        auto pieces = bulge_pieces(v, e.closed, z, m);
                        out.insert(out.end(), pieces.begin(), pieces.end());
                    } else if constexpr (std::is_same_v<T, HatchArcEdge>) {
                        auto s = e.counter_clockwise ? 1.0 : -1.0;
                        out.push_back(CountPiece::conic(at(e.center.x, e.center.y), m * Vector3{e.radius, 0, 0},
                                                        m * Vector3{0, s * e.radius, 0}, e.start_angle,
                                                        e.start_angle + span(e.start_angle, e.end_angle)));
                    } else if constexpr (std::is_same_v<T, HatchEllipseEdge>) {
                        auto s = e.counter_clockwise ? 1.0 : -1.0;
                        auto mx = e.major_axis_endpoint.x, my = e.major_axis_endpoint.y;
                        out.push_back(CountPiece::conic(
                            at(e.center.x, e.center.y), m * Vector3{mx, my, 0},
                            m * Vector3{-my * e.minor_axis_ratio * s, mx * e.minor_axis_ratio * s, 0}, e.start_angle,
                            e.start_angle + span(e.start_angle, e.end_angle)));
                    } else if constexpr (std::is_same_v<T, HatchSplineEdge>) {
                        std::vector<Vector3> pts;
                        for (auto q : e.control_points)
                            pts.push_back(at(q.x, q.y));
                        std::vector<double> weights;
                        if (e.rational)
                            for (auto q : e.control_points)
                                weights.push_back(q.z);
                        auto pieces = spline_beziers(static_cast<std::size_t>(std::max<std::int32_t>(e.degree, 1)),
                                                     e.knots, pts, weights);
                        out.insert(out.end(), pieces.begin(), pieces.end());
                    }
                },
                edge);
        }
    }
    return out;
}

std::vector<std::pair<std::vector<Vector3>, std::vector<std::size_t>>> hatch_loops(const HatchDefinition &h) {
    std::vector<std::pair<std::vector<Vector3>, std::vector<std::size_t>>> out;
    const double deg = std::acos(-1.0) / 180.0;
    for (const auto &path : h.paths) {
        HatchDefinition one = h;
        one.paths = {path};
        std::vector<Vector3> pts;
        std::vector<std::size_t> starts;
        for (const auto &piece : hatch_pieces(one)) {
            starts.push_back(pts.size());
            if (piece.kind == CountPiece::Kind::Segment)
                pts.push_back(piece.a);
            else if (piece.kind == CountPiece::Kind::Conic) {
                auto n = static_cast<std::size_t>(std::ceil((piece.t1 - piece.t0) / deg));
                if (n < 2)
                    n = 2;
                for (std::size_t k = 0; k < n; ++k) {
                    auto t = piece.t0 + (piece.t1 - piece.t0) * static_cast<double>(k) / static_cast<double>(n);
                    pts.push_back(piece.c + piece.u * std::cos(t) + piece.v * std::sin(t));
                }
            } else if (!piece.bezier.empty()) {
                for (int k = 0; k < 128; ++k) {
                    auto split = bezier_split(piece.bezier, static_cast<double>(k) / 128.0);
                    pts.push_back(split.first.empty() ? dehomog(piece.bezier.front()) : dehomog(split.first.back()));
                }
            }
        }
        if (pts.size() >= 2)
            out.emplace_back(std::move(pts), std::move(starts));
    }
    return out;
}

std::vector<Vector3> resampled(const std::vector<Vector3> &pts, std::size_t from, std::size_t n) {
    auto m = pts.size();
    std::vector<Vector3> ring;
    ring.reserve(m + 1);
    for (std::size_t i = 0; i <= m; ++i)
        ring.push_back(pts[(from + i) % m]);
    std::vector<double> seg;
    for (std::size_t i = 0; i + 1 < ring.size(); ++i)
        seg.push_back(std::hypot(ring[i + 1].x - ring[i].x, ring[i + 1].y - ring[i].y));
    double total = 0;
    for (auto s : seg)
        total += s;
    std::vector<Vector3> out;
    out.reserve(n);
    std::size_t i = 0;
    double done = 0;
    for (std::size_t k = 0; k < n; ++k) {
        auto want = total * static_cast<double>(k) / static_cast<double>(n);
        while (i + 1 < seg.size() && done + seg[i] < want) {
            done += seg[i];
            ++i;
        }
        auto f = seg[i] > 0.0 ? (want - done) / seg[i] : 0.0;
        out.push_back(ring[i] + (ring[i + 1] - ring[i]) * f);
    }
    return out;
}

double distance_to_ring(Vector3 p, const std::vector<Vector3> &pts) {
    auto m = pts.size();
    auto best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < m; ++i) {
        auto a = pts[i], b = pts[(i + 1) % m];
        auto d = b - a;
        auto l2 = d.x * d.x + d.y * d.y;
        auto t = l2 > 0.0 ? std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / l2, 0.0, 1.0) : 0.0;
        auto q = a + d * t;
        best = std::min(best, std::hypot(p.x - q.x, p.y - q.y));
    }
    return best;
}

struct HatchSimilarity {
    bool mirror = false;
    Vector3 from{}, to{};
    double s = 1, c = 1, sn = 0;
    Vector3 linear(Vector3 v) const {
        auto y = mirror ? -v.y : v.y;
        return {s * (c * v.x - sn * y), s * (sn * v.x + c * y), 0};
    }
    Vector3 apply(Vector3 p) const {
        auto m = [&](Vector3 q) { return mirror ? Vector3{q.x, -q.y, q.z} : q; };
        auto d = m(p) - m(from);
        return to + Vector3{s * (c * d.x - sn * d.y), s * (sn * d.x + c * d.y), 0};
    }
};

bool same_pattern(const HatchDefinition &a, const HatchDefinition &b, const HatchSimilarity &t) {
    const auto &pa = a.pattern_lines;
    const auto &pb = b.pattern_lines;
    if (pa.size() != pb.size())
        return false;
    auto near = [](double x, double y, double scale) { return std::abs(x - y) <= 1e-6 * std::max(scale, 1.0); };
    auto integral = [](double v) { return std::abs(v - std::round(v)) < 1e-6; };
    for (std::size_t i = 0; i < pa.size(); ++i) {
        const auto &la = pa[i];
        const auto &lb = pb[i];
        Vector3 dir_a{std::cos(la.angle), std::sin(la.angle), 0};
        auto dir_b = t.linear(Vector3{std::cos(lb.angle), std::sin(lb.angle), 0});
        auto len = std::hypot(dir_b.x, dir_b.y);
        if (len <= 0.0)
            return false;
        dir_b = dir_b * (1.0 / len);
        if (std::abs(dir_a.x * dir_b.y - dir_a.y * dir_b.x) > 1e-6)
            return false;
        Vector3 nrm{-dir_a.y, dir_a.x, 0};
        Vector3 oa{la.offset.x, la.offset.y, 0};
        auto ob = t.linear(Vector3{lb.offset.x, lb.offset.y, 0});
        auto spacing = oa.x * nrm.x + oa.y * nrm.y;
        if (!near(std::abs(spacing), std::abs(ob.x * nrm.x + ob.y * nrm.y), std::abs(spacing)))
            return false;
        if (la.dash_lengths.size() != lb.dash_lengths.size())
            return false;
        for (std::size_t d = 0; d < la.dash_lengths.size(); ++d)
            if (!near(la.dash_lengths[d], lb.dash_lengths[d] * t.s, std::abs(la.dash_lengths[d])))
                return false;
        double period = 0;
        for (auto dash : la.dash_lengths)
            period += std::abs(dash);
        auto base_b = t.apply(Vector3{lb.base_point.x, lb.base_point.y, 0});
        auto delta = base_b - Vector3{la.base_point.x, la.base_point.y, 0};
        if (std::abs(spacing) < 1e-12)
            continue;
        auto k = (delta.x * nrm.x + delta.y * nrm.y) / spacing;
        if (!integral(k))
            return false;
        auto r = delta - oa * std::round(k);
        if (!(period < 1e-12 || integral((r.x * dir_a.x + r.y * dir_a.y) / period)))
            return false;
    }
    return true;
}

bool same_hatch(const HatchDefinition &a, const HatchDefinition &b) {
    constexpr std::size_t n = 96;
    auto la = hatch_loops(a);
    auto lb = hatch_loops(b);
    if (la.size() != lb.size() || la.empty() || a.solid != b.solid || !ascii_ieq(a.pattern_name, b.pattern_name))
        return false;
    auto ra = resampled(la[0].first, 0, n);
    double size = 0;
    for (const auto &p : ra)
        size = std::max(size, std::hypot(p.x - ra[0].x, p.y - ra[0].y));
    if (size <= 0.0)
        return false;
    auto tol = size * 1e-5;
    bool mirrors[2] = {false, true};
    int mirror_count = a.solid ? 2 : 1;
    for (int mi = 0; mi < mirror_count; ++mi) {
        auto mirror = mirrors[mi];
        for (auto start : lb[0].second) {
            auto rb = resampled(lb[0].first, start, n);
            auto mpt = [&](Vector3 q) { return mirror ? Vector3{q.x, -q.y, q.z} : q; };
            auto va = ra[n / 2] - ra[0];
            auto vb = mpt(rb[n / 2]) - mpt(rb[0]);
            auto na = std::hypot(va.x, va.y), nb = std::hypot(vb.x, vb.y);
            if (nb <= 0.0 || na <= 0.0)
                continue;
            auto ang = std::atan2(va.y, va.x) - std::atan2(vb.y, vb.x);
            HatchSimilarity t{mirror, rb[0], ra[0], na / nb, std::cos(ang), std::sin(ang)};
            bool fits = true;
            for (std::size_t i = 0; i < la.size(); ++i) {
                std::vector<Vector3> tb;
                for (auto p : lb[i].first)
                    tb.push_back(t.apply(p));
                for (auto p : resampled(la[i].first, 0, n))
                    if (distance_to_ring(p, tb) > tol) {
                        fits = false;
                        break;
                    }
                if (!fits)
                    break;
                for (auto p : resampled(tb, 0, n))
                    if (distance_to_ring(p, la[i].first) > tol) {
                        fits = false;
                        break;
                    }
                if (!fits)
                    break;
            }
            if (fits && (a.solid || same_pattern(a, b, t)))
                return true;
        }
    }
    return false;
}
