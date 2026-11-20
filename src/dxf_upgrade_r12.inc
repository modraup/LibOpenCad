// SPDX-License-Identifier: MPL-2.0
// Based on acadrust c3573068 DXF reader post-read resolution and modern writer:
// defaults, unique identities, symbol/block references and modern subclasses.
// Runs only on the converter's staged copy. Legacy identities are remapped explicitly.
using UpgradePairs = std::vector<DxfRecord::Pair>;
static std::string upgrade_space(std::string name) {
    auto key = detail::symbol_name_key(name);
    if (key == detail::symbol_name_key("$MODEL_SPACE"))
        return "*Model_Space";
    if (key == detail::symbol_name_key("$PAPER_SPACE"))
        return "*Paper_Space";
    return name;
}
static UpgradePairs upgrade_body(const DxfRecord &source) {
    UpgradePairs out;
    bool tail = false;
    for (std::size_t i = 1; i < source.pairs_.size(); ++i) {
        const auto &p = source.pairs_[i];
        if (p.code >= 1000)
            tail = true;
        if (!tail && p.code == (source.type() == "DIMSTYLE" ? 105 : 5))
            continue;
        if (!tail && (p.code == 100 || p.code == 101 || p.code == 102 || (p.code >= 320 && p.code <= 369)))
            throw std::invalid_argument(
                "Modern subclass/application/owner data inside R12 source requires explicit schema handling");
        out.push_back(p);
    }
    return out;
}
static std::uint64_t upgrade_id(std::string_view text) {
    if (text.empty())
        return 0;
    std::size_t used = 0;
    auto value = std::stoull(std::string(text), &used, 16);
    if (used != text.size() || text.find_first_not_of("0123456789abcdefABCDEF") != text.npos)
        throw std::invalid_argument("Invalid R12 handle");
    return value;
}
// Port of acadrust io/dwg/legacy_viewport::restore_r12. Only the ACAD
// MVIEW packet is interpreted. The original XDATA remains attached.
void restore_legacy_view(DxfViewport& viewport) {
    for (const auto& app : viewport.extended_data()) {
        if (detail::symbol_name_key(app.application_name) != U"ACAD") continue;
        const auto& values = app.values;
        for (std::size_t i = 0; i + 1 < values.size(); ++i) {
            if (values[i].code != 1000 || std::get<std::string>(values[i].value) != "MVIEW" ||
                values[i+1].code != 1002 || std::get<std::string>(values[i+1].value) != "{") continue;
            std::size_t pos = i+1;
            auto take = [&](int code) -> const XDataValue& {
                if (pos == values.size() || values[pos].code != code)
                    throw std::invalid_argument("Malformed R12 VIEWPORT MVIEW field");
                return values[pos++];
            };
            auto brace = [&](const char* expected) {
                if (std::get<std::string>(take(1002).value) != expected)
                    throw std::invalid_argument("Malformed R12 VIEWPORT MVIEW nesting");
            };
            auto small = [&] {return std::get<std::int16_t>(take(1070).value);};
            auto real = [&] {return std::get<double>(take(1040).value);};
            auto xy = [&] {return Point3{real(), real(), 0};};
            brace("{");
            if (small() != 16) throw std::invalid_argument("Unknown MVIEW version");
            auto v = viewport.definition();
            v.view_target = std::get<Point3>(take(1010).value);
            v.view_direction = std::get<Point3>(take(1010).value);
            v.twist_angle = real();
            v.view_height = real();
            v.view_center = xy();
            v.lens_length = real();
            v.front_clip_z = real();
            v.back_clip_z = real();
            v.status_flags = small() | 0x8000;
            v.circle_sides = small();
            if (small()) v.status_flags |= 128;
            const auto icon = small();
            v.ucs_at_origin = (icon & 2) != 0;
            if (small()) v.status_flags |= 256;
            if (small()) v.status_flags |= 512;
            if (small()) v.status_flags |= 1024;
            const auto iso = small();
            if (iso == 1) v.status_flags |= 4096;
            if (iso == 2) v.status_flags |= 8192;
            v.snap_angle = real();
            v.snap_base = xy();
            v.snap_spacing = xy();
            v.grid_spacing = xy();
            if (small()) v.status_flags |= 2048;
            brace("{");
            v.frozen_layers.clear();
            while (pos < values.size() && values[pos].code == 1003) {
                const auto name = std::get<std::string>(take(1003).value);
                const auto layer = doc_.get_layer(name);
                if (!layer) throw std::invalid_argument("Missing frozen viewport layer: " + name);
                v.frozen_layers.push_back(upgrade_id(layer->handle()));
            }
            brace("}"); brace("}");
            viewport.set_definition(v);
            return;
        }
    }
}
void upgrade_r12() {
    auto source = doc_;
    // Decode old code pages and binary numbers before constructing UTF-8 defaults.
    for (auto &entry : source.records_) {
        std::visit([&](DxfRecord &r) { normalize(r, nullptr); }, entry);
        if (auto *group = sequence(entry)) {
            for (auto &r : group->children_)
                normalize(r, nullptr);
            if (group->seqend_)
                normalize(*group->seqend_, nullptr);
        }
    }
    struct Located {
        std::size_t index;
        std::string section, table, block;
    };
    std::vector<Located> located;
    std::string section, table, block;
    std::set<std::string> sections, tables;
    bool eof = false;
    for (std::size_t i = 0; i < source.records_.size(); ++i) {
        const auto &r = source.record_at({i, std::nullopt, false});
        const auto type = r.type();
        if (eof)
            throw std::invalid_argument("R12 records after EOF");
        if (type == "EOF") {
            if (!section.empty())
                throw std::invalid_argument("R12 EOF inside section");
            eof = true;
        }
        if (type == "SECTION") {
            if (!section.empty())
                throw std::invalid_argument("Nested R12 section");
            section = r.field("", 2);
            if (!sections.insert(section).second)
                throw std::invalid_argument("Duplicate R12 section");
            if (!one_of(section, {"HEADER", "TABLES", "BLOCKS", "ENTITIES"}))
                throw std::invalid_argument("Unreviewed R12 section: " + section);
        } else if (type == "ENDSEC") {
            if (section.empty() || !table.empty() || !block.empty())
                throw std::invalid_argument("Unclosed R12 table or block");
            section.clear();
        } else if (type == "TABLE") {
            if (section != "TABLES" || !table.empty())
                throw std::invalid_argument("Invalid R12 TABLE placement");
            table = r.field("", 2);
            if (table.empty() || !tables.insert(table).second)
                throw std::invalid_argument("Empty or duplicate R12 table name");
        } else if (type == "ENDTAB") {
            if (table.empty())
                throw std::invalid_argument("Orphan R12 ENDTAB");
            table.clear();
        } else if (type == "BLOCK") {
            if (section != "BLOCKS" || !block.empty())
                throw std::invalid_argument("Invalid R12 BLOCK placement");
            block = upgrade_space(r.text_field("", 2));
            if (block.empty())
                throw std::invalid_argument("Unnamed R12 block");
        } else if (type == "ENDBLK") {
            if (block.empty())
                throw std::invalid_argument("Orphan R12 ENDBLK");
        }
        if (type != "EOF" && type != "ENDSEC" && section.empty())
            throw std::invalid_argument("R12 record outside a section");
        located.push_back({i, section, table, block});
        if (type == "ENDBLK")
            block.clear();
    }
    if (!eof || !section.empty() || !table.empty() || !block.empty())
        throw std::invalid_argument("Unterminated R12 structure");
    doc_ = DxfDocument::create(false, source.insertion_units());
    std::map<std::uint64_t, std::string> remap;
    auto &imported = r12_imported_;
    auto bind = [&](const DxfRecord &old, const std::string &fresh) {
        const auto id = upgrade_id(old.handle());
        if (id) {
            if (!remap.emplace(id, fresh).second)
                throw std::invalid_argument("Ambiguous R12 source identity");
            report_.handle_mappings.push_back({old.type(), old.handle(), fresh});
        }
        imported.insert(fresh);
    };
    auto mutable_record = [&](const std::string &h) -> DxfRecord & {
        return const_cast<DxfRecord &>(*doc_.get_record(h));
    };
    auto table_owner = [&](std::string_view name) -> std::string {
        for (const auto &entry : doc_.records_) {
            const auto &r = std::visit([](const DxfRecord &v) -> const DxfRecord & { return v; }, entry);
            if (r.type() == "TABLE" && r.field("", 2) == name)
                return r.handle();
        }
        return {};
    };
    const std::map<std::string, std::pair<SymbolTable, std::string>> symbols{
        {"LAYER", {SymbolTable::Layer, "AcDbLayerTableRecord"}},
        {"LTYPE", {SymbolTable::LineType, "AcDbLinetypeTableRecord"}},
        {"STYLE", {SymbolTable::TextStyle, "AcDbTextStyleTableRecord"}},
        {"DIMSTYLE", {SymbolTable::DimStyle, "AcDbDimStyleTableRecord"}},
        {"APPID", {SymbolTable::AppId, "AcDbRegAppTableRecord"}},
        {"UCS", {SymbolTable::Ucs, "AcDbUCSTableRecord"}},
        {"VIEW", {SymbolTable::View, "AcDbViewTableRecord"}},
        {"VPORT", {SymbolTable::VPort, "AcDbViewportTableRecord"}}};
    std::set<std::pair<std::string, std::u32string>> names;
    // Table entries carry raw legacy properties; retaining them avoids inventing absent values.
    for (const auto &l : located) {
        const auto &r = source.record_at({l.index, std::nullopt, false});
        const auto type = r.type();
        if (l.section != "TABLES" || one_of(type, {"SECTION", "TABLE", "ENDTAB"}))
            continue;
        auto schema = symbols.find(type);
        if (schema == symbols.end() || l.table != type)
            throw std::invalid_argument("Unreviewed R12 symbol table: " + type);
        auto name = r.text_field("", 2);
        const bool shape_font = name.empty() && type == "STYLE" && (r.short_field("", 70, 0) & 1) &&
                                !r.text_field("", 3).empty();
        if (name.empty() && !shape_font)
            throw std::invalid_argument("Unnamed legacy symbol requires separate font-index handling");
        if (!shape_font && !names.emplace(type, detail::symbol_name_key(name)).second)
            throw std::invalid_argument("Duplicate R12 symbol name");
        auto fields = upgrade_body(r);
        auto found = shape_font ? std::nullopt : doc_.get_symbol(schema->second.first, name);
        std::string handle;
        DxfRecord converted = doc_.make_record(type, schema->second.second);
        converted.pairs_.insert(converted.pairs_.end(), fields.begin(), fields.end());
        if (found) {
            handle = found->handle;
            converted.set_field("", type == "DIMSTYLE" ? 105 : 5, handle);
            converted.set_field("", 330, doc_.get_record(handle)->owner_handle());
            mutable_record(handle) = converted;
        } else {
            DxfDocument::Record entry = converted;
            if (type == "LAYER") {
                DxfLayer typed;
                static_cast<DxfRecord &>(typed) = converted;
                entry = typed;
            }
            if (type == "LTYPE") {
                DxfLineType typed;
                static_cast<DxfRecord &>(typed) = converted;
                entry = typed;
            }
            handle = attribute_hex(doc_.insert_table_entry(type, std::move(entry)));
        }
        bind(r, handle);
        doc_.rebuild_indexes();
    }
    // Source TABLE identities map to the modern table headers, not to entries.
    for (const auto &l : located) {
        const auto &r = source.record_at({l.index, std::nullopt, false});
        if (r.type() == "TABLE") {
            if (!symbols.count(l.table))
                throw std::invalid_argument("Unreviewed empty R12 table: " + l.table);
            auto owner = table_owner(l.table);
            if (owner.empty()) {
                // Create an empty modern table without fabricating a named entry.
                auto header = doc_.make_record("TABLE");
                header.set_field("", 2, l.table);
                owner = attribute_hex(doc_.allocate_handle());
                header.set_field("", 5, owner);
                header.set_field("", 330, "0");
                header.pairs_.push_back({100, "AcDbSymbolTable"});
                header.set_short_field("AcDbSymbolTable", 70, 0);
                std::size_t pos = 0;
                bool active = false;
                for (; pos < doc_.records_.size(); ++pos) {
                    const auto &v = doc_.record_at({pos, std::nullopt, false});
                    if (v.type() == "SECTION")
                        active = v.field("", 2) == "TABLES";
                    if (v.type() == "ENDSEC" && active)
                        break;
                }
                doc_.records_.insert(doc_.records_.begin() + pos, {header, doc_.make_record("ENDTAB")});
                doc_.rebuild_indexes();
            }
            auto fields = upgrade_body(r);
            bool tail = false;
            for (const auto &p : fields) {
                if (p.code >= 1000)
                    tail = true;
                if (tail)
                    mutable_record(owner).pairs_.push_back(p);
                else if (!code_in(p.code, {2, 70}))
                    throw std::invalid_argument("Unreviewed R12 table header field");
            }
            bind(r, owner);
        }
    }
    // Materialize every named block before resolving INSERTs, including forward references.
    std::set<std::u32string> blocks, external_blocks;
    for (const auto &l : located) {
        const auto &r = source.record_at({l.index, std::nullopt, false});
        if (r.type() != "BLOCK")
            continue;
        if (!blocks.insert(detail::symbol_name_key(l.block)).second)
            throw std::invalid_argument("Duplicate R12 block name");
        if (r.short_field("",70,0) & 12) external_blocks.insert(detail::symbol_name_key(l.block));
        // BLOCK group 1 and group 70 carry the external path and flags
        // in both generations. Keep the definition and its INSERT references.
        if (!doc_.get_symbol(SymbolTable::BlockRecord, l.block)) {
            // The public name validator excludes anonymous names; create then rename its generated markers.
            auto temporary = "LIBOPENCAD_IMPORT_" + std::to_string(doc_.next_handle());
            while (doc_.get_symbol(SymbolTable::BlockRecord, temporary))
                temporary += "_";
            auto id = attribute_hex(doc_.add_symbol(SymbolTable::BlockRecord, temporary));
            mutable_record(id).set_text_field("AcDbBlockTableRecord", 2, l.block);
            for (auto &entry : doc_.records_)
                std::visit(
                    [&](DxfRecord &b) {
                        if (b.owner_handle() == id && b.type() == "BLOCK") {
                            b.set_text_field("AcDbBlockBegin", 2, l.block);
                            b.set_text_field("AcDbBlockBegin", 3, l.block);
                        }
                    },
                    entry);
            doc_.rebuild_indexes();
        }
    }
    auto modern_entity = [&](const DxfRecord &old, std::string scope,
                             const std::string &extra_scope = std::string(),
                             const std::set<int> &extra_codes = std::set<int>()) -> DxfRecord {
        auto fields = upgrade_body(old);
        DxfRecord result = doc_.make_record(old.type());
        result.pairs_.push_back({100, "AcDbEntity"});
        UpgradePairs geometry, extra, tail;
        bool xdata = false;
        for (const auto &p : fields) {
            if (p.code >= 1000)
                xdata = true;
            if (xdata) {
                tail.push_back(p);
                continue;
            }
            if (code_in(p.code, {8, 6, 62, 67, 48, 60}))
                result.pairs_.push_back(p);
            else if (extra_codes.count(p.code))
                extra.push_back(p);
            else
                geometry.push_back(p);
        }
        if (!scope.empty()) {
            result.pairs_.push_back({100, scope});
            result.pairs_.insert(result.pairs_.end(), geometry.begin(), geometry.end());
        } else if (!geometry.empty())
            throw std::invalid_argument("Unexpected fields on R12 structural marker");
        if (!extra_scope.empty()) {
            result.pairs_.push_back({100, extra_scope});
            result.pairs_.insert(result.pairs_.end(), extra.begin(), extra.end());
        }
        result.pairs_.insert(result.pairs_.end(), tail.begin(), tail.end());
        return result;
    };
    // Keep named block bases/flags/names and distinct BLOCK/ENDBLK identities.
    for (const auto &l : located) {
        const auto &old = source.record_at({l.index, std::nullopt, false});
        if (!one_of(old.type(), {"BLOCK", "ENDBLK"}))
            continue;
        const auto owner = doc_.get_symbol(SymbolTable::BlockRecord, l.block)->handle;
        std::string h;
        for (const auto &entry : doc_.records_)
            std::visit(
                [&](const DxfRecord &r) {
                    if (r.type() == old.type() && r.owner_handle() == owner)
                        h = r.handle();
                },
                entry);
        if (h.empty())
            throw std::invalid_argument("Missing generated space/block marker");
        auto r = modern_entity(old, old.type() == "BLOCK" ? "AcDbBlockBegin" : "AcDbBlockEnd");
        r.set_field("", 5, h);
        r.set_field("", 330, owner);
        if (old.type() == "BLOCK") {
            r.set_text_field("AcDbBlockBegin", 2, l.block);
            r.set_text_field("AcDbBlockBegin", 3, l.block);
        }
        mutable_record(h) = r;
        bind(old, h);
    }
    doc_.rebuild_indexes();
    for (const auto &l : located) {
        const auto &old = source.record_at({l.index, std::nullopt, false});
        const auto type = old.type();
        if (l.section != "ENTITIES" && l.section != "BLOCKS")
            continue;
        if (one_of(type, {"SECTION", "BLOCK", "ENDBLK"}))
            continue;
        if (l.section == "BLOCKS" && l.block.empty())
            throw std::invalid_argument("R12 block entity outside BLOCK/ENDBLK");
        if (l.section=="BLOCKS" && external_blocks.count(detail::symbol_name_key(l.block))) {
            issue(Kind::Loss,"xref-cache-omitted",&old,0,"Target writer retains the external drawing path and omits cached entities");
            continue;
        }
        auto entry = source.records_[l.index];
        std::string scope, extra;
        std::set<int> extra_codes;
        if (type == "VIEWPORT")
            scope = "AcDbViewport";
        else if (type == "SHAPE")
            scope = "AcDbShape";
        else if (type == "LINE")
            scope = "AcDbLine";
        else if (type == "POINT")
            scope = "AcDbPoint";
        else if (type == "CIRCLE" || type == "ARC") {
            scope = "AcDbCircle";
            if (type == "ARC") {
                extra = "AcDbArc";
                extra_codes = {50, 51};
            }
        } else if (type == "SOLID" || type == "TRACE")
            scope = "AcDbTrace";
        else if (type == "3DFACE")
            scope = "AcDbFace";
        else if (type == "TEXT" || type == "ATTDEF") {
            scope = "AcDbText";
            extra = type == "TEXT" ? "AcDbText" : "AcDbAttributeDefinition";
            extra_codes = type == "TEXT" ? std::set<int>{73} : std::set<int>{2, 3, 70, 73, 74};
        } else if (type == "INSERT")
            scope = old.short_field("", 70, 1) > 1 || old.short_field("", 71, 1) > 1 ? "AcDbMInsertBlock"
                                                                                     : "AcDbBlockReference";
        else if (type == "POLYLINE") {
            auto flags = old.short_field("", 70, 0);
            scope = (flags & 64)   ? "AcDbPolyFaceMesh"
                    : (flags & 16) ? "AcDbPolygonMesh"
                    : (flags & 8)  ? "AcDb3dPolyline"
                                   : "AcDb2dPolyline";
        } else if (type == "DIMENSION") {
            auto kind = old.short_field("", 70, 0) & 15;
            if (kind > 6)
                throw std::invalid_argument("Invalid R12 dimension kind");
            scope = "AcDbDimension";
            static const char *subclasses[] = {"AcDbAlignedDimension",      "AcDbAlignedDimension",
                                               "AcDb2LineAngularDimension", "AcDbDiametricDimension",
                                               "AcDbRadialDimension",       "AcDb3PointAngularDimension",
                                               "AcDbOrdinateDimension"};
            extra = subclasses[kind];
            extra_codes = {13, 23, 33, 14, 24, 34, 15, 25, 35, 16, 26, 36, 40, 50, 52};
        } else
            throw std::invalid_argument("Unreviewed R12 entity upgrade: " + type);
        auto modern = modern_entity(old, scope, extra, extra_codes);
        if (type == "DIMENSION" && (old.short_field("", 70, 0) & 15) == 0)
            modern.pairs_.insert(std::find_if(modern.pairs_.begin(), modern.pairs_.end(),
                                              [](const auto &p) { return p.code >= 1000; }),
                                 {100, "AcDbRotatedDimension"});
        std::visit(
            [&](auto &e) {
                static_cast<DxfRecord &>(e) = modern;
                if constexpr (std::is_base_of_v<DxfEntity, std::decay_t<decltype(e)>>)
                    e.version_ = V::AC1032;
            },
            entry);
        if (auto* viewport = std::get_if<DxfViewport>(&entry))
            restore_legacy_view(*viewport);
        EntityOptions options;
        const auto space = upgrade_space(l.block);
        if (!space.empty() && detail::symbol_name_key(space) != detail::symbol_name_key("*Model_Space") &&
            detail::symbol_name_key(space) != detail::symbol_name_key("*Paper_Space"))
            options.block_name = space;
        else
            options.space =
                (!space.empty() ? detail::symbol_name_key(space) == detail::symbol_name_key("*Paper_Space")
                                : old.short_field("", 67, 0) != 0)
                    ? EntitySpace::Paper
                    : EntitySpace::Model;
        if (type == "INSERT" && !doc_.get_symbol(SymbolTable::BlockRecord, old.text_field("", 2)))
            throw std::invalid_argument("Unresolved R12 INSERT block");
        // BLOCK ownership wins over the optional legacy group 67. Child records
        // follow their parent, even when a legacy file contains stale space flags.
        const auto space_flag = options.space == EntitySpace::Paper ? "1" : "0";
        std::visit([&](DxfRecord &r) { r.set_field("AcDbEntity", 67, space_flag); }, entry);
        auto *group = sequence(entry);
        const auto *old_group = sequence(source.records_[l.index]);
        if (group) {
            if (!group->seqend_ &&
                (type == "POLYLINE" || !group->children_.empty() || old.short_field("", 66, 0)))
                throw std::invalid_argument("Incomplete R12 entity sequence");
            for (auto &child : group->children_) {
                if (type == "INSERT") {
                    if (child.type() != "ATTRIB")
                        throw std::invalid_argument("Non-attribute in R12 INSERT");
                    child = modern_entity(child, "AcDbText", "AcDbAttribute", {2, 70, 73, 74});
                } else {
                    if (child.type() != "VERTEX")
                        throw std::invalid_argument("Non-vertex in R12 POLYLINE");
                    const auto flag = child.short_field("", 70, 0);
                    const auto sub = scope == "AcDbPolyFaceMesh"
                                         ? ((flag & 64) ? "AcDbPolyFaceMeshVertex" : "AcDbFaceRecord")
                                     : scope == "AcDbPolygonMesh" ? "AcDbPolygonMeshVertex"
                                     : scope == "AcDb3dPolyline"  ? "AcDb3dPolylineVertex"
                                                                  : "AcDb2dVertex";
                    child = modern_entity(child, "AcDbVertex", sub,
                                          {10, 20, 30, 40, 41, 42, 50, 70, 71, 72, 73, 74});
                }
            }
            for (auto &child : group->children_)
                child.set_field("AcDbEntity", 67, space_flag);
            if (group->seqend_) {
                *group->seqend_ = modern_entity(*group->seqend_, "");
                group->seqend_->set_field("AcDbEntity", 67, space_flag);
            }
        }
        const auto id = doc_.insert_entity(std::move(entry), options);
        const auto h = attribute_hex(id);
        bind(old, h);
        const auto location = doc_.handle_index_.at(id).front();
        auto *added = sequence(doc_.records_[location.record]);
        if (added && old_group) {
            for (std::size_t i = 0; i < added->children_.size(); ++i)
                bind(old_group->children_[i], added->children_[i].handle());
            if (added->seqend_)
                bind(*old_group->seqend_, added->seqend_->handle());
        }
    }
    // Copy header variables without importing the old allocator or version declaration.
    if (source.header_index_) {
        const auto &old = source.record_at({*source.header_index_, std::nullopt, false});
        auto &header = const_cast<DxfRecord &>(doc_.record_at({*doc_.header_index_, std::nullopt, false}));
        for (std::size_t i = 2; i < old.pairs_.size();) {
            if (old.pairs_[i].code != 9)
                throw std::invalid_argument("Malformed R12 HEADER variable");
            auto end = i + 1;
            while (end < old.pairs_.size() && old.pairs_[end].code != 9)
                ++end;
            const auto name = old.pairs_[i].value;
            if (!r12_header_variables_.insert(name).second)
                throw std::invalid_argument("Duplicate R12 HEADER variable");
            if (name != "$ACADVER" && name != "$HANDSEED" && name != "$ACADMAINTVER") {
                for (std::size_t j = 2; j < header.pairs_.size();) {
                    auto k = j + 1;
                    while (k < header.pairs_.size() && header.pairs_[k].code != 9)
                        ++k;
                    if (header.pairs_[j].value == name) {
                        header.pairs_.erase(header.pairs_.begin() + j, header.pairs_.begin() + k);
                        break;
                    }
                    j = k;
                }
                header.pairs_.insert(header.pairs_.end(), old.pairs_.begin() + i, old.pairs_.begin() + end);
                if (name == "$HANDLING")
                    header.pairs_.back().value = "1";
            }
            i = end;
        }
    }
    // Only imported XDATA is remapped; fresh defaults already carry modern references.
    for (const auto &h : imported) {
        auto &r = mutable_record(h);
        for (auto &p : r.pairs_) {
            if (p.code == 1001 && !doc_.get_symbol(SymbolTable::AppId, r.decode_string(p.value)))
                throw std::invalid_argument("Unregistered R12 XDATA application");
            if (p.code == 1005) {
                const auto id = upgrade_id(p.value);
                if (!id)
                    continue;
                auto found = remap.find(id);
                if (found == remap.end())
                    throw std::invalid_argument("Unresolved R12 XDATA handle reference");
                p.value = found->second;
            }
        }
    }
    // Check imported records, including compound children and symbol entries.
    auto require_name = [&](SymbolTable table, const std::string &name, const char *role) {
        if (!name.empty() && !doc_.get_symbol(table, name))
            throw std::invalid_argument(std::string("Unresolved R12 ") + role + ": " + name);
    };
    for (const auto &h : imported) {
        const auto &r = *doc_.get_record(h);
        const auto type = r.type();
        if (r.pairs_.end() != std::find_if(r.pairs_.begin(), r.pairs_.end(), [](const auto &p) {
                return p.code == 100 && p.value == "AcDbEntity";
            })) {
            require_name(SymbolTable::Layer, r.text_field("AcDbEntity", 8, "0"), "layer");
            require_name(SymbolTable::LineType, r.text_field("AcDbEntity", 6, "ByLayer"), "linetype");
        }
        if (one_of(type, {"TEXT", "ATTDEF", "ATTRIB"}))
            require_name(SymbolTable::TextStyle, r.text_field("AcDbText", 7, "Standard"), "text style");
        if (type == "DIMENSION") {
            require_name(SymbolTable::DimStyle, r.text_field("AcDbDimension", 3, "Standard"),
                         "dimension style");
            require_name(SymbolTable::BlockRecord, r.text_field("AcDbDimension", 2), "dimension block");
        }
        if (type == "LAYER")
            require_name(SymbolTable::LineType, r.text_field("AcDbLayerTableRecord", 6, "Continuous"),
                         "layer linetype");
        if (type == "DIMSTYLE")
            for (auto code : {5, 6, 7})
                require_name(SymbolTable::BlockRecord, r.text_field("AcDbDimStyleTableRecord", code),
                             "dimension arrow block");
    }
    if (doc_.header_index_) {
        const auto &h = doc_.record_at({*doc_.header_index_, std::nullopt, false});
        const std::map<std::string, SymbolTable> refs{{"$CLAYER", SymbolTable::Layer},
                                                      {"$CELTYPE", SymbolTable::LineType},
                                                      {"$TEXTSTYLE", SymbolTable::TextStyle},
                                                      {"$DIMSTYLE", SymbolTable::DimStyle}};
        for (std::size_t i = 2; i + 1 < h.pairs_.size(); ++i)
            if (h.pairs_[i].code == 9) {
                auto found = refs.find(h.pairs_[i].value);
                if (found != refs.end())
                    require_name(found->second, h.decode_string(h.pairs_[i + 1].value), "header symbol");
            }
    }
    doc_.rebuild_indexes();
    r12_defaults_created_ = true;
    report_.changed_records += imported.size();
    issue(Kind::Information, "r12-object-model-upgraded", nullptr, 0,
          "Created modern defaults, tables, blocks and unique parent/child identities; old handles are "
          "listed in handle_mappings");
}
