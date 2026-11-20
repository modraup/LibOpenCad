// SPDX-License-Identifier: MPL-2.0
// Native DWG history root/graph/operation and missing DICTIONARY/XRECORD authoring.
namespace {
int history_class_number(const DwgR2000Objects &o, std::string_view name) {
    auto it = std::find(o.class_names.begin(), o.class_names.end(), name);
    require(it != o.class_names.end(), "Missing history class");
    return 500 + int(std::distance(o.class_names.begin(), it));
}
std::int32_t history_id_next(std::int32_t n) {
    if (n == INT32_MAX)
        throw std::overflow_error("History id exhausted");
    return n + 1;
}
void write_object_type(DwgBitWriter &main, unsigned v, int type) {
    if (v >= 1024) {
        if (type >= 0 && type <= 255) {
            main.bits(2, 0);
            main.bits(8, unsigned(type));
        } else if (type >= 496 && type < 752) {
            main.bits(2, 1);
            main.bits(8, unsigned(type - 496));
        } else {
            require(type >= 0 && type <= 32767, "Invalid object type");
            main.bits(2, 2);
            main.little_endian(2, unsigned(type));
        }
    } else
        main.bit_short(std::int16_t(type));
}
std::size_t object_prefix(DwgBitWriter &main, DwgBitWriter &refs, unsigned v, int type, std::uint64_t id,
                          std::uint64_t owner, std::uint64_t xdic,
                          const std::vector<std::uint64_t> &reactors, bool relative_owner = false) {
    write_object_type(main, v, type);
    auto split = main.position();
    if (v >= 1015 && v < 1024)
        main.little_endian(4, 0);
    main.absolute_handle(0, id);
    main.bit_short(0);
    if (relative_owner && owner)
        refs.relative_handle(id, owner);
    else
        refs.absolute_handle(4, owner);
    require(reactors.size() <= 1000000, "Invalid history reactor count");
    main.bit_long(std::int32_t(reactors.size()));
    for (auto r : reactors)
        refs.absolute_handle(4, r);
    if (v >= 1018)
        main.bits(1, !xdic);
    if (xdic || v < 1018)
        refs.absolute_handle(3, xdic);
    if (v >= 1027)
        main.bits(1, 0);
    return split;
}
std::string finish_object(DwgBitWriter &main, const DwgBitWriter &strings, const DwgBitWriter &refs,
                          unsigned v, std::size_t split) {
    if (v >= 1021) {
        if (strings.position())
            return finish_symbol(main, strings, refs, v, split);
        main.bits(1, 0);
    }
    return pack_record(v, main, split, refs);
}
void write_cm_color(DwgBitWriter &w, Color color) {
    w.bit_short(0);
    std::uint32_t packed = 0xc0000000;
    switch (color.kind()) {
    case Color::Kind::ByBlock:
        packed = 0xc1000000;
        break;
    case Color::Kind::None:
        packed = 0xc8000000;
        break;
    case Color::Kind::Index:
        packed = 0xc3000000 | (unsigned(color.index().value_or(0)) & 255);
        break;
    case Color::Kind::Rgb:
        packed = 0xc2000000 | color.to_true_color_value().value_or(0);
        break;
    default:
        break;
    }
    w.bit_long(std::int32_t(packed));
    w.bits(8, 0);
}
void write_eval(DwgBitWriter &main, DwgBitWriter &refs, DwgBitWriter &strings, unsigned v,
                const SolidHistoryNode &b, const char *page) {
    main.bit_long(b.parent_id);
    main.bit_long(b.eval_major);
    main.bit_long(b.eval_minor);
    main.bit_short(b.value_code);
    switch (b.value_code) {
    case 40:
        require(std::holds_alternative<double>(b.value), "History evaluation type mismatch");
        main.bit_double(std::get<double>(b.value));
        break;
    case 10:
    case 11: {
        require(std::holds_alternative<Vector2>(b.value), "History evaluation type mismatch");
        auto p = std::get<Vector2>(b.value);
        raw_double(main, p.x);
        raw_double(main, p.y);
        break;
    }
    case 1:
        require(std::holds_alternative<std::string>(b.value), "History evaluation type mismatch");
        symbol_string(main, strings, v, std::get<std::string>(b.value), page);
        break;
    case 90:
        require(std::holds_alternative<std::int32_t>(b.value), "History evaluation type mismatch");
        main.bit_long(std::get<std::int32_t>(b.value));
        break;
    case 91:
        require(std::holds_alternative<std::uint64_t>(b.value), "History evaluation type mismatch");
        refs.absolute_handle(5, std::get<std::uint64_t>(b.value));
        break;
    case 70:
        require(std::holds_alternative<std::int16_t>(b.value), "History evaluation type mismatch");
        main.bit_short(std::get<std::int16_t>(b.value));
        break;
    default:
        break;
    }
    main.bit_long(b.eval_node_id);
}
std::string encode_history_root(unsigned v, int type, std::uint64_t id, std::uint64_t owner,
                                std::uint64_t graph, std::int32_t active, bool record) {
    DwgBitWriter main, refs, strings;
    auto split = object_prefix(main, refs, v, type, id, owner, 0, {});
    main.bit_long(1);
    main.bit_long(0);
    refs.absolute_handle(3, graph);
    main.bit_long(active);
    main.bits(1, 0);
    main.bits(1, record);
    return finish_object(main, strings, refs, v, split);
}
std::string encode_history_graph(unsigned v, int type, std::uint64_t id, std::uint64_t owner,
                                 std::uint64_t xdic, const std::vector<std::uint64_t> &reactors,
                                 const BlockEvaluationGraph &g) {
    DwgBitWriter main, refs, strings;
    auto split = object_prefix(main, refs, v, type, id, owner, xdic, reactors);
    require(g.nodes.size() <= 1000000 && g.edges.size() <= 1000000, "Invalid history graph size");
    main.bit_long(g.first_node_id);
    main.bit_long(g.first_node_id_copy);
    main.bit_long(std::int32_t(g.nodes.size()));
    for (auto &n : g.nodes) {
        main.bit_long(n.id);
        main.bit_long(n.edge_flags);
        main.bit_long(n.next_id);
        refs.absolute_handle(3, n.expression);
        for (auto x : n.node_data)
            main.bit_long(x);
    }
    main.bit_long(std::int32_t(g.edges.size()));
    for (auto &e : g.edges) {
        main.bit_long(e.id);
        main.bit_long(e.next_id);
        main.bit_long(e.incoming_edge);
        main.bit_long(e.source_node);
        main.bit_long(e.destination_node);
        for (auto x : e.outgoing_edges)
            main.bit_long(x);
    }
    return finish_object(main, strings, refs, v, split);
}
std::string encode_history_operation(unsigned v, const char *page, int type, std::uint64_t id,
                                     std::uint64_t owner, std::uint64_t xdic, const DwgHistoryRecord &rec) {
    require(rec.operation.has_value(), "Missing history operation");
    DwgBitWriter main, refs, strings;
    auto split = object_prefix(main, refs, v, type, id, owner, xdic, rec.reactors);
    auto &b = rec.operation->base;
    write_eval(main, refs, strings, v, b, page);
    main.bit_long(b.major);
    main.bit_long(b.minor);
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            require(std::isfinite(b.transform[col * 4 + row]), "Nonfinite history value");
            main.bit_double(b.transform[col * 4 + row]);
        }
    write_cm_color(main, b.color);
    main.bit_long(b.step_id);
    refs.absolute_handle(5, b.material);
    append_bits(main, detail::encode_dwg_history_parameters(rec, static_cast<DxfVersion>(v)));
    return finish_object(main, strings, refs, v, split);
}
std::string encode_dictionary(unsigned v, const char *page, std::uint64_t id, std::uint64_t owner,
                              const std::vector<std::pair<std::string, std::uint64_t>> &entries) {
    DwgBitWriter main, refs, strings;
    auto split = object_prefix(main, refs, v, 42, id, owner, 0, {});
    require(entries.size() <= 1000000, "Invalid dictionary count");
    main.bit_long(std::int32_t(entries.size()));
    main.bit_short(1);
    main.bits(8, 0);
    for (auto &[name, target] : entries) {
        symbol_string(main, strings, v, name, page);
        refs.absolute_handle(2, target);
    }
    return finish_object(main, strings, refs, v, split);
}
unsigned xrecord_handle_code(int group) {
    if (group <= 339)
        return 4;
    if (group <= 349)
        return 5;
    if (group <= 359)
        return 2;
    return 3;
}
std::string encode_xrecord(unsigned v, std::uint64_t id, std::uint64_t owner, const std::string &data) {
    DwgBitWriter main, refs, strings;
    auto split = object_prefix(main, refs, v, 79, id, owner, 0, {});
    require(data.size() <= INT32_MAX, "Metadata too large");
    main.bit_long(std::int32_t(data.size()));
    for (unsigned char c : data)
        main.bits(8, c);
    main.bit_short(0);
    for (auto f : detail::metadata_fields(data, v >= 1021))
        if (f.code >= 330 && f.code <= 369)
            refs.absolute_handle(xrecord_handle_code(f.code),
                                 detail::metadata_uint(data, f.offset, unsigned(f.size)));
    return finish_object(main, strings, refs, v, split);
}
DwgHistoryRecord encodeable_operation(SolidHistoryOperation op, const SurfaceProfiles &profiles) {
    if (auto loft = std::get_if<HistoryLoft>(&op.parameters))
        loft->settings.reset();
    DwgHistoryRecord rec;
    if (auto sweep = std::get_if<HistorySweep>(&op.parameters)) {
        rec.sweep_vector = sweep->reference_point;
        sweep->reference_point = {};
    }
    rec.operation = std::move(op);
    rec.profiles = profiles;
    return rec;
}
bool stores_history_handle(const DwgModeler &m, unsigned v) {
    return !m.surface && v >= 1021 && (m.type == 38 || v < 1027);
}
std::uint64_t attached_history_root(const DwgR2000Objects &o, std::uint64_t entity) {
    const auto &m = o.modelers.at(entity);
    if (m.history)
        return m.history;
    if (!m.surface)
        return 0;
    if (auto meta = detail::metadata_record(o, entity, m.properties.extension_dictionary,
                                            "CADCODEC_SURFACE_HISTORY_V1")) {
        if (detail::metadata_value(meta->metadata, 90) == 1)
            return detail::metadata_reference(*meta, 330);
    }
    return 0;
}
std::set<std::uint64_t> history_graph_owned(const DwgR2000Objects &o, std::uint64_t root) {
    auto r = lifecycle_object(o, root);
    require(r && r->root.has_value(), "Existing history handle is not a root");
    std::set<std::uint64_t> owned;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> pending{{root, r->owner}};
    for (std::size_t i = 0; i < pending.size(); ++i) {
        require(i < 1000000, "Excessive owned object graph");
        auto [h, parent] = pending[i];
        auto rec = lifecycle_object(o, h);
        require(rec && rec->owner == parent, "Missing, unsupported or foreign owned object");
        if (!owned.insert(h).second)
            continue;
        if (rec->extension_dictionary)
            pending.push_back({rec->extension_dictionary, h});
        if (rec->root && rec->root->evaluation_graph) {
            auto g = lifecycle_object(o, rec->root->evaluation_graph);
            require(g && g->graph.has_value() && g->owner == h, "Foreign history graph");
            pending.push_back({g->handle, h});
        }
        for (auto &[child, node] : o.history_records)
            if (node.owner == h)
                pending.push_back({child, h});
        if (rec->graph)
            for (auto &node : rec->graph->nodes) {
                auto op = lifecycle_object(o, node.expression);
                require(op && op->operation.has_value() && op->owner == h, "Foreign history expression");
            }
        for (auto &[name, target] : rec->dictionary_entries) {
            auto child = lifecycle_object(o, target);
            require(child && child->owner == h, "Private or foreign dictionary child");
            pending.push_back({target, h});
        }
    }
    return owned;
}
void guard_removed(const LifecycleStorage &s, const DwgR2000Objects &o, const std::set<std::uint64_t> &removed,
                   LifecycleEdits &edits) {
    for (auto &[id, r] : o.records) {
        if (removed.count(id))
            continue;
        std::string encoded;
        std::string_view body;
        auto changed = edits.find(id);
        auto record = r;
        if (changed != edits.end()) {
            encoded = changed->second;
            record = read_record(encoded, 0, id, s.version);
            body = std::string_view(encoded).substr(record.body_offset, record.body_size);
        } else
            body = s.body(o, id);
        bool entity = o.modelers.count(id) || o.lines.count(id) || o.circles.count(id) || o.arcs.count(id) ||
                      o.texts.count(id);
        if (entity || lifecycle_object(o, id)) {
            auto clean = lifecycle_clean_reactors(body, record, id, s.version, entity, removed);
            if (clean) {
                encoded = *clean;
                edits[id] = encoded;
                record = read_record(encoded, 0, id, s.version);
                body = std::string_view(encoded).substr(record.body_offset, record.body_size);
            }
        }
        if (auto object = lifecycle_object(o, id); object && object->type == "XRECORD" && !edits.count(id)) {
            std::size_t reference = 0;
            for (auto f : detail::metadata_fields(object->metadata, s.version >= 1021))
                if (f.code < 0 || f.code == 480 || f.code == 481 || (f.code >= 330 && f.code <= 369) ||
                    (f.code >= 390 && f.code <= 399) || f.code == 1005) {
                    auto target = detail::metadata_uint(object->metadata, f.offset, 8);
                    if (f.code >= 330 && f.code <= 369) {
                        if (reference < object->reference_values.size())
                            target = object->reference_values[reference];
                        ++reference;
                    }
                    require(!removed.count(target), "Surviving metadata references deleted history");
                }
        }
        if (!record.main_bits) {
            require(s.version < 1015, "Missing object stream split");
            bool graphic = (record.type >= 1 && record.type <= 41) ||
                           (record.type >= 43 && record.type <= 47) || record.type == 74 ||
                           record.type == 77 || record.type == 78 || record.type == 498;
            if (record.type >= 500) {
                require(std::size_t(record.type - 500) < o.classes.size(),
                        "Cannot validate opaque legacy class references");
                auto kind = o.classes[record.type - 500].item_class_id;
                require(kind == 498 || kind == 499, "Unknown class entity flag");
                graphic = kind == 498;
            }
            DwgBitReader prefix(body);
            prefix.seek(record.handle_end_bit);
            skip_entity_prefix(prefix, body.size() * 8, graphic, s.version);
            record.main_bits = prefix.little_endian(4);
            require(record.main_bits >= prefix.position() && record.main_bits <= body.size() * 8,
                    "Invalid legacy object split");
        }
        DwgBitReader refs(body);
        refs.seek(record.main_bits);
        auto end = body.size() * 8 - (8 - record.main_bits % 8) % 8;
        while (refs.position() < end) {
            require(end - refs.position() >= 8, "Unknown surviving reference stream");
            auto ref = refs.handle(id);
            require(!removed.count(ref.value), "History is referenced by a surviving object");
        }
    }
}
struct CommonXdic {
    std::size_t no_xdic_bit{}, xdic_handle_bit{};
    bool present{};
    std::uint64_t handle{};
};
CommonXdic read_xdic(std::string_view body, const DwgR2000Record &r, std::uint64_t h, unsigned v,
                     bool entity) {
    DwgBitReader main(body), refs(body);
    main.seek(r.handle_end_bit);
    refs.seek(r.main_bits);
    skip_entity_prefix(main, r.main_bits, entity, v);
    if (entity) {
        auto mode = main.bits(2);
        if (!mode)
            refs.handle(h);
    } else
        refs.handle(h);
    auto n = main.bit_long();
    require(n >= 0 && std::size_t(n) <= (body.size() * 8 - refs.position()) / 8, "Invalid reactor count");
    for (int i = 0; i < n; ++i)
        refs.handle(h);
    CommonXdic out;
    out.no_xdic_bit = main.position();
    out.xdic_handle_bit = refs.position();
    if (v >= 1018)
        out.present = !main.bits(1);
    else
        out.present = true;
    if (out.present)
        out.handle = refs.handle(h).value;
    return out;
}
std::string with_xdic(std::string_view body, const DwgR2000Record &r, std::uint64_t h, unsigned v, bool entity,
                      std::uint64_t dict) {
    auto x = read_xdic(body, r, h, v, entity);
    if (x.present) {
        require(x.handle == dict, "Existing extension dictionary");
        return {};
    }
    DwgBitReader old(body);
    DwgBitWriter main, refs;
    copy_until(main, old, x.no_xdic_bit);
    old.bits(1);
    main.bits(1, 0);
    copy_until(main, old, r.main_bits);
    old.seek(r.main_bits);
    copy_until(refs, old, x.xdic_handle_bit);
    refs.absolute_handle(3, dict);
    copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
    return pack_record(v, main, r.split_field_bit, refs);
}
std::string with_trailing_handle(std::string_view body, const DwgR2000Record &r, std::uint64_t h, unsigned v,
                                 std::uint64_t value) {
    auto end = body.size() * 8 - (8 - r.main_bits % 8) % 8;
    DwgBitReader scan(body);
    scan.seek(r.main_bits);
    require(scan.position() < end, "Missing history handle slot");
    auto last = scan.position();
    while (scan.position() < end) {
        require(end - scan.position() >= 8, "Invalid trailing handle");
        last = scan.position();
        scan.handle(h);
    }
    DwgBitReader old(body);
    DwgBitWriter main, refs;
    copy_until(main, old, r.main_bits);
    copy_until(refs, old, last);
    refs.absolute_handle(4, value);
    return pack_record(v, main, r.split_field_bit, refs);
}
std::string with_active_id(std::string_view body, const DwgR2000Record &r, const DwgHistoryRecord &history,
                           unsigned v, std::int32_t node) {
    DwgBitReader old(body);
    DwgBitWriter main, refs;
    copy_until(main, old, history.active_begin);
    main.bit_long(node);
    old.seek(history.active_end);
    copy_until(main, old, r.main_bits);
    copy_until(refs, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
    return pack_record(v, main, r.split_field_bit, refs);
}
struct HandleSeed {
    unsigned version;
    std::string header;
    std::uint64_t maximum;
    HandleSeed(std::string_view file, const DwgR2000Objects &o, unsigned v)
        : version(v), header(lifecycle_header(file, v)), maximum(o.records.rbegin()->first) {}
    std::uint64_t next() {
        std::uint64_t id;
        header = allocate_symbol_handle(header, version, maximum, id);
        maximum = id;
        return id;
    }
};
void attach_metadata(const LifecycleStorage &s, const DwgR2000Objects &o, std::uint64_t owner,
                     std::string_view key, const std::string &data, HandleSeed &seed, LifecycleEdits &edits,
                     LifecycleEdits &added) {
    const auto *existing = detail::metadata_record(o, owner, [&] {
        if (o.modelers.count(owner))
            return o.modelers.at(owner).properties.extension_dictionary;
        auto r = lifecycle_object(o, owner);
        require(r, "Metadata owner is not a supported object");
        return r->extension_dictionary;
    }(), key);
    if (existing) {
        edits[existing->handle] = encode_xrecord(s.version, existing->handle, existing->owner, data);
        return;
    }
    bool entity = o.modelers.count(owner);
    std::uint64_t dict = 0;
    if (entity)
        dict = o.modelers.at(owner).properties.extension_dictionary;
    else if (auto r = lifecycle_object(o, owner))
        dict = r->extension_dictionary;
    std::vector<std::pair<std::string, std::uint64_t>> entries;
    if (dict) {
        auto rec = lifecycle_object(o, dict);
        require(rec && rec->type == "DICTIONARY" && rec->owner == owner,
                "Missing or foreign metadata dictionary");
        entries = rec->dictionary_entries;
    } else
        dict = seed.next();
    auto xrecord = seed.next();
    entries.emplace_back(std::string(key), xrecord);
    auto dict_bytes = encode_dictionary(s.version, s.page, dict, owner, entries);
    if (o.records.count(dict))
        edits[dict] = dict_bytes;
    else
        added[dict] = dict_bytes;
    added[xrecord] = encode_xrecord(s.version, xrecord, dict, data);
    if (!o.records.count(dict) || (entity ? !o.modelers.at(owner).properties.extension_dictionary
                                          : !lifecycle_object(o, owner)->extension_dictionary)) {
        const auto &record = o.records.at(owner);
        auto body = edits.count(owner) ? std::string_view(edits.at(owner))
                                             .substr(read_record(edits.at(owner), 0, owner, s.version).body_offset,
                                                     read_record(edits.at(owner), 0, owner, s.version).body_size)
                                       : s.body(o, owner);
        auto rec = edits.count(owner) ? read_record(edits.at(owner), 0, owner, s.version) : record;
        if (edits.count(owner))
            body = std::string_view(edits.at(owner)).substr(rec.body_offset, rec.body_size);
        auto patched = with_xdic(body, rec, owner, s.version, entity, dict);
        if (!patched.empty())
            edits[owner] = patched;
    }
}
void prepare_operation(SolidHistoryOperation &op) {
    if (op.base.step_id <= 0)
        op.base.step_id = 1;
    op.base.eval_node_id = 1;
    op.base.parent_id = -1;
    (void)history_dxf_name(op.base.kind);
}
std::vector<std::tuple<std::string, std::string, std::int16_t>> history_classes(SolidHistoryKind kind) {
    return {{"ACSH_HISTORY_CLASS", "AcDbShHistory", 499},
            {"ACAD_EVALUATION_GRAPH", "AcDbEvalGraph", 499},
            {history_dxf_name(kind), history_cpp_name(kind), 499}};
}
} // namespace
std::string detail::register_dwg_classes(
    std::string_view file, const DwgR2000Objects &o,
    const std::vector<std::tuple<std::string, std::string, std::int16_t>> &needed) {
    require(o.classes_error.empty(), "Cannot register classes in damaged CLASSES");
    auto classes = o.classes;
    bool added = false;
    for (auto &entry : needed) {
        const auto &dxf = std::get<0>(entry);
        const auto &cpp = std::get<1>(entry);
        auto item = std::get<2>(entry);
        if (std::find(o.class_names.begin(), o.class_names.end(), dxf) != o.class_names.end() ||
            std::find_if(classes.begin(), classes.end(),
                         [&](const auto &c) { return c.dxf_name == dxf; }) != classes.end())
            continue;
        require(classes.size() < 32767 - 500, "Class table exhausted");
        DwgClassRecord c;
        c.number = std::int16_t(500 + classes.size());
        for (auto &old : classes)
            require(old.number < c.number, "Nonpositional class numbering");
        c.cpp_name = cpp;
        c.dxf_name = dxf;
        c.application = "ObjectDBX Classes";
        c.item_class_id = item;
        classes.push_back(c);
        added = true;
    }
    if (!added)
        return std::string(file);
    auto replace = file.substr(0, 6) == "AC1021" ? replace_ac21_sections : replace_ac18_sections;
    return replace(file, {{"AcDb:Classes", lifecycle_classes(file, classes)}});
}
std::string detail::create_dwg_history(std::string_view file, const DwgR2000Objects &o, std::uint64_t entity,
                                       SolidHistoryOperation operation, const SurfaceProfiles &profiles) {
    auto registered = register_dwg_classes(file, o, history_classes(operation.base.kind));
    if (registered != file)
        return create_dwg_history(registered, read_dwg_objects(registered), entity, operation, profiles);
    LifecycleStorage s(file);
    require(s.version >= 1021, "History authoring requires R2007+");
    require(o.modelers.count(entity), "History requires a modeler entity");
    const auto &m = o.modelers.at(entity);
    if ((m.type == 37 || m.type == 39) && s.version >= 1027)
        throw std::invalid_argument("REGION/BODY history is not stored on R2013+");
    prepare_operation(operation);
    auto rec = encodeable_operation(operation, profiles);
    rec.handle = 1;
    rec.owner = 1;
    (void)encode_history_operation(s.version, s.page, 500, 1, 1, 0, rec);
    HandleSeed seed(file, o, s.version);
    auto root = seed.next(), graph = seed.next(), node = seed.next();
    rec.handle = node;
    rec.owner = graph;
    std::optional<HistoryLoftSettings> loft;
    if (auto p = std::get_if<HistoryLoft>(&operation.parameters); p && p->settings)
        loft = *p->settings;
    LifecycleEdits added, edits;
    std::set<std::uint64_t> removed;
    if (auto old = attached_history_root(o, entity))
        removed = history_graph_owned(o, old);
    added[root] = encode_history_root(s.version, history_class_number(o, "ACSH_HISTORY_CLASS"), root, entity,
                                      graph, operation.base.node_id(), false);
    BlockEvaluationGraph eg;
    eg.first_node_id = eg.first_node_id_copy = 1;
    BlockEvaluationNode n;
    n.id = 0;
    n.edge_flags = 32;
    n.next_id = 1;
    n.expression = node;
    n.node_data = {-1, -1, -1, -1};
    eg.nodes.push_back(n);
    added[graph] = encode_history_graph(s.version, history_class_number(o, "ACAD_EVALUATION_GRAPH"), graph,
                                        root, 0, {}, eg);
    std::uint64_t node_xdic = 0;
    if (loft) {
        auto dict = seed.next(), xr = seed.next();
        node_xdic = dict;
        added[dict] = encode_dictionary(s.version, s.page, dict, node,
                                        {{"CADCODEC_LOFT_OPTIONS_V1", xr}});
        added[xr] = encode_xrecord(s.version, xr, dict, detail::loft_metadata_bytes(*loft));
    }
    added[node] =
        encode_history_operation(s.version, s.page, history_class_number(o, history_dxf_name(operation.base.kind)),
                                 node, graph, node_xdic, rec);
    if (stores_history_handle(m, s.version))
        edits[entity] = with_trailing_handle(s.body(o, entity), o.records.at(entity), entity, s.version, root);
    else if (m.surface)
        attach_metadata(s, o, entity, "CADCODEC_SURFACE_HISTORY_V1", detail::surface_history_bytes(root), seed,
                        edits, added);
    else
        throw std::invalid_argument("History requires a 3DSOLID, REGION, BODY or surface");
    guard_removed(s, o, removed, edits);
    return lifecycle_rewrite(file, o, edits, removed, added, seed.header, seed.maximum + 1);
}
std::string detail::append_dwg_history(std::string_view file, const DwgR2000Objects &o, std::uint64_t entity,
                                       SolidHistoryOperation operation, const SurfaceProfiles &profiles) {
    auto registered = register_dwg_classes(file, o, history_classes(operation.base.kind));
    if (registered != file)
        return append_dwg_history(registered, read_dwg_objects(registered), entity, operation, profiles);
    LifecycleStorage s(file);
    require(s.version >= 1021, "History authoring requires R2007+");
    require(o.modelers.count(entity), "History requires a modeler entity");
    auto root = attached_history_root(o, entity);
    require(root, "Missing history graph");
    const auto &root_rec = o.history_records.at(root);
    require(root_rec.root.has_value(), "Modeler history handle is not a root");
    auto eval = root_rec.root->evaluation_graph;
    auto git = o.history_records.find(eval);
    require(git != o.history_records.end() && git->second.graph.has_value(),
            "Append requires an evaluation graph");
    require(git->second.owner == root, "Foreign history graph");
    auto eg = *git->second.graph;
    const DwgHistoryRecord *active = nullptr;
    for (auto &[h, rec] : o.history_records)
        if (rec.operation && rec.owner == eval &&
            rec.operation->base.node_id() == root_rec.root->history_node_id) {
            require(!active, "Ambiguous active history operation");
            active = &rec;
        }
    require(active, "Ambiguous active history operation");
    auto parent = std::find_if(eg.nodes.begin(), eg.nodes.end(),
                               [&](const auto &n) { return n.expression == active->handle; });
    require(parent != eg.nodes.end() && parent->node_data[2] < 0, "Append requires the latest history result");
    std::int32_t maxnode = 0, maxstep = 0, maxid = -1, maxedge = -1;
    for (auto &[h, rec] : o.history_records)
        if (rec.operation && rec.owner == eval) {
            maxnode = std::max(maxnode, rec.operation->base.node_id());
            maxstep = std::max({maxstep, rec.operation->base.step_id, rec.operation->base.eval_node_id});
        }
    for (auto &n : eg.nodes)
        maxid = std::max(maxid, n.id);
    for (auto &e : eg.edges)
        maxedge = std::max(maxedge, e.id);
    operation.base.eval_node_id = history_id_next(maxnode);
    operation.base.step_id = history_id_next(maxstep);
    operation.base.parent_id = -1;
    auto id = history_id_next(maxid), edge = history_id_next(maxedge), next = history_id_next(id);
    auto source = parent->id;
    parent->node_data[2] = parent->node_data[3] = edge;
    auto rec = encodeable_operation(operation, profiles);
    rec.handle = 1;
    rec.owner = eval;
    (void)encode_history_operation(s.version, s.page, 500, 1, 1, 0, rec);
    HandleSeed seed(file, o, s.version);
    auto node = seed.next();
    rec.handle = node;
    BlockEvaluationNode n;
    n.id = id;
    n.edge_flags = 32;
    n.next_id = next;
    n.expression = node;
    n.node_data = {edge, edge, -1, -1};
    eg.nodes.push_back(n);
    BlockEvaluationEdge e;
    e.id = edge;
    e.next_id = 0;
    e.incoming_edge = 1;
    e.source_node = source;
    e.destination_node = id;
    e.outgoing_edges = {-1, -1, -1, -1, -1};
    eg.edges.push_back(e);
    eg.first_node_id = eg.first_node_id_copy = next;
    LifecycleEdits added, edits;
    std::uint64_t node_xdic = 0;
    if (auto p = std::get_if<HistoryLoft>(&operation.parameters); p && p->settings) {
        auto dict = seed.next(), xr = seed.next();
        node_xdic = dict;
        added[dict] = encode_dictionary(s.version, s.page, dict, node,
                                        {{"CADCODEC_LOFT_OPTIONS_V1", xr}});
        added[xr] = encode_xrecord(s.version, xr, dict, detail::loft_metadata_bytes(*p->settings));
    }
    added[node] = encode_history_operation(s.version, s.page,
                                           history_class_number(o, history_dxf_name(operation.base.kind)),
                                           node, eval, node_xdic, rec);
    edits[eval] = encode_history_graph(s.version, o.records.at(eval).type, eval, root,
                                       git->second.extension_dictionary, git->second.reactors, eg);
    edits[root] = with_active_id(s.body(o, root), o.records.at(root), root_rec, s.version,
                                 operation.base.node_id());
    guard_removed(s, o, {}, edits);
    return lifecycle_rewrite(file, o, edits, {}, added, seed.header, seed.maximum + 1);
}
std::string detail::ensure_dwg_metadata(std::string_view file, const DwgR2000Objects &o, std::uint64_t owner,
                                        std::string_view key, const std::string &data) {
    LifecycleStorage s(file);
    require(s.version >= 1018, "Metadata authoring requires R2004+");
    (void)detail::metadata_fields(data, s.version >= 1021);
    HandleSeed seed(file, o, s.version);
    LifecycleEdits added, edits;
    attach_metadata(s, o, owner, key, data, seed, edits, added);
    if (added.empty() && edits.empty())
        return std::string(file);
    guard_removed(s, o, {}, edits);
    return lifecycle_rewrite(file, o, edits, {}, added, added.empty() ? std::optional<std::string>{} : seed.header,
                             added.empty() ? 0 : seed.maximum + 1);
}
