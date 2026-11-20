// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/layer_state.rs CadDocument layer-state methods.
namespace {
constexpr const char *kLayerSubclass = "AcDbLayerTableRecord";
constexpr const char *kStatesDictionary = "ACAD_LAYERSTATES";
constexpr const char *kStatesDictionaryLegacy = "ACAD_LAYERSTATE";

std::optional<std::string> optional_text(std::string_view value) {
    return value.empty() ? std::nullopt : std::optional<std::string>{std::string(value)};
}

std::pair<std::optional<std::string>, std::optional<std::string>>
split_color_book_name(std::string_view value) {
    const auto at = value.find('$');
    if (at == std::string_view::npos)
        return {std::nullopt, optional_text(value)};
    return {optional_text(value.substr(0, at)), optional_text(value.substr(at + 1))};
}

std::optional<std::string> join_color_book_name(const std::optional<std::string> &book_name,
                                                const std::optional<std::string> &color_name) {
    const auto book = book_name && !book_name->empty() ? book_name : std::nullopt;
    const auto color = color_name && !color_name->empty() ? color_name : std::nullopt;
    if (book && color)
        return *book + "$" + *color;
    if (book)
        return *book + "$";
    if (color)
        return *color;
    return std::nullopt;
}

int layer_aci(const Color &color) {
    const auto index = color.index().value_or(color.approximate_index());
    if (index <= 0 || index > 255)
        return 7;
    return index;
}
} // namespace

DxfRecord &DxfDocument::mutable_object(std::uint64_t handle) {
    const auto found = handle_index_.find(handle);
    if (found == handle_index_.end() || found->second.size() != 1)
        throw std::invalid_argument("Layer-state object must resolve uniquely");
    return const_cast<DxfRecord &>(record_at(found->second.front()));
}

const DxfRecord *DxfDocument::object_record(std::uint64_t handle) const {
    const auto found = handle_index_.find(handle);
    if (found == handle_index_.end() || found->second.size() != 1)
        return nullptr;
    return &record_at(found->second.front());
}

std::string DxfDocument::layer_table_handle() const {
    const auto zero = get_symbol(SymbolTable::Layer, "0");
    if (!zero || zero->owner_handle.empty() || !handle_value(zero->owner_handle))
        throw std::invalid_argument("Missing LAYER table");
    return zero->owner_handle;
}

std::string DxfDocument::layer_states_dictionary_handle() const {
    const auto table = layer_table_handle();
    auto extension = extension_dictionary_handle(table);
    if (extension.empty()) {
        for (std::size_t i = 0; i < records_.size(); ++i) {
            const auto &record = record_at({i, std::nullopt, false});
            if (record.type() == "DICTIONARY" &&
                handle_value(record.owner_handle()) == handle_value(table)) {
                extension = record.handle();
                break;
            }
        }
    }
    if (extension.empty())
        return {};
    const auto *dict = get_record(extension);
    if (!dict || dict->type() != "DICTIONARY")
        return {};
    for (const auto &entry : named_dictionary_entries(extension)) {
        if (ascii_equal(entry.first, kStatesDictionary) || ascii_equal(entry.first, kStatesDictionaryLegacy))
            return entry.second ? hex_handle(entry.second) : std::string{};
    }
    return {};
}

std::uint64_t DxfDocument::ensure_layer_states_dictionary() {
    const auto table = layer_table_handle();
    auto extension = extension_dictionary_handle(table);
    if (extension.empty()) {
        for (std::size_t i = 0; i < records_.size(); ++i) {
            const auto &record = record_at({i, std::nullopt, false});
            if (record.type() == "DICTIONARY" &&
                handle_value(record.owner_handle()) == handle_value(table)) {
                extension = record.handle();
                break;
            }
        }
    }
    if (extension.empty())
        extension = hex_handle(add_extension_dictionary(table));

    if (auto existing = layer_states_dictionary_handle(); !existing.empty()) {
        const auto *record = get_record(existing);
        if (record && record->type() == "DICTIONARY")
            return handle_value(existing);
        auto replacement = make_record("DICTIONARY");
        replacement.pairs_.push_back({100, "AcDbDictionary"});
        replacement.set_short_field("AcDbDictionary", 280, 1);
        replacement.set_short_field("AcDbDictionary", 281, 1);
        return add_dictionary_object(extension, kStatesDictionary, std::move(replacement));
    }
    return add_dictionary(extension, kStatesDictionary);
}

LayerState DxfDocument::decode_layer_state(std::string_view name, const DxfRecord &xrecord) const {
    std::vector<std::size_t> payload;
    bool in_record = false;
    bool cloning = false;
    for (std::size_t i = 1; i < xrecord.pairs_.size(); ++i) {
        const auto &p = xrecord.pairs_[i];
        if (!in_record) {
            if (p.code == 100 && p.value == "AcDbXrecord")
                in_record = true;
            continue;
        }
        if (!cloning) {
            if (p.code == 280)
                cloning = true;
            continue;
        }
        payload.push_back(i);
    }

    auto payload_i32 = [&](std::size_t begin, std::size_t end, int code) -> std::optional<std::int32_t> {
        for (auto n = begin; n < end; ++n) {
            const auto &p = xrecord.pairs_[payload[n]];
            if (p.code != code)
                continue;
            if (auto bits = detail::as_i32_bits(p.value, xrecord.binary_))
                return static_cast<std::int32_t>(*bits);
        }
        return std::nullopt;
    };
    auto payload_i16 = [&](std::size_t begin, std::size_t end, int code) -> std::optional<std::int16_t> {
        for (auto n = begin; n < end; ++n) {
            const auto &p = xrecord.pairs_[payload[n]];
            if (p.code != code)
                continue;
            return detail::as_i16(code, p.value, xrecord.binary_);
        }
        return std::nullopt;
    };
    auto payload_string = [&](std::size_t begin, std::size_t end, int code) -> std::optional<std::string> {
        for (auto n = begin; n < end; ++n) {
            const auto &p = xrecord.pairs_[payload[n]];
            if (p.code == code)
                return xrecord.decode_string(p.value);
        }
        return std::nullopt;
    };

    std::size_t first_layer = payload.size();
    for (std::size_t n = 0; n < payload.size(); ++n) {
        if (xrecord.pairs_[payload[n]].code == 330) {
            first_layer = n;
            break;
        }
    }

    LayerState state;
    state.name = std::string(name);
    if (auto mask = payload_i32(0, first_layer, 91))
        state.mask = LayerStateMask::from_bits(static_cast<std::uint32_t>(*mask));
    else
        state.mask = LayerStateMask::from_bits(LayerStateMask::ALL);
    state.description = payload_string(0, first_layer, 301).value_or(std::string{});
    if (auto hidden = payload_i16(0, first_layer, 290))
        state.hidden = *hidden != 0;
    state.current_layer = payload_string(0, first_layer, 302).value_or(std::string{});

    std::size_t start = first_layer;
    while (start < payload.size()) {
        std::size_t end = payload.size();
        for (std::size_t n = start + 1; n < payload.size(); ++n) {
            if (xrecord.pairs_[payload[n]].code == 330) {
                end = n;
                break;
            }
        }
        if (auto layer = decode_layer_state_layer(xrecord, payload, start, end))
            state.layers.push_back(std::move(*layer));
        start = end;
    }
    return state;
}

std::optional<LayerStateLayer> DxfDocument::decode_layer_state_layer(
    const DxfRecord &xrecord, const std::vector<std::size_t> &payload, std::size_t begin,
    std::size_t end) const {
    if (begin >= end)
        return std::nullopt;
    const auto &first = xrecord.pairs_[payload[begin]];
    if (first.code != 330)
        return std::nullopt;
    const auto handle = parse_handle(first.value);
    if (!handle)
        return std::nullopt;

    auto payload_i32 = [&](int code) -> std::optional<std::int32_t> {
        for (auto n = begin; n < end; ++n) {
            const auto &p = xrecord.pairs_[payload[n]];
            if (p.code != code)
                continue;
            if (auto bits = detail::as_i32_bits(p.value, xrecord.binary_))
                return static_cast<std::int32_t>(*bits);
        }
        return std::nullopt;
    };
    auto payload_i16 = [&](int code) -> std::optional<std::int16_t> {
        for (auto n = begin; n < end; ++n) {
            const auto &p = xrecord.pairs_[payload[n]];
            if (p.code != code)
                continue;
            return detail::as_i16(code, p.value, xrecord.binary_);
        }
        return std::nullopt;
    };
    auto payload_string = [&](int code) -> std::optional<std::string> {
        for (auto n = begin; n < end; ++n) {
            const auto &p = xrecord.pairs_[payload[n]];
            if (p.code == code)
                return xrecord.decode_string(p.value);
        }
        return std::nullopt;
    };
    auto payload_handle = [&](int code) -> std::optional<std::uint64_t> {
        for (auto n = begin; n < end; ++n) {
            const auto &p = xrecord.pairs_[payload[n]];
            if (p.code == code)
                return parse_handle(p.value);
        }
        return std::nullopt;
    };

    LayerStateLayer layer;
    layer.layer_handle = *handle;
    const auto flags = payload_i32(90).value_or(0);
    layer.off = (flags & 0x01) != 0;
    layer.frozen = (flags & 0x02) != 0;
    layer.locked = (flags & 0x04) != 0;
    layer.plottable = (flags & 0x08) != 0;
    layer.new_viewport_frozen = (flags & 0x10) != 0;
    if (auto true_color = payload_i32(92))
        layer.color = Color::from_true_color_value(static_cast<std::uint32_t>(*true_color));
    else if (auto rgb = payload_i32(420))
        layer.color = Color::from_true_color_value(static_cast<std::uint32_t>(*rgb));
    else
        layer.color = Color::from_index(payload_i16(62).value_or(7));
    if (auto identity = payload_string(300)) {
        auto split = split_color_book_name(*identity);
        layer.book_name = std::move(split.first);
        layer.color_name = std::move(split.second);
    }
    if (const auto *record = object_record(*handle); record && record->type() == "LAYER")
        layer.layer_name = record->text_field(kLayerSubclass, 2);
    if (auto line_type = payload_handle(331)) {
        if (const auto *record = object_record(*line_type); record && record->type() == "LTYPE")
            layer.line_type = record->text_field("AcDbLinetypeTableRecord", 2);
    }
    layer.line_weight = LineWeight::from_value(payload_i16(370).value_or(-3));
    layer.plot_style = payload_string(1).value_or(std::string{});
    if (auto packed = payload_i32(440))
        layer.transparency = Transparency::from_layer_alpha_value(static_cast<std::uint32_t>(*packed));
    return layer;
}

std::vector<SymbolProperty> DxfDocument::encode_layer_state(const LayerState &state) const {
    std::vector<SymbolProperty> entries{
        {91, std::to_string(static_cast<std::int32_t>(state.mask.bits))},
        {301, state.description},
        {290, state.hidden ? "1" : "0"},
        {302, state.current_layer},
    };
    for (const auto &layer : state.layers) {
        auto handle = layer.layer_handle;
        if (!handle) {
            if (const auto *record = get_layer(layer.layer_name))
                handle = handle_value(record->handle());
        }
        if (!handle)
            continue;
        std::int32_t flags = 0;
        if (layer.off)
            flags |= 0x01;
        if (layer.frozen)
            flags |= 0x02;
        if (layer.locked)
            flags |= 0x04;
        if (layer.plottable)
            flags |= 0x08;
        if (layer.new_viewport_frozen)
            flags |= 0x10;
        std::uint64_t line_type = 0;
        if (const auto *named = get_linetype(layer.line_type))
            line_type = handle_value(named->handle());
        else if (const auto *continuous = get_linetype("Continuous"))
            line_type = handle_value(continuous->handle());
        entries.push_back({330, hex_handle(handle)});
        entries.push_back({90, std::to_string(flags)});
        entries.push_back({62, std::to_string(layer.color.approximate_index())});
        if (auto rgb = layer.color.to_true_color_value())
            entries.push_back({92, std::to_string(static_cast<std::int32_t>(0xC2000000u | *rgb))});
        entries.push_back({370, std::to_string(layer.line_weight.value())});
        entries.push_back({331, hex_handle(line_type)});
        entries.push_back({1, layer.plot_style});
        if (layer.transparency)
            entries.push_back(
                {440, std::to_string(static_cast<std::int32_t>(layer.transparency->to_dxf_value()))});
        if (auto identity = join_color_book_name(layer.book_name, layer.color_name))
            entries.push_back({300, *identity});
    }
    return entries;
}

void DxfDocument::set_xrecord_layer_state(DxfRecord &record, const LayerState &state) {
    auto encoded = [&](int code, const std::string &text) {
        if (detail::is_string_value(code))
            return record.encode_string(text);
        return record.binary_ ? detail::binary_value_from_text(code, text) : text;
    };
    bool in_record = false;
    std::optional<std::size_t> cloning;
    for (std::size_t i = 1; i < record.pairs_.size(); ++i) {
        const auto &p = record.pairs_[i];
        if (!in_record) {
            if (p.code == 100 && p.value == "AcDbXrecord")
                in_record = true;
            continue;
        }
        if (p.code == 280) {
            cloning = i;
            break;
        }
    }
    if (!cloning) {
        record.pairs_.push_back({100, "AcDbXrecord"});
        record.set_short_field("AcDbXrecord", 280, 1);
        cloning = record.pairs_.size() - 1;
    } else
        record.set_short_field("AcDbXrecord", 280, 1);
    record.pairs_.erase(record.pairs_.begin() + static_cast<std::ptrdiff_t>(*cloning + 1), record.pairs_.end());
    for (const auto &entry : encode_layer_state(state))
        record.pairs_.push_back({entry.code, encoded(entry.code, entry.value)});
}

std::string DxfDocument::header_current_layer() const {
    if (!header_index_)
        return "0";
    const auto &header = record_at({*header_index_, std::nullopt, false});
    std::string variable;
    for (const auto &p : header.pairs_) {
        if (p.code == 9)
            variable = std::string(trim(p.value));
        else if (p.code == 8 && variable == "$CLAYER") {
            auto name = header.decode_string(p.value);
            return name.empty() ? "0" : name;
        }
    }
    return "0";
}

void DxfDocument::set_header_current_layer(std::string name) {
    if (!header_index_)
        return;
    auto &header = const_cast<DxfRecord &>(record_at({*header_index_, std::nullopt, false}));
    std::string variable;
    for (auto &p : header.pairs_) {
        if (p.code == 9)
            variable = std::string(trim(p.value));
        else if (p.code == 8 && variable == "$CLAYER") {
            p.value = header.encode_string(name);
            return;
        }
    }
}

std::vector<LayerState> DxfDocument::layer_states() const {
    const auto dictionary = layer_states_dictionary_handle();
    if (dictionary.empty())
        return {};
    std::vector<LayerState> states;
    for (const auto &entry : named_dictionary_entries(dictionary)) {
        const auto *record = object_record(entry.second);
        if (!record || record->type() != "XRECORD")
            continue;
        states.push_back(decode_layer_state(entry.first, *record));
    }
    return states;
}

std::optional<LayerState> DxfDocument::layer_state(std::string_view name) const {
    for (auto &state : layer_states()) {
        if (ascii_equal(state.name, name))
            return std::move(state);
    }
    return std::nullopt;
}

std::uint64_t DxfDocument::capture_layer_state(std::string name, std::string description) {
    LayerState state;
    state.name = std::move(name);
    state.description = std::move(description);
    state.mask = LayerStateMask::from_bits(LayerStateMask::SUPPORTED);
    state.current_layer = header_current_layer();
    for (const auto &entry : symbols(SymbolTable::Layer)) {
        const auto *layer = get_layer(entry.name);
        if (!layer)
            continue;
        LayerStateLayer saved;
        saved.layer_handle = handle_value(layer->handle());
        saved.layer_name = layer->name();
        const auto signed_color = layer->signed_color_index();
        saved.off = signed_color < 0;
        const auto flags = layer->short_field(kLayerSubclass, 70, 0);
        saved.frozen = (flags & 1) != 0;
        saved.new_viewport_frozen = (flags & 2) != 0;
        saved.locked = (flags & 4) != 0;
        saved.plottable = layer->short_field(kLayerSubclass, 290, 1) != 0;
        saved.color = Color::from_index(static_cast<std::int16_t>(std::abs(signed_color)));
        const auto rgb = layer->locate(kLayerSubclass, 420);
        if (rgb.found) {
            if (auto bits = detail::as_i32_bits(layer->pairs_[rgb.index].value, layer->binary_))
                saved.color = Color::from_true_color_value(*bits);
        }
        const auto identity = layer->text_field(kLayerSubclass, 430);
        if (!identity.empty()) {
            auto split = split_color_book_name(identity);
            saved.book_name = std::move(split.first);
            saved.color_name = std::move(split.second);
        }
        saved.line_type = layer->text_field(kLayerSubclass, 6, "Continuous");
        saved.line_weight =
            LineWeight::from_value(static_cast<std::int16_t>(layer->short_field(kLayerSubclass, 370, -3)));
        saved.plot_style = "Color_" + std::to_string(saved.color.approximate_index());
        saved.transparency = Transparency::explicit_alpha(0);
        for (const auto &data : layer->extended_data()) {
            if (data.application_name != "AcCmTransparency")
                continue;
            for (const auto &item : data.values) {
                if (item.code != 1071)
                    continue;
                if (auto *value = std::get_if<std::int32_t>(&item.value)) {
                    if (auto parsed = Transparency::from_layer_alpha_value(static_cast<std::uint32_t>(*value)))
                        saved.transparency = *parsed;
                }
            }
        }
        state.layers.push_back(std::move(saved));
    }
    return store_layer_state(std::move(state));
}

std::uint64_t DxfDocument::store_layer_state(LayerState state) {
    DxfDocument staged = *this;
    const auto dictionary = staged.ensure_layer_states_dictionary();
    std::uint64_t existing = 0;
    std::string existing_key;
    for (const auto &entry : staged.named_dictionary_entries(hex_handle(dictionary))) {
        if (ascii_equal(entry.first, state.name)) {
            existing = entry.second;
            existing_key = entry.first;
            break;
        }
    }
    std::uint64_t id = existing;
    if (existing) {
        auto &record = staged.mutable_object(existing);
        if (record.type() != "XRECORD")
            throw std::invalid_argument("Layer state is not an XRECORD");
        staged.set_xrecord_layer_state(record, state);
        if (existing_key != state.name) {
            auto &dict = staged.mutable_object(dictionary);
            std::optional<std::size_t> key;
            dict.locate_if("AcDbDictionary", [&](std::size_t i, int code) {
                if (code == 3 && ascii_equal(dict.decode_string(dict.pairs_[i].value), existing_key)) {
                    key = i;
                    return true;
                }
                return false;
            });
            if (key)
                dict.pairs_[*key].value = dict.encode_string(state.name);
        }
    } else {
        id = staged.add_xrecord(hex_handle(dictionary), state.name, staged.encode_layer_state(state));
        staged.mutable_object(id).set_short_field("AcDbXrecord", 280, 1);
    }
    staged.rebuild_indexes();
    *this = std::move(staged);
    return id;
}

std::optional<std::size_t> DxfDocument::restore_layer_state(std::string_view name) {
    const auto state = layer_state(name);
    if (!state)
        return std::nullopt;
    DxfDocument staged = *this;
    std::size_t restored = 0;
    for (const auto &saved : state->layers) {
        std::string layer_name = saved.layer_name;
        if (saved.layer_handle) {
            if (const auto *record = staged.object_record(saved.layer_handle);
                record && record->type() == "LAYER")
                layer_name = record->text_field(kLayerSubclass, 2);
        }
        auto *layer = staged.get_layer(layer_name);
        if (!layer)
            continue;
        auto &record = staged.mutable_object(handle_value(layer->handle()));
        const auto flags_now = record.short_field(kLayerSubclass, 70, 0);
        const auto signed_now = record.short_field(kLayerSubclass, 62, 7);
        int flags = flags_now;
        if (state->mask.contains(LayerStateMask::FROZEN))
            flags = saved.frozen ? (flags | 1) : (flags & ~1);
        if (state->mask.contains(LayerStateMask::NEW_VIEWPORT))
            flags = saved.new_viewport_frozen ? (flags | 2) : (flags & ~2);
        if (state->mask.contains(LayerStateMask::LOCKED))
            flags = saved.locked ? (flags | 4) : (flags & ~4);
        if (state->mask.contains(LayerStateMask::FROZEN) || state->mask.contains(LayerStateMask::LOCKED) ||
            state->mask.contains(LayerStateMask::NEW_VIEWPORT))
            record.set_short_field(kLayerSubclass, 70, flags);
        if (state->mask.contains(LayerStateMask::ON) || state->mask.contains(LayerStateMask::COLOR)) {
            const bool off = state->mask.contains(LayerStateMask::ON) ? saved.off : signed_now < 0;
            Color color = saved.color;
            if (!state->mask.contains(LayerStateMask::COLOR)) {
                color = Color::from_index(static_cast<std::int16_t>(std::abs(signed_now)));
                const auto rgb = record.locate(kLayerSubclass, 420);
                if (rgb.found) {
                    if (auto bits = detail::as_i32_bits(record.pairs_[rgb.index].value, record.binary_))
                        color = Color::from_true_color_value(*bits);
                }
            }
            record.set_short_field(kLayerSubclass, 62, off ? -layer_aci(color) : layer_aci(color));
            if (state->mask.contains(LayerStateMask::COLOR)) {
                if (auto rgb = color.to_true_color_value()) {
                    const auto text = std::to_string(static_cast<std::int32_t>(*rgb));
                    record.set_field(kLayerSubclass, 420,
                                     record.binary_ ? detail::binary_value_from_text(420, text) : text);
                } else {
                    const auto pos = record.locate(kLayerSubclass, 420);
                    if (pos.found)
                        record.pairs_.erase(record.pairs_.begin() + static_cast<std::ptrdiff_t>(pos.index));
                }
                if (auto identity = join_color_book_name(saved.book_name, saved.color_name))
                    record.set_text_field(kLayerSubclass, 430, *identity);
                else {
                    const auto pos = record.locate(kLayerSubclass, 430);
                    if (pos.found)
                        record.pairs_.erase(record.pairs_.begin() + static_cast<std::ptrdiff_t>(pos.index));
                }
            }
        }
        if (state->mask.contains(LayerStateMask::PLOT)) {
            const auto text = saved.plottable ? std::string{"1"} : std::string{"0"};
            record.set_field(kLayerSubclass, 290,
                             record.binary_ ? detail::binary_value_from_text(290, text) : text);
        }
        if (state->mask.contains(LayerStateMask::LINE_TYPE) && !saved.line_type.empty() &&
            staged.get_linetype(saved.line_type))
            record.set_text_field(kLayerSubclass, 6, saved.line_type);
        if (state->mask.contains(LayerStateMask::LINE_WEIGHT))
            record.set_short_field(kLayerSubclass, 370, saved.line_weight.value());
        if (state->mask.contains(LayerStateMask::TRANSPARENCY) && saved.transparency) {
            staged.set_extended_data(
                record.handle(),
                {"AcCmTransparency",
                 {{1071, static_cast<std::int32_t>(saved.transparency->to_dxf_value())}}});
        }
        ++restored;
    }
    if (const auto *current = staged.get_layer(state->current_layer))
        staged.set_header_current_layer(current->name());
    staged.rebuild_indexes();
    *this = std::move(staged);
    return restored;
}

bool DxfDocument::rename_layer_state(std::string_view old_name, std::string_view new_name) {
    if (new_name.empty())
        return false;
    const auto dictionary = layer_states_dictionary_handle();
    if (dictionary.empty())
        return false;
    if (layer_state(new_name) && !ascii_equal(old_name, new_name))
        return false;
    DxfDocument staged = *this;
    auto &dict = staged.mutable_object(handle_value(dictionary));
    std::optional<std::size_t> key;
    dict.locate_if("AcDbDictionary", [&](std::size_t i, int code) {
        if (code == 3 && ascii_equal(dict.decode_string(dict.pairs_[i].value), old_name)) {
            key = i;
            return true;
        }
        return false;
    });
    if (!key)
        return false;
    dict.pairs_[*key].value = dict.encode_string(std::string(new_name));
    staged.rebuild_indexes();
    *this = std::move(staged);
    return true;
}

bool DxfDocument::delete_layer_state(std::string_view name) {
    const auto dictionary = layer_states_dictionary_handle();
    if (dictionary.empty())
        return false;
    DxfDocument staged = *this;
    auto &dict = staged.mutable_object(handle_value(dictionary));
    std::optional<std::size_t> key;
    dict.locate_if("AcDbDictionary", [&](std::size_t i, int code) {
        if (code == 3 && ascii_equal(dict.decode_string(dict.pairs_[i].value), name)) {
            key = i;
            return true;
        }
        return false;
    });
    if (!key)
        return false;
    const auto handle_at = *key + 1;
    if (handle_at >= dict.pairs_.size() ||
        (dict.pairs_[handle_at].code != 350 && dict.pairs_[handle_at].code != 360))
        return false;
    const auto target = handle_value(dict.pairs_[handle_at].value);
    dict.pairs_.erase(dict.pairs_.begin() + static_cast<std::ptrdiff_t>(handle_at));
    dict.pairs_.erase(dict.pairs_.begin() + static_cast<std::ptrdiff_t>(*key));
    if (target) {
        const auto found = staged.handle_index_.find(target);
        if (found != staged.handle_index_.end() && found->second.size() == 1 &&
            !found->second.front().vertex && !found->second.front().seqend)
            staged.records_.erase(staged.records_.begin() +
                                  static_cast<std::ptrdiff_t>(found->second.front().record));
    }
    staged.rebuild_indexes();
    *this = std::move(staged);
    return true;
}
