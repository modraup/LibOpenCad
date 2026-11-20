// SPDX-License-Identifier: MPL-2.0
// Typed DXF ClassObject codecs for render/section-manager, render-scene,
// path/TV, layer filter, partial viewing, section settings, sun study,
// data table, geomap image, model-doc view styles, data links, object data
// and ViewRep families.
namespace {
struct ClassFields {
    std::map<int, std::deque<std::string>> values;
    explicit ClassFields(const std::vector<DxfRawField> &fields) {
        for (const auto &field : fields)
            values[field.code].push_back(field.value);
    }
    std::string take(int code) {
        auto found = values.find(code);
        if (found == values.end() || found->second.empty())
            return {};
        auto text = found->second.front();
        found->second.pop_front();
        return text;
    }
    std::int32_t i32(int code) {
        auto text = std::string(trim(take(code)));
        if (text.empty())
            return 0;
        try {
            std::size_t end = 0;
            auto value = std::stoll(text, &end, 10);
            if (end != text.size() || value < INT32_MIN || value > INT32_MAX)
                return 0;
            return static_cast<std::int32_t>(value);
        } catch (const std::exception &) {
            return 0;
        }
    }
    std::int16_t i16(int code) { return static_cast<std::int16_t>(i32(code)); }
    bool flag(int code) { return i32(code) != 0; }
    double real(int code) {
        auto text = std::string(trim(take(code)));
        if (text.empty())
            return 0;
        try {
            std::size_t end = 0;
            auto value = std::stod(text, &end);
            return end == text.size() && std::isfinite(value) ? value : 0;
        } catch (const std::exception &) {
            return 0;
        }
    }
    std::uint64_t handle(int code) { return parse_handle(take(code)).value_or(0); }
    std::string text(int code) { return take(code); }
    std::uint32_t u32(int code) { return static_cast<std::uint32_t>(i32(code)); }
    std::uint8_t u8(int code) { return static_cast<std::uint8_t>(i32(code) & 255); }
    Color aci(int code) { return Color::from_index(i16(code)); }
    bool has(int code) const {
        auto found = values.find(code);
        return found != values.end() && !found->second.empty();
    }
    std::vector<std::string> take_all(int code) {
        std::vector<std::string> out;
        auto found = values.find(code);
        if (found == values.end())
            return out;
        while (!found->second.empty()) {
            out.push_back(found->second.front());
            found->second.pop_front();
        }
        return out;
    }
    std::int64_t i64(int code) {
        auto text = std::string(trim(take(code)));
        if (text.empty())
            return 0;
        try {
            std::size_t end = 0;
            auto value = std::stoll(text, &end, 10);
            return end == text.size() ? value : 0;
        } catch (const std::exception &) {
            return 0;
        }
    }
    Vector3 p3(int code) { return {real(code), real(code + 10), real(code + 20)}; }
    Vector2 p2(int code) { return {real(code), real(code + 10)}; }
    std::string text_skipping(int code, std::string_view marker) {
        for (;;) {
            auto value = take(code);
            if (value.empty() || value != marker)
                return value;
        }
    }
};
struct ClassWriter {
    std::vector<DxfRawField> values;
    void i(int code, std::int32_t value) { values.push_back({code, std::to_string(value)}); }
    void b(int code, bool value) { i(code, value ? 1 : 0); }
    void r(int code, double value) {
        if (!std::isfinite(value))
            throw std::invalid_argument("Nonfinite class object field");
        std::ostringstream out;
        out.precision(17);
        out << value;
        values.push_back({code, out.str()});
    }
    void s(int code, const std::string &value) { values.push_back({code, value}); }
    void bin(int code, const std::vector<std::uint8_t> &bytes) {
        constexpr char digits[] = "0123456789ABCDEF";
        for (std::size_t n = 0; n < bytes.size(); n += 127) {
            std::string hex;
            for (auto j = n; j < std::min(n + 127, bytes.size()); ++j) {
                hex += digits[bytes[j] >> 4];
                hex += digits[bytes[j] & 15];
            }
            s(code, hex);
        }
    }
    void h(int code, std::uint64_t value) { values.push_back({code, hex_handle(value)}); }
    void u(int code, std::uint32_t value) { i(code, static_cast<std::int32_t>(value)); }
    void u8(int code, std::uint8_t value) { i(code, value); }
    void color(int code, Color value) {
        int index = 256;
        switch (value.kind()) {
        case Color::Kind::ByBlock:
            index = 0;
            break;
        case Color::Kind::None:
            index = 257;
            break;
        case Color::Kind::Index:
            index = value.index().value_or(7);
            break;
        case Color::Kind::Rgb:
            index = value.approximate_index();
            break;
        default:
            break;
        }
        i(code, index);
    }
    void i64(int code, std::int64_t value) { values.push_back({code, std::to_string(value)}); }
    void p3(int code, Vector3 value) {
        r(code, value.x);
        r(code + 10, value.y);
        r(code + 20, value.z);
    }
    void p2(int code, Vector2 value) {
        r(code, value.x);
        r(code + 10, value.y);
    }
};
void require_data_table(const DataTable &table) {
    if (table.row_count < 0 || table.columns.size() > 100000 ||
        static_cast<std::uint32_t>(table.row_count) > 100000)
        throw std::invalid_argument("Invalid data table count");
    for (const auto &column : table.columns) {
        if (!data_table_has_native_codec(column.value_type) ||
            column.rows.size() != static_cast<std::size_t>(table.row_count))
            throw std::invalid_argument("Invalid data table column");
    }
}
RenderSettings read_render_settings_dxf(ClassFields &fields, bool has_predefined_in_base, DxfVersion version) {
    auto stored = fields.i32(90);
    RenderSettings value;
    value.class_version = has_predefined_in_base && version >= DxfVersion::AC1027 ? stored - 1 : stored;
    value.name = fields.text(1);
    value.fog_enabled = fields.flag(290);
    value.fog_background_enabled = fields.flag(290);
    value.backfaces_enabled = fields.flag(290);
    value.environment_image_enabled = fields.flag(290);
    value.environment_image_filename = fields.text(1);
    value.description = fields.text(1);
    value.display_index = fields.i32(90);
    value.has_predefined = has_predefined_in_base && fields.flag(290);
    return value;
}
void write_render_settings_dxf(ClassWriter &out, const RenderSettings &value, bool write_has_predefined,
                               DxfVersion version) {
    auto class_version = write_has_predefined && version >= DxfVersion::AC1027 ? value.class_version + 1
                                                                               : value.class_version;
    out.i(90, class_version);
    out.s(1, value.name);
    out.b(290, value.fog_enabled);
    out.b(290, value.fog_background_enabled);
    out.b(290, value.backfaces_enabled);
    out.b(290, value.environment_image_enabled);
    out.s(1, value.environment_image_filename);
    out.s(1, value.description);
    out.i(90, value.display_index);
    if (write_has_predefined && version >= DxfVersion::AC1027)
        out.b(290, value.has_predefined);
}
ModelDocViewStyle read_model_doc_view_style(ClassFields &fields) {
    ModelDocViewStyle value;
    value.class_version = fields.i16(70);
    value.description = fields.text(3);
    value.modified_for_recompute = fields.flag(290);
    value.display_name = fields.text(300);
    value.flags = fields.i32(90);
    return value;
}
void write_model_doc_view_style(ClassWriter &out, const ModelDocViewStyle &value, DxfVersion version) {
    out.i(70, value.class_version);
    out.s(3, value.description);
    out.b(290, value.modified_for_recompute);
    if (version >= DxfVersion::AC1032) {
        out.s(300, value.display_name_or_description());
        out.i(90, value.flags);
    }
}
void require_hatch_angles(std::int32_t count) {
    if (count < 0 || count > 100000)
        throw std::invalid_argument("Invalid hatch angle count");
}
void require_data_link_count(std::int32_t count) {
    if (count < 0 || count > 100000)
        throw std::invalid_argument("Invalid data link count");
}
void require_object_data_count(std::size_t count) {
    if (count > 100000)
        throw std::invalid_argument("Invalid object data count");
}
void require_view_rep_count(std::int32_t count) {
    if (count < 0 || count > 100000)
        throw std::invalid_argument("Invalid view representation count");
}
void require_view_rep_path(std::int32_t count) {
    if (count < 0 || count > 10000)
        throw std::invalid_argument("Invalid view representation path count");
}
void require_finite_view_rep(double value) {
    if (!std::isfinite(value))
        throw std::invalid_argument("Nonfinite view representation field");
}
void require_finite_view_rep_vec(Vector3 value) {
    require_finite_view_rep(value.x);
    require_finite_view_rep(value.y);
    require_finite_view_rep(value.z);
}
ViewRepGuid read_view_rep_guid(ClassFields &fields) {
    ViewRepGuid guid;
    guid.data1 = fields.i32(90);
    guid.data2 = fields.i16(70);
    guid.data3 = fields.i16(70);
    for (auto &item : guid.data4)
        item = fields.u8(280);
    return guid;
}
void write_view_rep_guid(ClassWriter &out, const ViewRepGuid &guid) {
    out.i(90, guid.data1);
    out.i(70, guid.data2);
    out.i(70, guid.data3);
    for (auto item : guid.data4)
        out.u8(280, item);
}
ViewRepSketchGeometry read_view_rep_geometry_dxf(ClassFields &fields) {
    ViewRepSketchGeometry geometry;
    geometry.type_code = fields.i32(90);
    if (geometry.type_code == 19 || geometry.type_code == 23) {
        geometry.kind = ViewRepSketchGeometry::Kind::Line;
        geometry.first = fields.p3(10);
        geometry.second = fields.p3(10);
    } else if (geometry.type_code == 11) {
        geometry.kind = ViewRepSketchGeometry::Kind::Circle;
        geometry.center = fields.p3(10);
        geometry.normal = fields.p3(10);
        geometry.direction = fields.p3(10);
        geometry.radius = fields.real(40);
        geometry.start_parameter = fields.real(40);
        geometry.end_parameter = fields.real(40);
        geometry.reserved = fields.real(40);
    } else if (geometry.type_code == 42) {
        geometry.kind = ViewRepSketchGeometry::Kind::Nurb;
        geometry.flags = {fields.flag(70), fields.flag(70)};
        geometry.degree = fields.i32(90);
        geometry.tolerance = fields.real(40);
        geometry.knot_header = {fields.i32(90), fields.i32(90), fields.i32(90)};
        require_view_rep_count(geometry.knot_header[0]);
        geometry.knots.reserve(static_cast<std::size_t>(geometry.knot_header[0]));
        for (std::int32_t i = 0; i < geometry.knot_header[0]; ++i)
            geometry.knots.push_back(fields.real(40));
        geometry.weight_header = {fields.i32(90), fields.i32(90), fields.i32(90)};
        require_view_rep_count(geometry.weight_header[0]);
        geometry.weights.reserve(static_cast<std::size_t>(geometry.weight_header[0]));
        for (std::int32_t i = 0; i < geometry.weight_header[0]; ++i)
            geometry.weights.push_back(fields.real(40));
        geometry.point_header = {fields.i32(90), fields.i32(90), fields.i32(90)};
        require_view_rep_count(geometry.point_header[0]);
        geometry.control_points.reserve(static_cast<std::size_t>(geometry.point_header[0]));
        for (std::int32_t i = 0; i < geometry.point_header[0]; ++i)
            geometry.control_points.push_back(fields.p3(10));
    }
    return geometry;
}
void write_view_rep_geometry_dxf(ClassWriter &out, const ViewRepSketchGeometry &geometry) {
    if (geometry.kind == ViewRepSketchGeometry::Kind::Line) {
        require_finite_view_rep_vec(geometry.first);
        require_finite_view_rep_vec(geometry.second);
        out.i(90, geometry.type_code);
        out.p3(10, geometry.first);
        out.p3(10, geometry.second);
        return;
    }
    if (geometry.kind == ViewRepSketchGeometry::Kind::Circle) {
        require_finite_view_rep_vec(geometry.center);
        require_finite_view_rep_vec(geometry.normal);
        require_finite_view_rep_vec(geometry.direction);
        require_finite_view_rep(geometry.radius);
        require_finite_view_rep(geometry.start_parameter);
        require_finite_view_rep(geometry.end_parameter);
        require_finite_view_rep(geometry.reserved);
        out.i(90, geometry.type_code);
        out.p3(10, geometry.center);
        out.p3(10, geometry.normal);
        out.p3(10, geometry.direction);
        out.r(40, geometry.radius);
        out.r(40, geometry.start_parameter);
        out.r(40, geometry.end_parameter);
        out.r(40, geometry.reserved);
        return;
    }
    if (geometry.kind == ViewRepSketchGeometry::Kind::Nurb) {
        require_view_rep_count(geometry.knot_header[0]);
        require_view_rep_count(geometry.weight_header[0]);
        require_view_rep_count(geometry.point_header[0]);
        if (geometry.knots.size() != static_cast<std::size_t>(geometry.knot_header[0]) ||
            geometry.weights.size() != static_cast<std::size_t>(geometry.weight_header[0]) ||
            geometry.control_points.size() != static_cast<std::size_t>(geometry.point_header[0]))
            throw std::invalid_argument("Invalid view representation spline");
        require_finite_view_rep(geometry.tolerance);
        for (auto item : geometry.knots)
            require_finite_view_rep(item);
        for (auto item : geometry.weights)
            require_finite_view_rep(item);
        for (auto point : geometry.control_points)
            require_finite_view_rep_vec(point);
        out.i(90, geometry.type_code);
        out.i(70, geometry.flags[0] ? 1 : 0);
        out.i(70, geometry.flags[1] ? 1 : 0);
        out.i(90, geometry.degree);
        out.r(40, geometry.tolerance);
        for (auto item : geometry.knot_header)
            out.i(90, item);
        for (auto item : geometry.knots)
            out.r(40, item);
        for (auto item : geometry.weight_header)
            out.i(90, item);
        for (auto item : geometry.weights)
            out.r(40, item);
        for (auto item : geometry.point_header)
            out.i(90, item);
        for (auto item : geometry.control_points)
            out.p3(10, item);
        return;
    }
    out.i(90, 0);
}
ViewRep read_view_rep_dxf(ClassFields &fields) {
    ViewRep value;
    value.header_values = {fields.i32(90), fields.i32(90), fields.i32(90), fields.i32(90), fields.i32(90)};
    value.name = fields.text(1);
    value.scale = fields.i32(90);
    value.header_status = fields.i32(90);
    value.description = fields.text(1);
    value.source_id = fields.i64(160);
    value.source_enabled = fields.flag(290);
    value.source_version = fields.i32(90);
    value.model_id = fields.i64(160);
    value.guid = read_view_rep_guid(fields);
    value.marker = fields.u8(280);
    for (auto &item : value.transform)
        item = fields.real(40);
    value.transform_version = fields.i32(90);
    value.database_id = fields.i64(160);
    value.geometry_version = fields.i32(90);
    value.geometry_marker = fields.i32(90);
    auto sketch_count = fields.i32(90);
    require_view_rep_count(sketch_count);
    value.sketches.reserve(static_cast<std::size_t>(sketch_count));
    for (std::int32_t i = 0; i < sketch_count; ++i) {
        ViewRepSketch sketch;
        sketch.id = fields.i32(90);
        sketch.version = fields.i32(90);
        auto reference_count = fields.i32(90);
        require_view_rep_count(reference_count);
        sketch.references.reserve(static_cast<std::size_t>(reference_count));
        for (std::int32_t j = 0; j < reference_count; ++j)
            sketch.references.push_back({fields.handle(330), fields.flag(290)});
        sketch.reserved = fields.i32(90);
        sketch.enabled = fields.flag(290);
        sketch.geometry = read_view_rep_geometry_dxf(fields);
        sketch.final_flag = fields.flag(290);
        value.sketches.push_back(std::move(sketch));
    }
    value.related_objects = {fields.handle(330), fields.handle(330)};
    value.source_manager = fields.handle(340);
    value.owned_objects = {fields.handle(360), fields.handle(360)};
    value.optional_objects = {fields.handle(330), fields.handle(330)};
    value.position = fields.p2(10);
    value.rotation = fields.real(40);
    value.orientation = fields.handle(340);
    value.is_active = fields.flag(290);
    value.projection = fields.i16(70);
    value.linked_views = {fields.handle(330), fields.handle(330)};
    auto path_count = fields.i32(90);
    require_view_rep_count(path_count);
    value.section_sketches.reserve(static_cast<std::size_t>(path_count));
    for (std::int32_t i = 0; i < path_count; ++i) {
        ViewRepObjectPath path;
        path.class_name = fields.text(1);
        auto extra = fields.i16(70);
        require_view_rep_path(extra);
        path.objects.reserve(static_cast<std::size_t>(extra) + 1);
        for (std::int32_t j = 0; j <= extra; ++j)
            path.objects.push_back(fields.handle(330));
        value.section_sketches.push_back(std::move(path));
    }
    value.action_mode = fields.i32(90);
    if (value.action_mode != 0)
        value.action = fields.handle(360);
    value.has_parent = fields.flag(290);
    value.parent = fields.handle(330);
    value.tail_version = fields.i32(90);
    value.tail_state = fields.i32(90);
    value.tail_id = fields.i64(160);
    value.path_count = fields.i32(90);
    value.path_version = fields.i32(90);
    value.path_id = fields.i64(160);
    value.has_block_path = fields.flag(290);
    if (value.has_block_path) {
        ViewRepBlockPath path;
        path.class_name = fields.text(1);
        path.version = fields.i32(90);
        auto entry_count = fields.i32(91);
        require_view_rep_count(entry_count);
        path.entries.reserve(static_cast<std::size_t>(entry_count));
        for (std::int32_t i = 0; i < entry_count; ++i)
            path.entries.push_back(
                {fields.u8(281), fields.u8(280), fields.handle(332)});
        value.block_path = std::move(path);
    }
    value.style = fields.handle(340);
    return value;
}
void write_view_rep_dxf(ClassWriter &out, const ViewRep &value) {
    require_view_rep_count(static_cast<std::int32_t>(value.sketches.size()));
    require_view_rep_count(static_cast<std::int32_t>(value.section_sketches.size()));
    for (auto item : value.header_values)
        out.i(90, item);
    out.s(1, value.name);
    out.i(90, value.scale);
    out.i(90, value.header_status);
    out.s(1, value.description);
    out.i64(160, value.source_id);
    out.b(290, value.source_enabled);
    out.i(90, value.source_version);
    out.i64(160, value.model_id);
    write_view_rep_guid(out, value.guid);
    out.u8(280, value.marker);
    for (auto item : value.transform) {
        require_finite_view_rep(item);
        out.r(40, item);
    }
    out.i(90, value.transform_version);
    out.i64(160, value.database_id);
    out.i(90, value.geometry_version);
    out.i(90, value.geometry_marker);
    out.i(90, static_cast<std::int32_t>(value.sketches.size()));
    for (const auto &sketch : value.sketches) {
        require_view_rep_count(static_cast<std::int32_t>(sketch.references.size()));
        out.i(90, sketch.id);
        out.i(90, sketch.version);
        out.i(90, static_cast<std::int32_t>(sketch.references.size()));
        for (const auto &reference : sketch.references) {
            out.h(330, reference.object);
            out.b(290, reference.flag);
        }
        out.i(90, sketch.reserved);
        out.b(290, sketch.enabled);
        write_view_rep_geometry_dxf(out, sketch.geometry);
        out.b(290, sketch.final_flag);
    }
    for (auto handle : value.related_objects)
        out.h(330, handle);
    out.h(340, value.source_manager);
    for (auto handle : value.owned_objects)
        out.h(360, handle);
    for (auto handle : value.optional_objects)
        out.h(330, handle);
    require_finite_view_rep(value.position.x);
    require_finite_view_rep(value.position.y);
    require_finite_view_rep(value.rotation);
    out.p2(10, value.position);
    out.r(40, value.rotation);
    out.h(340, value.orientation);
    out.b(290, value.is_active);
    out.i(70, value.projection);
    for (auto handle : value.linked_views)
        out.h(330, handle);
    out.i(90, static_cast<std::int32_t>(value.section_sketches.size()));
    for (const auto &path : value.section_sketches) {
        auto extra = path.objects.empty() ? 0 : static_cast<std::int32_t>(path.objects.size() - 1);
        require_view_rep_path(extra);
        out.s(1, path.class_name);
        out.i(70, extra);
        for (auto handle : path.objects)
            out.h(330, handle);
    }
    out.i(90, value.action_mode);
    if (value.action)
        out.h(360, *value.action);
    out.b(290, value.has_parent);
    out.h(330, value.parent);
    out.i(90, value.tail_version);
    out.i(90, value.tail_state);
    out.i64(160, value.tail_id);
    out.i(90, value.path_count);
    out.i(90, value.path_version);
    out.i64(160, value.path_id);
    out.b(290, value.has_block_path);
    if (value.block_path) {
        require_view_rep_count(static_cast<std::int32_t>(value.block_path->entries.size()));
        out.s(1, value.block_path->class_name);
        out.i(90, value.block_path->version);
        out.i(91, static_cast<std::int32_t>(value.block_path->entries.size()));
        for (const auto &entry : value.block_path->entries) {
            out.u8(281, entry.flag);
            out.u8(280, entry.kind);
            out.h(332, entry.object);
        }
    }
    out.h(340, value.style);
}
std::vector<std::uint8_t> object_data_payload_dxf(const std::vector<DxfRawField> &fields) {
    std::vector<std::uint8_t> payload;
    for (const auto &field : fields)
        if (field.code == 1004) {
            auto bytes = table_bytes(field.value);
            payload.insert(payload.end(), bytes.begin(), bytes.end());
        }
    return payload;
}
int field_int(std::string_view text) {
    auto value = std::string(trim(text));
    if (value.empty())
        return 0;
    try {
        std::size_t end = 0;
        auto parsed = std::stoll(value, &end, 10);
        return end == value.size() && parsed >= INT32_MIN && parsed <= INT32_MAX
                   ? static_cast<int>(parsed)
                   : 0;
    } catch (const std::exception &) {
        return 0;
    }
}
double field_real(std::string_view text) {
    auto value = std::string(trim(text));
    if (value.empty())
        return 0;
    try {
        std::size_t end = 0;
        auto parsed = std::stod(value, &end);
        return end == value.size() && std::isfinite(parsed) ? parsed : 0;
    } catch (const std::exception &) {
        return 0;
    }
}
void apply_data_link_value(TableValue &value, int code, const std::string &text) {
    if (code == 93)
        value.flags = field_int(text);
    else if (code == 90)
        value.type = field_int(text);
    else if (code == 91)
        value.number = field_int(text);
    else if (code == 140)
        value.number = field_real(text);
    else if (code == 1 || code == 2)
        value.text += text;
    else if (code == 92)
        value.data_size = field_int(text);
    else if (code == 11)
        value.point.x = field_real(text);
    else if (code == 21)
        value.point.y = field_real(text);
    else if (code == 31)
        value.point.z = field_real(text);
    else if (code == 94)
        value.unit = field_int(text);
    else if (code == 300)
        value.format = text;
    else if (code == 302 && value.unit != 12)
        value.formatted = text;
    else if (code >= 310 && code <= 319) {
        auto bytes = table_bytes(text);
        value.binary.insert(value.binary.end(), bytes.begin(), bytes.end());
    } else if (code == 330)
        value.handle = parse_handle(text).value_or(0);
}
std::vector<TableCustomData> data_map_from_pairs(const std::vector<DxfRawField> &pairs) {
    std::vector<TableCustomData> items;
    std::size_t start = pairs.size();
    for (std::size_t i = 0; i < pairs.size(); ++i)
        if (pairs[i].code == 1 && pairs[i].value == "DATAMAP_BEGIN") {
            start = i + 1;
            break;
        }
    if (start >= pairs.size())
        return items;
    std::string name;
    std::optional<TableValue> value;
    for (std::size_t at = start; at < pairs.size(); ++at) {
        const auto &field = pairs[at];
        bool next_item = field.code == 300 && at + 1 < pairs.size() && pairs[at + 1].code == 301 &&
                         pairs[at + 1].value == "DATAMAP_VALUE";
        bool map_end = field.code == 309 && field.value == "DATAMAP_END";
        if (field.code == 304 || next_item || map_end) {
            if (value) {
                items.push_back({std::move(name), *value});
                name.clear();
                value.reset();
            }
            if (field.code == 304)
                continue;
        }
        if (value) {
            apply_data_link_value(*value, field.code, field.value);
            continue;
        }
        if (field.code == 309 && field.value == "DATAMAP_END")
            break;
        if (field.code == 300)
            name = field.value;
        else if (field.code == 301 && field.value == "DATAMAP_VALUE")
            value = TableValue{};
    }
    return items;
}
void write_data_link_value(ClassWriter &out, const TableValue &value, DxfVersion version) {
    if (version >= DxfVersion::AC1021)
        out.i(93, value.flags);
    auto type = version >= DxfVersion::AC1021 ? value.type : value.type & ~0x200;
    out.i(90, type);
    if (version < DxfVersion::AC1021 || (value.flags & 1) == 0) {
        switch (type) {
        case 0:
        case 1: {
            if (!std::isfinite(value.number))
                throw std::invalid_argument("Nonfinite data link integer");
            auto stored = std::max(double(INT32_MIN), std::min(double(INT32_MAX), value.number));
            out.i(91, static_cast<std::int32_t>(stored));
            break;
        }
        case 2:
            out.r(140, value.number);
            break;
        case 4:
        case 0x200:
            out.s(1, value.text);
            break;
        case 8: {
            auto size = value.data_size > 0 && static_cast<std::size_t>(value.data_size) == value.binary.size()
                            ? value.data_size
                            : static_cast<std::int32_t>(value.binary.size());
            out.i(92, size);
            constexpr char digits[] = "0123456789ABCDEF";
            for (std::size_t n = 0; n < value.binary.size(); n += 127) {
                std::string hex;
                for (auto j = n; j < std::min(n + 127, value.binary.size()); ++j) {
                    hex += digits[value.binary[j] >> 4];
                    hex += digits[value.binary[j] & 15];
                }
                out.s(310, hex);
            }
            break;
        }
        case 0x10:
        case 0x20:
            out.i(92, value.data_size ? value.data_size : (type == 0x10 ? 16 : 24));
            out.r(11, value.point.x);
            out.r(21, value.point.y);
            if (type == 0x20)
                out.r(31, value.point.z);
            break;
        case 0x40:
            out.h(330, value.handle);
            break;
        default:
            break;
        }
    }
    if (version >= DxfVersion::AC1021) {
        out.i(94, value.unit);
        out.s(300, value.format);
        if (value.unit != 12)
            out.s(302, value.formatted);
    }
}
} // namespace
ClassObjectData DxfClassObject::definition() const {
    auto name = class_object_ascii_upper(type());
    if (name == "RENDERSETTINGS") {
        ClassFields fields(subclass_fields("AcDbRenderSettings"));
        return read_render_settings_dxf(fields, true, version_);
    }
    if (name == "MENTALRAYRENDERSETTINGS") {
        ClassFields base_fields(subclass_fields("AcDbRenderSettings"));
        MentalRayRenderSettings value;
        value.base = read_render_settings_dxf(base_fields, false, version_);
        ClassFields fields(subclass_fields("AcDbMentalRayRenderSettings"));
        value.version = fields.i32(90);
        value.sampling_min = fields.i32(90);
        value.sampling_max = fields.i32(90);
        value.sampling_filter = fields.i16(70);
        value.sampling_filter_width = fields.real(40);
        value.sampling_filter_height = fields.real(40);
        value.sampling_contrast = {fields.real(40), fields.real(40), fields.real(40), fields.real(40)};
        value.shadow_mode = fields.i16(70);
        value.shadow_maps_enabled = fields.flag(290);
        value.ray_tracing_enabled = fields.flag(290);
        value.ray_trace_depth = {fields.i32(90), fields.i32(90), fields.i32(90)};
        value.global_illumination_enabled = fields.flag(290);
        value.global_illumination_sample_count = fields.i32(90);
        value.global_illumination_sample_radius_enabled = fields.flag(290);
        value.global_illumination_sample_radius = fields.real(40);
        value.photons_per_light = fields.i32(90);
        value.photon_trace_depth = {fields.i32(90), fields.i32(90), fields.i32(90)};
        value.final_gathering_enabled = fields.flag(290);
        value.final_gathering_ray_count = fields.i32(90);
        value.final_gathering_sample_radius_state = {fields.flag(290), fields.flag(290), fields.flag(290)};
        value.final_gathering_sample_radius = {fields.real(40), fields.real(40)};
        value.light_luminance_scale = fields.real(40);
        value.diagnostics_mode = fields.i16(70);
        value.diagnostics_grid_mode = fields.i16(70);
        value.diagnostics_grid_size = fields.real(40);
        value.diagnostics_photon_mode = fields.i16(70);
        value.diagnostics_bsp_mode = fields.i16(70);
        value.export_mi_enabled = fields.flag(290);
        value.description = fields.text(1);
        value.tile_size = fields.i32(90);
        value.tile_order = fields.i16(70);
        value.memory_limit = fields.i32(90);
        value.diagnostics_samples_mode = fields.flag(290);
        value.energy_multiplier = fields.real(40);
        return value;
    }
    if (name == "RAPIDRTRENDERSETTINGS") {
        ClassFields base_fields(subclass_fields("AcDbRenderSettings"));
        RapidRtRenderSettings value;
        value.base = read_render_settings_dxf(base_fields, false, version_);
        ClassFields fields(subclass_fields("AcDbRapidRTRenderSettings"));
        value.version = fields.i32(90);
        value.render_target = fields.i32(70);
        value.render_level = fields.i32(90);
        value.render_time = fields.i32(90);
        value.lighting_model = fields.i32(70);
        value.filter_type = fields.i32(70);
        value.filter_width = fields.real(40);
        value.filter_height = fields.real(40);
        value.base.has_predefined = fields.flag(290);
        return value;
    }
    if (name == "SECTION_MANAGER") {
        ClassFields fields(subclass_fields("AcDbSectionManager"));
        SectionManager value;
        value.is_live = fields.flag(70);
        auto count = fields.i32(90);
        if (count < 0 || count > 100000)
            throw std::invalid_argument("Invalid section manager count");
        value.sections.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
            value.sections.push_back(fields.handle(330));
        return value;
    }
    if (name == "GRADIENT_BACKGROUND") {
        ClassFields fields(subclass_fields("AcDbGradientBackground"));
        GradientBackground value;
        value.class_version = fields.i32(90);
        value.color_top = fields.u32(90);
        value.color_middle = fields.u32(91);
        value.color_bottom = fields.u32(92);
        value.horizon = fields.real(140);
        value.height = fields.real(141);
        value.rotation = fields.real(142);
        return value;
    }
    if (name == "GROUND_PLANE_BACKGROUND") {
        ClassFields fields(subclass_fields("AcDbGroundPlaneBackground"));
        GroundPlaneBackground value;
        value.class_version = fields.i32(90);
        value.color_sky_zenith = fields.u32(90);
        value.color_sky_horizon = fields.u32(91);
        value.color_underground_horizon = fields.u32(92);
        value.color_underground_azimuth = fields.u32(93);
        value.color_near = fields.u32(94);
        value.color_far = fields.u32(95);
        return value;
    }
    if (name == "RAPIDRTRENDERENVIRONMENT" || name == "IBL_BACKGROUND") {
        ClassFields fields(subclass_fields("AcDbIBLBackground"));
        IblBackground value;
        value.class_version = fields.i32(90);
        value.enabled = fields.flag(290);
        value.name = fields.text(1);
        value.rotation = fields.real(40);
        value.display_image = fields.flag(290);
        value.secondary_background = fields.handle(340);
        return value;
    }
    if (name == "IMAGE_BACKGROUND") {
        ClassFields fields(subclass_fields("AcDbImageBackground"));
        ImageBackground value;
        value.class_version = fields.i32(90);
        value.filename = fields.text(300);
        value.fit_to_screen = fields.flag(290);
        value.maintain_aspect_ratio = fields.flag(291);
        value.use_tiling = fields.flag(292);
        value.offset = {fields.real(140), fields.real(141)};
        value.scale = {fields.real(142), fields.real(143)};
        return value;
    }
    if (name == "SKYLIGHT_BACKGROUND") {
        ClassFields fields(subclass_fields("AcDbSkyBackground"));
        SkyLightBackground value;
        value.class_version = fields.i32(90);
        value.sun = fields.handle(340);
        return value;
    }
    if (name == "SOLID_BACKGROUND") {
        ClassFields fields(subclass_fields("AcDbSolidBackground"));
        SolidBackground value;
        value.class_version = fields.i32(90);
        value.color = fields.u32(90);
        return value;
    }
    if (name == "RENDERENTRY") {
        ClassFields fields(subclass_fields("AcDbRenderEntry"));
        RenderEntry value;
        value.class_version = fields.i32(90);
        value.image_filename = fields.text(1);
        value.preset_name = fields.text(1);
        value.view_name = fields.text(1);
        value.width = fields.i32(90);
        value.height = fields.i32(90);
        value.start_year = fields.i16(70);
        value.start_month = fields.i16(70);
        value.start_day = fields.i16(70);
        value.start_hour = fields.i16(70);
        value.start_minute = fields.i16(70);
        value.start_second = fields.i16(70);
        value.start_millisecond = fields.i16(70);
        value.end_year = fields.i16(70);
        value.end_month = fields.i16(70);
        value.end_day = fields.i16(70);
        value.end_hour = fields.i16(70);
        value.end_minute = fields.i16(70);
        value.end_second = fields.i16(70);
        value.end_millisecond = fields.i16(70);
        value.render_time = fields.real(40);
        value.memory_amount = fields.i32(90);
        value.material_count = fields.i32(90);
        value.light_count = fields.i32(90);
        value.triangle_count = fields.i32(90);
        value.display_index = fields.i32(90);
        return value;
    }
    if (name == "RENDERENVIRONMENT") {
        ClassFields fields(subclass_fields("AcDbRenderEnvironment"));
        RenderEnvironment value;
        value.class_version = fields.i32(90);
        value.fog_enabled = fields.flag(290);
        value.fog_background_enabled = fields.flag(290);
        value.fog_color = {fields.u8(280), fields.u8(280), fields.u8(280)};
        value.fog_density_near = fields.real(40);
        value.fog_density_far = fields.real(40);
        value.fog_distance_near = fields.real(40);
        value.fog_distance_far = fields.real(40);
        value.environment_image_enabled = fields.flag(290);
        value.environment_image_filename = fields.text(1);
        return value;
    }
    if (name == "RENDERGLOBAL") {
        ClassFields fields(subclass_fields("AcDbRenderGlobal"));
        RenderGlobal value;
        value.class_version = fields.i32(90);
        value.procedure = fields.i32(90);
        value.destination = fields.i32(90);
        value.save_enabled = fields.flag(290);
        value.save_filename = fields.text(1);
        value.image_width = fields.i32(90);
        value.image_height = fields.i32(90);
        value.predefined_presets_first = fields.flag(290);
        value.high_level_info = fields.flag(290);
        return value;
    }
    if (name == "LIGHTLIST") {
        ClassFields fields(subclass_fields("AcDbLightList"));
        LightList value;
        value.class_version = fields.i32(90);
        auto count = fields.i32(90);
        if (count < 0 || count > 100000)
            throw std::invalid_argument("Invalid light list count");
        value.lights.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
            value.lights.push_back({fields.handle(5), fields.text(1)});
        return value;
    }
    if (name == "SUN") {
        ClassFields fields(subclass_fields("AcDbSun"));
        Sun value;
        value.class_version = fields.i32(90);
        value.is_on = fields.flag(290);
        value.color = fields.aci(63);
        value.intensity = fields.real(40);
        value.has_shadow = fields.flag(291);
        value.julian_day = fields.i32(91);
        value.milliseconds = fields.i32(92);
        value.is_daylight_savings_on = fields.flag(292);
        value.shadow_type = fields.i32(70);
        value.shadow_map_size = fields.i16(71);
        value.shadow_softness = fields.u8(280);
        return value;
    }
    if (name == "ACDBMOTIONPATH" || name == "MOTIONPATH") {
        ClassFields fields(subclass_fields("AcDbMotionPath"));
        MotionPath value;
        value.class_version = fields.i32(90);
        value.camera_path = fields.handle(340);
        value.target_path = fields.handle(340);
        value.view = fields.handle(340);
        value.frames = fields.i16(90);
        value.frame_rate = fields.i16(90);
        value.corner_deceleration = fields.flag(290);
        return value;
    }
    if (name == "ACDBCURVEPATH" || name == "CURVEPATH") {
        ClassFields fields(subclass_fields("AcDbCurvePath"));
        CurvePath value;
        value.class_version = fields.i32(90);
        value.entity = fields.handle(340);
        return value;
    }
    if (name == "ACDBPOINTPATH" || name == "POINTPATH") {
        ClassFields fields(subclass_fields("AcDbPointPath"));
        PointPath value;
        value.class_version = fields.i16(90);
        value.point = fields.p3(10);
        return value;
    }
    if (name == "TVDEVICEPROPERTIES") {
        ClassFields fields(subclass_fields("AcDbTvDeviceProperties"));
        TvDeviceProperties value;
        value.flags = fields.u32(90);
        value.max_regen_threads = fields.i16(70);
        value.use_lut_palette = fields.i32(91);
        value.alternate_highlight = fields.i64(160);
        value.alternate_highlight_color = fields.i64(161);
        value.geometry_shader_usage = fields.i64(162);
        value.blending_mode = fields.i32(92);
        value.antialiasing_level = fields.real(40);
        value.reserved_double = fields.real(41);
        return value;
    }
    if (name == "LAYERFILTER") {
        ClassFields fields(subclass_fields("AcDbLayerFilter"));
        LayerFilter value;
        auto count = fields.i32(90);
        if (count < 0 || count > 100000)
            throw std::invalid_argument("Invalid layer filter count");
        value.names.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
            value.names.push_back(fields.text(8));
        return value;
    }
    if (name == "PARTIAL_VIEWING_INDEX") {
        ClassFields fields(subclass_fields("OdDbPartialViewingIndex"));
        PartialViewingIndex value;
        auto count = fields.i32(90);
        if (count < 0 || count > 100000)
            throw std::invalid_argument("Invalid partial viewing index count");
        value.has_entries = fields.flag(290);
        value.entries.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
            value.entries.push_back({fields.p3(10), fields.p3(11), fields.handle(330)});
        return value;
    }
    if (name == "SUNSTUDY") {
        ClassFields fields(subclass_fields("AcDbSunStudy"));
        SunStudy value;
        value.class_version = fields.i32(90);
        value.setup_name = fields.text(1);
        value.description = fields.text(2);
        value.output_type = fields.i32(70);
        if (value.output_type == 0) {
            value.use_subset = fields.flag(290);
            value.sheet_set_name = fields.text(3);
            value.sheet_subset_name = fields.text(4);
        }
        value.select_dates_from_calendar = fields.flag(291);
        auto date_count = fields.i32(91);
        if (date_count < 0 || date_count > 10000)
            throw std::invalid_argument("Invalid sun study date count");
        value.dates.reserve(static_cast<std::size_t>(date_count));
        for (std::int32_t i = 0; i < date_count; ++i)
            value.dates.push_back({fields.i32(90), fields.i32(90)});
        value.select_range_of_dates = fields.flag(292);
        value.start_time = fields.i32(93);
        value.end_time = fields.i32(94);
        value.interval = fields.i32(95);
        auto hour_count = fields.i32(91);
        if (hour_count < 0 || hour_count > 10000)
            throw std::invalid_argument("Invalid sun study hour count");
        value.hours.reserve(static_cast<std::size_t>(hour_count));
        for (std::int32_t i = 0; i < hour_count; ++i)
            value.hours.push_back(fields.flag(290));
        value.shade_plot_type = fields.i32(74);
        value.viewport_count = fields.i32(75);
        value.rows = fields.i32(76);
        value.columns = fields.i32(77);
        value.spacing = fields.real(40);
        value.lock_viewports = fields.flag(293);
        value.label_viewports = fields.flag(294);
        value.page_setup_wizard = fields.handle(340);
        value.view = fields.handle(341);
        value.visual_style = fields.handle(342);
        value.text_style = fields.handle(343);
        return value;
    }
    if (name == "DATATABLE" || name == "ACDBDATATABLE") {
        ClassFields fields(subclass_fields("AcDbDataTable"));
        DataTable value;
        value.flags = fields.i16(70);
        auto column_count = fields.i32(90);
        value.row_count = fields.i32(91);
        if (column_count < 0 || column_count > 100000 || value.row_count < 0 || value.row_count > 100000)
            throw std::invalid_argument("Invalid data table count");
        value.name = fields.text(1);
        value.columns.reserve(static_cast<std::size_t>(column_count));
        for (std::int32_t i = 0; i < column_count; ++i) {
            DataTableColumn column;
            column.value_type = fields.i32(92);
            if (!data_table_has_native_codec(column.value_type))
                throw std::invalid_argument("Invalid data table column");
            column.name = fields.text(2);
            column.rows.reserve(static_cast<std::size_t>(value.row_count));
            for (std::int32_t j = 0; j < value.row_count; ++j) {
                DataTableValue cell;
                switch (static_cast<DataTableCellType>(column.value_type)) {
                case DataTableCellType::Integer:
                    cell.integer = fields.i32(93);
                    break;
                case DataTableCellType::Double:
                    cell.real = fields.real(40);
                    break;
                case DataTableCellType::Text:
                    cell.text = fields.text(3);
                    break;
                case DataTableCellType::Point:
                    cell.point = fields.p3(10);
                    break;
                case DataTableCellType::ObjectId:
                    cell.handle = fields.handle(331);
                    break;
                }
                column.rows.push_back(std::move(cell));
            }
            value.columns.push_back(std::move(column));
        }
        return value;
    }
    if (name == "GEOMAPIMAGE") {
        ClassFields fields(subclass_fields("AcDbGeomapImage"));
        GeoMapImage value;
        value.class_version = fields.i32(90);
        value.origin = fields.p3(10);
        value.image_size = fields.p2(13);
        value.display_properties = fields.i16(70);
        value.clipping_enabled = fields.flag(280);
        value.brightness = fields.u8(281);
        value.contrast = fields.u8(282);
        value.fade = fields.u8(283);
        return value;
    }
    if (name == "ACDBDETAILVIEWSTYLE" || name == "DETAILVIEWSTYLE") {
        ClassFields base(subclass_fields("AcDbModelDocViewStyle"));
        ClassFields fields(subclass_fields("AcDbDetailViewStyle"));
        DetailViewStyle value;
        value.base = read_model_doc_view_style(base);
        value.class_version = fields.i16(70);
        (void)fields.i16(71);
        value.flags = fields.i32(90);
        (void)fields.i16(71);
        value.identifier_style = fields.handle(340);
        value.identifier_color = fields.aci(62);
        value.identifier_height = fields.real(40);
        value.arrow_symbol = fields.handle(340);
        value.arrow_symbol_color = fields.aci(62);
        value.arrow_symbol_size = fields.real(40);
        value.identifier_excluded_characters = fields.text(300);
        value.identifier_offset = fields.real(40);
        value.identifier_placement = fields.u8(280);
        (void)fields.i16(71);
        value.boundary_linetype = fields.handle(340);
        value.boundary_lineweight = fields.i32(90);
        value.boundary_color = fields.aci(62);
        (void)fields.i16(71);
        value.view_label_text_style = fields.handle(340);
        value.view_label_text_color = fields.aci(62);
        value.view_label_text_height = fields.real(40);
        value.view_label_attachment = fields.i32(90);
        value.view_label_offset = fields.real(40);
        value.view_label_alignment = fields.i32(90);
        value.view_label_pattern = fields.text(300);
        (void)fields.i16(71);
        value.connection_linetype = fields.handle(340);
        value.connection_lineweight = fields.i32(90);
        value.connection_color = fields.aci(62);
        value.border_linetype = fields.handle(340);
        value.border_lineweight = fields.i32(90);
        value.border_color = fields.aci(62);
        value.model_edge = fields.u8(280);
        return value;
    }
    if (name == "ACDBSECTIONVIEWSTYLE" || name == "SECTIONVIEWSTYLE") {
        ClassFields base(subclass_fields("AcDbModelDocViewStyle"));
        ClassFields fields(subclass_fields("AcDbSectionViewStyle"));
        SectionViewStyle value;
        value.base = read_model_doc_view_style(base);
        value.class_version = fields.i16(70);
        (void)fields.i16(71);
        value.flags = fields.i32(90);
        (void)fields.i16(71);
        value.identifier_style = fields.handle(340);
        value.identifier_color = fields.aci(62);
        value.identifier_height = fields.real(40);
        value.arrow_start_symbol = fields.handle(340);
        value.arrow_end_symbol = fields.handle(340);
        value.arrow_symbol_color = fields.aci(62);
        value.arrow_symbol_size = fields.real(40);
        value.identifier_excluded_characters = fields.text(300);
        value.arrow_symbol_extension_length = fields.real(40);
        value.identifier_position = fields.i32(90);
        value.identifier_offset = fields.real(40);
        value.arrow_position = fields.i32(90);
        (void)fields.i16(71);
        value.plane_linetype = fields.handle(340);
        value.plane_lineweight = fields.i32(90);
        value.plane_color = fields.aci(62);
        value.bend_linetype = fields.handle(340);
        value.bend_lineweight = fields.i32(90);
        value.bend_color = fields.aci(62);
        value.bend_line_length = fields.real(40);
        value.end_line_overshoot = fields.real(40);
        value.end_line_length = fields.real(40);
        (void)fields.i16(71);
        value.view_label_text_style = fields.handle(340);
        value.view_label_text_color = fields.aci(62);
        value.view_label_text_height = fields.real(40);
        value.view_label_attachment = fields.i32(90);
        value.view_label_offset = fields.real(40);
        value.view_label_alignment = fields.i32(90);
        value.view_label_pattern = fields.text(300);
        (void)fields.i16(71);
        value.hatch_color = fields.aci(62);
        value.hatch_background_color = fields.aci(62);
        value.hatch_pattern = fields.text(300);
        value.hatch_scale = fields.real(40);
        value.hatch_transparency = fields.i32(90);
        value.reserved_flags[0] = fields.flag(290);
        value.reserved_flags[1] = fields.flag(290);
        auto angle_count = fields.i32(90);
        require_hatch_angles(angle_count);
        value.hatch_angles.reserve(static_cast<std::size_t>(angle_count));
        for (std::int32_t i = 0; i < angle_count; ++i)
            value.hatch_angles.push_back(fields.real(40));
        return value;
    }
    if (name == "IRD_DSC_DICT") {
        ClassFields fields(subclass_fields("AcDbDictionary"));
        ObjectDataDictionary value;
        value.hard_owner = fields.flag(280);
        value.duplicate_cloning = fields.has(281) ? fields.i16(281) : 1;
        auto names = fields.take_all(3);
        auto code = fields.has(350) ? 350 : 360;
        auto handles = fields.take_all(code);
        auto n = std::min(names.size(), handles.size());
        require_object_data_count(n);
        value.entries.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            value.entries.push_back({std::move(names[i]), parse_handle(handles[i]).value_or(0)});
        return value;
    }
    if (name == "IRD_DSC_RECORD") {
        ObjectDataDescriptor value;
        value.payload = object_data_payload_dxf(subclass_fields("CIrdBaseRecord"));
        ClassFields fields(subclass_fields("CIrdDscRecord"));
        value.kind = fields.i32(1071);
        value.name = fields.text(1000);
        return value;
    }
    if (name == "IRD_OBJ_RECORD") {
        ObjectDataRecord value;
        value.payload = object_data_payload_dxf(subclass_fields("CIrdBaseRecord"));
        value.table = ClassFields(subclass_fields("CIrdObjRecord")).handle(330);
        return value;
    }
    if (name == "ACDBVIEWREPSOURCEMGR") {
        ClassFields fields(subclass_fields("AcDbViewRepSourceMgr"));
        ViewRepSourceManager value;
        value.has_source = fields.flag(290);
        value.source = fields.handle(350);
        value.status = fields.i32(90);
        return value;
    }
    if (name == "ACDBVIEWREPSTANDARD") {
        ClassFields fields(subclass_fields("AcDbViewRepStandard"));
        ViewRepStandard value;
        value.values = {fields.i32(90), fields.i32(90), fields.i32(90),
                        fields.i32(90), fields.i32(90), fields.i32(90)};
        return value;
    }
    if (name == "ACDBVIEWREPORIENTATIONDEF")
        return ViewRepOrientationDefinition{};
    if (name == "ACDBVIEWREPORIENTATION") {
        ClassFields fields(subclass_fields("AcDbViewRepOrientation"));
        ViewRepOrientation value;
        value.camera = fields.p3(10);
        value.target = fields.p3(10);
        value.normal = fields.p3(210);
        return value;
    }
    if (name == "ACDBVIEWREPSECTIONDEFINITION") {
        ClassFields fields(subclass_fields("AcDbViewRepSectionDefinition"));
        ViewRepSectionDefinition value;
        value.version = fields.i32(90);
        value.section_depth = fields.real(40);
        value.flags = {fields.i32(90), fields.i32(90)};
        return value;
    }
    if (name == "ACDBSYMODELSPACEVIEWSELSET") {
        ClassFields fields(subclass_fields("AcDbViewRepModelSpaceViewSelSet"));
        ViewRepModelSpaceViewSelectionSet value;
        value.version = fields.i32(90);
        auto count = fields.i32(90);
        require_view_rep_count(count);
        value.entities.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i)
            value.entities.push_back(fields.handle(330));
        return value;
    }
    if (name == "ACDBVIEWREPMODELSPACESOURCE") {
        ClassFields fields(subclass_fields("AcDbViewRepModelSpaceSource"));
        ViewRepModelSpaceSource value;
        value.enabled = fields.flag(290);
        value.header_values = {fields.i32(90), fields.i32(90), fields.i32(90),
                               fields.i32(90), fields.i32(90), fields.i32(90)};
        for (auto &item : value.transform)
            item = fields.real(40);
        value.source_version = fields.i32(90);
        value.source_status = fields.i32(90);
        value.model = fields.handle(5);
        value.guid = read_view_rep_guid(fields);
        value.references = {fields.handle(330), fields.handle(330)};
        value.tail_values = {fields.i32(90), fields.i32(90)};
        value.orientation = fields.handle(350);
        return value;
    }
    if (name == "ACDBVIEWREP") {
        ClassFields fields(subclass_fields("AcDbViewRep"));
        return read_view_rep_dxf(fields);
    }
    if (name == "DATALINK") {
        auto ordered = subclass_fields("AcDbDataLink");
        ClassFields fields(ordered);
        DataLink value;
        value.data_adapter = fields.text(1);
        value.description = fields.text(300);
        value.tooltip = fields.text(301);
        value.connection_string = fields.text(302);
        value.option = fields.i32(90);
        value.update_option = fields.i32(91);
        value.flags = fields.i32(92);
        value.year = fields.i16(170);
        value.month = fields.i16(171);
        value.day = fields.i16(172);
        value.hour = fields.i16(173);
        value.minute = fields.i16(174);
        value.second = fields.i16(175);
        value.millisecond = fields.i16(176);
        value.path_option = fields.i16(177);
        value.status_flags = fields.i32(93);
        value.update_status = fields.text(304);
        auto custom_count = fields.i32(94);
        require_data_link_count(custom_count);
        value.custom_data.reserve(static_cast<std::size_t>(custom_count));
        for (std::int32_t i = 0; i < custom_count; ++i)
            value.custom_data.push_back({fields.handle(330), fields.text(304)});
        value.data_map = data_map_from_pairs(ordered);
        require_data_link_count(static_cast<std::int32_t>(value.data_map.size()));
        value.hard_owner = fields.handle(360);
        return value;
    }
    if (name != "SECTION_SETTINGS")
        throw std::invalid_argument("Unsupported class object");
    ClassFields fields(subclass_fields("AcDbSectionSettings"));
    SectionSettings value;
    value.current_type = fields.i32(90);
    auto type_count = fields.i32(91);
    if (type_count < 0 || type_count > 4)
        throw std::invalid_argument("Invalid section settings count");
    value.types.reserve(static_cast<std::size_t>(type_count));
    for (std::int32_t i = 0; i < type_count; ++i) {
        SectionTypeSettings type;
        type.section_type = fields.i32(90);
        type.generation = fields.i32(91);
        auto source_count = fields.i32(92);
        if (source_count < 0 || source_count > 100000)
            throw std::invalid_argument("Invalid section settings source count");
        type.sources.reserve(static_cast<std::size_t>(source_count));
        for (std::int32_t j = 0; j < source_count; ++j)
            type.sources.push_back(fields.handle(330));
        type.destination_block = fields.handle(331);
        type.destination_file = fields.text_skipping(1, "SectionTypeSettings");
        auto geometry_count = fields.i32(93);
        if (geometry_count < 0 || geometry_count > 100000)
            throw std::invalid_argument("Invalid section geometry count");
        type.geometry.reserve(static_cast<std::size_t>(geometry_count));
        for (std::int32_t j = 0; j < geometry_count; ++j) {
            SectionGeometrySettings geometry;
            geometry.geometry_count = fields.i32(90);
            geometry.index = fields.i32(91);
            geometry.flags = fields.i32(92);
            geometry.color = fields.aci(62);
            geometry.layer = fields.text(8);
            geometry.linetype = fields.text(6);
            geometry.linetype_scale = fields.real(40);
            geometry.plot_style = fields.text(1);
            geometry.lineweight = fields.i32(370);
            geometry.face_transparency = fields.i16(70);
            geometry.edge_transparency = fields.i16(71);
            geometry.hatch_type = fields.i16(72);
            geometry.hatch_pattern = fields.text_skipping(2, "SectionGeometrySettings");
            geometry.hatch_angle = fields.real(41);
            geometry.hatch_spacing = fields.real(42);
            geometry.hatch_scale = fields.real(43);
            type.geometry.push_back(std::move(geometry));
        }
        value.types.push_back(std::move(type));
    }
    return value;
}
void DxfClassObject::set_definition(const ClassObjectData &value) {
    if (!class_object_name_matches(type(), value))
        throw std::invalid_argument("Class object type mismatch");
    if (auto *settings = std::get_if<RenderSettings>(&value)) {
        ClassWriter out;
        write_render_settings_dxf(out, *settings, true, version_);
        set_subclass_fields("AcDbRenderSettings", out.values);
        return;
    }
    if (auto *settings = std::get_if<MentalRayRenderSettings>(&value)) {
        ClassWriter base;
        write_render_settings_dxf(base, settings->base, false, version_);
        set_subclass_fields("AcDbRenderSettings", base.values);
        ClassWriter out;
        out.i(90, settings->version);
        out.i(90, settings->sampling_min);
        out.i(90, settings->sampling_max);
        out.i(70, settings->sampling_filter);
        out.r(40, settings->sampling_filter_width);
        out.r(40, settings->sampling_filter_height);
        for (auto component : settings->sampling_contrast)
            out.r(40, component);
        out.i(70, settings->shadow_mode);
        out.b(290, settings->shadow_maps_enabled);
        out.b(290, settings->ray_tracing_enabled);
        for (auto depth : settings->ray_trace_depth)
            out.i(90, depth);
        out.b(290, settings->global_illumination_enabled);
        out.i(90, settings->global_illumination_sample_count);
        out.b(290, settings->global_illumination_sample_radius_enabled);
        out.r(40, settings->global_illumination_sample_radius);
        out.i(90, settings->photons_per_light);
        for (auto depth : settings->photon_trace_depth)
            out.i(90, depth);
        out.b(290, settings->final_gathering_enabled);
        out.i(90, settings->final_gathering_ray_count);
        for (auto state : settings->final_gathering_sample_radius_state)
            out.b(290, state);
        for (auto radius : settings->final_gathering_sample_radius)
            out.r(40, radius);
        out.r(40, settings->light_luminance_scale);
        out.i(70, settings->diagnostics_mode);
        out.i(70, settings->diagnostics_grid_mode);
        out.r(40, settings->diagnostics_grid_size);
        out.i(70, settings->diagnostics_photon_mode);
        out.i(70, settings->diagnostics_bsp_mode);
        out.b(290, settings->export_mi_enabled);
        out.s(1, settings->description);
        out.i(90, settings->tile_size);
        out.i(70, settings->tile_order);
        out.i(90, settings->memory_limit);
        out.b(290, settings->diagnostics_samples_mode);
        out.r(40, settings->energy_multiplier);
        set_subclass_fields("AcDbMentalRayRenderSettings", out.values);
        return;
    }
    if (auto *settings = std::get_if<RapidRtRenderSettings>(&value)) {
        ClassWriter base;
        write_render_settings_dxf(base, settings->base, false, version_);
        set_subclass_fields("AcDbRenderSettings", base.values);
        ClassWriter out;
        out.i(90, settings->version);
        out.i(70, settings->render_target);
        out.i(90, settings->render_level);
        out.i(90, settings->render_time);
        out.i(70, settings->lighting_model);
        out.i(70, settings->filter_type);
        out.r(40, settings->filter_width);
        out.r(40, settings->filter_height);
        out.b(290, settings->base.has_predefined);
        set_subclass_fields("AcDbRapidRTRenderSettings", out.values);
        return;
    }
    if (auto *background = std::get_if<GradientBackground>(&value)) {
        ClassWriter out;
        out.i(90, background->class_version);
        out.u(90, background->color_top);
        out.u(91, background->color_middle);
        out.u(92, background->color_bottom);
        out.r(140, background->horizon);
        out.r(141, background->height);
        out.r(142, background->rotation);
        set_subclass_fields("AcDbGradientBackground", out.values);
        return;
    }
    if (auto *background = std::get_if<GroundPlaneBackground>(&value)) {
        ClassWriter out;
        out.i(90, background->class_version);
        out.u(90, background->color_sky_zenith);
        out.u(91, background->color_sky_horizon);
        out.u(92, background->color_underground_horizon);
        out.u(93, background->color_underground_azimuth);
        out.u(94, background->color_near);
        out.u(95, background->color_far);
        set_subclass_fields("AcDbGroundPlaneBackground", out.values);
        return;
    }
    if (auto *background = std::get_if<IblBackground>(&value)) {
        ClassWriter out;
        out.i(90, background->class_version);
        out.b(290, background->enabled);
        out.s(1, background->name);
        out.r(40, background->rotation);
        out.b(290, background->display_image);
        out.h(340, background->secondary_background);
        set_subclass_fields("AcDbIBLBackground", out.values);
        return;
    }
    if (auto *background = std::get_if<ImageBackground>(&value)) {
        ClassWriter out;
        out.i(90, background->class_version);
        out.s(300, background->filename);
        out.b(290, background->fit_to_screen);
        out.b(291, background->maintain_aspect_ratio);
        out.b(292, background->use_tiling);
        out.r(140, background->offset.x);
        out.r(141, background->offset.y);
        out.r(142, background->scale.x);
        out.r(143, background->scale.y);
        set_subclass_fields("AcDbImageBackground", out.values);
        return;
    }
    if (auto *background = std::get_if<SkyLightBackground>(&value)) {
        ClassWriter out;
        out.i(90, background->class_version);
        out.h(340, background->sun);
        set_subclass_fields("AcDbSkyBackground", out.values);
        return;
    }
    if (auto *background = std::get_if<SolidBackground>(&value)) {
        ClassWriter out;
        out.i(90, background->class_version);
        out.u(90, background->color);
        set_subclass_fields("AcDbSolidBackground", out.values);
        return;
    }
    if (auto *entry = std::get_if<RenderEntry>(&value)) {
        ClassWriter out;
        out.i(90, entry->class_version);
        out.s(1, entry->image_filename);
        out.s(1, entry->preset_name);
        out.s(1, entry->view_name);
        out.i(90, entry->width);
        out.i(90, entry->height);
        out.i(70, entry->start_year);
        out.i(70, entry->start_month);
        out.i(70, entry->start_day);
        out.i(70, entry->start_hour);
        out.i(70, entry->start_minute);
        out.i(70, entry->start_second);
        out.i(70, entry->start_millisecond);
        out.i(70, entry->end_year);
        out.i(70, entry->end_month);
        out.i(70, entry->end_day);
        out.i(70, entry->end_hour);
        out.i(70, entry->end_minute);
        out.i(70, entry->end_second);
        out.i(70, entry->end_millisecond);
        out.r(40, entry->render_time);
        out.i(90, entry->memory_amount);
        out.i(90, entry->material_count);
        out.i(90, entry->light_count);
        out.i(90, entry->triangle_count);
        out.i(90, entry->display_index);
        set_subclass_fields("AcDbRenderEntry", out.values);
        return;
    }
    if (auto *environment = std::get_if<RenderEnvironment>(&value)) {
        ClassWriter out;
        out.i(90, environment->class_version);
        out.b(290, environment->fog_enabled);
        out.b(290, environment->fog_background_enabled);
        for (auto component : environment->fog_color)
            out.u8(280, component);
        out.r(40, environment->fog_density_near);
        out.r(40, environment->fog_density_far);
        out.r(40, environment->fog_distance_near);
        out.r(40, environment->fog_distance_far);
        out.b(290, environment->environment_image_enabled);
        out.s(1, environment->environment_image_filename);
        set_subclass_fields("AcDbRenderEnvironment", out.values);
        return;
    }
    if (auto *global = std::get_if<RenderGlobal>(&value)) {
        ClassWriter out;
        out.i(90, global->class_version);
        out.i(90, global->procedure);
        out.i(90, global->destination);
        out.b(290, global->save_enabled);
        out.s(1, global->save_filename);
        out.i(90, global->image_width);
        out.i(90, global->image_height);
        out.b(290, global->predefined_presets_first);
        out.b(290, global->high_level_info);
        set_subclass_fields("AcDbRenderGlobal", out.values);
        return;
    }
    if (auto *lights = std::get_if<LightList>(&value)) {
        if (lights->lights.size() > 100000)
            throw std::invalid_argument("Invalid light list count");
        ClassWriter out;
        out.i(90, lights->class_version);
        out.i(90, static_cast<std::int32_t>(lights->lights.size()));
        for (const auto &light : lights->lights) {
            out.h(5, light.handle);
            out.s(1, light.name);
        }
        set_subclass_fields("AcDbLightList", out.values);
        return;
    }
    if (auto *sun = std::get_if<Sun>(&value)) {
        ClassWriter out;
        out.i(90, sun->class_version);
        out.b(290, sun->is_on);
        out.color(63, sun->color);
        out.r(40, sun->intensity);
        out.b(291, sun->has_shadow);
        out.i(91, sun->julian_day);
        out.i(92, sun->milliseconds);
        out.b(292, sun->is_daylight_savings_on);
        out.i(70, sun->shadow_type);
        out.i(71, sun->shadow_map_size);
        out.u8(280, sun->shadow_softness);
        set_subclass_fields("AcDbSun", out.values);
        return;
    }
    if (auto *path = std::get_if<MotionPath>(&value)) {
        ClassWriter out;
        out.i(90, path->class_version);
        out.h(340, path->camera_path);
        out.h(340, path->target_path);
        out.h(340, path->view);
        out.i(90, path->frames);
        out.i(90, path->frame_rate);
        out.b(290, path->corner_deceleration);
        set_subclass_fields("AcDbMotionPath", out.values);
        return;
    }
    if (auto *path = std::get_if<CurvePath>(&value)) {
        ClassWriter out;
        out.i(90, path->class_version);
        out.h(340, path->entity);
        set_subclass_fields("AcDbCurvePath", out.values);
        return;
    }
    if (auto *path = std::get_if<PointPath>(&value)) {
        ClassWriter out;
        out.i(90, path->class_version);
        out.p3(10, path->point);
        set_subclass_fields("AcDbPointPath", out.values);
        return;
    }
    if (auto *device = std::get_if<TvDeviceProperties>(&value)) {
        ClassWriter out;
        out.u(90, device->flags);
        out.i(70, device->max_regen_threads);
        out.i(91, device->use_lut_palette);
        out.i64(160, device->alternate_highlight);
        out.i64(161, device->alternate_highlight_color);
        out.i64(162, device->geometry_shader_usage);
        out.i(92, device->blending_mode);
        out.r(40, device->antialiasing_level);
        out.r(41, device->reserved_double);
        set_subclass_fields("AcDbTvDeviceProperties", out.values);
        return;
    }
    if (auto *filter = std::get_if<LayerFilter>(&value)) {
        if (filter->names.size() > 100000)
            throw std::invalid_argument("Invalid layer filter count");
        ClassWriter out;
        out.i(90, static_cast<std::int32_t>(filter->names.size()));
        for (const auto &name : filter->names)
            out.s(8, name);
        set_subclass_fields("AcDbLayerFilter", out.values);
        return;
    }
    if (auto *index = std::get_if<PartialViewingIndex>(&value)) {
        if (index->entries.size() > 100000)
            throw std::invalid_argument("Invalid partial viewing index count");
        ClassWriter out;
        out.i(90, static_cast<std::int32_t>(index->entries.size()));
        out.b(290, index->has_entries);
        for (const auto &entry : index->entries) {
            out.p3(10, entry.extents_min);
            out.p3(11, entry.extents_max);
            out.h(330, entry.object);
        }
        set_subclass_fields("OdDbPartialViewingIndex", out.values);
        return;
    }
    if (auto *settings = std::get_if<SectionSettings>(&value)) {
        if (settings->types.size() > 4)
            throw std::invalid_argument("Invalid section settings count");
        ClassWriter out;
        out.i(90, settings->current_type);
        out.i(91, static_cast<std::int32_t>(settings->types.size()));
        for (const auto &type : settings->types) {
            if (type.sources.size() > 100000 || type.geometry.size() > 100000)
                throw std::invalid_argument("Invalid section settings source count");
            out.s(1, "SectionTypeSettings");
            out.i(90, type.section_type);
            out.i(91, type.generation);
            out.i(92, static_cast<std::int32_t>(type.sources.size()));
            for (auto source : type.sources)
                out.h(330, source);
            out.h(331, type.destination_block);
            out.s(1, type.destination_file);
            out.i(93, static_cast<std::int32_t>(type.geometry.size()));
            for (const auto &geometry : type.geometry) {
                out.s(2, "SectionGeometrySettings");
                out.i(90, geometry.geometry_count);
                out.i(91, geometry.index);
                out.i(92, geometry.flags);
                out.color(62, geometry.color);
                out.s(8, geometry.layer);
                out.s(6, geometry.linetype);
                out.r(40, geometry.linetype_scale);
                out.s(1, geometry.plot_style);
                out.i(370, geometry.lineweight);
                out.i(70, geometry.face_transparency);
                out.i(71, geometry.edge_transparency);
                out.i(72, geometry.hatch_type);
                out.s(2, geometry.hatch_pattern);
                out.r(41, geometry.hatch_angle);
                out.r(42, geometry.hatch_spacing);
                out.r(43, geometry.hatch_scale);
                out.s(3, "SectionGeometrySettingsEnd");
            }
            out.s(3, "SectionTypeSettingsEnd");
        }
        set_subclass_fields("AcDbSectionSettings", out.values);
        return;
    }
    if (auto *study = std::get_if<SunStudy>(&value)) {
        if (study->dates.size() > 10000 || study->hours.size() > 10000)
            throw std::invalid_argument("Invalid sun study date count");
        ClassWriter out;
        out.i(90, study->class_version);
        out.s(1, study->setup_name);
        out.s(2, study->description);
        out.i(70, study->output_type);
        if (study->output_type == 0) {
            out.b(290, study->use_subset);
            out.s(3, study->sheet_set_name);
            out.s(4, study->sheet_subset_name);
        }
        out.b(291, study->select_dates_from_calendar);
        out.i(91, static_cast<std::int32_t>(study->dates.size()));
        for (const auto &date : study->dates) {
            out.i(90, date.julian_day);
            out.i(90, date.milliseconds);
        }
        out.b(292, study->select_range_of_dates);
        if (study->select_range_of_dates) {
            out.i(93, study->start_time);
            out.i(94, study->end_time);
            out.i(95, study->interval);
        }
        out.i(91, static_cast<std::int32_t>(study->hours.size()));
        for (bool hour : study->hours)
            out.b(290, hour);
        out.i(74, study->shade_plot_type);
        out.i(75, study->viewport_count);
        out.i(76, study->rows);
        out.i(77, study->columns);
        out.r(40, study->spacing);
        out.b(293, study->lock_viewports);
        out.b(294, study->label_viewports);
        out.h(340, study->page_setup_wizard);
        out.h(341, study->view);
        out.h(342, study->visual_style);
        out.h(343, study->text_style);
        set_subclass_fields("AcDbSunStudy", out.values);
        return;
    }
    if (auto *table = std::get_if<DataTable>(&value)) {
        require_data_table(*table);
        ClassWriter out;
        out.i(70, table->flags);
        out.i(90, static_cast<std::int32_t>(table->columns.size()));
        out.i(91, table->row_count);
        out.s(1, table->name);
        for (const auto &column : table->columns) {
            out.i(92, column.value_type);
            out.s(2, column.name);
            for (const auto &row : column.rows) {
                switch (static_cast<DataTableCellType>(column.value_type)) {
                case DataTableCellType::Integer:
                    out.i(93, row.integer);
                    break;
                case DataTableCellType::Double:
                    out.r(40, row.real);
                    break;
                case DataTableCellType::Text:
                    out.s(3, row.text);
                    break;
                case DataTableCellType::Point:
                    out.p3(10, row.point);
                    break;
                case DataTableCellType::ObjectId:
                    out.h(331, row.handle);
                    break;
                }
            }
        }
        set_subclass_fields("AcDbDataTable", out.values);
        return;
    }
    if (auto *image = std::get_if<GeoMapImage>(&value)) {
        ClassWriter out;
        out.i(90, image->class_version);
        out.p3(10, image->origin);
        out.p2(13, image->image_size);
        out.i(70, image->display_properties);
        out.b(280, image->clipping_enabled);
        out.u8(281, image->brightness);
        out.u8(282, image->contrast);
        out.u8(283, image->fade);
        set_subclass_fields("AcDbGeomapImage", out.values);
        return;
    }
    if (auto *style = std::get_if<DetailViewStyle>(&value)) {
        ClassWriter base;
        write_model_doc_view_style(base, style->base, version_);
        set_subclass_fields("AcDbModelDocViewStyle", base.values);
        ClassWriter out;
        out.i(70, style->class_version);
        out.i(71, 0);
        out.i(90, style->flags);
        out.i(71, 1);
        out.h(340, style->identifier_style);
        out.color(62, style->identifier_color);
        out.r(40, style->identifier_height);
        out.h(340, style->arrow_symbol);
        out.color(62, style->arrow_symbol_color);
        out.r(40, style->arrow_symbol_size);
        out.s(300, style->identifier_excluded_characters);
        out.r(40, style->identifier_offset);
        out.u8(280, style->identifier_placement);
        out.i(71, 2);
        out.h(340, style->boundary_linetype);
        out.i(90, style->boundary_lineweight);
        out.color(62, style->boundary_color);
        out.i(71, 3);
        out.h(340, style->view_label_text_style);
        out.color(62, style->view_label_text_color);
        out.r(40, style->view_label_text_height);
        out.i(90, style->view_label_attachment);
        out.r(40, style->view_label_offset);
        out.i(90, style->view_label_alignment);
        out.s(300, style->view_label_pattern);
        out.i(71, 4);
        out.h(340, style->connection_linetype);
        out.i(90, style->connection_lineweight);
        out.color(62, style->connection_color);
        out.h(340, style->border_linetype);
        out.i(90, style->border_lineweight);
        out.color(62, style->border_color);
        out.u8(280, style->model_edge);
        set_subclass_fields("AcDbDetailViewStyle", out.values);
        return;
    }
    if (auto *style = std::get_if<SectionViewStyle>(&value)) {
        require_hatch_angles(static_cast<std::int32_t>(style->hatch_angles.size()));
        ClassWriter base;
        write_model_doc_view_style(base, style->base, version_);
        set_subclass_fields("AcDbModelDocViewStyle", base.values);
        ClassWriter out;
        out.i(70, style->class_version);
        out.i(71, 0);
        out.i(90, style->flags);
        out.i(71, 1);
        out.h(340, style->identifier_style);
        out.color(62, style->identifier_color);
        out.r(40, style->identifier_height);
        out.h(340, style->arrow_start_symbol);
        out.h(340, style->arrow_end_symbol);
        out.color(62, style->arrow_symbol_color);
        out.r(40, style->arrow_symbol_size);
        out.s(300, style->identifier_excluded_characters);
        out.r(40, style->arrow_symbol_extension_length);
        out.i(90, style->identifier_position);
        out.r(40, style->identifier_offset);
        out.i(90, style->arrow_position);
        out.i(71, 2);
        out.h(340, style->plane_linetype);
        out.i(90, style->plane_lineweight);
        out.color(62, style->plane_color);
        out.h(340, style->bend_linetype);
        out.i(90, style->bend_lineweight);
        out.color(62, style->bend_color);
        out.r(40, style->bend_line_length);
        out.r(40, style->end_line_overshoot);
        out.r(40, style->end_line_length);
        out.i(71, 3);
        out.h(340, style->view_label_text_style);
        out.color(62, style->view_label_text_color);
        out.r(40, style->view_label_text_height);
        out.i(90, style->view_label_attachment);
        out.r(40, style->view_label_offset);
        out.i(90, style->view_label_alignment);
        out.s(300, style->view_label_pattern);
        out.i(71, 4);
        out.color(62, style->hatch_color);
        out.color(62, style->hatch_background_color);
        out.s(300, style->hatch_pattern);
        out.r(40, style->hatch_scale);
        out.i(90, style->hatch_transparency);
        out.b(290, style->reserved_flags[0]);
        out.b(290, style->reserved_flags[1]);
        out.i(90, static_cast<std::int32_t>(style->hatch_angles.size()));
        for (auto angle : style->hatch_angles)
            out.r(40, angle);
        set_subclass_fields("AcDbSectionViewStyle", out.values);
        return;
    }
    if (auto *dictionary = std::get_if<ObjectDataDictionary>(&value)) {
        require_object_data_count(dictionary->entries.size());
        ClassWriter dict;
        dict.u8(280, dictionary->hard_owner);
        dict.u8(281, static_cast<std::uint8_t>(dictionary->duplicate_cloning));
        auto code = dictionary->hard_owner ? 360 : 350;
        for (const auto &entry : dictionary->entries) {
            dict.s(3, entry.first);
            dict.h(code, entry.second);
        }
        set_subclass_fields("AcDbDictionary", dict.values);
        ClassWriter extra;
        extra.r(1040, 0);
        set_subclass_fields("CIrdDscDict", extra.values, false, true);
        return;
    }
    if (auto *descriptor = std::get_if<ObjectDataDescriptor>(&value)) {
        ClassWriter base;
        base.i(1070, 516);
        base.i(1071, 0);
        base.bin(1004, descriptor->payload);
        set_subclass_fields("CIrdBaseRecord", base.values, false, true);
        ClassWriter rec;
        rec.i(1071, descriptor->kind);
        rec.s(1000, descriptor->name);
        rec.i(1071, 0);
        set_subclass_fields("CIrdDscRecord", rec.values, false, true);
        return;
    }
    if (auto *record = std::get_if<ObjectDataRecord>(&value)) {
        ClassWriter base;
        base.i(1070, 516);
        base.i(1071, 1);
        base.bin(1004, record->payload);
        set_subclass_fields("CIrdBaseRecord", base.values, false, true);
        ClassWriter rec;
        rec.h(330, record->table);
        set_subclass_fields("CIrdObjRecord", rec.values);
        return;
    }
    if (auto *manager = std::get_if<ViewRepSourceManager>(&value)) {
        ClassWriter out;
        out.b(290, manager->has_source);
        out.h(350, manager->source);
        out.i(90, manager->status);
        set_subclass_fields("AcDbViewRepSourceMgr", out.values);
        return;
    }
    if (auto *standard = std::get_if<ViewRepStandard>(&value)) {
        ClassWriter out;
        for (auto item : standard->values)
            out.i(90, item);
        set_subclass_fields("AcDbViewRepStandard", out.values);
        return;
    }
    if (std::holds_alternative<ViewRepOrientationDefinition>(value)) {
        set_subclass_fields("AcDbViewRepOrientationDef", {});
        return;
    }
    if (auto *orientation = std::get_if<ViewRepOrientation>(&value)) {
        require_finite_view_rep_vec(orientation->camera);
        require_finite_view_rep_vec(orientation->target);
        require_finite_view_rep_vec(orientation->normal);
        ClassWriter out;
        out.p3(10, orientation->camera);
        out.p3(10, orientation->target);
        out.p3(210, orientation->normal);
        set_subclass_fields("AcDbViewRepOrientation", out.values);
        return;
    }
    if (auto *section = std::get_if<ViewRepSectionDefinition>(&value)) {
        require_finite_view_rep(section->section_depth);
        set_subclass_fields("AcDbViewRepCutDefinition", {});
        ClassWriter out;
        out.i(90, section->version);
        out.r(40, section->section_depth);
        out.i(90, section->flags[0]);
        out.i(90, section->flags[1]);
        set_subclass_fields("AcDbViewRepSectionDefinition", out.values);
        return;
    }
    if (auto *set = std::get_if<ViewRepModelSpaceViewSelectionSet>(&value)) {
        require_view_rep_count(static_cast<std::int32_t>(set->entities.size()));
        ClassWriter out;
        out.i(90, set->version);
        out.i(90, static_cast<std::int32_t>(set->entities.size()));
        for (auto handle : set->entities)
            out.h(330, handle);
        set_subclass_fields("AcDbViewRepModelSpaceViewSelSet", out.values);
        return;
    }
    if (auto *source = std::get_if<ViewRepModelSpaceSource>(&value)) {
        for (auto item : source->transform)
            require_finite_view_rep(item);
        set_subclass_fields("AcDbViewRepSource", {});
        ClassWriter out;
        out.b(290, source->enabled);
        for (auto item : source->header_values)
            out.i(90, item);
        for (auto item : source->transform)
            out.r(40, item);
        out.i(90, source->source_version);
        out.i(90, source->source_status);
        out.h(5, source->model);
        write_view_rep_guid(out, source->guid);
        for (auto handle : source->references)
            out.h(330, handle);
        for (auto item : source->tail_values)
            out.i(90, item);
        out.h(350, source->orientation);
        set_subclass_fields("AcDbViewRepModelSpaceSource", out.values);
        return;
    }
    if (auto *view = std::get_if<ViewRep>(&value)) {
        ClassWriter out;
        write_view_rep_dxf(out, *view);
        set_subclass_fields("AcDbViewRep", out.values);
        return;
    }
    if (auto *link = std::get_if<DataLink>(&value)) {
        require_data_link_count(static_cast<std::int32_t>(link->custom_data.size()));
        require_data_link_count(static_cast<std::int32_t>(link->data_map.size()));
        ClassWriter out;
        out.s(1, link->data_adapter);
        out.s(300, link->description);
        out.s(301, link->tooltip);
        out.s(302, link->connection_string);
        out.i(90, link->option);
        out.i(91, link->update_option);
        out.i(92, link->flags);
        out.i(170, link->year);
        out.i(171, link->month);
        out.i(172, link->day);
        out.i(173, link->hour);
        out.i(174, link->minute);
        out.i(175, link->second);
        out.i(176, link->millisecond);
        out.i(177, link->path_option);
        out.i(93, link->status_flags);
        out.s(304, link->update_status);
        out.i(94, static_cast<std::int32_t>(link->custom_data.size()));
        for (const auto &item : link->custom_data)
            out.h(330, item.target);
        out.h(360, link->hard_owner);
        out.s(305, "CUSTOMDATA");
        out.s(1, "DATAMAP_BEGIN");
        out.i(90, static_cast<std::int32_t>(link->data_map.size()));
        for (const auto &item : link->data_map) {
            out.s(300, item.name);
            out.s(301, "DATAMAP_VALUE");
            write_data_link_value(out, item.value, version_);
            if (version_ >= DxfVersion::AC1021)
                out.s(304, "ACVALUE_END");
        }
        out.s(309, "DATAMAP_END");
        set_subclass_fields("AcDbDataLink", out.values);
        return;
    }
    auto manager = std::get<SectionManager>(value);
    if (manager.sections.size() > 100000)
        throw std::invalid_argument("Invalid section manager count");
    ClassWriter out;
    out.b(70, manager.is_live);
    out.i(90, static_cast<std::int32_t>(manager.sections.size()));
    for (auto section : manager.sections)
        out.h(330, section);
    set_subclass_fields("AcDbSectionManager", out.values);
}
std::uint64_t DxfDocument::add_class_object(std::string_view dictionary, std::string name,
                                            const ClassObjectData &value) {
    if (version_ < DxfVersion::AC1018)
        throw std::invalid_argument("Class object construction requires R2004+");
    auto type = class_object_dxf_name(value);
    DxfClassObject object;
    object.version_ = version_;
    static_cast<DxfRecord &>(object) = make_record(type);
    for (const auto &marker : class_object_subclasses(type))
        object.pairs_.push_back({100, marker});
    object.set_definition(value);
    return add_dictionary_object(dictionary, std::move(name), object);
}
