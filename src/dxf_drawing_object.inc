// SPDX-License-Identifier: MPL-2.0
// acadrust stub_objects.rs and DXF visual-style/geographic-data field schemas.
VisualStyleDefinition VisualStyleDefinition::defaults() {
    VisualStyleDefinition v;
    auto l = [&](int n) { v.properties.push_back({std::int32_t(n), 1}); };
    auto r = [&](double n) { v.properties.push_back({n, 1}); };
    auto b = [&](bool n) { v.properties.push_back({n, 1}); };
    auto c = [&](Color color, int enabled = 1) {
        v.properties.push_back({color, static_cast<std::int16_t>(enabled)});
    };
    l(0);
    l(0);
    l(0);
    l(0);
    r(.6);
    r(30);
    c(Color::from_index(7));
    l(0);
    l(0);
    c(Color::by_layer());
    c(Color::by_block());
    l(1);
    l(1);
    r(1);
    l(0);
    c(Color::by_layer());
    r(1);
    l(1);
    l(6);
    l(2);
    c(Color::by_layer());
    l(5);
    l(0);
    l(0);
    b(false);
    l(1);
    r(0);
    l(0);
    b(false);
    b(true);
    b(true);
    for (int n = 0; n < 6; ++n)
        b(false);
    l(50);
    r(0);
    r(1);
    l(0);
    c(Color::from_rgb(0, 0, 0));
    l(50);
    l(3);
    c(Color::from_rgb(0, 0, 255));
    b(false);
    l(50);
    l(50);
    l(50);
    b(false);
    l(50);
    c(Color::by_layer(), 0);
    v.properties.push_back({1., 0});
    l(2);
    v.properties.push_back({std::string("strokes_ogs.tif"), 1});
    b(false);
    r(1);
    r(1);
    return v;
}
DrawingObjectDefinition DxfDrawingObject::definition() const {
    if (type() == "VISUALSTYLE") {
        VisualStyleDefinition v;
        MLFields f(subclass_fields("AcDbVisualStyle"));
        v.description = f.s(2, {});
        v.lighting_model = f.i(177, 2);
        v.internal = f.b(291, false);
        auto counts = f.take(70);
        if (counts.empty())
            throw std::invalid_argument("Missing visual-style type");
        {
            MLFields one({f.values[counts[0]]});
            v.style_type = one.i(70, 0);
        }
        if (counts.size() < 2)
            throw std::invalid_argument("Typed visual-style properties require R2013+ layout");
        std::optional<VisualStyleValue> pending;
        for (std::size_t n = counts[1] + 1; n < f.values.size(); ++n) {
            if (f.hidden[n])
                continue;
            const auto &p = f.values[n];
            MLFields one({p});
            switch (p.code) {
            case 90:
                pending = std::int32_t(one.i(90, 0));
                break;
            case 40:
                pending = one.r(40, 0);
                break;
            case 290:
                pending = one.b(290, false);
                break;
            case 62:
                pending = Color::from_index(one.i(62, 256));
                break;
            case 420:
                pending = Color::from_true_color_value(one.i(420, 0));
                break;
            case 1:
                pending = one.s(1, {});
                break;
            case 176:
                if (pending) {
                    v.properties.push_back({*pending, static_cast<std::int16_t>(one.i(176, 0))});
                    pending.reset();
                }
                break;
            default:
                continue;
            }
            f.used[n] = true;
        }
        v.extra_fields = f.extras();
        return v;
    }
    if (type() != "GEODATA")
        throw std::invalid_argument("Unknown drawing object");
    MLFields f(subclass_fields("AcDbGeoData"));
    GeoDataDefinition v;
    v.version = f.i(90, 2);
    v.host_block = f.h(330, 0);
    v.coordinate_type = f.i(70, 0);
    v.design = f.p(10, {});
    v.reference = f.p(11, {});
    v.up = f.p(210, {});
    v.horizontal_scale = f.r(40, 1);
    v.vertical_scale = f.r(41, 1);
    v.horizontal_units = f.i(91, 0);
    v.vertical_units = f.i(92, 0);
    v.scale_method = f.i(95, 0);
    v.user_scale = f.r(141, 1);
    v.sea_correction = f.b(294, false);
    v.sea_elevation = f.r(142, 0);
    v.projection_radius = f.r(143, 0);
    v.wkt = f.s(304, {});
    v.rss = f.s(302, {});
    v.observation_from = f.s(305, {});
    v.observation_to = f.s(306, {});
    v.observation_coverage = f.s(307, {});
    v.north = {f.r(12, 0), f.r(22, 0)};
    bool civil = false;
    std::optional<double> sx, sy, dx;
    std::optional<std::int32_t> a, b;
    MLFields tail;
    for (std::size_t n = 0; n < f.values.size(); ++n) {
        if (f.hidden[n])
            continue;
        const auto &p = f.values[n];
        MLFields one({p});
        if (p.code == 3 && p.value == "CIVIL3D_DATA_BEGIN") {
            civil = true;
            f.used[n] = true;
            continue;
        }
        if (p.code == 4 && p.value == "CIVIL3D_DATA_END") {
            civil = false;
            f.used[n] = true;
            continue;
        }
        if (civil) {
            tail.values.push_back(p);
            f.used[n] = true;
            continue;
        }
        switch (p.code) {
        case 301:
        case 303:
            if (p.code == 303 && v.version == 1)
                v.datum = p.value;
            else {
                if (!v.coordinate_system.empty() && v.coordinate_system.back() == '^' && !p.value.empty() &&
                    (p.value[0] == 'J' || p.value[0] == 'M' || p.value[0] == 'I' || p.value[0] == ' ')) {
                    v.coordinate_system.pop_back();
                    v.coordinate_system += decode_text("^" + p.value);
                } else
                    v.coordinate_system += p.value;
            }
            break;
        case 13:
            sx = one.r(13, 0);
            break;
        case 23:
            sy = one.r(23, 0);
            break;
        case 14:
            dx = one.r(14, 0);
            break;
        case 24:
            if (sx && sy && dx)
                v.points.push_back({{*sx, *sy}, {*dx, one.r(24, 0)}});
            sx.reset();
            sy.reset();
            dx.reset();
            break;
        case 97:
            a = one.i(97, 0);
            break;
        case 98:
            b = one.i(98, 0);
            break;
        case 99:
            if (a && b)
                v.faces.push_back({*a, *b, one.i(99, 0)});
            a.reset();
            b.reset();
            break;
        case 93:
        case 96:
            break;
        default:
            continue;
        }
        f.used[n] = true;
    }
    MLFields c(tail.values);
    auto &cv = v.civil;
    cv.obsolete = c.b(292, false);
    cv.unknown_flag = c.b(293, false);
    cv.unknown1 = c.i(93, 0);
    cv.unknown2 = c.i(94, 0);
    cv.reference1 = {c.r(14, 0), c.r(24, 0)};
    cv.reference2 = {c.r(15, 0), c.r(25, 0)};
    cv.zero1 = {c.r(16, 0), c.r(26, 0)};
    cv.zero2 = {c.r(17, 0), c.r(27, 0)};
    cv.north_degrees = c.r(54, 0);
    cv.north_radians = c.r(140, 0);
    for (int code : {95, 141, 294, 142, 143})
        c.take(code);
    cv.extra_fields = c.extras();
    v.extra_fields = f.extras();
    return v;
}
void DxfDrawingObject::set_definition(const DrawingObjectDefinition &value) {
    MLFields f;
    if (auto p = std::get_if<VisualStyleDefinition>(&value)) {
        if (type() != "VISUALSTYLE")
            throw std::invalid_argument("Drawing object type mismatch");
        auto v = *p;
        if (v.properties.empty())
            v.properties = VisualStyleDefinition::defaults().properties;
        if (v.properties.size() > 32767)
            throw std::invalid_argument("Too many visual-style properties");
        f.put_s(2, v.description);
        f.put_i(70, v.style_type);
        f.put_i(177, v.lighting_model);
        f.put_b(291, v.internal);
        f.put_i(70, v.properties.size());
        for (const auto &p : v.properties) {
            std::visit(
                [&](const auto &value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::int32_t>)
                        f.put_i(90, value);
                    else if constexpr (std::is_same_v<T, double>)
                        f.put_r(40, value);
                    else if constexpr (std::is_same_v<T, bool>)
                        f.put_b(290, value);
                    else if constexpr (std::is_same_v<T, Color>)
                        cs_color(f, value);
                    else
                        f.put_s(1, value);
                },
                p.value);
            f.put_i(176, p.enabled);
        }
        mt_append(f, v.extra_fields);
        set_subclass_fields("AcDbVisualStyle", f.values);
        return;
    }
    if (type() != "GEODATA")
        throw std::invalid_argument("Drawing object type mismatch");
    const auto &v = std::get<GeoDataDefinition>(value);
    if (v.version < 1 || v.version > 3 || v.points.size() > INT32_MAX || v.faces.size() > INT32_MAX)
        throw std::invalid_argument("Invalid GEODATA version/count");
    f.put_i(90, v.version);
    f.put_h(330, v.host_block);
    f.put_i(70, v.coordinate_type);
    f.put_p(10, v.design);
    f.put_p(11, v.reference);
    f.put_p(210, v.up);
    f.put_r(40, v.horizontal_scale);
    f.put_r(41, v.vertical_scale);
    f.put_i(91, v.horizontal_units);
    f.put_i(92, v.vertical_units);
    f.put_i(95, v.scale_method);
    f.put_r(141, v.user_scale);
    f.put_b(294, v.sea_correction);
    f.put_r(142, v.sea_elevation);
    f.put_r(143, v.projection_radius);
    f.put_s(304, v.wkt);
    f.put_s(302, v.rss);
    f.put_s(305, v.observation_from);
    f.put_s(306, v.observation_to);
    f.put_s(307, v.observation_coverage);
    f.put_r(12, v.north.x);
    f.put_r(22, v.north.y);
    if (v.version == 1) {
        f.put_s(301, v.coordinate_system);
        f.put_s(303, v.datum);
    } else {
        std::vector<std::size_t> characters;
        for (std::size_t i = 0; i < v.coordinate_system.size(); ++i)
            if ((static_cast<unsigned char>(v.coordinate_system[i]) & 0xc0) != 0x80)
                characters.push_back(i);
        characters.push_back(v.coordinate_system.size());
        std::size_t at = 0;
        do {
            auto end = std::min(at + 250, characters.size() - 1);
            while (end < characters.size() - 1 && end > at + 1 &&
                   v.coordinate_system[characters[end - 1]] == '^')
                --end;
            f.put_s(end == characters.size() - 1 ? 301 : 303,
                    v.coordinate_system.substr(characters[at], characters[end] - characters[at]));
            at = end;
        } while (at < characters.size() - 1);
    }
    f.put_i(93, v.points.size());
    for (const auto &p : v.points) {
        f.put_r(13, p.source.x);
        f.put_r(23, p.source.y);
        f.put_r(14, p.destination.x);
        f.put_r(24, p.destination.y);
    }
    f.put_i(96, v.faces.size());
    for (const auto &face : v.faces) {
        f.put_i(97, face[0]);
        f.put_i(98, face[1]);
        f.put_i(99, face[2]);
    }
    if (v.version == 1) {
        const auto &c = v.civil;
        f.put_s(3, "CIVIL3D_DATA_BEGIN");
        f.put_b(292, c.obsolete);
        f.put_r(14, c.reference1.x);
        f.put_r(24, c.reference1.y);
        f.put_r(15, c.reference2.x);
        f.put_r(25, c.reference2.y);
        f.put_i(93, c.unknown1);
        f.put_i(94, c.unknown2);
        f.put_b(293, c.unknown_flag);
        f.put_r(16, c.zero1.x);
        f.put_r(26, c.zero1.y);
        f.put_r(17, c.zero2.x);
        f.put_r(27, c.zero2.y);
        f.put_r(54, c.north_degrees);
        f.put_r(140, c.north_radians);
        f.put_i(95, v.scale_method);
        f.put_r(141, v.user_scale);
        f.put_b(294, v.sea_correction);
        f.put_r(142, v.sea_elevation);
        f.put_r(143, v.projection_radius);
        mt_append(f, c.extra_fields);
        f.put_s(4, "CIVIL3D_DATA_END");
    }
    mt_append(f, v.extra_fields);
    set_subclass_fields("AcDbGeoData", f.values, true);
}
std::uint64_t DxfDocument::add_drawing_object(std::string_view dictionary, std::string name,
                                              const DrawingObjectDefinition &v) {
    if (version_ < DxfVersion::AC1027)
        throw std::invalid_argument("Drawing object construction requires R2013+");
    if (auto geo = std::get_if<GeoDataDefinition>(&v))
        mleader_check_handle(*this, geo->host_block, "BLOCK_RECORD");
    DxfDrawingObject object;
    static_cast<DxfRecord &>(object) = make_record(v.index() == 0 ? "VISUALSTYLE" : "GEODATA");
    object.pairs_.push_back({100, v.index() == 0 ? "AcDbVisualStyle" : "AcDbGeoData"});
    object.set_definition(v);
    return add_dictionary_object(dictionary, std::move(name), object);
}
