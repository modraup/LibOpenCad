// SPDX-License-Identifier: MPL-2.0
// Map 3D Object Data graph APIs on DWG, matching acadrust CadDocument.
namespace {
bool object_data_ascii_ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
        };
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}
int object_data_class_number(const DwgR2000Objects &o, std::string_view name) {
    for (const auto &c : o.classes)
        if (c.dxf_name == name)
            return c.number;
    throw std::invalid_argument("Missing class object class");
}
const DwgR2000EntityProperties *dwg_entity_properties(const DwgR2000Objects &o, std::uint64_t handle) {
    if (auto i = o.lines.find(handle); i != o.lines.end())
        return &i->second.properties;
    if (auto i = o.circles.find(handle); i != o.circles.end())
        return &i->second.properties;
    if (auto i = o.arcs.find(handle); i != o.arcs.end())
        return &i->second.properties;
    if (auto i = o.texts.find(handle); i != o.texts.end())
        return &i->second.properties;
    if (auto i = o.inserts.find(handle); i != o.inserts.end())
        return &i->second.properties;
    if (auto i = o.modelers.find(handle); i != o.modelers.end())
        return &i->second.properties;
    return nullptr;
}
std::optional<ObjectDataTable> dwg_object_data_table(const DwgR2000Objects &o, std::uint64_t handle) {
    auto found = o.class_objects.find(handle);
    if (found == o.class_objects.end())
        return std::nullopt;
    auto *descriptor = std::get_if<ObjectDataDescriptor>(&found->second.data);
    return descriptor ? descriptor->as_table() : std::nullopt;
}
std::uint64_t dwg_object_data_dictionary(const DwgR2000Objects &o) {
    auto nod = named_object_dictionary(o);
    auto rec = o.metadata_records.find(nod);
    if (rec == o.metadata_records.end())
        return 0;
    for (const auto &entry : rec->second.dictionary_entries)
        if (entry.first == object_data_dictionary_key)
            return entry.second;
    return 0;
}
std::vector<std::pair<std::uint64_t, ObjectDataTable>> dwg_object_data_tables(const DwgR2000Objects &o) {
    auto dictionary = dwg_object_data_dictionary(o);
    if (!dictionary)
        return {};
    auto found = o.class_objects.find(dictionary);
    if (found == o.class_objects.end())
        return {};
    auto *types = std::get_if<ObjectDataDictionary>(&found->second.data);
    if (!types)
        return {};
    std::vector<std::pair<std::uint64_t, ObjectDataTable>> out;
    for (const auto &entry : types->entries)
        if (auto table = dwg_object_data_table(o, entry.second))
            out.push_back({entry.second, std::move(*table)});
    return out;
}
std::vector<ObjectDataAttachment> dwg_object_data(const DwgR2000Objects &o, std::uint64_t entity) {
    auto props = dwg_entity_properties(o, entity);
    if (!props || !props->extension_dictionary)
        return {};
    auto rec = o.metadata_records.find(props->extension_dictionary);
    if (rec == o.metadata_records.end() || rec->second.type != "DICTIONARY")
        return {};
    std::vector<ObjectDataAttachment> out;
    for (const auto &entry : rec->second.dictionary_entries) {
        auto found = o.class_objects.find(entry.second);
        if (found == o.class_objects.end())
            continue;
        auto *record = std::get_if<ObjectDataRecord>(&found->second.data);
        if (!record)
            continue;
        auto table = dwg_object_data_table(o, record->table);
        if (!table)
            continue;
        auto values = record->values(table->fields);
        if (!values)
            continue;
        out.push_back({entry.second, record->table, std::move(*values)});
    }
    return out;
}
std::string encode_object_data_xdictionary(unsigned v, const char *page, std::uint64_t id, std::uint64_t owner,
                                           std::uint64_t xdic, const std::vector<std::uint64_t> &reactors,
                                           const std::vector<std::pair<std::string, std::uint64_t>> &entries) {
    DwgBitWriter main, refs, strings;
    auto split = object_prefix(main, refs, v, 42, id, owner, xdic, reactors);
    require(entries.size() <= 1000000, "Invalid dictionary count");
    main.bit_long(static_cast<std::int32_t>(entries.size()));
    main.bit_short(1);
    main.bits(8, 1);
    for (const auto &[name, target] : entries) {
        symbol_string(main, strings, v, name, page);
        refs.absolute_handle(2, target);
    }
    return finish_object(main, strings, refs, v, split);
}
const std::vector<std::tuple<std::string, std::string, std::int16_t>> object_data_classes = {
    {"IRD_DSC_DICT", "CIrdDscDict", 499},
    {"IRD_DSC_RECORD", "CIrdDscRecord", 499},
    {"IRD_OBJ_RECORD", "CIrdObjRecord", 499},
};
ClassObject object_data_class_object(std::uint64_t handle, std::uint64_t owner, ClassObjectData data) {
    ClassObject rec;
    rec.handle = handle;
    rec.owner = owner;
    rec.reactors = {owner};
    rec.data = std::move(data);
    rec.dxf_name = class_object_dxf_name(rec.data);
    rec.cpp_class_name = class_object_cpp_name(rec.dxf_name);
    return rec;
}
} // namespace
std::string detail::add_dwg_object_data_table(std::string_view file, const DwgR2000Objects &o,
                                              const ObjectDataTable &table, std::uint64_t &id) {
    auto registered = register_dwg_classes(file, o, object_data_classes);
    if (registered != file)
        return add_dwg_object_data_table(registered, read_dwg_objects(registered), table, id);
    LifecycleStorage s(file);
    require(s.version >= 1018, "Class object construction requires R2004+");
    for (const auto &[handle, existing] : dwg_object_data_tables(o)) {
        (void)handle;
        require(!object_data_ascii_ieq(existing.name, table.name),
                ("Object Data table " + table.name + " already exists").c_str());
    }
    auto dictionary = dwg_object_data_dictionary(o);
    if (dictionary) {
        auto found = o.class_objects.find(dictionary);
        require(found != o.class_objects.end() &&
                    std::holds_alternative<ObjectDataDictionary>(found->second.data),
                "IrdDscDictionary is not an Object Data dictionary");
    }
    HandleSeed seed(file, o, s.version);
    LifecycleEdits added, edits;
    auto descriptor = ObjectDataDescriptor::table(table);
    if (!dictionary) {
        auto nod = named_object_dictionary(o);
        require(o.metadata_records.count(nod), "Missing named objects dictionary");
        dictionary = seed.next();
        ObjectDataDictionary types;
        for (const auto &built_in : object_data_built_in_descriptors()) {
            auto handle = seed.next();
            types.entries.push_back({object_data_next_key(types.entries), handle});
            added[handle] = encode_class_object(s.version, s.page, object_data_class_number(o, "IRD_DSC_RECORD"),
                                                object_data_class_object(handle, dictionary, built_in));
        }
        id = seed.next();
        types.entries.push_back({object_data_next_key(types.entries), id});
        added[id] = encode_class_object(s.version, s.page, object_data_class_number(o, "IRD_DSC_RECORD"),
                                        object_data_class_object(id, dictionary, descriptor));
        added[dictionary] =
            encode_class_object(s.version, s.page, object_data_class_number(o, "IRD_DSC_DICT"),
                                object_data_class_object(dictionary, nod, std::move(types)));
        auto nod_rec = o.metadata_records.at(nod);
        auto nod_entries = nod_rec.dictionary_entries;
        nod_entries.emplace_back(object_data_dictionary_key, dictionary);
        edits[nod] = encode_named_dictionary(s.version, s.page, nod, nod_rec.owner, nod_rec.extension_dictionary,
                                             nod_rec.reactors, nod_entries);
    } else {
        auto current = o.class_objects.at(dictionary);
        auto types = std::get<ObjectDataDictionary>(current.data);
        id = seed.next();
        types.entries.push_back({object_data_next_key(types.entries), id});
        current.data = std::move(types);
        edits[dictionary] =
            encode_class_object(s.version, s.page, o.records.at(dictionary).type, current);
        added[id] = encode_class_object(s.version, s.page, object_data_class_number(o, "IRD_DSC_RECORD"),
                                        object_data_class_object(id, dictionary, descriptor));
    }
    return lifecycle_rewrite(file, o, edits, {}, added, seed.header, seed.maximum + 1);
}
std::string detail::attach_dwg_object_data(std::string_view file, const DwgR2000Objects &o, std::uint64_t entity,
                                           std::uint64_t table, const std::vector<ObjectDataValue> &values,
                                           std::uint64_t &id) {
    auto registered = register_dwg_classes(file, o, object_data_classes);
    if (registered != file)
        return attach_dwg_object_data(registered, read_dwg_objects(registered), entity, table, values, id);
    LifecycleStorage s(file);
    require(s.version >= 1018, "Class object construction requires R2004+");
    auto fields = dwg_object_data_table(o, table);
    require(fields.has_value(), "Object Data table not found");
    ObjectDataRecord record(table, fields->fields, values);
    auto props = dwg_entity_properties(o, entity);
    require(props, "Object Data entity not found");
    HandleSeed seed(file, o, s.version);
    LifecycleEdits added, edits;
    std::uint64_t dict = props->extension_dictionary;
    std::vector<std::pair<std::string, std::uint64_t>> entries;
    std::uint64_t dict_owner = entity;
    std::uint64_t dict_xdic = 0;
    std::vector<std::uint64_t> dict_reactors;
    if (dict) {
        auto rec = o.metadata_records.find(dict);
        require(rec != o.metadata_records.end() && rec->second.type == "DICTIONARY" && rec->second.owner == entity,
                "Missing or foreign metadata dictionary");
        entries = rec->second.dictionary_entries;
        dict_owner = rec->second.owner;
        dict_xdic = rec->second.extension_dictionary;
        dict_reactors = rec->second.reactors;
    } else
        dict = seed.next();
    id = seed.next();
    entries.emplace_back(object_data_next_key(entries), id);
    auto dict_bytes =
        encode_object_data_xdictionary(s.version, s.page, dict, dict_owner, dict_xdic, dict_reactors, entries);
    if (o.records.count(dict))
        edits[dict] = dict_bytes;
    else
        added[dict] = dict_bytes;
    added[id] = encode_class_object(s.version, s.page, object_data_class_number(o, "IRD_OBJ_RECORD"),
                                    object_data_class_object(id, dict, std::move(record)));
    if (!props->extension_dictionary) {
        auto patched = with_xdic(s.body(o, entity), o.records.at(entity), entity, s.version, true, dict);
        if (!patched.empty())
            edits[entity] = patched;
    }
    return lifecycle_rewrite(file, o, edits, {}, added, seed.header, seed.maximum + 1);
}
