// SPDX-License-Identifier: MPL-2.0
// Native encode/rewrite for typed DWG associative objects.
#include <algorithm>
namespace {
struct AssocWriter {
    DwgBitWriter main, refs, strings;
    unsigned v{};
    const char *page = nullptr;
    bool r2013() const { return v >= 1027; }
    void str(std::string_view s) { symbol_string(main, strings, v, s, page); }
    void vec(Vector3 p) { vector_bits(main, p); }
    void raw2(Vector2 p) {
        raw_double(main, p.x);
        raw_double(main, p.y);
    }
    void href(unsigned code, std::uint64_t h) { refs.absolute_handle(code, h); }
    void bit(bool value) { main.bits(1, value); }
    void byte(std::uint8_t value) { main.bits(8, value); }
    void handles(unsigned code, const std::vector<std::uint64_t> &values) {
        for (auto h : values)
            href(code, h);
    }
    int eval_kind(std::int16_t code) {
        if (code < 0 || code == 5 || code == 105 || (code >= 320 && code <= 369) ||
            (code >= 390 && code <= 399))
            return 6;
        if ((code >= 0 && code <= 9) || (code >= 100 && code <= 102) || (code >= 300 && code <= 309) ||
            (code >= 410 && code <= 419) || (code >= 430 && code <= 439) || (code >= 470 && code <= 479) ||
            code == 999 || (code >= 1000 && code <= 1009))
            return 5;
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
    void eval(const AssocEvalVariant &value) {
        main.bit_short(value.code);
        if (!value.code)
            return;
        switch (eval_kind(value.code)) {
        case 1:
            main.bit_double(std::get<double>(value.value));
            break;
        case 2:
            main.bit_long(std::get<std::int32_t>(value.value));
            break;
        case 3:
            main.bit_short(std::get<std::int16_t>(value.value));
            break;
        case 4:
            byte(std::get<std::uint8_t>(value.value));
            break;
        case 5:
            str(std::get<std::string>(value.value));
            break;
        case 6:
            href(5, std::get<std::uint64_t>(value.value));
            break;
        default:
            break;
        }
    }
    void value_param(const AssocValueParam &value) {
        main.bit_long(value.class_version);
        str(value.name);
        main.bit_long(value.unit_type);
        main.bit_long(std::int32_t(value.variables.size()));
        for (auto &variable : value.variables) {
            eval(variable.value);
            href(4, variable.handle);
        }
        href(4, value.controlled_object_dependency);
    }
    void values(const std::vector<AssocValueParam> &items) {
        for (auto &item : items)
            value_param(item);
    }
    void dependency(const AssocDependency &value) {
        main.bit_short(value.class_version);
        main.bit_long(value.status);
        bit(value.is_read_dependency);
        bit(value.is_write_dependency);
        bit(value.is_attached_to_object);
        bit(value.is_delegating_to_owning_action);
        main.bit_long(value.order);
        href(4, value.dependent_on);
        bit(value.name.has_value());
        if (value.name)
            str(*value.name);
        href(4, value.read_dependency);
        href(4, value.node);
        href(3, value.dependency_body);
        main.bit_long(value.dependency_body_id);
    }
    void action(const AssocAction &value) {
        main.bit_short(value.class_version);
        main.bit_long(value.geometry_status);
        href(4, value.owning_network);
        href(3, value.action_body);
        main.bit_long(value.action_index);
        main.bit_long(value.max_dependency_index);
        main.bit_long(std::int32_t(value.dependencies.size()));
        for (auto &item : value.dependencies) {
            bit(item.is_owned);
            href(item.is_owned ? 3 : 4, item.dependency);
        }
        if (value.class_version > 1) {
            main.bit_short(0);
            main.bit_long(std::int32_t(value.owned_parameters.size()));
            handles(3, value.owned_parameters);
            main.bit_short(0);
            main.bit_long(std::int32_t(value.values.size()));
            values(value.values);
        }
    }
    void action_param(const AssocActionParam &value) {
        main.bit_short(value.is_r2013);
        if (r2013())
            main.bit_long(value.version);
        str(value.name);
    }
    void action_body(const AssocActionBody &value) { main.bit_long(value.version); }
    void parameter_body(const AssocParamBasedActionBody &value) {
        if (r2013())
            return;
        main.bit_long(value.version);
        main.bit_long(value.minor);
        main.bit_long(std::int32_t(value.dependencies.size()));
        handles(4, value.dependencies);
        main.bit_long(value.marker);
        main.bit_long(std::int32_t(value.values.size()));
        if (value.values.empty()) {
            main.bit_long(value.empty_value_marker);
            href(5, value.dependency);
        }
        values(value.values);
    }
    void surface(const AssocSurfaceActionBody &value) {
        action_body(value.action_body);
        parameter_body(value.parameter_body);
        main.bit_long(value.surface_body.version);
        href(5, value.surface_body.dependency);
        bit(value.surface_body.is_semi_associative);
        main.bit_long(value.surface_body.marker);
        bit(value.surface_body.is_semi_override);
        main.bit_short(value.surface_body.grip_status);
        main.bit_long(value.path_status);
        if (value.kind != AssocSurfaceActionKind::Network && value.kind != AssocSurfaceActionKind::Patch &&
            value.kind != AssocSurfaceActionKind::EdgeChamfer &&
            value.kind != AssocSurfaceActionKind::EdgeFillet)
            main.bit_long(value.class_version);
        switch (value.kind) {
        case AssocSurfaceActionKind::Extend:
            byte(value.option);
            break;
        case AssocSurfaceActionKind::Offset:
            bit(value.flags[0]);
            break;
        case AssocSurfaceActionKind::Trim:
            bit(value.flags[0]);
            bit(value.flags[1]);
            main.bit_double(value.distance);
            break;
        case AssocSurfaceActionKind::Blend:
            bit(value.flags[0]);
            bit(value.flags[1]);
            bit(value.flags[2]);
            main.bit_short(value.status);
            bit(value.flags[3]);
            bit(value.flags[4]);
            main.bit_short(value.secondary_status);
            break;
        case AssocSurfaceActionKind::Fillet:
            main.bit_short(value.status);
            raw2(value.first_point);
            raw2(value.second_point);
            break;
        default:
            break;
        }
    }
    void annotation_base(const AssocAnnotationBase &value) {
        if (v >= 1021) {
            main.bit_short(value.version);
            href(5, value.dependency);
        } else {
            action_body(value.action_body);
            parameter_body(value.parameter_body);
        }
    }
    void annotation(const AssocAnnotationActionBody &value) {
        if (value.kind == AssocAnnotationKind::RestoreEntityState) {
            action_body(value.action_body);
            main.bit_long(value.class_version);
            href(5, value.entity);
            return;
        }
        annotation_base(value.annotation);
        if (value.kind == AssocAnnotationKind::ThreePointAngularDimension ||
            value.kind == AssocAnnotationKind::RotatedDimension)
            main.bit_short(static_cast<std::int16_t>(value.class_version));
        else
            main.bit_long(value.class_version);
        switch (value.kind) {
        case AssocAnnotationKind::MLeader:
            main.bit_long(std::int32_t(value.actions.size()));
            for (auto &item : value.actions) {
                main.bit_long(item.dependency_id);
                href(5, item.dependency);
            }
            break;
        case AssocAnnotationKind::AlignedDimension:
            href(4, value.read_node);
            href(4, value.dimension_node);
            break;
        case AssocAnnotationKind::ThreePointAngularDimension:
            href(4, value.read_node);
            href(4, value.dimension_node);
            href(5, value.dependency);
            break;
        case AssocAnnotationKind::OrdinateDimension:
        case AssocAnnotationKind::RotatedDimension:
            href(5, value.read_node);
            href(5, value.dimension_node);
            break;
        default:
            break;
        }
    }
    void single(const AssocSingleDependencyActionParam &value) {
        action_param(value.action_param);
        main.bit_long(value.dependency_class_version);
        href(4, value.dependency);
        main.bit_long(value.class_version);
    }
    void sat_blocks(std::string_view encrypted) {
        for (std::size_t i = 0; i < encrypted.size(); i += 4096) {
            auto n = std::min<std::size_t>(4096, encrypted.size() - i);
            main.bit_long(static_cast<std::int32_t>(n));
            for (std::size_t j = 0; j < n; ++j)
                main.bits(8, static_cast<unsigned char>(encrypted[i + j]));
        }
        main.bit_long(0);
    }
    void write_sat(std::string text) {
        main.bit_short(1);
        text = detail::dwg_sat_cipher(detail::dwg_sat_transport(text, true));
        sat_blocks(text);
    }
    void write_sab(const std::vector<std::uint8_t> &bytes) {
        main.bit_short(2);
        for (auto b : bytes)
            main.bits(8, b);
    }
    void write_revision() {
        bit(false);
        main.bit_long(0);
        main.bit_short(0);
        main.bit_short(0);
        for (int i = 0; i < 8; ++i)
            byte(0);
        main.bit_long(0);
    }
    bool write_wireframe(Vector3 point, bool present) {
        bool frame = (present && v < 1027) || point.x || point.y || point.z;
        bit(frame);
        if (frame) {
            bool has_point = point.x || point.y || point.z;
            bit(has_point);
            if (has_point)
                vec(point);
            main.bit_long(0);
            bit(false);
            main.bit_long(0);
        }
        return frame;
    }
    void asm_body(const AssocAsmBodyActionParam &value) {
        single(value.single_dependency);
        auto sat = value.sat_data;
        auto sab = value.sab_data;
        bool binary = value.is_binary;
        if (v >= 1018 && !binary && !sat.empty()) {
            try {
                sab = acis::SabWriter::write(acis::SatDocument::parse(sat));
                binary = true;
                sat.clear();
            } catch (const std::exception &) {
            }
        }
        bool present = !sat.empty() || !sab.empty();
        bit(!present);
        if (present) {
            bit(!binary);
            if (binary && !sab.empty()) {
                write_sab(sab);
                if (v >= 1021) {
                    auto frame = write_wireframe(value.point_of_reference, present);
                    if (frame || v < 1027)
                        bit(true);
                    main.bit_long(0);
                    if (v >= 1027)
                        write_revision();
                    href(4, value.history);
                    return;
                }
            } else
                write_sat(sat.empty() && !sab.empty() ? acis::SabReader::read(sab).to_sat_string() : sat);
        }
        auto frame = write_wireframe(value.point_of_reference, present);
        if (frame || v < 1027)
            bit(true);
        if (v >= 1021)
            main.bit_long(0);
        if (v >= 1027)
            write_revision();
        if (binary)
            href(4, value.history);
    }
    void compound(const AssocCompoundActionParam &value) {
        action_param(value.action_param);
        main.bit_short(value.class_version);
        main.bit_short(value.status);
        main.bit_long(std::int32_t(value.parameters.size()));
        handles(4, value.parameters);
        if (value.child_parameter) {
            auto &child = *value.child_parameter;
            main.bit_short(child.status);
            main.bit_long(child.id);
            href(3, child.parameter);
            if (child.id) {
                href(3, child.secondary_parameter);
                main.bit_long(child.marker);
                href(3, child.tertiary_parameter);
            }
        }
    }
    void array_item(const AssocArrayItem &item, bool parameters) {
        main.bit_long(item.class_version);
        for (auto x : item.location)
            main.bit_long(x);
        main.bit_long(item.flags);
        if (!parameters && item.uses_default_transform)
            vec(item.x_direction);
        else
            for (auto x : item.transform)
                main.bit_double(x);
        if (item.relative_transform)
            for (auto x : *item.relative_transform)
                main.bit_double(x);
        if (!parameters)
            href(4, item.first_handle.value_or(0));
        else if (item.first_handle)
            href(4, *item.first_handle);
        if (item.flags & 0x10)
            href(4, item.second_handle.value_or(0));
    }
    void array_body(const AssocArrayActionBody &value) {
        action_body(value.action_body);
        parameter_body(value.parameter_body);
        main.bit_long(value.version);
        str(value.parameter_block);
        main.bit_long(value.item_list_version);
        main.bit_long(std::int32_t(value.items.size()));
        str(value.item_class);
        for (auto &item : value.items)
            array_item(item, false);
        for (auto x : value.transform)
            main.bit_double(x);
    }
    void array_parameters(const AssocArrayParameters &value) {
        main.bit_long(value.version);
        main.bit_long(std::int32_t(value.items.size()));
        str(value.class_name);
        for (auto &item : value.items)
            array_item(item, true);
        main.bit_long(value.item_count);
        main.bit_long(value.row_count);
        main.bit_long(value.level_count);
    }
    void dimension(const AssocDimensionAssociation &value) {
        main.bit_long(value.associativity);
        bit(value.trans_space);
        byte(value.rotated_type);
        href(4, value.dimension);
        for (int slot = 0; slot < 4; ++slot) {
            if ((value.associativity & (1 << slot)) == 0)
                continue;
            AssocDimensionReference fallback;
            auto *stored = slot < 4 ? &value.references[slot] : nullptr;
            const AssocDimensionReference *items = stored && !stored->empty() ? stored->data() : &fallback;
            std::size_t n = stored && !stored->empty() ? stored->size() : 1;
            for (std::size_t i = 0; i < n; ++i) {
                auto &r = items[i];
                str(r.class_name);
                byte(r.osnap_type);
                main.bit_long(std::int32_t(r.xrefs.size()));
                handles(4, r.xrefs);
                if (AssocDimensionReference::stores_main_subentity(r.osnap_type, r.xrefs.size())) {
                    main.bit_long(r.main_subent_type);
                    main.bit_long(r.main_gs_marker);
                    main.bit_long(std::int32_t(r.xref_paths.size()));
                    for (auto &path : r.xref_paths)
                        str(path);
                }
                main.bit_double(r.osnap_distance);
                vec(r.osnap_point);
                if (AssocDimensionReference::stores_intersection(r.osnap_type)) {
                    main.bit_long(std::int32_t(r.intersection_objects.size()));
                    handles(5, r.intersection_objects);
                    if (AssocDimensionReference::stores_intersection_subentity(
                            r.osnap_type, r.intersection_objects.size())) {
                        main.bit_long(r.intersection_subent_type);
                        main.bit_long(r.intersection_gs_marker);
                        main.bit_long(std::int32_t(r.intersection_xref_paths.size()));
                        for (auto &path : r.intersection_xref_paths)
                            str(path);
                    }
                }
                bit(i + 1 < n);
            }
        }
    }
    void geometrical(std::int32_t owner, bool implied, bool active) {
        main.bit_long(owner);
        bit(implied);
        bit(active);
    }
    void explicit_constraint(std::int32_t owner, bool implied, bool active, std::uint64_t value,
                             std::uint64_t dimension) {
        geometrical(owner, implied, active);
        href(5, value);
        href(5, dimension);
    }
    void node_common(const AssocConstraintNode &node) {
        main.bit_long(node.node_id);
        if (!r2013())
            byte(node.status);
        main.bit_long(std::int32_t(node.connections.size()));
        for (auto c : node.connections)
            main.bit_long(c);
        if (r2013())
            byte(node.status);
    }
    void node_data(const AssocConstraintNodeData &data) {
        if (auto *g = std::get_if<AssocConstraintGeometrical>(&data))
            geometrical(g->owner_id, g->is_implied, g->is_active);
        else if (auto *c = std::get_if<AssocConstraintComposite>(&data)) {
            geometrical(c->owner_id, c->is_implied, c->is_active);
            main.bit_long(std::int32_t(c->owned_constraint_ids.size()));
            for (auto id : c->owned_constraint_ids)
                main.bit_long(id);
        } else if (auto *h = std::get_if<AssocConstraintHelpParameter>(&data)) {
            main.bit_double(h->value);
            bit(h->reserved);
        } else if (auto *a = std::get_if<AssocConstraintAngle>(&data)) {
            explicit_constraint(a->owner_id, a->is_implied, a->is_active, a->value_dependency,
                                a->dimension_dependency);
            byte(a->sector_type);
        } else if (auto *p = std::get_if<AssocConstraintParallel>(&data)) {
            geometrical(p->owner_id, p->is_implied, p->is_active);
            if (p->datum_line_index)
                main.bit_long(*p->datum_line_index);
        } else if (auto *d = std::get_if<AssocConstraintDistance>(&data)) {
            explicit_constraint(d->owner_id, d->is_implied, d->is_active, d->value_dependency,
                                d->dimension_dependency);
            byte(d->direction_type);
            if (d->direction_type)
                vec(d->distance.value_or(Vector3{}));
        } else if (auto *r = std::get_if<AssocConstraintRadiusDiameter>(&data)) {
            explicit_constraint(r->owner_id, r->is_implied, r->is_active, r->value_dependency,
                                r->dimension_dependency);
            byte(r->mode);
        } else if (auto *p = std::get_if<AssocConstraintImplicitPoint>(&data)) {
            href(4, p->geometry_dependency);
            main.bit_long(p->geometry_node_id);
            if (p->geometry_dependency)
                vec(p->point.value_or(Vector3{}));
            byte(p->point_type);
            main.bit_long(p->point_index);
            main.bit_long(p->curve_id);
        } else if (auto *p = std::get_if<AssocConstraintPoint>(&data)) {
            href(4, p->geometry_dependency);
            main.bit_long(p->geometry_node_id);
            if (p->geometry_dependency)
                vec(p->point.value_or(Vector3{}));
        } else if (auto *r = std::get_if<AssocConstraintRigidSet>(&data)) {
            href(4, r->geometry_dependency);
            main.bit_long(r->geometry_node_id);
            bit(r->reserved);
            for (auto x : r->transform)
                main.bit_double(x);
            main.bit_long(std::int32_t(r->geometry_ids.size()));
            for (auto id : r->geometry_ids)
                main.bit_long(id);
        } else if (auto *l = std::get_if<AssocConstraintLine>(&data)) {
            href(4, l->geometry_dependency);
            main.bit_long(l->geometry_node_id);
            vec(l->point);
            vec(l->direction);
        } else if (auto *l = std::get_if<AssocConstraintBoundedLine>(&data)) {
            href(4, l->geometry_dependency);
            main.bit_long(l->geometry_node_id);
            vec(l->point);
            vec(l->direction);
            bit(l->is_ray);
            vec(l->start_point);
            vec(l->end_point);
        } else if (auto *c = std::get_if<AssocConstraintCircle>(&data)) {
            href(4, c->geometry_dependency);
            main.bit_long(c->geometry_node_id);
            vec(c->center);
            vec(c->normal);
            vec(c->direction);
            main.bit_double(c->radius);
            main.bit_double(c->start_parameter);
            main.bit_double(c->end_parameter);
            if (r2013())
                main.bit_double(c->reserved);
        } else if (auto *a = std::get_if<AssocConstraintArc>(&data)) {
            href(4, a->geometry_dependency);
            main.bit_long(a->geometry_node_id);
            vec(a->center);
            vec(a->normal);
            vec(a->direction);
            main.bit_double(a->radius);
            main.bit_double(a->start_parameter);
            main.bit_double(a->end_parameter);
            if (r2013())
                main.bit_double(a->reserved);
            vec(a->start_point);
            vec(a->end_point);
        } else if (auto *e = std::get_if<AssocConstraintEllipse>(&data)) {
            href(4, e->geometry_dependency);
            main.bit_long(e->geometry_node_id);
            vec(e->center);
            vec(e->major_axis);
            main.bit_double(e->axis_ratio);
        } else if (auto *e = std::get_if<AssocConstraintBoundedEllipse>(&data)) {
            href(4, e->geometry_dependency);
            main.bit_long(e->geometry_node_id);
            vec(e->center);
            vec(e->major_axis);
            main.bit_double(e->axis_ratio);
            vec(e->start_point);
            vec(e->end_point);
        } else if (auto *sp = std::get_if<AssocConstraintSpline>(&data)) {
            href(4, sp->geometry_dependency);
            main.bit_long(sp->geometry_node_id);
            bit(sp->rational);
            bit(sp->periodic);
            main.bit_long(sp->degree);
            main.bit_double(sp->knot_tolerance);
            main.bit_long(std::int32_t(sp->knots.size()));
            main.bit_long(sp->knot_physical_length);
            main.bit_long(sp->knot_grow_length);
            for (auto x : sp->knots)
                main.bit_double(x);
            main.bit_long(std::int32_t(sp->weights.size()));
            main.bit_long(sp->weight_physical_length);
            main.bit_long(sp->weight_grow_length);
            for (auto x : sp->weights)
                main.bit_double(x);
            main.bit_long(std::int32_t(sp->control_points.size()));
            main.bit_long(sp->control_point_physical_length);
            main.bit_long(sp->control_point_grow_length);
            for (auto p : sp->control_points)
                vec(p);
            main.bit_long(std::int32_t(sp->implicit_point_ids.size()));
            for (auto id : sp->implicit_point_ids)
                main.bit_long(id);
        }
    }
    void constraint_group(const Assoc2dConstraintGroup &value) {
        action(value.action);
        main.bit_long(value.version == 0 ? 2 : value.version);
        bit(value.flag);
        for (auto p : value.work_plane)
            vec(p);
        href(3, value.dependency);
        main.bit_long(std::int32_t(value.actions.size()));
        handles(3, value.actions);
        if (!r2013()) {
            std::vector<const AssocConstraintNode *> registered;
            for (auto &node : value.nodes)
                if (!node.class_name.empty())
                    registered.push_back(&node);
            main.bit_long(value.effective_next_node_id());
            main.bit_long(std::int32_t(registered.size()));
            for (auto *node : registered) {
                str(node->class_name);
                node_common(*node);
                node_data(node->data);
            }
        } else if (!value.nodes.empty()) {
            auto &first = value.nodes.front();
            std::vector<const AssocConstraintNode *> registered;
            for (std::size_t i = 1; i < value.nodes.size(); ++i)
                if (!value.nodes[i].class_name.empty())
                    registered.push_back(&value.nodes[i]);
            main.bit_long(value.effective_next_node_id());
            main.bit_long(first.node_id);
            main.bit_long(std::int32_t(first.connections.size()));
            for (auto c : first.connections)
                main.bit_long(c);
            bit(first.status != 0);
            std::vector<std::string> types;
            for (auto *node : registered) {
                bool seen = false;
                for (auto &name : types)
                    if (ascii_upper(name) == ascii_upper(node->class_name))
                        seen = true;
                if (!seen)
                    types.push_back(node->class_name);
            }
            main.bit_long(std::int32_t(types.size()));
            for (auto &name : types)
                str(name);
            main.bit_long(std::int32_t(registered.size()));
            for (auto *node : registered) {
                bit(node->registry_flag);
                std::int32_t index = 0;
                for (std::size_t i = 0; i < types.size(); ++i)
                    if (ascii_upper(types[i]) == ascii_upper(node->class_name))
                        index = std::int32_t(i + 1);
                main.bit_long(index);
                main.bit_long(node->node_id);
            }
            for (auto *node : registered) {
                node_common(*node);
                node_data(node->data);
            }
        } else {
            main.bit_long(value.effective_next_node_id());
            main.bit_long(0);
            main.bit_long(0);
            bit(false);
            main.bit_long(0);
            main.bit_long(0);
        }
    }
    void curve(const std::vector<AssocCurveValue> &curve) {
        for (auto &item : curve) {
            if (auto *b = std::get_if<bool>(&item))
                bit(*b);
            else if (auto *i = std::get_if<std::int32_t>(&item))
                main.bit_long(*i);
            else if (auto *r = std::get_if<double>(&item))
                main.bit_double(*r);
            else
                vec(std::get<Vector3>(item));
        }
    }
    void write(const AssociativeData &data) {
        if (auto *d = std::get_if<AssocDependency>(&data))
            dependency(*d);
        else if (auto *d = std::get_if<AssocValueDependency>(&data)) {
            dependency(d->dependency);
            main.bit_long(d->class_version);
            str(d->name);
            eval(d->value);
        } else if (auto *d = std::get_if<AssocGeomDependency>(&data)) {
            dependency(d->dependency);
            main.bit_short(d->class_version);
            bit(d->enabled);
            auto &sub = d->persistent_subent;
            if (r2013()) {
                bit(sub.leading_flag);
                std::int32_t code = sub.class_code;
                auto name = ascii_upper(sub.class_name);
                if (name == "ACDBASSOCSINGLEEDGEPERSSUBENTID")
                    code = 1;
                else if (name == "ACDBASSOCEDGEPERSSUBENTID")
                    code = 3;
                else if (name == "ACDBASSOCASMBASEDENTITYPERSSUBENTID")
                    code = 5;
                main.bit_long(code);
                for (auto item : sub.values)
                    main.bit_long(item);
            } else {
                auto name = sub.class_name;
                if (name.empty()) {
                    if (sub.class_code == 1)
                        name = "AcDbAssocSingleEdgePersSubentId";
                    else if (sub.class_code == 3)
                        name = "AcDbAssocEdgePersSubentId";
                    else if (sub.class_code == 5)
                        name = "AcDbAssocAsmBasedEntityPersSubentId";
                }
                str(name);
            }
            bit(sub.dependent_on_compound_object);
        } else if (auto *d = std::get_if<AssocSurfaceActionBody>(&data))
            surface(*d);
        else if (auto *d = std::get_if<AssocAction>(&data))
            action(*d);
        else if (auto *d = std::get_if<AssocNetwork>(&data)) {
            action(d->action);
            main.bit_short(d->network_version);
            main.bit_long(d->network_action_index);
            main.bit_long(std::int32_t(d->actions.size()));
            for (auto &item : d->actions) {
                bit(item.is_owned);
                href(item.is_owned ? 3 : 4, item.dependency);
            }
            main.bit_long(std::int32_t(d->owned_actions.size()));
            handles(4, d->owned_actions);
        } else if (auto *d = std::get_if<AssocAnnotationActionBody>(&data))
            annotation(*d);
        else if (auto *d = std::get_if<AssocPersSubentManager>(&data)) {
            main.bit_long(d->class_version);
            for (auto m : d->markers)
                main.bit_long(m);
            for (auto item : d->values)
                main.bit_long(item);
            bit(d->final_flag);
        } else if (auto *d = std::get_if<AssocEdgeActionParam>(&data)) {
            single(d->single_dependency);
            href(3, d->parameter);
            bit(d->has_action);
            if (d->has_action) {
                main.bit_long(d->action_type);
                curve(d->curve);
            }
        } else if (auto *d = std::get_if<Assoc2dConstraintGroup>(&data))
            constraint_group(*d);
        else if (auto *d = std::get_if<AssocVariable>(&data)) {
            action(d->action);
            main.bit_long(d->class_version);
            str(d->name);
            str(d->expression);
            str(d->evaluator);
            str(d->description);
            eval(d->value);
            bit(d->has_cached_value);
            if (d->has_cached_value)
                str(d->cached_value);
            bit(d->flag);
            main.bit_long(std::int32_t(d->dependencies.size()));
            for (auto &item : d->dependencies) {
                href(3, item.dependency);
                main.bit_long(item.flags);
            }
        } else if (auto *d = std::get_if<AssocActionParam>(&data))
            action_param(*d);
        else if (auto *d = std::get_if<AssocCompoundActionParam>(&data))
            compound(*d);
        else if (auto *d = std::get_if<AssocPointRefActionParam>(&data))
            compound(d->compound);
        else if (auto *d = std::get_if<AssocOsnapPointRefActionParam>(&data)) {
            compound(d->compound);
            main.bit_short(d->status);
            byte(d->osnap_mode);
            main.bit_double(d->parameter);
        } else if (auto *d = std::get_if<AssocSingleDependencyActionParam>(&data))
            single(*d);
        else if (auto *d = std::get_if<AssocPathActionParam>(&data)) {
            compound(d->compound);
            main.bit_long(d->version);
        } else if (auto *d = std::get_if<AssocDimDependencyBody>(&data)) {
            main.bit_short(d->dependency_body_version);
            main.bit_short(d->base_version);
            str(d->name);
            main.bit_short(d->class_version);
        } else if (auto *d = std::get_if<AssocFaceActionParam>(&data)) {
            single(d->single_dependency);
            main.bit_long(d->index);
        } else if (auto *d = std::get_if<AssocVertexActionParam>(&data)) {
            single(d->single_dependency);
            vec(d->point);
        } else if (auto *d = std::get_if<AssocAsmBodyActionParam>(&data))
            asm_body(*d);
        else if (auto *d = std::get_if<AssocArrayParameters>(&data))
            array_parameters(*d);
        else if (auto *d = std::get_if<AssocArrayActionBody>(&data))
            array_body(*d);
        else if (auto *d = std::get_if<AssocArrayModifyActionBody>(&data)) {
            array_body(d->body);
            main.bit_short(d->status);
            main.bit_long(std::int32_t(d->item_locations.size()));
            for (auto &item : d->item_locations)
                for (auto x : item)
                    main.bit_long(x);
        } else if (auto *d = std::get_if<AssocDimensionAssociation>(&data))
            dimension(*d);
        else if (auto *d = std::get_if<PersSubentManager>(&data)) {
            main.bit_long(d->class_version);
            main.bit_long(d->marker_zero);
            main.bit_long(d->marker_two);
            main.bit_long(d->associative_step_count);
            main.bit_long(d->associative_subent_count);
            for (auto item : d->values)
                main.bit_long(item);
        } else if (auto *d = std::get_if<AssocViewRepActionBody>(&data)) {
            action_body(d->action_body);
            main.bit_short(d->class_version);
            href(3, d->view_rep);
            main.bit_long(d->view_type);
            main.bit_double(d->rotation);
        } else if (auto *d = std::get_if<AssocViewObjectActionParam>(&data)) {
            single(d->single_dependency);
            main.bit_short(d->class_version);
        } else if (auto *d = std::get_if<AssocViewRepHatchManager>(&data)) {
            compound(d->compound);
            main.bit_short(d->class_version);
            main.bit_long(std::int32_t(d->items.size()));
            for (auto &item : d->items) {
                main.bit_long_long(item.first_id);
                main.bit_long_long(item.second_id);
                main.bit_long(item.status);
                href(4, item.parameter);
            }
        } else if (auto *d = std::get_if<AssocViewRepHatchActionParam>(&data)) {
            single(d->single_dependency);
            main.bit_short(d->class_version);
            vec(d->normal);
            main.bit_long(d->hatch_index);
            main.bit_long(d->flags);
        } else if (auto *d = std::get_if<AssocSmartCenterActionBody>(&data)) {
            action_body(d->action_body);
            parameter_body(d->parameter_body);
            main.bit_long(d->version);
        } else if (auto *d = std::get_if<AssocViewLabelActionParam>(&data)) {
            single(d->single_dependency);
            main.bit_short(d->class_version);
            main.bit_short(d->label_version);
            raw2(d->offset);
            byte(d->flag);
        } else
            throw std::invalid_argument("Cannot rewrite unknown associative object");
    }
};
std::string encode_associative_record(unsigned v, const char *page, int type, const AssociativeObject &rec) {
    require(!std::holds_alternative<std::monostate>(rec.data), "Cannot rewrite unknown associative object");
    AssocWriter w;
    w.v = v;
    w.page = page;
    auto split = object_prefix(w.main, w.refs, v, type, rec.handle, rec.owner,
                               rec.xdictionary_handle.value_or(0), rec.reactors);
    w.write(rec.data);
    return finish_object(w.main, w.strings, w.refs, v, split);
}
} // namespace
std::string detail::edit_dwg_associative_records(std::string_view file, const DwgR2000Objects &o,
                                                 const std::vector<AssociativeObject> &records) {
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    LifecycleEdits edits;
    for (auto &rec : records) {
        require(o.associative_records.count(rec.handle), "Missing associative object");
        require(o.records.count(rec.handle), "Missing associative record");
        if (rec.dxf_name != o.associative_records.at(rec.handle).dxf_name)
            throw std::invalid_argument("Associative object name mismatch");
        edits[rec.handle] =
            encode_associative_record(v, drawing_code_page(file, v), o.records.at(rec.handle).type, rec);
    }
    return lifecycle_rewrite(file, o, edits, {}, {});
}
