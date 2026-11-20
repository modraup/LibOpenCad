// SPDX-License-Identifier: MPL-2.0
namespace {
constexpr int material_codes[6][7] = {{42, 72, 3, 73, 74, 75, 43},       {46, 77, 4, 78, 79, 170, 47},
                                      {48, 171, 6, 172, 173, 174, 49},   {141, 175, 7, 176, 177, 178, 142},
                                      {143, 179, 8, 270, 271, 272, 144}, {146, 273, 9, 274, 275, 276, 147}};
MaterialColor material_color(MLFields &f, int flag, int factor, int rgb) {
    MaterialColor v;
    v.flag = f.i(flag, 0);
    v.factor = f.r(factor, 1);
    if (!f.take(rgb).empty())
        v.rgb = f.i(rgb, 0);
    return v;
}
void material_color(MLFields &f, const MaterialColor &v, int flag, int factor, int rgb) {
    f.put_i(flag, v.flag);
    f.put_r(factor, v.factor);
    if (v.flag == 1)
        f.put_i(rgb, v.rgb.value_or(0));
}
MaterialTexture material_texture(MLFields &f, std::size_t &at, unsigned depth = 0) {
    if (depth > 32)
        throw std::invalid_argument("Material texture nesting exceeds 32");
    MaterialTexture v;
    auto take = [&](int code) -> std::optional<MLFields> {
        if (at >= f.values.size() || f.hidden[at] || f.values[at].code != code)
            return {};
        f.used[at] = true;
        return MLFields({f.values[at++]});
    };
    if (auto p = take(277))
        v.mode = p->i(277, 0);
    else
        return v;
    auto color = [&](int flag, int factor, int rgb) {
        MaterialColor c;
        if (auto p = take(flag))
            c.flag = p->i(flag, 0);
        else
            return c;
        if (auto p = take(factor))
            c.factor = p->r(factor, 1);
        else
            return c;
        if (c.flag == 1)
            if (auto p = take(rgb))
                c.rgb = p->i(rgb, 0);
        return c;
    };
    if (v.mode == 0) {
        v.color1 = color(278, 460, 95);
        v.color2 = color(279, 461, 96);
    } else if (v.mode == 1) {
        v.color1 = color(280, 465, 97);
        v.color2 = color(281, 466, 98);
    } else if (v.mode == 2) {
        if (auto p = take(291))
            v.value = p->b(291, false);
        else if (auto p = take(271))
            v.value = static_cast<std::int16_t>(p->i(271, 0));
        else if (auto p = take(469))
            v.value = p->r(469, 0);
        else if (auto p = take(62)) {
            auto c = Color::from_index(p->i(62, 256));
            if (auto rgb = take(420))
                c = Color::from_true_color_value(rgb->i(420, 0));
            v.value = c;
        } else if (auto p = take(420))
            v.value = Color::from_true_color_value(p->i(420, 0));
        else if (auto p = take(301))
            v.value = p->s(301, {});
        else if (at < f.values.size() && f.values[at].code == 300) {
            v.table = true;
            while (auto p = take(300)) {
                if (at >= f.values.size() || f.values[at].code != 277)
                    break;
                auto name = p->s(300, {});
                v.children.emplace_back(name, material_texture(f, at, depth + 1));
            }
            if (auto p = take(292))
                v.table_end = p->b(292, false);
        } else if (auto p = take(292)) {
            v.table = true;
            v.table_end = p->b(292, false);
        }
    }
    return v;
}
void material_texture(MLFields &f, const MaterialTexture &v, unsigned depth = 0) {
    if (depth > 32)
        throw std::invalid_argument("Material texture nesting exceeds 32");
    f.put_i(277, v.mode);
    if (v.mode == 0) {
        material_color(f, v.color1, 278, 460, 95);
        material_color(f, v.color2, 279, 461, 96);
    } else if (v.mode == 1) {
        material_color(f, v.color1, 280, 465, 97);
        material_color(f, v.color2, 281, 466, 98);
    } else if (v.mode == 2) {
        if (v.table) {
            if (v.children.size() > 32767)
                throw std::invalid_argument("Too many texture children");
            for (const auto &child : v.children) {
                f.put_s(300, child.first);
                material_texture(f, child.second, depth + 1);
            }
            f.put_b(292, v.table_end);
        } else
            std::visit(
                [&](const auto &x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, bool>)
                        f.put_b(291, x);
                    else if constexpr (std::is_same_v<T, std::int16_t>)
                        f.put_i(271, x);
                    else if constexpr (std::is_same_v<T, double>)
                        f.put_r(469, x);
                    else if constexpr (std::is_same_v<T, Color>)
                        table_color(f, 62, 420, x);
                    else if constexpr (std::is_same_v<T, std::string>)
                        f.put_s(301, x);
                },
                v.value);
    }
}
AdvancedMaterial advanced_material(MLFields f, bool inline_data = false) {
    AdvancedMaterial v;
    const double scale = inline_data ? 1 : 100;
    v.color_bleed_scale = f.r(460, scale) / scale;
    v.indirect_bump_scale = f.r(461, scale) / scale;
    v.reflectance_scale = f.r(462, scale) / scale;
    v.transmittance_scale = f.r(463, scale) / scale;
    v.luminance = f.r(464, 0);
    v.two_sided = f.b(290, false);
    v.anonymous = f.b(293, false);
    v.luminance_mode = f.i(270, 0);
    v.global_illumination = f.i(272, 0);
    v.final_gather = f.i(273, 0);
    v.normal_map_method = f.i(271, 0);
    v.normal_map_strength = f.r(465, 0);
    v.extra_fields = f.extras();
    return v;
}
std::vector<DxfRawField> advanced_material(const AdvancedMaterial &v, bool inline_data = false) {
    MLFields f;
    const double scale = inline_data ? 1 : 100;
    f.put_r(460, v.color_bleed_scale * scale);
    f.put_r(461, v.indirect_bump_scale * scale);
    f.put_r(462, v.reflectance_scale * scale);
    f.put_r(463, v.transmittance_scale * scale);
    f.put_r(464, v.luminance);
    f.put_b(290, v.two_sided);
    f.put_b(293, v.anonymous);
    f.put_i(270, v.luminance_mode);
    f.put_i(272, v.global_illumination);
    f.put_i(273, v.final_gather);
    mt_append(f, v.extra_fields);
    return f.values;
}
} // namespace
MaterialDefinition DxfMaterial::definition() const {
    MLFields f(subclass_fields("AcDbMaterial"));
    MaterialDefinition v;
    // Consume nested procedural packets before scalar fields sharing their codes.
    int active = -1;
    bool advanced = false;
    for (std::size_t n = 0; n < f.values.size();) {
        if (f.hidden[n]) {
            ++n;
            continue;
        }
        int code = f.values[n].code;
        if ((code >= 460 && code <= 464) || code == 290 || code == 293)
            advanced = true;
        for (int map = 0; map < 6; ++map)
            if (code == material_codes[map][1])
                active = advanced && code == 72 ? 6 : map;
        if (code == 277 && active >= 0) {
            auto &m = active == 6 ? v.normal_map : v.maps[active];
            m.texture = material_texture(f, n);
            continue;
        }
        ++n;
    }
    auto raw = f.extras();
    std::size_t split = raw.size();
    {
        MLFields scan(raw);
        for (std::size_t n = 0; n < raw.size(); ++n)
            if (!scan.hidden[n] &&
                ((raw[n].code >= 460 && raw[n].code <= 464) || raw[n].code == 290 || raw[n].code == 293)) {
                split = n;
                break;
            }
    }
    if (split < raw.size()) {
        MLFields tail({raw.begin() + split, raw.end()});
        auto &m = v.normal_map;
        m.blend_factor = tail.r(42, 1);
        m.source = tail.i(72, 0);
        m.file_name = tail.s(3, {});
        m.projection = tail.i(73, 2);
        m.tiling = tail.i(74, 1);
        m.auto_transform = tail.i(75, 1);
        auto values = tail.take(43);
        for (std::size_t i = 0; i < std::min(values.size(), std::size_t(16)); ++i) {
            MLFields one({tail.values[values[i]]});
            m.transform[i] = one.r(43, 0);
        }
        v.advanced = advanced_material(MLFields(tail.extras()), true);
        raw.resize(split);
    }
    MLFields scalar(std::move(raw));
    v.name = scalar.s(1, {});
    v.description = scalar.s(2, {});
    v.ambient.flag = scalar.i(70, 0);
    v.ambient.factor = scalar.r(40, 1);
    auto ninety = scalar.take(90);
    std::size_t begin = 0;
    if (v.ambient.flag == 1 && !ninety.empty()) {
        MLFields p({scalar.values[ninety[0]]});
        v.ambient.rgb = p.i(90, 0);
        begin = 1;
    }
    for (auto i = begin; i < ninety.size(); ++i) {
        MLFields p({scalar.values[ninety[i]]});
        v.self_illumination = p.i(90, 0);
    }
    v.diffuse = material_color(scalar, 71, 41, 91);
    v.specular = material_color(scalar, 76, 45, 92);
    for (int i = 0; i < 6; ++i) {
        auto &m = v.maps[i];
        auto c = material_codes[i];
        m.blend_factor = scalar.r(c[0], 1);
        m.source = scalar.i(c[1], 0);
        m.file_name = scalar.s(c[2], {});
        m.projection = scalar.i(c[3], 2);
        m.tiling = scalar.i(c[4], 1);
        m.auto_transform = scalar.i(c[5], 1);
        auto matrix = scalar.take(c[6]);
        for (std::size_t n = 0; n < std::min(matrix.size(), std::size_t(16)); ++n) {
            MLFields p({scalar.values[matrix[n]]});
            m.transform[n] = p.r(c[6], 0);
        }
    }
    v.gloss = scalar.r(44, .5);
    v.opacity = scalar.r(140, 1);
    v.refraction_index = scalar.r(145, 1);
    v.translucence = scalar.r(148, 0);
    v.self_illumination = scalar.r(149, v.self_illumination);
    v.reflectivity = scalar.r(468, 0);
    v.illumination_model = scalar.i(93, 0);
    v.channel_flags = scalar.i(94, 127);
    v.mode = scalar.i(282, 0);
    v.extra_fields = scalar.extras();
    return v;
}
void DxfMaterial::set_definition(const MaterialDefinition &v) {
    MLFields f;
    f.put_s(1, v.name);
    auto key = detail::symbol_name_key(v.name);
    if (key == U"BYLAYER" || key == U"BYBLOCK" || key == U"GLOBAL") {
        f.put_i(72, v.maps[0].source);
        f.put_i(94, v.channel_flags);
    } else {
        if (!v.description.empty())
            f.put_s(2, v.description);
        material_color(f, v.ambient, 70, 40, 90);
        material_color(f, v.diffuse, 71, 41, 91);
        auto map = [&](int i) {
            const auto &m = v.maps[i];
            auto c = material_codes[i];
            f.put_r(c[0], m.blend_factor);
            if (!m.file_name.empty())
                f.put_s(c[2], m.file_name);
            f.put_i(c[1], m.source);
            // The Rust reader consumes 277 immediately after source=2. Its writer places
            // it after the matrix, losing textures on its own readback. Emit reader order.
            if (m.source == 2)
                material_texture(f, m.texture.value_or(MaterialTexture{}));
            f.put_i(c[3], m.projection);
            f.put_i(c[4], m.tiling);
            f.put_i(c[5], m.auto_transform);
            for (double x : m.transform)
                f.put_r(c[6], x);
        };
        map(0);
        material_color(f, v.specular, 76, 45, 92);
        f.put_r(44, v.gloss);
        map(1);
        map(2);
        f.put_r(140, v.opacity);
        map(3);
        map(4);
        f.put_r(145, v.refraction_index);
        map(5);
        f.put_r(148, v.translucence);
        finite(v.self_illumination);
        f.put_i(90, static_cast<std::int32_t>(
                        std::clamp(std::round(v.self_illumination), double(INT32_MIN), double(INT32_MAX))));
        f.put_r(149, v.self_illumination);
        f.put_r(468, v.reflectivity);
        f.put_i(93, v.illumination_model);
        f.put_i(94, v.channel_flags);
        f.put_i(282, v.mode);
    }
    mt_append(f, v.extra_fields);
    if (v.advanced) {
        auto a = *v.advanced;
        auto tail = advanced_material(a, true);
        f.append(tail);
        f.put_i(271, a.normal_map_method);
        f.put_r(465, a.normal_map_strength);
        const auto &m = v.normal_map;
        f.put_r(42, m.blend_factor);
        f.put_s(3, m.file_name);
        f.put_i(72, m.source);
        if (m.source == 2)
            material_texture(f, m.texture.value_or(MaterialTexture{}));
        f.put_i(73, m.projection);
        f.put_i(74, m.tiling);
        f.put_i(75, m.auto_transform);
        for (auto value : m.transform)
            f.put_r(43, value);
    }
    set_subclass_fields("AcDbMaterial", f.values);
}
MaterialDefinition DxfDocument::material_definition(std::string_view handle) const {
    mleader_check_handle(*this, handle_value(handle), "MATERIAL");
    if (records_with_handle(handle).size() != 1)
        throw std::invalid_argument("Expected unique MATERIAL");
    MaterialDefinition v;
    for_each<DxfMaterial>([&](const auto &m) {
        if (handle_value(m.handle()) == handle_value(handle))
            v = m.definition();
    });
    auto dict = extension_dictionary_handle(handle);
    if (!dict.empty()) {
        auto h = dictionary_entry(dict, "ADVMATERIAL");
        if (!h.empty()) {
            const auto *r = get_record(h);
            if (r->type() != "XRECORD" || handle_value(r->owner_handle()) != handle_value(dict))
                throw std::invalid_argument("Invalid advanced material ownership");
            MLFields f(r->subclass_fields("AcDbXrecord"));
            f.take(280);
            auto restored = advanced_material(MLFields(f.extras()));
            if (v.advanced) {
                restored.normal_map_method = v.advanced->normal_map_method;
                restored.normal_map_strength = v.advanced->normal_map_strength;
            }
            v.advanced = restored;
        }
    }
    if (!dict.empty()) {
        const char *names[] = {"DIFFUSE", "SPECULAR",   "REFLECTION", "OPACITY",
                               "BUMP",    "REFRACTION", "NORMAL"};
        for (int i = 0; i < 7; ++i) {
            auto h = dictionary_entry(dict, names[i]);
            if (h.empty())
                continue;
            const auto *r = get_record(h);
            if (!r || r->type() != "XRECORD" || handle_value(r->owner_handle()) != handle_value(dict))
                throw std::invalid_argument("Invalid material asset ownership");
            MLFields f(r->subclass_fields("AcDbXrecord"));
            bool checker = false;
            int active = -1;
            std::optional<std::int32_t> colors[2];
            for (std::size_t n = 0; n < f.values.size(); ++n) {
                if (f.hidden[n])
                    continue;
                const auto &p = f.values[n];
                if (p.code == 301 && detail::symbol_name_key(p.value) == U"CHECKER")
                    checker = true;
                if (p.code == 300) {
                    auto key = detail::symbol_name_key(p.value);
                    if (key == U"MAP1")
                        active = 0;
                    else if (key == U"MAP2")
                        active = 1;
                    else if (key == U"MAPPER")
                        active = -1;
                }
                if (p.code == 420 && active >= 0) {
                    MLFields one({p});
                    colors[active] = one.i(420, 0);
                }
            }
            if (checker && colors[0] && colors[1]) {
                MaterialTexture t;
                t.color1 = {1, 1, colors[0]};
                t.color2 = {1, 1, colors[1]};
                (i == 6 ? v.normal_map : v.maps[i]).texture = t;
            }
        }
    }
    return v;
}
void DxfDocument::set_material_definition(std::string_view handle, const MaterialDefinition &v) {
    auto old = material_definition(handle);
    if (v.name != old.name)
        throw std::invalid_argument("Material setter cannot rename dictionary identity");
    DxfDocument staged = *this;
    staged.for_each<DxfMaterial>([&](auto &m) {
        if (handle_value(m.handle()) == handle_value(handle))
            m.set_definition(v);
    });
    if (v.advanced) {
        auto dict = staged.extension_dictionary_handle(handle);
        if (dict.empty())
            dict = hex_handle(staged.add_extension_dictionary(handle));
        auto h = staged.dictionary_entry(dict, "ADVMATERIAL");
        if (h.empty())
            h = hex_handle(staged.add_xrecord(dict, "ADVMATERIAL", {}));
        auto &r = const_cast<DxfRecord &>(*staged.get_record(h));
        auto values = advanced_material(*v.advanced);
        values.insert(values.begin(), {280, "1"});
        r.set_subclass_fields("AcDbXrecord", values);
    }
    auto dict = staged.extension_dictionary_handle(handle);
    const char *names[] = {"DIFFUSE", "SPECULAR", "REFLECTION", "OPACITY", "BUMP", "REFRACTION", "NORMAL"};
    for (int i = 0; i < 7; ++i) {
        const auto &map = i == 6 ? v.normal_map : v.maps[i];
        auto asset = dict.empty() ? std::string{} : staged.dictionary_entry(dict, names[i]);
        const bool checker =
            map.texture && map.texture->mode == 0 && map.texture->color1.rgb && map.texture->color2.rgb;
        if (asset.empty() && !(checker && map.source != 2))
            continue;
        if (dict.empty())
            dict = hex_handle(staged.add_extension_dictionary(handle));
        if (asset.empty())
            asset = hex_handle(staged.add_xrecord(dict, names[i], {}));
        auto &r = const_cast<DxfRecord &>(*staged.get_record(asset));
        if (r.type() != "XRECORD" || handle_value(r.owner_handle()) != handle_value(dict))
            throw std::invalid_argument("Invalid material asset ownership");
        auto raw = r.subclass_fields("AcDbXrecord");
        MLFields scan(raw);
        bool found = false;
        int active = -1;
        bool written[2] = {false, false};
        std::vector<DxfRawField> values;
        for (std::size_t n = 0; n < raw.size(); ++n) {
            auto p = raw[n];
            if (!scan.hidden[n]) {
                if (p.code == 301 && detail::symbol_name_key(p.value) == U"CHECKER") {
                    found = true;
                    if (!checker)
                        continue;
                }
                if (p.code == 300) {
                    auto key = detail::symbol_name_key(p.value);
                    if (key == U"MAP1")
                        active = 0;
                    else if (key == U"MAP2")
                        active = 1;
                    else if (key == U"MAPPER")
                        active = -1;
                }
                if (p.code == 420 && active >= 0 && checker) {
                    p.value =
                        std::to_string(*(active == 0 ? map.texture->color1.rgb : map.texture->color2.rgb));
                    written[active] = true;
                }
            }
            values.push_back(p);
        }
        if (checker) {
            if (!found)
                values.push_back({301, "Checker"});
            for (int n = 0; n < 2; ++n)
                if (!written[n]) {
                    values.push_back({300, n == 0 ? "Map1" : "Map2"});
                    values.push_back(
                        {420, std::to_string(*(n == 0 ? map.texture->color1.rgb : map.texture->color2.rgb))});
                }
        }
        r.set_subclass_fields("AcDbXrecord", values);
    }
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_material(const MaterialDefinition &v) {
    if (version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Material construction requires R2007+");
    valid_symbol_name(v.name);
    DxfDocument staged = *this;
    auto root = staged.root_dictionary_handle();
    auto dict = staged.dictionary_entry(root, "ACAD_MATERIAL");
    if (dict.empty())
        dict = hex_handle(staged.add_dictionary(root, "ACAD_MATERIAL"));
    DxfMaterial object;
    static_cast<DxfRecord &>(object) = staged.make_record("MATERIAL");
    object.pairs_.push_back({100, "AcDbMaterial"});
    object.set_definition(v);
    auto id = staged.add_dictionary_object(dict, v.name, object);
    staged.set_material_definition(hex_handle(id), v);
    *this = std::move(staged);
    return id;
}
