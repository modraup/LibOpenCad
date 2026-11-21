// SPDX-License-Identifier: MPL-2.0
// DWG class-object queries, rewrite and allocation.
std::optional<ClassObject> DwgDocument::class_object(std::uint64_t handle) const {
    if (!handle)
        return std::nullopt;
    auto error = objects_.class_object_errors.find(handle);
    if (error != objects_.class_object_errors.end())
        throw std::invalid_argument(error->second);
    auto found = objects_.class_objects.find(handle);
    if (found == objects_.class_objects.end())
        return std::nullopt;
    return found->second;
}
void DwgDocument::update_class_object(const ClassObject &object) {
    if (!object.handle || object.dxf_name.empty())
        throw std::invalid_argument("Class object requires an identity");
    auto current = class_object(object.handle);
    if (!current)
        throw std::invalid_argument("Missing class object");
    if (current->dxf_name != object.dxf_name)
        throw std::invalid_argument("Class object name mismatch");
    if (current->handle != object.handle)
        throw std::invalid_argument("Class object handle mismatch");
    *this = DwgDocument(detail::edit_dwg_class_objects(source_bytes(), objects_, {object}));
}
std::uint64_t DwgDocument::insert_class_object(ClassObject object) {
    if (object.handle && objects_.class_objects.count(object.handle)) {
        update_class_object(object);
        return object.handle;
    }
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_class_object(source_bytes(), objects_, std::move(object), id));
    return id;
}
void DwgDocument::remap_class_object_handles(const std::unordered_map<std::uint64_t, std::uint64_t> &map) {
    std::vector<ClassObject> objects;
    for (auto &[handle, object] : objects_.class_objects) {
        auto copy = object;
        copy.remap_handles(map);
        objects.push_back(std::move(copy));
    }
    if (objects.empty())
        return;
    *this = DwgDocument(detail::edit_dwg_class_objects(source_bytes(), objects_, objects));
}
namespace {
std::optional<ObjectDataTable> dwg_document_object_data_table(const DwgR2000Objects &o, std::uint64_t handle) {
    auto found = o.class_objects.find(handle);
    if (found == o.class_objects.end())
        return std::nullopt;
    auto *descriptor = std::get_if<ObjectDataDescriptor>(&found->second.data);
    return descriptor ? descriptor->as_table() : std::nullopt;
}
std::uint64_t dwg_document_object_data_dictionary(const DwgR2000Objects &o) {
    for (const auto &[handle, record] : o.metadata_records) {
        bool nod = false;
        std::uint64_t types = 0;
        for (const auto &entry : record.dictionary_entries) {
            if (entry.first == "ACAD_GROUP" || entry.first == "ACAD_LAYOUT" || entry.first == "ACAD_MLINESTYLE")
                nod = true;
            if (entry.first == object_data_dictionary_key)
                types = entry.second;
        }
        if (nod && types)
            return types;
    }
    return 0;
}
const DwgR2000EntityProperties *dwg_document_entity_properties(const DwgR2000Objects &o, std::uint64_t handle) {
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
} // namespace
std::vector<std::pair<std::uint64_t, ObjectDataTable>> DwgDocument::object_data_tables() const {
    auto dictionary = dwg_document_object_data_dictionary(objects_);
    if (!dictionary)
        return {};
    auto found = objects_.class_objects.find(dictionary);
    if (found == objects_.class_objects.end())
        return {};
    auto *types = std::get_if<ObjectDataDictionary>(&found->second.data);
    if (!types)
        return {};
    std::vector<std::pair<std::uint64_t, ObjectDataTable>> out;
    for (const auto &entry : types->entries)
        if (auto table = dwg_document_object_data_table(objects_, entry.second))
            out.push_back({entry.second, std::move(*table)});
    return out;
}
std::uint64_t DwgDocument::add_object_data_table(const ObjectDataTable &table) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::add_dwg_object_data_table(source_bytes(), objects_, table, id));
    return id;
}
std::uint64_t DwgDocument::attach_object_data(std::uint64_t entity, std::uint64_t table,
                                             const std::vector<ObjectDataValue> &values) {
    std::uint64_t id = 0;
    *this = DwgDocument(detail::attach_dwg_object_data(source_bytes(), objects_, entity, table, values, id));
    return id;
}
std::vector<ObjectDataAttachment> DwgDocument::object_data(std::uint64_t entity) const {
    auto props = dwg_document_entity_properties(objects_, entity);
    if (!props || !props->extension_dictionary)
        return {};
    auto rec = objects_.metadata_records.find(props->extension_dictionary);
    if (rec == objects_.metadata_records.end() || rec->second.type != "DICTIONARY")
        return {};
    std::vector<ObjectDataAttachment> out;
    for (const auto &entry : rec->second.dictionary_entries) {
        auto found = objects_.class_objects.find(entry.second);
        if (found == objects_.class_objects.end())
            continue;
        auto *record = std::get_if<ObjectDataRecord>(&found->second.data);
        if (!record)
            continue;
        auto table = dwg_document_object_data_table(objects_, record->table);
        if (!table)
            continue;
        auto values = record->values(table->fields);
        if (!values)
            continue;
        out.push_back({entry.second, record->table, std::move(*values)});
    }
    return out;
}
