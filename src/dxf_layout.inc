// SPDX-License-Identifier: MPL-2.0
// Port of acadrust Layout/PlotSettings and Viewport DXF fields.
std::string DxfLayout::name() const { return text_field("AcDbLayout", 1); }
std::string DxfLayout::block_record_handle() const { return field("AcDbLayout", 330); }
std::string DxfLayout::viewport_handle() const { return field("AcDbLayout", 331); }
LayoutDefinition DxfLayout::definition() const {
    LayoutDefinition d;
    d.flags = static_cast<std::int16_t>(short_field("AcDbLayout", 70, d.flags));
    d.tab_order = static_cast<std::int16_t>(short_field("AcDbLayout", 71, d.tab_order));
    d.ucs_ortho_type = static_cast<std::int16_t>(short_field("AcDbLayout", 76, d.ucs_ortho_type));
    d.min_limits = {number("AcDbLayout", 10, d.min_limits.x), number("AcDbLayout", 20, d.min_limits.y)};
    d.max_limits = {number("AcDbLayout", 11, d.max_limits.x), number("AcDbLayout", 21, d.max_limits.y)};
    d.insertion_base = {number("AcDbLayout", 12, d.insertion_base.x),
                        number("AcDbLayout", 22, d.insertion_base.y),
                        number("AcDbLayout", 32, d.insertion_base.z)};
    d.min_extents = {number("AcDbLayout", 14, d.min_extents.x), number("AcDbLayout", 24, d.min_extents.y),
                     number("AcDbLayout", 34, d.min_extents.z)};
    d.max_extents = {number("AcDbLayout", 15, d.max_extents.x), number("AcDbLayout", 25, d.max_extents.y),
                     number("AcDbLayout", 35, d.max_extents.z)};
    d.ucs_origin = {number("AcDbLayout", 13, d.ucs_origin.x), number("AcDbLayout", 23, d.ucs_origin.y),
                    number("AcDbLayout", 33, d.ucs_origin.z)};
    d.ucs_x_axis = {number("AcDbLayout", 16, d.ucs_x_axis.x), number("AcDbLayout", 26, d.ucs_x_axis.y),
                    number("AcDbLayout", 36, d.ucs_x_axis.z)};
    d.ucs_y_axis = {number("AcDbLayout", 17, d.ucs_y_axis.x), number("AcDbLayout", 27, d.ucs_y_axis.y),
                    number("AcDbLayout", 37, d.ucs_y_axis.z)};
    d.elevation = number("AcDbLayout", 146, 0);
    d.named_ucs = handle_value(field("AcDbLayout", 345));
    d.base_ucs = handle_value(field("AcDbLayout", 346));
    return d;
}
void DxfLayout::set_definition(const LayoutDefinition &d) {
    DxfLayout staged = *this;
    staged.set_short_field("AcDbLayout", 70, d.flags);
    staged.set_short_field("AcDbLayout", 71, d.tab_order);
    staged.set_short_field("AcDbLayout", 76, d.ucs_ortho_type);
    staged.set_number("AcDbLayout", 10, d.min_limits.x);
    staged.set_number("AcDbLayout", 20, d.min_limits.y);
    staged.set_number("AcDbLayout", 11, d.max_limits.x);
    staged.set_number("AcDbLayout", 21, d.max_limits.y);
    staged.set_point("AcDbLayout", 12, d.insertion_base);
    staged.set_point("AcDbLayout", 14, d.min_extents);
    staged.set_point("AcDbLayout", 15, d.max_extents);
    staged.set_point("AcDbLayout", 13, d.ucs_origin);
    staged.set_point("AcDbLayout", 16, d.ucs_x_axis);
    staged.set_point("AcDbLayout", 17, d.ucs_y_axis);
    staged.set_number("AcDbLayout", 146, d.elevation);
    staged.set_field("AcDbLayout", 345, hex_handle(d.named_ucs));
    staged.set_field("AcDbLayout", 346, hex_handle(d.base_ucs));
    *this = std::move(staged);
}
PlotSettingsDefinition DxfLayout::plot_settings() const {
    PlotSettingsDefinition d;
    d.page_name = text_field("AcDbPlotSettings", 1);
    d.printer_name = text_field("AcDbPlotSettings", 2);
    d.paper_size = text_field("AcDbPlotSettings", 4);
    d.view_name = text_field("AcDbPlotSettings", 6);
    d.style_sheet = text_field("AcDbPlotSettings", 7);
    d.margin_left = number("AcDbPlotSettings", 40, d.margin_left);
    d.margin_bottom = number("AcDbPlotSettings", 41, d.margin_bottom);
    d.margin_right = number("AcDbPlotSettings", 42, d.margin_right);
    d.margin_top = number("AcDbPlotSettings", 43, d.margin_top);
    d.paper_width = number("AcDbPlotSettings", 44, d.paper_width);
    d.paper_height = number("AcDbPlotSettings", 45, d.paper_height);
    d.origin_x = number("AcDbPlotSettings", 46, d.origin_x);
    d.origin_y = number("AcDbPlotSettings", 47, d.origin_y);
    d.window_min_x = number("AcDbPlotSettings", 48, d.window_min_x);
    d.window_min_y = number("AcDbPlotSettings", 49, d.window_min_y);
    d.window_max_x = number("AcDbPlotSettings", 140, d.window_max_x);
    d.window_max_y = number("AcDbPlotSettings", 141, d.window_max_y);
    d.scale_numerator = number("AcDbPlotSettings", 142, d.scale_numerator);
    d.scale_denominator = number("AcDbPlotSettings", 143, d.scale_denominator);
    d.scale_factor = number("AcDbPlotSettings", 147, d.scale_factor);
    d.image_origin_x = number("AcDbPlotSettings", 148, d.image_origin_x);
    d.image_origin_y = number("AcDbPlotSettings", 149, d.image_origin_y);
    d.flags = static_cast<std::int16_t>(short_field("AcDbPlotSettings", 70, d.flags));
    d.paper_units = static_cast<std::int16_t>(short_field("AcDbPlotSettings", 72, d.paper_units));
    d.rotation = static_cast<std::int16_t>(short_field("AcDbPlotSettings", 73, d.rotation));
    d.plot_type = static_cast<std::int16_t>(short_field("AcDbPlotSettings", 74, d.plot_type));
    d.scale_type = static_cast<std::int16_t>(short_field("AcDbPlotSettings", 75, d.scale_type));
    d.shade_mode = static_cast<std::int16_t>(short_field("AcDbPlotSettings", 76, d.shade_mode));
    d.shade_resolution = static_cast<std::int16_t>(short_field("AcDbPlotSettings", 77, d.shade_resolution));
    d.shade_dpi = static_cast<std::int16_t>(short_field("AcDbPlotSettings", 78, d.shade_dpi));
    d.visual_style = handle_value(field("AcDbPlotSettings", 333));
    return d;
}
void DxfLayout::set_plot_settings(const PlotSettingsDefinition &d) {
    DxfLayout staged = *this;
    staged.set_text_field("AcDbPlotSettings", 1, d.page_name);
    staged.set_text_field("AcDbPlotSettings", 2, d.printer_name);
    staged.set_text_field("AcDbPlotSettings", 4, d.paper_size);
    staged.set_text_field("AcDbPlotSettings", 6, d.view_name);
    staged.set_text_field("AcDbPlotSettings", 7, d.style_sheet);
    staged.set_number("AcDbPlotSettings", 40, d.margin_left);
    staged.set_number("AcDbPlotSettings", 41, d.margin_bottom);
    staged.set_number("AcDbPlotSettings", 42, d.margin_right);
    staged.set_number("AcDbPlotSettings", 43, d.margin_top);
    staged.set_number("AcDbPlotSettings", 44, d.paper_width);
    staged.set_number("AcDbPlotSettings", 45, d.paper_height);
    staged.set_number("AcDbPlotSettings", 46, d.origin_x);
    staged.set_number("AcDbPlotSettings", 47, d.origin_y);
    staged.set_number("AcDbPlotSettings", 48, d.window_min_x);
    staged.set_number("AcDbPlotSettings", 49, d.window_min_y);
    staged.set_number("AcDbPlotSettings", 140, d.window_max_x);
    staged.set_number("AcDbPlotSettings", 141, d.window_max_y);
    staged.set_number("AcDbPlotSettings", 142, d.scale_numerator);
    staged.set_number("AcDbPlotSettings", 143, d.scale_denominator);
    staged.set_number("AcDbPlotSettings", 147, d.scale_factor);
    staged.set_number("AcDbPlotSettings", 148, d.image_origin_x);
    staged.set_number("AcDbPlotSettings", 149, d.image_origin_y);
    staged.set_short_field("AcDbPlotSettings", 70, d.flags | (name() == "Model" ? 1024 : 0));
    staged.set_short_field("AcDbPlotSettings", 72, d.paper_units);
    staged.set_short_field("AcDbPlotSettings", 73, d.rotation);
    staged.set_short_field("AcDbPlotSettings", 74, d.plot_type);
    staged.set_short_field("AcDbPlotSettings", 75, d.scale_type);
    staged.set_short_field("AcDbPlotSettings", 76, d.shade_mode);
    staged.set_short_field("AcDbPlotSettings", 77, d.shade_resolution);
    staged.set_short_field("AcDbPlotSettings", 78, d.shade_dpi);
    staged.set_field("AcDbPlotSettings", 333, hex_handle(d.visual_style));
    *this = std::move(staged);
}
ViewportDefinition DxfViewport::definition() const {
    ViewportDefinition d;
    d.center = point_or("AcDbViewport", 10, d.center);
    d.view_direction = point_or("AcDbViewport", 16, d.view_direction);
    d.view_target = point_or("AcDbViewport", 17, d.view_target);
    d.ucs_origin = point_or("AcDbViewport", 110, d.ucs_origin);
    d.ucs_x_axis = point_or("AcDbViewport", 111, d.ucs_x_axis);
    d.ucs_y_axis = point_or("AcDbViewport", 112, d.ucs_y_axis);
    d.view_center = point_or("AcDbViewport", 12, {});
    d.view_center.z = 0;
    d.snap_base = {number("AcDbViewport", 13, d.snap_base.x), number("AcDbViewport", 23, d.snap_base.y), 0};
    d.snap_spacing = {number("AcDbViewport", 14, d.snap_spacing.x),
                      number("AcDbViewport", 24, d.snap_spacing.y), 0};
    d.grid_spacing = {number("AcDbViewport", 15, d.grid_spacing.x),
                      number("AcDbViewport", 25, d.grid_spacing.y), 0};
    d.width = number("AcDbViewport", 40, d.width);
    d.height = number("AcDbViewport", 41, d.height);
    d.lens_length = number("AcDbViewport", 42, d.lens_length);
    d.front_clip_z = number("AcDbViewport", 43, d.front_clip_z);
    d.back_clip_z = number("AcDbViewport", 44, d.back_clip_z);
    d.view_height = number("AcDbViewport", 45, d.view_height);
    d.elevation = number("AcDbViewport", 146, d.elevation);
    d.brightness = number("AcDbViewport", 141, d.brightness);
    d.contrast = number("AcDbViewport", 142, d.contrast);
    d.id = static_cast<std::int16_t>(short_field("AcDbViewport", 69, d.id));
    d.circle_sides = static_cast<std::int16_t>(short_field("AcDbViewport", 72, d.circle_sides));
    d.render_mode = static_cast<std::int16_t>(short_field("AcDbViewport", 281, d.render_mode));
    d.ucs_ortho_type = static_cast<std::int16_t>(short_field("AcDbViewport", 79, d.ucs_ortho_type));
    d.shade_plot_mode = static_cast<std::int16_t>(short_field("AcDbViewport", 170, d.shade_plot_mode));
    d.grid_major = static_cast<std::int16_t>(short_field("AcDbViewport", 61, d.grid_major));
    d.default_lighting_type =
        static_cast<std::int16_t>(short_field("AcDbViewport", 282, d.default_lighting_type));
    d.clip_boundary = handle_value(field("AcDbViewport", 340));
    d.ucs = handle_value(field("AcDbViewport", 345));
    d.base_ucs = handle_value(field("AcDbViewport", 346));
    d.background = handle_value(field("AcDbViewport", 332));
    d.shade_plot = handle_value(field("AcDbViewport", 333));
    d.visual_style = handle_value(field("AcDbViewport", 348));
    d.sun = handle_value(field("AcDbViewport", 361));
    d.snap_angle = number("AcDbViewport", 50, 0) * pi / 180.;
    d.twist_angle = number("AcDbViewport", 51, 0) * pi / 180.;
    d.style_sheet = text_field("AcDbViewport", 1);
    d.ucs_at_origin = short_field("AcDbViewport", 74, 0) != 0;
    d.ucs_per_viewport = short_field("AcDbViewport", 71, 0) != 0;
    if (d.render_mode < 0 || d.render_mode > 6)
        d.render_mode = 0;
    auto int_at = [&](std::size_t i) {
        return integer(binary_ ? detail::text_value_from_binary(pairs_[i].code, pairs_[i].value)
                               : pairs_[i].value);
    };
    locate_if("AcDbViewport", [&](std::size_t i, int code) {
        if (code == 90)
            d.status_flags = int_at(i);
        if (code == 68) {
            auto status = static_cast<std::int16_t>(int_at(i));
            d.off_screen = status == -1;
            if (status != d.id)
                d.dxf_status = status;
        }
        if (code == 292)
            d.default_lighting = int_at(i) != 0;
        if (code == 331) {
            if (auto h = parse_handle(pairs_[i].value))
                d.frozen_layers.push_back(*h);
        }
        if (code == 63 && d.ambient_color.kind() != Color::Kind::Rgb)
            d.ambient_color = Color::from_index(static_cast<std::int16_t>(int_at(i)));
        if (code == 421)
            d.ambient_color = Color::from_true_color_value(static_cast<std::uint32_t>(int_at(i)));
        return false;
    });
    if (!(d.status_flags & 0x20000) && locate("AcDbViewport", 68).found) {
        auto status = short_field("AcDbViewport", 68, 0);
        d.status_flags = (d.status_flags & ~0x8000) | (status ? 0x8000 : 0);
    }
    return d;
}
void DxfViewport::set_definition(const ViewportDefinition &d) {
    if (d.render_mode < 0 || d.render_mode > 6)
        throw std::invalid_argument("Invalid viewport render mode");
    DxfViewport staged = *this;
    staged.set_point("AcDbViewport", 10, d.center);
    staged.set_point("AcDbViewport", 16, d.view_direction);
    staged.set_point("AcDbViewport", 17, d.view_target);
    staged.set_point("AcDbViewport", 110, d.ucs_origin);
    staged.set_point("AcDbViewport", 111, d.ucs_x_axis);
    staged.set_point("AcDbViewport", 112, d.ucs_y_axis);
    staged.set_number("AcDbViewport", 12, d.view_center.x);
    staged.set_number("AcDbViewport", 22, d.view_center.y);
    staged.set_number("AcDbViewport", 13, d.snap_base.x);
    staged.set_number("AcDbViewport", 23, d.snap_base.y);
    staged.set_number("AcDbViewport", 14, d.snap_spacing.x);
    staged.set_number("AcDbViewport", 24, d.snap_spacing.y);
    staged.set_number("AcDbViewport", 15, d.grid_spacing.x);
    staged.set_number("AcDbViewport", 25, d.grid_spacing.y);
    staged.set_number("AcDbViewport", 40, d.width);
    staged.set_number("AcDbViewport", 41, d.height);
    staged.set_number("AcDbViewport", 42, d.lens_length);
    staged.set_number("AcDbViewport", 43, d.front_clip_z);
    staged.set_number("AcDbViewport", 44, d.back_clip_z);
    staged.set_number("AcDbViewport", 45, d.view_height);
    staged.set_number("AcDbViewport", 146, d.elevation);
    staged.set_number("AcDbViewport", 141, d.brightness);
    staged.set_number("AcDbViewport", 142, d.contrast);
    staged.set_short_field("AcDbViewport", 69, d.id);
    staged.set_short_field("AcDbViewport", 72, d.circle_sides);
    staged.set_short_field("AcDbViewport", 281, d.render_mode);
    staged.set_short_field("AcDbViewport", 79, d.ucs_ortho_type);
    staged.set_short_field("AcDbViewport", 170, d.shade_plot_mode);
    staged.set_short_field("AcDbViewport", 61, d.grid_major);
    staged.set_short_field("AcDbViewport", 282, d.default_lighting_type);
    staged.set_field("AcDbViewport", 340, hex_handle(d.clip_boundary));
    staged.set_field("AcDbViewport", 345, hex_handle(d.ucs));
    staged.set_field("AcDbViewport", 346, hex_handle(d.base_ucs));
    staged.set_field("AcDbViewport", 332, hex_handle(d.background));
    staged.set_field("AcDbViewport", 333, hex_handle(d.shade_plot));
    staged.set_field("AcDbViewport", 348, hex_handle(d.visual_style));
    staged.set_field("AcDbViewport", 361, hex_handle(d.sun));
    staged.set_number("AcDbViewport", 50, d.snap_angle * 180. / pi);
    staged.set_number("AcDbViewport", 51, d.twist_angle * 180. / pi);
    staged.set_text_field("AcDbViewport", 1, d.style_sheet);
    staged.set_short_field("AcDbViewport", 74, d.ucs_at_origin);
    staged.set_short_field("AcDbViewport", 71, d.ucs_per_viewport);
    staged.set_short_field("AcDbViewport", 68,
                           d.dxf_status.value_or(!d.is_on()     ? 0
                                                 : d.off_screen ? -1
                                                                : d.id));
    auto integer = [&](int code, std::int32_t value) {
        auto text = std::to_string(value);
        staged.set_field("AcDbViewport", code, binary_ ? detail::binary_value_from_text(code, text) : text);
    };
    integer(90, d.status_flags);
    integer(292, d.default_lighting);
    std::vector<std::size_t> remove;
    staged.locate_if("AcDbViewport", [&](std::size_t i, int code) {
        if (code == 331 || code == 63 || code == 421)
            remove.push_back(i);
        return false;
    });
    for (auto it = remove.rbegin(); it != remove.rend(); ++it)
        staged.pairs_.erase(staged.pairs_.begin() + *it);
    auto end = staged.locate_if("AcDbViewport", [](std::size_t, int) { return false; }).index;
    std::vector<Pair> layers;
    for (auto h : d.frozen_layers)
        if (h)
            layers.push_back({331, hex_handle(h)});
    staged.pairs_.insert(staged.pairs_.begin() + end, layers.begin(), layers.end());
    if (d.ambient_color != Color::by_block()) {
        integer(63, d.ambient_color.index().value_or(d.ambient_color.approximate_index()));
        if (auto rgb = d.ambient_color.to_true_color_value())
            integer(421, static_cast<std::int32_t>(*rgb));
    }
    *this = std::move(staged);
}
const DxfLayout *DxfDocument::get_layout(std::string_view name) const {
    const auto key = detail::symbol_name_key(name);
    const DxfLayout *result = nullptr;
    for_each<DxfLayout>([&](const auto &layout) {
        if (detail::symbol_name_key(layout.name()) == key) {
            if (result)
                throw std::invalid_argument("Ambiguous layout name");
            result = &layout;
        }
    });
    return result;
}
std::vector<std::string> DxfDocument::entity_handles_in_layout(std::string_view name) const {
    const auto *layout = get_layout(name);
    if (!layout)
        return {};
    const auto *block = block_record(layout->block_record_handle());
    return block ? entity_handles_in_block(block->name()) : std::vector<std::string>{};
}
std::uint64_t DxfDocument::add_viewport(const ViewportDefinition &definition, const EntityOptions &options) {
    if (version_ < DxfVersion::AC1015)
        throw std::invalid_argument("Viewport creation currently requires R2000 or later");
    DxfViewport entity;
    static_cast<DxfEntity &>(entity) = make_entity("VIEWPORT", "AcDbViewport", options);
    auto d = definition;
    // Assign the next visible viewport ID within the target layout, as the Rust writer does.
    std::string target = options.block_name.empty()
                             ? (options.space == EntitySpace::Paper ? "*Paper_Space" : "*Model_Space")
                             : options.block_name;
    if (!options.layout_name.empty()) {
        const auto *layout = get_layout(options.layout_name);
        const auto *block = layout ? block_record(layout->block_record_handle()) : nullptr;
        if (!block)
            throw std::invalid_argument("Unknown viewport layout");
        target = block->name();
    }
    const auto block = get_symbol(SymbolTable::BlockRecord, target);
    if (!block)
        throw std::invalid_argument("Unknown viewport block");
    const auto *owner = block_record(block->handle);
    if (d.id == 0 && owner && owner->is_paper_space()) {
        int highest = 1;
        for_each<DxfViewport>([&](const auto &viewport) {
            if (handle_value(viewport.owner_handle()) == handle_value(block->handle))
                highest = std::max(highest, static_cast<int>(viewport.definition().id));
        });
        if (highest == INT16_MAX)
            throw std::overflow_error("Viewport ID space exhausted");
        d.id = static_cast<std::int16_t>(highest + 1);
    }
    for (auto h : d.frozen_layers) {
        const auto *layer = get_record(hex_handle(h));
        if (h && (!layer || layer->type() != "LAYER"))
            throw std::invalid_argument("Viewport frozen layer must exist");
    }
    entity.set_definition(d);
    return insert_entity(std::move(entity), options);
}
std::uint64_t DxfDocument::add_layout(std::string name) {
    if (version_ < DxfVersion::AC1015)
        throw std::invalid_argument("Named layouts require R2000 or later");
    valid_symbol_name(name);
    if (get_layout(name))
        throw std::invalid_argument("Layout already exists");
    std::string dictionary;
    for (const auto &entry : records_) {
        const auto &r = std::visit([](const DxfRecord &r) -> const DxfRecord & { return r; }, entry);
        if (r.type() != "DICTIONARY" || handle_value(r.owner_handle()))
            continue;
        bool match = false;
        r.locate_if("AcDbDictionary", [&](std::size_t i, int code) {
            if (code == 3)
                match = detail::symbol_name_key(r.decode_string(r.pairs_[i].value)) ==
                        detail::symbol_name_key("ACAD_LAYOUT");
            else if (match && (code == 350 || code == 360)) {
                if (!dictionary.empty())
                    throw std::invalid_argument("Ambiguous ACAD_LAYOUT dictionary");
                dictionary = r.pairs_[i].value;
                match = false;
            }
            return false;
        });
    }
    if (dictionary.empty())
        throw std::invalid_argument("Missing ACAD_LAYOUT dictionary");
    std::optional<int> last_tab;
    for_each<DxfLayout>([&](const auto &layout) {
        last_tab = std::max(last_tab.value_or(INT16_MIN), int(layout.short_field("AcDbLayout", 71, 0)));
    });
    std::string block_name = "*Paper_Space";
    if (get_symbol(SymbolTable::BlockRecord, block_name)) {
        for (std::size_t i = 0;; ++i) {
            block_name = "*Paper_Space" + std::to_string(i);
            if (!get_symbol(SymbolTable::BlockRecord, block_name))
                break;
        }
    }
    DxfDocument staged = *this;
    DxfBlockRecord block;
    static_cast<DxfRecord &>(block) = staged.make_record("BLOCK_RECORD", "AcDbBlockTableRecord");
    block.set_text_field("AcDbBlockTableRecord", 2, block_name);
    block.set_short_field("AcDbBlockTableRecord", 70, 0);
    block.set_short_field("AcDbBlockTableRecord", 280, 1);
    block.set_short_field("AcDbBlockTableRecord", 281, 0);
    const auto block_id = staged.insert_table_entry("BLOCK_RECORD", block);
    DxfBlock begin;
    static_cast<DxfEntity &>(begin) = staged.make_entity("BLOCK", "AcDbBlockBegin", {});
    auto end = staged.make_entity("ENDBLK", "AcDbBlockEnd", {});
    begin.set_field("", 5, hex_handle(staged.allocate_handle()));
    end.set_field("", 5, hex_handle(staged.allocate_handle()));
    begin.set_field("", 330, hex_handle(block_id));
    end.set_field("", 330, hex_handle(block_id));
    begin.set_text_field("AcDbBlockBegin", 2, block_name);
    begin.set_text_field("AcDbBlockBegin", 3, block_name);
    begin.set_flags(0);
    begin.set_base_point({});
    std::optional<std::size_t> position;
    std::string section;
    for (std::size_t i = 0; i < staged.records_.size(); ++i) {
        const auto &r = staged.record_at({i, std::nullopt, false});
        if (r.type() == "SECTION")
            section = r.field("", 2);
        if (r.type() == "ENDSEC") {
            if (section == "BLOCKS") {
                if (position)
                    throw std::invalid_argument("Multiple BLOCKS sections");
                position = i;
            }
            section.clear();
        }
    }
    if (!position)
        throw std::invalid_argument("Missing BLOCKS section");
    std::vector<Record> boundaries{begin, static_cast<DxfRecord>(end)};
    staged.records_.insert(staged.records_.begin() + *position, boundaries.begin(), boundaries.end());
    staged.rebuild_indexes();
    DxfLayout layout;
    static_cast<DxfRecord &>(layout) = staged.make_record("LAYOUT", "AcDbPlotSettings");
    layout.pairs_.push_back({100, "AcDbLayout"});
    layout.set_text_field("AcDbLayout", 1, name);
    LayoutDefinition definition;
    definition.tab_order = static_cast<std::int16_t>(std::min<int>(INT16_MAX, last_tab.value_or(0) + 1));
    layout.set_definition(definition);
    layout.set_plot_settings({});
    layout.set_field("AcDbLayout", 330, hex_handle(block_id));
    const auto layout_id = staged.add_dictionary_object(dictionary, name, std::move(layout));
    auto &block_record = const_cast<DxfRecord &>(staged.record_at(staged.handle_index_.at(block_id).front()));
    block_record.set_field("AcDbBlockTableRecord", 340, hex_handle(layout_id));
    EntityOptions options;
    options.layout_name = name;
    ViewportDefinition overall;
    overall.id = 1;
    const auto viewport_id = staged.add_viewport(overall, options);
    auto &saved = const_cast<DxfRecord &>(staged.record_at(staged.handle_index_.at(layout_id).front()));
    saved.set_field("AcDbLayout", 331, hex_handle(viewport_id));
    staged.rebuild_indexes();
    *this = std::move(staged);
    return layout_id;
}
