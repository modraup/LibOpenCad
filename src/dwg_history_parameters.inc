// SPDX-License-Identifier: MPL-2.0
// acadrust object_reader/dynamic_block.rs: native history operation payloads.
std::optional<EmbeddedProfile> read_history_profile(Read &r, DxfVersion v) {
    const auto begin = r.r.position();
    auto type = r.r.bit_long();
    if (!type)
        return {};
    if (type == 37 || type == 38 || type == 39) {
        auto checkpoint = r;
        try {
            if (r.b())
                throw std::invalid_argument("Not an inline history body");
            (void)r.b();
            if (r.r.bit_short() != 2)
                throw std::invalid_argument("Not SAB");
            auto a = read_acis_payload(r, 2);
            if (type == 37) {
                ProfileRegion g;
                g.geometry = std::move(a);
                return g;
            }
            return ProfileBody{type, acis::SabReader::read(a.sab).to_sat_string()};
        } catch (const std::exception &) {
            r = checkpoint;
        }
    }
    r.r.seek(begin);
    return read_surface_profile(r, v);
}
void read_history_parameters(Read &r, DwgBitReader &refs, DxfVersion v, DwgHistoryRecord &record) {
    auto &o = *record.operation;
    using K = SolidHistoryKind;
    record.parameters_begin_bit = r.r.position();
    if (o.base.kind == K::Brep || o.base.kind == K::Loft) {
        o.operation_major = o.base.major;
        o.operation_minor = o.base.minor;
    } else {
        o.operation_major = r.r.bit_long();
        o.operation_minor = r.r.bit_long();
    }
    switch (o.base.kind) {
    case K::Box:
    case K::Wedge: {
        HistoryBox p;
        p.length = r.d();
        p.width = r.d();
        p.height = r.d();
        o.parameters = p;
        break;
    }
    case K::Sphere: {
        HistorySphere p;
        p.radius = r.d();
        o.parameters = p;
        break;
    }
    case K::Cylinder: {
        HistoryCylinder p;
        p.height = r.d();
        p.major_radius = r.d();
        p.minor_radius = r.d();
        p.x_radius = r.d();
        o.parameters = p;
        break;
    }
    case K::Cone: {
        HistoryCone p;
        p.height = r.d();
        p.base_x_radius = r.d();
        p.base_y_radius = r.d();
        p.top_radius = r.d();
        o.parameters = p;
        break;
    }
    case K::Pyramid: {
        HistoryPyramid p;
        p.height = r.d();
        p.sides = r.r.bit_long();
        p.radius = r.d();
        p.top_radius = r.d();
        o.parameters = p;
        break;
    }
    case K::Torus: {
        HistoryTorus p;
        p.major_radius = r.d();
        p.minor_radius = r.d();
        o.parameters = p;
        break;
    }
    case K::Boolean: {
        HistoryBoolean p;
        p.operation = r.r.bits(8);
        p.first_operand = r.r.bit_long();
        p.second_operand = r.r.bit_long();
        o.parameters = p;
        o.base.boolean_operation = p.operation;
        o.base.first_operand = p.first_operand;
        o.base.second_operand = p.second_operand;
        break;
    }
    case K::Chamfer: {
        HistoryChamfer p;
        p.method = r.r.bit_long();
        p.base_distance = r.d();
        p.other_distance = r.d();
        auto n = r.n();
        for (std::size_t i = 0; i < n; ++i)
            p.edges.push_back(r.r.bit_long());
        p.base_face = r.r.bit_long();
        o.parameters = p;
        break;
    }
    case K::Fillet: {
        HistoryFillet p;
        p.method = r.r.bit_long();
        auto n = r.n();
        for (std::size_t i = 0; i < n; ++i)
            p.edges.push_back(r.r.bit_long());
        for (auto *a : {&p.radii, &p.start_setbacks, &p.end_setbacks}) {
            n = r.n();
            for (std::size_t i = 0; i < n; ++i)
                a->push_back(r.d());
        }
        o.parameters = p;
        break;
    }
    case K::Brep: {
        DwgModeler m;
        RegionDatabase db{refs, record.handle, m, true, false};
        record.brep = read_region(r, v, &db);
        auto &a = record.brep->geometry;
        HistoryBrep p;
        p.sat = a.is_binary ? (a.sab.empty() ? std::string() : acis::SabReader::read(a.sab).to_sat_string())
                            : a.sat;
        o.parameters = std::move(p);
        break;
    }
    case K::Sweep:
    case K::Extrusion: {
        HistorySweep p;
        p.direction = r.p();
        p.draft_angle = r.d();
        p.start_draft_distance = r.d();
        p.end_draft_distance = r.d();
        p.twist_angle = r.d();
        p.scale_factor = r.d();
        p.align_angle = r.d();
        p.has_align_start = r.b();
        p.align_option = std::clamp<int>(r.r.bit_short(), 0, 255);
        p.miter_option = std::clamp<int>(r.r.bit_short(), 0, 255);
        p.align_start = r.b();
        p.bank = r.b();
        p.flags_294_296[0] = r.b();
        p.flags_294_296[1] = r.b();
        record.sweep_vector = r.p();
        p.flags_294_296[2] = r.b();
        p.sweep_entity_transform = read_surface_matrix(r);
        p.path_entity_transform = read_surface_matrix(r);
        record.profiles.profile = read_history_profile(r, v);
        record.profiles.path = read_history_profile(r, v);
        o.parameters = std::move(p);
        break;
    }
    case K::Loft: {
        HistoryLoft p;
        auto n = r.n();
        for (std::size_t i = 0; i < n; ++i) {
            auto e = read_history_profile(r, v);
            if (!e)
                throw std::invalid_argument("Absent history loft section");
            record.profiles.sections.push_back(*e);
        }
        n = r.n();
        for (std::size_t i = 0; i < n; ++i) {
            auto e = read_history_profile(r, v);
            if (!e)
                throw std::invalid_argument("Absent history loft guide");
            record.profiles.guides.push_back(*e);
        }
        record.profiles.path = read_history_profile(r, v);
        p.start_draft_angle = r.d();
        p.end_draft_angle = r.d();
        p.start_magnitude = r.d();
        p.end_magnitude = r.d();
        for (auto &b : p.flags)
            b = r.b();
        auto option = r.r.bit_long();
        if (option < INT16_MIN || option > INT16_MAX)
            throw std::invalid_argument("History loft option exceeds native range");
        p.surface_option = option;
        o.parameters = std::move(p);
        break;
    }
    case K::Revolve: {
        HistoryRevolve p;
        p.axis_point = r.p();
        p.direction = r.p();
        p.revolve_angle = r.d();
        p.start_angle = r.d();
        p.draft_angle = r.d();
        p.field_44 = r.d();
        p.field_45 = r.d();
        p.twist_angle = r.d();
        p.flag_290 = r.b();
        p.close_to_axis = r.b();
        record.profiles.profile = read_history_profile(r, v);
        o.parameters = std::move(p);
        break;
    }
    }
    record.parameters_end_bit = r.r.position();
}
void write_history_profile(Write &w, const std::optional<EmbeddedProfile> &p, DxfVersion v) {
    if (!p) {
        w.w.bit_long(0);
        return;
    }
    write_surface_profile(w, p, v);
}
void write_history_parameters(Write &w, const DwgHistoryRecord &record, DxfVersion v) {
    const auto &o = *record.operation;
    const auto &g = record.profiles;
    using K = SolidHistoryKind;
    if (g.revolve_class_version || g.revolve_entity_id || g.swept_legacy_class_version)
        throw std::invalid_argument("Legacy DXF prefixes are not DWG history inputs");
    auto k = o.base.kind;
    if (k != K::Loft && (!g.sections.empty() || !g.guides.empty()))
        throw std::invalid_argument("Only loft history supports section/guide arrays");
    if (k == K::Loft && g.profile)
        throw std::invalid_argument("Loft requires section arrays");
    if (k != K::Loft && k != K::Sweep && k != K::Extrusion && k != K::Revolve && (g.profile || g.path))
        throw std::invalid_argument("Operation has no embedded profiles");
    if (k == K::Revolve && g.path)
        throw std::invalid_argument("Revolve has no path");
    if (k != K::Brep && k != K::Loft) {
        w.w.bit_long(o.operation_major);
        w.w.bit_long(o.operation_minor);
    }
    switch (k) {
    case K::Box:
    case K::Wedge: {
        auto &p = std::get<HistoryBox>(o.parameters);
        w.d(p.length);
        w.d(p.width);
        w.d(p.height);
        break;
    }
    case K::Sphere:
        w.d(std::get<HistorySphere>(o.parameters).radius);
        break;
    case K::Cylinder: {
        auto &p = std::get<HistoryCylinder>(o.parameters);
        w.d(p.height);
        w.d(p.major_radius);
        w.d(p.minor_radius);
        w.d(p.x_radius);
        break;
    }
    case K::Cone: {
        auto &p = std::get<HistoryCone>(o.parameters);
        w.d(p.height);
        w.d(p.base_x_radius);
        w.d(p.base_y_radius);
        w.d(p.top_radius);
        break;
    }
    case K::Pyramid: {
        auto &p = std::get<HistoryPyramid>(o.parameters);
        w.d(p.height);
        w.w.bit_long(p.sides);
        w.d(p.radius);
        w.d(p.top_radius);
        break;
    }
    case K::Torus: {
        auto &p = std::get<HistoryTorus>(o.parameters);
        w.d(p.major_radius);
        w.d(p.minor_radius);
        break;
    }
    case K::Boolean: {
        auto &p = std::get<HistoryBoolean>(o.parameters);
        w.w.bits(8, p.operation);
        w.w.bit_long(p.first_operand);
        w.w.bit_long(p.second_operand);
        break;
    }
    case K::Fillet: {
        auto &p = std::get<HistoryFillet>(o.parameters);
        w.w.bit_long(p.method);
        w.w.bit_long(count(p.edges.size()));
        for (auto x : p.edges)
            w.w.bit_long(x);
        for (auto *a : {&p.radii, &p.start_setbacks, &p.end_setbacks}) {
            w.w.bit_long(count(a->size()));
            for (auto x : *a)
                w.d(x);
        }
        break;
    }
    case K::Chamfer: {
        auto &p = std::get<HistoryChamfer>(o.parameters);
        w.w.bit_long(p.method);
        w.d(p.base_distance);
        w.d(p.other_distance);
        w.w.bit_long(count(p.edges.size()));
        for (auto x : p.edges)
            w.w.bit_long(x);
        w.w.bit_long(p.base_face);
        break;
    }
    case K::Brep: {
        auto &p = std::get<HistoryBrep>(o.parameters);
        if (record.brep) {
            auto &a = *record.brep;
            if (a.wireframe_present || a.extra || !a.materials.empty() || a.revision.has_guid ||
                a.revision.major || a.revision.minor1 || a.revision.minor2 || a.revision.end_marker ||
                std::any_of(a.revision.bytes.begin(), a.revision.bytes.end(), [](auto x) { return x != 0; }))
                throw std::invalid_argument("History BREP cache/revision editing is unsupported");
        }
        w.b(p.sat.empty());
        if (!p.sat.empty()) {
            w.b(false);
            ProfileAcis a;
            a.is_binary = true;
            a.sab = acis::SabWriter::write(acis::SatDocument::parse(p.sat));
            write_acis_payload(w, a);
        } else {
            // Rust's reader requires the empty COMMON_3DSOLID tail even though
            // its current history writer emits only the leading empty bit.
            w.b(false); // no wireframe
            if (v < DxfVersion::AC1027)
                w.b(true);   // absent secondary body
            w.w.bit_long(0); // no materials
            if (v >= DxfVersion::AC1027) {
                w.b(false);
                w.w.bit_long(0);
                w.w.bit_short(0);
                w.w.bit_short(0);
                for (int i = 0; i < 8; ++i)
                    w.w.bits(8, 0);
                w.w.bit_long(0);
            }
        }
        break;
    }
    case K::Sweep:
    case K::Extrusion: {
        auto &p = std::get<HistorySweep>(o.parameters);
        if (!p.profiles.fields.empty() || p.reference_point.length() != 0)
            throw std::invalid_argument("DXF-only sweep fields cannot be authored as DWG history");
        w.p(p.direction);
        for (auto x : {p.draft_angle, p.start_draft_distance, p.end_draft_distance, p.twist_angle,
                       p.scale_factor, p.align_angle})
            w.d(x);
        w.b(p.has_align_start);
        w.w.bit_short(p.align_option);
        w.w.bit_short(p.miter_option);
        w.b(p.align_start);
        w.b(p.bank);
        w.b(p.flags_294_296[0]);
        w.b(p.flags_294_296[1]);
        w.p(record.sweep_vector);
        w.b(p.flags_294_296[2]);
        write_surface_matrix(w, p.sweep_entity_transform);
        write_surface_matrix(w, p.path_entity_transform);
        write_history_profile(w, g.profile, v);
        write_history_profile(w, g.path, v);
        break;
    }
    case K::Loft: {
        auto &p = std::get<HistoryLoft>(o.parameters);
        if (p.settings || !p.profiles.fields.empty())
            throw std::invalid_argument("Loft metadata requires its dedicated object adapter");
        w.w.bit_long(count(g.sections.size()));
        for (auto &e : g.sections)
            write_history_profile(w, e, v);
        w.w.bit_long(count(g.guides.size()));
        for (auto &e : g.guides)
            write_history_profile(w, e, v);
        write_history_profile(w, g.path, v);
        for (auto x : {p.start_draft_angle, p.end_draft_angle, p.start_magnitude, p.end_magnitude})
            w.d(x);
        for (bool x : p.flags)
            w.b(x);
        w.w.bit_long(p.surface_option);
        break;
    }
    case K::Revolve: {
        auto &p = std::get<HistoryRevolve>(o.parameters);
        if (!p.profiles.fields.empty())
            throw std::invalid_argument("Use typed DWG history profiles");
        w.p(p.axis_point);
        w.p(p.direction);
        for (auto x : {p.revolve_angle, p.start_angle, p.draft_angle, p.field_44, p.field_45, p.twist_angle})
            w.d(x);
        w.b(p.flag_290);
        w.b(p.close_to_axis);
        write_history_profile(w, g.profile, v);
        break;
    }
    }
}
