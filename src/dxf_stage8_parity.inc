// SPDX-License-Identifier: MPL-2.0
// rust CadDocument::{rename_layer,mirror_entity,try_reserve_handles,
// snapshot_structure,swap_structure,export_block_graph,import_block_graph}.
namespace {
bool valid_mirror_transform(const Transform &t) {
    if (t.is_identity())
        return false;
    for (const auto &row : t.matrix.m)
        for (double v : row)
            if (!std::isfinite(v))
                return false;
    return true;
}

template <class T, class = void> struct has_mirror_options : std::false_type {};
template <class T>
struct has_mirror_options<
    T, std::void_t<decltype(std::declval<T &>().apply_mirror(std::declval<const Transform &>(),
                                                             std::declval<const MirrorOptions &>()))>>
    : std::true_type {};

template <class T, class = void> struct has_mirror : std::false_type {};
template <class T>
struct has_mirror<T, std::void_t<decltype(std::declval<T &>().apply_mirror(
                         std::declval<const Transform &>()))>> : std::true_type {};

bool block_graph_movable(std::string_view type) {
    return type == "DICTIONARY" || type == "ACDBDICTIONARYWDFLT" || type == "XRECORD" ||
           type == "BLOCKVISIBILITYPARAMETER" || is_dynamic_block_object_name(type);
}

HandleReservation plan_handle_range(std::uint64_t next, std::uint64_t seed, std::uint64_t protected_max,
                                    std::size_t count) {
    if (protected_max == std::numeric_limits<std::uint64_t>::max())
        throw HandleReservationError::exhausted();
    const auto protected_end = protected_max + 1;
    auto start = std::max<std::uint64_t>({1, next, seed, protected_end});
    std::uint64_t width = 0;
    if (count > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()))
        throw HandleReservationError::exhausted();
    width = static_cast<std::uint64_t>(count);
    if (start > std::numeric_limits<std::uint64_t>::max() - width)
        throw HandleReservationError::exhausted();
    return HandleReservation(start, start + width, count);
}
} // namespace

std::uint64_t DxfDocument::rename_layer(std::string_view old_name, std::string new_name) {
    return rename_symbol(SymbolTable::Layer, old_name, std::move(new_name));
}

bool DxfDocument::mirror_entity(std::uint64_t handle, Vector3 first, Vector3 second) {
    MirrorOptions options;
    options.mirror_text = header_int16("$MIRRTEXT", 0) != 0;
    return mirror_entity_with_options(handle, first, second, options);
}

bool DxfDocument::mirror_entity_with_options(std::uint64_t handle, Vector3 first, Vector3 second,
                                             MirrorOptions options) {
    return mirror_entity_with_transform(handle, Transform::from_mirror_line(first, second), options);
}

bool DxfDocument::mirror3d_entity(std::uint64_t handle, MirrorPlane plane) {
    MirrorOptions options;
    options.mirror_text = header_int16("$MIRRTEXT", 0) != 0;
    return mirror3d_entity_with_options(handle, plane, options);
}

bool DxfDocument::mirror3d_entity_with_options(std::uint64_t handle, MirrorPlane plane,
                                               MirrorOptions options) {
    return mirror_entity_with_transform(handle, plane.transform(), options);
}

bool DxfDocument::mirror_entity_with_transform(std::uint64_t handle, const Transform &transform,
                                               MirrorOptions options) {
    if (!valid_mirror_transform(transform))
        return false;
    const auto found = handle_index_.find(handle);
    if (!handle || found == handle_index_.end() || found->second.size() != 1)
        return false;
    const auto location = found->second.front();
    if (location.vertex || location.seqend)
        return false;
    DxfDocument staged = *this;
    bool applied = false;
    try {
        std::visit(
            [&](auto &item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, DxfBlock> || std::is_same_v<T, DxfProxyEntity> ||
                              std::is_same_v<T, DxfRecord>)
                    return;
                else if constexpr (has_mirror_options<T>::value) {
                    item.apply_mirror(transform, options);
                    applied = true;
                } else if constexpr (has_mirror<T>::value) {
                    item.apply_mirror(transform);
                    applied = true;
                }
            },
            staged.records_.at(location.record));
    } catch (const std::exception &) {
        return false;
    }
    if (!applied)
        return false;
    *this = std::move(staged);
    return true;
}

HandleReservation DxfDocument::try_reserve_handles(std::size_t count) {
    if (count == 0)
        return {};
    if (!duplicate_handles().empty())
        throw HandleReservationError::inconsistent("duplicate_handles", duplicate_handles().front());
    std::uint64_t protected_max = 0;
    auto consider = [&](const DxfRecord &record, bool skip_handseed) {
        std::string variable;
        for (const auto &p : record.pairs_) {
            if (p.code == 9)
                variable = std::string(trim(p.value));
            if ((p.code == 5 || p.code == 105) && skip_handseed && variable == "$HANDSEED")
                continue;
            if (p.code == 5 || p.code == 105 || pointer_code(p.code)) {
                if (!p.value.empty() && !parse_handle(p.value) && p.code != 5 && p.code != 105)
                    throw HandleReservationError::uninspectable(record.type());
                protected_max = std::max(protected_max, handle_value(p.value));
            }
        }
    };
    std::string section;
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto &item = record_at({i, std::nullopt, false});
        const auto type = item.type();
        if (type == "SECTION") {
            section = trim(item.field("", 2));
            continue;
        }
        if (type == "ENDSEC") {
            section.clear();
            continue;
        }
        if ((section == "ENTITIES" || section == "BLOCKS") && std::holds_alternative<DxfRecord>(records_[i]) &&
            type != "BLOCK" && type != "ENDBLK" && type != "VERTEX" && type != "SEQEND" && type != "ATTRIB")
            throw HandleReservationError::uninspectable(std::string(type));
        consider(item, header_index_ && i == *header_index_);
        if (const auto *path = sequence(records_[i])) {
            for (const auto &child : path->children_)
                consider(child, false);
            if (path->seqend_)
                consider(*path->seqend_, false);
        }
    }
    std::uint64_t seed = next_handle_;
    if (header_index_) {
        const auto &header = record_at({*header_index_, std::nullopt, false});
        std::string variable;
        for (const auto &p : header.pairs_) {
            if (p.code == 9)
                variable = std::string(trim(p.value));
            else if (p.code == 5 && variable == "$HANDSEED")
                seed = std::max(seed, handle_value(p.value));
        }
    }
    auto reservation = plan_handle_range(next_handle_, seed, protected_max, count);
    next_handle_ = reservation.end();
    handle_reserved_ = true;
    return reservation;
}

std::vector<DxfDocument::Record> DxfDocument::extract_graphical_entities() {
    std::vector<Record> live;
    auto indices = entity_records_;
    std::sort(indices.begin(), indices.end());
    live.reserve(indices.size());
    for (auto i : indices)
        live.push_back(records_[i]);
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(*it));
    rebuild_indexes();
    return live;
}

void DxfDocument::insert_graphical_entities(std::vector<Record> entities) {
    for (auto &entry : entities) {
        const auto &as_record = std::visit([](const auto &v) -> const DxfRecord & { return v; }, entry);
        const auto owner = handle_value(as_record.owner_handle());
        const auto *block = owner ? block_record(hex_handle(owner)) : nullptr;
        const bool space = !block || block->is_model_space() || block->is_paper_space();
        std::optional<std::size_t> at;
        std::string section, current_block;
        for (std::size_t i = 0; i < records_.size(); ++i) {
            const auto &r = record_at({i, std::nullopt, false});
            if (r.type() == "SECTION")
                section = trim(r.field("", 2));
            else if (r.type() == "ENDSEC") {
                if (space && section == "ENTITIES")
                    at = i;
                section.clear();
            } else if (r.type() == "BLOCK")
                current_block = r.text_field("AcDbBlockBegin", 2);
            else if (!space && r.type() == "ENDBLK" && section == "BLOCKS" && block &&
                     detail::symbol_name_key(current_block) == detail::symbol_name_key(block->name())) {
                at = i;
                break;
            }
        }
        if (!at)
            throw std::invalid_argument("Missing destination section for structure swap");
        records_.insert(records_.begin() + static_cast<std::ptrdiff_t>(*at), std::move(entry));
    }
    rebuild_indexes();
}

DxfDocument DxfDocument::snapshot_structure() {
    DxfDocument snap = *this;
    snap.extract_graphical_entities();
    return snap;
}

DxfDocument DxfDocument::swap_structure(DxfDocument snapshot) {
    auto live = extract_graphical_entities();
    using std::swap;
    swap(*this, snapshot);
    insert_graphical_entities(std::move(live));
    return snapshot;
}

bool DxfDocument::owner_chain_reaches(std::uint64_t handle, std::uint64_t target) const {
    const auto chain = ownership_chain(hex_handle(handle));
    for (const auto *record : chain.records)
        if (handle_value(record->handle()) == target)
            return true;
    return false;
}

void DxfDocument::set_xdictionary_handle(DxfRecord &host, std::uint64_t dictionary) {
    unsigned depth = 0;
    for (std::size_t i = 1; i < host.pairs_.size(); ++i) {
        const auto &p = host.pairs_[i];
        if (!depth && p.code == 102 && p.value == "{ACAD_XDICTIONARY") {
            if (i + 1 < host.pairs_.size() && host.pairs_[i + 1].code == 360) {
                host.pairs_[i + 1].value = hex_handle(dictionary);
                return;
            }
        }
        if (p.code != 102)
            continue;
        if (!p.value.empty() && p.value.front() == '{')
            ++depth;
        else if (depth && p.value == "}")
            --depth;
    }
    std::size_t insertion = host.pairs_.size();
    depth = 0;
    for (std::size_t i = 1; i < host.pairs_.size(); ++i) {
        const auto &p = host.pairs_[i];
        if (!depth && (p.code == 100 || p.code == 1001 || p.code == 101)) {
            insertion = i;
            break;
        }
        if (p.code != 102)
            continue;
        if (!p.value.empty() && p.value.front() == '{')
            ++depth;
        else if (p.value == "}") {
            if (depth)
                --depth;
        }
    }
    host.pairs_.insert(host.pairs_.begin() + static_cast<std::ptrdiff_t>(insertion),
                       {{102, "{ACAD_XDICTIONARY"}, {360, hex_handle(dictionary)}, {102, "}"}});
}

std::optional<BlockGraph> DxfDocument::export_block_graph(std::uint64_t record) const {
    if (!record || records_with_handle(hex_handle(record)).size() != 1)
        return std::nullopt;
    const auto *block = get_record(hex_handle(record));
    if (!block || block->type() != "BLOCK_RECORD")
        return std::nullopt;
    std::optional<std::uint64_t> root;
    try {
        auto xdict = extension_dictionary_handle(hex_handle(record));
        if (!xdict.empty())
            root = handle_value(xdict);
    } catch (const std::invalid_argument &) {
        return std::nullopt;
    }
    BlockGraph graph;
    graph.record = record;
    graph.root = root;
    std::unordered_set<std::uint64_t> seen;
    std::vector<std::uint64_t> pending;
    if (root)
        pending.push_back(*root);
    while (!pending.empty()) {
        auto handle = pending.back();
        pending.pop_back();
        const bool owned = (root && handle == *root) || (root && owner_chain_reaches(handle, *root));
        if (!seen.insert(handle).second || !owned)
            continue;
        const auto found = handle_index_.find(handle);
        if (found == handle_index_.end() || found->second.size() != 1)
            continue;
        const auto &object = record_at(found->second.front());
        if (!block_graph_movable(object.type()))
            return std::nullopt;
        for (const auto &p : object.pairs_)
            if (pointer_code(p.code)) {
                auto child = handle_value(p.value);
                if (child)
                    pending.push_back(child);
            }
        graph.objects.push_back(object);
        if (object.type() == "BLOCKVISIBILITYPARAMETER")
            if (auto vis = block_visibility_parameter(record))
                graph.visibility.push_back(*vis);
        auto xdata = object.extended_data();
        if (!xdata.empty())
            graph.xdata.emplace_back(handle, std::move(xdata));
        for (const auto &cls : classes())
            if (cls.name == object.type() &&
                (is_dynamic_block_object_name(object.type()) || object.type() == "BLOCKVISIBILITYPARAMETER"))
                graph.classes.push_back(cls);
    }
    auto record_xdata = block->extended_data();
    if (!record_xdata.empty())
        graph.xdata.emplace_back(record, std::move(record_xdata));
    if (graph.objects.empty() && graph.xdata.empty())
        return std::nullopt;
    return graph;
}

void DxfDocument::import_block_graph(const BlockGraph &graph, std::uint64_t record,
                                     const std::unordered_map<std::uint64_t, std::uint64_t> &map) {
    if (!record || records_with_handle(hex_handle(record)).size() != 1)
        throw std::invalid_argument("Block graph import target must resolve uniquely");
    DxfDocument staged = *this;
    std::unordered_map<std::uint64_t, std::uint64_t> remap = map;
    remap[graph.record] = record;
    for (const auto &object : graph.objects) {
        auto old = handle_value(object.handle());
        if (!old)
            throw std::invalid_argument("Block graph object is missing a handle");
        remap[old] = staged.allocate_handle();
    }
    auto rewrite_pairs = [&](DxfRecord &object) {
        for (auto &p : object.pairs_) {
            if (!pointer_code(p.code))
                continue;
            auto it = remap.find(handle_value(p.value));
            if (it != remap.end())
                p.value = hex_handle(it->second);
        }
    };
    for (const auto &source : graph.objects) {
        auto old = handle_value(source.handle());
        auto fresh = remap.at(old);
        auto saved = staged.next_handle_;
        staged.next_handle_ = fresh;
        DxfRecord object = source;
        rewrite_pairs(object);
        object.set_field("", 5, hex_handle(fresh));
        staged.insert_object(std::move(object));
        staged.next_handle_ = std::max(saved, staged.next_handle_);
        auto got = handle_value(staged.get_record(hex_handle(fresh))
                                    ? staged.get_record(hex_handle(fresh))->handle()
                                    : "");
        if (got != fresh) {
            auto &inserted = staged.mutable_object(got);
            inserted.set_field("", 5, hex_handle(fresh));
            staged.rebuild_indexes();
        }
    }
    for (const auto &[handle, data] : graph.xdata) {
        auto dest = remap.count(handle) ? remap.at(handle) : handle;
        for (auto record_data : data) {
            for (auto &value : record_data.values) {
                if (value.code != 1005)
                    continue;
                if (auto *h = std::get_if<std::uint64_t>(&value.value)) {
                    auto it = remap.find(*h);
                    if (it != remap.end())
                        *h = it->second;
                }
            }
            staged.set_extended_data(hex_handle(dest), record_data);
        }
    }
    for (const auto &cls : graph.classes)
        staged.set_class(cls);
    if (graph.root) {
        auto root = remap.at(*graph.root);
        auto &host = staged.mutable_object(record);
        staged.set_xdictionary_handle(host, root);
        auto &dict = staged.mutable_object(root);
        dict.set_field("", 330, hex_handle(record));
        staged.rebuild_indexes();
    }
    *this = std::move(staged);
}
