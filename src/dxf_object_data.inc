// SPDX-License-Identifier: MPL-2.0
// Map 3D Object Data graph APIs from acadrust objects/object_data.rs CadDocument methods.
namespace {
bool ascii_ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c); };
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}
bool dxf_is_entity(const DxfDocument &d, std::uint64_t handle) {
    bool found = false;
    d.for_each<DxfEntity>([&](const DxfEntity &entity) {
        if (handle_value(entity.handle()) == handle)
            found = true;
    });
    return found;
}
} // namespace
const DxfClassObject *DxfDocument::class_object_record(std::uint64_t handle) const {
    const DxfClassObject *found = nullptr;
    for_each<DxfClassObject>([&](const DxfClassObject &object) {
        if (handle_value(object.handle()) == handle)
            found = &object;
    });
    return found;
}
std::vector<std::pair<std::string, std::uint64_t>>
DxfDocument::named_dictionary_entries(std::string_view dictionary) const {
    const auto *dict = get_record(dictionary);
    if (!dict || dict->type() != "DICTIONARY")
        return {};
    std::vector<std::pair<std::string, std::uint64_t>> out;
    std::string name;
    dict->locate_if("AcDbDictionary", [&](std::size_t i, int code) {
        const auto &p = dict->pairs_[i];
        if (code == 3)
            name = dict->decode_string(p.value);
        else if ((code == 350 || code == 360) && !name.empty()) {
            out.push_back({std::move(name), parse_handle(p.value).value_or(0)});
            name.clear();
        }
        return false;
    });
    return out;
}
DxfClassObject *DxfDocument::class_object_record(std::uint64_t handle) {
    DxfClassObject *found = nullptr;
    for_each<DxfClassObject>([&](DxfClassObject &object) {
        if (handle_value(object.handle()) == handle)
            found = &object;
    });
    return found;
}
std::optional<ObjectDataTable> DxfDocument::object_data_table(std::uint64_t descriptor) const {
    auto *object = class_object_record(descriptor);
    if (!object)
        return std::nullopt;
    auto data = object->definition();
    auto *value = std::get_if<ObjectDataDescriptor>(&data);
    return value ? value->as_table() : std::nullopt;
}
std::uint64_t DxfDocument::insert_owned_class_object(std::uint64_t owner, const ClassObjectData &value) {
    auto type = class_object_dxf_name(value);
    DxfClassObject object;
    object.version_ = version_;
    static_cast<DxfRecord &>(object) = make_record(type);
    object.set_field("", 330, hex_handle(owner));
    object.pairs_.insert(object.pairs_.end(),
                         {{102, "{ACAD_REACTORS"}, {330, hex_handle(owner)}, {102, "}"}});
    for (const auto &marker : class_object_subclasses(type))
        object.pairs_.push_back({100, marker});
    object.set_definition(value);
    return insert_object(std::move(object));
}
std::uint64_t DxfDocument::add_object_data_descriptor(std::uint64_t dictionary,
                                                     const ObjectDataDescriptor &descriptor) {
    auto *object = class_object_record(dictionary);
    if (!object)
        throw std::invalid_argument("Missing Object Data dictionary");
    auto data = object->definition();
    auto *types = std::get_if<ObjectDataDictionary>(&data);
    if (!types)
        throw std::invalid_argument("IrdDscDictionary is not an Object Data dictionary");
    auto key = object_data_next_key(types->entries);
    auto handle = insert_owned_class_object(dictionary, descriptor);
    object = class_object_record(dictionary);
    data = object->definition();
    types = std::get_if<ObjectDataDictionary>(&data);
    types->entries.push_back({std::move(key), handle});
    object->set_definition(*types);
    return handle;
}
std::uint64_t DxfDocument::ensure_object_data_dictionary() {
    auto root = root_dictionary_handle();
    auto existing = dictionary_entry(root, object_data_dictionary_key);
    if (!existing.empty()) {
        auto *object = class_object_record(handle_value(existing));
        if (!object)
            throw std::invalid_argument("IrdDscDictionary is not an Object Data dictionary");
        auto data = object->definition();
        if (!std::holds_alternative<ObjectDataDictionary>(data))
            throw std::invalid_argument("IrdDscDictionary is not an Object Data dictionary");
        return handle_value(existing);
    }
    auto handle = add_class_object(root, object_data_dictionary_key, ObjectDataDictionary{});
    if (auto *object = class_object_record(handle)) {
        bool has_reactors = false;
        std::size_t subclass = object->pairs_.size();
        for (std::size_t i = 0; i < object->pairs_.size(); ++i) {
            const auto &pair = object->pairs_[i];
            if (pair.code == 102 && pair.value == "{ACAD_REACTORS")
                has_reactors = true;
            if (pair.code == 100 && subclass == object->pairs_.size())
                subclass = i;
        }
        if (!has_reactors)
            object->pairs_.insert(object->pairs_.begin() + subclass,
                                  {{102, "{ACAD_REACTORS"}, {330, root}, {102, "}"}});
    }
    for (const auto &descriptor : object_data_built_in_descriptors())
        add_object_data_descriptor(handle, descriptor);
    return handle;
}
std::vector<std::pair<std::uint64_t, ObjectDataTable>> DxfDocument::object_data_tables() const {
    auto root = root_dictionary_handle();
    auto existing = dictionary_entry(root, object_data_dictionary_key);
    if (existing.empty())
        return {};
    auto *object = class_object_record(handle_value(existing));
    if (!object)
        return {};
    auto data = object->definition();
    auto *types = std::get_if<ObjectDataDictionary>(&data);
    if (!types)
        return {};
    std::vector<std::pair<std::uint64_t, ObjectDataTable>> out;
    for (const auto &entry : types->entries)
        if (auto table = object_data_table(entry.second))
            out.push_back({entry.second, std::move(*table)});
    return out;
}
std::uint64_t DxfDocument::add_object_data_table(const ObjectDataTable &table) {
    if (version_ < DxfVersion::AC1018)
        throw std::invalid_argument("Class object construction requires R2004+");
    for (const auto &[handle, existing] : object_data_tables()) {
        (void)handle;
        if (ascii_ieq(existing.name, table.name))
            throw std::invalid_argument("Object Data table " + table.name + " already exists");
    }
    DxfDocument staged = *this;
    auto dictionary = staged.ensure_object_data_dictionary();
    auto handle = staged.add_object_data_descriptor(dictionary, ObjectDataDescriptor::table(table));
    *this = std::move(staged);
    return handle;
}
std::uint64_t DxfDocument::attach_object_data(std::uint64_t entity, std::uint64_t table,
                                             const std::vector<ObjectDataValue> &values) {
    if (version_ < DxfVersion::AC1018)
        throw std::invalid_argument("Class object construction requires R2004+");
    auto fields = object_data_table(table);
    if (!fields)
        throw std::invalid_argument("Object Data table not found");
    ObjectDataRecord record(table, fields->fields, values);
    if (!dxf_is_entity(*this, entity))
        throw std::invalid_argument("Object Data entity not found");
    DxfDocument staged = *this;
    auto owner = hex_handle(entity);
    auto dict = staged.extension_dictionary_handle(owner);
    if (dict.empty())
        dict = hex_handle(staged.add_extension_dictionary(owner));
    auto *dictionary = const_cast<DxfRecord *>(staged.get_record(dict));
    if (!dictionary || dictionary->type() != "DICTIONARY")
        throw std::invalid_argument("Missing extension dictionary");
    dictionary->set_short_field("AcDbDictionary", 281, 1);
    auto key = object_data_next_key(staged.named_dictionary_entries(dict));
    auto type = class_object_dxf_name(record);
    DxfClassObject object;
    object.version_ = staged.version_;
    static_cast<DxfRecord &>(object) = staged.make_record(type);
    object.set_field("", 330, dict);
    object.pairs_.insert(object.pairs_.end(), {{102, "{ACAD_REACTORS"}, {330, dict}, {102, "}"}});
    for (const auto &marker : class_object_subclasses(type))
        object.pairs_.push_back({100, marker});
    object.set_definition(record);
    auto handle = staged.add_dictionary_object(dict, std::move(key), object);
    *this = std::move(staged);
    return handle;
}
std::vector<ObjectDataAttachment> DxfDocument::object_data(std::uint64_t entity) const {
    if (!dxf_is_entity(*this, entity))
        return {};
    auto dict = extension_dictionary_handle(hex_handle(entity));
    if (dict.empty())
        return {};
    const auto *dictionary = get_record(dict);
    if (!dictionary)
        return {};
    std::vector<ObjectDataAttachment> out;
    for (const auto &entry : named_dictionary_entries(dict)) {
        auto *object = class_object_record(entry.second);
        if (!object)
            continue;
        auto data = object->definition();
        auto *record = std::get_if<ObjectDataRecord>(&data);
        if (!record)
            continue;
        auto table = object_data_table(record->table);
        if (!table)
            continue;
        auto values = record->values(table->fields);
        if (!values)
            continue;
        out.push_back({entry.second, record->table, std::move(*values)});
    }
    return out;
}
