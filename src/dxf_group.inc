// SPDX-License-Identifier: MPL-2.0
// rust CadDocument named groups (ACAD_GROUP, GROUP objects, entity reactors).
std::string DxfDocument::ensure_group_dictionary() {
    auto nod = root_dictionary_handle();
    auto existing = dictionary_entry(nod, "ACAD_GROUP");
    if (!existing.empty()) {
        const auto *rec = get_record(existing);
        if (rec && rec->type() == "DICTIONARY")
            return existing;
        throw std::invalid_argument("ACAD_GROUP is not a dictionary");
    }
    return hex_handle(add_dictionary(nod, "ACAD_GROUP"));
}

std::vector<std::pair<std::string, std::uint64_t>> DxfDocument::group_dictionary_entries() const {
    std::vector<std::pair<std::string, std::uint64_t>> out;
    auto nod = root_dictionary_handle();
    if (nod.empty())
        return out;
    auto dict = dictionary_entry(nod, "ACAD_GROUP");
    if (dict.empty())
        return out;
    const auto *record = get_record(dict);
    if (!record || record->type() != "DICTIONARY")
        return out;
    bool active = false;
    std::string key;
    record->locate_if("AcDbDictionary", [&](std::size_t i, int code) {
        const auto &p = record->pairs_[i];
        if (code == 3) {
            active = true;
            key = record->decode_string(p.value);
        } else if (active && (code == 350 || code == 360)) {
            out.push_back({key, handle_value(p.value)});
            active = false;
        }
        return false;
    });
    return out;
}

std::optional<GroupObject> DxfDocument::group_object_at(std::uint64_t handle, std::string name) const {
    std::optional<GroupObject> out;
    for_each<DxfAuxiliaryObject>([&](const auto &o) {
        if (handle_value(o.handle()) != handle || o.type() != "GROUP")
            return;
        GroupObject g;
        g.handle = handle;
        g.owner = handle_value(o.owner_handle());
        g.name = std::move(name);
        g.definition = std::get<GroupDefinition>(o.definition());
        out = std::move(g);
    });
    return out;
}

void DxfDocument::attach_group_reactor(std::uint64_t entity, std::uint64_t group) {
    auto found = handle_index_.find(entity);
    if (found == handle_index_.end() || found->second.empty())
        return;
    auto &record = const_cast<DxfRecord &>(record_at(found->second.back()));
    auto hex = hex_handle(group);
    bool in_reactors = false;
    unsigned depth = 0;
    std::size_t reactors_end = std::string_view::npos;
    for (std::size_t i = 0; i < record.pairs_.size(); ++i) {
        const auto &p = record.pairs_[i];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{') {
                if (!depth && p.value == "{ACAD_REACTORS")
                    in_reactors = true;
                ++depth;
            } else if (p.value == "}" && depth) {
                if (in_reactors && depth == 1)
                    reactors_end = i;
                --depth;
                if (!depth)
                    in_reactors = false;
            }
            continue;
        }
        if (in_reactors && p.code == 330 && handle_value(p.value) == group)
            return;
    }
    if (reactors_end != std::string_view::npos) {
        record.pairs_.insert(record.pairs_.begin() + static_cast<std::ptrdiff_t>(reactors_end), {330, hex});
        return;
    }
    std::size_t insertion = record.pairs_.size();
    depth = 0;
    for (std::size_t i = 1; i < record.pairs_.size(); ++i) {
        const auto &p = record.pairs_[i];
        if (!depth && (p.code == 100 || p.code == 1001 || p.code == 101)) {
            insertion = i;
            break;
        }
        if (p.code != 102)
            continue;
        if (!p.value.empty() && p.value.front() == '{')
            ++depth;
        else if (p.value == "}" && depth)
            --depth;
    }
    record.pairs_.insert(record.pairs_.begin() + static_cast<std::ptrdiff_t>(insertion),
                         {{102, "{ACAD_REACTORS"}, {330, hex}, {102, "}"}});
}

void DxfDocument::detach_group_reactor(std::uint64_t entity, std::uint64_t group) {
    auto found = handle_index_.find(entity);
    if (found == handle_index_.end() || found->second.empty())
        return;
    auto &record = const_cast<DxfRecord &>(record_at(found->second.back()));
    std::size_t begin = std::string_view::npos, end = std::string_view::npos;
    unsigned depth = 0;
    bool in_reactors = false;
    std::vector<std::size_t> drop;
    std::size_t remaining = 0;
    for (std::size_t i = 0; i < record.pairs_.size(); ++i) {
        const auto &p = record.pairs_[i];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{') {
                if (!depth && p.value == "{ACAD_REACTORS") {
                    in_reactors = true;
                    begin = i;
                }
                ++depth;
            } else if (p.value == "}" && depth) {
                if (in_reactors && depth == 1)
                    end = i;
                --depth;
                if (!depth)
                    in_reactors = false;
            }
            continue;
        }
        if (!in_reactors || p.code != 330)
            continue;
        if (handle_value(p.value) == group)
            drop.push_back(i);
        else
            ++remaining;
    }
    if (drop.empty())
        return;
    if (!remaining && begin != std::string_view::npos && end != std::string_view::npos && end > begin) {
        record.pairs_.erase(record.pairs_.begin() + static_cast<std::ptrdiff_t>(begin),
                            record.pairs_.begin() + static_cast<std::ptrdiff_t>(end + 1));
        return;
    }
    for (auto i = drop.rbegin(); i != drop.rend(); ++i)
        record.pairs_.erase(record.pairs_.begin() + static_cast<std::ptrdiff_t>(*i));
}

std::optional<GroupObject> DxfDocument::get_group(std::string_view name) const {
    for (const auto &entry : group_dictionary_entries()) {
        if (detail::symbol_name_key(entry.first) != detail::symbol_name_key(name))
            continue;
        if (auto g = group_object_at(entry.second, entry.first))
            return g;
    }
    return std::nullopt;
}

std::optional<GroupObject> DxfDocument::get_group_by_handle(std::uint64_t handle) const {
    std::string name;
    for (const auto &entry : group_dictionary_entries())
        if (entry.second == handle)
            name = entry.first;
    return group_object_at(handle, std::move(name));
}

std::vector<GroupObject> DxfDocument::groups() const {
    std::vector<GroupObject> out;
    auto names = group_dictionary_entries();
    for_each<DxfAuxiliaryObject>([&](const auto &o) {
        if (o.type() != "GROUP")
            return;
        auto h = handle_value(o.handle());
        std::string name;
        for (const auto &entry : names)
            if (entry.second == h)
                name = entry.first;
        if (auto g = group_object_at(h, std::move(name)))
            out.push_back(std::move(*g));
    });
    return out;
}

std::uint64_t DxfDocument::create_group(std::string name, const std::vector<std::uint64_t> &entities) {
    if (name.empty())
        throw std::invalid_argument("group name cannot be empty");
    DxfDocument staged = *this;
    auto dict = staged.ensure_group_dictionary();
    if (!staged.dictionary_entry(dict, name).empty())
        throw std::invalid_argument("group already exists");
    GroupDefinition v;
    v.selectable = true;
    for (auto h : entities)
        if (std::find(v.entities.begin(), v.entities.end(), h) == v.entities.end())
            v.entities.push_back(h);
    auto id = staged.add_auxiliary_object(dict, std::move(name), v);
    for (auto h : v.entities)
        staged.attach_group_reactor(h, id);
    *this = std::move(staged);
    return id;
}

std::uint64_t DxfDocument::create_unnamed_group(const std::vector<std::uint64_t> &entities) {
    DxfDocument staged = *this;
    auto dict = staged.ensure_group_dictionary();
    std::string name;
    for (int i = 1;; ++i) {
        auto candidate = "*A" + std::to_string(i);
        if (staged.dictionary_entry(dict, candidate).empty()) {
            name = std::move(candidate);
            break;
        }
    }
    GroupDefinition v;
    v.unnamed = true;
    v.selectable = true;
    for (auto h : entities)
        if (std::find(v.entities.begin(), v.entities.end(), h) == v.entities.end())
            v.entities.push_back(h);
    auto id = staged.add_auxiliary_object(dict, name, v);
    for (auto h : v.entities)
        staged.attach_group_reactor(h, id);
    *this = std::move(staged);
    return id;
}

void DxfDocument::add_entity_to_group(std::string_view name, std::uint64_t entity) {
    auto found = get_group(name);
    if (!found)
        throw std::invalid_argument("group not found: " + std::string(name));
    DxfDocument staged = *this;
    auto next = found->definition;
    if (std::find(next.entities.begin(), next.entities.end(), entity) == next.entities.end())
        next.entities.push_back(entity);
    staged.set_auxiliary_definition(hex_handle(found->handle), next);
    staged.attach_group_reactor(entity, found->handle);
    *this = std::move(staged);
}

void DxfDocument::remove_entity_from_group(std::string_view name, std::uint64_t entity) {
    auto found = get_group(name);
    if (!found)
        throw std::invalid_argument("group not found: " + std::string(name));
    DxfDocument staged = *this;
    auto next = found->definition;
    next.entities.erase(std::remove(next.entities.begin(), next.entities.end(), entity), next.entities.end());
    staged.set_auxiliary_definition(hex_handle(found->handle), next);
    staged.detach_group_reactor(entity, found->handle);
    *this = std::move(staged);
}

std::optional<GroupObject> DxfDocument::remove_group(std::string_view name) {
    auto found = get_group(name);
    if (!found)
        return std::nullopt;
    DxfDocument staged = *this;
    for (auto h : found->definition.entities)
        staged.detach_group_reactor(h, found->handle);
    auto dict = staged.dictionary_entry(staged.root_dictionary_handle(), "ACAD_GROUP");
    if (!dict.empty()) {
        auto loc = staged.handle_index_.find(handle_value(dict));
        if (loc != staged.handle_index_.end() && !loc->second.empty())
            staged.erase_dictionary_entry(const_cast<DxfRecord &>(staged.record_at(loc->second.back())), name);
    }
    staged.erase_object_handles({found->handle});
    *this = std::move(staged);
    return found;
}
