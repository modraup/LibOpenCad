// SPDX-License-Identifier: MPL-2.0
// rust CadDocument XRecord helpers: mesh UVW, viewport annotation scale, layer
// viewport overrides, and EntityChangeRecorder first-touch snapshots.
namespace {
struct ViewportOverrideSpec {
    const char *key;
    const char *section;
    int value_code;
};
std::optional<ViewportOverrideSpec> viewport_override_spec(LayerViewportOverrideKind kind) {
    switch (kind) {
    case LayerViewportOverrideKind::Alpha:
        return ViewportOverrideSpec{"ADSK_XREC_LAYER_ALPHA_OVR", "ADSK_LYR_ALPHA_OVERRIDE", 440};
    case LayerViewportOverrideKind::Color:
        return ViewportOverrideSpec{"ADSK_XREC_LAYER_COLOR_OVR", "ADSK_LYR_COLOR_OVERRIDE", 420};
    case LayerViewportOverrideKind::Linetype:
        return ViewportOverrideSpec{"ADSK_XREC_LAYER_LINETYPE_OVR", "ADSK_LYR_LINETYPE_OVERRIDE", 343};
    case LayerViewportOverrideKind::Lineweight:
        return ViewportOverrideSpec{"ADSK_XREC_LAYER_LINEWT_OVR", "ADSK_LYR_LINEWT_OVERRIDE", 91};
    }
    return std::nullopt;
}
} // namespace

const DxfRecord *DxfDocument::named_xrecord(std::uint64_t owner, std::string_view key) const {
    auto dictionary = extension_dictionary_handle(hex_handle(owner));
    if (dictionary.empty())
        return nullptr;
    auto entry = dictionary_entry(dictionary, key);
    if (entry.empty())
        return nullptr;
    const auto *record = get_record(entry);
    if (!record || record->type() != "XRECORD")
        return nullptr;
    return record;
}

std::uint64_t DxfDocument::ensure_named_xrecord(std::uint64_t owner, std::string_view key) {
    if (auto *existing = named_xrecord(owner, key))
        return handle_value(existing->handle());
    auto dictionary = extension_dictionary_handle(hex_handle(owner));
    if (dictionary.empty())
        dictionary = hex_handle(add_extension_dictionary(hex_handle(owner)));
    auto entry = dictionary_entry(dictionary, key);
    if (!entry.empty()) {
        const auto *record = get_record(entry);
        if (record && record->type() == "XRECORD")
            return handle_value(entry);
        throw std::invalid_argument("dictionary entry is not an XRECORD");
    }
    return add_xrecord(dictionary, std::string(key), {});
}

void DxfDocument::record_entity_before(std::uint64_t handle, std::optional<std::string> before) const {
    if (entity_change_recorder_)
        entity_change_recorder_->record(handle, std::move(before));
}

std::shared_ptr<EntityChangeRecorder> DxfDocument::begin_entity_change_recording() {
    entity_change_recorder_ = std::make_shared<EntityChangeRecorder>();
    return entity_change_recorder_;
}

void DxfDocument::end_entity_change_recording() { entity_change_recorder_.reset(); }

void DxfDocument::record_all_entities_for_transaction() const {
    if (!entity_change_recorder_)
        return;
    for_each<DxfEntity>([&](const auto &entity) {
        auto handle = handle_value(entity.handle());
        if (handle)
            entity_change_recorder_->record(handle, std::string(entity.type()));
    });
}

std::vector<Vector3> DxfDocument::mesh_texture_coordinates(std::uint64_t mesh) const {
    std::vector<Vector3> out;
    const auto *record = named_xrecord(mesh, "ADSK_XREC_SUBDVERTEXTEXCOORDS");
    if (!record)
        return out;
    const auto &pairs = record->pairs_;
    for (std::size_t i = 0; i + 2 < pairs.size(); ++i) {
        if (pairs[i].code != 43)
            continue;
        if (pairs[i + 1].code != 44 || pairs[i + 2].code != 45)
            continue;
        try {
            out.push_back({record->number_at(i), record->number_at(i + 1), record->number_at(i + 2)});
            i += 2;
        } catch (const std::runtime_error &) {
        }
    }
    return out;
}

void DxfDocument::set_mesh_texture_coordinates(std::uint64_t mesh, const std::vector<Vector3> &coordinates) {
    auto id = ensure_named_xrecord(mesh, "ADSK_XREC_SUBDVERTEXTEXCOORDS");
    DxfDocument staged = *this;
    auto &record = staged.mutable_object(id);
    record.pairs_.erase(std::remove_if(record.pairs_.begin(), record.pairs_.end(),
                                       [](const DxfRecord::Pair &pair) { return pair.code >= 43 && pair.code <= 45; }),
                        record.pairs_.end());
    for (const auto &coordinate : coordinates) {
        record.pairs_.push_back({43, record.encode_number(coordinate.x)});
        record.pairs_.push_back({44, record.encode_number(coordinate.y)});
        record.pairs_.push_back({45, record.encode_number(coordinate.z)});
    }
    *this = std::move(staged);
}

std::optional<std::uint64_t> DxfDocument::viewport_annotation_scale(std::uint64_t viewport) const {
    const auto *record = named_xrecord(viewport, "ASDK_XREC_ANNOTATION_SCALE_INFO");
    if (!record)
        return std::nullopt;
    for (const auto &pair : record->pairs_) {
        if (pair.code == 340) {
            auto handle = handle_value(pair.value);
            if (handle)
                return handle;
        }
    }
    return std::nullopt;
}

void DxfDocument::set_viewport_annotation_scale(std::uint64_t viewport, std::uint64_t scale) {
    auto id = ensure_named_xrecord(viewport, "ASDK_XREC_ANNOTATION_SCALE_INFO");
    DxfDocument staged = *this;
    auto &record = staged.mutable_object(id);
    auto encode = [&](int code, const std::string &ascii) {
        if (detail::is_string_value(code))
            return record.encode_string(ascii);
        if (record.binary_)
            return detail::binary_value_from_text(code, ascii);
        return ascii;
    };
    auto encoded = encode(340, hex_handle(scale));
    bool updated = false;
    for (auto &pair : record.pairs_) {
        if (pair.code == 340) {
            pair.value = encoded;
            updated = true;
            break;
        }
    }
    if (!updated)
        record.pairs_.push_back({340, std::move(encoded)});
    *this = std::move(staged);
}

bool DxfDocument::set_layer_viewport_override(std::uint64_t layer, LayerViewportOverrideKind kind,
                                              std::uint64_t viewport, XRecordOverrideValue value) {
    auto spec = viewport_override_spec(kind);
    if (!spec)
        return false;
    const bool handle_value_ok = spec->value_code == 343 && value.kind == XRecordOverrideValue::Kind::Handle;
    const bool int_value_ok = (spec->value_code == 91 || spec->value_code == 420 || spec->value_code == 440) &&
                              value.kind == XRecordOverrideValue::Kind::Int32;
    if (!handle_value_ok && !int_value_ok)
        return false;
    auto id = ensure_named_xrecord(layer, spec->key);
    DxfDocument staged = *this;
    auto &record = staged.mutable_object(id);
    auto encode = [&](int code, const std::string &ascii) {
        if (detail::is_string_value(code))
            return record.encode_string(ascii);
        if (record.binary_)
            return detail::binary_value_from_text(code, ascii);
        return ascii;
    };
    auto closes = [&](const auto &pair) {
        if (pair.code != 102)
            return false;
        auto text = record.decode_string(pair.value);
        return !text.empty() && text.back() == '}';
    };
    std::string encoded;
    if (value.kind == XRecordOverrideValue::Kind::Handle)
        encoded = encode(spec->value_code, hex_handle(value.handle));
    else
        encoded = encode(spec->value_code, std::to_string(value.i32));
    bool updated = false;
    auto &pairs = record.pairs_;
    for (std::size_t index = 0; index < pairs.size(); ++index) {
        if (pairs[index].code != 335 || handle_value(pairs[index].value) != viewport)
            continue;
        for (std::size_t at = index + 1; at < pairs.size(); ++at) {
            if (pairs[at].code == 335 || closes(pairs[at]))
                break;
            if (pairs[at].code == spec->value_code) {
                pairs[at].value = encoded;
                updated = true;
                break;
            }
        }
        if (updated)
            break;
    }
    if (!updated) {
        pairs.push_back({102, encode(102, std::string("{") + spec->section)});
        pairs.push_back({335, encode(335, hex_handle(viewport))});
        pairs.push_back({spec->value_code, std::move(encoded)});
        pairs.push_back({102, encode(102, "}")});
    }
    *this = std::move(staged);
    return true;
}

std::vector<LayerViewportOverride> DxfDocument::layer_viewport_overrides(std::uint64_t layer,
                                                                         LayerViewportOverrideKind kind) const {
    std::vector<LayerViewportOverride> out;
    auto spec = viewport_override_spec(kind);
    if (!spec)
        return out;
    const auto *record = named_xrecord(layer, spec->key);
    if (!record)
        return out;
    std::optional<std::uint64_t> viewport;
    for (const auto &pair : record->pairs_) {
        if (pair.code == 335) {
            viewport = handle_value(pair.value);
            continue;
        }
        if (pair.code != spec->value_code || !viewport)
            continue;
        LayerViewportOverride item;
        item.viewport = *viewport;
        if (spec->value_code == 343)
            item.value = XRecordOverrideValue::from_handle(handle_value(pair.value));
        else if (auto bits = detail::as_i32_bits(pair.value, record->binary_))
            item.value = XRecordOverrideValue::from_int32(static_cast<std::int32_t>(*bits));
        else
            continue;
        out.push_back(item);
        viewport.reset();
    }
    return out;
}
