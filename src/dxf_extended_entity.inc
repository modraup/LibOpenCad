// SPDX-License-Identifier: MPL-2.0
// Stored DXF fields ported from acadrust light, ole2frame, section_symbol and view_border.
namespace {
constexpr const char *extended_names[] = {"OLE2FRAME",       "LIGHT",
                                          "SECTIONLINE",     "DRAWINGVIEW",
                                          "CAMERA",          "SECTIONOBJECT",
                                          "ARCALIGNEDTEXT",  "RTEXT",
                                          "POSITIONMARKER",  "COORDINATION_MODEL",
                                          "OLEFRAME",        "LAYOUTPRINTCONFIG",
                                          "Format",          "ACDBPOINTCLOUD",
                                          "ACDBPOINTCLOUDEX"};
constexpr const char *extended_scopes[] = {"AcDbOle2Frame",       "AcDbLight",      "AcDbSectionSymbol",
                                           "AcDbViewBorder",      "AcDbCamera",     "AcDbSection",
                                           "AcDbArcAlignedText",  "RText",          "AcDbGeoPositionMarker",
                                           "AcDbNavisworksModel", "AcDbOleFrame",   "CAcLayoutPrintConfig",
                                           "mcsDbObjectFormat",   "AcDbPointCloud", "AcDbPointCloudEx"};
int extended_index(std::string_view name) {
    for (int i = 0; i < int(std::size(extended_names)); ++i)
        if (name == extended_names[i])
            return i;
    return -1;
}
std::vector<Point3> extended_points(MLFields &f, int code) {
    std::vector<Point3> result;
    auto starts = f.take(code);
    for (std::size_t n = 0; n < starts.size(); ++n) {
        const auto end = n + 1 < starts.size() ? starts[n + 1] : f.values.size();
        std::vector<DxfRawField> values;
        for (auto i = starts[n]; i < end; ++i)
            if (!f.hidden[i] && (f.values[i].code == code || f.values[i].code == code + 10 ||
                                 f.values[i].code == code + 20)) {
                values.push_back(f.values[i]);
                f.used[i] = true;
            }
        MLFields p(values);
        if (!p.take(code + 10).empty())
            result.push_back(p.p(code, {}));
    }
    return result;
}
std::vector<DxfRawField> extended_tail(MLFields &f, int code) {
    for (std::size_t i = 0; i < f.values.size(); ++i)
        if (!f.hidden[i] && f.values[i].code == code) {
            std::vector<DxfRawField> tail(f.values.begin() + i + 1, f.values.end());
            f = MLFields({f.values.begin(), f.values.begin() + i});
            return tail;
        }
    return {};
}
} // namespace
ExtendedEntityDefinition DxfExtendedEntity::definition() const {
    int kind = extended_index(type());
    if (kind < 0)
        throw std::invalid_argument("Unknown extended entity");
    MLFields f(subclass_fields(extended_scopes[kind]));
    if (kind == 0) {
        OleFrameDefinition v;
        v.version = f.i(70, 2);
        v.object_type = f.i(71, 2);
        if (v.object_type < 1 || v.object_type > 3)
            v.object_type = 2;
        v.source_application = f.s(3, {});
        v.upper_left = {f.r(10, 1), f.r(20, 1), f.r(30, 0)};
        v.lower_right = {f.r(11, 0), f.r(21, 0), f.r(31, 0)};
        v.paper_space = f.b(72, false);
        v.lock_aspect = std::clamp(f.i(73, 0), 0, 255);
        f.take(1);
        f.take(90);
        for (auto n : f.take(310)) {
            auto bytes = table_bytes(f.values[n].value);
            v.payload.insert(v.payload.end(), bytes.begin(), bytes.end());
        }
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 1) {
        LightDefinition v;
        const bool photo = !f.take(295).empty();
        MLFields p(extended_tail(f, 295));
        v.photometric_mode = photo;
        v.class_version = f.i(90, 1);
        v.name = f.s(1, {});
        v.type = f.i(70, 2);
        v.status = f.b(290, true);
        v.plot_glyph = f.b(291, false);
        v.intensity = f.r(40, 1);
        v.position = f.p(10, {});
        v.target = f.p(11, {});
        v.attenuation = f.i(72, 0);
        v.use_attenuation_limits = f.b(292, false);
        v.attenuation_start = f.r(41, 0);
        v.attenuation_end = f.r(42, 0);
        v.hotspot_angle = f.r(50, 0);
        v.falloff_angle = f.r(51, 0);
        v.cast_shadows = f.b(293, false);
        v.shadow_type = f.i(73, 0);
        v.shadow_map_size = f.i(91, 0);
        v.shadow_softness = f.i(280, 0);
        v.color = read_table_color(f, 63, 421, Color::from_index(7));
        v.extra_fields = f.extras();
        if (photo) {
            LightPhotometricData data;
            data.has_web_file = p.b(290, false);
            data.web_file = p.s(300, {});
            data.intensity_method = p.i(70, 0);
            data.intensity = p.r(40, 0);
            data.illuminance_distance = p.r(41, 0);
            data.lamp_color_type = p.i(71, 0);
            data.temperature = p.r(42, 0);
            data.lamp_color_preset = p.i(72, 0);
            data.web_rotation = p.p(43, {1, 1, 1});
            data.shape = p.i(73, 0);
            data.length = p.r(46, 0);
            data.width = p.r(47, 0);
            data.radius = p.r(48, 0);
            data.web_file_type = p.i(74, 0);
            data.web_symmetry = p.i(75, 0);
            data.target_grip = p.i(76, 0);
            data.web_flux = p.r(49, 0);
            data.glyph_display_type = p.i(77, 0);
            // The pinned Rust reader routes every group 53 to web_rotation.y.
            // Read repeated web angles independently to preserve stored data.
            for (int n = 0; n < 5; ++n) {
                auto values = p.take(50 + n);
                if (!values.empty() && (n != 3 || values.size() > 1)) {
                    MLFields one({p.values[values.back()]});
                    data.web_angles[n] = one.r(50 + n, 0);
                }
            }
            auto rotations = p.take(53);
            if (!rotations.empty()) {
                MLFields one({p.values[rotations.front()]});
                data.web_rotation.y = one.r(53, 1);
            }
            data.extra_fields = p.extras();
            v.photometric = data;
        }
        return v;
    }
    if (kind == 2) {
        SectionSymbolDefinition v;
        MLFields base(subclass_fields("AcDbViewSymbol"));
        auto versions = base.take(70);
        if (!versions.empty()) {
            MLFields one({base.values[versions[0]]});
            v.view_version = one.i(70, 0);
        }
        if (versions.size() > 1) {
            MLFields one({base.values[versions[1]]});
            v.view_flags = one.i(70, 0);
        }
        v.style = base.h(340, 0);
        v.view_rep = base.h(330, 0);
        v.scale = base.r(40, 1);
        v.view_extra_fields = base.extras();
        v.version = f.i(70, 0);
        auto counts = f.take(90);
        for (std::size_t n = 0; n < std::min(std::size_t(3), counts.size()); ++n) {
            MLFields one({f.values[counts[n]]});
            auto value = one.i(90, 0);
            if (n == 0)
                v.point_count = value;
            else if (n == 1)
                v.flags = value;
            else
                v.point_record_count = value;
        }
        auto starts = f.take(10);
        for (std::size_t n = 0; n < starts.size(); ++n) {
            auto first = starts[n], end = n + 1 < starts.size() ? starts[n + 1] : f.values.size();
            MLFields p({f.values.begin() + first, f.values.begin() + end});
            SectionSymbolPoint point;
            point.position = {p.r(10, 0), p.r(20, 0), p.r(30, 0)};
            point.label_offset = {p.r(11, 0), p.r(21, 0), p.r(31, 0)};
            point.label = p.s(1, {});
            point.bulge = p.r(40, 0);
            point.flags = static_cast<std::uint8_t>(p.i(280, 0));
            point.extra_fields = p.extras();
            v.points.push_back(std::move(point));
            for (auto i = first; i < end; ++i)
                f.used[i] = true;
        }
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 4) {
        CameraDefinition v;
        v.view = f.h(340, 0);
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 5) {
        SectionObjectDefinition v;
        v.state = f.i(90, 0);
        v.flags = f.i(91, 0);
        v.name = f.s(1, {});
        v.vertical = f.p(10, {});
        v.top = f.r(40, 0);
        v.bottom = f.r(41, 0);
        v.indicator_alpha = f.i(70, 0);
        v.settings = f.h(360, 0);
        v.indicator_color = read_table_color(f, 62, 420, Color::by_layer());
        f.take(92);
        f.take(93);
        v.vertices = extended_points(f, 11);
        v.back_vertices = extended_points(f, 12);
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 6) {
        ArcAlignedTextDefinition v;
        v.text = f.s(1, {});
        v.font = f.s(2, {});
        v.big_font = f.s(3, {});
        v.style = f.s(7, {});
        v.center = f.p(10, {});
        v.radius = f.r(40, 0);
        v.x_scale = f.r(41, 1);
        v.size = f.r(42, 0);
        v.spacing = f.r(43, 0);
        v.arc_offset = f.r(44, 0);
        v.right_offset = f.r(45, 0);
        v.left_offset = f.r(46, 0);
        v.start_angle = f.r(50, 0) * radians_per_degree;
        v.end_angle = f.r(51, 0) * radians_per_degree;
        v.reverse = f.b(70, false);
        v.direction = f.i(71, 0);
        v.alignment = f.i(72, 0);
        v.position = f.i(73, 0);
        v.bold = f.b(74, false);
        v.italic = f.b(75, false);
        v.underlined = f.b(76, false);
        v.charset = f.i(77, 0);
        v.pitch_family = f.i(78, 0);
        v.shx = f.b(79, false);
        v.color = f.i(90, 0);
        v.normal = f.p(210, {0, 0, 1});
        v.wizard = f.b(280, false);
        v.arc = f.h(330, 0);
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 7) {
        RemoteTextDefinition v;
        v.position = f.p(10, {});
        v.normal = f.p(210, {0, 0, 1});
        v.rotation = f.r(50, 0);
        v.height = f.r(40, 0);
        v.style = f.s(7, {});
        v.flags = f.i(70, 0);
        v.text = f.s(1, {});
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 8) {
        PositionMarkerDefinition v;
        v.class_version = f.i(90, 0);
        v.position = f.p(10, {});
        v.notes = f.s(1, {});
        v.alignment = f.i(280, 0);
        auto reals = f.take(40), flags = f.take(290);
        if (!reals.empty()) {
            MLFields p({f.values[reals.front()]});
            v.radius = p.r(40, 0);
        }
        if (reals.size() > 1) {
            MLFields p({f.values[reals.back()]});
            v.landing_gap = p.r(40, 0);
        }
        if (!flags.empty()) {
            MLFields p({f.values[flags.front()]});
            v.text_visible = p.b(290, false);
        }
        if (flags.size() > 1) {
            MLFields p({f.values[flags.back()]});
            v.frame_text = p.b(290, false);
        }
        if (locate("AcDbMTextObjectEmbedded", 10).index != std::string_view::npos) {
            MLFields m(subclass_fields("AcDbMTextObjectEmbedded"));
            MarkerTextDefinition text;
            text.insertion = m.p(10, {});
            text.normal = m.p(210, {0, 0, 1});
            text.height = m.r(40, 1);
            text.width = m.r(41, 0);
            text.text = m.s(1, {});
            text.style = m.s(7, "Standard");
            text.extra_fields = m.extras();
            v.embedded = text;
        }
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 9) {
        CoordinationModelDefinition v;
        v.flags = f.i(70, 0);
        v.definition = f.h(340, 0);
        auto values = f.take(40);
        for (std::size_t n = 0; n < std::min(values.size(), std::size_t(16)); ++n) {
            MLFields p({f.values[values[n]]});
            v.transform[n] = p.r(40, 0);
        }
        if (values.size() > 16) {
            MLFields p({f.values[values.back()]});
            v.unit_factor = p.r(40, 1);
        }
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 10) {
        OleLegacyDefinition v;
        v.flags = f.i(70, 0);
        v.mode = f.i(71, 0);
        f.take(90);
        for (auto n : f.take(310)) {
            auto bytes = table_bytes(f.values[n].value);
            v.payload.insert(v.payload.end(), bytes.begin(), bytes.end());
        }
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 11) {
        LayoutPrintDefinition v;
        v.flags = f.i(93, 0);
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 12) {
        FormatEntityDefinition v;
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 13) {
        PointCloudDefinition v;
        v.class_version = f.i(70, 0);
        v.origin = f.p(10, {});
        v.filename = f.s(1, {});
        v.extents_min = f.p(11, {});
        v.extents_max = f.p(12, {});
        v.point_count = f.i(92, 0);
        v.ucs_name = f.s(3, {});
        v.ucs_origin = f.p(13, {});
        v.ucs_x = f.p(210, {1, 0, 0});
        v.ucs_y = f.p(211, {0, 1, 0});
        v.ucs_z = f.p(212, {0, 0, 1});
        v.definition = f.h(330, 0);
        v.reactor = f.h(360, 0);
        v.show_intensity = f.b(290, false);
        v.intensity_scheme = f.i(71, 0);
        v.minimum = f.r(40, 0);
        v.maximum = f.r(41, 0);
        v.low_threshold = f.r(42, 0);
        v.high_threshold = f.r(43, 0);
        f.take(90);
        for (auto at : f.take(2))
            v.source_files.push_back(f.values[at].value);
        v.extra_fields = f.extras();
        return v;
    }
    if (kind == 14) {
        PointCloudExDefinition v;
        auto all = f.values;
        auto count = f.take(92);
        std::vector<DxfRawField> tail;
        int crop_count = 0;
        if (!count.empty()) {
            auto at = count.front();
            MLFields c({all[at]});
            crop_count = c.i(92, 0);
            if (crop_count < 0 || std::size_t(crop_count) > all.size())
                throw std::invalid_argument("Invalid cloud crop count");
            tail.assign(all.begin() + at + 1, all.end());
            f = MLFields({all.begin(), all.begin() + at});
        }
        v.class_version = f.i(70, 0);
        v.extents_min = f.p(10, {});
        v.extents_max = f.p(11, {});
        v.ucs_origin = f.p(12, {});
        v.ucs_x = f.p(210, {1, 0, 0});
        v.ucs_y = f.p(211, {0, 1, 0});
        v.ucs_z = f.p(212, {0, 0, 1});
        v.locked = f.b(290, false);
        v.definition = f.h(340, f.h(330, 0));
        v.reactor = f.h(360, 0);
        v.show_intensity = f.b(291, false);
        v.elevation_min = f.r(40, 0);
        v.elevation_max = f.r(41, 0);
        v.intensity_min = f.i(90, 0);
        v.intensity_max = f.i(91, 0);
        v.elevation_out_of_range = f.i(72, 0);
        v.elevation_fixed_range = f.b(292, false);
        v.intensity_gradient = f.b(293, false);
        v.elevation_gradient = f.b(294, false);
        v.show_cropping = f.b(295, false);
        auto names = f.take(1);
        std::string *targets[] = {&v.name, &v.intensity_scheme, &v.current_scheme, &v.classification_scheme};
        for (std::size_t i = 0; i < std::min(names.size(), std::size_t(4)); ++i)
            *targets[i] = f.values[names[i]].value;
        auto behaviors = f.take(71);
        if (!behaviors.empty()) {
            MLFields p({f.values[behaviors[0]]});
            v.stylization = p.i(71, 0);
        }
        if (behaviors.size() > 1) {
            MLFields p({f.values[behaviors[1]]});
            v.intensity_out_of_range = p.i(71, 0);
        }
        v.extra_fields = f.extras();
        std::size_t cursor = 0;
        auto one = [&](int code) {
            if (cursor >= tail.size() || tail[cursor].code != code)
                throw std::invalid_argument("Malformed point cloud crop/tail");
            return MLFields({tail[cursor++]});
        };
        auto integer = [&](int code) { return one(code).i(code, 0); };
        auto point = [&](int code) {
            auto x = one(code).r(code, 0), y = one(code + 10).r(code + 10, 0),
                 z = one(code + 20).r(code + 20, 0);
            return Point3{x, y, z};
        };
        auto bounded = [&](int code) {
            auto n = integer(code);
            if (n < 0 || std::size_t(n) > tail.size() - cursor)
                throw std::invalid_argument("Invalid point cloud collection count");
            return n;
        };
        for (int i = 0; i < crop_count; ++i) {
            PointCloudCrop c;
            c.type = integer(280);
            c.inside = integer(290) != 0;
            c.inverted = integer(290) != 0;
            c.plane = point(13);
            c.x_direction = point(213);
            c.y_direction = point(213);
            auto n = bounded(93);
            for (int j = 0; j < n; ++j)
                c.points.push_back(point(13));
            v.crops.push_back(std::move(c));
        }
        if (cursor < tail.size()) {
            auto n = bounded(93);
            for (int i = 0; i < n; ++i)
                v.hidden_scans.push_back(one(1).s(1, {}));
            n = bounded(93);
            for (int i = 0; i < n; ++i)
                v.hidden_regions.push_back(integer(93));
        }
        v.extra_fields.insert(v.extra_fields.end(), tail.begin() + cursor, tail.end());
        return v;
    }
    ViewBorderDefinition v;
    v.version = f.i(70, 0);
    auto xs = f.take(10), ys = f.take(20), values = f.take(40);
    auto real = [&](const auto &positions, std::size_t n, double fallback) {
        if (n >= positions.size())
            return fallback;
        MLFields p({f.values[positions[n]]});
        return p.r(p.values[0].code, fallback);
    };
    v.min = {real(xs, 0, 0), real(ys, 0, 0)};
    v.max = {real(xs, 1, 0), real(ys, 1, 0)};
    v.scale = real(values, 0, 1);
    v.rotation = real(values, 1, 0);
    v.center = {real(values, 2, 0), real(values, 3, 0)};
    v.viewport = f.h(330, 0);
    v.scale_handle = f.h(340, 0);
    v.extra_fields = f.extras();
    return v;
}
void DxfExtendedEntity::set_definition(const ExtendedEntityDefinition &value) {
    if (extended_index(type()) != int(value.index()))
        throw std::invalid_argument("Extended entity kind cannot change");
    DxfExtendedEntity staged = *this;
    MLFields f;
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, OleFrameDefinition>) {
                if (v.object_type < 1 || v.object_type > 3 || v.payload.size() > INT32_MAX)
                    throw std::invalid_argument("Invalid OLE type or payload size");
                f.put_i(70, v.version);
                f.put_s(3, v.source_application);
                f.put_p(10, v.upper_left);
                f.put_p(11, v.lower_right);
                f.put_i(71, v.object_type);
                f.put_i(72, v.paper_space);
                f.put_i(73, v.lock_aspect);
                if (!v.payload.empty()) {
                    f.put_i(90, v.payload.size());
                    proxy_binary(f, v.payload);
                }
                f.put_s(1, "OLE");
            } else if constexpr (std::is_same_v<T, LightDefinition>) {
                f.put_i(90, v.class_version);
                f.put_s(1, v.name);
                f.put_i(70, v.type);
                f.put_b(290, v.status);
                f.put_b(291, v.plot_glyph);
                f.put_r(40, v.intensity);
                f.put_p(10, v.position);
                f.put_p(11, v.target);
                f.put_i(72, v.attenuation);
                f.put_b(292, v.use_attenuation_limits);
                f.put_r(41, v.attenuation_start);
                f.put_r(42, v.attenuation_end);
                f.put_r(50, v.hotspot_angle);
                f.put_r(51, v.falloff_angle);
                f.put_b(293, v.cast_shadows);
                f.put_i(73, v.shadow_type);
                f.put_i(91, v.shadow_map_size);
                f.put_i(280, v.shadow_softness);
                f.put_i(63, v.color.index().value_or(v.color.approximate_index()));
                if (auto rgb = v.color.to_true_color_value())
                    f.put_i(421, *rgb);
                mt_append(f, v.extra_fields);
                if (v.photometric_mode || v.photometric) {
                    f.put_b(295, v.photometric.has_value());
                    if (v.photometric) {
                        const auto &data = *v.photometric;
                        f.put_b(290, data.has_web_file);
                        f.put_s(300, data.web_file);
                        f.put_i(70, data.intensity_method);
                        f.put_r(40, data.intensity);
                        f.put_r(41, data.illuminance_distance);
                        f.put_i(71, data.lamp_color_type);
                        f.put_r(42, data.temperature);
                        f.put_i(72, data.lamp_color_preset);
                        f.put_p(43, data.web_rotation);
                        f.put_i(73, data.shape);
                        f.put_r(46, data.length);
                        f.put_r(47, data.width);
                        f.put_r(48, data.radius);
                        f.put_i(74, data.web_file_type);
                        f.put_i(75, data.web_symmetry);
                        f.put_i(76, data.target_grip);
                        f.put_r(49, data.web_flux);
                        f.put_i(77, data.glyph_display_type);
                        for (int n = 0; n < 5; ++n)
                            f.put_r(50 + n, data.web_angles[n]);
                        mt_append(f, data.extra_fields);
                    }
                }
                return;
            } else if constexpr (std::is_same_v<T, SectionSymbolDefinition>) {
                MLFields base;
                base.put_i(70, v.view_version);
                base.put_s(340, hex_handle(v.style));
                base.put_r(40, v.scale);
                base.put_s(330, hex_handle(v.view_rep));
                base.put_i(70, v.view_flags);
                mt_append(base, v.view_extra_fields);
                staged.set_subclass_fields("AcDbViewSymbol", base.values);
                f.put_i(70, v.version);
                f.put_i(90, v.point_count);
                f.put_i(90, v.flags);
                f.put_i(90, v.point_record_count);
                mt_append(f, v.extra_fields);
                for (const auto &p : v.points) {
                    f.put_p(10, p.position);
                    f.put_r(40, p.bulge);
                    f.put_s(1, p.label);
                    f.put_p(11, p.label_offset);
                    f.put_i(280, p.flags);
                    mt_append(f, p.extra_fields);
                }
                return;
            } else if constexpr (std::is_same_v<T, ViewBorderDefinition>) {
                f.put_i(70, v.version);
                f.put_r(10, v.min.x);
                f.put_r(20, v.min.y);
                f.put_r(10, v.max.x);
                f.put_r(20, v.max.y);
                f.put_s(330, hex_handle(v.viewport));
                f.put_r(40, v.scale);
                f.put_r(40, v.rotation);
                f.put_r(40, v.center.x);
                f.put_r(40, v.center.y);
                f.put_s(340, hex_handle(v.scale_handle));
            } else if constexpr (std::is_same_v<T, PointCloudDefinition>) {
                f.put_i(70, v.class_version);
                f.put_p(10, v.origin);
                f.put_s(1, v.filename);
                if (v.source_files.size() > INT32_MAX)
                    throw std::invalid_argument("Too many cloud sources");
                f.put_i(90, v.source_files.size());
                if (v.source_files.empty()) {
                    f.put_p(11, v.extents_min);
                    f.put_p(12, v.extents_max);
                    f.put_i(92, v.point_count);
                    f.put_s(3, v.ucs_name);
                    f.put_p(13, v.ucs_origin);
                    f.put_p(210, v.ucs_x);
                    f.put_p(211, v.ucs_y);
                    f.put_p(212, v.ucs_z);
                    f.put_s(330, hex_handle(v.definition));
                    f.put_h(360, v.reactor);
                    f.put_b(290, v.show_intensity);
                    f.put_i(71, v.intensity_scheme);
                    f.put_r(40, v.minimum);
                    f.put_r(41, v.maximum);
                    f.put_r(42, v.low_threshold);
                    f.put_r(43, v.high_threshold);
                }
                for (auto &name : v.source_files)
                    f.put_s(2, name);
            } else if constexpr (std::is_same_v<T, PointCloudExDefinition>) {
                f.put_i(70, v.class_version);
                f.put_p(10, v.extents_min);
                f.put_p(11, v.extents_max);
                f.put_p(12, v.ucs_origin);
                f.put_p(210, v.ucs_x);
                f.put_p(211, v.ucs_y);
                f.put_p(212, v.ucs_z);
                f.put_b(290, v.locked);
                f.put_s(340, hex_handle(v.definition));
                f.put_s(360, hex_handle(v.reactor));
                f.put_s(1, v.name);
                f.put_b(291, v.show_intensity);
                f.put_i(71, v.stylization);
                f.put_s(1, v.intensity_scheme);
                f.put_s(1, v.current_scheme);
                f.put_s(1, v.classification_scheme);
                f.put_r(40, v.elevation_min);
                f.put_r(41, v.elevation_max);
                f.put_i(90, v.intensity_min);
                f.put_i(91, v.intensity_max);
                f.put_i(71, v.intensity_out_of_range);
                f.put_i(72, v.elevation_out_of_range);
                f.put_b(292, v.elevation_fixed_range);
                f.put_b(293, v.intensity_gradient);
                f.put_b(294, v.elevation_gradient);
                f.put_b(295, v.show_cropping);
                auto count = [&](int code, std::size_t n) {
                    if (n > INT32_MAX)
                        throw std::invalid_argument("Point cloud collection too large");
                    f.put_i(code, n);
                };
                count(92, v.crops.size());
                for (const auto &c : v.crops) {
                    f.put_i(280, c.type);
                    f.put_b(290, c.inside);
                    f.put_b(290, c.inverted);
                    f.put_p(13, c.plane);
                    f.put_p(213, c.x_direction);
                    f.put_p(213, c.y_direction);
                    count(93, c.points.size());
                    for (auto p : c.points)
                        f.put_p(13, p);
                }
                count(93, v.hidden_scans.size());
                for (const auto &s : v.hidden_scans)
                    f.put_s(1, s);
                count(93, v.hidden_regions.size());
                for (auto n : v.hidden_regions)
                    f.put_i(93, n);
            } else if constexpr (std::is_same_v<T, CameraDefinition>) {
                f.put_h(340, v.view);
            } else if constexpr (std::is_same_v<T, SectionObjectDefinition>) {
                f.put_i(90, v.state);
                f.put_i(91, v.flags);
                f.put_s(1, v.name);
                f.put_p(10, v.vertical);
                f.put_r(40, v.top);
                f.put_r(41, v.bottom);
                f.put_i(70, v.indicator_alpha);
                f.put_h(360, v.settings);
                f.put_i(62, v.indicator_color.index().value_or(v.indicator_color.approximate_index()));
                if (v.vertices.size() > INT32_MAX || v.back_vertices.size() > INT32_MAX)
                    throw std::invalid_argument("Too many section vertices");
                f.put_i(92, v.vertices.size());
                for (auto p : v.vertices)
                    f.put_p(11, p);
                f.put_i(93, v.back_vertices.size());
                for (auto p : v.back_vertices)
                    f.put_p(12, p);
            } else if constexpr (std::is_same_v<T, ArcAlignedTextDefinition>) {
                f.put_s(1, v.text);
                f.put_s(2, v.font);
                f.put_s(3, v.big_font);
                f.put_s(7, v.style);
                f.put_p(10, v.center);
                f.put_r(40, v.radius);
                f.put_r(41, v.x_scale);
                f.put_r(42, v.size);
                f.put_r(43, v.spacing);
                f.put_r(44, v.arc_offset);
                f.put_r(45, v.right_offset);
                f.put_r(46, v.left_offset);
                f.put_r(50, v.start_angle / radians_per_degree);
                f.put_r(51, v.end_angle / radians_per_degree);
                f.put_b(70, v.reverse);
                f.put_i(71, v.direction);
                f.put_i(72, v.alignment);
                f.put_i(73, v.position);
                f.put_b(74, v.bold);
                f.put_b(75, v.italic);
                f.put_b(76, v.underlined);
                f.put_i(77, v.charset);
                f.put_i(78, v.pitch_family);
                f.put_b(79, v.shx);
                f.put_i(90, v.color);
                f.put_p(210, v.normal);
                f.put_b(280, v.wizard);
                f.put_s(330, hex_handle(v.arc));
            } else if constexpr (std::is_same_v<T, RemoteTextDefinition>) {
                f.put_p(10, v.position);
                f.put_p(210, v.normal);
                f.put_r(50, v.rotation);
                f.put_r(40, v.height);
                f.put_s(7, v.style);
                f.put_i(70, v.flags);
                f.put_s(1, v.text);
            } else if constexpr (std::is_same_v<T, PositionMarkerDefinition>) {
                f.put_i(90, v.class_version);
                f.put_p(10, v.position);
                f.put_s(1, v.notes);
                f.put_i(280, v.alignment);
                f.put_r(40, v.radius);
                f.put_r(40, v.landing_gap);
                f.put_b(290, v.text_visible);
                f.put_b(290, v.frame_text);
                auto existing = staged.locate("AcDbMTextObjectEmbedded", 10);
                if (v.embedded) {
                    if (existing.index == std::string_view::npos) {
                        auto end =
                            staged.locate_if("AcDbGeoPositionMarker", [](std::size_t, int) { return false; })
                                .index;
                        staged.pairs_.insert(staged.pairs_.begin() + end, {100, "AcDbMTextObjectEmbedded"});
                    }
                    const auto &t = *v.embedded;
                    MLFields m;
                    m.put_p(10, t.insertion);
                    m.put_p(210, t.normal);
                    m.put_r(40, t.height);
                    m.put_r(41, t.width);
                    m.put_s(1, t.text);
                    m.put_s(7, t.style);
                    mt_append(m, t.extra_fields);
                    staged.set_subclass_fields("AcDbMTextObjectEmbedded", m.values);
                } else if (existing.index != std::string_view::npos) {
                    auto fields = staged.subclass_fields("AcDbMTextObjectEmbedded");
                    auto end =
                        staged.locate_if("AcDbMTextObjectEmbedded", [](std::size_t, int) { return false; })
                            .index;
                    staged.pairs_.erase(staged.pairs_.begin() + end - fields.size() - 1,
                                        staged.pairs_.begin() + end);
                }
            } else if constexpr (std::is_same_v<T, CoordinationModelDefinition>) {
                f.put_i(70, v.flags);
                f.put_h(340, v.definition);
                for (auto n : v.transform)
                    f.put_r(40, n);
                f.put_r(40, v.unit_factor);
            } else if constexpr (std::is_same_v<T, OleLegacyDefinition>) {
                f.put_i(70, v.flags);
                f.put_i(71, v.mode);
                if (v.payload.size() > INT32_MAX)
                    throw std::invalid_argument("OLE payload too large");
                f.put_i(90, v.payload.size());
                proxy_binary(f, v.payload);
            } else if constexpr (std::is_same_v<T, LayoutPrintDefinition>) {
                f.put_i(93, v.flags);
            } else if constexpr (std::is_same_v<T, FormatEntityDefinition>) {
            }
            mt_append(f, v.extra_fields);
        },
        value);
    staged.set_subclass_fields(extended_scopes[value.index()], f.values);
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_extended_entity(const ExtendedEntityDefinition &v,
                                               const EntityOptions &options) {
    if (version_ < DxfVersion::AC1027)
        throw std::invalid_argument("Extended entity construction requires R2013+");
    std::visit(
        [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ViewBorderDefinition>) {
                mleader_check_handle(*this, value.viewport, "VIEWPORT");
                mleader_check_handle(*this, value.scale_handle, "SCALE");
            } else if constexpr (std::is_same_v<T, CameraDefinition>)
                mleader_check_handle(*this, value.view, "VIEW");
            else if constexpr (std::is_same_v<T, SectionObjectDefinition>) {
                if (value.settings)
                    throw std::invalid_argument("New section cannot adopt owned settings");
            } else if constexpr (std::is_same_v<T, ArcAlignedTextDefinition>) {
                mleader_check_handle(*this, value.arc, "ARC");
                if (!value.style.empty() && !get_text_style(value.style))
                    throw std::invalid_argument("Missing arc text style");
            } else if constexpr (std::is_same_v<T, RemoteTextDefinition>) {
                if (!value.style.empty() && !get_text_style(value.style))
                    throw std::invalid_argument("Missing remote text style");
            } else if constexpr (std::is_same_v<T, CoordinationModelDefinition>) {
                mleader_check_handle(*this, value.definition, "NAVISWORKSMODELDEF");
            } else if constexpr (std::is_same_v<T, PointCloudDefinition> ||
                                 std::is_same_v<T, PointCloudExDefinition>) {
                mleader_check_handle(*this, value.definition,
                                     std::is_same_v<T, PointCloudDefinition> ? "ACDBPOINTCLOUDDEF"
                                                                             : "ACDBPOINTCLOUDDEF_EX");
                if (value.reactor)
                    throw std::invalid_argument("New cloud cannot adopt an owned reactor");
            } else if constexpr (std::is_same_v<T, SectionSymbolDefinition>) {
                for (auto h : {value.style, value.view_rep})
                    if (h && records_with_handle(hex_handle(h)).size() != 1)
                        throw std::invalid_argument("Missing section symbol reference");
            }
        },
        v);
    DxfExtendedEntity entity;
    static_cast<DxfEntity &>(entity) = make_entity(
        extended_names[v.index()], v.index() == 2 ? "AcDbViewSymbol" : extended_scopes[v.index()], options);
    if (v.index() == 2)
        entity.pairs_.push_back({100, "AcDbSectionSymbol"});
    entity.set_definition(v);
    if (v.index() == 13 || v.index() == 14) {
        DxfDocument staged = *this;
        auto id = staged.insert_entity(std::move(entity), options);
        auto value = v;
        std::uint64_t def = 0;
        std::visit(
            [&](auto &p) {
                using T = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<T, PointCloudDefinition> ||
                              std::is_same_v<T, PointCloudExDefinition>) {
                    def = p.definition;
                    if (def) {
                        const bool extended = std::is_same_v<T, PointCloudExDefinition>;
                        DxfDataObject reactor;
                        static_cast<DxfRecord &>(reactor) = staged.make_record(
                            extended ? "ACDBPOINTCLOUDDEF_REACTOR_EX" : "ACDBPOINTCLOUDDEF_REACTOR");
                        reactor.set_field("", 330, hex_handle(id));
                        reactor.set_definition({CloudReactorDefinition{extended, 1}, {}});
                        p.reactor = staged.insert_object(reactor);
                    }
                }
            },
            value);
        staged.for_each<DxfExtendedEntity>([&](auto &e) {
            if (handle_value(e.handle()) == id)
                e.set_definition(value);
        });
        staged.link_image_reactors(def);
        *this = std::move(staged);
        return id;
    }
    return insert_entity(std::move(entity), options);
}
