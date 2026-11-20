// SPDX-License-Identifier: MPL-2.0
// Included from associative.cpp. DXF associative object codec.
struct AssocPair {
    int code = 0;
    std::string value;
};
struct AssocDxfRecord {
    std::uint64_t handle = 0;
    std::uint64_t owner = 0;
    std::vector<std::uint64_t> reactors;
    std::optional<std::uint64_t> xdictionary_handle;
    std::map<std::string, std::vector<AssocPair>> sections;
    std::size_t ambiguous_value_params = 0;
    std::vector<std::string> values(const std::string &section, int code) const {
        std::vector<std::string> out;
        auto found = sections.find(section);
        if (found == sections.end())
            return out;
        for (const auto &pair : found->second)
            if (pair.code == code)
                out.push_back(pair.value);
        return out;
    }
    std::int32_t i32(const std::string &section, int code, std::size_t index) const {
        auto items = values(section, code);
        return index < items.size() ? integer_of(items[index]) : 0;
    }
    std::int16_t i16(const std::string &section, int code, std::size_t index) const {
        return static_cast<std::int16_t>(i32(section, code, index));
    }
    std::uint8_t byte(const std::string &section, int code, std::size_t index) const {
        return static_cast<std::uint8_t>(i32(section, code, index));
    }
    bool boolean(const std::string &section, int code, std::size_t index) const {
        return i32(section, code, index) != 0;
    }
    double f64(const std::string &section, int code, std::size_t index) const {
        auto items = values(section, code);
        return index < items.size() ? real_of(items[index]) : 0;
    }
    std::string text(const std::string &section, int code, std::size_t index) const {
        auto items = values(section, code);
        return index < items.size() ? items[index] : std::string{};
    }
    std::uint64_t handle_at(const std::string &section, int code, std::size_t index) const {
        auto items = values(section, code);
        return index < items.size() ? handle_of(items[index]) : 0;
    }
    Vector3 point(const std::string &section, int code, std::size_t index) const {
        return {f64(section, code, index), f64(section, code + 10, index), f64(section, code + 20, index)};
    }
};
struct AssocCursor {
    const AssocPair *entries = nullptr;
    std::size_t size = 0;
    std::size_t position = 0;
    std::size_t *ambiguous = nullptr;
    AssocCursor() = default;
    AssocCursor(const AssocDxfRecord &record, const std::string &section, std::size_t *amb) : ambiguous(amb) {
        auto found = record.sections.find(section);
        if (found != record.sections.end() && !found->second.empty()) {
            entries = found->second.data();
            size = found->second.size();
        }
    }
    AssocCursor slice(std::size_t begin, std::size_t end) const {
        AssocCursor out;
        if (entries && begin < size) {
            out.entries = entries + begin;
            out.size = std::min(end, size) - begin;
        }
        out.ambiguous = ambiguous;
        return out;
    }
    std::optional<int> peek_code() const {
        if (position >= size)
            return std::nullopt;
        return entries[position].code;
    }
    std::optional<std::string_view> next(int code) {
        while (position < size) {
            const auto &pair = entries[position++];
            if (pair.code == code)
                return pair.value;
        }
        return std::nullopt;
    }
    std::int32_t i32(int code) { return integer_of(next(code).value_or("")); }
    std::int64_t i64(int code) { return integer64_of(next(code).value_or("")); }
    std::int16_t i16(int code) { return static_cast<std::int16_t>(i32(code)); }
    bool boolean(int code) { return i32(code) != 0; }
    double f64(int code) { return real_of(next(code).value_or("")); }
    std::string text(int code) { return std::string(next(code).value_or("")); }
    std::uint64_t handle(int code) { return handle_of(next(code).value_or("")); }
    Vector3 point3(int code) { return {f64(code), f64(code + 10), f64(code + 20)}; }
    std::vector<std::uint64_t> consecutive_handles(int code) {
        std::vector<std::uint64_t> values;
        while (peek_code() == code)
            values.push_back(handle(code));
        return values;
    }
    std::vector<std::string> consecutive_text(int code) {
        std::vector<std::string> values;
        while (peek_code() == code)
            values.push_back(text(code));
        return values;
    }
};
struct AssocWriter {
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
};
bool is_eval_handle_code(std::int16_t code) {
    return code < 0 || code == 5 || code == 105 || (code >= 320 && code <= 369) ||
           (code >= 390 && code <= 399);
}
bool is_eval_text_code(std::int16_t code) {
    return (code >= 0 && code <= 9) || (code >= 100 && code <= 102) || (code >= 300 && code <= 309) ||
           (code >= 410 && code <= 419) || (code >= 430 && code <= 439) || (code >= 470 && code <= 479) ||
           code == 999 || (code >= 1000 && code <= 1009);
}
bool is_eval_real_code(std::int16_t code) {
    return (code >= 38 && code <= 59) || (code >= 140 && code <= 149) || (code >= 460 && code <= 469) ||
           (code >= 1040 && code <= 1042);
}
bool is_eval_short_code(std::int16_t code) {
    return (code >= 60 && code <= 79) || (code >= 170 && code <= 179) || (code >= 270 && code <= 279) ||
           (code >= 370 && code <= 389) || (code >= 400 && code <= 409) || code == 1070;
}
bool is_eval_long_code(std::int16_t code) {
    return (code >= 80 && code <= 99) || (code >= 420 && code <= 429) || (code >= 440 && code <= 459) ||
           code == 1071;
}
AssocEvalVariant read_eval(AssocCursor &cursor, std::optional<std::size_t> handle_values_remaining) {
    if (!cursor.peek_code())
        return {};
    std::int16_t code = 0;
    if (cursor.peek_code() == 70) {
        auto marker = integer_of(cursor.entries[cursor.position].value);
        auto next_code = cursor.position + 1 < cursor.size
                             ? static_cast<std::int16_t>(cursor.entries[cursor.position + 1].code)
                             : std::int16_t{0};
        if (marker == next_code) {
            code = cursor.i16(70);
        } else
            code = 70;
    } else
        code = static_cast<std::int16_t>(*cursor.peek_code());
    if (handle_values_remaining && code == 330) {
        bool followed = cursor.position + 1 < cursor.size && cursor.entries[cursor.position + 1].code == 330;
        if (*handle_values_remaining == 0 || !followed)
            return {};
    }
    AssocEvalVariant value;
    value.code = code;
    if (is_eval_handle_code(code))
        value.value = cursor.handle(code);
    else if (is_eval_text_code(code))
        value.value = cursor.text(code);
    else if (is_eval_real_code(code))
        value.value = cursor.f64(code);
    else if (is_eval_short_code(code))
        value.value = cursor.i16(code);
    else if (is_eval_long_code(code))
        value.value = cursor.i32(code);
    else if (code >= 280 && code <= 289)
        value.value = static_cast<std::uint8_t>(cursor.i32(code));
    else
        value.value = std::monostate{};
    return value;
}
AssocValueParam read_value_param(AssocCursor &cursor) {
    AssocValueParam value;
    value.class_version = cursor.i32(90);
    value.name = cursor.text(1);
    value.unit_type = cursor.i32(90);
    auto count = capped(cursor.i32(90));
    auto tail = cursor.size > cursor.position ? cursor.size - cursor.position : 0;
    std::size_t end = tail;
    for (std::size_t i = 0; i + 1 < tail; ++i)
        if (cursor.entries[cursor.position + i].code == 90 &&
            cursor.entries[cursor.position + i + 1].code == 1) {
            end = i;
            break;
        }
    std::size_t handle_values_remaining = 0;
    for (std::size_t i = 0; i < end; ++i) {
        auto code = cursor.entries[cursor.position + i].code;
        if (code >= 330 && code <= 369)
            ++handle_values_remaining;
    }
    handle_values_remaining = handle_values_remaining > static_cast<std::size_t>(count) + 1
                                  ? handle_values_remaining - static_cast<std::size_t>(count) - 1
                                  : 0;
    int slots_at_330 = 0, handle_values_at_330 = 0;
    value.variables.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        bool starts_at_330 = cursor.peek_code() == 330;
        auto eval = read_eval(cursor, handle_values_remaining);
        if (is_eval_handle_code(eval.code) && std::holds_alternative<std::uint64_t>(eval.value))
            handle_values_remaining = handle_values_remaining ? handle_values_remaining - 1 : 0;
        if (starts_at_330) {
            ++slots_at_330;
            if (std::holds_alternative<std::uint64_t>(eval.value))
                ++handle_values_at_330;
        }
        AssocValueParamVariable variable;
        variable.value = std::move(eval);
        variable.handle = cursor.handle(330);
        value.variables.push_back(std::move(variable));
    }
    if (handle_values_at_330 > 0 && handle_values_at_330 < slots_at_330 && cursor.ambiguous)
        ++*cursor.ambiguous;
    value.controlled_object_dependency = cursor.handle(330);
    return value;
}
AssocDependency read_dependency(const AssocDxfRecord &record) {
    AssocCursor cursor(record, "AcDbAssocDependency", nullptr);
    AssocDependency value;
    value.class_version = cursor.i16(90);
    value.status = cursor.i32(90);
    value.is_read_dependency = cursor.boolean(290);
    value.is_write_dependency = cursor.boolean(290);
    value.is_attached_to_object = cursor.boolean(290);
    value.is_delegating_to_owning_action = cursor.boolean(290);
    value.order = cursor.i32(90);
    value.dependent_on = cursor.handle(330);
    if (cursor.boolean(290))
        value.name = cursor.text(1);
    value.read_dependency = cursor.handle(330);
    value.node = cursor.handle(330);
    value.dependency_body = cursor.handle(360);
    value.dependency_body_id = cursor.i32(90);
    return value;
}
AssocAction read_action(const AssocDxfRecord &record) {
    AssocCursor cursor(record, "AcDbAssocAction", nullptr);
    AssocAction value;
    value.class_version = cursor.i16(90);
    value.geometry_status = cursor.i32(90);
    value.owning_network = cursor.handle(330);
    value.action_body = cursor.handle(360);
    value.action_index = cursor.i32(90);
    value.max_dependency_index = cursor.i32(90);
    auto count = capped(cursor.i32(90));
    value.dependencies.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        AssocActionDependency dependency;
        while (cursor.position < cursor.size) {
            const auto &pair = cursor.entries[cursor.position++];
            if (pair.code == 330 || pair.code == 360) {
                dependency.dependency = handle_of(pair.value);
                dependency.is_owned = pair.code == 360;
                break;
            }
        }
        value.dependencies.push_back(dependency);
    }
    if (value.class_version > 1) {
        cursor.i16(90);
        count = capped(cursor.i32(90));
        value.owned_parameters.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
            value.owned_parameters.push_back(cursor.handle(360));
        cursor.i16(90);
        count = capped(cursor.i32(90));
        value.values.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
            value.values.push_back(read_value_param(cursor));
    }
    return value;
}
void skip_action(AssocCursor &cursor) {
    auto class_version = cursor.i16(90);
    cursor.i32(90);
    cursor.handle(330);
    cursor.handle(360);
    cursor.i32(90);
    cursor.i32(90);
    auto count = capped(cursor.i32(90));
    for (int i = 0; i < count; ++i) {
        if (cursor.peek_code() == 360)
            cursor.handle(360);
        else
            cursor.handle(330);
    }
    if (class_version > 1) {
        cursor.i16(90);
        auto owned = capped(cursor.i32(90));
        for (int i = 0; i < owned; ++i)
            cursor.handle(360);
        cursor.i16(90);
        auto values = capped(cursor.i32(90));
        for (int i = 0; i < values; ++i)
            read_value_param(cursor);
    }
}
AssocActionParam read_action_param(const AssocDxfRecord &record) {
    AssocCursor cursor(record, "AcDbAssocActionParam", nullptr);
    AssocActionParam value;
    value.is_r2013 = cursor.i16(90);
    if (cursor.peek_code() == 90)
        value.version = cursor.i32(90);
    value.name = cursor.text(1);
    return value;
}
AssocParamBasedActionBody read_parameter_body(const AssocDxfRecord &record) {
    AssocCursor cursor(record, "AcDbAssocParamBasedActionBody", nullptr);
    AssocParamBasedActionBody value;
    value.version = cursor.i32(90);
    value.minor = cursor.i32(90);
    auto count = capped(cursor.i32(90));
    value.dependencies.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        value.dependencies.push_back(cursor.handle(360));
    value.marker = cursor.i32(90);
    count = capped(cursor.i32(90));
    if (count == 0) {
        value.empty_value_marker = cursor.i32(90);
        value.dependency = cursor.handle(330);
    }
    value.values.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        value.values.push_back(read_value_param(cursor));
    return value;
}
AssocCompoundActionParam read_compound(const AssocDxfRecord &record) {
    AssocCursor cursor(record, "AcDbAssocCompoundActionParam", nullptr);
    AssocCompoundActionParam value;
    value.action_param = read_action_param(record);
    value.class_version = cursor.i16(90);
    value.status = cursor.i16(90);
    auto count = capped(cursor.i32(90));
    value.parameters.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        value.parameters.push_back(cursor.handle(360));
    if (cursor.position < cursor.size) {
        AssocChildParameter child;
        child.status = cursor.i16(90);
        child.id = cursor.i32(90);
        child.parameter = cursor.handle(330);
        if (child.id != 0) {
            child.secondary_parameter = cursor.handle(330);
            child.marker = cursor.i32(90);
            child.tertiary_parameter = cursor.handle(330);
        }
        value.child_parameter = child;
    }
    return value;
}
AssocSingleDependencyActionParam read_single(const AssocDxfRecord &record, const std::string &section) {
    AssocSingleDependencyActionParam value;
    value.action_param = read_action_param(record);
    value.dependency_class_version = record.i32("AcDbAssocSingleDependencyActionParam", 90, 0);
    value.dependency = record.handle_at("AcDbAssocSingleDependencyActionParam", 330, 0);
    value.class_version = record.i32(section, 90, 0);
    return value;
}
AssocArrayItem read_array_item(AssocCursor &cursor) {
    AssocArrayItem item;
    item.class_version = cursor.i32(90);
    item.location = {cursor.i32(90), cursor.i32(90), cursor.i32(90)};
    item.flags = cursor.i32(90);
    item.uses_default_transform = cursor.peek_code() == 11;
    if (item.uses_default_transform) {
        item.x_direction.x = cursor.f64(11);
        item.x_direction.y = cursor.f64(21);
        item.x_direction.z = cursor.f64(31);
    } else {
        for (auto &value : item.transform)
            value = cursor.f64(40);
    }
    if (item.flags & 2) {
        std::array<double, 16> matrix{};
        for (auto &value : matrix)
            value = cursor.f64(40);
        item.relative_transform = matrix;
    }
    std::size_t consecutive = 0;
    for (std::size_t i = cursor.position; i < cursor.size && cursor.entries[i].code == 330; ++i)
        ++consecutive;
    if (consecutive > std::size_t((item.flags & 0x10) != 0))
        item.first_handle = cursor.handle(330);
    if (item.flags & 0x10)
        item.second_handle = cursor.handle(330);
    return item;
}
AssocArrayParameters read_array_parameters(const AssocDxfRecord &record) {
    AssocCursor cursor(record, "AcDbAssocArrayCommonParameters", nullptr);
    AssocArrayParameters value;
    value.version = cursor.i32(90);
    auto count = capped(cursor.i32(90));
    value.class_name = cursor.text(1);
    value.items.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        value.items.push_back(read_array_item(cursor));
    value.item_count = cursor.i32(90);
    value.row_count = cursor.i32(90);
    value.level_count = cursor.i32(90);
    return value;
}
std::optional<AssocSurfaceActionKind> surface_kind(const std::string &name) {
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
    return std::nullopt;
}
const char *surface_section(AssocSurfaceActionKind kind) {
    switch (kind) {
    case AssocSurfaceActionKind::Plane:
        return "AcDbAssocPlaneSurfaceActionBody";
    case AssocSurfaceActionKind::Extend:
        return "AcDbAssocExtendSurfaceActionBody";
    case AssocSurfaceActionKind::Extruded:
        return "AcDbAssocExtrudedSurfaceActionBody";
    case AssocSurfaceActionKind::Lofted:
        return "AcDbAssocLoftedSurfaceActionBody";
    case AssocSurfaceActionKind::Network:
        return "AcDbAssocNetworkSurfaceActionBody";
    case AssocSurfaceActionKind::Offset:
        return "AcDbAssocOffsetSurfaceActionBody";
    case AssocSurfaceActionKind::Revolved:
        return "AcDbAssocRevolvedSurfaceActionBody";
    case AssocSurfaceActionKind::Trim:
        return "AcDbAssocTrimSurfaceActionBody";
    case AssocSurfaceActionKind::Blend:
        return "AcDbAssocBlendSurfaceActionBody";
    case AssocSurfaceActionKind::Patch:
        return "AcDbAssocPatchSurfaceActionBody";
    case AssocSurfaceActionKind::Fillet:
        return "AcDbAssocFilletSurfaceActionBody";
    case AssocSurfaceActionKind::Swept:
        return "AcDbAssocSweptSurfaceActionBody";
    case AssocSurfaceActionKind::EdgeChamfer:
        return "AcDbAssocEdgeChamferActionBody";
    case AssocSurfaceActionKind::EdgeFillet:
        return "AcDbAssocEdgeFilletActionBody";
    }
    return "AcDbAssocPlaneSurfaceActionBody";
}
AssocSurfaceActionBody read_surface(const AssocDxfRecord &record, AssocSurfaceActionKind kind) {
    AssocSurfaceActionBody value;
    value.kind = kind;
    value.action_body.version = record.i32("AcDbAssocActionBody", 90, 0);
    value.parameter_body = read_parameter_body(record);
    value.surface_body.version = record.i32("AcDbAssocSurfaceActionBody", 90, 0);
    value.surface_body.dependency = record.handle_at("AcDbAssocSurfaceActionBody", 330, 0);
    value.surface_body.is_semi_associative = record.boolean("AcDbAssocSurfaceActionBody", 290, 0);
    value.surface_body.marker = record.i32("AcDbAssocSurfaceActionBody", 90, 1);
    value.surface_body.is_semi_override = record.boolean("AcDbAssocSurfaceActionBody", 290, 1);
    value.surface_body.grip_status = record.i16("AcDbAssocSurfaceActionBody", 70, 0);
    value.path_status = record.i32("AcDbAssocPathBasedSurfaceActionBody", 90, 0);
    auto section = surface_section(kind);
    value.class_version = record.i32(section, 90, 0);
    if (kind == AssocSurfaceActionKind::Extend)
        value.option = record.byte(section, 280, 0);
    else if (kind == AssocSurfaceActionKind::Offset)
        value.flags[0] = record.boolean(section, 290, 0);
    else if (kind == AssocSurfaceActionKind::Trim) {
        value.flags[0] = record.boolean(section, 290, 0);
        value.flags[1] = record.boolean(section, 290, 1);
        value.distance = record.f64(section, 40, 0);
    } else if (kind == AssocSurfaceActionKind::Blend) {
        for (int i = 0; i < 5; ++i)
            value.flags[static_cast<std::size_t>(i)] = record.boolean(section, 290 + i, 0);
        value.status = record.i16(section, 72, 0);
        value.secondary_status = record.i16(section, 73, 0);
    } else if (kind == AssocSurfaceActionKind::Fillet) {
        value.status = record.i16(section, 70, 0);
        value.first_point = {record.f64(section, 10, 0), record.f64(section, 20, 0)};
        value.second_point = {record.f64(section, 10, 1), record.f64(section, 20, 1)};
    }
    return value;
}
AssocConstraintNode read_constraint_common(AssocCursor &cursor, DxfVersion version) {
    AssocConstraintNode node;
    node.node_id = cursor.i32(90);
    bool status_before = version < DxfVersion::AC1027;
    if (status_before)
        node.status = static_cast<std::uint8_t>(cursor.i32(70));
    auto count = capped(cursor.i32(90));
    node.connections.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        node.connections.push_back(cursor.i32(90));
    if (!status_before)
        node.status = static_cast<std::uint8_t>(cursor.i32(70));
    return node;
}
AssocConstraintNode read_constraint_root(AssocCursor &cursor) {
    AssocConstraintNode node;
    node.node_id = cursor.i32(90);
    auto count = capped(cursor.i32(90));
    node.connections.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        node.connections.push_back(cursor.i32(90));
    node.status = static_cast<std::uint8_t>(cursor.i32(290));
    return node;
}
bool is_plain_geometrical_constraint(const std::string &class_name) {
    auto name = ascii_upper(class_name);
    static const std::unordered_set<std::string> names = {
        "ACCENTERPOINTCONSTRAINT",
        "ACCOLINEARCONSTRAINT",
        "ACCONCENTRICCONSTRAINT",
        "ACEQUALCURVATURECONSTRAINT",
        "ACEQUALDISTANCECONSTRAINT",
        "ACEQUALHELPPARAMETERCONSTRAINT",
        "ACEQUALLENGTHCONSTRAINT",
        "ACEQUALRADIUSCONSTRAINT",
        "ACFIXEDCONSTRAINT",
        "ACMIDPOINTCONSTRAINT",
        "ACNORMALCONSTRAINT",
        "ACPERPENDICULARCONSTRAINT",
        "ACPOINTCOINCIDENCECONSTRAINT",
        "ACPOINTCURVECONSTRAINT",
        "ACSYMMETRICCONSTRAINT",
        "ACTANGENTCONSTRAINT",
    };
    return names.count(name) != 0;
}
AssocConstraintNodeData read_constraint_data(AssocCursor &cursor, const std::string &class_name,
                                             bool circle_reserved) {
    auto name = ascii_upper(class_name);
    auto geometry = [&] {
        return std::tuple{cursor.i32(90), cursor.boolean(290), cursor.boolean(290)};
    };
    auto explicit_constraint = [&] {
        auto [owner, implied, active] = geometry();
        return std::tuple{owner, implied, active, cursor.handle(340), cursor.handle(340)};
    };
    if (name == "ACG2SMOOTHCONSTRAINT") {
        AssocConstraintComposite value;
        std::tie(value.owner_id, value.is_implied, value.is_active) = geometry();
        auto count = capped(cursor.i32(90));
        for (int i = 0; i < count; ++i)
            value.owned_constraint_ids.push_back(cursor.i32(90));
        return value;
    }
    if (name == "ACHELPPARAMETER")
        return AssocConstraintHelpParameter{cursor.f64(40), cursor.boolean(290)};
    if (name == "ACCONSTRAINEDCIRCLE") {
        AssocConstraintCircle value;
        value.geometry_dependency = cursor.handle(330);
        value.geometry_node_id = cursor.i32(90);
        value.center = cursor.point3(10);
        value.normal = cursor.point3(10);
        value.direction = cursor.point3(10);
        value.radius = cursor.f64(40);
        value.start_parameter = cursor.f64(40);
        value.end_parameter = cursor.f64(40);
        value.reserved = circle_reserved ? cursor.f64(40) : 0;
        return value;
    }
    if (name == "ACCONSTRAINEDARC") {
        AssocConstraintArc value;
        value.geometry_dependency = cursor.handle(330);
        value.geometry_node_id = cursor.i32(90);
        value.center = cursor.point3(10);
        value.normal = cursor.point3(10);
        value.direction = cursor.point3(10);
        value.radius = cursor.f64(40);
        value.start_parameter = cursor.f64(40);
        value.end_parameter = cursor.f64(40);
        value.reserved = circle_reserved ? cursor.f64(40) : 0;
        value.start_point = cursor.point3(10);
        value.end_point = cursor.point3(11);
        return value;
    }
    if (name == "ACCONSTRAINEDIMPLICITPOINT") {
        AssocConstraintImplicitPoint value;
        value.geometry_dependency = cursor.handle(330);
        value.geometry_node_id = cursor.i32(90);
        if (value.geometry_dependency && cursor.peek_code() == 10)
            value.point = cursor.point3(10);
        value.point_type = static_cast<std::uint8_t>(cursor.i32(280));
        value.point_index = cursor.i32(90);
        value.curve_id = cursor.i32(90);
        return value;
    }
    if (name == "ACCONSTRAINEDPOINT") {
        AssocConstraintPoint value;
        value.geometry_dependency = cursor.handle(330);
        value.geometry_node_id = cursor.i32(90);
        if (value.geometry_dependency && cursor.peek_code() == 10)
            value.point = cursor.point3(10);
        return value;
    }
    if (name == "ACCONSTRAINEDRIGIDSET") {
        AssocConstraintRigidSet value;
        value.geometry_dependency = cursor.handle(330);
        value.geometry_node_id = cursor.i32(90);
        value.reserved = cursor.boolean(290);
        for (auto &item : value.transform)
            item = cursor.f64(40);
        auto count = capped(cursor.i32(90));
        for (int i = 0; i < count; ++i)
            value.geometry_ids.push_back(cursor.i32(90));
        return value;
    }
    if (name == "ACCONSTRAINEDLINE" || name == "ACCONSTRAINEDCONSTRUCTIONLINE" ||
        name == "ACCONSTRAINED2POINTSCONSTRUCTIONLINE" || name == "ACCONSTRAINEDDATUMLINE") {
        AssocConstraintLine value;
        value.geometry_dependency = cursor.handle(330);
        value.geometry_node_id = cursor.i32(90);
        value.point = cursor.point3(10);
        value.direction = cursor.point3(10);
        return value;
    }
    if (name == "ACCONSTRAINEDBOUNDEDLINE") {
        AssocConstraintBoundedLine value;
        value.geometry_dependency = cursor.handle(330);
        value.geometry_node_id = cursor.i32(90);
        value.point = cursor.point3(10);
        value.direction = cursor.point3(10);
        value.is_ray = cursor.boolean(290);
        value.start_point = cursor.point3(10);
        value.end_point = cursor.point3(11);
        return value;
    }
    if (name == "ACANGLECONSTRAINT" || name == "AC3POINTANGLECONSTRAINT") {
        AssocConstraintAngle value;
        std::tie(value.owner_id, value.is_implied, value.is_active, value.value_dependency,
                 value.dimension_dependency) = explicit_constraint();
        value.sector_type = static_cast<std::uint8_t>(cursor.i32(280));
        return value;
    }
    if (name == "ACPARALLELCONSTRAINT" || name == "ACHORIZONTALCONSTRAINT" ||
        name == "ACVERTICALCONSTRAINT") {
        AssocConstraintParallel value;
        std::tie(value.owner_id, value.is_implied, value.is_active) = geometry();
        if (name != "ACPARALLELCONSTRAINT")
            value.datum_line_index = cursor.i32(90);
        return value;
    }
    if (name == "ACDISTANCECONSTRAINT") {
        AssocConstraintDistance value;
        std::tie(value.owner_id, value.is_implied, value.is_active, value.value_dependency,
                 value.dimension_dependency) = explicit_constraint();
        value.direction_type = static_cast<std::uint8_t>(cursor.i32(280));
        if (value.direction_type)
            value.distance = cursor.point3(10);
        return value;
    }
    if (name == "ACRADIUSDIAMETERCONSTRAINT") {
        AssocConstraintRadiusDiameter value;
        std::tie(value.owner_id, value.is_implied, value.is_active, value.value_dependency,
                 value.dimension_dependency) = explicit_constraint();
        value.mode = static_cast<std::uint8_t>(cursor.i32(280));
        return value;
    }
    if (name == "ACCONSTRAINEDELLIPSE") {
        AssocConstraintEllipse value;
        value.geometry_dependency = cursor.handle(330);
        value.geometry_node_id = cursor.i32(90);
        value.center = cursor.point3(10);
        value.major_axis = cursor.point3(11);
        value.axis_ratio = cursor.f64(40);
        return value;
    }
    if (name == "ACCONSTRAINEDBOUNDEDELLIPSE") {
        AssocConstraintBoundedEllipse value;
        value.geometry_dependency = cursor.handle(330);
        value.geometry_node_id = cursor.i32(90);
        value.center = cursor.point3(10);
        value.major_axis = cursor.point3(11);
        value.axis_ratio = cursor.f64(40);
        value.start_point = cursor.point3(10);
        value.end_point = cursor.point3(11);
        return value;
    }
    if (name == "ACCONSTRAINEDSPLINE") {
        AssocConstraintSpline value;
        value.geometry_dependency = cursor.handle(330);
        value.geometry_node_id = cursor.i32(90);
        value.rational = cursor.boolean(70);
        value.periodic = cursor.boolean(70);
        value.degree = cursor.i32(90);
        value.knot_tolerance = cursor.f64(40);
        auto knot_count = capped(cursor.i32(90));
        value.knot_physical_length = cursor.i32(90);
        value.knot_grow_length = cursor.i32(90);
        for (int i = 0; i < knot_count; ++i)
            value.knots.push_back(cursor.f64(40));
        auto weight_count = capped(cursor.i32(90));
        value.weight_physical_length = cursor.i32(90);
        value.weight_grow_length = cursor.i32(90);
        for (int i = 0; i < weight_count; ++i)
            value.weights.push_back(cursor.f64(40));
        auto control_count = capped(cursor.i32(90));
        value.control_point_physical_length = cursor.i32(90);
        value.control_point_grow_length = cursor.i32(90);
        for (int i = 0; i < control_count; ++i)
            value.control_points.push_back(cursor.point3(10));
        auto implicit_count = capped(cursor.i32(90));
        for (int i = 0; i < implicit_count; ++i)
            value.implicit_point_ids.push_back(cursor.i32(90));
        return value;
    }
    if (is_plain_geometrical_constraint(class_name)) {
        AssocConstraintGeometrical value;
        std::tie(value.owner_id, value.is_implied, value.is_active) = geometry();
        return value;
    }
    return std::monostate{};
}
std::vector<AssocCurveValue> read_edge_curve(const AssocDxfRecord &record) {
    auto found = record.sections.find("AcDbAssocEdgeActionParam");
    if (found == record.sections.end())
        return {};
    const auto &pairs = found->second;
    int seen = 0;
    std::size_t start = pairs.size();
    for (std::size_t i = 0; i < pairs.size(); ++i)
        if (pairs[i].code == 90 && ++seen == 2) {
            start = i + 1;
            break;
        }
    std::vector<AssocCurveValue> curve;
    for (std::size_t i = start; i < pairs.size(); ++i) {
        const auto &pair = pairs[i];
        if (pair.code == 70)
            curve.emplace_back(real_of(pair.value) != 0);
        else if (pair.code == 90)
            curve.emplace_back(integer_of(pair.value));
        else if (pair.code == 40)
            curve.emplace_back(real_of(pair.value));
        else if (pair.code == 10) {
            auto coord = [&](std::size_t offset, int expected) -> std::optional<double> {
                if (i + offset < pairs.size() && pairs[i + offset].code == expected)
                    return real_of(pairs[i + offset].value);
                return std::nullopt;
            };
            auto y = coord(1, 20);
            auto z = coord(2, 30);
            curve.emplace_back(Vector3{real_of(pair.value), y.value_or(0), z.value_or(0)});
            i += std::size_t(y.has_value()) + std::size_t(z.has_value());
        }
    }
    return curve;
}
AssocDimensionAssociation read_dimension_association(const AssocDxfRecord &record) {
    AssocDimensionAssociation value;
    value.dimension = record.handle_at("AcDbDimAssoc", 330, 0);
    AssocCursor cursor(record, "AcDbDimAssoc", nullptr);
    value.associativity = cursor.i32(90);
    value.trans_space = cursor.boolean(70);
    value.rotated_type = static_cast<std::uint8_t>(cursor.i32(71));
    std::size_t total = 0;
    bool stop = false;
    for (int slot = 0; slot < 4 && !stop; ++slot) {
        if ((value.associativity & (1 << slot)) == 0)
            continue;
        while (true) {
            if (total >= 6 || cursor.peek_code() != 1) {
                stop = true;
                break;
            }
            AssocDimensionReference reference;
            reference.class_name = cursor.text(1);
            reference.osnap_type = static_cast<std::uint8_t>(cursor.i32(72));
            reference.xrefs = cursor.consecutive_handles(331);
            if (AssocDimensionReference::stores_main_subentity(reference.osnap_type, reference.xrefs.size())) {
                reference.main_subent_type = cursor.i32(73);
                reference.main_gs_marker = cursor.i32(91);
                reference.xref_paths = cursor.consecutive_text(301);
            }
            reference.osnap_distance = cursor.f64(40);
            reference.osnap_point = {cursor.f64(10), cursor.f64(20), cursor.f64(30)};
            if (reference.osnap_type == 6 || reference.osnap_type == 11) {
                reference.intersection_objects = cursor.consecutive_handles(332);
                reference.intersection_subent_type = cursor.i32(74);
                reference.intersection_gs_marker = cursor.i32(92);
                reference.intersection_xref_paths = cursor.consecutive_text(302);
            }
            reference.has_last_point_reference = cursor.boolean(75);
            bool more = reference.has_last_point_reference;
            value.references[static_cast<std::size_t>(slot)].push_back(std::move(reference));
            ++total;
            if (!more)
                break;
        }
    }
    return value;
}
PersSubentManager read_static_pers_subent(const AssocDxfRecord &record) {
    AssocCursor cursor(record, "AcDbPersSubentManager", nullptr);
    PersSubentManager value;
    value.class_version = cursor.i32(90);
    value.marker_zero = cursor.i32(90);
    value.marker_two = cursor.i32(90);
    value.associative_step_count = cursor.i32(90);
    value.associative_subent_count = cursor.i32(90);
    while (cursor.peek_code() == 90)
        value.values.push_back(cursor.i32(90));
    return value;
}
AssociativeData parse_associative_data(AssocDxfRecord &record, const std::string &canonical,
                                       DxfVersion version) {
    if (canonical == "ASSOCDEPENDENCY")
        return read_dependency(record);
    if (canonical == "ASSOCVALUEDEPENDENCY") {
        AssocCursor cursor(record, "AcDbAssocValueDependency", &record.ambiguous_value_params);
        AssocValueDependency value;
        value.dependency = read_dependency(record);
        value.class_version = cursor.i32(90);
        value.name = cursor.text(1);
        value.value = read_eval(cursor, std::nullopt);
        return value;
    }
    if (canonical == "ASSOCGEOMDEPENDENCY") {
        auto section = record.sections.count("AcDbAssocPersSubentId") ? "AcDbAssocPersSubentId"
                                                                      : "AcDbAssocGeomDependency";
        const auto &entries = record.sections[section];
        std::size_t start = entries.size();
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].code == 1) {
                start = i;
                break;
            }
        AssocGeomDependency value;
        value.dependency = read_dependency(record);
        value.class_version = record.i16("AcDbAssocGeomDependency", 90, 0);
        value.enabled = record.boolean("AcDbAssocGeomDependency", 290, 0);
        if (start < entries.size())
            value.persistent_subent.class_name = entries[start].value;
        for (std::size_t i = start + 1; i < entries.size() && entries[i].code == 90; ++i)
            value.persistent_subent.values.push_back(integer_of(entries[i].value));
        for (std::size_t i = start + 1; i < entries.size(); ++i)
            if (entries[i].code == 290) {
                value.persistent_subent.dependent_on_compound_object = trimmed(entries[i].value) != "0";
                break;
            }
        if (value.persistent_subent.class_name == "AcDbAssocSingleEdgePersSubentId")
            value.persistent_subent.class_code = 1;
        else if (value.persistent_subent.class_name == "AcDbAssocEdgePersSubentId")
            value.persistent_subent.class_code = 3;
        else if (value.persistent_subent.class_name == "AcDbAssocAsmBasedEntityPersSubentId")
            value.persistent_subent.class_code = 5;
        return value;
    }
    if (canonical == "ASSOCACTION")
        return read_action(record);
    if (canonical == "ASSOCNETWORK") {
        AssocCursor cursor(record, "AcDbAssocNetwork", &record.ambiguous_value_params);
        AssocNetwork value;
        value.action = read_action(record);
        value.network_version = cursor.i16(90);
        value.network_action_index = cursor.i32(90);
        auto count = capped(cursor.i32(90));
        for (int i = 0; i < count; ++i) {
            AssocActionDependency dependency;
            while (cursor.position < cursor.size) {
                const auto &pair = cursor.entries[cursor.position++];
                if (pair.code == 330 || pair.code == 360) {
                    dependency.dependency = handle_of(pair.value);
                    dependency.is_owned = pair.code == 360;
                    break;
                }
            }
            value.actions.push_back(dependency);
        }
        count = capped(cursor.i32(90));
        for (int i = 0; i < count; ++i)
            value.owned_actions.push_back(cursor.handle(330));
        return value;
    }
    if (auto kind = surface_kind(canonical))
        return read_surface(record, *kind);
    if (canonical == "ASSOCRESTOREENTITYSTATEACTIONBODY") {
        AssocAnnotationActionBody value;
        value.kind = AssocAnnotationKind::RestoreEntityState;
        value.action_body.version = record.i32("AcDbAssocActionBody", 90, 0);
        value.class_version = record.i32("AcDbAssocRestoreEntityStateActionBody", 90, 0);
        value.entity = record.handle_at("AcDbAssocRestoreEntityStateActionBody", 330, 0);
        return value;
    }
    if (canonical == "ASSOCMLEADERACTIONBODY") {
        auto count = capped(record.i32("AcDbAssocMLeaderActionBody", 90, 1));
        auto ids = record.values("AcDbAssocMLeaderActionBody", 90);
        auto handles = record.values("AcDbAssocMLeaderActionBody", 330);
        AssocAnnotationActionBody value;
        value.kind = AssocAnnotationKind::MLeader;
        value.annotation.action_body.version = record.i32("AcDbAssocActionBody", 90, 0);
        value.annotation.version = record.i16("AcDbAssocActionBody", 90, 1);
        value.annotation.dependency = record.handle_at("AcDbAssocActionBody", 330, 0);
        value.class_version = record.i32("AcDbAssocMLeaderActionBody", 90, 0);
        for (int i = 0; i < count; ++i) {
            AssocAnnotationDependency dependency;
            auto index = static_cast<std::size_t>(i) + 2;
            dependency.dependency_id = index < ids.size() ? integer_of(ids[index]) : 0;
            dependency.dependency =
                static_cast<std::size_t>(i) < handles.size() ? handle_of(handles[static_cast<std::size_t>(i)]) : 0;
            value.actions.push_back(dependency);
        }
        return value;
    }
    if (canonical == "ASSOCALIGNEDDIMACTIONBODY" || canonical == "ASSOC3POINTANGULARDIMACTIONBODY" ||
        canonical == "ASSOCORDINATEDIMACTIONBODY" || canonical == "ASSOCROTATEDDIMACTIONBODY") {
        AssocAnnotationActionBody value;
        const char *section = "AssocRotatedDimActionBody";
        if (canonical == "ASSOCALIGNEDDIMACTIONBODY") {
            value.kind = AssocAnnotationKind::AlignedDimension;
            section = "ACDBASSOCALIGNEDDIMACTIONBODY";
        } else if (canonical == "ASSOC3POINTANGULARDIMACTIONBODY") {
            value.kind = AssocAnnotationKind::ThreePointAngularDimension;
            section = "Assoc3PointAngularDimActionBody";
        } else if (canonical == "ASSOCORDINATEDIMACTIONBODY") {
            value.kind = AssocAnnotationKind::OrdinateDimension;
            section = "AssocOrdinatedDimActionBody";
        } else
            value.kind = AssocAnnotationKind::RotatedDimension;
        value.annotation.action_body.version = record.i32("AcDbAssocActionBody", 90, 0);
        value.annotation.version = record.i16("AcDbAssocActionBody", 90, 1);
        value.annotation.dependency = record.handle_at("AcDbAssocActionBody", 330, 0);
        value.class_version = record.i32(section, 90, 0);
        value.read_node = record.handle_at(section, 330, 0);
        value.dimension_node = record.handle_at(section, 330, 1);
        value.dependency = record.handle_at(section, 330, 2);
        return value;
    }
    if (canonical == "ASSOCPERSSUBENTMANAGER") {
        auto values = record.values("AcDbAssocPersSubentManager", 90);
        AssocPersSubentManager value;
        if (!values.empty())
            value.class_version = integer_of(values[0]);
        for (std::size_t i = 1; i < 4 && i < values.size(); ++i)
            value.markers[i - 1] = integer_of(values[i]);
        for (std::size_t i = 4; i < values.size(); ++i)
            value.values.push_back(integer_of(values[i]));
        value.final_flag = record.boolean("AcDbAssocPersSubentManager", 290, 0);
        return value;
    }
    if (canonical == "ASSOCEDGEACTIONPARAM") {
        AssocEdgeActionParam value;
        value.single_dependency = read_single(record, "AcDbAssocEdgeActionParam");
        value.parameter = record.handle_at("AcDbAssocEdgeActionParam", 330, 0);
        value.has_action = record.boolean("AcDbAssocEdgeActionParam", 290, 0);
        value.action_type = record.i32("AcDbAssocEdgeActionParam", 90, 1);
        switch (value.action_type) {
        case 11:
            value.subcurve_kind = AssocSubcurveKind::Arc;
            break;
        case 17:
            value.subcurve_kind = AssocSubcurveKind::Ellipse;
            break;
        case 19:
            value.subcurve_kind = AssocSubcurveKind::Line;
            break;
        case 23:
            value.subcurve_kind = AssocSubcurveKind::LineSegment3d;
            break;
        case 42:
            value.subcurve_kind = AssocSubcurveKind::Nurb3d;
            break;
        case 27:
            value.subcurve_kind = AssocSubcurveKind::Curve3d;
            break;
        default:
            value.subcurve_kind = AssocSubcurveKind::None;
            break;
        }
        value.curve = read_edge_curve(record);
        return value;
    }
    if (canonical == "ASSOC2DCONSTRAINTGROUP") {
        bool has_group = record.sections.count("AcDbAssoc2dConstraintGroup") != 0;
        AssocCursor cursor(record, has_group ? "AcDbAssoc2dConstraintGroup" : "AcDbAssocAction",
                           &record.ambiguous_value_params);
        if (!has_group)
            skip_action(cursor);
        Assoc2dConstraintGroup value;
        value.action = read_action(record);
        value.version = cursor.i32(90);
        value.flag = cursor.boolean(70);
        for (auto &point : value.work_plane) {
            point.x = cursor.f64(10);
            point.y = cursor.f64(20);
            point.z = cursor.f64(30);
        }
        value.dependency = cursor.handle(360);
        auto count = capped(cursor.i32(90));
        for (int i = 0; i < count; ++i)
            value.actions.push_back(cursor.handle(360));
        value.next_node_id = cursor.i32(90);
        bool inline_nodes = version < DxfVersion::AC1027 && cursor.peek_code() == 90 &&
                            (cursor.position + 1 >= cursor.size || cursor.entries[cursor.position + 1].code == 1);
        if (inline_nodes) {
            count = capped(cursor.i32(90));
            if (count > 0)
                value.nodes.push_back({});
            for (int i = 0; i < count; ++i) {
                auto class_name = cursor.text(1);
                if (cursor.peek_code() == 330)
                    cursor.handle(330);
                std::size_t end = cursor.size;
                for (std::size_t n = cursor.position; n < cursor.size; ++n)
                    if (cursor.entries[n].code == 1) {
                        end = n;
                        break;
                    }
                auto node_cursor = cursor.slice(cursor.position, end);
                auto node = read_constraint_common(node_cursor, version);
                node.data = read_constraint_data(node_cursor, class_name, false);
                if (std::holds_alternative<std::monostate>(node.data))
                    throw std::invalid_argument("Unsupported DXF constraint node class " + class_name);
                node.class_name = class_name;
                value.nodes.push_back(std::move(node));
                cursor.position = end;
            }
        } else if (value.next_node_id > 0) {
            value.nodes.push_back(read_constraint_root(cursor));
            auto after_root = cursor.position;
            auto registered_count = capped(cursor.i32(90));
            bool has_registry = registered_count == 0 || cursor.peek_code() == 1;
            if (has_registry) {
                std::vector<std::pair<std::string, std::int32_t>> registry;
                for (int i = 0; i < registered_count; ++i)
                    registry.push_back({cursor.text(1), cursor.i32(90)});
                for (auto &[class_name, registry_node_id] : registry) {
                    auto node = read_constraint_common(cursor, version);
                    if (node.node_id == 0)
                        node.node_id = registry_node_id;
                    node.class_name = class_name;
                    node.data = read_constraint_data(cursor, node.class_name, true);
                    if (std::holds_alternative<std::monostate>(node.data))
                        throw std::invalid_argument("Unsupported DXF constraint node class " + node.class_name);
                    value.nodes.push_back(std::move(node));
                }
            } else {
                cursor.position = after_root;
                auto remaining = capped(value.next_node_id);
                for (int i = 1; i < remaining; ++i)
                    value.nodes.push_back(read_constraint_common(cursor, version));
            }
        }
        return value;
    }
    if (canonical == "ASSOCVARIABLE") {
        AssocCursor cursor(record, "AcDbAssocVariable", &record.ambiguous_value_params);
        AssocVariable value;
        value.action = read_action(record);
        value.class_version = cursor.i32(90);
        value.name = cursor.text(1);
        value.expression = cursor.text(1);
        value.evaluator = cursor.text(1);
        value.description = cursor.text(1);
        if (cursor.peek_code() != 290)
            value.value = read_eval(cursor, std::nullopt);
        value.has_cached_value = cursor.boolean(290);
        if (value.has_cached_value)
            value.cached_value = cursor.text(1);
        value.flag = cursor.boolean(290);
        auto count = capped(cursor.i32(90));
        for (int i = 0; i < count; ++i)
            value.dependencies.push_back({cursor.handle(360), cursor.i32(90)});
        return value;
    }
    if (canonical == "ASSOCACTIONPARAM")
        return read_action_param(record);
    if (canonical == "ASSOCCOMPOUNDACTIONPARAM")
        return read_compound(record);
    if (canonical == "ASSOCOSNAPPOINTREFACTIONPARAM") {
        AssocOsnapPointRefActionParam value;
        value.compound = read_compound(record);
        value.status = record.i16("ACDBASSOCOSNAPPOINTREFACTIONPARAM", 90, 0);
        value.osnap_mode = record.byte("ACDBASSOCOSNAPPOINTREFACTIONPARAM", 90, 1);
        value.parameter = record.f64("ACDBASSOCOSNAPPOINTREFACTIONPARAM", 40, 0);
        return value;
    }
    if (canonical == "ASSOCPOINTREFACTIONPARAM")
        return AssocPointRefActionParam{read_compound(record)};
    if (canonical == "ASSOCOBJECTACTIONPARAM" || canonical == "ASSOCFACEACTIONPARAM" ||
        canonical == "ASSOCVERTEXACTIONPARAM" || canonical == "ASSOCASMBODYACTIONPARAM") {
        const char *section = "AcDbAssocObjectActionParam";
        if (canonical == "ASSOCFACEACTIONPARAM")
            section = "AcDbAssocFaceActionParam";
        else if (canonical == "ASSOCVERTEXACTIONPARAM")
            section = "AcDbAssocVertexActionParam";
        else if (canonical == "ASSOCASMBODYACTIONPARAM")
            section = "AcDbAssocAsmbodyActionParam";
        auto single = read_single(record, section);
        if (canonical == "ASSOCFACEACTIONPARAM")
            return AssocFaceActionParam{single, record.i32(section, 90, 1)};
        if (canonical == "ASSOCVERTEXACTIONPARAM")
            return AssocVertexActionParam{single, record.point(section, 10, 0)};
        if (canonical == "ASSOCASMBODYACTIONPARAM") {
            AssocAsmBodyActionParam value;
            value.single_dependency = single;
            auto sat = record.values("AcDbModelerGeometry", 1);
            auto extra = record.values("AcDbModelerGeometry", 3);
            sat.insert(sat.end(), extra.begin(), extra.end());
            std::ostringstream joined;
            for (std::size_t i = 0; i < sat.size(); ++i) {
                if (i)
                    joined << '\n';
                joined << sat[i];
            }
            value.sat_data = joined.str();
            value.sat_encoded = record.i16("AcDbModelerGeometry", 70, 0) == 1;
            return value;
        }
        return single;
    }
    if (canonical == "ASSOCPATHACTIONPARAM")
        return AssocPathActionParam{read_compound(record), record.i32("AcDbAssocPathActionParam", 90, 0)};
    if (canonical == "ASSOCDIMDEPENDENCYBODY")
        return AssocDimDependencyBody{record.i16("AcDbAssocDependencyBody", 90, 0),
                                      record.i16("AcDbImpAssocDimDependencyBodyBase", 90, 0),
                                      record.text("AcDbImpAssocDimDependencyBodyBase", 1, 0),
                                      record.i16("AcDbAssocDimDependencyBody", 90, 0)};
    if (canonical == "ASSOCARRAYMODIFYPARAMETERS" || canonical == "ASSOCARRAYPATHPARAMETERS" ||
        canonical == "ASSOCARRAYPOLARPARAMETERS" || canonical == "ASSOCARRAYRECTANGULARPARAMETERS")
        return read_array_parameters(record);
    if (canonical == "ASSOCARRAYACTIONBODY" || canonical == "ASSOCARRAYMODIFYACTIONBODY") {
        AssocCursor cursor(record, "AcDbAssocArrayActionBody", nullptr);
        AssocArrayActionBody body;
        body.action_body.version = record.i32("AcDbAssocActionBody", 90, 0);
        body.parameter_body = read_parameter_body(record);
        body.version = cursor.i32(90);
        body.parameter_block = cursor.text(1);
        bool has_items = cursor.peek_code() == 90;
        if (has_items) {
            body.item_list_version = cursor.i32(90);
            auto count = capped(cursor.i32(90));
            body.item_class = cursor.text(1);
            AssocCursor item_cursor(record, "AcDbAssocArrayItem", nullptr);
            for (int i = 0; i < count; ++i)
                body.items.push_back(read_array_item(item_cursor));
            auto &matrix_cursor = count > 0 ? item_cursor : cursor;
            for (auto &item : body.transform)
                item = matrix_cursor.f64(40);
        }
        if (canonical == "ASSOCARRAYMODIFYACTIONBODY") {
            AssocCursor modify(record, "AcDbAssocArrayModifyActionBody", nullptr);
            AssocArrayModifyActionBody value;
            value.body = std::move(body);
            value.status = modify.i16(70);
            auto count = capped(modify.i32(90));
            for (int i = 0; i < count; ++i)
                value.item_locations.push_back({modify.i32(90), modify.i32(90), modify.i32(90)});
            return value;
        }
        return body;
    }
    if (canonical == "ACDBCENTERMARKACTIONBODY" || canonical == "ACDBCENTERLINEACTIONBODY") {
        AssocSmartCenterActionBody value;
        value.action_body.version = record.i32("AcDbAssocActionBody", 90, 0);
        value.parameter_body = read_parameter_body(record);
        value.version = record.i32("AcDbSmartCenterActionBody", 90, 0);
        return value;
    }
    if (canonical == "ASSOCVIEWREPACTIONBODY") {
        AssocViewRepActionBody value;
        value.action_body.version = record.i32("AcDbAssocActionBody", 90, 0);
        value.class_version = record.i16("AcDbAssocViewRepActionBody", 70, 0);
        value.view_rep = record.handle_at("AcDbAssocViewRepActionBody", 360, 0);
        value.view_type = record.i32("AcDbAssocViewRepActionBody", 90, 0);
        value.rotation = record.f64("AcDbAssocViewRepActionBody", 40, 0);
        return value;
    }
    if (canonical == "ASSOCVIEWBORDERACTIONPARAM" || canonical == "ASSOCVIEWREPACTIONPARAM" ||
        canonical == "ASSOCVIEWSYMBOLACTIONPARAM" || canonical == "ASSOCVIEWSTYLEACTIONPARAM") {
        AssocViewObjectActionParam value;
        const char *section = "AcDbAssocViewBorderActionParam";
        if (canonical == "ASSOCVIEWREPACTIONPARAM") {
            value.kind = AssocViewObjectActionParamKind::ViewRep;
            section = "AcDbAssocViewRepActionParam";
        } else if (canonical == "ASSOCVIEWSYMBOLACTIONPARAM") {
            value.kind = AssocViewObjectActionParamKind::ViewSymbol;
            section = "AcDbAssocViewSymbolActionParam";
        } else if (canonical == "ASSOCVIEWSTYLEACTIONPARAM") {
            value.kind = AssocViewObjectActionParamKind::ViewStyle;
            section = "AcDbAssocViewStyleActionParam";
        }
        value.single_dependency = read_single(record, "AcDbAssocObjectActionParam");
        value.class_version = record.i16(section, 70, 0);
        return value;
    }
    if (canonical == "ASSOCVIEWREPHATCHMANAGER") {
        AssocCursor cursor(record, "AcDbAssocViewRepHatchManager", nullptr);
        AssocViewRepHatchManager value;
        value.compound = read_compound(record);
        value.class_version = cursor.i16(70);
        auto count = capped(cursor.i32(90));
        for (int i = 0; i < count; ++i)
            value.items.push_back(
                {cursor.i64(160), cursor.i64(160), cursor.i32(90), cursor.handle(330)});
        return value;
    }
    if (canonical == "ASSOCVIEWREPHATCHACTIONPARAM") {
        AssocViewRepHatchActionParam value;
        value.single_dependency = read_single(record, "AcDbAssocObjectActionParam");
        value.class_version = record.i16("AcDbAssocViewRepHatchActionParam", 70, 0);
        value.normal = record.point("AcDbAssocViewRepHatchActionParam", 210, 0);
        value.hatch_index = record.i32("AcDbAssocViewRepHatchActionParam", 90, 0);
        value.flags = record.i32("AcDbAssocViewRepHatchActionParam", 90, 1);
        return value;
    }
    if (canonical == "ASSOCVIEWLABELACTIONPARAM") {
        AssocViewLabelActionParam value;
        value.single_dependency = read_single(record, "AcDbAssocObjectActionParam");
        value.class_version = record.i16("AcDbAssocViewLabelActionParam", 70, 0);
        value.label_version = record.i16("AcDbAssocViewLabelActionParam", 70, 1);
        value.offset = {record.f64("AcDbAssocViewLabelActionParam", 210, 0),
                        record.f64("AcDbAssocViewLabelActionParam", 220, 0)};
        value.flag = record.byte("AcDbAssocViewLabelActionParam", 280, 0);
        return value;
    }
    if (canonical == "DIMASSOC")
        return read_dimension_association(record);
    if (canonical == "PERSUBENTMGR")
        return read_static_pers_subent(record);
    return std::monostate{};
}
AssociativeObject parse_associative_object(AssocDxfRecord &collected, const std::string &dxf_name,
                                           DxfVersion version) {
    AssociativeObject object;
    object.handle = collected.handle;
    object.owner = collected.owner;
    object.reactors = collected.reactors;
    object.xdictionary_handle = collected.xdictionary_handle;
    object.dxf_name = dxf_name;
    object.cpp_class_name = associative_cpp_class_name(dxf_name).value_or("AcDbObject");
    object.data = parse_associative_data(collected, associative_canonical_name(dxf_name), version);
    object.source_version = version;
    return object;
}
void write_eval(AssocWriter &writer, const AssocEvalVariant &value) {
    auto code = [&](int fallback) { return value.code == 0 ? fallback : static_cast<int>(value.code); };
    std::visit(
        [&](const auto &item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, double>)
                writer.f64(code(40), item);
            else if constexpr (std::is_same_v<T, std::int32_t>)
                writer.i32(code(90), item);
            else if constexpr (std::is_same_v<T, std::int16_t>)
                writer.i16(code(70), item);
            else if constexpr (std::is_same_v<T, std::uint8_t>)
                writer.u8(code(280), item);
            else if constexpr (std::is_same_v<T, std::string>)
                writer.text(code(1), item);
            else if constexpr (std::is_same_v<T, std::uint64_t>)
                writer.handle(code(330), item);
        },
        value.value);
}
void write_value_param(AssocWriter &writer, const AssocValueParam &value) {
    writer.i32(90, value.class_version);
    writer.text(1, value.name);
    writer.i32(90, value.unit_type);
    writer.i32(90, static_cast<std::int32_t>(value.variables.size()));
    for (const auto &variable : value.variables) {
        write_eval(writer, variable.value);
        writer.handle(330, variable.handle);
    }
    writer.handle(330, value.controlled_object_dependency);
}
void write_dependency(AssocWriter &writer, const AssocDependency &value) {
    writer.subclass("AcDbAssocDependency");
    writer.i16(90, value.class_version);
    writer.i32(90, value.status);
    writer.boolean(290, value.is_read_dependency);
    writer.boolean(290, value.is_write_dependency);
    writer.boolean(290, value.is_attached_to_object);
    writer.boolean(290, value.is_delegating_to_owning_action);
    writer.i32(90, value.order);
    writer.handle(330, value.dependent_on);
    writer.boolean(290, value.name.has_value());
    if (value.name)
        writer.text(1, *value.name);
    writer.handle(330, value.read_dependency);
    writer.handle(330, value.node);
    writer.handle(360, value.dependency_body);
    writer.i32(90, value.dependency_body_id);
}
void write_action(AssocWriter &writer, const AssocAction &value) {
    writer.subclass("AcDbAssocAction");
    writer.i16(90, value.class_version);
    writer.i32(90, value.geometry_status);
    writer.handle(330, value.owning_network);
    writer.handle(360, value.action_body);
    writer.i32(90, value.action_index);
    writer.i32(90, value.max_dependency_index);
    writer.i32(90, static_cast<std::int32_t>(value.dependencies.size()));
    for (const auto &dependency : value.dependencies)
        writer.handle(dependency.is_owned ? 360 : 330, dependency.dependency);
    if (value.class_version > 1) {
        writer.i16(90, 0);
        writer.i32(90, static_cast<std::int32_t>(value.owned_parameters.size()));
        for (auto handle : value.owned_parameters)
            writer.handle(360, handle);
        writer.i16(90, 0);
        writer.i32(90, static_cast<std::int32_t>(value.values.size()));
        for (const auto &item : value.values)
            write_value_param(writer, item);
    }
}
void write_action_param(AssocWriter &writer, const AssocActionParam &value, DxfVersion version) {
    writer.subclass("AcDbAssocActionParam");
    writer.i16(90, value.is_r2013);
    if (version >= DxfVersion::AC1027)
        writer.i32(90, value.version);
    writer.text(1, value.name);
}
void write_action_body(AssocWriter &writer, const AssocActionBody &value) {
    writer.subclass("AcDbAssocActionBody");
    writer.i32(90, value.version);
}
void write_parameter_body(AssocWriter &writer, const AssocParamBasedActionBody &value) {
    if (value.version == 0 && value.minor == 0 && value.dependencies.empty() && value.values.empty() &&
        value.dependency == 0)
        return;
    writer.subclass("AcDbAssocParamBasedActionBody");
    writer.i32(90, value.version);
    writer.i32(90, value.minor);
    writer.i32(90, static_cast<std::int32_t>(value.dependencies.size()));
    for (auto handle : value.dependencies)
        writer.handle(360, handle);
    writer.i32(90, value.marker);
    writer.i32(90, static_cast<std::int32_t>(value.values.size()));
    if (value.values.empty()) {
        writer.i32(90, value.empty_value_marker);
        writer.handle(330, value.dependency);
    }
    for (const auto &item : value.values)
        write_value_param(writer, item);
}
void write_single(AssocWriter &writer, const AssocSingleDependencyActionParam &value, const char *leaf,
                  DxfVersion version) {
    write_action_param(writer, value.action_param, version);
    writer.subclass("AcDbAssocSingleDependencyActionParam");
    writer.i32(90, value.dependency_class_version);
    writer.handle(330, value.dependency);
    writer.subclass(leaf);
    writer.i32(90, value.class_version);
}
void write_compound(AssocWriter &writer, const AssocCompoundActionParam &value, DxfVersion version) {
    write_action_param(writer, value.action_param, version);
    writer.subclass("AcDbAssocCompoundActionParam");
    writer.i16(90, value.class_version);
    writer.i16(90, value.status);
    writer.i32(90, static_cast<std::int32_t>(value.parameters.size()));
    for (auto handle : value.parameters)
        writer.handle(360, handle);
    if (value.child_parameter) {
        writer.i16(90, value.child_parameter->status);
        writer.i32(90, value.child_parameter->id);
        writer.handle(330, value.child_parameter->parameter);
        if (value.child_parameter->id != 0) {
            writer.handle(330, value.child_parameter->secondary_parameter);
            writer.i32(90, value.child_parameter->marker);
            writer.handle(330, value.child_parameter->tertiary_parameter);
        }
    }
}
void write_array_item(AssocWriter &writer, const AssocArrayItem &item) {
    writer.i32(90, item.class_version);
    for (auto location : item.location)
        writer.i32(90, location);
    writer.i32(90, item.flags);
    if (item.uses_default_transform)
        writer.point3(11, item.x_direction);
    else
        for (auto value : item.transform)
            writer.f64(40, value);
    if (item.relative_transform)
        for (auto value : *item.relative_transform)
            writer.f64(40, value);
    if (item.first_handle)
        writer.handle(330, *item.first_handle);
    if (item.flags & 0x10)
        writer.handle(330, item.second_handle.value_or(0));
}
void write_constraint_common(AssocWriter &writer, const AssocConstraintNode &node, DxfVersion version) {
    writer.i32(90, node.node_id);
    if (version < DxfVersion::AC1027)
        writer.u8(70, node.status);
    writer.i32(90, static_cast<std::int32_t>(node.connections.size()));
    for (auto connection : node.connections)
        writer.i32(90, connection);
    if (version >= DxfVersion::AC1027)
        writer.u8(70, node.status);
}
void write_constraint_data(AssocWriter &writer, const AssocConstraintNodeData &data, DxfVersion version) {
    std::visit(
        [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AssocConstraintGeometrical>) {
                writer.i32(90, value.owner_id);
                writer.boolean(290, value.is_implied);
                writer.boolean(290, value.is_active);
            } else if constexpr (std::is_same_v<T, AssocConstraintComposite>) {
                writer.i32(90, value.owner_id);
                writer.boolean(290, value.is_implied);
                writer.boolean(290, value.is_active);
                writer.i32(90, static_cast<std::int32_t>(value.owned_constraint_ids.size()));
                for (auto id : value.owned_constraint_ids)
                    writer.i32(90, id);
            } else if constexpr (std::is_same_v<T, AssocConstraintHelpParameter>) {
                writer.f64(40, value.value);
                writer.boolean(290, value.reserved);
            } else if constexpr (std::is_same_v<T, AssocConstraintAngle>) {
                writer.i32(90, value.owner_id);
                writer.boolean(290, value.is_implied);
                writer.boolean(290, value.is_active);
                writer.handle(340, value.value_dependency);
                writer.handle(340, value.dimension_dependency);
                writer.u8(280, value.sector_type);
            } else if constexpr (std::is_same_v<T, AssocConstraintParallel>) {
                writer.i32(90, value.owner_id);
                writer.boolean(290, value.is_implied);
                writer.boolean(290, value.is_active);
                if (value.datum_line_index)
                    writer.i32(90, *value.datum_line_index);
            } else if constexpr (std::is_same_v<T, AssocConstraintDistance>) {
                writer.i32(90, value.owner_id);
                writer.boolean(290, value.is_implied);
                writer.boolean(290, value.is_active);
                writer.handle(340, value.value_dependency);
                writer.handle(340, value.dimension_dependency);
                writer.u8(280, value.direction_type);
                if (value.distance)
                    writer.point3(10, *value.distance);
            } else if constexpr (std::is_same_v<T, AssocConstraintRadiusDiameter>) {
                writer.i32(90, value.owner_id);
                writer.boolean(290, value.is_implied);
                writer.boolean(290, value.is_active);
                writer.handle(340, value.value_dependency);
                writer.handle(340, value.dimension_dependency);
                writer.u8(280, value.mode);
            } else if constexpr (std::is_same_v<T, AssocConstraintImplicitPoint>) {
                writer.handle(330, value.geometry_dependency);
                writer.i32(90, value.geometry_node_id);
                if (value.point)
                    writer.point3(10, *value.point);
                writer.u8(280, value.point_type);
                writer.i32(90, value.point_index);
                writer.i32(90, value.curve_id);
            } else if constexpr (std::is_same_v<T, AssocConstraintPoint>) {
                writer.handle(330, value.geometry_dependency);
                writer.i32(90, value.geometry_node_id);
                if (value.point)
                    writer.point3(10, *value.point);
            } else if constexpr (std::is_same_v<T, AssocConstraintRigidSet>) {
                writer.handle(330, value.geometry_dependency);
                writer.i32(90, value.geometry_node_id);
                writer.boolean(290, value.reserved);
                for (auto item : value.transform)
                    writer.f64(40, item);
                writer.i32(90, static_cast<std::int32_t>(value.geometry_ids.size()));
                for (auto id : value.geometry_ids)
                    writer.i32(90, id);
            } else if constexpr (std::is_same_v<T, AssocConstraintLine>) {
                writer.handle(330, value.geometry_dependency);
                writer.i32(90, value.geometry_node_id);
                writer.point3(10, value.point);
                writer.point3(10, value.direction);
            } else if constexpr (std::is_same_v<T, AssocConstraintBoundedLine>) {
                writer.handle(330, value.geometry_dependency);
                writer.i32(90, value.geometry_node_id);
                writer.point3(10, value.point);
                writer.point3(10, value.direction);
                writer.boolean(290, value.is_ray);
                writer.point3(10, value.start_point);
                writer.point3(11, value.end_point);
            } else if constexpr (std::is_same_v<T, AssocConstraintCircle> ||
                                 std::is_same_v<T, AssocConstraintArc>) {
                writer.handle(330, value.geometry_dependency);
                writer.i32(90, value.geometry_node_id);
                writer.point3(10, value.center);
                writer.point3(10, value.normal);
                writer.point3(10, value.direction);
                writer.f64(40, value.radius);
                writer.f64(40, value.start_parameter);
                writer.f64(40, value.end_parameter);
                if (version >= DxfVersion::AC1027)
                    writer.f64(40, value.reserved);
                if constexpr (std::is_same_v<T, AssocConstraintArc>) {
                    writer.point3(10, value.start_point);
                    writer.point3(11, value.end_point);
                }
            } else if constexpr (std::is_same_v<T, AssocConstraintEllipse> ||
                                 std::is_same_v<T, AssocConstraintBoundedEllipse>) {
                writer.handle(330, value.geometry_dependency);
                writer.i32(90, value.geometry_node_id);
                writer.point3(10, value.center);
                writer.point3(11, value.major_axis);
                writer.f64(40, value.axis_ratio);
                if constexpr (std::is_same_v<T, AssocConstraintBoundedEllipse>) {
                    writer.point3(10, value.start_point);
                    writer.point3(11, value.end_point);
                }
            } else if constexpr (std::is_same_v<T, AssocConstraintSpline>) {
                writer.handle(330, value.geometry_dependency);
                writer.i32(90, value.geometry_node_id);
                writer.boolean(70, value.rational);
                writer.boolean(70, value.periodic);
                writer.i32(90, value.degree);
                writer.f64(40, value.knot_tolerance);
                writer.i32(90, static_cast<std::int32_t>(value.knots.size()));
                writer.i32(90, value.knot_physical_length);
                writer.i32(90, value.knot_grow_length);
                for (auto knot : value.knots)
                    writer.f64(40, knot);
                writer.i32(90, static_cast<std::int32_t>(value.weights.size()));
                writer.i32(90, value.weight_physical_length);
                writer.i32(90, value.weight_grow_length);
                for (auto weight : value.weights)
                    writer.f64(40, weight);
                writer.i32(90, static_cast<std::int32_t>(value.control_points.size()));
                writer.i32(90, value.control_point_physical_length);
                writer.i32(90, value.control_point_grow_length);
                for (auto point : value.control_points)
                    writer.point3(10, point);
                writer.i32(90, static_cast<std::int32_t>(value.implicit_point_ids.size()));
                for (auto id : value.implicit_point_ids)
                    writer.i32(90, id);
            }
        },
        data);
}
void write_surface(AssocWriter &writer, const AssocSurfaceActionBody &value) {
    write_action_body(writer, value.action_body);
    write_parameter_body(writer, value.parameter_body);
    writer.subclass("AcDbAssocSurfaceActionBody");
    writer.i32(90, value.surface_body.version);
    writer.handle(330, value.surface_body.dependency);
    writer.boolean(290, value.surface_body.is_semi_associative);
    writer.i32(90, value.surface_body.marker);
    writer.boolean(290, value.surface_body.is_semi_override);
    writer.i16(70, value.surface_body.grip_status);
    writer.subclass("AcDbAssocPathBasedSurfaceActionBody");
    writer.i32(90, value.path_status);
    writer.subclass(surface_section(value.kind));
    if (value.kind != AssocSurfaceActionKind::EdgeChamfer &&
        value.kind != AssocSurfaceActionKind::EdgeFillet)
        writer.i32(90, value.class_version);
    if (value.kind == AssocSurfaceActionKind::Extend)
        writer.u8(280, value.option);
    else if (value.kind == AssocSurfaceActionKind::Offset)
        writer.boolean(290, value.flags[0]);
    else if (value.kind == AssocSurfaceActionKind::Trim) {
        writer.boolean(290, value.flags[0]);
        writer.boolean(290, value.flags[1]);
        writer.f64(40, value.distance);
    } else if (value.kind == AssocSurfaceActionKind::Blend) {
        writer.boolean(290, value.flags[0]);
        writer.boolean(291, value.flags[1]);
        writer.boolean(292, value.flags[2]);
        writer.i16(72, value.status);
        writer.boolean(293, value.flags[3]);
        writer.boolean(294, value.flags[4]);
        writer.i16(73, value.secondary_status);
    } else if (value.kind == AssocSurfaceActionKind::Fillet) {
        writer.i16(70, value.status);
        writer.point2(10, value.first_point);
        writer.point2(10, value.second_point);
    }
}
void write_annotation(AssocWriter &writer, const AssocAnnotationActionBody &value) {
    if (value.kind == AssocAnnotationKind::RestoreEntityState) {
        write_action_body(writer, value.action_body);
        writer.subclass("AcDbAssocRestoreEntityStateActionBody");
        writer.i32(90, value.class_version);
        writer.handle(330, value.entity);
        return;
    }
    write_action_body(writer, value.annotation.action_body);
    writer.i16(90, value.annotation.version);
    writer.handle(330, value.annotation.dependency);
    const char *section = "AcDbAssocMLeaderActionBody";
    if (value.kind == AssocAnnotationKind::AlignedDimension)
        section = "ACDBASSOCALIGNEDDIMACTIONBODY";
    else if (value.kind == AssocAnnotationKind::ThreePointAngularDimension)
        section = "Assoc3PointAngularDimActionBody";
    else if (value.kind == AssocAnnotationKind::OrdinateDimension)
        section = "AssocOrdinatedDimActionBody";
    else if (value.kind == AssocAnnotationKind::RotatedDimension)
        section = "AssocRotatedDimActionBody";
    writer.subclass(section);
    writer.i32(90, value.class_version);
    if (value.kind == AssocAnnotationKind::MLeader) {
        writer.i32(90, static_cast<std::int32_t>(value.actions.size()));
        for (const auto &action : value.actions) {
            writer.i32(90, action.dependency_id);
            writer.handle(330, action.dependency);
        }
    } else if (value.kind == AssocAnnotationKind::ThreePointAngularDimension) {
        writer.handle(330, value.read_node);
        writer.handle(330, value.dimension_node);
        writer.handle(330, value.dependency);
    } else {
        writer.handle(330, value.read_node);
        writer.handle(330, value.dimension_node);
    }
}
void write_array_body(AssocWriter &writer, const AssocArrayActionBody &value) {
    write_action_body(writer, value.action_body);
    write_parameter_body(writer, value.parameter_body);
    writer.subclass("AcDbAssocArrayActionBody");
    writer.i32(90, value.version);
    writer.text(1, value.parameter_block);
    writer.i32(90, value.item_list_version);
    writer.i32(90, static_cast<std::int32_t>(value.items.size()));
    writer.text(1, value.item_class);
    for (const auto &item : value.items) {
        writer.subclass("AcDbAssocArrayItem");
        write_array_item(writer, item);
    }
    for (auto item : value.transform)
        writer.f64(40, item);
}
void write_dimension_association(AssocWriter &writer, const AssocDimensionAssociation &value) {
    writer.subclass("AcDbDimAssoc");
    writer.handle(330, value.dimension);
    writer.i32(90, value.associativity);
    writer.boolean(70, value.trans_space);
    writer.u8(71, value.rotated_type);
    for (int slot = 0; slot < 4; ++slot) {
        if ((value.associativity & (1 << slot)) == 0)
            continue;
        AssocDimensionReference fallback;
        const auto &stored = value.references[static_cast<std::size_t>(slot)];
        const AssocDimensionReference *items = stored.empty() ? &fallback : stored.data();
        std::size_t count = stored.empty() ? 1 : stored.size();
        for (std::size_t i = 0; i < count; ++i) {
            const auto &reference = items[i];
            writer.text(1, reference.class_name);
            writer.u8(72, reference.osnap_type);
            for (auto handle : reference.xrefs)
                writer.handle(331, handle);
            if (AssocDimensionReference::stores_main_subentity(reference.osnap_type, reference.xrefs.size())) {
                writer.i32(73, reference.main_subent_type);
                writer.i32(91, reference.main_gs_marker);
                for (const auto &path : reference.xref_paths)
                    writer.text(301, path);
            }
            writer.f64(40, reference.osnap_distance);
            writer.point3(10, reference.osnap_point);
            if (AssocDimensionReference::stores_intersection(reference.osnap_type)) {
                for (auto handle : reference.intersection_objects)
                    writer.handle(332, handle);
                if (AssocDimensionReference::stores_intersection_subentity(
                        reference.osnap_type, reference.intersection_objects.size())) {
                    writer.i32(74, reference.intersection_subent_type);
                    writer.i32(92, reference.intersection_gs_marker);
                    for (const auto &path : reference.intersection_xref_paths)
                        writer.text(302, path);
                }
            }
            writer.boolean(75, i + 1 < count);
        }
    }
}
std::vector<DxfRawField> encode_associative_fields(const AssociativeObject &object, DxfVersion version) {
    AssocWriter writer;
    writer.text(0, object.dxf_name);
    writer.handle(5, object.handle);
    if (!object.reactors.empty()) {
        writer.text(102, "{ACAD_REACTORS");
        for (auto reactor : object.reactors)
            writer.handle(330, reactor);
        writer.text(102, "}");
    }
    if (object.xdictionary_handle) {
        writer.text(102, "{ACAD_XDICTIONARY");
        writer.handle(360, *object.xdictionary_handle);
        writer.text(102, "}");
    }
    writer.handle(330, object.owner);
    std::visit(
        [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AssocDependency>)
                write_dependency(writer, value);
            else if constexpr (std::is_same_v<T, AssocValueDependency>) {
                write_dependency(writer, value.dependency);
                writer.subclass("AcDbAssocValueDependency");
                writer.i32(90, value.class_version);
                writer.text(1, value.name);
                write_eval(writer, value.value);
            } else if constexpr (std::is_same_v<T, AssocGeomDependency>) {
                write_dependency(writer, value.dependency);
                writer.subclass("AcDbAssocGeomDependency");
                writer.i16(90, value.class_version);
                writer.boolean(290, value.enabled);
                auto name = value.persistent_subent.class_name;
                if (name.empty()) {
                    if (value.persistent_subent.class_code == 1)
                        name = "AcDbAssocSingleEdgePersSubentId";
                    else if (value.persistent_subent.class_code == 3)
                        name = "AcDbAssocEdgePersSubentId";
                    else if (value.persistent_subent.class_code == 5)
                        name = "AcDbAssocAsmBasedEntityPersSubentId";
                }
                writer.text(1, name);
                for (auto item : value.persistent_subent.values)
                    writer.i32(90, item);
                writer.boolean(290, value.persistent_subent.dependent_on_compound_object);
            } else if constexpr (std::is_same_v<T, AssocSurfaceActionBody>)
                write_surface(writer, value);
            else if constexpr (std::is_same_v<T, AssocAction>)
                write_action(writer, value);
            else if constexpr (std::is_same_v<T, AssocNetwork>) {
                write_action(writer, value.action);
                writer.subclass("AcDbAssocNetwork");
                writer.i16(90, value.network_version);
                writer.i32(90, value.network_action_index);
                writer.i32(90, static_cast<std::int32_t>(value.actions.size()));
                for (const auto &action : value.actions)
                    writer.handle(action.is_owned ? 360 : 330, action.dependency);
                writer.i32(90, static_cast<std::int32_t>(value.owned_actions.size()));
                for (auto action : value.owned_actions)
                    writer.handle(330, action);
            } else if constexpr (std::is_same_v<T, AssocAnnotationActionBody>)
                write_annotation(writer, value);
            else if constexpr (std::is_same_v<T, AssocPersSubentManager>) {
                writer.subclass("AcDbAssocPersSubentManager");
                writer.i32(90, value.class_version);
                for (auto marker : value.markers)
                    writer.i32(90, marker);
                for (auto item : value.values)
                    writer.i32(90, item);
                writer.boolean(290, value.final_flag);
            } else if constexpr (std::is_same_v<T, AssocEdgeActionParam>) {
                write_single(writer, value.single_dependency, "AcDbAssocEdgeActionParam", version);
                writer.handle(330, value.parameter);
                writer.boolean(290, value.has_action);
                if (value.has_action) {
                    writer.i32(90, value.action_type);
                    for (const auto &item : value.curve)
                        std::visit(
                            [&](const auto &curve) {
                                using C = std::decay_t<decltype(curve)>;
                                if constexpr (std::is_same_v<C, bool>)
                                    writer.boolean(70, curve);
                                else if constexpr (std::is_same_v<C, std::int32_t>)
                                    writer.i32(90, curve);
                                else if constexpr (std::is_same_v<C, double>)
                                    writer.f64(40, curve);
                                else {
                                    writer.f64(10, curve.x);
                                    writer.f64(20, curve.y);
                                    writer.f64(30, curve.z);
                                }
                            },
                            item);
                }
            } else if constexpr (std::is_same_v<T, Assoc2dConstraintGroup>) {
                write_action(writer, value.action);
                writer.i32(90, value.version == 0 ? 2 : value.version);
                writer.boolean(70, value.flag);
                for (auto point : value.work_plane)
                    writer.point3(10, point);
                writer.handle(360, value.dependency);
                writer.i32(90, static_cast<std::int32_t>(value.actions.size()));
                for (auto action : value.actions)
                    writer.handle(360, action);
                writer.i32(90, value.effective_next_node_id());
                std::vector<const AssocConstraintNode *> registered;
                const AssocConstraintNode *root = nullptr;
                for (const auto &node : value.nodes) {
                    if (node.class_name.empty())
                        root = &node;
                    else
                        registered.push_back(&node);
                }
                if (version < DxfVersion::AC1027) {
                    writer.i32(90, static_cast<std::int32_t>(registered.size()));
                    for (auto *node : registered) {
                        writer.text(1, node->class_name);
                        writer.handle(330, object.handle);
                        write_constraint_common(writer, *node, version);
                        write_constraint_data(writer, node->data, version);
                    }
                    return;
                }
                if (root) {
                    writer.i32(90, root->node_id);
                    writer.i32(90, static_cast<std::int32_t>(root->connections.size()));
                    for (auto connection : root->connections)
                        writer.i32(90, connection);
                    writer.boolean(290, root->status != 0);
                } else {
                    writer.i32(90, 0);
                    writer.i32(90, 0);
                    writer.boolean(290, false);
                }
                writer.i32(90, static_cast<std::int32_t>(registered.size()));
                for (auto *node : registered) {
                    writer.text(1, node->class_name);
                    writer.i32(90, node->node_id);
                }
                for (auto *node : registered) {
                    write_constraint_common(writer, *node, version);
                    write_constraint_data(writer, node->data, version);
                }
            } else if constexpr (std::is_same_v<T, AssocVariable>) {
                write_action(writer, value.action);
                writer.subclass("AcDbAssocVariable");
                writer.i32(90, value.class_version);
                writer.text(1, value.name);
                writer.text(1, value.expression);
                writer.text(1, value.evaluator);
                writer.text(1, value.description);
                write_eval(writer, value.value);
                writer.boolean(290, value.has_cached_value);
                if (value.has_cached_value)
                    writer.text(1, value.cached_value);
                writer.boolean(290, value.flag);
                writer.i32(90, static_cast<std::int32_t>(value.dependencies.size()));
                for (const auto &dependency : value.dependencies) {
                    writer.handle(360, dependency.dependency);
                    writer.i32(90, dependency.flags);
                }
            } else if constexpr (std::is_same_v<T, AssocActionParam>)
                write_action_param(writer, value, version);
            else if constexpr (std::is_same_v<T, AssocCompoundActionParam>)
                write_compound(writer, value, version);
            else if constexpr (std::is_same_v<T, AssocPointRefActionParam>)
                write_compound(writer, value.compound, version);
            else if constexpr (std::is_same_v<T, AssocOsnapPointRefActionParam>) {
                write_compound(writer, value.compound, version);
                writer.subclass("ACDBASSOCOSNAPPOINTREFACTIONPARAM");
                writer.i16(90, value.status);
                writer.u8(90, value.osnap_mode);
                writer.f64(40, value.parameter);
            } else if constexpr (std::is_same_v<T, AssocSingleDependencyActionParam>)
                write_single(writer, value, "AcDbAssocObjectActionParam", version);
            else if constexpr (std::is_same_v<T, AssocPathActionParam>) {
                write_compound(writer, value.compound, version);
                writer.subclass("AcDbAssocPathActionParam");
                writer.i32(90, value.version);
            } else if constexpr (std::is_same_v<T, AssocDimDependencyBody>) {
                writer.subclass("AcDbAssocDependencyBody");
                writer.i16(90, value.dependency_body_version);
                writer.subclass("AcDbImpAssocDimDependencyBodyBase");
                writer.i16(90, value.base_version);
                writer.text(1, value.name);
                writer.subclass("AcDbAssocDimDependencyBody");
                writer.i16(90, value.class_version);
            } else if constexpr (std::is_same_v<T, AssocFaceActionParam>) {
                write_single(writer, value.single_dependency, "AcDbAssocFaceActionParam", version);
                writer.i32(90, value.index);
            } else if constexpr (std::is_same_v<T, AssocVertexActionParam>) {
                write_single(writer, value.single_dependency, "AcDbAssocVertexActionParam", version);
                writer.point3(10, value.point);
            } else if constexpr (std::is_same_v<T, AssocAsmBodyActionParam>) {
                write_single(writer, value.single_dependency, "AcDbAssocAsmbodyActionParam", version);
                writer.subclass("AcDbModelerGeometry");
                writer.text(1, value.sat_data);
            } else if constexpr (std::is_same_v<T, AssocArrayParameters>) {
                writer.subclass("AcDbAssocArrayCommonParameters");
                writer.i32(90, value.version);
                writer.i32(90, static_cast<std::int32_t>(value.items.size()));
                writer.text(1, value.class_name);
                for (const auto &item : value.items)
                    write_array_item(writer, item);
                writer.i32(90, value.item_count);
                writer.i32(90, value.row_count);
                writer.i32(90, value.level_count);
                writer.subclass(object.cpp_class_name);
            } else if constexpr (std::is_same_v<T, AssocArrayActionBody>)
                write_array_body(writer, value);
            else if constexpr (std::is_same_v<T, AssocArrayModifyActionBody>) {
                write_array_body(writer, value.body);
                writer.subclass("AcDbAssocArrayModifyActionBody");
                writer.i16(70, value.status);
                writer.i32(90, static_cast<std::int32_t>(value.item_locations.size()));
                for (const auto &location : value.item_locations)
                    for (auto item : location)
                        writer.i32(90, item);
            } else if constexpr (std::is_same_v<T, AssocDimensionAssociation>)
                write_dimension_association(writer, value);
            else if constexpr (std::is_same_v<T, PersSubentManager>) {
                writer.subclass("AcDbPersSubentManager");
                writer.i32(90, value.class_version);
                writer.i32(90, value.marker_zero);
                writer.i32(90, value.marker_two);
                writer.i32(90, value.associative_step_count);
                writer.i32(90, value.associative_subent_count);
                for (auto item : value.values)
                    writer.i32(90, item);
            } else if constexpr (std::is_same_v<T, AssocViewRepActionBody>) {
                write_action_body(writer, value.action_body);
                writer.subclass("AcDbAssocViewRepActionBody");
                writer.i16(70, value.class_version);
                writer.handle(360, value.view_rep);
                writer.i32(90, value.view_type);
                writer.f64(40, value.rotation);
            } else if constexpr (std::is_same_v<T, AssocViewObjectActionParam>) {
                write_single(writer, value.single_dependency, "AcDbAssocObjectActionParam", version);
                const char *section = "AcDbAssocViewBorderActionParam";
                if (value.kind == AssocViewObjectActionParamKind::ViewRep)
                    section = "AcDbAssocViewRepActionParam";
                else if (value.kind == AssocViewObjectActionParamKind::ViewSymbol)
                    section = "AcDbAssocViewSymbolActionParam";
                else if (value.kind == AssocViewObjectActionParamKind::ViewStyle)
                    section = "AcDbAssocViewStyleActionParam";
                writer.subclass(section);
                writer.i16(70, value.class_version);
            } else if constexpr (std::is_same_v<T, AssocViewRepHatchManager>) {
                write_compound(writer, value.compound, version);
                writer.subclass("AcDbAssocViewRepHatchManager");
                writer.i16(70, value.class_version);
                writer.i32(90, static_cast<std::int32_t>(value.items.size()));
                for (const auto &item : value.items) {
                    writer.i64(160, item.first_id);
                    writer.i64(160, item.second_id);
                    writer.i32(90, item.status);
                    writer.handle(330, item.parameter);
                }
            } else if constexpr (std::is_same_v<T, AssocViewRepHatchActionParam>) {
                write_single(writer, value.single_dependency, "AcDbAssocObjectActionParam", version);
                writer.subclass("AcDbAssocViewRepHatchActionParam");
                writer.i16(70, value.class_version);
                writer.point3(210, value.normal);
                writer.i32(90, value.hatch_index);
                writer.i32(90, value.flags);
            } else if constexpr (std::is_same_v<T, AssocSmartCenterActionBody>) {
                write_action_body(writer, value.action_body);
                write_parameter_body(writer, value.parameter_body);
                writer.subclass("AcDbSmartCenterActionBody");
                writer.i32(90, value.version);
            } else if constexpr (std::is_same_v<T, AssocViewLabelActionParam>) {
                write_single(writer, value.single_dependency, "AcDbAssocObjectActionParam", version);
                writer.subclass("AcDbAssocViewLabelActionParam");
                writer.i16(70, value.class_version);
                writer.i16(70, value.label_version);
                writer.f64(210, value.offset.x);
                writer.f64(220, value.offset.y);
                writer.u8(280, value.flag);
            }
        },
        object.data);
    return writer.fields;
}
