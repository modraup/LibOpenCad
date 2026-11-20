// SPDX-License-Identifier: MPL-2.0
// Port of acadrust DWG surface reader/writer at 52034a6.
SurfaceMatrix read_surface_matrix(Read &r) {
    SurfaceMatrix m;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            m[col * 4 + row] = r.d();
    return m;
}
void write_surface_matrix(Write &w, const SurfaceMatrix &m) {
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            w.d(m[col * 4 + row]);
}
SurfaceSweepOptions read_surface_options(Read &r) {
    SurfaceSweepOptions o;
    o.draft_angle = r.d();
    o.draft_start_distance = r.d();
    o.draft_end_distance = r.d();
    o.twist_angle = r.d();
    o.scale_factor = r.d();
    o.align_angle = r.d();
    o.is_solid = r.b();
    o.sweep_alignment_flags = r.r.bit_short();
    o.path_flags = r.r.bit_short();
    o.align_start = r.b();
    o.bank = r.b();
    o.base_point_set = r.b();
    o.reference_vector = r.p();
    o.sweep_entity_transform_computed = r.b();
    o.path_entity_transform_computed = r.b();
    o.sweep_entity_transform = read_surface_matrix(r);
    o.path_entity_transform = read_surface_matrix(r);
    return o;
}
void write_surface_options(Write &w, const SurfaceSweepOptions &o) {
    w.d(o.draft_angle);
    w.d(o.draft_start_distance);
    w.d(o.draft_end_distance);
    w.d(o.twist_angle);
    w.d(o.scale_factor);
    w.d(o.align_angle);
    w.b(o.is_solid);
    w.w.bit_short(o.sweep_alignment_flags);
    w.w.bit_short(o.path_flags);
    w.b(o.align_start);
    w.b(o.bank);
    w.b(o.base_point_set);
    w.p(o.reference_vector);
    w.b(o.sweep_entity_transform_computed);
    w.b(o.path_entity_transform_computed);
    write_surface_matrix(w, o.sweep_entity_transform);
    write_surface_matrix(w, o.path_entity_transform);
}
std::optional<EmbeddedProfile> read_surface_profile(Read &r, DxfVersion v) {
    auto type = r.r.bit_long();
    if (type == 15 || type == 16) {
        ProfileBody b;
        b.type_code = type;
        if (!r.b()) {
            (void)r.b();
            auto a = read_acis_payload(r, r.r.bit_short());
            b.sat = a.is_binary ? acis::SabReader::read(a.sab).to_sat_string() : a.sat;
        }
        return b;
    }
    auto bits = r.n(1);
    DwgBitWriter w;
    for (std::size_t i = 0; i < bits; ++i)
        w.bits(1, r.r.bits(1));
    if (!type && !bits)
        return std::nullopt;
    return decode_embedded_profile({type, bits, w.bytes()}, v);
}
void write_surface_profile(Write &w, const std::optional<EmbeddedProfile> &p, DxfVersion v) {
    if (!p) {
        w.w.bit_long(0);
        w.w.bit_long(0);
        return;
    }
    if (auto b = std::get_if<ProfileBody>(&*p)) {
        if (b->type_code != 15 && b->type_code != 16)
            throw std::invalid_argument("DWG surface body profile requires type 15 or 16");
        w.w.bit_long(b->type_code);
        w.b(b->sat.empty());
        if (!b->sat.empty()) {
            w.b(false);
            ProfileAcis a;
            a.is_binary = true;
            a.sab = acis::SabWriter::write(acis::SatDocument::parse(b->sat));
            write_acis_payload(w, a);
        }
        return;
    }
    auto e = encode_embedded_profile(*p, v);
    w.w.bit_long(e.type_code);
    w.w.bit_long(count(e.bytes.size() * 8));
    for (unsigned char c : e.bytes)
        w.w.bits(8, c);
}
void read_surface_parameters(Read &r, DxfVersion v, DwgSurface &s) {
    s.parameters_begin_bit = r.r.position();
    s.u_isolines = r.r.bit_short();
    s.v_isolines = r.r.bit_short();
    auto &p = s.profiles;
    if (s.type == "EXTRUDEDSURFACE") {
        ExtrudedSurfaceDefinition d;
        d.options = read_surface_options(r);
        d.sweep_vector = r.p();
        d.sweep_transform = read_surface_matrix(r);
        p.profile = read_surface_profile(r, v);
        s.definition = d;
    } else if (s.type == "LOFTEDSURFACE") {
        LoftedSurfaceDefinition d;
        d.loft_transform = read_surface_matrix(r);
        auto nc = r.r.bit_short(), ng = r.r.bit_short();
        bool path = r.b();
        if (nc < 0 || ng < 0 || std::size_t(nc) + std::size_t(ng) > (r.limit - r.r.position()) / 4)
            throw std::invalid_argument("Invalid DWG loft profile count");
        d.start_draft_angle = r.d();
        d.end_draft_angle = r.d();
        d.start_draft_magnitude = r.d();
        d.end_draft_magnitude = r.d();
        d.arc_length_parameterization = r.b();
        d.no_twist = r.b();
        d.align_direction = r.b();
        d.simple_surfaces = r.b();
        d.closed_surfaces = r.b();
        d.solid = r.b();
        d.ruled_surface = r.b();
        d.virtual_guide = r.b();
        auto plane = r.r.bit_long();
        if (plane < INT16_MIN || plane > INT16_MAX)
            throw std::invalid_argument("Unsupported loft plane-normal value");
        d.plane_normal_lofting_type = plane;
        for (int i = 0; i < nc; ++i) {
            auto e = read_surface_profile(r, v);
            if (!e)
                throw std::invalid_argument("Absent DWG loft section");
            p.sections.push_back(*e);
        }
        for (int i = 0; i < ng; ++i) {
            auto e = read_surface_profile(r, v);
            if (!e)
                throw std::invalid_argument("Absent DWG loft guide");
            p.guides.push_back(*e);
        }
        if (path)
            p.path = read_surface_profile(r, v);
        s.definition = d;
    } else if (s.type == "REVOLVEDSURFACE") {
        RevolvedSurfaceDefinition d;
        d.draft_angle = r.d();
        d.draft_start_distance = r.d();
        d.draft_end_distance = r.d();
        d.twist_angle = r.d();
        d.solid = r.b();
        d.close_to_axis = r.b();
        d.axis_point = r.p();
        d.axis_vector = r.p();
        d.revolve_angle = r.d();
        d.start_angle = r.d();
        d.entity_transform = read_surface_matrix(r);
        p.profile = read_surface_profile(r, v);
        s.definition = d;
    } else if (s.type == "SWEPTSURFACE") {
        SweptSurfaceDefinition d;
        d.options = read_surface_options(r);
        d.sweep_transform = read_surface_matrix(r);
        d.path_transform = read_surface_matrix(r);
        p.profile = read_surface_profile(r, v);
        p.path = read_surface_profile(r, v);
        s.definition = d;
    } else if (s.type == "NURBSURFACE") {
        NurbSurfaceDefinition d;
        if (v >= DxfVersion::AC1027) {
            d.short_170 = r.r.bit_short();
            d.cv_hull_display = r.b();
            d.u_vector1 = r.p();
            d.v_vector1 = r.p();
            d.u_vector2 = r.p();
            d.v_vector2 = r.p();
        }
        s.definition = d;
    } else if (s.type != "SURFACE" && s.type != "PLANESURFACE")
        throw std::invalid_argument("Unknown DWG surface class");
    if (r.r.position() > r.limit)
        throw std::invalid_argument("DWG surface exceeds main stream");
    s.parameters_end_bit = r.r.position();
}
void write_surface_parameters(Write &w, DxfVersion v, const DwgSurface &s) {
    const auto &p = s.profiles;
    const std::size_t kind = s.type == "EXTRUDEDSURFACE"   ? 1
                             : s.type == "LOFTEDSURFACE"   ? 2
                             : s.type == "REVOLVEDSURFACE" ? 3
                             : s.type == "SWEPTSURFACE"    ? 4
                             : s.type == "NURBSURFACE"     ? 5
                                                           : 0;
    if (s.definition.index() != kind)
        throw std::invalid_argument("DWG surface class and parameter variant disagree");
    if (p.revolve_class_version || p.revolve_entity_id || p.swept_legacy_class_version)
        throw std::invalid_argument("Legacy DXF profile prefixes have no DWG representation");
    if (s.type != "LOFTEDSURFACE" && (!p.sections.empty() || !p.guides.empty()))
        throw std::invalid_argument("Only lofted surfaces accept section and guide arrays");
    if ((s.type == "SURFACE" || s.type == "PLANESURFACE" || s.type == "NURBSURFACE") && (p.profile || p.path))
        throw std::invalid_argument("Surface class does not store profiles");
    if ((s.type == "EXTRUDEDSURFACE" || s.type == "REVOLVEDSURFACE") && p.path)
        throw std::invalid_argument("Surface class does not store a path");
    if (s.type == "LOFTEDSURFACE" && p.profile)
        throw std::invalid_argument("Loft uses section arrays, not a single profile");
    w.w.bit_short(s.u_isolines);
    w.w.bit_short(s.v_isolines);
    if (s.type == "EXTRUDEDSURFACE") {
        const auto &d = std::get<ExtrudedSurfaceDefinition>(s.definition);
        write_surface_options(w, d.options);
        w.p(d.sweep_vector);
        write_surface_matrix(w, d.sweep_transform);
        write_surface_profile(w, p.profile, v);
    } else if (s.type == "LOFTEDSURFACE") {
        const auto &d = std::get<LoftedSurfaceDefinition>(s.definition);
        write_surface_matrix(w, d.loft_transform);
        if (p.sections.size() > 32767 || p.guides.size() > 32767)
            throw std::length_error("DWG loft profile count exceeds BS range");
        w.w.bit_short(p.sections.size());
        w.w.bit_short(p.guides.size());
        w.b(bool(p.path));
        w.d(d.start_draft_angle);
        w.d(d.end_draft_angle);
        w.d(d.start_draft_magnitude);
        w.d(d.end_draft_magnitude);
        w.b(d.arc_length_parameterization);
        w.b(d.no_twist);
        w.b(d.align_direction);
        w.b(d.simple_surfaces);
        w.b(d.closed_surfaces);
        w.b(d.solid);
        w.b(d.ruled_surface);
        w.b(d.virtual_guide);
        w.w.bit_long(d.plane_normal_lofting_type);
        for (auto &e : p.sections)
            write_surface_profile(w, e, v);
        for (auto &e : p.guides)
            write_surface_profile(w, e, v);
        if (p.path)
            write_surface_profile(w, p.path, v);
    } else if (s.type == "REVOLVEDSURFACE") {
        const auto &d = std::get<RevolvedSurfaceDefinition>(s.definition);
        w.d(d.draft_angle);
        w.d(d.draft_start_distance);
        w.d(d.draft_end_distance);
        w.d(d.twist_angle);
        w.b(d.solid);
        w.b(d.close_to_axis);
        w.p(d.axis_point);
        w.p(d.axis_vector);
        w.d(d.revolve_angle);
        w.d(d.start_angle);
        write_surface_matrix(w, d.entity_transform);
        write_surface_profile(w, p.profile, v);
    } else if (s.type == "SWEPTSURFACE") {
        const auto &d = std::get<SweptSurfaceDefinition>(s.definition);
        write_surface_options(w, d.options);
        write_surface_matrix(w, d.sweep_transform);
        write_surface_matrix(w, d.path_transform);
        write_surface_profile(w, p.profile, v);
        write_surface_profile(w, p.path, v);
    } else if (s.type == "NURBSURFACE") {
        const auto &d = std::get<NurbSurfaceDefinition>(s.definition);
        if (v >= DxfVersion::AC1027) {
            w.w.bit_short(d.short_170);
            w.b(d.cv_hull_display);
            w.p(d.u_vector1);
            w.p(d.v_vector1);
            w.p(d.u_vector2);
            w.p(d.v_vector2);
        } else if (d.short_170 || d.cv_hull_display || d.u_vector1.length() != 0 ||
                   d.v_vector1.length() != 0 || d.u_vector2.length() != 0 || d.v_vector2.length() != 0)
            throw std::invalid_argument("NURBSURFACE control-hull fields require R2013+");
    } else if (s.type == "SURFACE" || s.type == "PLANESURFACE") {
        if (!std::holds_alternative<std::monostate>(s.definition))
            throw std::invalid_argument("Unexpected generic surface parameters");
    } else
        throw std::invalid_argument("Unknown DWG surface class");
}
