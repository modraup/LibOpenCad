// SPDX-License-Identifier: MPL-2.0
// Native encode/rewrite for typed DWG class objects, including ViewRep.
namespace {
struct ClassObjectWriter {
    DwgBitWriter main, refs, strings;
    unsigned v{};
    const char *page = nullptr;
    void str(std::string_view s) { symbol_string(main, strings, v, s, page); }
    void href(unsigned code, std::uint64_t h) { refs.absolute_handle(code, h); }
    void bit(bool value) { main.bits(1, value); }
    void byte(std::uint8_t value) { main.bits(8, value); }
    void vec(Vector3 p) { vector_bits(main, p); }
    void raw2(Vector2 p) {
        raw_double(main, p.x);
        raw_double(main, p.y);
    }
    void require_view_rep_n(std::int32_t count) {
        require(count >= 0 && count <= 100000, "Invalid view representation count");
    }
    void require_view_rep_path_n(std::int32_t count) {
        require(count >= 0 && count <= 10000, "Invalid view representation path count");
    }
    void require_finite(double value) {
        require(std::isfinite(value), "Nonfinite view representation field");
    }
    void require_finite_vec(Vector3 value) {
        require_finite(value.x);
        require_finite(value.y);
        require_finite(value.z);
    }
    void write_view_rep_guid(const ViewRepGuid &guid) {
        main.bit_long(guid.data1);
        main.bit_short(guid.data2);
        main.bit_short(guid.data3);
        for (auto item : guid.data4)
            byte(item);
    }
    void write_view_rep_geometry(const ViewRepSketchGeometry &geometry) {
        if (geometry.kind == ViewRepSketchGeometry::Kind::Line) {
            require_finite_vec(geometry.first);
            require_finite_vec(geometry.second);
            main.bit_long(geometry.type_code);
            vec(geometry.first);
            vec(geometry.second);
            return;
        }
        if (geometry.kind == ViewRepSketchGeometry::Kind::Circle) {
            require_finite_vec(geometry.center);
            require_finite_vec(geometry.normal);
            require_finite_vec(geometry.direction);
            require_finite(geometry.radius);
            require_finite(geometry.start_parameter);
            require_finite(geometry.end_parameter);
            require_finite(geometry.reserved);
            main.bit_long(geometry.type_code);
            vec(geometry.center);
            vec(geometry.normal);
            vec(geometry.direction);
            main.bit_double(geometry.radius);
            main.bit_double(geometry.start_parameter);
            main.bit_double(geometry.end_parameter);
            main.bit_double(geometry.reserved);
            return;
        }
        if (geometry.kind == ViewRepSketchGeometry::Kind::Nurb) {
            require_view_rep_n(geometry.knot_header[0]);
            require_view_rep_n(geometry.weight_header[0]);
            require_view_rep_n(geometry.point_header[0]);
            require(geometry.knots.size() == static_cast<std::size_t>(geometry.knot_header[0]) &&
                        geometry.weights.size() == static_cast<std::size_t>(geometry.weight_header[0]) &&
                        geometry.control_points.size() == static_cast<std::size_t>(geometry.point_header[0]),
                    "Invalid view representation spline");
            require_finite(geometry.tolerance);
            for (auto item : geometry.knots)
                require_finite(item);
            for (auto item : geometry.weights)
                require_finite(item);
            for (auto point : geometry.control_points)
                require_finite_vec(point);
            main.bit_long(geometry.type_code);
            bit(geometry.flags[0]);
            bit(geometry.flags[1]);
            main.bit_short(static_cast<std::int16_t>(geometry.degree));
            main.bit_double(geometry.tolerance);
            for (auto item : geometry.knot_header)
                main.bit_long(item);
            for (auto item : geometry.knots)
                main.bit_double(item);
            for (auto item : geometry.weight_header)
                main.bit_long(item);
            for (auto item : geometry.weights)
                main.bit_double(item);
            for (auto item : geometry.point_header)
                main.bit_long(item);
            for (auto point : geometry.control_points)
                vec(point);
            return;
        }
        main.bit_long(0);
    }
    void write_view_rep(const ViewRep &value) {
        require_view_rep_n(static_cast<std::int32_t>(value.sketches.size()));
        require_view_rep_n(static_cast<std::int32_t>(value.section_sketches.size()));
        for (auto item : value.header_values)
            main.bit_long(item);
        str(value.name);
        main.bit_long(value.scale);
        main.bit_long(value.header_status);
        str(value.description);
        main.bit_long_long(value.source_id);
        bit(value.source_enabled);
        main.bit_long(value.source_version);
        main.bit_long_long(value.model_id);
        write_view_rep_guid(value.guid);
        byte(value.marker);
        for (auto item : value.transform) {
            require_finite(item);
            main.bit_double(item);
        }
        main.bit_long(value.transform_version);
        main.bit_long_long(value.database_id);
        main.bit_long(value.geometry_version);
        main.bit_long(value.geometry_marker);
        main.bit_long(static_cast<std::int32_t>(value.sketches.size()));
        for (const auto &sketch : value.sketches) {
            require_view_rep_n(static_cast<std::int32_t>(sketch.references.size()));
            main.bit_long(sketch.id);
            main.bit_long(sketch.version);
            main.bit_long(static_cast<std::int32_t>(sketch.references.size()));
            for (const auto &reference : sketch.references) {
                href(4, reference.object);
                bit(reference.flag);
            }
            main.bit_long(sketch.reserved);
            bit(sketch.enabled);
            write_view_rep_geometry(sketch.geometry);
            bit(sketch.final_flag);
        }
        for (auto handle : value.related_objects)
            href(4, handle);
        href(5, value.source_manager);
        for (auto handle : value.owned_objects)
            href(3, handle);
        for (auto handle : value.optional_objects)
            href(4, handle);
        require_finite(value.position.x);
        require_finite(value.position.y);
        require_finite(value.rotation);
        raw2(value.position);
        main.bit_double(value.rotation);
        href(5, value.orientation);
        bit(value.is_active);
        main.bit_short(value.projection);
        for (auto handle : value.linked_views)
            href(4, handle);
        main.bit_long(static_cast<std::int32_t>(value.section_sketches.size()));
        for (const auto &path : value.section_sketches) {
            auto extra = path.objects.empty() ? 0 : static_cast<std::int32_t>(path.objects.size() - 1);
            require_view_rep_path_n(extra);
            str(path.class_name);
            main.bit_short(static_cast<std::int16_t>(extra));
            for (auto handle : path.objects)
                href(4, handle);
        }
        main.bit_long(value.action_mode);
        if (value.action)
            href(3, *value.action);
        bit(value.has_parent);
        href(4, value.parent);
        main.bit_long(value.tail_version);
        main.bit_long(value.tail_state);
        main.bit_long_long(value.tail_id);
        main.bit_long(value.path_count);
        main.bit_long(value.path_version);
        main.bit_long_long(value.path_id);
        bit(value.has_block_path);
        if (value.block_path) {
            require_view_rep_n(static_cast<std::int32_t>(value.block_path->entries.size()));
            str(value.block_path->class_name);
            main.bit_long(value.block_path->version);
            main.bit_long(static_cast<std::int32_t>(value.block_path->entries.size()));
            for (const auto &entry : value.block_path->entries) {
                byte(entry.flag);
                byte(entry.kind);
                href(4, entry.object);
            }
        }
        href(5, value.style);
    }
    void write_render_settings(const RenderSettings &value, bool rapid_rt, bool write_predefined_bit) {
        auto class_version = !rapid_rt && write_predefined_bit ? value.class_version + 1 : value.class_version;
        main.bit_long(class_version);
        str(value.name);
        bit(value.fog_enabled);
        bit(value.fog_background_enabled);
        bit(value.backfaces_enabled);
        bit(value.environment_image_enabled);
        str(value.environment_image_filename);
        str(value.description);
        main.bit_long(value.display_index);
        if (write_predefined_bit)
            bit(value.has_predefined);
    }
    void write_model_doc_view_style(const ModelDocViewStyle &value) {
        main.bit_short(value.class_version);
        str(value.description);
        bit(value.modified_for_recompute);
        if (v >= 1032) {
            str(value.display_name_or_description());
            main.bit_long(value.flags);
        }
    }
    void write_bytes(std::string_view bytes) {
        for (unsigned char byte : bytes)
            main.bits(8, byte);
    }
    std::string utf8_to_utf16le(std::string_view text) {
        std::string out;
        for (char32_t scalar : detail::utf8_scalars(text)) {
            if (scalar >= 0x10000) {
                scalar -= 0x10000;
                auto high = static_cast<std::uint16_t>(0xd800 | (scalar >> 10));
                auto low = static_cast<std::uint16_t>(0xdc00 | (scalar & 0x3ff));
                out.push_back(static_cast<char>(high & 255));
                out.push_back(static_cast<char>(high >> 8));
                out.push_back(static_cast<char>(low & 255));
                out.push_back(static_cast<char>(low >> 8));
            } else {
                auto unit = static_cast<std::uint16_t>(scalar);
                out.push_back(static_cast<char>(unit & 255));
                out.push_back(static_cast<char>(unit >> 8));
            }
        }
        return out;
    }
    void write_object_data_payload(bool record, const std::vector<std::uint8_t> &payload) {
        require(payload.size() <= 10000000, "Invalid object data payload");
        main.bit_short(516);
        main.bit_long(record ? 1 : 0);
        main.bit_long(0);
        main.bit_long(static_cast<std::int32_t>(payload.size() + 2));
        for (auto byte : payload)
            main.bits(8, byte);
    }
    void write_cad_string(std::string_view text) {
        if (v >= 1021) {
            auto bytes = utf8_to_utf16le(text);
            bytes.push_back(0);
            bytes.push_back(0);
            main.bit_long(static_cast<std::int32_t>(bytes.size()));
            write_bytes(bytes);
            return;
        }
        auto encoded = detail::to_legacy(text, page);
        main.bit_long(static_cast<std::int32_t>(encoded.size() + 1));
        write_bytes(encoded);
        main.bits(8, 0);
    }
    void write_cad_value(const TableValue &value) {
        if (v >= 1021)
            main.bit_long(value.flags);
        main.bit_long(value.type);
        if (v < 1021 || (value.flags & 1) == 0) {
            switch (value.type) {
            case 0:
            case 1:
                require(std::isfinite(value.number), "Nonfinite data link integer");
                main.bit_long(static_cast<std::int32_t>(
                    std::max(double(INT32_MIN), std::min(double(INT32_MAX), value.number))));
                break;
            case 2:
                main.bit_double(value.number);
                break;
            case 4:
            case 0x200:
                write_cad_string(value.text);
                break;
            case 8: {
                auto size = value.data_size > 0 &&
                                    static_cast<std::size_t>(value.data_size) == value.binary.size()
                                ? value.data_size
                                : static_cast<std::int32_t>(value.binary.size());
                main.bit_long(size);
                write_bytes(std::string(value.binary.begin(), value.binary.end()));
                break;
            }
            case 0x10:
                main.bit_long(value.data_size ? value.data_size : 16);
                raw_double(main, value.point.x);
                raw_double(main, value.point.y);
                break;
            case 0x20:
                main.bit_long(value.data_size ? value.data_size : 24);
                raw_double(main, value.point.x);
                raw_double(main, value.point.y);
                raw_double(main, value.point.z);
                break;
            case 0x40:
                href(4, value.handle);
                break;
            default:
                break;
            }
        }
        if (v >= 1021) {
            main.bit_long(value.unit);
            str(value.format);
            if (value.unit != 12)
                str(value.formatted);
        }
    }
    void write(const ClassObjectData &data) {
        if (auto *settings = std::get_if<RenderSettings>(&data)) {
            write_render_settings(*settings, false, v >= 1027);
            return;
        }
        if (auto *settings = std::get_if<MentalRayRenderSettings>(&data)) {
            write_render_settings(settings->base, false, false);
            main.bit_long(settings->version);
            main.bit_long(settings->sampling_min);
            main.bit_long(settings->sampling_max);
            main.bit_short(settings->sampling_filter);
            main.bit_double(settings->sampling_filter_width);
            main.bit_double(settings->sampling_filter_height);
            for (auto component : settings->sampling_contrast)
                main.bit_double(component);
            main.bit_short(settings->shadow_mode);
            bit(settings->shadow_maps_enabled);
            bit(settings->ray_tracing_enabled);
            for (auto depth : settings->ray_trace_depth)
                main.bit_long(depth);
            bit(settings->global_illumination_enabled);
            main.bit_long(settings->global_illumination_sample_count);
            bit(settings->global_illumination_sample_radius_enabled);
            main.bit_double(settings->global_illumination_sample_radius);
            main.bit_long(settings->photons_per_light);
            for (auto depth : settings->photon_trace_depth)
                main.bit_long(depth);
            bit(settings->final_gathering_enabled);
            main.bit_long(settings->final_gathering_ray_count);
            for (auto state : settings->final_gathering_sample_radius_state)
                bit(state);
            for (auto radius : settings->final_gathering_sample_radius)
                main.bit_double(radius);
            main.bit_double(settings->light_luminance_scale);
            main.bit_short(settings->diagnostics_mode);
            main.bit_short(settings->diagnostics_grid_mode);
            main.bit_double(settings->diagnostics_grid_size);
            main.bit_short(settings->diagnostics_photon_mode);
            main.bit_short(settings->diagnostics_bsp_mode);
            bit(settings->export_mi_enabled);
            str(settings->description);
            main.bit_long(settings->tile_size);
            main.bit_short(settings->tile_order);
            main.bit_long(settings->memory_limit);
            bit(settings->diagnostics_samples_mode);
            main.bit_double(settings->energy_multiplier);
            return;
        }
        if (auto *settings = std::get_if<RapidRtRenderSettings>(&data)) {
            write_render_settings(settings->base, true, false);
            main.bit_long(settings->version);
            main.bit_long(settings->render_target);
            main.bit_long(settings->render_level);
            main.bit_long(settings->render_time);
            main.bit_long(settings->lighting_model);
            main.bit_long(settings->filter_type);
            main.bit_double(settings->filter_width);
            main.bit_double(settings->filter_height);
            bit(settings->base.has_predefined);
            return;
        }
        if (auto *background = std::get_if<GradientBackground>(&data)) {
            main.bit_long(background->class_version);
            main.bit_long(static_cast<std::int32_t>(background->color_top));
            main.bit_long(static_cast<std::int32_t>(background->color_middle));
            main.bit_long(static_cast<std::int32_t>(background->color_bottom));
            main.bit_double(background->horizon);
            main.bit_double(background->height);
            main.bit_double(background->rotation);
            return;
        }
        if (auto *background = std::get_if<GroundPlaneBackground>(&data)) {
            main.bit_long(background->class_version);
            main.bit_long(static_cast<std::int32_t>(background->color_sky_zenith));
            main.bit_long(static_cast<std::int32_t>(background->color_sky_horizon));
            main.bit_long(static_cast<std::int32_t>(background->color_underground_horizon));
            main.bit_long(static_cast<std::int32_t>(background->color_underground_azimuth));
            main.bit_long(static_cast<std::int32_t>(background->color_near));
            main.bit_long(static_cast<std::int32_t>(background->color_far));
            return;
        }
        if (auto *background = std::get_if<IblBackground>(&data)) {
            main.bit_long(background->class_version);
            bit(background->enabled);
            str(background->name);
            main.bit_double(background->rotation);
            bit(background->display_image);
            href(5, background->secondary_background);
            return;
        }
        if (auto *background = std::get_if<ImageBackground>(&data)) {
            main.bit_long(background->class_version);
            str(background->filename);
            bit(background->fit_to_screen);
            bit(background->maintain_aspect_ratio);
            bit(background->use_tiling);
            main.bit_double(background->offset.x);
            main.bit_double(background->offset.y);
            main.bit_double(background->scale.x);
            main.bit_double(background->scale.y);
            return;
        }
        if (auto *background = std::get_if<SkyLightBackground>(&data)) {
            main.bit_long(background->class_version);
            href(5, background->sun);
            return;
        }
        if (auto *background = std::get_if<SolidBackground>(&data)) {
            main.bit_long(background->class_version);
            main.bit_long(static_cast<std::int32_t>(background->color));
            return;
        }
        if (auto *entry = std::get_if<RenderEntry>(&data)) {
            main.bit_long(entry->class_version);
            str(entry->image_filename);
            str(entry->preset_name);
            str(entry->view_name);
            main.bit_long(entry->width);
            main.bit_long(entry->height);
            main.bit_short(entry->start_year);
            main.bit_short(entry->start_month);
            main.bit_short(entry->start_day);
            main.bit_short(entry->start_hour);
            main.bit_short(entry->start_minute);
            main.bit_short(entry->start_second);
            main.bit_short(entry->start_millisecond);
            main.bit_short(entry->end_year);
            main.bit_short(entry->end_month);
            main.bit_short(entry->end_day);
            main.bit_short(entry->end_hour);
            main.bit_short(entry->end_minute);
            main.bit_short(entry->end_second);
            main.bit_short(entry->end_millisecond);
            main.bit_double(entry->render_time);
            main.bit_long(entry->memory_amount);
            main.bit_long(entry->material_count);
            main.bit_long(entry->light_count);
            main.bit_long(entry->triangle_count);
            main.bit_long(entry->display_index);
            return;
        }
        if (auto *environment = std::get_if<RenderEnvironment>(&data)) {
            main.bit_long(environment->class_version);
            bit(environment->fog_enabled);
            bit(environment->fog_background_enabled);
            for (auto component : environment->fog_color)
                main.bits(8, component);
            main.bit_double(environment->fog_density_near);
            main.bit_double(environment->fog_density_far);
            main.bit_double(environment->fog_distance_near);
            main.bit_double(environment->fog_distance_far);
            bit(environment->environment_image_enabled);
            str(environment->environment_image_filename);
            return;
        }
        if (auto *global = std::get_if<RenderGlobal>(&data)) {
            main.bit_long(global->class_version);
            main.bit_long(global->procedure);
            main.bit_long(global->destination);
            bit(global->save_enabled);
            str(global->save_filename);
            main.bit_long(global->image_width);
            main.bit_long(global->image_height);
            bit(global->predefined_presets_first);
            bit(global->high_level_info);
            return;
        }
        if (auto *lights = std::get_if<LightList>(&data)) {
            require(lights->lights.size() <= 100000, "Invalid light list count");
            main.bit_long(lights->class_version);
            main.bit_long(static_cast<std::int32_t>(lights->lights.size()));
            for (const auto &light : lights->lights) {
                href(5, light.handle);
                str(light.name);
            }
            return;
        }
        if (auto *sun = std::get_if<Sun>(&data)) {
            main.bit_long(sun->class_version);
            bit(sun->is_on);
            write_cm_color(main, sun->color);
            main.bit_double(sun->intensity);
            bit(sun->has_shadow);
            main.bit_long(sun->julian_day);
            main.bit_long(sun->milliseconds);
            bit(sun->is_daylight_savings_on);
            main.bit_long(sun->shadow_type);
            main.bit_short(sun->shadow_map_size);
            main.bits(8, sun->shadow_softness);
            return;
        }
        if (auto *path = std::get_if<MotionPath>(&data)) {
            main.bit_long(path->class_version);
            href(5, path->camera_path);
            href(5, path->target_path);
            href(5, path->view);
            main.bit_short(path->frames);
            main.bit_short(path->frame_rate);
            bit(path->corner_deceleration);
            return;
        }
        if (auto *path = std::get_if<CurvePath>(&data)) {
            main.bit_long(path->class_version);
            href(5, path->entity);
            return;
        }
        if (auto *path = std::get_if<PointPath>(&data)) {
            main.bit_short(path->class_version);
            main.bit_double(path->point.x);
            main.bit_double(path->point.y);
            main.bit_double(path->point.z);
            return;
        }
        if (auto *device = std::get_if<TvDeviceProperties>(&data)) {
            main.bit_long(static_cast<std::int32_t>(device->flags));
            main.bit_short(device->max_regen_threads);
            main.bit_long(device->use_lut_palette);
            main.bit_long_long(device->alternate_highlight);
            main.bit_long_long(device->alternate_highlight_color);
            main.bit_long_long(device->geometry_shader_usage);
            main.bit_long(device->blending_mode);
            main.bit_double(device->antialiasing_level);
            main.bit_double(device->reserved_double);
            return;
        }
        if (auto *filter = std::get_if<LayerFilter>(&data)) {
            require(filter->names.size() <= 100000, "Invalid layer filter count");
            main.bit_long(static_cast<std::int32_t>(filter->names.size()));
            for (const auto &name : filter->names)
                str(name);
            return;
        }
        if (auto *index = std::get_if<PartialViewingIndex>(&data)) {
            require(index->entries.size() <= 100000, "Invalid partial viewing index count");
            main.bit_long(static_cast<std::int32_t>(index->entries.size()));
            if (!index->entries.empty())
                bit(index->has_entries);
            for (const auto &entry : index->entries) {
                main.bit_double(entry.extents_min.x);
                main.bit_double(entry.extents_min.y);
                main.bit_double(entry.extents_min.z);
                main.bit_double(entry.extents_max.x);
                main.bit_double(entry.extents_max.y);
                main.bit_double(entry.extents_max.z);
                href(5, entry.object);
            }
            return;
        }
        if (auto *settings = std::get_if<SectionSettings>(&data)) {
            require(settings->types.size() <= 4, "Invalid section settings count");
            main.bit_long(settings->current_type);
            main.bit_long(static_cast<std::int32_t>(settings->types.size()));
            for (const auto &type : settings->types) {
                require(type.sources.size() <= 100000 && type.geometry.size() <= 100000,
                        "Invalid section settings source count");
                main.bit_long(type.section_type);
                main.bit_long(type.generation);
                main.bit_long(static_cast<std::int32_t>(type.sources.size()));
                for (auto source : type.sources)
                    href(5, source);
                href(4, type.destination_block);
                str(type.destination_file);
                main.bit_long(static_cast<std::int32_t>(type.geometry.size()));
                for (const auto &geometry : type.geometry) {
                    main.bit_long(geometry.geometry_count);
                    main.bit_long(geometry.index);
                    main.bit_long(geometry.flags);
                    write_cm_color(main, geometry.color);
                    str(geometry.layer);
                    str(geometry.linetype);
                    main.bit_double(geometry.linetype_scale);
                    str(geometry.plot_style);
                    main.bit_long(geometry.lineweight);
                    main.bit_short(geometry.face_transparency);
                    main.bit_short(geometry.edge_transparency);
                    main.bit_short(geometry.hatch_type);
                    str(geometry.hatch_pattern);
                    main.bit_double(geometry.hatch_angle);
                    main.bit_double(geometry.hatch_spacing);
                    main.bit_double(geometry.hatch_scale);
                }
            }
            return;
        }
        if (auto *study = std::get_if<SunStudy>(&data)) {
            require(study->dates.size() <= 10000 && study->hours.size() <= 10000,
                    "Invalid sun study date count");
            main.bit_long(study->class_version);
            str(study->setup_name);
            str(study->description);
            main.bit_long(study->output_type);
            if (study->output_type == 0) {
                bit(study->use_subset);
                str(study->sheet_set_name);
                str(study->sheet_subset_name);
            }
            bit(study->select_dates_from_calendar);
            main.bit_long(static_cast<std::int32_t>(study->dates.size()));
            for (const auto &date : study->dates) {
                main.bit_long(date.julian_day);
                main.bit_long(date.milliseconds);
            }
            bit(study->select_range_of_dates);
            if (study->select_range_of_dates) {
                main.bit_long(study->start_time);
                main.bit_long(study->end_time);
                main.bit_long(study->interval);
            }
            main.bit_long(static_cast<std::int32_t>(study->hours.size()));
            for (bool hour : study->hours)
                bit(hour);
            main.bit_long(study->shade_plot_type);
            main.bit_long(study->viewport_count);
            main.bit_long(study->rows);
            main.bit_long(study->columns);
            main.bit_double(study->spacing);
            bit(study->lock_viewports);
            bit(study->label_viewports);
            href(5, study->page_setup_wizard);
            href(5, study->view);
            href(2, study->visual_style);
            href(2, study->text_style);
            return;
        }
        if (auto *table = std::get_if<DataTable>(&data)) {
            require(table->row_count >= 0 && static_cast<std::uint32_t>(table->row_count) <= 100000 &&
                        table->columns.size() <= 100000,
                    "Invalid data table count");
            main.bit_short(table->flags);
            main.bit_long(static_cast<std::int32_t>(table->columns.size()));
            main.bit_long(table->row_count);
            str(table->name);
            for (const auto &column : table->columns) {
                require(data_table_has_native_codec(column.value_type) &&
                            column.rows.size() == static_cast<std::size_t>(table->row_count),
                        "Invalid data table column");
                main.bit_long(column.value_type);
                str(column.name);
                for (const auto &row : column.rows) {
                    switch (static_cast<DataTableCellType>(column.value_type)) {
                    case DataTableCellType::Integer:
                        main.bit_long(row.integer);
                        break;
                    case DataTableCellType::Double:
                        main.bit_double(row.real);
                        break;
                    case DataTableCellType::Text:
                        str(row.text);
                        break;
                    case DataTableCellType::Point:
                        main.bit_double(row.point.x);
                        main.bit_double(row.point.y);
                        main.bit_double(row.point.z);
                        break;
                    case DataTableCellType::ObjectId:
                        href(4, row.handle);
                        break;
                    }
                }
            }
            return;
        }
        if (auto *image = std::get_if<GeoMapImage>(&data)) {
            main.bit_long(image->class_version);
            raw_double(main, image->origin.x);
            raw_double(main, image->origin.y);
            raw_double(main, image->origin.z);
            raw_double(main, image->image_size.x);
            raw_double(main, image->image_size.y);
            main.bit_short(image->display_properties);
            bit(image->clipping_enabled);
            main.bits(8, image->brightness);
            main.bits(8, image->contrast);
            main.bits(8, image->fade);
            return;
        }
        if (auto *style = std::get_if<DetailViewStyle>(&data)) {
            write_model_doc_view_style(style->base);
            main.bit_short(style->class_version);
            main.bit_long(style->flags);
            href(5, style->identifier_style);
            write_cm_color(main, style->identifier_color);
            main.bit_double(style->identifier_height);
            str(style->identifier_excluded_characters);
            main.bit_double(style->identifier_offset);
            main.bits(8, style->identifier_placement);
            href(5, style->arrow_symbol);
            write_cm_color(main, style->arrow_symbol_color);
            main.bit_double(style->arrow_symbol_size);
            href(5, style->boundary_linetype);
            main.bit_long(style->boundary_lineweight);
            write_cm_color(main, style->boundary_color);
            href(5, style->view_label_text_style);
            write_cm_color(main, style->view_label_text_color);
            main.bit_double(style->view_label_text_height);
            main.bit_long(style->view_label_attachment);
            main.bit_double(style->view_label_offset);
            main.bit_long(style->view_label_alignment);
            str(style->view_label_pattern);
            href(5, style->connection_linetype);
            main.bit_long(style->connection_lineweight);
            write_cm_color(main, style->connection_color);
            href(5, style->border_linetype);
            main.bit_long(style->border_lineweight);
            write_cm_color(main, style->border_color);
            main.bits(8, style->model_edge);
            return;
        }
        if (auto *style = std::get_if<SectionViewStyle>(&data)) {
            require(style->hatch_angles.size() <= 100000, "Invalid hatch angle count");
            write_model_doc_view_style(style->base);
            main.bit_short(style->class_version);
            main.bit_long(style->flags);
            href(5, style->identifier_style);
            write_cm_color(main, style->identifier_color);
            main.bit_double(style->identifier_height);
            href(5, style->arrow_start_symbol);
            href(5, style->arrow_end_symbol);
            write_cm_color(main, style->arrow_symbol_color);
            main.bit_double(style->arrow_symbol_size);
            str(style->identifier_excluded_characters);
            main.bit_double(style->arrow_symbol_extension_length);
            href(5, style->plane_linetype);
            main.bit_long(style->plane_lineweight);
            write_cm_color(main, style->plane_color);
            href(5, style->bend_linetype);
            main.bit_long(style->bend_lineweight);
            write_cm_color(main, style->bend_color);
            main.bit_double(style->bend_line_length);
            main.bit_double(style->end_line_length);
            href(5, style->view_label_text_style);
            write_cm_color(main, style->view_label_text_color);
            main.bit_double(style->view_label_text_height);
            main.bit_long(style->view_label_attachment);
            main.bit_double(style->view_label_offset);
            main.bit_long(style->view_label_alignment);
            str(style->view_label_pattern);
            write_cm_color(main, style->hatch_color);
            write_cm_color(main, style->hatch_background_color);
            str(style->hatch_pattern);
            main.bit_double(style->hatch_scale);
            main.bit_long(style->hatch_transparency);
            bit(style->reserved_flags[0]);
            bit(style->reserved_flags[1]);
            main.bit_long(style->identifier_position);
            main.bit_double(style->identifier_offset);
            main.bit_long(style->arrow_position);
            main.bit_double(style->end_line_overshoot);
            main.bit_long(static_cast<std::int32_t>(style->hatch_angles.size()));
            for (auto angle : style->hatch_angles)
                main.bit_double(angle);
            return;
        }
        if (auto *dictionary = std::get_if<ObjectDataDictionary>(&data)) {
            require(dictionary->entries.size() <= 100000, "Invalid object data count");
            auto entries = dictionary->entries;
            std::sort(entries.begin(), entries.end(),
                      [](const auto &a, const auto &b) { return a.first < b.first; });
            main.bit_long(static_cast<std::int32_t>(entries.size()));
            main.bit_short(dictionary->duplicate_cloning);
            main.bits(8, dictionary->hard_owner);
            for (const auto &entry : entries) {
                str(entry.first);
                href(2, entry.second);
            }
            main.bit_double(0);
            return;
        }
        if (auto *descriptor = std::get_if<ObjectDataDescriptor>(&data)) {
            write_object_data_payload(false, descriptor->payload);
            main.bit_long(descriptor->kind);
            str(descriptor->name);
            main.bit_long(0);
            return;
        }
        if (auto *record = std::get_if<ObjectDataRecord>(&data)) {
            write_object_data_payload(true, record->payload);
            href(4, record->table);
            return;
        }
        if (auto *manager = std::get_if<ViewRepSourceManager>(&data)) {
            bit(manager->has_source);
            href(2, manager->source);
            main.bit_long(manager->status);
            return;
        }
        if (auto *standard = std::get_if<ViewRepStandard>(&data)) {
            for (auto item : standard->values)
                main.bit_long(item);
            return;
        }
        if (std::holds_alternative<ViewRepOrientationDefinition>(data))
            return;
        if (auto *orientation = std::get_if<ViewRepOrientation>(&data)) {
            require_finite_vec(orientation->camera);
            require_finite_vec(orientation->target);
            require_finite_vec(orientation->normal);
            vec(orientation->camera);
            vec(orientation->target);
            vec(orientation->normal);
            return;
        }
        if (auto *section = std::get_if<ViewRepSectionDefinition>(&data)) {
            require_finite(section->section_depth);
            main.bit_long(section->version);
            main.bit_double(section->section_depth);
            main.bit_long(section->flags[0]);
            main.bit_long(section->flags[1]);
            return;
        }
        if (auto *set = std::get_if<ViewRepModelSpaceViewSelectionSet>(&data)) {
            require_view_rep_n(static_cast<std::int32_t>(set->entities.size()));
            main.bit_long(set->version);
            main.bit_long(static_cast<std::int32_t>(set->entities.size()));
            for (auto handle : set->entities)
                href(4, handle);
            return;
        }
        if (auto *source = std::get_if<ViewRepModelSpaceSource>(&data)) {
            for (auto item : source->transform)
                require_finite(item);
            bit(source->enabled);
            for (auto item : source->header_values)
                main.bit_long(item);
            for (auto item : source->transform)
                main.bit_double(item);
            main.bit_long(source->source_version);
            main.bit_long(source->source_status);
            main.absolute_handle(0, source->model);
            write_view_rep_guid(source->guid);
            for (auto handle : source->references)
                href(4, handle);
            for (auto item : source->tail_values)
                main.bit_long(item);
            href(2, source->orientation);
            return;
        }
        if (auto *view = std::get_if<ViewRep>(&data)) {
            write_view_rep(*view);
            return;
        }
        if (auto *link = std::get_if<DataLink>(&data)) {
            require(link->custom_data.size() <= 100000 && link->data_map.size() <= 100000,
                    "Invalid data link count");
            str(link->data_adapter);
            str(link->description);
            str(link->tooltip);
            str(link->connection_string);
            auto has_map = !link->data_map.empty();
            auto option = has_map ? link->option | DataLink::has_data_map
                                  : link->option & ~DataLink::has_data_map;
            main.bit_long(option);
            main.bit_long(link->update_option);
            main.bit_long(link->flags);
            main.bit_short(link->year);
            main.bit_short(link->month);
            main.bit_short(link->day);
            main.bit_short(link->hour);
            main.bit_short(link->minute);
            main.bit_short(link->second);
            main.bit_short(link->millisecond);
            main.bit_short(link->path_option);
            main.bit_long(link->status_flags);
            str(link->update_status);
            main.bit_long(static_cast<std::int32_t>(link->custom_data.size()));
            for (const auto &item : link->custom_data)
                href(4, item.target);
            if (has_map) {
                main.bit_long(static_cast<std::int32_t>(link->data_map.size()));
                for (const auto &item : link->data_map) {
                    str(item.name);
                    write_cad_value(item.value);
                }
            }
            href(3, link->hard_owner);
            return;
        }
        auto manager = std::get<SectionManager>(data);
        require(manager.sections.size() <= 32767, "Invalid section manager count");
        bit(manager.is_live);
        main.bit_short(static_cast<std::int16_t>(manager.sections.size()));
        for (auto section : manager.sections)
            href(5, section);
    }
};
std::string encode_class_object(unsigned v, const char *page, int type, const ClassObject &rec) {
    ClassObjectWriter w;
    w.v = v;
    w.page = page;
    auto relative = std::holds_alternative<ObjectDataDescriptor>(rec.data) ||
                    std::holds_alternative<ObjectDataRecord>(rec.data) ||
                    std::holds_alternative<ViewRep>(rec.data);
    auto split = object_prefix(w.main, w.refs, v, type, rec.handle, rec.owner,
                               rec.xdictionary_handle.value_or(0), rec.reactors, relative);
    w.write(rec.data);
    return finish_object(w.main, w.strings, w.refs, v, split);
}
std::uint64_t named_object_dictionary(const DwgR2000Objects &o) {
    for (const auto &[handle, record] : o.metadata_records)
        for (const auto &entry : record.dictionary_entries)
            if (entry.first == "ACAD_GROUP" || entry.first == "ACAD_LAYOUT")
                return handle;
    return 0xC;
}
} // namespace
std::string detail::edit_dwg_class_objects(std::string_view file, const DwgR2000Objects &o,
                                           const std::vector<ClassObject> &records) {
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    LifecycleEdits edits;
    for (const auto &rec : records) {
        require(o.class_objects.count(rec.handle), "Missing class object");
        require(o.records.count(rec.handle), "Missing class object record");
        if (rec.dxf_name != o.class_objects.at(rec.handle).dxf_name)
            throw std::invalid_argument("Class object name mismatch");
        edits[rec.handle] =
            encode_class_object(v, drawing_code_page(file, v), o.records.at(rec.handle).type, rec);
    }
    return lifecycle_rewrite(file, o, edits, {}, {});
}
std::string detail::add_dwg_class_object(std::string_view file, const DwgR2000Objects &o, ClassObject rec,
                                         std::uint64_t &id) {
    rec.dxf_name = rec.dxf_name.empty() ? class_object_dxf_name(rec.data) : rec.dxf_name;
    rec.cpp_class_name =
        rec.cpp_class_name.empty() ? class_object_cpp_name(rec.dxf_name) : rec.cpp_class_name;
    require(is_class_object_name(rec.dxf_name) && !rec.cpp_class_name.empty(), "Unknown class object");
    auto registered = register_dwg_classes(file, o, {{rec.dxf_name, rec.cpp_class_name, 499}});
    if (registered != file)
        return add_dwg_class_object(registered, read_dwg_objects(registered), rec, id);
    LifecycleStorage s(file);
    require(s.version >= 1018, "Class object allocation requires R2004+");
    auto header = allocate_symbol_handle(lifecycle_header(file, s.version), s.version,
                                         o.records.rbegin()->first, id);
    rec.handle = id;
    if (!rec.owner)
        rec.owner = named_object_dictionary(o);
    int type = -1;
    for (const auto &c : o.classes)
        if (c.dxf_name == rec.dxf_name) {
            type = c.number;
            break;
        }
    require(type >= 500, "Missing class object class");
    LifecycleEdits added{{id, encode_class_object(s.version, s.page, type, rec)}}, edits;
    return lifecycle_rewrite(file, o, edits, {}, added, header, id + 1);
}
