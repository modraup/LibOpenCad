// SPDX-License-Identifier: MPL-2.0
void decode_architecture_record(DwgR2000Objects &result, std::string_view body, DwgR2000Record rec,
                                std::uint64_t handle, unsigned version, const char *page,
                                std::string_view name) {
    auto kind = architecture_kind_of(name);
    std::string raw(body);
    auto handle_bits = static_cast<std::int64_t>(raw.size() * 8 - rec.main_bits);
    if (kind || is_civil_entity_name(name)) {
        ArchEntityData e;
        e.dxf_name = std::string(name);
        e.cpp_class_name = architecture_cpp_class_name(name).value_or("");
        e.kind = kind.value_or(ArchKind::Other);
        e.dwg_type_code = rec.type;
        e.dwg_handle_bits = handle_bits;
        e.dwg_source_version = static_cast<DxfVersion>(version);
        e.raw_dwg_data = raw;
        e.handle = handle;
        e.entity = true;
        if (e.cpp_class_name.empty())
            for (auto &c : result.classes)
                if (c.dxf_name == name) {
                    e.cpp_class_name = c.cpp_name;
                    break;
                }
        try {
            auto s = open_dynamic(raw, rec, handle, version, page);
            std::vector<std::uint8_t> graphics;
            std::size_t graphics_bit = 0;
            auto props = read_entity_properties(s.main, s.refs, handle, s.end, version, rec, nullptr, nullptr,
                                                &graphics, &graphics_bit);
            e.graphic_data = std::move(graphics);
            e.profiles = decode_profiles(e.graphic_data);
            e.owner = props.owner;
            e.layer = props.layer;
            e.extension_dictionary = props.extension_dictionary;
            e.reactors = props.reactors;
            for (auto &[h, symbol] : result.symbols)
                if (symbol.type == 51 && h == e.layer)
                    e.layer_name = symbol.name;
        } catch (const std::exception &) {
        }
        e.decode_fields();
        result.arch_entities.emplace(handle, std::move(e));
        return;
    }
    ArchObject o;
    o.dxf_name = std::string(name);
    o.cpp_class_name = architecture_cpp_class_name(name).value_or("");
    for (auto &c : result.classes)
        if (c.dxf_name == name && o.cpp_class_name.empty())
            o.cpp_class_name = c.cpp_name;
    o.kind = ArchKind::Other;
    o.handle = handle;
    o.raw_dwg_data = raw;
    o.raw_dwg_handle_bits = handle_bits;
    o.raw_dwg_version = static_cast<DxfVersion>(version);
    try {
        auto s = open_dynamic(raw, rec, handle, version, page);
        skip_entity_prefix(s.main, s.end, false, version);
        o.owner = s.href();
        auto reactors = s.main.bit_long();
        require(reactors >= 0 && reactors <= 1000000, "Invalid Arch reactor count");
        for (int i = 0; i < reactors; ++i)
            o.reactors.push_back(s.href());
        if (version < 1018 || !s.main.bits(1))
            o.extension_dictionary = s.href();
    } catch (const std::exception &) {
    }
    result.arch_objects.emplace(handle, std::move(o));
}
