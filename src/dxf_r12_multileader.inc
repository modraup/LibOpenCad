// SPDX-License-Identifier: MPL-2.0
// acadrust entities/modification/explode/leaders.rs::explode_multileader,
// as dispatched by R12Writer::write_exploded (flatten rather than native EXPLODE).
std::size_t R12Exporter::multileader(const DxfMultiLeader &r, bool paper) {
    const auto ml = r.definition();
    const auto &ctx = ml.context;
    // Read only the entity's common properties, never nested branch/style colors.
    Fields props{{8, r.layer()}, {6, r.linetype()}};
    const auto color = r.color();
    if (auto rgb = color.to_true_color_value())
        props.push_back({420, std::to_string(*rgb)});
    else if (auto index = color.index())
        props.push_back({62, std::to_string(*index)});
    std::size_t pieces = 0;
    auto loss = [&](const char *code, const char *message) { issue(R12IssueKind::Loss, code, &r, message); };
    auto primitive = [&](const char *type) {
        pair(0, type);
        common(r, props, paper);
        ++pieces;
    };
    auto line = [&](Point3 a, Point3 b) {
        primitive("LINE");
        point(10, a);
        point(11, b);
    };
    loss("multileader-decomposed", "MULTILEADER style, overrides and editable branch/content relationships "
                                   "replaced by independent R12 primitives");
    if (ml.path_type != 0)
        for (const auto &root : ctx.leader_roots) {
            for (const auto &branch : root.lines) {
                auto path = branch.points;
                if (path.empty())
                    continue;
                if ((path.back() - root.connection_point).length() > std::numeric_limits<double>::epsilon())
                    path.push_back(root.connection_point);
                if (path.size() < 2)
                    continue;
                const auto arrow =
                    (branch.override_flags & 32) ? branch.arrowhead_handle : ml.arrowhead_handle;
                const double size = (branch.override_flags & 16) ? branch.arrowhead_size : ctx.arrowhead_size;
                const auto tip = path[0], delta = tip - path[1];
                const double length = delta.length();
                if (arrow)
                    loss("multileader-custom-arrow-omitted",
                         "The upstream R12 flattener does not instantiate custom arrow blocks");
                else if (size > 0 && length >= size) {
                    const auto dir = delta * (1. / length), base = tip - dir * size,
                               side = Point3{-dir.y, dir.x, 0} * (size / 6.);
                    primitive("SOLID");
                    point(10, tip);
                    point(11, base + side);
                    point(12, base - side);
                    point(13, base - side);
                    path[0] = base;
                } else if (size > 0)
                    loss("multileader-short-arrow-omitted",
                         "First segment is shorter than the requested arrow");
                if (branch.path_type == 2 && path.size() >= 3) {
                    SplineDefinition spline;
                    spline.degree = static_cast<std::int16_t>(std::min<std::size_t>(4, path.size()) - 1);
                    spline.control_points = std::move(path);
                    sampled_polyline(r, props, paper,
                                     detail::approximate_spline(spline, options.max_output_bytes / 32), false,
                                     spline.normal);
                    ++pieces;
                    loss("multileader-spline-sampled", "Curved branch replaced by a sampled polyline");
                    continue;
                }
                // Project breaks onto the trimmed shaft; sort and merge overlapping gaps.
                std::map<std::size_t, std::vector<std::pair<double, double>>> gaps;
                for (const auto &br : branch.breaks) {
                    const auto i = static_cast<std::size_t>(br.segment_index);
                    if (i >= path.size() - 1)
                        continue;
                    const auto d = path[i + 1] - path[i];
                    const double sq = d.length_squared();
                    if (sq <= 1e-20)
                        continue;
                    auto project = [&](Point3 p) { return std::clamp((p - path[i]).dot(d) / sq, 0., 1.); };
                    for (const auto &gap : br.points) {
                        auto a = project(gap.start), b = project(gap.end);
                        if (a > b)
                            std::swap(a, b);
                        if (b - a > 1e-12)
                            gaps[i].emplace_back(a, b);
                    }
                }
                for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                    const auto a = path[i], d = path[i + 1] - a;
                    if (d.length_squared() <= 1e-20)
                        continue;
                    double cursor = 0;
                    if (auto it = gaps.find(i); it != gaps.end()) {
                        std::sort(it->second.begin(), it->second.end());
                        for (auto [lo, hi] : it->second) {
                            if (lo > cursor + 1e-12)
                                line(a + d * cursor, a + d * lo);
                            cursor = std::max(cursor, hi);
                        }
                    }
                    if (cursor < 1. - 1e-12)
                        line(a + d * cursor, path[i + 1]);
                }
            }
            const auto norm = root.direction.length();
            if (ml.enable_dogleg && root.landing_distance > 0 &&
                norm > std::numeric_limits<double>::epsilon())
                line(root.connection_point,
                     root.connection_point + root.direction * (root.landing_distance / norm));
        }
    if (ctx.has_block_contents)
        loss("multileader-block-content-omitted",
             "The upstream R12 flattener omits block annotation content and attributes");
    if (ctx.has_text_contents && !ctx.text_string.empty()) {
        MTextDefinition t;
        t.text = ctx.text_string;
        t.insertion = ctx.text_location;
        t.normal = ctx.text_normal;
        t.height = ctx.text_height;
        t.rectangle_width = ctx.text_width;
        t.rotation = ctx.text_rotation;
        t.line_spacing_factor = ctx.line_spacing_factor;
        for (const auto &piece : detail::approximate_mtext(t, options.max_output_bytes / 32)) {
            text_piece(r, props, paper, piece);
            ++pieces;
        }
        loss("multileader-text-decomposed",
             "Annotation written as independent TEXT using approximate MTEXT layout");
    }
    return pieces;
}
