// SPDX-License-Identifier: MPL-2.0
// Native DWG associative decode. Port of acadrust object_reader/associative.rs.
struct AssocIO {
    DynamicStream s;
    std::string_view body;
    AssocIO(std::string_view b, const DwgR2000Record &r, std::uint64_t h, unsigned v, const char *page)
        : s(open_dynamic(b, r, h, v, page)), body(b) {}
    bool r2013() const { return s.v >= 1027; }
    std::size_t main_left() const { return s.end > s.main.position() ? s.end - s.main.position() : 0; }
    std::size_t handle_left() const {
        auto n = body.size() * 8;
        return n > s.refs.position() ? n - s.refs.position() : 0;
    }
    std::size_t text_left() const { return s.text ? s.strings.remaining() : 0; }
    std::uint64_t href() { return s.href(); }
    std::string str() { return s.str(); }
    std::string try_str() { return text_left() ? str() : std::string(); }
    std::uint8_t byte() { return static_cast<std::uint8_t>(s.main.bits(8)); }
    bool bit() { return s.main.bits(1); }
    Vector3 vec() { return s.vec(); }
    Vector2 raw2() { return {s.main.raw_double(), s.main.raw_double()}; }
    std::size_t count(std::int32_t n) {
        require(n >= 0 && n <= 100000, "Invalid associative count");
        return std::size_t(n);
    }
    std::size_t count() { return count(s.main.bit_long()); }
    std::vector<std::uint64_t> handles(std::int32_t n) {
        auto c = count(n);
        std::vector<std::uint64_t> out;
        out.reserve(c);
        for (std::size_t i = 0; i < c; ++i)
            out.push_back(href());
        return out;
    }
    bool try_long(std::int32_t &out) {
        if (main_left() < 2)
            return false;
        auto pos = s.main.position();
        auto sel = s.main.bits(2);
        if (sel == 0) {
            if (main_left() < 32) {
                s.main.seek(pos);
                return false;
            }
            out = static_cast<std::int32_t>(static_cast<std::int64_t>(s.main.little_endian(4) ^ 0x80000000u) -
                                            0x80000000LL);
            return true;
        }
        if (sel == 1) {
            if (main_left() < 8) {
                s.main.seek(pos);
                return false;
            }
            out = static_cast<std::int32_t>(s.main.bits(8));
            return true;
        }
        out = 0;
        return true;
    }
    int eval_kind(std::int16_t code) {
        if (code < 0 || code == 5 || code == 105 || (code >= 320 && code <= 369) ||
            (code >= 390 && code <= 399))
            return 6;
        if ((code >= 0 && code <= 9) || (code >= 100 && code <= 102) || (code >= 300 && code <= 309) ||
            (code >= 410 && code <= 419) || (code >= 430 && code <= 439) || (code >= 470 && code <= 479) ||
            code == 999 || (code >= 1000 && code <= 1009))
            return 5;
        if ((code >= 10 && code <= 37) || (code >= 110 && code <= 139) || (code >= 210 && code <= 269) ||
            (code >= 1010 && code <= 1039) || (code >= 1043 && code <= 1069))
            return 0;
        if ((code >= 38 && code <= 59) || (code >= 140 && code <= 149) || (code >= 460 && code <= 469) ||
            (code >= 1040 && code <= 1042))
            return 1;
        if ((code >= 60 && code <= 79) || (code >= 170 && code <= 179) || (code >= 270 && code <= 279) ||
            (code >= 370 && code <= 389) || (code >= 400 && code <= 409) || code == 1070)
            return 3;
        if ((code >= 80 && code <= 99) || (code >= 420 && code <= 429) || (code >= 440 && code <= 459) ||
            code == 1071)
            return 2;
        if (code >= 280 && code <= 289)
            return 4;
        return 0;
    }
    AssocEvalVariant eval() {
        AssocEvalVariant v;
        v.code = s.main.bit_short();
        if (!v.code)
            return v;
        switch (eval_kind(v.code)) {
        case 1:
            v.value = s.main.bit_double();
            break;
        case 2:
            v.value = s.main.bit_long();
            break;
        case 3:
            v.value = s.main.bit_short();
            break;
        case 4:
            v.value = byte();
            break;
        case 5:
            v.value = str();
            break;
        case 6:
            v.value = href();
            break;
        default:
            break;
        }
        return v;
    }
    void skip_acis_bytes(std::size_t n) {
        require(n <= main_left() / 8, "Associative ACIS payload overruns the object");
        for (std::size_t i = 0; i < n; ++i)
            (void)byte();
    }
    std::string read_sat_blocks() {
        std::string encrypted;
        for (;;) {
            auto n = s.main.bit_long();
            require(n >= 0 && std::size_t(n) <= main_left() / 8, "Invalid associative SAT block");
            if (!n)
                break;
            for (std::int32_t i = 0; i < n; ++i)
                encrypted += char(byte());
        }
        return detail::dwg_sat_transport(detail::dwg_sat_cipher(encrypted), false);
    }
    std::vector<std::uint8_t> read_sab() {
        auto start = s.main.position();
        std::vector<std::uint8_t> probe;
        auto available = main_left() / 8;
        probe.reserve(available);
        for (std::size_t i = 0; i < available; ++i)
            probe.push_back(byte());
        std::size_t used = probe.size();
        try {
            used = acis::SabReader::read_with_consumed(probe).second;
        } catch (const std::exception &) {
        }
        require(used && used <= probe.size(), "Invalid associative SAB length");
        s.main.seek(start + used * 8);
        probe.resize(used);
        return probe;
    }
    void skip_acis_payload(int version) {
        if (version == 1)
            (void)read_sat_blocks();
        else if (version == 2)
            (void)read_sab();
        else
            throw std::invalid_argument("Unsupported associative ACIS version");
    }
    void skip_wire(bool wide) {
        (void)byte();
        (void)s.main.bit_long();
        if (wide)
            (void)s.main.bit_long();
        else
            (void)s.main.bit_short();
        (void)s.main.bit_long();
        auto n = count();
        for (std::size_t i = 0; i < n; ++i)
            (void)vec();
        if (bit()) {
            (void)vec();
            (void)vec();
            (void)vec();
            (void)vec();
            (void)s.main.bit_double();
            (void)bit();
            (void)bit();
            (void)bit();
        }
    }
    Vector3 skip_wireframe(bool wide) {
        Vector3 point{};
        if (bit())
            point = vec();
        (void)s.main.bit_long();
        if (bit()) {
            auto n = count();
            for (std::size_t i = 0; i < n; ++i)
                skip_wire(wide);
        }
        auto silhouettes = count();
        for (std::size_t i = 0; i < silhouettes; ++i) {
            if (s.v >= 1024)
                (void)s.main.bit_long_long();
            else
                (void)s.main.bit_long();
            (void)vec();
            (void)vec();
            (void)vec();
            (void)bit();
            if (bit()) {
                auto n = count();
                for (std::size_t j = 0; j < n; ++j)
                    skip_wire(wide);
            }
        }
        return point;
    }
    AssocAsmBodyActionParam asm_body() {
        AssocAsmBodyActionParam p;
        p.single_dependency = single();
        bool empty = bit();
        int acis_version = 0;
        bool sab_consumed = false;
        if (!empty) {
            (void)bit();
            acis_version = s.main.bit_short();
            if (acis_version == 1)
                p.sat_data = read_sat_blocks();
            else if (acis_version == 2) {
                p.is_binary = true;
                auto remaining = main_left() / 8;
                p.sab_data = read_sab();
                sab_consumed = p.sab_data.size() == remaining;
            } else
                throw std::invalid_argument("Unsupported associative ACIS version");
        }
        if (!sab_consumed) {
            bool wireframe = bit();
            if (wireframe) {
                auto start = s.main.position();
                try {
                    p.point_of_reference = skip_wireframe(false);
                } catch (const std::invalid_argument &) {
                    s.main.seek(start);
                    p.point_of_reference = skip_wireframe(true);
                }
            }
            bool extra_gate = wireframe || (s.v < 1027 && (s.v >= 1021 || main_left()));
            if (extra_gate && !bit()) {
                (void)bit();
                skip_acis_payload(s.main.bit_short());
            }
            if (s.v >= 1021) {
                if (acis_version > 1) {
                    auto n = count();
                    for (std::size_t i = 0; i < n; ++i) {
                        (void)s.main.bit_long();
                        (void)s.main.bit_long();
                        (void)href();
                    }
                } else
                    (void)s.main.bit_long();
            }
            if (s.v >= 1027) {
                (void)bit();
                (void)s.main.bit_long();
                (void)s.main.bit_short();
                (void)s.main.bit_short();
                skip_acis_bytes(8);
                (void)s.main.bit_long();
            }
        }
        if (acis_version > 1)
            p.history = href();
        return p;
    }
    AssocValueParam value_param() {
        AssocValueParam p;
        p.class_version = s.main.bit_long();
        p.name = str();
        p.unit_type = s.main.bit_long();
        auto n = count();
        p.variables.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            p.variables.push_back({eval(), href()});
        p.controlled_object_dependency = href();
        return p;
    }
    std::vector<AssocValueParam> values(std::int32_t n) {
        auto c = count(n);
        std::vector<AssocValueParam> out;
        out.reserve(c);
        for (std::size_t i = 0; i < c; ++i)
            out.push_back(value_param());
        return out;
    }
    AssocDependency dependency() {
        AssocDependency d;
        d.class_version = s.main.bit_short();
        d.status = s.main.bit_long();
        d.is_read_dependency = bit();
        d.is_write_dependency = bit();
        d.is_attached_to_object = bit();
        d.is_delegating_to_owning_action = bit();
        d.order = s.main.bit_long();
        d.dependent_on = href();
        if (bit())
            d.name = str();
        d.read_dependency = href();
        d.node = href();
        d.dependency_body = href();
        d.dependency_body_id = s.main.bit_long();
        return d;
    }
    AssocPersistentSubentId persistent_subent() {
        AssocPersistentSubentId id;
        auto compact = r2013() && text_left() == 0 && main_left() >= 12;
        if (compact) {
            id.leading_flag = bit();
            if (!try_long(id.class_code))
                id.class_code = 0;
            static const std::pair<std::int32_t, const char *> names[] = {
                {1, "AcDbAssocSingleEdgePersSubentId"},
                {3, "AcDbAssocEdgePersSubentId"},
                {5, "AcDbAssocAsmBasedEntityPersSubentId"},
            };
            const char *name = "";
            std::optional<std::size_t> fields;
            for (auto &item : names)
                if (item.first == id.class_code) {
                    name = item.second;
                    fields = item.first == 1 ? std::optional<std::size_t>(0)
                             : item.first == 3 ? std::optional<std::size_t>(3)
                                               : std::nullopt;
                }
            id.class_name = name;
            if (fields) {
                id.values.reserve(*fields);
                for (std::size_t i = 0; i < *fields; ++i) {
                    std::int32_t value = 0;
                    if (!try_long(value))
                        break;
                    id.values.push_back(value);
                }
            }
            id.dependent_on_compound_object = main_left() >= 1 && bit();
            return id;
        }
        id.class_name = try_str();
        if (ascii_upper(id.class_name) == "ACDBASSOCSINGLEEDGEPERSSUBENTID")
            id.class_code = 1;
        else if (ascii_upper(id.class_name) == "ACDBASSOCEDGEPERSSUBENTID")
            id.class_code = 3;
        else if (ascii_upper(id.class_name) == "ACDBASSOCASMBASEDENTITYPERSSUBENTID")
            id.class_code = 5;
        id.dependent_on_compound_object = main_left() >= 1 && bit();
        return id;
    }
    AssocAction action() {
        AssocAction a;
        a.class_version = s.main.bit_short();
        a.geometry_status = s.main.bit_long();
        a.owning_network = href();
        a.action_body = href();
        a.action_index = s.main.bit_long();
        a.max_dependency_index = s.main.bit_long();
        auto n = count();
        a.dependencies.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            a.dependencies.push_back({bit(), href()});
        if (a.class_version > 1) {
            s.main.bit_short();
            a.owned_parameters = handles(s.main.bit_long());
            s.main.bit_short();
            a.values = values(s.main.bit_long());
        }
        return a;
    }
    AssocActionParam action_param() {
        AssocActionParam p;
        p.is_r2013 = s.main.bit_short();
        if (r2013())
            p.version = s.main.bit_long();
        p.name = str();
        return p;
    }
    AssocActionBody action_body() { return {s.main.bit_long()}; }
    AssocParamBasedActionBody parameter_body() {
        if (r2013())
            return {};
        AssocParamBasedActionBody b;
        b.version = s.main.bit_long();
        b.minor = s.main.bit_long();
        b.dependencies = handles(s.main.bit_long());
        b.marker = s.main.bit_long();
        auto n = count();
        if (!n) {
            b.empty_value_marker = s.main.bit_long();
            b.dependency = href();
        }
        b.values = values(static_cast<std::int32_t>(n));
        return b;
    }
    AssocSurfaceBody surface_body() {
        AssocSurfaceBody b;
        b.version = s.main.bit_long();
        b.dependency = href();
        b.is_semi_associative = bit();
        b.marker = s.main.bit_long();
        b.is_semi_override = bit();
        b.grip_status = s.main.bit_short();
        return b;
    }
    AssocSurfaceActionBody surface(AssocSurfaceActionKind kind) {
        AssocSurfaceActionBody v;
        v.kind = kind;
        v.action_body = action_body();
        v.parameter_body = parameter_body();
        bool tail = main_left() >= 8 && handle_left() >= 8;
        if (tail)
            v.surface_body = surface_body();
        v.path_status = tail ? s.main.bit_long() : 0;
        if (tail && kind != AssocSurfaceActionKind::Network && kind != AssocSurfaceActionKind::Patch &&
            kind != AssocSurfaceActionKind::EdgeChamfer && kind != AssocSurfaceActionKind::EdgeFillet)
            v.class_version = s.main.bit_long();
        switch (kind) {
        case AssocSurfaceActionKind::Extend:
            v.option = byte();
            break;
        case AssocSurfaceActionKind::Offset:
            v.flags[0] = bit();
            break;
        case AssocSurfaceActionKind::Trim:
            v.flags[0] = bit();
            v.flags[1] = bit();
            v.distance = s.main.bit_double();
            break;
        case AssocSurfaceActionKind::Blend:
            v.flags[0] = bit();
            v.flags[1] = bit();
            v.flags[2] = bit();
            v.status = s.main.bit_short();
            v.flags[3] = bit();
            v.flags[4] = bit();
            v.secondary_status = s.main.bit_short();
            break;
        case AssocSurfaceActionKind::Fillet:
            v.status = s.main.bit_short();
            v.first_point = raw2();
            v.second_point = raw2();
            break;
        default:
            break;
        }
        return v;
    }
    AssocAnnotationBase annotation_base() {
        AssocAnnotationBase b;
        if (s.v >= 1021) {
            b.version = s.main.bit_short();
            b.dependency = href();
        } else {
            b.action_body = action_body();
            b.parameter_body = parameter_body();
        }
        return b;
    }
    AssocAnnotationActionBody annotation(AssocAnnotationKind kind) {
        AssocAnnotationActionBody v;
        v.kind = kind;
        if (kind == AssocAnnotationKind::RestoreEntityState) {
            v.action_body = action_body();
            v.class_version = s.main.bit_long();
            v.entity = href();
            return v;
        }
        v.annotation = annotation_base();
        v.class_version = kind == AssocAnnotationKind::ThreePointAngularDimension ||
                                  kind == AssocAnnotationKind::RotatedDimension
                              ? s.main.bit_short()
                              : s.main.bit_long();
        switch (kind) {
        case AssocAnnotationKind::MLeader: {
            auto n = count();
            v.actions.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                v.actions.push_back({s.main.bit_long(), href()});
            break;
        }
        case AssocAnnotationKind::AlignedDimension:
        case AssocAnnotationKind::OrdinateDimension:
        case AssocAnnotationKind::RotatedDimension:
            v.read_node = href();
            v.dimension_node = href();
            break;
        case AssocAnnotationKind::ThreePointAngularDimension:
            v.read_node = href();
            v.dimension_node = href();
            v.dependency = href();
            break;
        default:
            break;
        }
        return v;
    }
    AssocSingleDependencyActionParam single() {
        return {action_param(), s.main.bit_long(), href(), s.main.bit_long()};
    }
    AssocCompoundActionParam compound(bool child) {
        AssocCompoundActionParam p;
        p.action_param = action_param();
        p.class_version = s.main.bit_short();
        p.status = s.main.bit_short();
        p.parameters = handles(s.main.bit_long());
        if (child) {
            AssocChildParameter c;
            c.status = s.main.bit_short();
            c.id = s.main.bit_long();
            c.parameter = href();
            if (c.id) {
                c.secondary_parameter = href();
                c.marker = s.main.bit_long();
                c.tertiary_parameter = href();
            }
            p.child_parameter = c;
        }
        return p;
    }
    AssocArrayItem array_item() {
        AssocArrayItem item;
        item.class_version = s.main.bit_long();
        item.location = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long()};
        item.flags = s.main.bit_long();
        item.uses_default_transform = (item.flags & 4) == 0;
        if (item.uses_default_transform)
            item.x_direction = vec();
        else
            for (auto &x : item.transform)
                x = s.main.bit_double();
        if (item.flags & 2) {
            std::array<double, 16> m{};
            for (auto &x : m)
                x = s.main.bit_double();
            item.relative_transform = m;
        }
        item.first_handle = href();
        if (item.flags & 0x10)
            item.second_handle = href();
        return item;
    }
    AssocArrayActionBody array_body() {
        AssocArrayActionBody b;
        b.action_body = action_body();
        b.parameter_body = parameter_body();
        b.version = s.main.bit_long();
        b.parameter_block = str();
        b.item_list_version = s.main.bit_long();
        auto n = count();
        b.item_class = str();
        b.items.reserve(n);
        for (std::size_t i = 0; i < n && main_left(); ++i)
            b.items.push_back(array_item());
        for (auto &x : b.transform)
            x = s.main.bit_double();
        return b;
    }
    AssocArrayParameters array_parameters() {
        AssocArrayParameters p;
        p.version = s.main.bit_long();
        auto n = count();
        p.class_name = str();
        p.items.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            AssocArrayItem item;
            item.class_version = s.main.bit_long();
            item.location = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long()};
            item.flags = s.main.bit_long();
            for (auto &x : item.transform)
                x = s.main.bit_double();
            if (item.flags & 2) {
                std::array<double, 16> m{};
                for (auto &x : m)
                    x = s.main.bit_double();
                item.relative_transform = m;
            }
            if (item.flags & 0x10)
                item.second_handle = href();
            p.items.push_back(item);
        }
        p.item_count = s.main.bit_long();
        p.row_count = s.main.bit_long();
        p.level_count = s.main.bit_long();
        return p;
    }
    AssocDimensionAssociation dimension() {
        AssocDimensionAssociation d;
        d.associativity = s.main.bit_long();
        d.trans_space = bit();
        d.rotated_type = byte();
        d.dimension = href();
        std::size_t total = 0;
        for (int slot = 0; slot < 4; ++slot) {
            if ((d.associativity & (1 << slot)) == 0)
                continue;
            while (total < 6) {
                AssocDimensionReference r;
                r.class_name = str();
                r.osnap_type = byte();
                r.xrefs = handles(s.main.bit_long());
                if (AssocDimensionReference::stores_main_subentity(r.osnap_type, r.xrefs.size())) {
                    r.main_subent_type = s.main.bit_long();
                    r.main_gs_marker = s.main.bit_long();
                    auto n = count();
                    r.xref_paths.reserve(n);
                    for (std::size_t i = 0; i < n; ++i)
                        r.xref_paths.push_back(str());
                }
                r.osnap_distance = s.main.bit_double();
                r.osnap_point = vec();
                if (AssocDimensionReference::stores_intersection(r.osnap_type)) {
                    r.intersection_objects = handles(s.main.bit_long());
                    if (AssocDimensionReference::stores_intersection_subentity(
                            r.osnap_type, r.intersection_objects.size())) {
                        r.intersection_subent_type = s.main.bit_long();
                        r.intersection_gs_marker = s.main.bit_long();
                        auto n = count();
                        r.intersection_xref_paths.reserve(n);
                        for (std::size_t i = 0; i < n; ++i)
                            r.intersection_xref_paths.push_back(str());
                    }
                }
                r.has_last_point_reference = bit();
                d.references[slot].push_back(r);
                ++total;
                if (!r.has_last_point_reference)
                    break;
            }
        }
        return d;
    }
    void edge_curve(std::int32_t type, std::vector<AssocCurveValue> &out, std::size_t depth) {
        auto point = [&] { return AssocCurveValue(vec()); };
        auto real = [&] { return AssocCurveValue(s.main.bit_double()); };
        if (type == 11 || type == 17) {
            for (int i = 0; i < 3; ++i)
                out.push_back(point());
            int scalars = type == 11 ? 4 : 5;
            for (int i = 0; i < scalars; ++i) {
                if (type == 11 && i == 3 && depth == 0 && s.v == 1024 && main_left() == 0)
                    break;
                out.push_back(real());
            }
        } else if (type == 19 || type == 23) {
            out.push_back(point());
            out.push_back(point());
        } else if (type == 42) {
            out.push_back(AssocCurveValue(bit()));
            out.push_back(AssocCurveValue(bit()));
            out.push_back(AssocCurveValue(s.main.bit_long()));
            out.push_back(real());
            for (bool points : {false, false, true}) {
                auto n = s.main.bit_long();
                out.push_back(AssocCurveValue(n));
                out.push_back(AssocCurveValue(s.main.bit_long()));
                out.push_back(AssocCurveValue(s.main.bit_long()));
                auto c = count(n);
                for (std::size_t i = 0; i < c; ++i)
                    out.push_back(points ? point() : real());
            }
        } else if (type == 47 && depth < 8) {
            auto n = s.main.bit_long();
            out.push_back(AssocCurveValue(n));
            auto c = count(n);
            for (std::size_t i = 0; i < c; ++i) {
                auto part = s.main.bit_long();
                out.push_back(AssocCurveValue(part));
                edge_curve(part, out, depth + 1);
            }
        }
    }
    AssocConstraintNode node_common() {
        AssocConstraintNode n;
        n.node_id = s.main.bit_long();
        bool status_before = !r2013();
        if (status_before)
            n.status = byte();
        auto c = count();
        n.connections.reserve(c);
        for (std::size_t i = 0; i < c; ++i)
            n.connections.push_back(s.main.bit_long());
        if (!status_before)
            n.status = byte();
        return n;
    }
    bool plain_constraint(const std::string &n) {
        static const char *const names[] = {
            "ACCENTERPOINTCONSTRAINT",     "ACCOLINEARCONSTRAINT",
            "ACCONCENTRICCONSTRAINT",      "ACEQUALCURVATURECONSTRAINT",
            "ACEQUALDISTANCECONSTRAINT",   "ACEQUALHELPPARAMETERCONSTRAINT",
            "ACEQUALLENGTHCONSTRAINT",     "ACEQUALRADIUSCONSTRAINT",
            "ACFIXEDCONSTRAINT",           "ACMIDPOINTCONSTRAINT",
            "ACNORMALCONSTRAINT",          "ACPERPENDICULARCONSTRAINT",
            "ACPOINTCOINCIDENCECONSTRAINT","ACPOINTCURVECONSTRAINT",
            "ACSYMMETRICCONSTRAINT",       "ACTANGENTCONSTRAINT"};
        auto u = ascii_upper(n);
        for (auto name : names)
            if (u == name)
                return true;
        return false;
    }
    std::tuple<std::int32_t, bool, bool> geometrical(bool legacy) {
        return {s.main.bit_long(), bit(), legacy || bit()};
    }
    AssocConstraintNodeData node_data(const std::string &class_name, bool legacy, bool circle_reserved) {
        auto name = ascii_upper(class_name);
        auto geom_dep = [&] {
            auto h = href();
            auto id = s.main.bit_long();
            return std::pair{h, id};
        };
        if (name == "ACG2SMOOTHCONSTRAINT") {
            auto [owner, implied, active] = geometrical(legacy);
            auto n = count();
            AssocConstraintComposite c;
            c.owner_id = owner;
            c.is_implied = implied;
            c.is_active = active;
            c.owned_constraint_ids.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                c.owned_constraint_ids.push_back(s.main.bit_long());
            return c;
        }
        if (name == "ACHELPPARAMETER")
            return AssocConstraintHelpParameter{s.main.bit_double(), bit()};
        if (name == "ACCONSTRAINEDCIRCLE") {
            auto [h, id] = geom_dep();
            AssocConstraintCircle c;
            c.geometry_dependency = h;
            c.geometry_node_id = id;
            c.center = vec();
            c.normal = vec();
            c.direction = vec();
            c.radius = s.main.bit_double();
            c.start_parameter = s.main.bit_double();
            c.end_parameter = s.main.bit_double();
            c.reserved = circle_reserved ? s.main.bit_double() : 0;
            return c;
        }
        if (name == "ACCONSTRAINEDARC") {
            auto [h, id] = geom_dep();
            AssocConstraintArc a;
            a.geometry_dependency = h;
            a.geometry_node_id = id;
            a.center = vec();
            a.normal = vec();
            a.direction = vec();
            a.radius = s.main.bit_double();
            a.start_parameter = s.main.bit_double();
            a.end_parameter = s.main.bit_double();
            a.reserved = circle_reserved ? s.main.bit_double() : 0;
            a.start_point = vec();
            a.end_point = vec();
            return a;
        }
        if (name == "ACCONSTRAINEDIMPLICITPOINT") {
            AssocConstraintImplicitPoint p;
            p.geometry_dependency = href();
            p.geometry_node_id = s.main.bit_long();
            if (p.geometry_dependency)
                p.point = vec();
            p.point_type = byte();
            p.point_index = s.main.bit_long();
            p.curve_id = s.main.bit_long();
            return p;
        }
        if (name == "ACCONSTRAINEDPOINT") {
            AssocConstraintPoint p;
            p.geometry_dependency = href();
            p.geometry_node_id = s.main.bit_long();
            if (p.geometry_dependency)
                p.point = vec();
            return p;
        }
        if (name == "ACCONSTRAINEDRIGIDSET") {
            AssocConstraintRigidSet r;
            r.geometry_dependency = href();
            r.geometry_node_id = s.main.bit_long();
            r.reserved = bit();
            for (auto &x : r.transform)
                x = s.main.bit_double();
            auto n = count();
            r.geometry_ids.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                r.geometry_ids.push_back(s.main.bit_long());
            return r;
        }
        if (name == "ACCONSTRAINEDLINE" || name == "ACCONSTRAINEDCONSTRUCTIONLINE" ||
            name == "ACCONSTRAINED2POINTSCONSTRUCTIONLINE" || name == "ACCONSTRAINEDDATUMLINE") {
            auto [h, id] = geom_dep();
            return AssocConstraintLine{h, id, vec(), vec()};
        }
        if (name == "ACCONSTRAINEDBOUNDEDLINE") {
            auto [h, id] = geom_dep();
            AssocConstraintBoundedLine l;
            l.geometry_dependency = h;
            l.geometry_node_id = id;
            l.point = vec();
            l.direction = vec();
            l.is_ray = bit();
            l.start_point = vec();
            l.end_point = vec();
            return l;
        }
        if (name == "ACANGLECONSTRAINT" || name == "AC3POINTANGLECONSTRAINT") {
            auto [owner, implied, active] = geometrical(legacy);
            AssocConstraintAngle a;
            a.owner_id = owner;
            a.is_implied = implied;
            a.is_active = active;
            a.value_dependency = href();
            a.dimension_dependency = href();
            a.sector_type = byte();
            return a;
        }
        if (name == "ACPARALLELCONSTRAINT" || name == "ACHORIZONTALCONSTRAINT" ||
            name == "ACVERTICALCONSTRAINT") {
            auto [owner, implied, active] = geometrical(legacy);
            AssocConstraintParallel p;
            p.owner_id = owner;
            p.is_implied = implied;
            p.is_active = active;
            if (name != "ACPARALLELCONSTRAINT")
                p.datum_line_index = s.main.bit_long();
            return p;
        }
        if (name == "ACDISTANCECONSTRAINT") {
            auto [owner, implied, active] = geometrical(legacy);
            AssocConstraintDistance d;
            d.owner_id = owner;
            d.is_implied = implied;
            d.is_active = active;
            d.value_dependency = href();
            d.dimension_dependency = href();
            d.direction_type = byte();
            if (d.direction_type)
                d.distance = vec();
            return d;
        }
        if (name == "ACRADIUSDIAMETERCONSTRAINT") {
            auto [owner, implied, active] = geometrical(legacy);
            AssocConstraintRadiusDiameter r;
            r.owner_id = owner;
            r.is_implied = implied;
            r.is_active = active;
            r.value_dependency = href();
            r.dimension_dependency = href();
            r.mode = byte();
            return r;
        }
        if (name == "ACCONSTRAINEDELLIPSE") {
            auto [h, id] = geom_dep();
            return AssocConstraintEllipse{h, id, vec(), vec(), s.main.bit_double()};
        }
        if (name == "ACCONSTRAINEDBOUNDEDELLIPSE") {
            auto [h, id] = geom_dep();
            AssocConstraintBoundedEllipse e;
            e.geometry_dependency = h;
            e.geometry_node_id = id;
            e.center = vec();
            e.major_axis = vec();
            e.axis_ratio = s.main.bit_double();
            e.start_point = vec();
            e.end_point = vec();
            return e;
        }
        if (name == "ACCONSTRAINEDSPLINE") {
            AssocConstraintSpline sp;
            sp.geometry_dependency = href();
            sp.geometry_node_id = s.main.bit_long();
            sp.rational = bit();
            sp.periodic = bit();
            sp.degree = s.main.bit_long();
            sp.knot_tolerance = s.main.bit_double();
            auto knots = count();
            sp.knot_physical_length = s.main.bit_long();
            sp.knot_grow_length = s.main.bit_long();
            sp.knots.reserve(knots);
            for (std::size_t i = 0; i < knots; ++i)
                sp.knots.push_back(s.main.bit_double());
            auto weights = count();
            sp.weight_physical_length = s.main.bit_long();
            sp.weight_grow_length = s.main.bit_long();
            sp.weights.reserve(weights);
            for (std::size_t i = 0; i < weights; ++i)
                sp.weights.push_back(s.main.bit_double());
            auto points = count();
            sp.control_point_physical_length = s.main.bit_long();
            sp.control_point_grow_length = s.main.bit_long();
            sp.control_points.reserve(points);
            for (std::size_t i = 0; i < points; ++i)
                sp.control_points.push_back(vec());
            auto ids = count();
            sp.implicit_point_ids.reserve(ids);
            for (std::size_t i = 0; i < ids; ++i)
                sp.implicit_point_ids.push_back(s.main.bit_long());
            return sp;
        }
        if (plain_constraint(class_name)) {
            auto [owner, implied, active] = geometrical(legacy);
            return AssocConstraintGeometrical{owner, implied, active};
        }
        return std::monostate{};
    }
    Assoc2dConstraintGroup constraint_group() {
        Assoc2dConstraintGroup g;
        g.action = action();
        auto group_version = s.main.bit_long();
        g.flag = bit();
        g.work_plane = {vec(), vec(), vec()};
        g.dependency = href();
        g.actions = handles(s.main.bit_long());
        g.next_node_id = s.main.bit_long();
        require(g.next_node_id >= 0, "Negative DWG constraint-group next node id");
        bool legacy = group_version == 0;
        bool inline_nodes = legacy;
        if (!legacy && !r2013() && main_left() >= 2) {
            auto pos = s.main.position();
            auto remaining = main_left();
            auto n = s.main.bit_long();
            s.main.seek(pos);
            inline_nodes = n > 0 || (n == 0 && remaining < 11);
        }
        g.version = legacy ? 2 : group_version;
        if (inline_nodes) {
            auto n = count();
            if (n)
                g.nodes.emplace_back();
            for (std::size_t i = 0; i < n; ++i) {
                auto class_name = str();
                AssocConstraintNode node;
                if (legacy) {
                    node.node_id = s.main.bit_long();
                    auto c = count();
                    node.connections.reserve(c);
                    for (std::size_t j = 0; j < c; ++j)
                        node.connections.push_back(s.main.bit_long());
                } else
                    node = node_common();
                node.data = node_data(class_name, legacy, false);
                if (std::holds_alternative<std::monostate>(node.data))
                    throw std::invalid_argument("Unsupported DWG constraint node class " + class_name);
                node.class_name = class_name;
                g.nodes.push_back(std::move(node));
            }
        } else if (g.next_node_id > 0) {
            AssocConstraintNode root;
            root.node_id = s.main.bit_long();
            auto c = count();
            root.connections.reserve(c);
            for (std::size_t i = 0; i < c; ++i)
                root.connections.push_back(s.main.bit_long());
            root.status = bit();
            g.nodes.push_back(root);
            auto types_n = count();
            std::vector<std::string> types;
            types.reserve(types_n);
            for (std::size_t i = 0; i < types_n; ++i)
                types.push_back(str());
            auto registered = count();
            std::vector<std::tuple<std::string, std::int32_t, bool>> registry;
            registry.reserve(registered);
            for (std::size_t i = 0; i < registered; ++i) {
                bool flag = bit();
                auto index = s.main.bit_long();
                auto id = s.main.bit_long();
                std::string class_name;
                if (index >= 1 && std::size_t(index - 1) < types.size())
                    class_name = types[std::size_t(index - 1)];
                registry.emplace_back(class_name, id, flag);
            }
            for (auto &[class_name, id, flag] : registry) {
                auto node = node_common();
                if (!node.node_id)
                    node.node_id = id;
                node.data = node_data(class_name, false, true);
                if (std::holds_alternative<std::monostate>(node.data))
                    throw std::invalid_argument("Unsupported DWG constraint node class " + class_name);
                node.class_name = class_name;
                node.registry_flag = flag;
                g.nodes.push_back(std::move(node));
            }
        }
        return g;
    }
    AssociativeData read(const std::string &dxf_name) {
        auto name = associative_canonical_name(dxf_name);
        auto surface_kind = [&]() -> std::optional<AssocSurfaceActionKind> {
            if (name == "ASSOCPLANESURFACEACTIONBODY")
                return AssocSurfaceActionKind::Plane;
            if (name == "ASSOCEXTENDSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Extend;
            if (name == "ASSOCEXTRUDEDSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Extruded;
            if (name == "ASSOCLOFTEDSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Lofted;
            if (name == "ASSOCNETWORKSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Network;
            if (name == "ASSOCOFFSETSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Offset;
            if (name == "ASSOCREVOLVEDSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Revolved;
            if (name == "ASSOCTRIMSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Trim;
            if (name == "ASSOCBLENDSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Blend;
            if (name == "ASSOCPATCHSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Patch;
            if (name == "ASSOCFILLETSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Fillet;
            if (name == "ASSOCSWEPTSURFACEACTIONBODY")
                return AssocSurfaceActionKind::Swept;
            if (name == "ASSOCEDGECHAMFERACTIONBODY")
                return AssocSurfaceActionKind::EdgeChamfer;
            if (name == "ASSOCEDGEFILLETACTIONBODY")
                return AssocSurfaceActionKind::EdgeFillet;
            return {};
        }();
        if (name == "ASSOCDEPENDENCY")
            return dependency();
        if (name == "ASSOCVALUEDEPENDENCY")
            return AssocValueDependency{dependency(), s.main.bit_long(), str(), eval()};
        if (name == "ASSOCGEOMDEPENDENCY")
            return AssocGeomDependency{dependency(), s.main.bit_short(), bit(), persistent_subent()};
        if (name == "ASSOCACTION")
            return action();
        if (name == "ASSOCNETWORK") {
            AssocNetwork n;
            n.action = action();
            n.network_version = s.main.bit_short();
            n.network_action_index = s.main.bit_long();
            auto c = count();
            n.actions.reserve(c);
            for (std::size_t i = 0; i < c; ++i)
                n.actions.push_back({bit(), href()});
            n.owned_actions = handles(s.main.bit_long());
            return n;
        }
        if (surface_kind)
            return surface(*surface_kind);
        if (name == "ASSOCRESTOREENTITYSTATEACTIONBODY")
            return annotation(AssocAnnotationKind::RestoreEntityState);
        if (name == "ASSOCMLEADERACTIONBODY")
            return annotation(AssocAnnotationKind::MLeader);
        if (name == "ASSOCALIGNEDDIMACTIONBODY")
            return annotation(AssocAnnotationKind::AlignedDimension);
        if (name == "ASSOC3POINTANGULARDIMACTIONBODY")
            return annotation(AssocAnnotationKind::ThreePointAngularDimension);
        if (name == "ASSOCORDINATEDIMACTIONBODY")
            return annotation(AssocAnnotationKind::OrdinateDimension);
        if (name == "ASSOCROTATEDDIMACTIONBODY")
            return annotation(AssocAnnotationKind::RotatedDimension);
        if (name == "ASSOCPERSSUBENTMANAGER") {
            AssocPersSubentManager m;
            m.class_version = s.main.bit_long();
            m.markers = {s.main.bit_long(), s.main.bit_long(), s.main.bit_long()};
            while (main_left() > 1)
                m.values.push_back(s.main.bit_long());
            m.final_flag = bit();
            return m;
        }
        if (name == "ASSOCEDGEACTIONPARAM") {
            AssocEdgeActionParam e;
            e.single_dependency = single();
            e.parameter = href();
            e.has_action = bit();
            if (e.has_action) {
                e.action_type = s.main.bit_long();
                e.subcurve_kind = e.action_type == 11   ? AssocSubcurveKind::Arc
                                  : e.action_type == 17 ? AssocSubcurveKind::Ellipse
                                  : e.action_type == 19 ? AssocSubcurveKind::Line
                                  : e.action_type == 23 ? AssocSubcurveKind::LineSegment3d
                                  : e.action_type == 42 ? AssocSubcurveKind::Nurb3d
                                  : e.action_type == 27 ? AssocSubcurveKind::Curve3d
                                                        : AssocSubcurveKind::None;
                edge_curve(e.action_type, e.curve, 0);
            }
            return e;
        }
        if (name == "ASSOC2DCONSTRAINTGROUP")
            return constraint_group();
        if (name == "ASSOCVARIABLE") {
            AssocVariable v;
            v.action = action();
            v.class_version = s.main.bit_long();
            v.name = str();
            v.expression = str();
            v.evaluator = str();
            v.description = str();
            v.value = eval();
            v.has_cached_value = bit();
            if (v.has_cached_value)
                v.cached_value = str();
            v.flag = bit();
            auto n = main_left() >= 2 ? count() : 0;
            v.dependencies.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                v.dependencies.push_back({href(), s.main.bit_long()});
            return v;
        }
        if (name == "ASSOCACTIONPARAM")
            return action_param();
        if (name == "ASSOCCOMPOUNDACTIONPARAM")
            return compound(false);
        if (name == "ASSOCOSNAPPOINTREFACTIONPARAM") {
            AssocOsnapPointRefActionParam p;
            p.compound = compound(true);
            p.status = s.main.bit_short();
            p.osnap_mode = byte();
            p.parameter = s.main.bit_double();
            return p;
        }
        if (name == "ASSOCPOINTREFACTIONPARAM")
            return AssocPointRefActionParam{compound(true)};
        if (name == "ASSOCOBJECTACTIONPARAM")
            return single();
        if (name == "ASSOCPATHACTIONPARAM")
            return AssocPathActionParam{compound(false), s.main.bit_long()};
        if (name == "ASSOCDIMDEPENDENCYBODY")
            return AssocDimDependencyBody{s.main.bit_short(), s.main.bit_short(), str(), s.main.bit_short()};
        if (name == "ASSOCFACEACTIONPARAM")
            return AssocFaceActionParam{single(), s.main.bit_long()};
        if (name == "ASSOCVERTEXACTIONPARAM")
            return AssocVertexActionParam{single(), vec()};
        if (name == "ASSOCASMBODYACTIONPARAM")
            return asm_body();
        if (name == "ASSOCARRAYMODIFYPARAMETERS" || name == "ASSOCARRAYPATHPARAMETERS" ||
            name == "ASSOCARRAYPOLARPARAMETERS" || name == "ASSOCARRAYRECTANGULARPARAMETERS")
            return array_parameters();
        if (name == "ASSOCARRAYACTIONBODY")
            return array_body();
        if (name == "ASSOCARRAYMODIFYACTIONBODY") {
            AssocArrayModifyActionBody m;
            m.body = array_body();
            m.status = s.main.bit_short();
            auto n = count();
            m.item_locations.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                m.item_locations.push_back({s.main.bit_long(), s.main.bit_long(), s.main.bit_long()});
            return m;
        }
        if (name == "DIMASSOC")
            return dimension();
        if (name == "PERSUBENTMGR") {
            PersSubentManager m;
            m.class_version = s.main.bit_long();
            m.marker_zero = s.main.bit_long();
            m.marker_two = s.main.bit_long();
            m.associative_step_count = s.main.bit_long();
            m.associative_subent_count = s.main.bit_long();
            while (main_left() >= 2)
                m.values.push_back(s.main.bit_long());
            return m;
        }
        if (name == "ACDBCENTERMARKACTIONBODY" || name == "ACDBCENTERLINEACTIONBODY")
            return AssocSmartCenterActionBody{action_body(), parameter_body(), s.main.bit_long()};
        if (name == "ASSOCVIEWREPACTIONBODY")
            return AssocViewRepActionBody{action_body(), s.main.bit_short(), href(), s.main.bit_long(),
                                          s.main.bit_double()};
        if (name == "ASSOCVIEWBORDERACTIONPARAM" || name == "ASSOCVIEWREPACTIONPARAM" ||
            name == "ASSOCVIEWSYMBOLACTIONPARAM" || name == "ASSOCVIEWSTYLEACTIONPARAM") {
            AssocViewObjectActionParam p;
            p.kind = name == "ASSOCVIEWREPACTIONPARAM"       ? AssocViewObjectActionParamKind::ViewRep
                     : name == "ASSOCVIEWSYMBOLACTIONPARAM" ? AssocViewObjectActionParamKind::ViewSymbol
                     : name == "ASSOCVIEWSTYLEACTIONPARAM"  ? AssocViewObjectActionParamKind::ViewStyle
                                                            : AssocViewObjectActionParamKind::ViewBorder;
            p.single_dependency = single();
            p.class_version = s.main.bit_short();
            return p;
        }
        if (name == "ASSOCVIEWREPHATCHMANAGER") {
            AssocViewRepHatchManager m;
            m.compound = compound(false);
            m.class_version = s.main.bit_short();
            auto n = count();
            m.items.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                m.items.push_back({s.main.bit_long_long(), s.main.bit_long_long(), s.main.bit_long(), href()});
            return m;
        }
        if (name == "ASSOCVIEWREPHATCHACTIONPARAM") {
            AssocViewRepHatchActionParam p;
            p.single_dependency = single();
            p.class_version = s.main.bit_short();
            p.normal = vec();
            p.hatch_index = s.main.bit_long();
            p.flags = s.main.bit_long();
            return p;
        }
        if (name == "ASSOCVIEWLABELACTIONPARAM") {
            AssocViewLabelActionParam p;
            p.single_dependency = single();
            p.class_version = s.main.bit_short();
            p.label_version = s.main.bit_short();
            p.offset = raw2();
            p.flag = byte();
            return p;
        }
        throw std::invalid_argument("Unknown associative DWG object");
    }
};
AssociativeObject read_associative_record(std::string_view body, DwgR2000Record &record, std::uint64_t h,
                                          unsigned v, const char *page, const std::string &dxf_name) {
    require(v >= 1018, "Native DWG associative objects require R2004+");
    AssocIO io(body, record, h, v, page);
    AssociativeObject out;
    out.handle = h;
    out.dxf_name = dxf_name;
    out.cpp_class_name = associative_cpp_class_name(dxf_name).value_or("");
    DwgDynamicRecord common;
    read_dynamic_common(io.s, common, false, record);
    out.owner = common.owner;
    out.reactors = common.reactors;
    if (common.extension_dictionary)
        out.xdictionary_handle = common.extension_dictionary;
    out.data = io.read(dxf_name);
    out.source_version = static_cast<DxfVersion>(v);
    return out;
}
