// SPDX-License-Identifier: MPL-2.0
// DWG associative-object queries and typed rewrite.
std::optional<AssociativeObject> DwgDocument::associative_object(std::uint64_t handle) const {
    if (!handle)
        return std::nullopt;
    auto error = objects_.associative_errors.find(handle);
    if (error != objects_.associative_errors.end())
        throw std::invalid_argument(error->second);
    auto found = objects_.associative_records.find(handle);
    if (found == objects_.associative_records.end())
        return std::nullopt;
    return found->second;
}
void DwgDocument::update_associative_object(const AssociativeObject &object) {
    if (std::holds_alternative<std::monostate>(object.data))
        throw std::invalid_argument("Cannot rewrite unknown associative object");
    if (!object.handle || object.dxf_name.empty())
        throw std::invalid_argument("Associative object requires an identity");
    auto current = associative_object(object.handle);
    if (!current)
        throw std::invalid_argument("Missing associative object");
    if (current->dxf_name != object.dxf_name)
        throw std::invalid_argument("Associative object name mismatch");
    if (current->handle != object.handle)
        throw std::invalid_argument("Associative object handle mismatch");
    *this = DwgDocument(detail::edit_dwg_associative_records(bytes_, objects_, {object}));
}
std::vector<std::uint64_t> DwgDocument::associative_references(std::uint64_t handle) const {
    auto object = associative_object(handle);
    if (!object)
        return {};
    return object->referenced_handles();
}
std::vector<std::uint64_t> DwgDocument::associative_objects_referencing(std::uint64_t target) const {
    std::vector<std::uint64_t> handles;
    for (auto &[handle, object] : objects_.associative_records)
        if (object.references_handle(target))
            handles.push_back(handle);
    std::sort(handles.begin(), handles.end());
    handles.erase(std::unique(handles.begin(), handles.end()), handles.end());
    return handles;
}
void DwgDocument::remap_associative_handles(const std::unordered_map<std::uint64_t, std::uint64_t> &map) {
    std::vector<AssociativeObject> objects;
    for (auto &[handle, object] : objects_.associative_records) {
        if (std::holds_alternative<std::monostate>(object.data))
            continue;
        auto copy = object;
        copy.remap_handles(map);
        objects.push_back(std::move(copy));
    }
    if (objects.empty())
        return;
    *this = DwgDocument(detail::edit_dwg_associative_records(bytes_, objects_, objects));
}
std::vector<std::uint64_t> DwgDocument::associative_owned_network(std::uint64_t network) const {
    std::vector<std::uint64_t> owned;
    std::unordered_set<std::uint64_t> seen;
    std::vector<std::uint64_t> stack{network};
    while (!stack.empty()) {
        auto handle = stack.back();
        stack.pop_back();
        if (!handle || !seen.insert(handle).second)
            continue;
        owned.push_back(handle);
        auto object = associative_object(handle);
        if (!object)
            continue;
        if (auto *value = std::get_if<AssocNetwork>(&object->data))
            for (auto action : value->owned_actions)
                stack.push_back(action);
    }
    return owned;
}
