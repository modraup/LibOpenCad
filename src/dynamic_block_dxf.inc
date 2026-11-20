// SPDX-License-Identifier: MPL-2.0
// Included from dynamic_block.cpp. DXF dynamic-block object codec.
struct DynamicPair {
    int code = 0;
    std::string value;
};
std::int32_t i32_of(std::string_view input) {
    auto value = trimmed(std::string(input));
    if (value.empty())
        return 0;
    std::int64_t result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        return 0;
    if (result < std::numeric_limits<std::int32_t>::min() ||
        result > std::numeric_limits<std::int32_t>::max())
        return 0;
    return static_cast<std::int32_t>(result);
}
std::uint64_t h_of(std::string_view input) {
    auto value = trimmed(std::string(input));
    if (value.empty())
        return 0;
    std::uint64_t result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        return 0;
    return result;
}
double f64_of(std::string_view input) {
    auto value = trimmed(std::string(input));
    if (value.empty())
        return 0;
    std::istringstream in{std::string(value)};
    in.imbue(std::locale::classic());
    double result = 0;
    if (!(in >> result) || !(in >> std::ws).eof() || !std::isfinite(result))
        return 0;
    return result;
}
struct DynamicDxfFields {
    std::map<std::string, std::vector<DynamicPair>> sections;
    std::vector<std::string> values(const std::string &section, int code) const {
        std::vector<std::string> out;
        auto found = sections.find(section);
        if (found == sections.end())
            return out;
        bool keep = detail::is_string_value(code);
        for (const auto &pair : found->second)
            if (pair.code == code)
                out.push_back(keep ? pair.value : trimmed(pair.value));
        return out;
    }
    std::string text(const std::string &section, int code) const {
        auto items = values(section, code);
        return items.empty() ? std::string{} : items.back();
    }
    std::int32_t i32(const std::string &section, int code) const { return i32_of(text(section, code)); }
    std::int16_t i16(const std::string &section, int code) const {
        return static_cast<std::int16_t>(i32(section, code));
    }
    double f64(const std::string &section, int code) const { return f64_of(text(section, code)); }
    bool boolean(const std::string &section, int code) const { return i32(section, code) != 0; }
    std::uint64_t handle_at(const std::string &section, int code) const { return h_of(text(section, code)); }
    Vector3 point(const std::string &section, int code) const {
        return {f64(section, code), f64(section, code + 10), f64(section, code + 20)};
    }
    Vector3 vector_140(const std::string &section) const {
        auto pick = [&](int a, int b) { return values(section, a).empty() ? f64(section, b) : f64(section, a); };
        return {f64(section, 140), pick(141, 150), pick(142, 160)};
    }
};
struct DynamicWriter {
    std::vector<DxfRawField> fields;
    void raw(int code, const std::string &value) { fields.push_back({code, value}); }
    void i16(int code, std::int16_t value) { raw(code, std::to_string(value)); }
    void i32(int code, std::int32_t value) { raw(code, std::to_string(value)); }
    void i64(int code, std::int64_t value) { raw(code, std::to_string(value)); }
    void u8(int code, std::uint8_t value) { raw(code, std::to_string(value)); }
    void boolean(int code, bool value) { raw(code, value ? "1" : "0"); }
    void f64(int code, double value) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out.precision(std::numeric_limits<double>::max_digits10);
        out << value;
        raw(code, out.str());
    }
    void text(int code, const std::string &value) { raw(code, value); }
    void handle(int code, std::uint64_t value) { raw(code, hex(value)); }
    void subclass(const std::string &name) { raw(100, name); }
    void point3(int code, Vector3 value) {
        f64(code, value.x);
        f64(code + 10, value.y);
        f64(code + 20, value.z);
    }
    void point2(int code, Vector2 value) {
        f64(code, value.x);
        f64(code + 10, value.y);
    }
    void eval_value(std::int16_t code, const BlockEvalValue &value, int real_code) {
        std::visit(
            [&](const auto &item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, double>)
                    f64(real_code, item);
                else if constexpr (std::is_same_v<T, Vector2>) {
                    auto base = (code == 10 || code == 11) ? code : 10;
                    f64(base, item.x);
                    f64(base + 10, item.y);
                } else if constexpr (std::is_same_v<T, std::string>)
                    text(1, item);
                else if constexpr (std::is_same_v<T, std::int32_t>)
                    i32(90, item);
                else if constexpr (std::is_same_v<T, std::uint64_t>)
                    handle(91, item);
                else if constexpr (std::is_same_v<T, std::int16_t>)
                    i16(70, item);
            },
            value);
    }
    void eval(const BlockEvalExpression &value) {
        subclass("AcDbEvalExpr");
        i32(90, value.node_id);
        i32(98, value.major);
        i32(99, value.minor);
        if (value.value_code == -9999)
            return;
        text(1, "");
        i16(70, value.value_code);
        eval_value(value.value_code, value.value, 140);
    }
    void element(const BlockElement &value) {
        eval(value.eval);
        subclass("AcDbBlockElement");
        text(300, value.name);
        i32(98, value.major);
        i32(99, value.minor);
        i32(1071, value.eed_1071);
    }
    void parameter(const BlockParameter &value) {
        element(value.element);
        subclass("AcDbBlockParameter");
        boolean(280, value.show_properties);
        boolean(281, value.chain_actions);
    }
    void property(const BlockParameterProperty &value, int count, int code, int name) {
        i32(count, static_cast<std::int32_t>(value.connections.size()));
        for (const auto &item : value.connections) {
            i32(code, item.code);
            text(name, item.name);
        }
    }
    void one_point(const BlockOnePointParameter &value) {
        parameter(value.parameter);
        subclass("AcDbBlock1PtParameter");
        point3(1010, value.definition_point);
        i32(93, value.property_count);
        for (std::size_t i = 0; i < value.properties.size(); ++i)
            property(value.properties[i], 170 + static_cast<int>(i), 91 + static_cast<int>(i),
                     301 + static_cast<int>(i));
    }
    void two_point(const BlockTwoPointParameter &value) {
        parameter(value.parameter);
        subclass("AcDbBlock2PtParameter");
        point3(1010, value.definition_base_point);
        point3(1011, value.definition_end_point);
        i32(170, 4);
        for (auto state : value.property_states)
            i32(91, state);
        for (std::size_t i = 0; i < value.properties.size(); ++i)
            property(value.properties[i], 171 + static_cast<int>(i), 92 + static_cast<int>(i),
                     301 + static_cast<int>(i));
        i16(177, value.parameter_base_location);
    }
    void grip(const BlockGrip &value) {
        element(value.element);
        subclass("AcDbBlockGrip");
        i32(91, value.flags_91);
        i32(92, value.flags_92);
        point3(1010, value.location);
        boolean(280, value.insert_cycling);
        i32(93, value.insert_cycling_weight);
    }
    void action(const BlockAction &value) {
        element(value.element);
        subclass("AcDbBlockAction");
        i32(70, static_cast<std::int32_t>(value.action_ids.size()));
        for (auto id : value.action_ids)
            i32(91, id);
        i32(71, static_cast<std::int32_t>(value.dependencies.size()));
        for (auto h : value.dependencies)
            handle(330, h);
        point3(1010, value.display_location);
    }
    template <class C>
    void sequential(const C &items, int code, int name) {
        for (std::size_t i = 0; i < items.size(); ++i)
            i32(code + static_cast<int>(i), items[i].code);
        for (std::size_t i = 0; i < items.size(); ++i)
            text(name + static_cast<int>(i), items[i].name);
    }
    void action_base(const BlockActionWithBasePoint &value) {
        action(value.action);
        subclass("AcDbBlockActionWithBasePt");
        sequential(value.connections, 92, 301);
        point3(1011, value.offset);
        boolean(280, value.dependent);
        point3(1012, value.base_point);
    }
    void constraint(const BlockConstraintParameter &value) {
        two_point(value.parameter);
        subclass("AcDbBlockConstraintParameter");
        handle(330, value.dependency);
    }
    void value_set(const BlockParameterValueSet &value, int flags, int real, int count, int description) {
        text(description, value.description);
        i32(flags, value.flags);
        f64(real, value.minimum);
        f64(real + 1, value.maximum);
        f64(real + 2, value.increment);
        i16(count, static_cast<std::int16_t>(value.values.size()));
        for (auto item : value.values)
            f64(real + 3, item);
    }
    void linear_constraint(const BlockLinearConstraintParameter &value) {
        constraint(value.constraint);
        subclass("AcDbBlockLinearConstraintParameter");
        text(305, value.expression_name);
        text(306, value.expression_description);
        f64(140, value.value);
        value_set(value.value_set, 96, 128, 175, 307);
    }
    void offsets(const BlockActionOffsets &value) {
        f64(140, value.distance_multiplier);
        f64(141, value.angle_offset);
        u8(280, value.flags);
    }
};
std::vector<BlockConnection> dxf_connections(const DynamicDxfFields &fields, const std::string &section,
                                             int code, int name) {
    auto codes = fields.values(section, code);
    auto names = fields.values(section, name);
    auto count = std::max(codes.size(), names.size());
    std::vector<BlockConnection> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (i < codes.size())
            out[i].code = i32_of(codes[i]);
        if (i < names.size())
            out[i].name = names[i];
    }
    return out;
}
std::vector<BlockConnection> dxf_sequential(const DynamicDxfFields &fields, const std::string &section, int code,
                                            int name, std::size_t count) {
    std::vector<BlockConnection> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i].code = fields.i32(section, code + static_cast<int>(i));
        out[i].name = fields.text(section, name + static_cast<int>(i));
    }
    return out;
}
BlockEvalValue dxf_eval_value(const DynamicDxfFields &fields, const std::string &section, std::int16_t code) {
    auto shorts = fields.values(section, 70);
    switch (code) {
    case 40:
        return fields.values(section, 140).empty() ? fields.f64(section, 40) : fields.f64(section, 140);
    case 10:
    case 11:
        return Vector2{fields.f64(section, code), fields.f64(section, code + 10)};
    case 1:
        return fields.text(section, 1);
    case 90: {
        auto values = fields.values(section, 90);
        return values.empty() ? 0 : i32_of(values.back());
    }
    case 91:
        return fields.handle_at(section, 91);
    case 70:
        return static_cast<std::int16_t>(shorts.size() > 1 ? i32_of(shorts[1]) : 0);
    default:
        return std::monostate{};
    }
}
BlockEvalExpression dxf_eval(const DynamicDxfFields &fields) {
    const std::string section = "AcDbEvalExpr";
    auto shorts = fields.values(section, 70);
    BlockEvalExpression value;
    value.parent_id = -1;
    value.value_code = shorts.empty() ? std::int16_t(-9999) : static_cast<std::int16_t>(i32_of(shorts.front()));
    value.value = dxf_eval_value(fields, section, value.value_code);
    value.major = fields.i32(section, 98);
    value.minor = fields.i32(section, 99);
    auto nineties = fields.values(section, 90);
    value.node_id = nineties.empty() ? 0 : i32_of(nineties.front());
    return value;
}
BlockElement dxf_element(const DynamicDxfFields &fields) {
    const std::string section = "AcDbBlockElement";
    return {dxf_eval(fields), fields.text(section, 300), fields.i32(section, 98), fields.i32(section, 99),
            fields.i32(section, 1071)};
}
BlockParameter dxf_parameter(const DynamicDxfFields &fields) {
    return {dxf_element(fields), fields.boolean("AcDbBlockParameter", 280),
            fields.boolean("AcDbBlockParameter", 281)};
}
BlockOnePointParameter dxf_one_point(const DynamicDxfFields &fields) {
    const std::string section = "AcDbBlock1PtParameter";
    BlockOnePointParameter value;
    value.parameter = dxf_parameter(fields);
    value.definition_point = fields.point(section, 1010);
    value.properties[0].connections = dxf_connections(fields, section, 91, 301);
    value.properties[1].connections = dxf_connections(fields, section, 92, 302);
    value.property_count = fields.i32(section, 93);
    return value;
}
BlockTwoPointParameter dxf_two_point(const DynamicDxfFields &fields) {
    const std::string section = "AcDbBlock2PtParameter";
    BlockTwoPointParameter value;
    value.parameter = dxf_parameter(fields);
    value.definition_base_point = fields.point(section, 1010);
    value.definition_end_point = fields.point(section, 1011);
    auto states = fields.values(section, 91);
    for (std::size_t i = 0; i < value.property_states.size() && i < states.size(); ++i)
        value.property_states[i] = i32_of(states[i]);
    value.properties[0].connections = dxf_connections(fields, section, 92, 301);
    value.properties[1].connections = dxf_connections(fields, section, 93, 302);
    value.properties[2].connections = dxf_connections(fields, section, 94, 303);
    value.properties[3].connections = dxf_connections(fields, section, 95, 304);
    value.parameter_base_location = fields.i16(section, 177);
    return value;
}
BlockGrip dxf_grip(const DynamicDxfFields &fields) {
    const std::string section = "AcDbBlockGrip";
    BlockGrip value;
    value.element = dxf_element(fields);
    value.flags_91 = fields.i32(section, 91);
    value.flags_92 = fields.i32(section, 92);
    value.location = fields.point(section, 1010);
    value.insert_cycling = fields.boolean(section, 280);
    value.insert_cycling_weight = fields.i32(section, 93);
    return value;
}
BlockAction dxf_action(const DynamicDxfFields &fields) {
    const std::string section = "AcDbBlockAction";
    BlockAction value;
    value.element = dxf_element(fields);
    value.display_location = fields.point(section, 1010);
    for (auto &item : fields.values(section, 330))
        value.dependencies.push_back(h_of(item));
    for (auto &item : fields.values(section, 91))
        value.action_ids.push_back(i32_of(item));
    return value;
}
BlockActionWithBasePoint dxf_action_base(const DynamicDxfFields &fields) {
    const std::string section = "AcDbBlockActionWithBasePt";
    BlockActionWithBasePoint value;
    value.action = dxf_action(fields);
    value.offset = fields.point(section, 1011);
    value.connections = dxf_sequential(fields, section, 92, 301, 2);
    value.dependent = fields.boolean(section, 280);
    value.base_point = fields.point(section, 1012);
    return value;
}
BlockParameterValueSet dxf_value_set(const DynamicDxfFields &fields, const std::string &section, int flags,
                                     int real, int description) {
    BlockParameterValueSet value;
    value.description = fields.text(section, description);
    value.flags = fields.i32(section, flags);
    value.minimum = fields.f64(section, real);
    value.maximum = fields.f64(section, real + 1);
    value.increment = fields.f64(section, real + 2);
    for (auto &item : fields.values(section, real + 3))
        value.values.push_back(f64_of(item));
    return value;
}
BlockConstraintParameter dxf_constraint(const DynamicDxfFields &fields) {
    return {dxf_two_point(fields), fields.handle_at("AcDbBlockConstraintParameter", 330)};
}
BlockLinearConstraintParameter dxf_linear_constraint(const DynamicDxfFields &fields) {
    const std::string section = "AcDbBlockLinearConstraintParameter";
    return {dxf_constraint(fields), fields.text(section, 305), fields.text(section, 306), fields.f64(section, 140),
            dxf_value_set(fields, section, 96, 128, 307)};
}
template <std::size_t N>
std::array<BlockConnection, N> take_connections(const std::vector<BlockConnection> &items) {
    std::array<BlockConnection, N> out{};
    for (std::size_t i = 0; i < N && i < items.size(); ++i)
        out[i] = items[i];
    return out;
}
BlockEvalValue dxf_user_value(const DynamicDxfFields &fields, const std::string &section, std::int16_t code) {
    switch (code) {
    case 40:
        return fields.f64(section, 40);
    case 1:
        return fields.text(section, 1);
    case 90:
        return fields.i32(section, 90);
    case 91:
        return fields.handle_at(section, 91);
    case 70:
        return fields.i16(section, 70);
    default:
        return std::monostate{};
    }
}
std::string hex_bytes(const std::vector<std::uint8_t> &bytes) {
    std::string out;
    constexpr char digits[] = "0123456789ABCDEF";
    for (auto b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}
std::vector<std::uint8_t> parse_hex_bytes(const std::string &text) {
    std::vector<std::uint8_t> out;
    if (text.size() % 2)
        return out;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        unsigned value = 0;
        auto parsed = std::from_chars(text.data() + i, text.data() + i + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + i + 2)
            break;
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}
std::pair<DynamicBlockKind, DynamicBlockPayload> parse_dynamic_payload(const std::string &name,
                                                                       const DynamicDxfFields &fields) {
    auto n = ascii_upper(name);
    auto kind = dynamic_block_kind_from_name(n);
    if (kind == DynamicBlockKind::EmptyEntity)
        return {kind, std::monostate{}};
    if (n == "BLOCKANGULARCONSTRAINTPARAMETERENTITY") {
        BlockAngularConstraintParameterEntity value;
        value.constraint = dxf_constraint(fields);
        const std::string section = "AcDbBlockAngularConstraintParameterEntity";
        value.center_point = fields.point(section, 1011);
        value.label_point = fields.point(section, 1012);
        value.expression_name = fields.text(section, 305);
        value.expression_description = fields.text(section, 306);
        value.angle = fields.f64(section, 140);
        value.orientation_on_both_grips = fields.boolean(section, 280);
        value.value_set = dxf_value_set(fields, section, 96, 128, 307);
        return {kind, value};
    }
    if (n == "ACDB_BLOCKREPRESENTATION_DATA" || n == "BLOCKREPRESENTATION")
        return {kind, BlockRepresentationData{fields.i16("AcDbBlockRepresentationData", 70),
                                              fields.handle_at("AcDbBlockRepresentationData", 340)}};
    if (n == "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION" || n == "DYNAMICBLOCKPURGEPREVENTER")
        return {kind, BlockRepresentationData{fields.i16("AcDbDynamicBlockPurgePreventer", 70),
                                              fields.handle_at("AcDbDynamicBlockPurgePreventer", 340)}};
    if (n == "ACDB_DYNAMICBLOCKPROXYNODE" || n == "DYNAMICBLOCKPROXYNODE")
        return {kind, dxf_eval(fields)};
    if (n == "BLOCKGRIPLOCATIONCOMPONENT") {
        BlockGripExpression value;
        value.eval = dxf_eval(fields);
        value.grip_type = fields.i32("AcDbBlockGripExpr", 91);
        value.expression = fields.text("AcDbBlockGripExpr", 300);
        return {kind, value};
    }
    if (n == "BLOCKALIGNMENTGRIP" || n == "BLOCKLINEARGRIP") {
        BlockOrientedGrip value;
        value.grip = dxf_grip(fields);
        value.orientation = fields.vector_140(n == "BLOCKALIGNMENTGRIP" ? "AcDbBlockAlignmentGrip"
                                                                        : "AcDbBlockLinearGrip");
        return {kind, value};
    }
    if (n == "BLOCKFLIPGRIP") {
        BlockFlipGrip value;
        value.grip = dxf_grip(fields);
        value.combined_state = fields.i32("AcDbBlockFlipGrip", 93);
        value.orientation = fields.vector_140("AcDbBlockFlipGrip");
        return {kind, value};
    }
    if (n == "BLOCKLOOKUPGRIP" || n == "BLOCKPOLARGRIP" || n == "BLOCKROTATIONGRIP" ||
        n == "BLOCKVISIBILITYGRIP" || n == "BLOCKXYGRIP" || n == "BLOCKPROPERTIESTABLEGRIP")
        return {kind, dxf_grip(fields)};
    if (n == "BLOCKALIGNMENTPARAMETER") {
        BlockAlignmentParameter value;
        value.parameter = dxf_two_point(fields);
        value.align_perpendicular = fields.boolean("AcDbBlockAlignmentParameter", 280);
        return {kind, value};
    }
    if (n == "BLOCKBASEPOINTPARAMETER") {
        BlockBasePointParameter value;
        value.parameter = dxf_one_point(fields);
        value.point = fields.point("AcDbBlockBasepointParameter", 1011);
        value.base_point = fields.point("AcDbBlockBasepointParameter", 1012);
        return {kind, value};
    }
    if (n == "BLOCKFLIPPARAMETER") {
        const std::string section = "AcDbBlockFlipParameter";
        BlockFlipParameter value;
        value.parameter = dxf_two_point(fields);
        value.flip_label = fields.text(section, 305);
        value.flip_label_description = fields.text(section, 306);
        value.base_state_label = fields.text(section, 307);
        value.flipped_state_label = fields.text(section, 308);
        value.definition_label_point = fields.point(section, 1012);
        value.tooltip = fields.text(section, 309);
        value.flags_96 = fields.i32(section, 96);
        return {kind, value};
    }
    if (n == "BLOCKLINEARPARAMETER") {
        const std::string section = "AcDbBlockLinearParameter";
        BlockLinearParameter value;
        value.parameter = dxf_two_point(fields);
        value.distance_name = fields.text(section, 305);
        value.distance_description = fields.text(section, 306);
        value.distance = fields.f64(section, 140);
        value.value_set = dxf_value_set(fields, section, 96, 141, 307);
        return {kind, value};
    }
    if (n == "BLOCKLOOKUPPARAMETER") {
        BlockLookupParameter value;
        value.parameter = dxf_one_point(fields);
        value.index = fields.i32("AcDbBlockLookupParameter", 94);
        value.lookup_name = fields.text("AcDbBlockLookupParameter", 303);
        value.lookup_description = fields.text("AcDbBlockLookupParameter", 304);
        return {kind, value};
    }
    if (n == "BLOCKPOINTPARAMETER") {
        BlockPointParameter value;
        value.parameter = dxf_one_point(fields);
        value.position_name = fields.text("AcDbBlockPointParameter", 303);
        value.position_description = fields.text("AcDbBlockPointParameter", 304);
        value.definition_label_point = fields.point("AcDbBlockPointParameter", 1011);
        return {kind, value};
    }
    if (n == "BLOCKPOLARPARAMETER") {
        const std::string section = "AcDbBlockPolarParameter";
        BlockPolarParameter value;
        value.parameter = dxf_two_point(fields);
        value.distance_name = fields.text(section, 305);
        value.distance_description = fields.text(section, 306);
        value.angle_name = fields.text(section, 307);
        value.angle_description = fields.text(section, 308);
        value.offset = fields.f64(section, 140);
        value.distance_value_set = dxf_value_set(fields, section, 96, 141, 309);
        value.angle_value_set = dxf_value_set(fields, section, 97, 145, 410);
        return {kind, value};
    }
    if (n == "BLOCKROTATIONPARAMETER") {
        const std::string section = "AcDbBlockRotationParameter";
        BlockRotationParameter value;
        value.parameter = dxf_two_point(fields);
        value.definition_base_angle_point = fields.point(section, 1011);
        value.angle_name = fields.text(section, 305);
        value.angle_description = fields.text(section, 306);
        value.angle = fields.f64(section, 140);
        value.value_set = dxf_value_set(fields, section, 96, 141, 307);
        return {kind, value};
    }
    if (n == "BLOCKXYPARAMETER") {
        const std::string section = "AcDbBlockXYParameter";
        BlockXYParameter value;
        value.parameter = dxf_two_point(fields);
        value.y_label = fields.text(section, 305);
        value.x_label = fields.text(section, 306);
        value.y_label_description = fields.text(section, 307);
        value.x_label_description = fields.text(section, 308);
        value.x_value = fields.f64(section, 141);
        value.y_value = fields.f64(section, 140);
        value.x_value_set = dxf_value_set(fields, section, 96, 142, 410);
        value.y_value_set = dxf_value_set(fields, section, 97, 146, 309);
        return {kind, value};
    }
    if (n == "BLOCKUSERPARAMETER") {
        const std::string section = "AcDbBlockUserParameter";
        BlockUserParameter value;
        value.parameter = dxf_one_point(fields);
        value.flags = fields.i16(section, 90);
        value.associated_variable = fields.handle_at(section, 330);
        value.expression = fields.text(section, 301);
        value.value_code = fields.i16(section, 70);
        value.value = dxf_user_value(fields, section, value.value_code);
        value.value_type = fields.i16(section, 170);
        return {kind, value};
    }
    if (n == "BLOCKVISIBILITYPARAMETER") {
        auto one = dxf_one_point(fields);
        const std::string section = "AcDbBlockVisibilityParameter";
        BlockVisibilityParameter value;
        value.eval_parent_id = one.parameter.element.eval.parent_id;
        value.eval_major = one.parameter.element.eval.major;
        value.eval_minor = one.parameter.element.eval.minor;
        value.eval_value_code = one.parameter.element.eval.value_code;
        value.eval_value = one.parameter.element.eval.value;
        value.eval_node_id = one.parameter.element.eval.node_id;
        value.element_name = one.parameter.element.name;
        value.element_major = one.parameter.element.major;
        value.element_minor = one.parameter.element.minor;
        value.element_eed_1071 = one.parameter.element.eed_1071;
        value.show_properties = one.parameter.show_properties;
        value.chain_actions = one.parameter.chain_actions;
        value.definition_point = one.definition_point;
        value.property_info = one.properties;
        value.property_count = one.property_count;
        value.name = fields.text(section, 301);
        value.description = fields.text(section, 302);
        value.is_initialized = fields.boolean(section, 281);
        value.unknown_bool = fields.boolean(section, 91);
        for (auto &item : fields.values(section, 331))
            value.all_blocks.push_back(h_of(item));
        auto names = fields.values(section, 303);
        auto blocks = fields.values(section, 332);
        auto params = fields.values(section, 333);
        auto block_counts = fields.values(section, 94);
        auto param_counts = fields.values(section, 95);
        std::size_t block_offset = 0, param_offset = 0;
        for (std::size_t i = 0; i < names.size(); ++i) {
            BlockVisibilityState state;
            state.name = names[i];
            auto nb = i < block_counts.size() ? static_cast<std::size_t>(std::max(0, i32_of(block_counts[i]))) : 0;
            auto np = i < param_counts.size() ? static_cast<std::size_t>(std::max(0, i32_of(param_counts[i]))) : 0;
            for (std::size_t j = 0; j < nb && block_offset < blocks.size(); ++j)
                state.visible_blocks.push_back(h_of(blocks[block_offset++]));
            for (std::size_t j = 0; j < np && param_offset < params.size(); ++j)
                state.visible_params.push_back(h_of(params[param_offset++]));
            value.states.push_back(std::move(state));
        }
        return {kind, std::move(value)};
    }
    if (n == "BLOCKANGULARCONSTRAINTPARAMETER") {
        const std::string section = "AcDbBlockAngularConstraintParameter";
        BlockAngularConstraintParameter value;
        value.constraint = dxf_constraint(fields);
        value.center_point = fields.point(section, 1011);
        value.end_point = fields.point(section, 1012);
        value.expression_name = fields.text(section, 305);
        value.expression_description = fields.text(section, 306);
        value.angle = fields.f64(section, 140);
        value.orientation_on_both_grips = fields.boolean(section, 280);
        value.value_set = dxf_value_set(fields, section, 96, 128, 307);
        return {kind, value};
    }
    if (n == "BLOCKDIAMETRICCONSTRAINTPARAMETER" || n == "BLOCKRADIALCONSTRAINTPARAMETER") {
        auto section = n == "BLOCKDIAMETRICCONSTRAINTPARAMETER" ? "AcDbBlockDiametricConstraintParameter"
                                                                : "AcDbBlockRadialConstraintParameter";
        BlockDistanceConstraintParameter value;
        value.constraint = dxf_constraint(fields);
        value.expression_name = fields.text(section, 305);
        value.expression_description = fields.text(section, 306);
        value.distance = fields.f64(section, 140);
        value.value_set = dxf_value_set(fields, section, 96, 128, 307);
        return {kind, value};
    }
    if (n == "BLOCKALIGNEDCONSTRAINTPARAMETER" || n == "BLOCKLINEARCONSTRAINTPARAMETER" ||
        n == "BLOCKHORIZONTALCONSTRAINTPARAMETER" || n == "BLOCKVERTICALCONSTRAINTPARAMETER")
        return {kind, dxf_linear_constraint(fields)};
    if (n == "ACDBBLOCKPARAMDEPENDENCYBODY" || n == "BLOCKPARAMDEPENDENCYBODY") {
        BlockParameterDependencyBody value;
        value.dependency_body_version = fields.i16("AcDbAssocDependencyBody", 90);
        value.dimension_base_version = fields.i16("AcDbImpAssocDimDependencyBodyBase", 90);
        value.name = fields.text("AcDbImpAssocDimDependencyBodyBase", 1);
        value.class_version = fields.i16("AcDbBlockParameterDependencyBody", 90);
        return {kind, value};
    }
    if (n == "BLOCKMOVEACTION") {
        const std::string section = "AcDbBlockMoveAction";
        BlockMoveAction value;
        value.action = dxf_action(fields);
        value.connections = take_connections<2>(dxf_sequential(fields, section, 92, 301, 2));
        value.offsets.distance_multiplier = fields.f64(section, 140);
        value.offsets.angle_offset = fields.f64(section, 141);
        value.offsets.flags = static_cast<std::uint8_t>(fields.i32(section, 280));
        return {kind, value};
    }
    if (n == "BLOCKFLIPACTION") {
        BlockFlipAction value;
        value.action = dxf_action(fields);
        value.connections = take_connections<4>(dxf_sequential(fields, "AcDbBlockFlipAction", 92, 301, 4));
        return {kind, value};
    }
    if (n == "BLOCKROTATEACTION" || n == "BLOCKSCALEACTION") {
        auto section = n == "BLOCKROTATEACTION" ? "AcDbBlockRotationAction" : "AcDbBlockScaleAction";
        BlockBasePointAction value;
        value.action = dxf_action_base(fields);
        value.connections = dxf_sequential(fields, section, 94, 303, n == "BLOCKROTATEACTION" ? 1 : 3);
        return {kind, value};
    }
    if (n == "BLOCKARRAYACTION") {
        const std::string section = "AcDbBlockArrayAction";
        BlockArrayAction value;
        value.action = dxf_action(fields);
        value.connections = take_connections<4>(dxf_sequential(fields, section, 92, 301, 4));
        value.row_offset = fields.f64(section, 140);
        value.column_offset = fields.f64(section, 141);
        return {kind, value};
    }
    if (n == "BLOCKLOOKUPACTION") {
        const std::string section = "AcDbBlockLookupAction";
        BlockLookupAction value;
        value.action = dxf_action(fields);
        value.row_count = fields.i32(section, 92);
        value.column_count = fields.i32(section, 93);
        auto count = static_cast<std::size_t>(std::max(0, value.row_count) * std::max(0, value.column_count));
        auto expressions = fields.values(section, 302);
        value.expressions.assign(expressions.begin(), expressions.begin() + std::min(count, expressions.size()));
        auto ids = fields.values(section, 94), types = fields.values(section, 95), properties = fields.values(section, 96);
        auto names = fields.values(section, 304), unmatched = fields.values(section, 305);
        auto lookup = fields.values(section, 282), writable = fields.values(section, 281);
        auto columns = static_cast<std::size_t>(std::max(0, value.column_count));
        for (std::size_t i = 0; i < columns; ++i) {
            BlockLookupColumn column;
            if (i < ids.size())
                column.node_id = i32_of(ids[i]);
            if (i < types.size())
                column.value_type = i32_of(types[i]);
            if (i < properties.size())
                column.property_type = i32_of(properties[i]);
            if (i < unmatched.size())
                column.unmatched_name = unmatched[i];
            if (i < names.size())
                column.connection_name = names[i];
            column.lookup_property = i < lookup.size() && i32_of(lookup[i]) != 0;
            column.writable = i < writable.size() && i32_of(writable[i]) != 0;
            value.columns.push_back(std::move(column));
        }
        value.flag_280 = fields.boolean(section, 280);
        return {kind, value};
    }
    if (n == "BLOCKPROPERTIESTABLE") {
        BlockPropertiesTable value;
        value.parameter = dxf_one_point(fields);
        auto found = fields.sections.find("AcDbBlockPropertiesTable");
        if (found != fields.sections.end())
            for (const auto &pair : found->second) {
                if (pair.code >= 1000 && pair.code <= 1071)
                    continue;
                BlockPropertiesTableField field;
                field.code = pair.code;
                if (pair.code >= 290 && pair.code <= 299)
                    field.value = i32_of(pair.value) != 0;
                else if ((pair.code >= 60 && pair.code <= 79) || (pair.code >= 90 && pair.code <= 99) ||
                         (pair.code >= 160 && pair.code <= 179) || (pair.code >= 270 && pair.code <= 289) ||
                         (pair.code >= 370 && pair.code <= 389) || (pair.code >= 400 && pair.code <= 409) ||
                         (pair.code >= 420 && pair.code <= 459) || pair.code == 1071)
                    field.value = static_cast<std::int64_t>(i32_of(pair.value));
                else if ((pair.code >= 10 && pair.code <= 59) || (pair.code >= 110 && pair.code <= 149) ||
                         (pair.code >= 210 && pair.code <= 239) || (pair.code >= 460 && pair.code <= 469) ||
                         (pair.code >= 1010 && pair.code <= 1059))
                    field.value = f64_of(pair.value);
                else if ((pair.code >= 320 && pair.code <= 369) || (pair.code >= 390 && pair.code <= 399) ||
                         pair.code == 5 || pair.code == 105)
                    field.value = h_of(pair.value);
                else if ((pair.code >= 310 && pair.code <= 319) || pair.code == 1004)
                    field.value = parse_hex_bytes(pair.value);
                else
                    field.value = pair.value;
                value.fields.push_back(std::move(field));
            }
        return {kind, value};
    }
    if (n == "BLOCKSTRETCHACTION") {
        const std::string section = "AcDbBlockStretchAction";
        BlockStretchAction value;
        value.action = dxf_action(fields);
        value.connections = take_connections<2>(dxf_sequential(fields, section, 92, 301, 2));
        auto xs = fields.values(section, 1011), ys = fields.values(section, 1021);
        for (std::size_t i = 0; i < xs.size() && i < ys.size(); ++i)
            value.points.push_back({f64_of(xs[i]), f64_of(ys[i])});
        auto handles = fields.values(section, 331);
        auto handle_counts = fields.values(section, 74);
        auto code_values = fields.values(section, 95);
        auto code_counts = fields.values(section, 76);
        std::vector<std::int32_t> indexes;
        for (auto &item : fields.values(section, 94))
            indexes.push_back(i32_of(item));
        std::size_t offset = 0;
        auto take = [&](std::size_t count) {
            auto end = std::min(offset + count, indexes.size());
            std::vector<std::int32_t> out(indexes.begin() + static_cast<std::ptrdiff_t>(offset),
                                          indexes.begin() + static_cast<std::ptrdiff_t>(end));
            offset = end;
            return out;
        };
        for (std::size_t i = 0; i < handles.size(); ++i) {
            auto count = i < handle_counts.size() ? static_cast<std::size_t>(std::max(0, i32_of(handle_counts[i]))) : 0;
            value.handles.push_back({h_of(handles[i]), take(count)});
        }
        for (std::size_t i = 0; i < code_values.size(); ++i) {
            auto count = i < code_counts.size() ? static_cast<std::size_t>(std::max(0, i32_of(code_counts[i]))) : 0;
            value.codes.push_back({i32_of(code_values[i]), take(count)});
        }
        value.offsets.distance_multiplier = fields.f64(section, 140);
        value.offsets.angle_offset = fields.f64(section, 141);
        value.offsets.flags = static_cast<std::uint8_t>(fields.i32(section, 280));
        return {kind, value};
    }
    if (n == "BLOCKPOLARSTRETCHACTION") {
        const std::string section = "AcDbBlockPolarStretchAction";
        if (fields.i32(section, 77) != 0)
            throw std::invalid_argument("DXF polar stretch extension data");
        BlockPolarStretchAction value;
        value.action = dxf_action(fields);
        value.connections = take_connections<6>(dxf_sequential(fields, section, 92, 301, 6));
        auto xs = fields.values(section, 1011), ys = fields.values(section, 1021);
        for (std::size_t i = 0; i < xs.size() && i < ys.size(); ++i)
            value.points.push_back({f64_of(xs[i]), f64_of(ys[i])});
        for (auto &item : fields.values(section, 331))
            value.handles.push_back(h_of(item));
        std::vector<std::int32_t> indexes;
        for (auto &item : fields.values(section, 76))
            indexes.push_back(i32_of(item));
        std::size_t offset = 0;
        auto take = [&](std::size_t count) {
            auto end = std::min(offset + count, indexes.size());
            std::vector<std::int32_t> out(indexes.begin() + static_cast<std::ptrdiff_t>(offset),
                                          indexes.begin() + static_cast<std::ptrdiff_t>(end));
            offset = end;
            return out;
        };
        auto bindings = fields.values(section, 332);
        auto binding_counts = fields.values(section, 75);
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            auto count = i < binding_counts.size() ? static_cast<std::size_t>(std::max(0, i32_of(binding_counts[i]))) : 0;
            value.bindings.push_back({h_of(bindings[i]), take(count)});
        }
        auto codes = fields.values(section, 98);
        auto code_counts = fields.values(section, 79);
        for (std::size_t i = 0; i < codes.size(); ++i) {
            auto count = i < code_counts.size() ? static_cast<std::size_t>(std::max(0, i32_of(code_counts[i]))) : 0;
            value.codes.push_back({i32_of(codes[i]), take(count)});
        }
        value.distance_multiplier = fields.f64(section, 141);
        value.angle_offset = fields.f64(section, 140);
        return {kind, value};
    }
    return {DynamicBlockKind::Unknown, std::monostate{}};
}
DynamicBlockObject parse_dynamic_object(const DxfRecord &record, std::uint64_t handle, std::uint64_t owner,
                                        const std::vector<std::uint64_t> &reactors,
                                        std::optional<std::uint64_t> xdictionary, const DynamicDxfFields &fields) {
    DynamicBlockObject object;
    object.handle = handle;
    object.owner = owner;
    object.reactors = reactors;
    object.xdictionary_handle = xdictionary;
    object.dxf_name = record.type();
    object.cpp_class_name = dynamic_block_cpp_class_name(object.dxf_name).value_or("AcDbObject");
    auto parsed = parse_dynamic_payload(object.dxf_name, fields);
    object.kind = parsed.first;
    object.data = std::move(parsed.second);
    if (object.kind == DynamicBlockKind::VisibilityParameter)
        if (auto *value = std::get_if<BlockVisibilityParameter>(&object.data)) {
            value->handle = object.handle;
            value->owner = object.owner;
        }
    return object;
}
std::vector<DxfRawField> encode_dynamic_fields(const DynamicBlockObject &object) {
    DynamicWriter w;
    const bool entity = object.is_entity || is_dynamic_block_entity_name(object.dxf_name);
    w.text(0, object.dxf_name);
    w.handle(5, object.handle);
    if (entity) {
        w.handle(330, object.owner);
        if (object.xdictionary_handle) {
            w.text(102, "{ACAD_XDICTIONARY");
            w.handle(360, *object.xdictionary_handle);
            w.text(102, "}");
        }
        if (!object.reactors.empty()) {
            w.text(102, "{ACAD_REACTORS");
            for (auto reactor : object.reactors)
                w.handle(330, reactor);
            w.text(102, "}");
        }
        w.subclass("AcDbEntity");
        if (object.paper_space)
            w.i16(67, 1);
        w.text(8, object.layer.empty() ? "0" : object.layer);
    } else {
        if (!object.reactors.empty()) {
            w.text(102, "{ACAD_REACTORS");
            for (auto reactor : object.reactors)
                w.handle(330, reactor);
            w.text(102, "}");
        }
        if (object.xdictionary_handle) {
            w.text(102, "{ACAD_XDICTIONARY");
            w.handle(360, *object.xdictionary_handle);
            w.text(102, "}");
        }
        w.handle(330, object.owner);
    }
    auto kind = object.kind;
    auto require = [&](auto *value) {
        if (!value)
            throw std::invalid_argument("Dynamic payload mismatch");
        return value;
    };
    if (kind == DynamicBlockKind::Representation) {
        auto *value = require(std::get_if<BlockRepresentationData>(&object.data));
        w.subclass(object.cpp_class_name);
        w.i16(70, value->flags);
        w.handle(340, value->block);
    } else if (kind == DynamicBlockKind::ProxyNode) {
        w.eval(*require(std::get_if<BlockEvalExpression>(&object.data)));
        w.subclass("AcDbDynamicBlockProxyNode");
    } else if (kind == DynamicBlockKind::GripLocationComponent) {
        auto *value = require(std::get_if<BlockGripExpression>(&object.data));
        w.eval(value->eval);
        w.subclass("AcDbBlockGripExpr");
        w.i32(91, value->grip_type);
        w.text(300, value->expression);
    } else if (kind == DynamicBlockKind::AlignmentGrip || kind == DynamicBlockKind::LinearGrip) {
        auto *value = require(std::get_if<BlockOrientedGrip>(&object.data));
        w.grip(value->grip);
        w.subclass(kind == DynamicBlockKind::AlignmentGrip ? "AcDbBlockAlignmentGrip" : "AcDbBlockLinearGrip");
        w.f64(140, value->orientation.x);
        w.f64(141, value->orientation.y);
        w.f64(142, value->orientation.z);
    } else if (kind == DynamicBlockKind::FlipGrip) {
        auto *value = require(std::get_if<BlockFlipGrip>(&object.data));
        w.grip(value->grip);
        w.subclass("AcDbBlockFlipGrip");
        w.f64(140, value->orientation.x);
        w.f64(141, value->orientation.y);
        w.f64(142, value->orientation.z);
        w.i32(93, value->combined_state);
    } else if (kind == DynamicBlockKind::LookupGrip || kind == DynamicBlockKind::PolarGrip ||
               kind == DynamicBlockKind::RotationGrip || kind == DynamicBlockKind::VisibilityGrip ||
               kind == DynamicBlockKind::XYGrip || kind == DynamicBlockKind::PropertiesTableGrip) {
        w.grip(*require(std::get_if<BlockGrip>(&object.data)));
        w.subclass(object.cpp_class_name);
    } else if (kind == DynamicBlockKind::AlignmentParameter) {
        auto *value = require(std::get_if<BlockAlignmentParameter>(&object.data));
        w.two_point(value->parameter);
        w.subclass("AcDbBlockAlignmentParameter");
        w.boolean(280, value->align_perpendicular);
    } else if (kind == DynamicBlockKind::BasePointParameter) {
        auto *value = require(std::get_if<BlockBasePointParameter>(&object.data));
        w.one_point(value->parameter);
        w.subclass("AcDbBlockBasepointParameter");
        w.point3(1011, value->point);
        w.point3(1012, value->base_point);
    } else if (kind == DynamicBlockKind::FlipParameter) {
        auto *value = require(std::get_if<BlockFlipParameter>(&object.data));
        w.two_point(value->parameter);
        w.subclass("AcDbBlockFlipParameter");
        w.text(305, value->flip_label);
        w.text(306, value->flip_label_description);
        w.text(307, value->base_state_label);
        w.text(308, value->flipped_state_label);
        w.point3(1012, value->definition_label_point);
        w.text(309, value->tooltip);
        w.i32(96, value->flags_96);
    } else if (kind == DynamicBlockKind::LinearParameter) {
        auto *value = require(std::get_if<BlockLinearParameter>(&object.data));
        w.two_point(value->parameter);
        w.subclass("AcDbBlockLinearParameter");
        w.text(305, value->distance_name);
        w.text(306, value->distance_description);
        w.f64(140, value->distance);
        w.value_set(value->value_set, 96, 141, 175, 307);
    } else if (kind == DynamicBlockKind::LookupParameter) {
        auto *value = require(std::get_if<BlockLookupParameter>(&object.data));
        w.one_point(value->parameter);
        w.subclass("AcDbBlockLookupParameter");
        w.i32(94, value->index);
        w.text(303, value->lookup_name);
        w.text(304, value->lookup_description);
    } else if (kind == DynamicBlockKind::PointParameter) {
        auto *value = require(std::get_if<BlockPointParameter>(&object.data));
        w.one_point(value->parameter);
        w.subclass("AcDbBlockPointParameter");
        w.text(303, value->position_name);
        w.text(304, value->position_description);
        w.point3(1011, value->definition_label_point);
    } else if (kind == DynamicBlockKind::PolarParameter) {
        auto *value = require(std::get_if<BlockPolarParameter>(&object.data));
        w.two_point(value->parameter);
        w.subclass("AcDbBlockPolarParameter");
        w.text(305, value->distance_name);
        w.text(306, value->distance_description);
        w.text(307, value->angle_name);
        w.text(308, value->angle_description);
        w.f64(140, value->offset);
        w.value_set(value->distance_value_set, 96, 141, 175, 309);
        w.value_set(value->angle_value_set, 97, 145, 176, 410);
    } else if (kind == DynamicBlockKind::RotationParameter) {
        auto *value = require(std::get_if<BlockRotationParameter>(&object.data));
        w.two_point(value->parameter);
        w.subclass("AcDbBlockRotationParameter");
        w.text(305, value->angle_name);
        w.text(306, value->angle_description);
        w.point3(1011, value->definition_base_angle_point);
        w.f64(140, value->angle);
        w.value_set(value->value_set, 96, 141, 175, 307);
    } else if (kind == DynamicBlockKind::XYParameter) {
        auto *value = require(std::get_if<BlockXYParameter>(&object.data));
        w.two_point(value->parameter);
        w.subclass("AcDbBlockXYParameter");
        w.text(305, value->y_label);
        w.text(306, value->x_label);
        w.text(307, value->y_label_description);
        w.text(308, value->x_label_description);
        w.f64(141, value->x_value);
        w.f64(140, value->y_value);
        w.value_set(value->y_value_set, 97, 146, 176, 309);
        w.value_set(value->x_value_set, 96, 142, 175, 410);
    } else if (kind == DynamicBlockKind::UserParameter) {
        auto *value = require(std::get_if<BlockUserParameter>(&object.data));
        w.one_point(value->parameter);
        w.subclass("AcDbBlockUserParameter");
        w.i16(90, value->flags);
        w.handle(330, value->associated_variable);
        w.text(301, value->expression);
        w.i16(70, value->value_code);
        w.eval_value(value->value_code, value->value, 40);
        w.i16(170, value->value_type);
    } else if (kind == DynamicBlockKind::VisibilityParameter) {
        auto *value = require(std::get_if<BlockVisibilityParameter>(&object.data));
        BlockEvalExpression eval;
        eval.parent_id = -1;
        eval.major = value->eval_major;
        eval.minor = value->eval_minor;
        eval.value_code = value->eval_value_code;
        eval.value = value->eval_value;
        eval.node_id = value->eval_node_id;
        w.eval(eval);
        w.subclass("AcDbBlockElement");
        w.text(300, value->element_name);
        w.i32(98, value->element_major);
        w.i32(99, value->element_minor);
        w.i32(1071, value->element_eed_1071);
        w.subclass("AcDbBlockParameter");
        w.boolean(280, value->show_properties);
        w.boolean(281, value->chain_actions);
        w.subclass("AcDbBlock1PtParameter");
        w.point3(1010, value->definition_point);
        w.i32(93, value->property_count);
        for (std::size_t i = 0; i < value->property_info.size(); ++i)
            w.property(value->property_info[i], 170 + static_cast<int>(i), 91 + static_cast<int>(i),
                       301 + static_cast<int>(i));
        w.subclass("AcDbBlockVisibilityParameter");
        w.boolean(281, value->is_initialized);
        w.text(301, value->name);
        w.text(302, value->description);
        w.boolean(91, value->unknown_bool);
        w.i32(93, static_cast<std::int32_t>(value->all_blocks.size()));
        for (auto h : value->all_blocks)
            w.handle(331, h);
        w.i32(92, static_cast<std::int32_t>(value->states.size()));
        for (const auto &state : value->states) {
            w.text(303, state.name);
            w.i32(94, static_cast<std::int32_t>(state.visible_blocks.size()));
            for (auto h : state.visible_blocks)
                w.handle(332, h);
            w.i32(95, static_cast<std::int32_t>(state.visible_params.size()));
            for (auto h : state.visible_params)
                w.handle(333, h);
        }
    } else if (kind == DynamicBlockKind::AngularConstraintParameter) {
        auto *value = require(std::get_if<BlockAngularConstraintParameter>(&object.data));
        w.constraint(value->constraint);
        w.subclass("AcDbBlockAngularConstraintParameter");
        w.point3(1011, value->center_point);
        w.point3(1012, value->end_point);
        w.text(305, value->expression_name);
        w.text(306, value->expression_description);
        w.f64(140, value->angle);
        w.boolean(280, value->orientation_on_both_grips);
        w.value_set(value->value_set, 96, 128, 175, 307);
    } else if (kind == DynamicBlockKind::DiametricConstraintParameter ||
               kind == DynamicBlockKind::RadialConstraintParameter) {
        auto *value = require(std::get_if<BlockDistanceConstraintParameter>(&object.data));
        w.constraint(value->constraint);
        w.subclass(object.cpp_class_name);
        w.text(305, value->expression_name);
        w.text(306, value->expression_description);
        w.f64(140, value->distance);
        w.value_set(value->value_set, 96, 128, 175, 307);
    } else if (kind == DynamicBlockKind::AlignedConstraintParameter ||
               kind == DynamicBlockKind::LinearConstraintParameter ||
               kind == DynamicBlockKind::HorizontalConstraintParameter ||
               kind == DynamicBlockKind::VerticalConstraintParameter) {
        w.linear_constraint(*require(std::get_if<BlockLinearConstraintParameter>(&object.data)));
        if (object.dxf_name != "BLOCKLINEARCONSTRAINTPARAMETER")
            w.subclass(object.cpp_class_name);
    } else if (kind == DynamicBlockKind::ParameterDependencyBody) {
        auto *value = require(std::get_if<BlockParameterDependencyBody>(&object.data));
        w.subclass("AcDbAssocDependencyBody");
        w.i16(90, value->dependency_body_version);
        w.subclass("AcDbImpAssocDimDependencyBodyBase");
        w.i16(90, value->dimension_base_version);
        w.text(1, value->name);
        w.subclass("AcDbBlockParameterDependencyBody");
        w.i16(90, value->class_version);
    } else if (kind == DynamicBlockKind::MoveAction) {
        auto *value = require(std::get_if<BlockMoveAction>(&object.data));
        w.action(value->action);
        w.subclass("AcDbBlockMoveAction");
        w.sequential(value->connections, 92, 301);
        w.offsets(value->offsets);
    } else if (kind == DynamicBlockKind::FlipAction) {
        auto *value = require(std::get_if<BlockFlipAction>(&object.data));
        w.action(value->action);
        w.subclass("AcDbBlockFlipAction");
        w.sequential(value->connections, 92, 301);
    } else if (kind == DynamicBlockKind::RotateAction || kind == DynamicBlockKind::ScaleAction) {
        auto *value = require(std::get_if<BlockBasePointAction>(&object.data));
        w.action_base(value->action);
        w.subclass(kind == DynamicBlockKind::RotateAction ? "AcDbBlockRotationAction" : "AcDbBlockScaleAction");
        w.sequential(value->connections, 94, 303);
    } else if (kind == DynamicBlockKind::ArrayAction) {
        auto *value = require(std::get_if<BlockArrayAction>(&object.data));
        w.action(value->action);
        w.subclass("AcDbBlockArrayAction");
        w.sequential(value->connections, 92, 301);
        w.f64(140, value->row_offset);
        w.f64(141, value->column_offset);
    } else if (kind == DynamicBlockKind::LookupAction) {
        auto *value = require(std::get_if<BlockLookupAction>(&object.data));
        w.action(value->action);
        w.subclass("AcDbBlockLookupAction");
        w.i32(92, value->row_count);
        w.i32(93, value->column_count);
        w.text(301, "");
        for (const auto &expression : value->expressions)
            w.text(302, expression);
        for (const auto &column : value->columns) {
            w.text(303, "");
            w.i32(94, column.node_id);
            w.i32(95, column.value_type);
            w.i32(96, column.property_type);
            w.boolean(282, column.lookup_property);
            w.text(305, column.unmatched_name);
            w.boolean(281, column.writable);
            w.text(304, column.connection_name);
        }
        w.boolean(280, value->flag_280);
    } else if (kind == DynamicBlockKind::PropertiesTable) {
        auto *value = require(std::get_if<BlockPropertiesTable>(&object.data));
        w.one_point(value->parameter);
        w.subclass("AcDbBlockPropertiesTable");
        for (const auto &field : value->fields)
            std::visit(
                [&](const auto &item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, std::string>)
                        w.text(field.code, item);
                    else if constexpr (std::is_same_v<T, bool>)
                        w.boolean(field.code, item);
                    else if constexpr (std::is_same_v<T, std::int64_t>) {
                        if (field.code >= 160 && field.code <= 169)
                            w.i64(field.code, item);
                        else if ((field.code >= 60 && field.code <= 79) || (field.code >= 170 && field.code <= 179) ||
                                 (field.code >= 270 && field.code <= 279) ||
                                 (field.code >= 370 && field.code <= 389) ||
                                 (field.code >= 400 && field.code <= 409))
                            w.i16(field.code, static_cast<std::int16_t>(item));
                        else
                            w.i32(field.code, static_cast<std::int32_t>(item));
                    } else if constexpr (std::is_same_v<T, double>)
                        w.f64(field.code, item);
                    else if constexpr (std::is_same_v<T, std::uint64_t>)
                        w.handle(field.code, item);
                    else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>)
                        w.text(field.code, hex_bytes(item));
                },
                field.value);
    } else if (kind == DynamicBlockKind::StretchAction) {
        auto *value = require(std::get_if<BlockStretchAction>(&object.data));
        w.action(value->action);
        w.subclass("AcDbBlockStretchAction");
        w.sequential(value->connections, 92, 301);
        w.i32(72, static_cast<std::int32_t>(value->points.size()));
        for (auto point : value->points)
            w.point2(1011, point);
        w.i32(73, static_cast<std::int32_t>(value->handles.size()));
        for (const auto &item : value->handles) {
            w.handle(331, item.handle);
            w.i16(74, static_cast<std::int16_t>(item.indexes.size()));
            for (auto index : item.indexes)
                w.i32(94, index);
        }
        w.i32(75, static_cast<std::int32_t>(value->codes.size()));
        for (const auto &item : value->codes) {
            w.i32(95, item.code);
            w.i16(76, static_cast<std::int16_t>(item.indexes.size()));
            for (auto index : item.indexes)
                w.i32(94, index);
        }
        w.offsets(value->offsets);
    } else if (kind == DynamicBlockKind::PolarStretchAction) {
        auto *value = require(std::get_if<BlockPolarStretchAction>(&object.data));
        w.action(value->action);
        w.subclass("AcDbBlockPolarStretchAction");
        w.sequential(value->connections, 92, 301);
        w.i32(73, static_cast<std::int32_t>(value->points.size()));
        for (auto point : value->points)
            w.point2(1011, point);
        w.i32(72, static_cast<std::int32_t>(value->handles.size()));
        for (auto h : value->handles)
            w.handle(331, h);
        w.i32(74, static_cast<std::int32_t>(value->bindings.size()));
        for (const auto &item : value->bindings) {
            w.handle(332, item.handle);
            w.i32(75, static_cast<std::int32_t>(item.indexes.size()));
            for (auto index : item.indexes)
                w.i32(76, index);
        }
        w.i32(78, static_cast<std::int32_t>(value->codes.size()));
        for (const auto &item : value->codes) {
            w.i32(98, item.code);
            w.i32(79, static_cast<std::int32_t>(item.indexes.size()));
            for (auto index : item.indexes)
                w.i32(76, index);
        }
        w.f64(141, value->distance_multiplier);
        w.f64(140, value->angle_offset);
        w.i32(77, 0);
    } else if (kind == DynamicBlockKind::EmptyEntity)
        w.subclass(object.cpp_class_name.empty()
                       ? dynamic_block_cpp_class_name(object.dxf_name).value_or("AcDbEntity")
                       : object.cpp_class_name);
    else if (kind == DynamicBlockKind::AngularConstraintParameterEntity) {
        auto *value = require(std::get_if<BlockAngularConstraintParameterEntity>(&object.data));
        w.constraint(value->constraint);
        w.subclass("AcDbBlockAngularConstraintParameterEntity");
        w.point3(1011, value->center_point);
        w.point3(1012, value->label_point);
        w.text(305, value->expression_name);
        w.text(306, value->expression_description);
        w.f64(140, value->angle);
        w.boolean(280, value->orientation_on_both_grips);
        w.value_set(value->value_set, 96, 128, 175, 307);
    } else
        throw std::invalid_argument("Cannot rewrite unknown dynamic object");
    return w.fields;
}
void visit_eval_value(BlockEvalValue &value, const std::function<void(std::uint64_t &)> &visit) {
    if (auto *handle = std::get_if<std::uint64_t>(&value))
        visit(*handle);
}
void visit_element(BlockElement &value, const std::function<void(std::uint64_t &)> &visit) {
    visit_eval_value(value.eval.value, visit);
}
void visit_one_point(BlockOnePointParameter &value, const std::function<void(std::uint64_t &)> &visit) {
    visit_element(value.parameter.element, visit);
}
void visit_two_point(BlockTwoPointParameter &value, const std::function<void(std::uint64_t &)> &visit) {
    visit_element(value.parameter.element, visit);
}
void visit_grip(BlockGrip &value, const std::function<void(std::uint64_t &)> &visit) {
    visit_element(value.element, visit);
}
void visit_action(BlockAction &value, const std::function<void(std::uint64_t &)> &visit) {
    visit_element(value.element, visit);
    for (auto &handle : value.dependencies)
        visit(handle);
}
void visit_constraint(BlockConstraintParameter &value, const std::function<void(std::uint64_t &)> &visit) {
    visit_two_point(value.parameter, visit);
    visit(value.dependency);
}
void visit_dynamic_data(DynamicBlockPayload &data, const std::function<void(std::uint64_t &)> &visit) {
    std::visit(
        [&](auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, BlockRepresentationData>)
                visit(value.block);
            else if constexpr (std::is_same_v<T, BlockEvalExpression>)
                visit_eval_value(value.value, visit);
            else if constexpr (std::is_same_v<T, BlockGripExpression>)
                visit_eval_value(value.eval.value, visit);
            else if constexpr (std::is_same_v<T, BlockOrientedGrip>)
                visit_grip(value.grip, visit);
            else if constexpr (std::is_same_v<T, BlockFlipGrip>)
                visit_grip(value.grip, visit);
            else if constexpr (std::is_same_v<T, BlockGrip>)
                visit_grip(value, visit);
            else if constexpr (std::is_same_v<T, BlockAlignmentParameter>)
                visit_two_point(value.parameter, visit);
            else if constexpr (std::is_same_v<T, BlockBasePointParameter> ||
                               std::is_same_v<T, BlockLookupParameter> ||
                               std::is_same_v<T, BlockPointParameter>)
                visit_one_point(value.parameter, visit);
            else if constexpr (std::is_same_v<T, BlockFlipParameter> ||
                               std::is_same_v<T, BlockLinearParameter> ||
                               std::is_same_v<T, BlockPolarParameter> ||
                               std::is_same_v<T, BlockRotationParameter> ||
                               std::is_same_v<T, BlockXYParameter>)
                visit_two_point(value.parameter, visit);
            else if constexpr (std::is_same_v<T, BlockUserParameter>) {
                visit_one_point(value.parameter, visit);
                visit(value.associated_variable);
                visit_eval_value(value.value, visit);
            } else if constexpr (std::is_same_v<T, BlockVisibilityParameter>) {
                visit(value.handle);
                visit(value.owner);
                visit_eval_value(value.eval_value, visit);
                for (auto &handle : value.all_blocks)
                    visit(handle);
                for (auto &state : value.states) {
                    for (auto &handle : state.visible_blocks)
                        visit(handle);
                    for (auto &handle : state.visible_params)
                        visit(handle);
                }
            } else if constexpr (std::is_same_v<T, BlockAngularConstraintParameter> ||
                                 std::is_same_v<T, BlockDistanceConstraintParameter> ||
                                 std::is_same_v<T, BlockLinearConstraintParameter> ||
                                 std::is_same_v<T, BlockAngularConstraintParameterEntity>)
                visit_constraint(value.constraint, visit);
            else if constexpr (std::is_same_v<T, BlockMoveAction> || std::is_same_v<T, BlockFlipAction> ||
                               std::is_same_v<T, BlockArrayAction> || std::is_same_v<T, BlockLookupAction>)
                visit_action(value.action, visit);
            else if constexpr (std::is_same_v<T, BlockBasePointAction>)
                visit_action(value.action.action, visit);
            else if constexpr (std::is_same_v<T, BlockStretchAction>) {
                visit_action(value.action, visit);
                for (auto &item : value.handles)
                    visit(item.handle);
            } else if constexpr (std::is_same_v<T, BlockPolarStretchAction>) {
                visit_action(value.action, visit);
                for (auto &handle : value.handles)
                    visit(handle);
                for (auto &item : value.bindings)
                    visit(item.handle);
            } else if constexpr (std::is_same_v<T, BlockPropertiesTable>) {
                visit_one_point(value.parameter, visit);
                for (auto &field : value.fields)
                    if (auto *handle = std::get_if<std::uint64_t>(&field.value))
                        visit(*handle);
            }
        },
        data);
}
