// SPDX-License-Identifier: MPL-2.0
// Native encode/rewrite for typed DWG dynamic-block objects.
namespace {
struct DynamicWriter {
    DwgBitWriter main, refs, strings;
    unsigned v{};
    const char *page = nullptr;
    void str(std::string_view s) { symbol_string(main, strings, v, s, page); }
    void vec(Vector3 p) { vector_bits(main, p); }
    void raw2(Vector2 p) {
        raw_double(main, p.x);
        raw_double(main, p.y);
    }
    void href(unsigned code, std::uint64_t h) { refs.absolute_handle(code, h); }
    void eval_value(std::int16_t code, const BlockEvalValue &value) {
        switch (code) {
        case 40:
            main.bit_double(std::get<double>(value));
            break;
        case 10:
        case 11:
            raw2(std::get<Vector2>(value));
            break;
        case 1:
            str(std::get<std::string>(value));
            break;
        case 90:
            main.bit_long(std::get<std::int32_t>(value));
            break;
        case 91:
            href(5, std::get<std::uint64_t>(value));
            break;
        case 70:
            main.bit_short(std::get<std::int16_t>(value));
            break;
        default:
            break;
        }
    }
    void eval(const BlockEvalExpression &e) {
        main.bit_long(e.parent_id);
        main.bit_long(e.major);
        main.bit_long(e.minor);
        main.bit_short(e.value_code);
        eval_value(e.value_code, e.value);
        main.bit_long(e.node_id);
    }
    void element(const BlockElement &e) {
        eval(e.eval);
        str(e.name);
        main.bit_long(e.major);
        main.bit_long(e.minor);
        main.bit_long(e.eed_1071);
    }
    void connection(const BlockConnection &c) {
        main.bit_long(c.code);
        str(c.name);
    }
    void property(const BlockParameterProperty &p) {
        main.bit_long(std::int32_t(p.connections.size()));
        for (auto &c : p.connections)
            connection(c);
    }
    void parameter(const BlockParameter &p) {
        element(p.element);
        main.bits(1, p.show_properties);
        main.bits(1, p.chain_actions);
    }
    void one_point(const BlockOnePointParameter &p) {
        parameter(p.parameter);
        vec(p.definition_point);
        for (auto &item : p.properties)
            property(item);
        main.bit_long(p.property_count);
    }
    void two_point(const BlockTwoPointParameter &p) {
        parameter(p.parameter);
        vec(p.definition_base_point);
        vec(p.definition_end_point);
        for (auto &item : p.properties)
            property(item);
        for (auto item : p.property_states)
            main.bit_long(item);
        main.bit_short(p.parameter_base_location);
    }
    void grip(const BlockGrip &g) {
        element(g.element);
        main.bit_long(g.flags_91);
        main.bit_long(g.flags_92);
        vec(g.location);
        main.bits(1, g.insert_cycling);
        main.bit_long(g.insert_cycling_weight);
    }
    void action(const BlockAction &a) {
        element(a.element);
        vec(a.display_location);
        main.bit_long(std::int32_t(a.dependencies.size()));
        for (auto h : a.dependencies)
            href(4, h);
        main.bit_long(std::int32_t(a.action_ids.size()));
        for (auto id : a.action_ids)
            main.bit_long(id);
    }
    void action_base(const BlockActionWithBasePoint &a) {
        action(a.action);
        vec(a.offset);
        require(a.connections.size() >= 2, "Incomplete action connections");
        connection(a.connections[0]);
        connection(a.connections[1]);
        main.bits(1, a.dependent);
        vec(a.base_point);
    }
    void value_set(const BlockParameterValueSet &s) {
        main.bit_long(s.flags);
        main.bit_double(s.minimum);
        main.bit_double(s.maximum);
        main.bit_double(s.increment);
        main.bit_short(std::int16_t(s.values.size()));
        for (auto x : s.values)
            main.bit_double(x);
    }
    void constraint(const BlockConstraintParameter &c) {
        two_point(c.parameter);
        href(5, c.dependency);
    }
    void linear_constraint(const BlockLinearConstraintParameter &p) {
        constraint(p.constraint);
        str(p.expression_name);
        str(p.expression_description);
        main.bit_double(p.value);
        value_set(p.value_set);
    }
    void offsets(const BlockActionOffsets &o) {
        main.bit_double(o.distance_multiplier);
        main.bit_double(o.angle_offset);
        main.bits(8, o.flags);
    }
    template <class T>
    const T &as(const DwgDynamicRecord &r) {
        auto *v = std::get_if<T>(&r.data);
        require(v, "Dynamic payload mismatch");
        return *v;
    }
};
std::size_t entity_prefix(DwgBitWriter &main, DwgBitWriter &refs, unsigned v, int type,
                          const DwgDynamicRecord &rec) {
    write_object_type(main, v, type);
    auto split = main.position();
    if (v >= 1015 && v < 1024)
        main.little_endian(4, 0);
    main.absolute_handle(0, rec.handle);
    main.bit_short(0);
    main.bits(1, 0);
    auto mode = rec.properties.mode ? rec.properties.mode : 2u;
    require(mode <= 2, "Reserved DWG entity mode");
    main.bits(2, mode);
    if (!mode)
        refs.absolute_handle(4, rec.owner);
    require(rec.reactors.size() <= 1000000, "Invalid dynamic reactor count");
    main.bit_long(std::int32_t(rec.reactors.size()));
    for (auto reactor : rec.reactors)
        refs.absolute_handle(4, reactor);
    if (v >= 1018)
        main.bits(1, !rec.extension_dictionary);
    if (rec.extension_dictionary || v < 1018)
        refs.absolute_handle(3, rec.extension_dictionary);
    if (v >= 1027)
        main.bits(1, 0);
    main.bit_short(static_cast<std::int16_t>(rec.properties.color.approximate_index()));
    main.bit_double(rec.properties.linetype_scale == 0 ? 1 : rec.properties.linetype_scale);
    refs.absolute_handle(5, rec.properties.layer);
    main.bits(2, rec.properties.linetype_flags);
    if (rec.properties.linetype_flags == 3)
        refs.absolute_handle(5, rec.properties.linetype);
    if (v >= 1021) {
        main.bits(2, rec.properties.material_flags);
        main.bits(8, rec.properties.shadow_flags);
        if (rec.properties.material_flags == 3)
            refs.absolute_handle(5, rec.properties.material);
    }
    main.bits(2, rec.properties.plotstyle_flags);
    if (rec.properties.plotstyle_flags == 3)
        refs.absolute_handle(5, rec.properties.plotstyle);
    if (v >= 1024)
        main.bits(3, 0);
    main.bit_short(rec.properties.invisible ? 1 : 0);
    main.bits(8, rec.properties.lineweight_index ? rec.properties.lineweight_index : 29);
    return split;
}
std::string encode_dynamic_record(unsigned v, const char *page, int type, const DwgDynamicRecord &rec) {
    require(rec.kind != DynamicBlockKind::Unknown, "Unknown dynamic object");
    DynamicWriter w;
    w.v = v;
    w.page = page;
    const bool entity = rec.entity || rec.kind == DynamicBlockKind::EmptyEntity ||
                        rec.kind == DynamicBlockKind::AngularConstraintParameterEntity;
    auto split = entity ? entity_prefix(w.main, w.refs, v, type, rec)
                        : object_prefix(w.main, w.refs, v, type, rec.handle, rec.owner, rec.extension_dictionary,
                                        rec.reactors);
    auto kind = rec.kind;
    if (kind == DynamicBlockKind::Representation) {
        auto &d = w.as<BlockRepresentationData>(rec);
        w.main.bit_short(d.flags);
        w.href(5, d.block);
    } else if (kind == DynamicBlockKind::ProxyNode)
        w.eval(w.as<BlockEvalExpression>(rec));
    else if (kind == DynamicBlockKind::GripLocationComponent) {
        auto &d = w.as<BlockGripExpression>(rec);
        w.eval(d.eval);
        w.main.bit_long(d.grip_type);
        w.str(d.expression);
    } else if (kind == DynamicBlockKind::AlignmentGrip || kind == DynamicBlockKind::LinearGrip) {
        auto &d = w.as<BlockOrientedGrip>(rec);
        w.grip(d.grip);
        w.vec(d.orientation);
    } else if (kind == DynamicBlockKind::FlipGrip) {
        auto &d = w.as<BlockFlipGrip>(rec);
        w.grip(d.grip);
        w.main.bit_long(d.combined_state);
        w.vec(d.orientation);
    } else if (kind == DynamicBlockKind::LookupGrip || kind == DynamicBlockKind::PolarGrip ||
               kind == DynamicBlockKind::RotationGrip || kind == DynamicBlockKind::VisibilityGrip ||
               kind == DynamicBlockKind::XYGrip || kind == DynamicBlockKind::PropertiesTableGrip)
        w.grip(w.as<BlockGrip>(rec));
    else if (kind == DynamicBlockKind::AlignmentParameter) {
        auto &d = w.as<BlockAlignmentParameter>(rec);
        w.two_point(d.parameter);
        w.main.bits(1, d.align_perpendicular);
    } else if (kind == DynamicBlockKind::BasePointParameter) {
        auto &d = w.as<BlockBasePointParameter>(rec);
        w.one_point(d.parameter);
        w.vec(d.point);
        w.vec(d.base_point);
    } else if (kind == DynamicBlockKind::FlipParameter) {
        auto &d = w.as<BlockFlipParameter>(rec);
        w.two_point(d.parameter);
        w.str(d.flip_label);
        w.str(d.flip_label_description);
        w.str(d.base_state_label);
        w.str(d.flipped_state_label);
        w.vec(d.definition_label_point);
        w.main.bit_long(d.flags_96);
        w.str(d.tooltip);
    } else if (kind == DynamicBlockKind::LinearParameter) {
        auto &d = w.as<BlockLinearParameter>(rec);
        w.two_point(d.parameter);
        w.str(d.distance_name);
        w.str(d.distance_description);
        w.main.bit_double(d.distance);
        w.value_set(d.value_set);
    } else if (kind == DynamicBlockKind::LookupParameter) {
        auto &d = w.as<BlockLookupParameter>(rec);
        w.one_point(d.parameter);
        w.main.bit_long(d.index);
        w.str(d.lookup_name);
        w.str(d.lookup_description);
    } else if (kind == DynamicBlockKind::PointParameter) {
        auto &d = w.as<BlockPointParameter>(rec);
        w.one_point(d.parameter);
        w.str(d.position_name);
        w.str(d.position_description);
        w.vec(d.definition_label_point);
    } else if (kind == DynamicBlockKind::PolarParameter) {
        auto &d = w.as<BlockPolarParameter>(rec);
        w.two_point(d.parameter);
        w.str(d.distance_name);
        w.str(d.distance_description);
        w.str(d.angle_name);
        w.str(d.angle_description);
        w.main.bit_double(d.offset);
        w.value_set(d.distance_value_set);
        w.value_set(d.angle_value_set);
    } else if (kind == DynamicBlockKind::RotationParameter) {
        auto &d = w.as<BlockRotationParameter>(rec);
        w.two_point(d.parameter);
        w.vec(d.definition_base_angle_point);
        w.str(d.angle_name);
        w.str(d.angle_description);
        w.main.bit_double(d.angle);
        w.value_set(d.value_set);
    } else if (kind == DynamicBlockKind::UserParameter) {
        auto &d = w.as<BlockUserParameter>(rec);
        w.one_point(d.parameter);
        w.main.bit_short(d.flags);
        w.href(5, d.associated_variable);
        w.str(d.expression);
        w.main.bit_short(d.value_code);
        w.eval_value(d.value_code, d.value);
        w.main.bit_short(d.value_type);
    } else if (kind == DynamicBlockKind::XYParameter) {
        auto &d = w.as<BlockXYParameter>(rec);
        w.two_point(d.parameter);
        w.str(d.y_label);
        w.str(d.x_label);
        w.str(d.y_label_description);
        w.str(d.x_label_description);
        w.main.bit_double(d.x_value);
        w.main.bit_double(d.y_value);
        w.value_set(d.x_value_set);
        w.value_set(d.y_value_set);
    } else if (kind == DynamicBlockKind::VisibilityParameter) {
        auto &p = w.as<BlockVisibilityParameter>(rec);
        w.main.bit_long(p.eval_parent_id);
        w.main.bit_long(p.eval_major);
        w.main.bit_long(p.eval_minor);
        w.main.bit_short(p.eval_value_code);
        w.eval_value(p.eval_value_code, p.eval_value);
        w.main.bit_long(p.eval_node_id);
        w.str(p.element_name);
        w.main.bit_long(p.element_major);
        w.main.bit_long(p.element_minor);
        w.main.bit_long(p.element_eed_1071);
        w.main.bits(1, p.show_properties);
        w.main.bits(1, p.chain_actions);
        w.vec(p.definition_point);
        w.property(p.property_info[0]);
        w.property(p.property_info[1]);
        w.main.bit_long(p.property_count);
        w.main.bits(1, p.is_initialized);
        w.str(p.name);
        w.str(p.description);
        w.main.bits(1, p.unknown_bool);
        w.main.bit_long(std::int32_t(p.all_blocks.size()));
        for (auto h : p.all_blocks)
            w.href(4, h);
        w.main.bit_long(std::int32_t(p.states.size()));
        for (auto &st : p.states) {
            w.str(st.name);
            w.main.bit_long(std::int32_t(st.visible_blocks.size()));
            for (auto h : st.visible_blocks)
                w.href(4, h);
            w.main.bit_long(std::int32_t(st.visible_params.size()));
            for (auto h : st.visible_params)
                w.href(4, h);
        }
    } else if (kind == DynamicBlockKind::AngularConstraintParameter) {
        auto &d = w.as<BlockAngularConstraintParameter>(rec);
        w.constraint(d.constraint);
        w.vec(d.center_point);
        w.vec(d.end_point);
        w.str(d.expression_name);
        w.str(d.expression_description);
        w.main.bit_double(d.angle);
        w.main.bits(1, d.orientation_on_both_grips);
        w.value_set(d.value_set);
    } else if (kind == DynamicBlockKind::DiametricConstraintParameter ||
               kind == DynamicBlockKind::RadialConstraintParameter) {
        auto &d = w.as<BlockDistanceConstraintParameter>(rec);
        w.constraint(d.constraint);
        w.str(d.expression_name);
        w.str(d.expression_description);
        w.main.bit_double(d.distance);
        w.value_set(d.value_set);
    } else if (kind == DynamicBlockKind::AlignedConstraintParameter ||
               kind == DynamicBlockKind::LinearConstraintParameter ||
               kind == DynamicBlockKind::HorizontalConstraintParameter ||
               kind == DynamicBlockKind::VerticalConstraintParameter)
        w.linear_constraint(w.as<BlockLinearConstraintParameter>(rec));
    else if (kind == DynamicBlockKind::ParameterDependencyBody) {
        auto &d = w.as<BlockParameterDependencyBody>(rec);
        w.main.bit_short(d.dependency_body_version);
        w.main.bit_short(d.dimension_base_version);
        w.str(d.name);
        w.main.bit_short(d.class_version);
    } else if (kind == DynamicBlockKind::MoveAction) {
        auto &d = w.as<BlockMoveAction>(rec);
        w.action(d.action);
        w.connection(d.connections[0]);
        w.connection(d.connections[1]);
        w.offsets(d.offsets);
    } else if (kind == DynamicBlockKind::FlipAction) {
        auto &d = w.as<BlockFlipAction>(rec);
        w.action(d.action);
        for (auto &c : d.connections)
            w.connection(c);
    } else if (kind == DynamicBlockKind::RotateAction) {
        auto &d = w.as<BlockBasePointAction>(rec);
        w.action_base(d.action);
        require(!d.connections.empty(), "Incomplete rotate action");
        w.connection(d.connections[0]);
    } else if (kind == DynamicBlockKind::ScaleAction) {
        auto &d = w.as<BlockBasePointAction>(rec);
        w.action_base(d.action);
        require(d.connections.size() >= 3, "Incomplete scale action");
        for (int i = 0; i < 3; ++i)
            w.connection(d.connections[i]);
        w.main.bits(8, d.dwg_tail_byte);
    } else if (kind == DynamicBlockKind::ArrayAction) {
        auto &d = w.as<BlockArrayAction>(rec);
        w.action(d.action);
        for (auto &c : d.connections)
            w.connection(c);
        w.main.bit_double(d.row_offset);
        w.main.bit_double(d.column_offset);
    } else if (kind == DynamicBlockKind::LookupAction) {
        auto &d = w.as<BlockLookupAction>(rec);
        w.action(d.action);
        w.main.bit_long(d.row_count);
        w.main.bit_long(d.column_count);
        for (auto &e : d.expressions)
            w.str(e);
        for (auto &c : d.columns) {
            w.main.bit_long(c.node_id);
            w.main.bit_long(c.value_type);
            w.main.bit_long(c.property_type);
            w.main.bits(1, c.lookup_property);
            w.str(c.unmatched_name);
            w.main.bits(1, c.writable);
            w.str(c.connection_name);
        }
        w.main.bits(1, d.flag_280);
    } else if (kind == DynamicBlockKind::PropertiesTable) {
        auto &d = w.as<BlockPropertiesTable>(rec);
        w.one_point(d.parameter);
        for (auto &f : d.fields) {
            std::visit(
                [&](const auto &value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::string>)
                        w.str(value);
                    else if constexpr (std::is_same_v<T, bool>)
                        w.main.bits(1, value);
                    else if constexpr (std::is_same_v<T, std::int64_t>) {
                        if ((f.code >= 170 && f.code <= 179) || (f.code >= 270 && f.code <= 279) ||
                            (f.code >= 370 && f.code <= 389))
                            w.main.bit_short(std::int16_t(value));
                        else
                            w.main.bit_long(std::int32_t(value));
                    } else if constexpr (std::is_same_v<T, double>)
                        w.main.bit_double(value);
                    else if constexpr (std::is_same_v<T, std::uint64_t>)
                        w.href(5, value);
                },
                f.value);
        }
    } else if (kind == DynamicBlockKind::StretchAction) {
        auto &d = w.as<BlockStretchAction>(rec);
        w.action(d.action);
        w.connection(d.connections[0]);
        w.connection(d.connections[1]);
        w.main.bit_long(std::int32_t(d.points.size()));
        for (auto p : d.points)
            w.raw2(p);
        w.main.bit_long(std::int32_t(d.handles.size()));
        for (auto &item : d.handles) {
            w.href(4, item.handle);
            w.main.bit_short(std::int16_t(item.indexes.size()));
            for (auto i : item.indexes)
                w.main.bit_long(i);
        }
        w.main.bit_long(std::int32_t(d.codes.size()));
        for (auto &item : d.codes) {
            w.main.bit_long(item.code);
            w.main.bit_short(std::int16_t(item.indexes.size()));
            for (auto i : item.indexes)
                w.main.bit_long(i);
        }
        w.offsets(d.offsets);
    } else if (kind == DynamicBlockKind::PolarStretchAction) {
        auto &d = w.as<BlockPolarStretchAction>(rec);
        w.action(d.action);
        for (auto &c : d.connections)
            w.connection(c);
        w.main.bit_long(std::int32_t(d.points.size()));
        for (auto p : d.points)
            w.raw2(p);
        w.main.bit_long(std::int32_t(d.handles.size()));
        for (auto h : d.handles)
            w.href(4, h);
        w.main.bit_long(std::int32_t(d.bindings.size()));
        for (auto &item : d.bindings) {
            w.href(4, item.handle);
            w.main.bit_long(std::int32_t(item.indexes.size()));
            for (auto i : item.indexes)
                w.main.bit_long(i);
        }
        w.main.bit_long(std::int32_t(d.codes.size()));
        for (auto &item : d.codes) {
            w.main.bit_long(item.code);
            w.main.bit_long(std::int32_t(item.indexes.size()));
            for (auto i : item.indexes)
                w.main.bit_long(i);
        }
        w.main.bit_double(d.distance_multiplier);
        w.main.bit_double(d.angle_offset);
        w.main.bit_long(std::int32_t(d.extra.size()));
        for (auto x : d.extra)
            w.main.bit_long(x);
    } else if (kind == DynamicBlockKind::EmptyEntity) {
    } else if (kind == DynamicBlockKind::AngularConstraintParameterEntity) {
        auto &d = w.as<BlockAngularConstraintParameterEntity>(rec);
        w.constraint(d.constraint);
        w.vec(d.center_point);
        w.vec(d.label_point);
        w.str(d.expression_name);
        w.str(d.expression_description);
        w.main.bit_double(d.angle);
        w.main.bits(1, d.orientation_on_both_grips);
        w.value_set(d.value_set);
    } else
        throw std::invalid_argument("Unsupported dynamic object rewrite");
    return finish_object(w.main, w.strings, w.refs, v, split);
}
} // namespace
std::string detail::edit_dwg_dynamic_record(std::string_view file, const DwgR2000Objects &o, std::uint64_t h,
                                            const DwgDynamicRecord &rec) {
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    require(o.dynamic_records.count(h), "Missing dynamic object");
    require(o.records.count(h), "Missing dynamic record");
    auto type = o.records.at(h).type;
    LifecycleEdits edits;
    edits[h] = encode_dynamic_record(v, drawing_code_page(file, v), type, rec);
    return lifecycle_rewrite(file, o, edits, {}, {});
}
std::string detail::add_dwg_dynamic_record(std::string_view file, const DwgR2000Objects &o, DwgDynamicRecord rec,
                                           std::uint64_t target, std::uint64_t &id) {
    require(rec.kind != DynamicBlockKind::Unknown && !rec.dxf_name.empty(), "Unknown dynamic object");
    rec.entity = rec.entity || rec.kind == DynamicBlockKind::EmptyEntity ||
                 rec.kind == DynamicBlockKind::AngularConstraintParameterEntity;
    auto item = rec.entity ? std::int16_t(498) : std::int16_t(499);
    auto cpp = rec.cpp_name.empty() ? dynamic_block_cpp_class_name(rec.dxf_name).value_or("AcDbObject")
                                    : rec.cpp_name;
    auto registered = register_dwg_classes(file, o, {{rec.dxf_name, cpp, item}});
    if (registered != file)
        return add_dwg_dynamic_record(registered, read_dwg_objects(registered), rec, target, id);
    LifecycleStorage s(file);
    require(s.version >= 1018, "Dynamic allocation requires R2004+");
    auto header = allocate_symbol_handle(lifecycle_header(file, s.version), s.version,
                                         o.records.rbegin()->first, id);
    rec.handle = id;
    if (!rec.properties.layer) {
        auto layer = symbol_id(o, s.version, 51, "0");
        require(layer.has_value(), "Missing layer 0");
        rec.properties.layer = *layer;
    }
    if (!rec.properties.mode)
        rec.properties.mode = 2;
    if (rec.properties.linetype_scale == 0)
        rec.properties.linetype_scale = 1;
    int type = -1;
    for (const auto &c : o.classes)
        if (c.dxf_name == rec.dxf_name) {
            type = c.number;
            break;
        }
    require(type >= 500, "Missing dynamic class");
    LifecycleEdits added{{id, encode_dynamic_record(s.version, s.page, type, rec)}}, edits;
    if (rec.entity) {
        target = lifecycle_target(o, target);
        lifecycle_membership(s, o, target, 0, id, edits, added);
    }
    return lifecycle_rewrite(file, o, edits, {}, added, header, id + 1);
}
