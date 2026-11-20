// SPDX-License-Identifier: MPL-2.0
ShapeDefinition DxfShape::definition() const {
    MLFields f(subclass_fields("AcDbShape"));
    ShapeDefinition v;
    v.name = f.s(2, {});
    v.insertion = f.p(10, {});
    v.normal = f.p(210, {0, 0, 1});
    v.size = f.r(40, 1);
    v.rotation = f.r(50, 0) * radians_per_degree;
    v.oblique_angle = f.r(51, 0) * radians_per_degree;
    v.relative_x_scale = f.r(41, 1);
    v.thickness = f.r(39, 0);
    v.extra_fields = f.extras();
    return v;
}
void DxfShape::set_definition(const ShapeDefinition &v) {
    MLFields f;
    f.put_p(10, v.insertion);
    f.put_p(210, v.normal);
    f.put_s(2, v.name);
    f.put_r(40, v.size);
    f.put_r(50, v.rotation * degrees_per_radian);
    f.put_r(51, v.oblique_angle * degrees_per_radian);
    f.put_r(41, v.relative_x_scale);
    f.put_r(39, v.thickness);
    mt_append(f, v.extra_fields);
    set_subclass_fields("AcDbShape", f.values);
}
std::uint64_t DxfDocument::add_shape(const ShapeDefinition &v, const EntityOptions &options) {
    if (version_ < DxfVersion::AC1015)
        throw std::invalid_argument("SHAPE construction requires R2000+");
    DxfDocument staged = *this;
    DxfShape shape;
    static_cast<DxfEntity &>(shape) = staged.make_entity("SHAPE", "AcDbShape", options);
    shape.set_definition(v);
    auto id = staged.insert_entity(shape, options);
    *this = std::move(staged);
    return id;
}
std::uint64_t DxfDocument::add_shape_file_style(const TextStyleDefinition &v) {
    if (v.font_file.empty())
        throw std::invalid_argument("Shape-file STYLE needs a font file");
    for (const auto &entry : symbols(SymbolTable::TextStyle)) {
        const auto value = get_text_style_by_handle(entry.handle);
        if (value && (value->flags & 1) &&
            detail::symbol_name_key(value->font_file) == detail::symbol_name_key(v.font_file))
            throw std::invalid_argument("Shape-file STYLE already exists");
    }
    DxfDocument staged = *this;
    auto entry = staged.make_record("STYLE", "AcDbTextStyleTableRecord");
    entry.set_text_field("AcDbTextStyleTableRecord", 2, "");
    entry.set_short_field("AcDbTextStyleTableRecord", 70, 1);
    auto id = staged.insert_table_entry("STYLE", entry);
    auto value = v;
    value.flags |= 1;
    staged.set_text_style_by_handle(hex_handle(id), value);
    *this = std::move(staged);
    return id;
}
double DxfLine::thickness() const { return number("AcDbLine", 39, 0); }
Point3 DxfLine::normal() const { return point_or("AcDbLine", 210, {0, 0, 1}); }
void DxfLine::set_thickness(double value) { set_number("AcDbLine", 39, value); }
void DxfLine::set_normal(Point3 value) { set_point("AcDbLine", 210, value); }
EntityDisplayProperties DxfEntity::display_properties() const {
    MLFields f(subclass_fields("AcDbEntity"));
    EntityDisplayProperties v;
    v.linetype_scale = f.r(48, 1);
    v.transparency = Transparency::from_alpha_value(f.i(440, 0));
    v.color_name = f.s(430, {});
    v.material = f.h(347, 0);
    v.material_flags = material_flags_;
    v.shadow_flags = f.i(284, 0);
    v.plot_style = f.h(390, 0);
    v.plot_style_mode = v.plot_style ? 3 : f.e(380, 0, 0, 2);
    for (auto n : f.take(348)) {
        if (auto h = parse_handle(f.values[n].value))
            v.visual_styles.push_back(*h);
    }
    return v;
}
void DxfEntity::set_display_properties(const EntityDisplayProperties &v) {
    if (v.visual_styles.size() > 3 || v.plot_style_mode > 3 || v.material_flags > 3)
        throw std::invalid_argument("Invalid display properties");
    material_flags_ = v.material_flags;
    if (version_ < DxfVersion::AC1024)
        throw std::invalid_argument("Full display property edits require R2010+");
    MLFields f;
    f.put_r(48, v.linetype_scale);
    f.put_i(440, v.transparency.to_dxf_value());
    if (!v.color_name.empty())
        f.put_s(430, v.color_name);
    f.put_h(347, v.material);
    f.put_i(284, v.shadow_flags);
    if (!v.plot_style && (v.plot_style_mode == 1 || v.plot_style_mode == 2))
        f.put_i(380, v.plot_style_mode);
    f.put_h(390, v.plot_style);
    for (auto h : v.visual_styles)
        f.put_s(348, hex_handle(h));
    std::vector<Pair> replacement;
    for (const auto &p : f.values)
        replacement.push_back(
            {p.code, detail::is_string_value(p.code)
                         ? encode_string(p.value)
                         : (binary_ ? detail::binary_value_from_text(p.code, p.value) : p.value)});
    replace_common_fields({48, 440, 430, 347, 284, 390, 380, 348}, std::move(replacement));
}
void DxfDocument::set_entity_display_properties(std::string_view handle, const EntityDisplayProperties &v) {
    mleader_check_handle(*this, v.material, "MATERIAL");
    mleader_check_handle(*this, v.plot_style, "ACDBPLACEHOLDER");
    for (auto h : v.visual_styles)
        mleader_check_handle(*this, h, "VISUALSTYLE");
    if (records_with_handle(handle).size() != 1)
        throw std::invalid_argument("Expected unique entity");
    DxfDocument staged = *this;
    bool found = false;
    staged.for_each<DxfEntity>([&](auto &e) {
        if (handle_value(e.handle()) == handle_value(handle)) {
            e.set_display_properties(v);
            found = true;
        }
    });
    if (!found)
        throw std::invalid_argument("Expected entity");
    *this = std::move(staged);
}
std::optional<std::vector<std::uint8_t>> DxfEntity::graphic_data() const {
    std::vector<std::uint8_t> result;
    bool present = false;
    locate_if("AcDbEntity", [&](std::size_t at, int code) {
        if (code == 92 || code == 160)
            present = true;
        if (code == 310) {
            auto text = binary_ ? detail::text_value_from_binary(310, pairs_[at].value) : pairs_[at].value;
            auto bytes = table_bytes(text);
            result.insert(result.end(), bytes.begin(), bytes.end());
            present = true;
        }
        return false;
    });
    if (!present)
        return {};
    return result;
}
void DxfEntity::set_graphic_data(const std::optional<std::vector<std::uint8_t>> &value) {
    std::vector<Pair> fields;
    if (value) {
        if (value->size() > INT32_MAX)
            throw std::invalid_argument("Graphic data too large");
        const int code = version_ >= DxfVersion::AC1024 ? 160 : 92;
        auto size = std::to_string(value->size());
        fields.push_back({code, binary_ ? detail::binary_value_from_text(code, size) : size});
        constexpr char hex[] = "0123456789ABCDEF";
        for (std::size_t at = 0; at < value->size(); at += 127) {
            std::string bytes;
            for (std::size_t n = at; n < std::min(at + 127, value->size()); ++n) {
                bytes += hex[(*value)[n] >> 4];
                bytes += hex[(*value)[n] & 15];
            }
            fields.push_back({310, binary_ ? detail::binary_value_from_text(310, bytes) : bytes});
        }
    }
    replace_common_fields({92, 160, 310}, std::move(fields));
}
