// SPDX-License-Identifier: MPL-2.0
// Native history edits and synchronization with owned versioned XRECORD metadata.
void DwgDocument::set_history_active_node(std::uint64_t h, std::int32_t node) {
    const auto &r = history_record(h);
    if (!r.root)
        throw std::invalid_argument("Not a history root");
    const auto &g = history_record(r.root->evaluation_graph);
    if (!g.graph || g.owner != h)
        throw std::invalid_argument("Foreign history graph");
    std::size_t matches = 0;
    // History IDs belong to operations; evaluation-graph node IDs are independent.
    for (const auto &item : objects_.history_records) {
        const auto &op = item.second;
        if (op.operation && op.owner == g.handle && op.operation->base.node_id() == node)
            ++matches;
    }
    if (matches != 1)
        throw std::invalid_argument("Missing or ambiguous active history node");
    DwgDocument staged(
        detail::edit_dwg_history_slot(source_bytes(), objects_, h, r.active_begin, r.active_end, node, false));
    *this = std::move(staged);
}
void DwgDocument::set_history_evaluation_graph(std::uint64_t root, std::uint64_t graph) {
    const auto &r = history_record(root);
    const auto &g = history_record(graph);
    if (!r.root || !g.graph || g.owner != root)
        throw std::invalid_argument("Missing or foreign evaluation graph");
    std::size_t matches = 0;
    for (const auto &item : objects_.history_records) {
        const auto &op = item.second;
        if (op.operation && op.owner == graph && op.operation->base.node_id() == r.root->history_node_id)
            ++matches;
    }
    if (matches != 1)
        throw std::invalid_argument("Active operation missing or ambiguous in target graph");
    DwgDocument staged(detail::edit_dwg_history_slot(source_bytes(), objects_, root, r.graph_reference_begin,
                                                     r.graph_reference_end, graph, true, 3));
    *this = std::move(staged);
}
void DwgDocument::set_history_node_expression(std::uint64_t graph, std::int32_t node,
                                              std::uint64_t expression) {
    const auto &g = history_record(graph);
    const auto &op = history_record(expression);
    if (!g.graph || !op.operation || op.owner != graph)
        throw std::invalid_argument("Missing or foreign graph expression");
    for (std::size_t i = 0; i < g.graph->nodes.size(); ++i)
        if (g.graph->nodes[i].id == node) {
            auto slot = g.reference_slots.at(i);
            DwgDocument staged(detail::edit_dwg_history_slot(source_bytes(), objects_, graph, slot.first, slot.second,
                                                             expression, true, 3));
            *this = std::move(staged);
            return;
        }
    throw std::invalid_argument("Unknown graph node");
}
void DwgDocument::set_history_material(std::uint64_t h, std::uint64_t material) {
    const auto &r = history_record(h);
    if (!r.operation)
        throw std::invalid_argument("Not a history operation");
    if (material) {
        auto t = objects_.records.at(material).type;
        if (t < 500 || std::size_t(t - 500) >= objects_.class_names.size() ||
            objects_.class_names[t - 500] != "MATERIAL")
            throw std::invalid_argument("Not a material");
    }
    DwgDocument staged(
        detail::edit_dwg_history_slot(source_bytes(), objects_, h, r.material_begin, r.material_end, material, true));
    *this = std::move(staged);
}
void DwgDocument::set_surface_history(std::uint64_t h, std::uint64_t root) {
    (void)surface(h);
    if (root) {
        const auto &r = history_record(root);
        if (!r.root || r.owner != h)
            throw std::invalid_argument("Foreign surface history");
    }
    auto r = detail::metadata_record(objects_, h, modeler(h).properties.extension_dictionary,
                                     "CADCODEC_SURFACE_HISTORY_V1");
    if (!r) {
        if (!root)
            return;
        DwgDocument staged(detail::ensure_dwg_metadata(source_bytes(), objects_, h, "CADCODEC_SURFACE_HISTORY_V1",
                                                       detail::surface_history_bytes(root)));
        auto graph = staged.modeler_history_graph(h);
        if (!graph || graph->root != root)
            throw std::invalid_argument("Surface history attachment failed");
        *this = std::move(staged);
        return;
    }
    if (r->reference_slots.size() != 1 || detail::metadata_value(r->metadata, 90) != 1)
        throw std::invalid_argument("Unsupported surface history metadata");
    auto data = r->metadata;
    detail::metadata_put(data, 330, root, 8);
    auto id = r->handle;
    DwgDocument staged(detail::edit_dwg_metadata(source_bytes(), objects_, id, data));
    auto slot = staged.objects_.metadata_records.at(id).reference_slots.at(0);
    staged = DwgDocument(detail::edit_dwg_history_slot(staged.source_bytes(), staged.objects_, id, slot.first,
                                                       slot.second, root, true, 4));
    *this = std::move(staged);
}
HistoryLoftSettings DwgDocument::history_loft_settings(std::uint64_t h) const {
    const auto &o = history_record(h);
    if (!o.operation || o.operation->base.kind != SolidHistoryKind::Loft)
        throw std::invalid_argument("Not loft history");
    auto r = detail::metadata_record(objects_, h, o.extension_dictionary, "CADCODEC_LOFT_OPTIONS_V1");
    if (!r || detail::metadata_value(r->metadata, 90) != 1)
        throw std::invalid_argument("Missing or unknown loft metadata");
    auto &s = r->metadata;
    HistoryLoftSettings v;
    auto small = [&](int code, int def) {
        auto x = detail::metadata_has(s, code) ? detail::metadata_value(s, code) : std::uint64_t(def);
        if (x > 32767)
            throw std::invalid_argument("Invalid loft enumeration");
        return std::int16_t(x);
    };
    auto real = [&](int code, double def) {
        return detail::metadata_has(s, code) ? detail::metadata_double(s, code) : def;
    };
    v.normals = small(70, -1);
    v.start_continuity = small(71, 1);
    v.end_continuity = small(72, 1);
    v.start_draft_angle = detail::metadata_double(s, 40);
    v.end_draft_angle = detail::metadata_double(s, 41);
    v.start_magnitude = detail::metadata_double(s, 42);
    v.end_magnitude = detail::metadata_double(s, 43);
    v.start_bulge = real(44, .5);
    v.end_bulge = real(45, .5);
    v.closed = detail::metadata_value(s, 290) != 0;
    v.surface = detail::metadata_value(s, 291) != 0;
    v.align_direction = detail::metadata_value(s, 292) != 0;
    v.periodic = detail::metadata_has(s, 293) ? detail::metadata_value(s, 293) != 0 : true;
    for (auto &f : detail::metadata_profile_fields(s)) {
        if (f.code == 91)
            v.section_counts.push_back(std::stoi(f.value));
        else if (f.code == 92 || f.code == 93 || f.code == 310)
            v.path.push_back(f);
    }
    detail::validate_loft_metadata(v);
    if (!v.path.empty()) {
        SolidHistoryOperation probe(SolidHistoryKind::Loft);
        std::get<HistoryLoft>(probe.parameters).settings = v;
        (void)read_history_profiles(probe, DxfVersion::AC1032);
    }
    return v;
}
void DwgDocument::set_history_loft_settings(std::uint64_t h, const HistoryLoftSettings &v) {
    const auto &operation = history_record(h);
    if (!operation.operation || operation.operation->base.kind != SolidHistoryKind::Loft)
        throw std::invalid_argument("Not loft history");
    detail::validate_loft_metadata(v);
    if (!detail::metadata_record(objects_, h, operation.extension_dictionary, "CADCODEC_LOFT_OPTIONS_V1")) {
        DwgDocument staged(detail::ensure_dwg_metadata(source_bytes(), objects_, h, "CADCODEC_LOFT_OPTIONS_V1",
                                                       detail::loft_metadata_bytes(v)));
        *this = std::move(staged);
    }
    (void)history_loft_settings(h);
    auto record = history_record(h);
    auto r = detail::metadata_record(objects_, h, record.extension_dictionary, "CADCODEC_LOFT_OPTIONS_V1");
    auto raw = r->metadata;
    auto id = r->handle;
    detail::metadata_set(raw, 70, v.normals, 2);
    detail::metadata_set(raw, 71, v.start_continuity, 2);
    detail::metadata_set(raw, 72, v.end_continuity, 2);
    detail::metadata_set_double(raw, 40, v.start_draft_angle);
    detail::metadata_set_double(raw, 41, v.end_draft_angle);
    detail::metadata_set_double(raw, 42, v.start_magnitude);
    detail::metadata_set_double(raw, 43, v.end_magnitude);
    detail::metadata_set_double(raw, 44, v.start_bulge);
    detail::metadata_set_double(raw, 45, v.end_bulge);
    detail::metadata_set(raw, 290, v.closed, 1);
    detail::metadata_set(raw, 291, v.surface, 1);
    detail::metadata_set(raw, 292, v.align_direction, 1);
    detail::metadata_set(raw, 293, v.periodic, 1);
    std::string filtered;
    for (auto f : detail::metadata_fields(raw))
        if (f.code != 91 && f.code != 92 && f.code != 93 && f.code != 310)
            filtered += raw.substr(f.offset - 2, f.size + 2);
    for (auto n : v.section_counts)
        detail::metadata_append(filtered, 91, n, 4);
    if (!v.path.empty()) {
        SolidHistoryOperation probe(SolidHistoryKind::Loft);
        std::get<HistoryLoft>(probe.parameters).settings = v;
        auto path = read_history_profiles(probe, DxfVersion::AC1032).metadata_path;
        if (!path)
            throw std::invalid_argument("Missing loft metadata path");
        auto version = dxf_version_from_string(source_bytes().substr(0, 6));
        // Validation and transcode occur before either object is modified.
        auto encoded = encode_embedded_profile(*path, version);
        (void)encoded;
        record.profiles.path = path;
        filtered += detail::metadata_profile_bytes(v.path);
    } else
        record.profiles.path.reset();
    auto &native = std::get<HistoryLoft>(record.operation->parameters);
    native.start_draft_angle = v.start_draft_angle;
    native.end_draft_angle = v.end_draft_angle;
    native.start_magnitude = v.start_magnitude;
    native.end_magnitude = v.end_magnitude;
    DwgDocument staged(detail::edit_dwg_history_parameters(source_bytes(), objects_, h, record));
    staged = DwgDocument(detail::edit_dwg_metadata(staged.source_bytes(), staged.objects_, id, filtered));
    (void)staged.history_loft_settings(h);
    *this = std::move(staged);
}
void DwgDocument::update_history_inputs(std::uint64_t h, const SolidHistoryParameters &p,
                                        const SurfaceProfiles &profiles) {
    auto record = history_record(h);
    std::optional<HistoryLoftSettings> settings;
    if (record.operation && record.operation->base.kind == SolidHistoryKind::Loft &&
        record.extension_dictionary) {
        if (detail::metadata_record(objects_, h, record.extension_dictionary, "CADCODEC_LOFT_OPTIONS_V1"))
            settings = history_loft_settings(h);
    }
    if (!record.operation)
        throw std::invalid_argument("History record is not an operation");
    if (record.operation->parameters.index() != p.index())
        throw std::invalid_argument("History parameter kind mismatch");
    record.operation->parameters = p;
    record.profiles = profiles;
    DwgDocument staged(detail::edit_dwg_history_parameters(source_bytes(), objects_, h, record));
    if (settings) {
        const auto &loft = std::get<HistoryLoft>(p);
        settings->start_draft_angle = loft.start_draft_angle;
        settings->end_draft_angle = loft.end_draft_angle;
        settings->start_magnitude = loft.start_magnitude;
        settings->end_magnitude = loft.end_magnitude;
        settings->path.clear();
        if (profiles.path) {
            SolidHistoryOperation probe(SolidHistoryKind::Loft);
            HistoryProfileGeometry geometry;
            geometry.metadata_path = profiles.path;
            set_history_profiles(probe, geometry, dxf_version_from_string(source_bytes().substr(0, 6)));
            settings->path = std::get<HistoryLoft>(probe.parameters).settings->path;
        }
        staged.set_history_loft_settings(h, *settings);
    }
    (void)staged.history_record(h);
    *this = std::move(staged);
}
const DwgHistoryRecord &DwgDocument::history_record(std::uint64_t h) const {
    if (auto e = objects_.history_errors.find(h); e != objects_.history_errors.end())
        throw std::invalid_argument("DWG history " + hex(h) + ": " + e->second);
    return objects_.history_records.at(h);
}
SolidHistoryGraph DwgDocument::create_solid_history(std::uint64_t entity, SolidHistoryOperation operation,
                                                    SurfaceProfiles profiles) {
    (void)modeler(entity);
    DwgDocument staged(detail::create_dwg_history(source_bytes(), objects_, entity, std::move(operation), profiles));
    auto graph = staged.modeler_history_graph(entity);
    if (!graph)
        throw std::invalid_argument("History attachment failed");
    *this = std::move(staged);
    return *graph;
}
SolidHistoryGraph DwgDocument::append_solid_history(std::uint64_t entity, SolidHistoryOperation operation,
                                                    SurfaceProfiles profiles) {
    (void)modeler(entity);
    DwgDocument staged(detail::append_dwg_history(source_bytes(), objects_, entity, std::move(operation), profiles));
    auto graph = staged.modeler_history_graph(entity);
    if (!graph || !staged.history_record(*graph->evaluation_graph).graph)
        throw std::invalid_argument("Invalid appended history");
    *this = std::move(staged);
    return *graph;
}
std::optional<SolidHistoryGraph> DwgDocument::modeler_history_graph(std::uint64_t h) const {
    auto root = modeler(h).history;
    if (!root && modeler(h).surface) {
        if (auto r = detail::metadata_record(objects_, h, modeler(h).properties.extension_dictionary,
                                             "CADCODEC_SURFACE_HISTORY_V1")) {
            if (detail::metadata_value(r->metadata, 90) == 1) {
                auto target = detail::metadata_reference(*r, 330);
                auto it = objects_.history_records.find(target);
                if (it != objects_.history_records.end() && it->second.root && it->second.owner == h)
                    root = target;
            }
        }
    }
    if (!root)
        return {};
    const auto &record = history_record(root);
    if (!record.root)
        throw std::invalid_argument("Modeler history handle is not a root");
    SolidHistoryGraph graph;
    graph.root = root;
    auto eval = record.root->evaluation_graph;
    if (objects_.history_errors.count(eval))
        (void)history_record(eval);
    if (auto i = objects_.history_records.find(eval); i != objects_.history_records.end() && i->second.graph)
        graph.evaluation_graph = eval;
    if (graph.evaluation_graph)
        for (const auto &node : history_record(*graph.evaluation_graph).graph->nodes)
            if (objects_.history_errors.count(node.expression))
                (void)history_record(node.expression);
    for (auto &[id, r] : objects_.history_records) {
        if (!r.operation)
            continue;
        auto owner = r.owner;
        for (int i = 0; i < 16 && owner; ++i) {
            if (owner == root) {
                graph.nodes.push_back(id);
                break;
            }
            auto it = objects_.history_records.find(owner);
            if (it == objects_.history_records.end())
                break;
            owner = it->second.owner;
        }
    }
    std::sort(graph.nodes.begin(), graph.nodes.end(), [&](auto a, auto b) {
        const auto &x = *history_record(a).operation;
        const auto &y = *history_record(b).operation;
        return std::make_pair(x.base.step_id, a) < std::make_pair(y.base.step_id, b);
    });
    return graph;
}
