// SPDX-License-Identifier: MPL-2.0
// acadrust: R12Writer::write_exploded -> EntityType::flatten -> explode_mline.
// Geometry depends on the stored style NAME/count; resolve the object for diagnostics only.
std::size_t R12Exporter::mline(const DxfMLine &r, const Fields &f, bool paper) {
    const auto m = r.definition();
    const auto n = m.vertices.size();
    if (n < 2 || n > 32767 || integer_value(f, 72, static_cast<int>(n)) != static_cast<int>(n))
        throw std::invalid_argument("Invalid MLINE vertex count");
    const bool closed = (m.flags & 2) != 0,
               standard = upper(m.style_name) == "STANDARD" && m.style_element_count >= 2;
    auto loss = [&](const char *code, const std::string &message) {
        issue(R12IssueKind::Loss, code, &r, message);
    };
    loss("mline-decomposed",
         "Editable MLINE and its style association replaced by independent LINE segments");
    if (!standard)
        loss("mline-centerline", "Style " + m.style_name + " is flattened to a single zero-offset path");
    const auto *style = doc.get_mline_style(m.style_name);
    if (m.style_handle && (!style || doc.get_record(value(f, 340)) != static_cast<const DxfRecord *>(style)))
        throw std::invalid_argument("Unresolved or conflicting MLINESTYLE reference");
    if (style) {
        const auto s = style->definition();
        if (!closed && (s.flags & (16 | 32 | 64)) && !(m.flags & 4))
            loss("mline-start-cap-omitted", "Configured start cap is not drawn");
        if (!closed && (s.flags & (256 | 512 | 1024)) && !(m.flags & 8))
            loss("mline-end-cap-omitted", "Configured end cap is not drawn");
        if (s.flags & 1)
            loss("mline-fill-omitted", "Style fill is not drawn");
        if ((s.flags & 2) && (n > 2))
            loss("mline-joints-omitted",
                 "Additional style joint lines are not drawn; offset segments still meet at each miter");
        if (!standard || s.elements.size() != 2 || s.elements[0].offset != .5 ||
            s.elements[1].offset != -.5 ||
            std::any_of(s.elements.begin(), s.elements.end(), [](const auto &e) {
                return e.color.kind() != Color::Kind::ByLayer || upper(e.linetype) != "BYLAYER";
            }))
            loss("mline-element-style-lost", "Stored element offsets, colors and linetypes are not applied; "
                                             "lines inherit entity properties");
    } else
        loss("mline-style-unavailable", "No style definition to inspect for cap, fill or element settings");
    if (std::any_of(m.vertices.begin(), m.vertices.end(), [](const auto &v) {
            return std::any_of(v.segments.begin(), v.segments.end(), [](const auto &s) {
                return !s.parameters.empty() || !s.area_fill_parameters.empty();
            });
        }))
        loss("mline-parameters-lost", "Per-element segment and fill parameters are not applied");
    Fields props;
    for (const auto &p : f)
        if (p.code == 6 || p.code == 8 || p.code == 62 || p.code == 420)
            props.push_back(p);
    const double half = .5 * m.scale_factor;
    const double offsets[] = {standard ? half : 0., -half};
    auto emit = [&](std::size_t i, std::size_t j, double offset) {
        pair(0, "LINE");
        common(r, props, paper);
        point(10, m.vertices[i].position + m.vertices[i].miter.normalize() * offset);
        point(11, m.vertices[j].position + m.vertices[j].miter.normalize() * offset);
        if (std::abs(m.normal.x) > 1e-12 || std::abs(m.normal.y) > 1e-12 || std::abs(m.normal.z - 1) > 1e-12)
            point(210, m.normal);
    };
    const unsigned tracks = standard ? 2 : 1;
    for (unsigned t = 0; t < tracks; ++t)
        for (std::size_t j = 1; j < n; ++j)
            emit(j - 1, j, offsets[t]);
    // Rust appends both closing edges after both open chains.
    if (closed && n > 2)
        for (unsigned t = 0; t < tracks; ++t)
            emit(n - 1, 0, offsets[t]);
    omitted_fields(r, f, {8,  6,  62, 420, 67, 2,  340, 40, 71, 72,  73,  11,
                          21, 31, 13, 23,  33, 74, 75,  41, 42, 210, 220, 230});
    return tracks * (n - 1 + (closed && n > 2));
}
