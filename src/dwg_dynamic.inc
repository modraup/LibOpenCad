// SPDX-License-Identifier: MPL-2.0
// Native DWG dynamic-block object/entity and INSERT adapters.
std::string ascii_upper(std::string_view s) {
    std::string o(s);
    for (char &c : o)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return o;
}
bool is_dynamic_entity_name(std::string_view name) {
    auto n = ascii_upper(name);
    return n == "ALIGNMENTPARAMETERENTITY" || n == "BASEPOINTPARAMETERENTITY" || n == "FLIPPARAMETERENTITY" ||
           n == "LINEARPARAMETERENTITY" || n == "POINTPARAMETERENTITY" || n == "ROTATIONPARAMETERENTITY" ||
           n == "VISIBILITYPARAMETERENTITY" || n == "XYPARAMETERENTITY" || n == "FLIPGRIPENTITY" ||
           n == "LINEARGRIPENTITY" || n == "POLARGRIPENTITY" || n == "ROTATIONGRIPENTITY" ||
           n == "VISIBILITYGRIPENTITY" || n == "XYGRIPENTITY" ||
           n == "BLOCKANGULARCONSTRAINTPARAMETERENTITY";
}
bool is_dynamic_object_name(std::string_view name) {
    auto n = ascii_upper(name);
    if (n == "ACAD_EVALUATION_GRAPH" || n == "EVALUATION_GRAPH" || history_class(name))
        return false;
    static const char *const names[] = {
        "ACDBBLOCKPARAMDEPENDENCYBODY",
        "ACDB_BLOCKREPRESENTATION_DATA",
        "ACDB_DYNAMICBLOCKPROXYNODE",
        "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION",
        "BLOCKALIGNEDCONSTRAINTPARAMETER",
        "BLOCKALIGNMENTGRIP",
        "BLOCKALIGNMENTPARAMETER",
        "BLOCKANGULARCONSTRAINTPARAMETER",
        "BLOCKARRAYACTION",
        "BLOCKBASEPOINTPARAMETER",
        "BLOCKDIAMETRICCONSTRAINTPARAMETER",
        "BLOCKFLIPACTION",
        "BLOCKFLIPGRIP",
        "BLOCKFLIPPARAMETER",
        "BLOCKGRIPLOCATIONCOMPONENT",
        "BLOCKHORIZONTALCONSTRAINTPARAMETER",
        "BLOCKLINEARCONSTRAINTPARAMETER",
        "BLOCKLINEARGRIP",
        "BLOCKLINEARPARAMETER",
        "BLOCKLOOKUPACTION",
        "BLOCKLOOKUPGRIP",
        "BLOCKLOOKUPPARAMETER",
        "BLOCKMOVEACTION",
        "BLOCKPARAMDEPENDENCYBODY",
        "BLOCKPOINTPARAMETER",
        "BLOCKPOLARGRIP",
        "BLOCKPOLARPARAMETER",
        "BLOCKPOLARSTRETCHACTION",
        "BLOCKPROPERTIESTABLE",
        "BLOCKPROPERTIESTABLEGRIP",
        "BLOCKRADIALCONSTRAINTPARAMETER",
        "BLOCKREPRESENTATION",
        "BLOCKROTATEACTION",
        "BLOCKROTATIONGRIP",
        "BLOCKROTATIONPARAMETER",
        "BLOCKSCALEACTION",
        "BLOCKSTRETCHACTION",
        "BLOCKUSERPARAMETER",
        "BLOCKVERTICALCONSTRAINTPARAMETER",
        "BLOCKVISIBILITYGRIP",
        "BLOCKVISIBILITYPARAMETER",
        "BLOCKXYGRIP",
        "BLOCKXYPARAMETER",
        "DYNAMICBLOCKPROXYNODE",
        "DYNAMICBLOCKPURGEPREVENTER"};
    for (auto item : names)
        if (n == item)
            return true;
    return is_dynamic_entity_name(n);
}
DynamicBlockKind dynamic_kind(std::string_view name) {
    auto n = ascii_upper(name);
    if (n == "ACDB_BLOCKREPRESENTATION_DATA" || n == "BLOCKREPRESENTATION" ||
        n == "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION" || n == "DYNAMICBLOCKPURGEPREVENTER")
        return DynamicBlockKind::Representation;
    if (n == "DYNAMICBLOCKPROXYNODE" || n == "ACDB_DYNAMICBLOCKPROXYNODE")
        return DynamicBlockKind::ProxyNode;
    if (n == "BLOCKGRIPLOCATIONCOMPONENT")
        return DynamicBlockKind::GripLocationComponent;
    if (n == "BLOCKALIGNMENTGRIP")
        return DynamicBlockKind::AlignmentGrip;
    if (n == "BLOCKFLIPGRIP")
        return DynamicBlockKind::FlipGrip;
    if (n == "BLOCKLINEARGRIP")
        return DynamicBlockKind::LinearGrip;
    if (n == "BLOCKLOOKUPGRIP")
        return DynamicBlockKind::LookupGrip;
    if (n == "BLOCKPOLARGRIP")
        return DynamicBlockKind::PolarGrip;
    if (n == "BLOCKROTATIONGRIP")
        return DynamicBlockKind::RotationGrip;
    if (n == "BLOCKVISIBILITYGRIP")
        return DynamicBlockKind::VisibilityGrip;
    if (n == "BLOCKXYGRIP")
        return DynamicBlockKind::XYGrip;
    if (n == "BLOCKPROPERTIESTABLEGRIP")
        return DynamicBlockKind::PropertiesTableGrip;
    if (n == "BLOCKALIGNMENTPARAMETER")
        return DynamicBlockKind::AlignmentParameter;
    if (n == "BLOCKBASEPOINTPARAMETER")
        return DynamicBlockKind::BasePointParameter;
    if (n == "BLOCKFLIPPARAMETER")
        return DynamicBlockKind::FlipParameter;
    if (n == "BLOCKLINEARPARAMETER")
        return DynamicBlockKind::LinearParameter;
    if (n == "BLOCKLOOKUPPARAMETER")
        return DynamicBlockKind::LookupParameter;
    if (n == "BLOCKPOINTPARAMETER")
        return DynamicBlockKind::PointParameter;
    if (n == "BLOCKPOLARPARAMETER")
        return DynamicBlockKind::PolarParameter;
    if (n == "BLOCKROTATIONPARAMETER")
        return DynamicBlockKind::RotationParameter;
    if (n == "BLOCKUSERPARAMETER")
        return DynamicBlockKind::UserParameter;
    if (n == "BLOCKVISIBILITYPARAMETER")
        return DynamicBlockKind::VisibilityParameter;
    if (n == "BLOCKXYPARAMETER")
        return DynamicBlockKind::XYParameter;
    if (n == "BLOCKANGULARCONSTRAINTPARAMETER")
        return DynamicBlockKind::AngularConstraintParameter;
    if (n == "BLOCKDIAMETRICCONSTRAINTPARAMETER")
        return DynamicBlockKind::DiametricConstraintParameter;
    if (n == "BLOCKRADIALCONSTRAINTPARAMETER")
        return DynamicBlockKind::RadialConstraintParameter;
    if (n == "BLOCKALIGNEDCONSTRAINTPARAMETER")
        return DynamicBlockKind::AlignedConstraintParameter;
    if (n == "BLOCKLINEARCONSTRAINTPARAMETER")
        return DynamicBlockKind::LinearConstraintParameter;
    if (n == "BLOCKHORIZONTALCONSTRAINTPARAMETER")
        return DynamicBlockKind::HorizontalConstraintParameter;
    if (n == "BLOCKVERTICALCONSTRAINTPARAMETER")
        return DynamicBlockKind::VerticalConstraintParameter;
    if (n == "ACDBBLOCKPARAMDEPENDENCYBODY" || n == "BLOCKPARAMDEPENDENCYBODY")
        return DynamicBlockKind::ParameterDependencyBody;
    if (n == "BLOCKMOVEACTION")
        return DynamicBlockKind::MoveAction;
    if (n == "BLOCKFLIPACTION")
        return DynamicBlockKind::FlipAction;
    if (n == "BLOCKROTATEACTION")
        return DynamicBlockKind::RotateAction;
    if (n == "BLOCKSCALEACTION")
        return DynamicBlockKind::ScaleAction;
    if (n == "BLOCKARRAYACTION")
        return DynamicBlockKind::ArrayAction;
    if (n == "BLOCKLOOKUPACTION")
        return DynamicBlockKind::LookupAction;
    if (n == "BLOCKSTRETCHACTION")
        return DynamicBlockKind::StretchAction;
    if (n == "BLOCKPOLARSTRETCHACTION")
        return DynamicBlockKind::PolarStretchAction;
    if (n == "BLOCKPROPERTIESTABLE")
        return DynamicBlockKind::PropertiesTable;
    if (n == "BLOCKANGULARCONSTRAINTPARAMETERENTITY")
        return DynamicBlockKind::AngularConstraintParameterEntity;
    if (is_dynamic_entity_name(n))
        return DynamicBlockKind::EmptyEntity;
    return DynamicBlockKind::Unknown;
}
struct DynamicStream {
    DwgBitReader main, refs, strings;
    std::size_t end{};
    bool text = false;
    unsigned v{};
    const char *page = nullptr;
    std::uint64_t self{};
    DynamicStream(std::string_view body, std::size_t end_bits)
        : main(body, end_bits), refs(body), strings(body, end_bits), end(end_bits) {}
    std::string str() {
        return v < 1021 ? symbol_text(main, false, page) : text ? symbol_text(strings, true, page) : std::string();
    }
    double real() {
        auto x = main.bit_double();
        require(std::isfinite(x), "Nonfinite dynamic value");
        return x;
    }
    Vector3 vec() { return {real(), real(), real()}; }
    Vector2 raw2() {
        double x = main.raw_double(), y = main.raw_double();
        require(std::isfinite(x) && std::isfinite(y), "Nonfinite dynamic point");
        return {x, y};
    }
    std::size_t count() {
        auto n = main.bit_long();
        require(n >= 0 && n <= 1000000, "Invalid dynamic count");
        return std::size_t(n);
    }
    std::size_t short_count() {
        auto n = main.bit_short();
        require(n >= 0, "Invalid dynamic count");
        return std::size_t(n);
    }
    std::uint64_t href() { return refs.handle(self).value; }
    BlockEvalValue eval_value(std::int16_t code) {
        switch (code) {
        case 40:
            return real();
        case 10:
        case 11:
            return raw2();
        case 1:
            return str();
        case 90:
            return main.bit_long();
        case 91:
            return href();
        case 70:
            return main.bit_short();
        default:
            return std::monostate{};
        }
    }
    BlockEvalExpression eval() {
        BlockEvalExpression e;
        e.parent_id = main.bit_long();
        e.major = main.bit_long();
        e.minor = main.bit_long();
        e.value_code = main.bit_short();
        e.value = eval_value(e.value_code);
        e.node_id = main.bit_long();
        return e;
    }
    BlockElement element() {
        BlockElement e;
        e.eval = eval();
        e.name = str();
        e.major = main.bit_long();
        e.minor = main.bit_long();
        e.eed_1071 = main.bit_long();
        return e;
    }
    BlockConnection connection() { return {main.bit_long(), str()}; }
    BlockParameterProperty property() {
        BlockParameterProperty p;
        auto n = count();
        p.connections.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            p.connections.push_back(connection());
        return p;
    }
    BlockParameter parameter() {
        BlockParameter p;
        p.element = element();
        p.show_properties = main.bits(1);
        p.chain_actions = main.bits(1);
        return p;
    }
    BlockOnePointParameter one_point() {
        BlockOnePointParameter p;
        p.parameter = parameter();
        p.definition_point = vec();
        p.properties[0] = property();
        p.properties[1] = property();
        p.property_count = main.bit_long();
        return p;
    }
    BlockTwoPointParameter two_point() {
        BlockTwoPointParameter p;
        p.parameter = parameter();
        p.definition_base_point = vec();
        p.definition_end_point = vec();
        for (auto &item : p.properties)
            item = property();
        for (auto &item : p.property_states)
            item = main.bit_long();
        p.parameter_base_location = main.bit_short();
        return p;
    }
    BlockGrip grip() {
        BlockGrip g;
        g.element = element();
        g.flags_91 = main.bit_long();
        g.flags_92 = main.bit_long();
        g.location = vec();
        g.insert_cycling = main.bits(1);
        g.insert_cycling_weight = main.bit_long();
        return g;
    }
    BlockAction action() {
        BlockAction a;
        a.element = element();
        a.display_location = vec();
        auto n = count();
        a.dependencies.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            a.dependencies.push_back(href());
        n = count();
        a.action_ids.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            a.action_ids.push_back(main.bit_long());
        return a;
    }
    BlockActionWithBasePoint action_base() {
        BlockActionWithBasePoint a;
        a.action = action();
        a.offset = vec();
        a.connections.push_back(connection());
        a.connections.push_back(connection());
        a.dependent = main.bits(1);
        a.base_point = vec();
        return a;
    }
    BlockParameterValueSet value_set() {
        BlockParameterValueSet s;
        s.flags = main.bit_long();
        s.minimum = real();
        s.maximum = real();
        s.increment = real();
        auto n = short_count();
        s.values.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            s.values.push_back(real());
        return s;
    }
    BlockConstraintParameter constraint() { return {two_point(), href()}; }
    BlockLinearConstraintParameter linear_constraint() {
        BlockLinearConstraintParameter p;
        p.constraint = constraint();
        p.expression_name = str();
        p.expression_description = str();
        p.value = real();
        p.value_set = value_set();
        return p;
    }
    BlockActionOffsets offsets() { return {real(), real(), static_cast<std::uint8_t>(main.bits(8))}; }
    BlockStretchHandle stretch_handle(bool short_indexes) {
        BlockStretchHandle item;
        item.handle = href();
        auto n = short_indexes ? short_count() : count();
        item.indexes.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            item.indexes.push_back(main.bit_long());
        return item;
    }
    BlockStretchCode stretch_code(bool short_indexes) {
        BlockStretchCode item;
        item.code = main.bit_long();
        auto n = short_indexes ? short_count() : count();
        item.indexes.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            item.indexes.push_back(main.bit_long());
        return item;
    }
};
DynamicStream open_dynamic(std::string_view body, const DwgR2000Record &record, std::uint64_t h, unsigned v,
                           const char *page) {
    DynamicStream s(body, record.main_bits);
    s.v = v;
    s.page = page;
    s.self = h;
    if (v >= 1021) {
        require(s.end > record.handle_end_bit, "Missing dynamic text flag");
        s.strings.seek(--s.end);
        s.text = s.strings.bits(1);
        if (s.text) {
            auto trailer = [&]() {
                require(s.end >= 16, "Invalid dynamic text trailer");
                s.end -= 16;
                s.strings.seek(s.end);
                return s.strings.little_endian(2);
            };
            auto n = trailer();
            if (n & 32768)
                n = (n & 32767) + (trailer() << 15);
            require(n <= s.end, "Invalid dynamic string length");
            s.strings = DwgBitReader(body, s.end);
            s.end -= n;
            s.strings.seek(s.end);
        }
    }
    s.main = DwgBitReader(body, s.end);
    s.main.seek(record.handle_end_bit);
    s.refs = DwgBitReader(body);
    s.refs.seek(record.main_bits);
    return s;
}
void finish_dynamic(const DynamicStream &s, std::string_view body) {
    require(s.main.position() == s.end, "Unrecognized DWG dynamic main-stream tail");
    require(body.size() * 8 - s.refs.position() < 8, "Unrecognized DWG dynamic handle-stream tail");
}
void read_dynamic_common(DynamicStream &s, DwgDynamicRecord &out, bool entity, DwgR2000Record &record) {
    if (entity) {
        std::vector<DwgEedItem> eed;
        out.properties = read_entity_properties(s.main, s.refs, s.self, s.end, s.v, record, nullptr, &eed);
        out.owner = out.properties.owner;
        out.extension_dictionary = out.properties.extension_dictionary;
        out.reactors = out.properties.reactors;
        return;
    }
    skip_entity_prefix(s.main, s.end, false, s.v);
    out.owner = s.href();
    auto n = s.main.bit_long();
    require(n >= 0 && n <= 1000000, "Invalid dynamic reactor count");
    for (int i = 0; i < n; ++i)
        out.reactors.push_back(s.href());
    if (s.v < 1018 || !s.main.bits(1))
        out.extension_dictionary = s.href();
    if (s.v >= 1027)
        require(!s.main.bits(1), "Dynamic object datastore not supported");
}
DwgInsert read_insert_record(std::string_view body, DwgR2000Record &record, std::uint64_t h, unsigned v,
                             bool minsert, std::vector<DwgEedItem> *eed) {
    auto s = open_dynamic(body, record, h, v, nullptr);
    DwgInsert out;
    out.minsert = minsert;
    out.properties = read_entity_properties(s.main, s.refs, h, s.end, v, record, nullptr, eed);
    out.insertion = s.vec();
    if (v < 1015)
        out.scale = s.vec();
    else {
        auto flags = s.main.bits(2);
        if (flags == 3)
            out.scale = {1, 1, 1};
        else if (flags == 2) {
            auto x = s.main.raw_double();
            require(std::isfinite(x), "Nonfinite INSERT scale");
            out.scale = {x, x, x};
        } else if (flags == 1) {
            out.scale.x = 1;
            out.scale.y = s.main.default_double(1);
            out.scale.z = s.main.default_double(1);
        } else {
            auto x = s.main.raw_double();
            require(std::isfinite(x), "Nonfinite INSERT scale");
            out.scale.x = x;
            out.scale.y = s.main.default_double(x);
            out.scale.z = s.main.default_double(x);
        }
        require(std::isfinite(out.scale.y) && std::isfinite(out.scale.z), "Nonfinite INSERT scale");
    }
    out.rotation = s.real();
    out.normal = s.vec();
    out.has_attributes = s.main.bits(1);
    if (v >= 1018 && out.has_attributes)
        s.main.bit_long();
    if (minsert) {
        out.columns = s.main.bit_short();
        out.rows = s.main.bit_short();
        out.column_spacing = s.real();
        out.row_spacing = s.real();
    }
    out.block = s.href();
    while (body.size() * 8 - s.refs.position() >= 8)
        s.refs.handle(h);
    if (s.main.position() != s.end)
        require(s.end - s.main.position() < 8, "Unrecognized INSERT main-stream tail");
    return out;
}
DwgDynamicRecord read_dynamic_record(std::string_view body, DwgR2000Record &record, std::uint64_t h, unsigned v,
                                     const char *page, const std::string &type, bool entity) {
    require(v >= 1018, "Native DWG dynamic objects require R2004+");
    auto s = open_dynamic(body, record, h, v, page);
    DwgDynamicRecord out;
    out.handle = h;
    out.dxf_name = type;
    out.entity = entity;
    out.kind = dynamic_kind(type);
    read_dynamic_common(s, out, entity, record);
    auto table_int = [&](std::int32_t code, auto value) {
        BlockPropertiesTableField f;
        f.code = code;
        f.value = std::int64_t(value);
        return f;
    };
    if (out.kind == DynamicBlockKind::EmptyEntity) {
        finish_dynamic(s, body);
        return out;
    }
    if (out.kind == DynamicBlockKind::Representation) {
        BlockRepresentationData d;
        d.flags = s.main.bit_short();
        d.block = s.href();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::ProxyNode)
        out.data = s.eval();
    else if (out.kind == DynamicBlockKind::GripLocationComponent) {
        BlockGripExpression d;
        d.eval = s.eval();
        d.grip_type = s.main.bit_long();
        d.expression = s.str();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::AlignmentGrip || out.kind == DynamicBlockKind::LinearGrip) {
        BlockOrientedGrip d;
        d.grip = s.grip();
        d.orientation = s.vec();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::FlipGrip) {
        BlockFlipGrip d;
        d.grip = s.grip();
        d.combined_state = s.main.bit_long();
        d.orientation = s.vec();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::LookupGrip || out.kind == DynamicBlockKind::PolarGrip ||
               out.kind == DynamicBlockKind::RotationGrip || out.kind == DynamicBlockKind::VisibilityGrip ||
               out.kind == DynamicBlockKind::XYGrip || out.kind == DynamicBlockKind::PropertiesTableGrip)
        out.data = s.grip();
    else if (out.kind == DynamicBlockKind::AlignmentParameter) {
        BlockAlignmentParameter d;
        d.parameter = s.two_point();
        d.align_perpendicular = s.main.bits(1);
        out.data = d;
    } else if (out.kind == DynamicBlockKind::BasePointParameter) {
        BlockBasePointParameter d;
        d.parameter = s.one_point();
        d.point = s.vec();
        d.base_point = s.vec();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::FlipParameter) {
        BlockFlipParameter d;
        d.parameter = s.two_point();
        d.flip_label = s.str();
        d.flip_label_description = s.str();
        d.base_state_label = s.str();
        d.flipped_state_label = s.str();
        d.definition_label_point = s.vec();
        d.flags_96 = s.main.bit_long();
        d.tooltip = s.str();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::LinearParameter) {
        BlockLinearParameter d;
        d.parameter = s.two_point();
        d.distance_name = s.str();
        d.distance_description = s.str();
        d.distance = s.real();
        d.value_set = s.value_set();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::LookupParameter) {
        BlockLookupParameter d;
        d.parameter = s.one_point();
        d.index = s.main.bit_long();
        d.lookup_name = s.str();
        d.lookup_description = s.str();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::PointParameter) {
        BlockPointParameter d;
        d.parameter = s.one_point();
        d.position_name = s.str();
        d.position_description = s.str();
        d.definition_label_point = s.vec();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::PolarParameter) {
        BlockPolarParameter d;
        d.parameter = s.two_point();
        d.distance_name = s.str();
        d.distance_description = s.str();
        d.angle_name = s.str();
        d.angle_description = s.str();
        d.offset = s.real();
        d.distance_value_set = s.value_set();
        d.angle_value_set = s.value_set();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::RotationParameter) {
        BlockRotationParameter d;
        d.parameter = s.two_point();
        d.definition_base_angle_point = s.vec();
        d.angle_name = s.str();
        d.angle_description = s.str();
        d.angle = s.real();
        d.value_set = s.value_set();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::UserParameter) {
        BlockUserParameter d;
        d.parameter = s.one_point();
        d.flags = s.main.bit_short();
        d.associated_variable = s.href();
        d.expression = s.str();
        d.value_code = s.main.bit_short();
        d.value = s.eval_value(d.value_code);
        d.value_type = s.main.bit_short();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::XYParameter) {
        BlockXYParameter d;
        d.parameter = s.two_point();
        d.y_label = s.str();
        d.x_label = s.str();
        d.y_label_description = s.str();
        d.x_label_description = s.str();
        d.x_value = s.real();
        d.y_value = s.real();
        d.x_value_set = s.value_set();
        d.y_value_set = s.value_set();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::VisibilityParameter) {
        BlockVisibilityParameter p;
        p.handle = h;
        p.owner = out.owner;
        p.eval_parent_id = s.main.bit_long();
        p.eval_major = s.main.bit_long();
        p.eval_minor = s.main.bit_long();
        p.eval_value_code = s.main.bit_short();
        p.eval_value = s.eval_value(p.eval_value_code);
        p.eval_node_id = s.main.bit_long();
        p.element_name = s.str();
        p.element_major = s.main.bit_long();
        p.element_minor = s.main.bit_long();
        p.element_eed_1071 = s.main.bit_long();
        p.show_properties = s.main.bits(1);
        p.chain_actions = s.main.bits(1);
        p.definition_point = s.vec();
        p.property_info[0] = s.property();
        p.property_info[1] = s.property();
        p.property_count = s.main.bit_long();
        p.is_initialized = s.main.bits(1);
        p.name = s.str();
        p.description = s.str();
        p.unknown_bool = s.main.bits(1);
        auto blocks = s.count();
        p.all_blocks.reserve(blocks);
        for (std::size_t i = 0; i < blocks; ++i)
            p.all_blocks.push_back(s.href());
        auto states = s.count();
        p.states.reserve(states);
        for (std::size_t i = 0; i < states; ++i) {
            BlockVisibilityState st;
            st.name = s.str();
            auto nb = s.count();
            st.visible_blocks.reserve(nb);
            for (std::size_t j = 0; j < nb; ++j)
                st.visible_blocks.push_back(s.href());
            auto np = s.count();
            st.visible_params.reserve(np);
            for (std::size_t j = 0; j < np; ++j)
                st.visible_params.push_back(s.href());
            p.states.push_back(std::move(st));
        }
        out.data = std::move(p);
    } else if (out.kind == DynamicBlockKind::AngularConstraintParameterEntity) {
        BlockAngularConstraintParameterEntity d;
        d.constraint = s.constraint();
        d.center_point = s.vec();
        d.label_point = s.vec();
        d.expression_name = s.str();
        d.expression_description = s.str();
        d.angle = s.real();
        d.orientation_on_both_grips = s.main.bits(1);
        d.value_set = s.value_set();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::AngularConstraintParameter) {
        BlockAngularConstraintParameter d;
        d.constraint = s.constraint();
        d.center_point = s.vec();
        d.end_point = s.vec();
        d.expression_name = s.str();
        d.expression_description = s.str();
        d.angle = s.real();
        d.orientation_on_both_grips = s.main.bits(1);
        d.value_set = s.value_set();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::DiametricConstraintParameter ||
               out.kind == DynamicBlockKind::RadialConstraintParameter) {
        BlockDistanceConstraintParameter d;
        d.constraint = s.constraint();
        d.expression_name = s.str();
        d.expression_description = s.str();
        d.distance = s.real();
        d.value_set = s.value_set();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::AlignedConstraintParameter ||
               out.kind == DynamicBlockKind::LinearConstraintParameter ||
               out.kind == DynamicBlockKind::HorizontalConstraintParameter ||
               out.kind == DynamicBlockKind::VerticalConstraintParameter)
        out.data = s.linear_constraint();
    else if (out.kind == DynamicBlockKind::ParameterDependencyBody) {
        BlockParameterDependencyBody d;
        d.dependency_body_version = s.main.bit_short();
        d.dimension_base_version = s.main.bit_short();
        d.name = s.str();
        d.class_version = s.main.bit_short();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::MoveAction) {
        BlockMoveAction d;
        d.action = s.action();
        d.connections[0] = s.connection();
        d.connections[1] = s.connection();
        d.offsets = s.offsets();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::FlipAction) {
        BlockFlipAction d;
        d.action = s.action();
        for (auto &c : d.connections)
            c = s.connection();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::RotateAction) {
        BlockBasePointAction d;
        d.action = s.action_base();
        d.connections.push_back(s.connection());
        out.data = d;
    } else if (out.kind == DynamicBlockKind::ScaleAction) {
        BlockBasePointAction d;
        d.action = s.action_base();
        d.connections.push_back(s.connection());
        d.connections.push_back(s.connection());
        d.connections.push_back(s.connection());
        if (s.end - s.main.position() >= 8)
            d.dwg_tail_byte = static_cast<std::uint8_t>(s.main.bits(8));
        out.data = d;
    } else if (out.kind == DynamicBlockKind::ArrayAction) {
        BlockArrayAction d;
        d.action = s.action();
        for (auto &c : d.connections)
            c = s.connection();
        d.row_offset = s.real();
        d.column_offset = s.real();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::LookupAction) {
        BlockLookupAction d;
        d.action = s.action();
        d.row_count = s.main.bit_long();
        d.column_count = s.main.bit_long();
        require(d.row_count >= 0 && d.column_count >= 0 &&
                    std::uint64_t(d.row_count) * std::uint64_t(d.column_count) <= 1000000,
                "Invalid lookup table");
        auto expected = std::size_t(d.row_count) * std::size_t(d.column_count);
        d.expressions.reserve(expected);
        for (std::size_t i = 0; i < expected; ++i)
            d.expressions.push_back(s.str());
        d.columns.reserve(std::size_t(d.column_count));
        for (std::int32_t i = 0; i < d.column_count; ++i) {
            BlockLookupColumn c;
            c.node_id = s.main.bit_long();
            c.value_type = s.main.bit_long();
            c.property_type = s.main.bit_long();
            c.lookup_property = s.main.bits(1);
            c.unmatched_name = s.str();
            c.writable = s.main.bits(1);
            c.connection_name = s.str();
            d.columns.push_back(std::move(c));
        }
        d.flag_280 = s.main.bits(1);
        out.data = std::move(d);
    } else if (out.kind == DynamicBlockKind::StretchAction) {
        BlockStretchAction d;
        d.action = s.action();
        d.connections[0] = s.connection();
        d.connections[1] = s.connection();
        auto np = s.count();
        d.points.reserve(np);
        for (std::size_t i = 0; i < np; ++i)
            d.points.push_back(s.raw2());
        auto nh = s.count();
        d.handles.reserve(nh);
        for (std::size_t i = 0; i < nh; ++i)
            d.handles.push_back(s.stretch_handle(true));
        auto nc = s.count();
        d.codes.reserve(nc);
        for (std::size_t i = 0; i < nc; ++i)
            d.codes.push_back(s.stretch_code(true));
        d.offsets = s.offsets();
        out.data = d;
    } else if (out.kind == DynamicBlockKind::PolarStretchAction) {
        BlockPolarStretchAction d;
        d.action = s.action();
        for (auto &c : d.connections)
            c = s.connection();
        auto np = s.count();
        d.points.reserve(np);
        for (std::size_t i = 0; i < np; ++i)
            d.points.push_back(s.raw2());
        auto nh = s.count();
        d.handles.reserve(nh);
        for (std::size_t i = 0; i < nh; ++i)
            d.handles.push_back(s.href());
        auto nb = s.count();
        d.bindings.reserve(nb);
        for (std::size_t i = 0; i < nb; ++i)
            d.bindings.push_back(s.stretch_handle(false));
        auto nc = s.count();
        d.codes.reserve(nc);
        for (std::size_t i = 0; i < nc; ++i)
            d.codes.push_back(s.stretch_code(false));
        d.distance_multiplier = s.real();
        d.angle_offset = s.real();
        auto ne = s.count();
        d.extra.reserve(ne);
        for (std::size_t i = 0; i < ne; ++i)
            d.extra.push_back(s.main.bit_long());
        out.data = d;
    } else if (out.kind == DynamicBlockKind::PropertiesTable) {
        BlockPropertiesTable d;
        d.parameter = s.one_point();
        d.fields.push_back(table_int(90, s.main.bit_long()));
        for (int code : {300, 301})
            d.fields.push_back({code, s.str()});
        auto columns = s.count();
        d.fields.push_back(table_int(91, std::int32_t(columns)));
        for (std::size_t c = 0; c < columns; ++c) {
            d.fields.push_back({340, s.href()});
            d.fields.push_back(table_int(170, s.main.bit_short()));
            d.fields.push_back(table_int(171, s.main.bit_short()));
            d.fields.push_back({300, s.str()});
            d.fields.push_back({301, s.str()});
            d.fields.push_back(table_int(90, s.main.bit_long()));
            d.fields.push_back(table_int(170, s.main.bit_short()));
            d.fields.push_back(table_int(170, s.main.bit_short()));
            for (int code = 290; code <= 294; ++code)
                d.fields.push_back({code, bool(s.main.bits(1))});
            d.fields.push_back({302, s.str()});
            d.fields.push_back({340, s.href()});
        }
        auto rows = s.count();
        d.fields.push_back(table_int(92, std::int32_t(rows)));
        for (std::size_t r = 0; r < rows; ++r) {
            d.fields.push_back(table_int(90, s.main.bit_long()));
            for (std::size_t c = 0; c < columns; ++c) {
                auto value_type = s.main.bit_short();
                d.fields.push_back(table_int(170, value_type));
                BlockPropertiesTableField f;
                switch (value_type) {
                case 40:
                    f = {140, s.real()};
                    break;
                case 1:
                    f = {300, s.str()};
                    break;
                case 90:
                    f = {90, std::int64_t(s.main.bit_long())};
                    break;
                case 70:
                    f = {170, std::int64_t(s.main.bit_short())};
                    break;
                case 91:
                    f = {340, s.href()};
                    break;
                case 290:
                    f = {290, bool(s.main.bits(1))};
                    break;
                default:
                    throw std::invalid_argument("Unsupported properties table value");
                }
                d.fields.push_back(std::move(f));
            }
        }
        d.fields.push_back(table_int(93, s.main.bit_long()));
        for (int code = 290; code <= 292; ++code)
            d.fields.push_back({code, bool(s.main.bits(1))});
        out.data = std::move(d);
    } else
        throw std::invalid_argument("Unsupported dynamic object " + type);
    finish_dynamic(s, body);
    return out;
}
