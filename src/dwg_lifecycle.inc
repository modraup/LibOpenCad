// SPDX-License-Identifier: MPL-2.0
// Stored object lifecycle, following acadrust document ownership and DWG object/handle writers.
namespace {
std::string lifecycle_classes(std::string_view file, const std::vector<DwgClassRecord> &classes) {
    auto v = unsigned(dxf_version_from_string(file.substr(0, 6)));
    require(v >= 1018, "Surface classes require R2004+");
    DwgBitWriter main, strings;
    if (v >= 1021)
        main.little_endian(4, 0);
    std::int16_t maximum = 0;
    for (auto &c : classes)
        maximum = std::max(maximum, c.number);
    main.bit_short(maximum);
    main.bits(16, 0);
    main.bits(1, 1);
    for (auto &c : classes) {
        main.bit_short(c.number);
        main.bit_short(c.proxy_flags);
        for (auto &text : {c.application, c.cpp_name, c.dxf_name})
            symbol_string(main, strings, v, text, drawing_code_page(file, v));
        main.bits(1, c.zombie);
        main.bit_short(c.item_class_id);
        for (auto n : c.fields)
            main.bit_long(n);
    }
    if (v >= 1021) {
        append_bits(main, strings);
        auto bits = strings.position();
        if (bits >= 32768)
            main.little_endian(2, bits >> 15);
        main.little_endian(2, bits >= 32768 ? (bits & 32767) | 32768 : bits);
        main.bits(1, 1);
    }
    auto data = main.bytes();
    if (v >= 1021)
        set_integer(data, 0, main.position(), 4);
    const unsigned char marker[] = {0x8d, 0xa1, 0xc4, 0xb8, 0xc4, 0xa9, 0xf8, 0xc5,
                                    0xc0, 0xdc, 0xf4, 0x5f, 0xe7, 0xcf, 0xb6, 0x8a};
    std::string result(reinterpret_cast<const char *>(marker), 16);
    result.resize(20, 0);
    set_integer(result, 16, data.size(), 4);
    if ((v >= 1024 && static_cast<unsigned char>(file[11]) > 3) || v >= 1032)
        result.append(4, 0);
    result += data;
    result.resize(result.size() + 2);
    set_crc(result, 16, result.size() - 2);
    for (auto c : marker)
        result += char(c ^ 255);
    result.append(8, 0);
    return result;
}
struct LifecycleStorage {
    unsigned version;
    std::string section;
    std::string_view bytes;
    const char *page;
    LifecycleStorage(std::string_view file, std::optional<std::string> object_section = std::nullopt)
        : version(unsigned(dxf_version_from_string(file.substr(0, 6)))) {
        require(version >= 1012, "Modeler lifecycle requires R13+");
        page = drawing_code_page(file, version);
        if (version >= 1018) {
            section = object_section ? std::move(*object_section) : read_dwg_paged_section(file, "AcDb:AcDbObjects");
            bytes = section;
        } else
            bytes = file;
    }
    std::string_view body(const DwgR2000Objects &o, std::uint64_t h) const {
        const auto &r = o.records.at(h);
        return bytes.substr(r.body_offset, r.body_size);
    }
};
std::uint64_t lifecycle_target(const DwgR2000Objects &o, std::uint64_t target) {
    if (!target)
        return o.model_space;
    std::uint64_t result = 0;
    for (auto &[h, s] : o.symbols)
        if (s.type == 49 && (h == target || (s.layout && s.layout == target))) {
            require(!result, "Ambiguous modeler destination");
            result = h;
        }
    require(result, "Missing modeler block/layout");
    return result;
}
std::uint64_t lifecycle_owner(const DwgR2000Objects &o, const DwgR2000EntityProperties &p) {
    return p.mode == 2 ? o.model_space : p.mode == 1 ? o.paper_space : p.owner;
}
std::vector<std::uint64_t> lifecycle_members(const LifecycleStorage &s, const DwgR2000Objects &o,
                                             std::uint64_t block, BlockBits &bits) {
    auto record = o.records.at(block);
    auto symbol = read_symbol(s.body(o, block), record, block, s.version, s.page, &bits);
    require(symbol.type == 49 && !bits.external, "Cannot edit xref or non-block ownership");
    auto result = symbol.members;
    if (s.version < 1018) {
        std::set<std::uint64_t> visited;
        require(bool(symbol.first) == bool(symbol.last), "Invalid block endpoints");
        for (auto h = symbol.first; h;) {
            require(visited.insert(h).second, "Cyclic block members");
            auto entry = ownership_record(s.body(o, h), o.records.at(h), h, s.version);
            require(lifecycle_owner(o, entry.props) == block, "Cross-owner block chain");
            result.push_back(h);
            if (h == symbol.last)
                break;
            h = entry.props.next;
            require(h, "Truncated block chain");
        }
    }
    require(std::set<std::uint64_t>(result.begin(), result.end()).size() == result.size(),
            "Duplicate block members");
    return result;
}
using LifecycleEdits = std::map<std::uint64_t, std::string>;
void lifecycle_membership(const LifecycleStorage &s, const DwgR2000Objects &o, std::uint64_t block,
                          std::uint64_t remove, std::uint64_t append, LifecycleEdits &edits,
                          LifecycleEdits &added) {
    BlockBits positions;
    auto members = lifecycle_members(s, o, block, positions);
    if (remove) {
        auto it = std::find(members.begin(), members.end(), remove);
        require(it != members.end(), "Missing modeler member");
        members.erase(it);
    }
    if (append) {
        require(std::find(members.begin(), members.end(), append) == members.end(),
                "Duplicate modeler member");
        members.push_back(append);
    }
    edits[block] = with_members(s.body(o, block), positions, s.version, members);
    for (std::size_t i = 0; i < members.size(); ++i) {
        auto h = members[i];
        if (s.version >= 1018 && h != append)
            continue;
        bool fresh = added.count(h);
        auto rec = fresh ? read_record(added.at(h), 0, h, s.version) : o.records.at(h);
        std::string_view body =
            fresh ? std::string_view(added.at(h)).substr(rec.body_offset, rec.body_size) : s.body(o, h);
        auto entry = ownership_record(body, rec, h, s.version);
        auto mode = block == o.model_space ? 2u : block == o.paper_space ? 1u : 0u;
        auto data = with_owner(body, entry, h, s.version, mode, block, i ? members[i - 1] : 0,
                               i + 1 < members.size() ? members[i + 1] : 0);
        (fresh ? added : edits)[h] = std::move(data);
    }
}
std::string lifecycle_header(std::string_view file, unsigned v) {
    if (v >= 1018)
        return read_dwg_paged_section(file, "AcDb:Header");
    auto i = read_dwg_linear_index(file).sections.at(0);
    return std::string(file.substr(i.offset, i.size));
}
struct LifecycleRecoverySource {
    const LifecycleStorage &storage;
    const DwgR2000Index &index;
};
// Rebuild records and offset map together; opaque survivors and gaps remain byte-for-byte.
std::string lifecycle_rewrite(std::string_view file, const LifecycleStorage &s, const DwgR2000Objects &o,
                              const LifecycleEdits &edits,
                              const std::set<std::uint64_t> &removed, const LifecycleEdits &added,
                              const std::optional<std::string> &header = {}, std::uint64_t next = 0,
                              const DwgR2000Index *recovered_index = nullptr) {
    auto v = s.version;
    std::map<std::size_t, std::uint64_t> order;
    for (auto &[h, r] : o.records)
        order.emplace(r.offset, h);
    std::map<std::uint64_t, std::size_t> offsets;
    if (v >= 1018) {
        std::string bytes;
        std::size_t cursor = 0;
        for (auto [pos, h] : order) {
            bytes.append(s.bytes.substr(cursor, pos - cursor));
            const auto &r = o.records.at(h);
            cursor = r.body_offset + r.body_size + 2;
            if (removed.count(h))
                continue;
            offsets[h] = bytes.size();
            auto it = edits.find(h);
            if (it != edits.end())
                bytes += it->second;
            else
                bytes.append(s.bytes.substr(pos, cursor - pos));
        }
        bytes.append(s.bytes.substr(cursor));
        for (auto &[h, data] : added) {
            require(!offsets.count(h), "Handle conflict");
            offsets[h] = bytes.size();
            bytes += data;
        }
        std::map<std::string, std::string> sections{{"AcDb:AcDbObjects", std::move(bytes)},
                                                    {"AcDb:Handles", write_map(offsets)}};
        auto classes = o.classes;
        std::set<unsigned> touched;
        for (auto h : removed)
            if (o.records.at(h).type >= 500)
                touched.insert(o.records.at(h).type);
        for (auto &[h, data] : added) {
            auto type = read_record(data, 0, h, v).type;
            if (type >= 500)
                touched.insert(type);
        }
        for (auto type : touched) {
            require(type - 500 < classes.size(), "Missing lifecycle class definition");
            std::int32_t count = 0;
            for (auto &[h, r] : o.records)
                if (!removed.count(h) && unsigned(r.type) == type)
                    ++count;
            for (auto &[h, data] : added)
                if (unsigned(read_record(data, 0, h, v).type) == type)
                    ++count;
            classes[type - 500].fields[0] = count;
        }
        if (!touched.empty())
            sections["AcDb:Classes"] = lifecycle_classes(file, classes);
        auto optional_section = [&](std::string_view name) -> std::optional<std::string> {
            try {
                return read_dwg_paged_section(file, name);
            } catch (const std::invalid_argument &error) {
                if (!recovered_index || std::string_view(error.what()) != "DWG section not found")
                    throw;
                return std::nullopt;
            }
        };
        if (auto free = optional_section("AcDb:ObjFreeSpace")) {
            set_integer(*free, 4, offsets.size(), 4);
            sections["AcDb:ObjFreeSpace"] = std::move(*free);
        }
        if (header) {
            sections["AcDb:Header"] = *header;
            if (auto aux = optional_section("AcDb:AuxHeader")) {
                update_aux_seed(*aux, v, next);
                sections["AcDb:AuxHeader"] = std::move(*aux);
            }
        }
        auto replace = v == 1021 ? detail::replace_ac21_sections : detail::replace_ac18_sections;
        return replace(file, sections);
    }
    std::optional<DwgR2000Index> current_index;
    if (!recovered_index)
        current_index = read_dwg_linear_index(file);
    const auto &idx = recovered_index ? *recovered_index : *current_index;
    auto map = idx.sections.at(2);
    struct Change {
        std::size_t length;
        std::string bytes;
    };
    std::map<std::size_t, Change> changes;
    if (header) {
        auto loc = idx.sections.at(0);
        changes[loc.offset] = {loc.size, *header};
    }
    for (auto [pos, h] : order) {
        const auto &r = o.records.at(h);
        require(r.body_offset + r.body_size + 2 <= map.offset, "Object after map");
        auto it = edits.find(h);
        if (removed.count(h) || it != edits.end())
            changes[pos] = {r.body_offset + r.body_size + 2 - pos,
                            removed.count(h) ? std::string() : it->second};
    }
    auto relocate = [&](std::size_t pos) {
        auto n = pos;
        for (auto &[at, c] : changes)
            if (at < pos) {
                require(at + c.length <= pos, "Overlapping lifecycle edits");
                n = n - c.length + c.bytes.size();
            }
        return n;
    };
    for (auto [pos, h] : order)
        if (!removed.count(h))
            offsets[h] = relocate(pos);
    std::string extra;
    for (auto &[h, data] : added) {
        require(!offsets.count(h), "Handle conflict");
        offsets[h] = relocate(map.offset) + extra.size();
        extra += data;
    }
    auto map_offset = relocate(map.offset) + extra.size();
    auto encoded = write_map(offsets);
    changes[map.offset] = {map.size, extra + encoded};
    std::string out;
    std::size_t cursor = 0;
    for (auto &[at, c] : changes) {
        require(at >= cursor, "Overlapping lifecycle edits");
        out.append(file.substr(cursor, at - cursor));
        out += c.bytes;
        cursor = at + c.length;
    }
    out.append(file.substr(cursor));
    for (std::size_t i = 0; i < idx.sections.size(); ++i) {
        auto field = 25 + i * 9;
        auto id = static_cast<unsigned char>(file[field]);
        auto loc = idx.sections.at(id);
        auto pos = relocate(loc.offset);
        set_integer(out, field + 1, id == 2 ? map_offset : pos, 4);
        if (id == 2)
            set_integer(out, field + 5, encoded.size(), 4);
        if (id == 0 && header)
            set_integer(out, field + 5, header->size(), 4);
        if (id == 3 && loc.size >= 8)
            set_integer(out, pos + 4, offsets.size(), 4);
        if (id == 5 && header && loc.size) {
            auto aux = std::string(file.substr(loc.offset, loc.size));
            update_aux_seed(aux, v, next);
            out.replace(pos, aux.size(), aux);
        }
    }
    auto preview = get_u32(file, 13);
    if (preview) {
        auto pos = relocate(preview);
        set_integer(out, 13, pos, 4);
        require(preview + 37 <= file.size(), "Truncated preview");
        auto n = static_cast<unsigned char>(file[preview + 20]);
        for (unsigned i = 0; i < n; ++i) {
            auto f = 22 + i * 9;
            set_integer(out, pos + f, relocate(get_u32(file, preview + f)), 4);
        }
    }
    set_crc(out, 0, 25 + 9 * idx.sections.size());
    return out;
}
std::string lifecycle_rewrite(std::string_view file, const DwgR2000Objects &o, const LifecycleEdits &edits,
                              const std::set<std::uint64_t> &removed, const LifecycleEdits &added,
                              const std::optional<std::string> &header = {}, std::uint64_t next = 0,
                              const LifecycleRecoverySource *recovery = nullptr) {
    if (recovery)
        return lifecycle_rewrite(file, recovery->storage, o, edits, removed, added, header, next, &recovery->index);
    return lifecycle_rewrite(file, LifecycleStorage(file), o, edits, removed, added, header, next);
}
const DwgHistoryRecord *lifecycle_object(const DwgR2000Objects &o, std::uint64_t h) {
    auto m = o.metadata_records.find(h);
    if (m != o.metadata_records.end())
        return &m->second;
    auto r = o.history_records.find(h);
    return r == o.history_records.end() ? nullptr : &r->second;
}
std::set<std::uint64_t> lifecycle_owned(const DwgR2000Objects &o, std::uint64_t entity) {
    const auto &m = o.modelers.at(entity);
    std::set<std::uint64_t> owned{entity};
    std::vector<std::pair<std::uint64_t, std::uint64_t>> pending;
    if (m.properties.extension_dictionary)
        pending.push_back({m.properties.extension_dictionary, entity});
    if (m.history)
        pending.push_back({m.history, entity});
    if (m.surface) {
        if (auto meta = detail::metadata_record(o, entity, m.properties.extension_dictionary,
                                                "CADCODEC_SURFACE_HISTORY_V1")) {
            require(detail::metadata_value(meta->metadata, 90) == 1, "Unsupported surface history schema");
            auto root = detail::metadata_reference(*meta, 330);
            if (root)
                pending.push_back({root, entity});
        }
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
        require(i < 1000000, "Excessive owned object graph");
        auto [h, parent] = pending[i];
        auto r = lifecycle_object(o, h);
        require(r && r->owner == parent, "Missing, unsupported or foreign owned object");
        if (!owned.insert(h).second)
            continue;
        if (r->extension_dictionary)
            pending.push_back({r->extension_dictionary, h});
        if (r->root && r->root->evaluation_graph) {
            auto g = lifecycle_object(o, r->root->evaluation_graph);
            require(g && g->graph && g->owner == h, "Foreign history graph");
            pending.push_back({g->handle, h});
        }
        for (auto &[child, node] : o.history_records)
            if (node.owner == h)
                pending.push_back({child, h});
        if (r->graph)
            for (auto &node : r->graph->nodes) {
                auto op = lifecycle_object(o, node.expression);
                require(op && op->operation && op->owner == h, "Foreign history expression");
            }
        for (auto &[name, target] : r->dictionary_entries) {
            auto child = lifecycle_object(o, target);
            require(child && child->owner == h, "Private or foreign dictionary child");
            pending.push_back({target, h});
        }
    }
    return owned;
}
std::uint64_t lifecycle_remap(const std::map<std::uint64_t, std::uint64_t> &ids, std::uint64_t h) {
    auto it = ids.find(h);
    return it == ids.end() ? h : it->second;
}
std::string lifecycle_clone(const LifecycleStorage &s, const DwgR2000Objects &o, std::uint64_t h,
                            const std::map<std::uint64_t, std::uint64_t> &ids) {
    const auto &r = o.records.at(h);
    auto id = ids.at(h);
    auto body = s.body(o, h);
    DwgBitReader old(body);
    DwgBitWriter main, refs;
    auto own_begin = r.type_end_bit + ((s.version >= 1015 && s.version < 1024) ? 32 : 0);
    copy_until(main, old, own_begin);
    main.absolute_handle(0, id);
    old.seek(r.handle_end_bit);
    auto shift = std::int64_t(main.position()) - std::int64_t(r.handle_end_bit);
    auto split = r.split_field_bit + (r.split_field_bit >= r.handle_end_bit ? shift : 0);
    require(old.bit_short() == 0, "Copying modeler XDATA is not implemented");
    old.seek(r.handle_end_bit);
    auto meta = o.metadata_records.find(h);
    if (meta != o.metadata_records.end() && meta->second.type == "XRECORD") {
        const auto &x = meta->second;
        auto data = x.metadata;
        for (auto f : detail::metadata_fields(data, s.version >= 1021))
            if (f.code < 0 || f.code == 480 || f.code == 481 || (f.code >= 330 && f.code <= 369) ||
                (f.code >= 390 && f.code <= 399) || f.code == 1005) {
                auto value = lifecycle_remap(ids, detail::metadata_uint(data, f.offset, 8));
                for (unsigned i = 0; i < 8; ++i)
                    data[f.offset + i] = char(value >> (8 * i));
            }
        copy_until(main, old, x.parameters_begin_bit);
        main.bit_long(data.size());
        for (unsigned char c : data)
            main.bits(8, c);
        old.seek(x.parameters_end_bit);
    }
    copy_until(main, old, r.main_bits);
    auto end = body.size() * 8 - (8 - r.main_bits % 8) % 8;
    while (old.position() < end) {
        require(end - old.position() >= 8, "Invalid reference tail");
        auto ref = old.handle(h);
        auto target = lifecycle_remap(ids, ref.value);
        if (ref.code >= 6)
            refs.relative_handle(id, target);
        else
            refs.absolute_handle(ref.code, target);
    }
    return pack_record(s.version, main, split, refs);
}
std::optional<std::string> lifecycle_clean_reactors(std::string_view body, const DwgR2000Record &record,
                                                    std::uint64_t h, unsigned v, bool entity,
                                                    const std::set<std::uint64_t> &removed) {
    auto r = record;
    DwgBitReader main(body), refs(body);
    main.seek(r.handle_end_bit);
    skip_entity_prefix(main, body.size() * 8, entity, v);
    if (v < 1015) {
        r.split_field_bit = main.position();
        r.main_bits = main.little_endian(4);
    }
    refs.seek(r.main_bits);
    auto mode = entity ? main.bits(2) : 0;
    if (!mode)
        refs.handle(h);
    auto count_begin = main.position();
    auto count = main.bit_long();
    auto count_end = main.position();
    require(count >= 0 && std::size_t(count) <= (body.size() * 8 - refs.position()) / 8,
            "Invalid reactor count");
    auto ref_begin = refs.position();
    DwgBitWriter surviving;
    std::size_t kept = 0;
    for (int i = 0; i < count; ++i) {
        auto start = refs.position();
        auto ref = refs.handle(h);
        if (!removed.count(ref.value)) {
            auto end = refs.position();
            refs.seek(start);
            copy_until(surviving, refs, end);
            ++kept;
        }
    }
    if (kept == std::size_t(count))
        return {};
    auto ref_end = refs.position();
    DwgBitReader old(body);
    DwgBitWriter output, handles;
    copy_until(output, old, count_begin);
    output.bit_long(kept);
    old.seek(count_end);
    copy_until(output, old, r.main_bits);
    copy_until(handles, old, ref_begin);
    append_bits(handles, surviving);
    old.seek(ref_end);
    copy_until(handles, old, body.size() * 8 - (8 - r.main_bits % 8) % 8);
    return pack_record(v, output, r.split_field_bit, handles);
}
} // namespace
#include "dwg_history_author.inc"
#include "dwg_dynamic_write.inc"
#include "dwg_associative_write.inc"
#include "dwg_class_object_write.inc"
#include "arch_dwg.inc"
#include "dwg_object_data.inc"
#include "civil_dwg.inc"
std::string detail::register_dwg_surface(std::string_view file, const DwgR2000Objects &o,
                                         std::string_view name) {
    static const std::map<std::string, std::string> known{{"SURFACE", "AcDbSurface"},
                                                          {"PLANESURFACE", "AcDbPlaneSurface"},
                                                          {"EXTRUDEDSURFACE", "AcDbExtrudedSurface"},
                                                          {"LOFTEDSURFACE", "AcDbLoftedSurface"},
                                                          {"REVOLVEDSURFACE", "AcDbRevolvedSurface"},
                                                          {"SWEPTSURFACE", "AcDbSweptSurface"},
                                                          {"NURBSURFACE", "AcDbNurbSurface"}};
    auto cpp = known.find(std::string(name));
    require(cpp != known.end(), "Unknown surface class");
    return register_dwg_classes(file, o, {{std::string(name), cpp->second, 498}});
}
std::string detail::add_dwg_modeler(std::string_view file, const DwgR2000Objects &o, unsigned type,
                                    const DwgSurface *surface, std::uint64_t target, std::uint64_t &id) {
    LifecycleStorage s(file);
    target = lifecycle_target(o, target);
    require((type >= 37 && type <= 39) || (surface && type >= 500 && s.version >= 1018),
            "Unsupported modeler class");
    auto header =
        allocate_symbol_handle(lifecycle_header(file, s.version), s.version, o.records.rbegin()->first, id);
    auto layer = symbol_id(o, s.version, 51, "0");
    require(layer.has_value(), "Missing layer 0");
    DwgPrimitive primitive;
    primitive.geometry = DwgLineGeometry{};
    NewEntity entry{id, 0, 0, &primitive};
    LifecycleEdits added{
        {id, new_record(s.version, entry, 0, drawing_code_page(file, s.version), type, *layer, surface)}},
        edits;
    lifecycle_membership(s, o, target, 0, id, edits, added);
    return lifecycle_rewrite(file, o, edits, {}, added, header, id + 1);
}
std::string detail::copy_dwg_modeler(std::string_view file, const DwgR2000Objects &o, std::uint64_t h,
                                     std::uint64_t target, std::map<std::uint64_t, std::uint64_t> &ids) {
    LifecycleStorage s(file);
    target = lifecycle_target(o, target);
    auto owned = lifecycle_owned(o, h);
    auto header = lifecycle_header(file, s.version);
    auto maximum = o.records.rbegin()->first;
    for (auto old : owned) {
        std::uint64_t id;
        header = allocate_symbol_handle(header, s.version, maximum, id);
        ids[old] = id;
        maximum = id;
    }
    LifecycleEdits added, edits;
    for (auto old : owned)
        added[ids.at(old)] = lifecycle_clone(s, o, old, ids);
    lifecycle_membership(s, o, target, 0, ids.at(h), edits, added);
    auto out = lifecycle_rewrite(file, o, edits, {}, added, header, maximum + 1);
    if (s.version >= 1027 && !o.modelers.at(h).acis_empty) {
        const auto &data = o.modelers.at(h).data.geometry.sab;
        require(!data.empty(), "Missing source AcDs body");
        auto section = read_dwg_paged_section(file, "AcDb:AcDsPrototype_1b");
        section = insert_dwg_acds_body(section, ids.at(h), data);
        out = replace_ac18_sections(out, {{"AcDb:AcDsPrototype_1b", std::move(section)}});
    }
    return out;
}
std::string detail::remove_dwg_modeler(std::string_view file, const DwgR2000Objects &o, std::uint64_t h) {
    LifecycleStorage s(file);
    auto owned = lifecycle_owned(o, h);
    LifecycleEdits edits, added;
    lifecycle_membership(s, o, lifecycle_owner(o, o.modelers.at(h).properties), h, 0, edits, added);
    // Clean supported reactor arrays; other surviving references make deletion ambiguous and reject.
    for (auto &[id, r] : o.records) {
        if (owned.count(id))
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
            auto clean = lifecycle_clean_reactors(body, record, id, s.version, entity, owned);
            if (clean) {
                encoded = *clean;
                edits[id] = encoded;
                record = read_record(encoded, 0, id, s.version);
                body = std::string_view(encoded).substr(record.body_offset, record.body_size);
            }
        }
        if (auto object = lifecycle_object(o, id); object && object->type == "XRECORD") {
            std::size_t reference = 0;
            for (auto f : metadata_fields(object->metadata, s.version >= 1021))
                if (f.code < 0 || f.code == 480 || f.code == 481 || (f.code >= 330 && f.code <= 369) ||
                    (f.code >= 390 && f.code <= 399) || f.code == 1005) {
                    auto target = metadata_uint(object->metadata, f.offset, 8);
                    // As in read_xrecord, stream references override 330-369
                    // entries in order; other codes do not consume a slot.
                    if (f.code >= 330 && f.code <= 369) {
                        if (reference < object->reference_values.size())
                            target = object->reference_values[reference];
                        ++reference;
                    }
                    require(!owned.count(target), "Surviving metadata references deleted modeler");
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
            require(!owned.count(ref.value), "Modeler is referenced by a surviving object");
        }
    }
    auto out = lifecycle_rewrite(file, o, edits, owned, {});
    if (s.version >= 1027 && !o.modelers.at(h).acis_empty) {
        auto section = remove_dwg_acds_body(read_dwg_paged_section(file, "AcDb:AcDsPrototype_1b"), h);
        out = replace_ac18_sections(out, {{"AcDb:AcDsPrototype_1b", std::move(section)}});
    }
    return out;
}
