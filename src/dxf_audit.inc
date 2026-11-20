// SPDX-License-Identifier: MPL-2.0
// acadrust audit.rs, adapted to record-backed native indexes and ownership.
AuditReport DxfDocument::audit() const {
    AuditReport report;
    auto issue = [&](AuditIssueCode code, AuditSeverity severity, AuditLocation location,
                     std::optional<std::uint64_t> h, std::string message, bool repairable = false) {
        report.issues.push_back({code, severity, std::move(location), h, std::move(message), repairable});
    };
    std::unordered_map<std::uint64_t, std::vector<RecordLocation>> observed;
    std::string section, table;
    std::uint64_t maximum = 0;
    auto identity = [&](const DxfRecord &r, RecordLocation pos, AuditLocation location, bool counted) {
        auto h = parse_handle(r.handle());
        location.handle = h.value_or(0);
        if (counted) {
            ++report.scanned_records;
            if (location.kind == AuditLocationKind::Entity)
                ++report.scanned_entities;
            else if (location.kind == AuditLocationKind::Object)
                ++report.scanned_objects;
            else if (location.kind == AuditLocationKind::TableEntry)
                ++report.scanned_table_entries;
        }
        if (!h && !r.handle().empty())
            issue(AuditIssueCode::InvalidHandle, AuditSeverity::Error, location, {},
                  "Record identity is not a hexadecimal handle");
        else if (!h || !*h)
            issue(AuditIssueCode::NullHandle, AuditSeverity::Error, location, 0, "Record has a null handle",
                  true);
        else {
            maximum = std::max(maximum, *h);
            if (!observed[*h].empty())
                issue(AuditIssueCode::DuplicateHandle, AuditSeverity::Error, location, *h,
                      "Duplicate stored handle");
            observed[*h].push_back(pos);
        }
    };
    std::unordered_set<std::size_t> expected_entities;
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto &r = record_at({i, {}, false});
        auto type = r.type();
        if (type == "SECTION") {
            section = trim(r.field("", 2));
            continue;
        }
        if (type == "ENDSEC") {
            section.clear();
            continue;
        }
        if (type == "TABLE") {
            table = trim(r.field("", 2));
            auto h = parse_handle(r.handle());
            if (h && *h) {
                maximum = std::max(maximum, *h);
                observed[*h].push_back({i, {}, false});
            }
            continue;
        }
        if (type == "ENDTAB") {
            table.clear();
            continue;
        }
        bool entity = (section == "ENTITIES" || section == "BLOCKS") && type != "BLOCK" && type != "ENDBLK" &&
                      type != "VERTEX" && type != "SEQEND" && type != "ATTRIB";
        bool marker = section == "BLOCKS" && (type == "BLOCK" || type == "ENDBLK");
        bool entry = section == "TABLES", object = section == "OBJECTS";
        if (!entity && !entry && !object && !marker)
            continue;
        AuditLocation loc;
        loc.kind = entry    ? AuditLocationKind::TableEntry
                   : object ? AuditLocationKind::Object
                            : AuditLocationKind::Entity;
        loc.table = entry ? table : "";
        if (entry)
            loc.name = r.text_field("", 2);
        loc.handle = handle_value(r.handle());
        identity(r, {i, {}, false}, loc, !marker);
        if (const auto *group = sequence(records_[i])) {
            for (std::size_t j = 0; j < group->children_.size(); ++j)
                identity(group->children_[j], {i, j, false}, {AuditLocationKind::Entity, "", "", 0, 0},
                         false);
            if (group->seqend_)
                identity(*group->seqend_, {i, {}, true}, {AuditLocationKind::Entity, "", "", 0, 0}, false);
        }
        if (entity) {
            expected_entities.insert(i);
            try {
                std::visit(
                    [&](const auto &e) {
                        using T = std::decay_t<decltype(e)>;
                        if constexpr (std::is_base_of_v<DxfEntity, T>) {
                            auto scale = e.linetype_scale();
                            if (!std::isfinite(scale) || scale <= 0)
                                issue(AuditIssueCode::InvalidGeometry, AuditSeverity::Error, loc, loc.handle,
                                      "Entity has a nonfinite or nonpositive linetype scale");
                            if (trim(e.layer()).empty())
                                issue(AuditIssueCode::InvalidGeometry, AuditSeverity::Warning, loc,
                                      loc.handle, "Entity has an empty layer name");
                            bool invalid = false;
                            if constexpr (std::is_same_v<T, DxfCircle> || std::is_same_v<T, DxfArc>)
                                invalid = !std::isfinite(e.radius()) || e.radius() < 0;
                            if constexpr (std::is_same_v<T, DxfArc>)
                                invalid |= !std::isfinite(e.start_angle_radians()) ||
                                           !std::isfinite(e.end_angle_radians());
                            if constexpr (std::is_same_v<T, DxfEllipse>)
                                invalid = !std::isfinite(e.minor_axis_ratio()) || e.minor_axis_ratio() <= 0 ||
                                          !std::isfinite(e.start_parameter()) ||
                                          !std::isfinite(e.end_parameter());
                            if constexpr (std::is_same_v<T, DxfSpline>) {
                                auto d = e.definition();
                                invalid = d.degree < 1;
                                for (auto k : d.knots)
                                    invalid |= !std::isfinite(k);
                                for (auto w : d.weights)
                                    invalid |= !std::isfinite(w) || w <= 0;
                                for (std::size_t n = 1; n < d.knots.size(); ++n)
                                    invalid |= d.knots[n] < d.knots[n - 1];
                            }
                            if constexpr (std::is_same_v<T, DxfMesh>) {
                                auto d = e.definition();
                                for (const auto &f : d.faces) {
                                    invalid |= f.vertices.size() < 3;
                                    for (auto v : f.vertices)
                                        invalid |= v >= d.vertices.size();
                                }
                            }
                            if (invalid)
                                issue(AuditIssueCode::InvalidGeometry, AuditSeverity::Error, loc, loc.handle,
                                      "Entity geometry violates a finite/range invariant");
                        }
                    },
                    records_[i]);
            } catch (const std::exception &e) {
                issue(AuditIssueCode::InvalidGeometry, AuditSeverity::Error, loc, loc.handle,
                      std::string("Cannot decode geometry: ") + e.what());
            }
        }
        if (object && std::holds_alternative<DxfRecord>(records_[i]) && type != "DICTIONARY" &&
            type != "XRECORD" && type != "ACDBDICTIONARYWDFLT" && type != "ACDBPLACEHOLDER" &&
            type != "PLOTSETTINGS")
            issue(AuditIssueCode::UnsupportedRecord, AuditSeverity::Warning, loc, loc.handle,
                  "Unsupported object retained as raw record");
    }
    auto same = [](const RecordLocation &a, const RecordLocation &b) {
        return a.record == b.record && a.vertex == b.vertex && a.seqend == b.seqend;
    };
    for (const auto &[h, locations] : observed) {
        auto stored = handle_index_.find(h);
        bool good = stored != handle_index_.end() && stored->second.size() == locations.size();
        if (good)
            for (std::size_t i = 0; i < locations.size(); ++i)
                good &= same(locations[i], stored->second[i]);
        if (!good)
            issue(AuditIssueCode::EntityIndexMismatch, AuditSeverity::Error,
                  {AuditLocationKind::Entity, "", "", h}, h,
                  "Stored handle index differs from record positions", true);
    }
    for (const auto &[h, positions] : handle_index_)
        if (!observed.count(h))
            issue(AuditIssueCode::EntityIndexMismatch, AuditSeverity::Error,
                  {AuditLocationKind::Entity, "", "", h}, h, "Index refers to a missing identity", true);
    if (std::unordered_set<std::size_t>(entity_records_.begin(), entity_records_.end()) != expected_entities)
        issue(AuditIssueCode::EntityIndexMismatch, AuditSeverity::Error, {}, {},
              "Entity membership index is stale", true);
    for (const auto &r : reference_issues()) {
        AuditIssueCode code = AuditIssueCode::DanglingReference;
        AuditSeverity severity = AuditSeverity::Error;
        bool repairable = false;
        std::string message;
        switch (r.status) {
        case ReferenceStatus::OwnerCycle:
            code = AuditIssueCode::OwnerCycle;
            message = "Owner relationship contains a cycle";
            break;
        case ReferenceStatus::MissingOwner:
            code = AuditIssueCode::OwnerMismatch;
            message = "Record has no owner";
            break;
        case ReferenceStatus::InvalidOwner:
        case ReferenceStatus::UnlistedChild:
            code = AuditIssueCode::OwnerMismatch;
            message = "Owner type or membership does not match the record";
            break;
        case ReferenceStatus::StaleAttributeFlag:
            code = AuditIssueCode::OwnerMismatch;
            message = "Block attribute flag has no ATTDEF";
            repairable = true;
            break;
        case ReferenceStatus::Ambiguous:
            message = "Reference resolves to multiple records";
            break;
        case ReferenceStatus::Invalid:
            message = "Reference is not a hexadecimal handle";
            break;
        case ReferenceStatus::Dangling:
            message = "Reference target does not exist";
            break;
        }
        // Upstream treats dangling persistent reactors as warnings. Owner and
        // dictionary pointers with the same group code remain errors.
        if (r.status == ReferenceStatus::Dangling && !r.table) {
            if (const auto *source = get_record(r.source_handle)) {
                bool reactor = false, found_reactor = false, found_other = false;
                for (const auto &p : source->pairs_) {
                    if (p.code == 102) {
                        reactor = p.value == "{ACAD_REACTORS";
                        continue;
                    }
                    if (p.code == r.code && p.value == r.target) {
                        if (reactor)
                            found_reactor = true;
                        else
                            found_other = true;
                    }
                }
                if (found_reactor && !found_other)
                    severity = AuditSeverity::Warning;
            }
        }
        auto h = parse_handle(r.source_handle), target = parse_handle(r.target);
        issue(code, severity, {AuditLocationKind::Reference, "", "", h.value_or(0), target.value_or(0)}, h,
              std::move(message), repairable);
    }
    std::uint64_t seed = 0;
    if (handle_reserved_)
        seed = next_handle_;
    else if (header_index_) {
        const auto &h = record_at({*header_index_, {}, false});
        for (std::size_t i = 0; i + 1 < h.pairs_.size(); ++i)
            if (h.pairs_[i].code == 9 && h.pairs_[i].value == "$HANDSEED" && h.pairs_[i + 1].code == 5)
                seed = handle_value(h.pairs_[i + 1].value);
    }
    if (seed <= maximum || next_handle_ <= maximum)
        issue(AuditIssueCode::InvalidHandleSeed, AuditSeverity::Warning, {}, {},
              "HANDSEED or allocator is not above every stored identity",
              maximum != std::numeric_limits<std::uint64_t>::max());
    return report;
}
RepairReport DxfDocument::repair() {
    RepairReport report;
    report.before = audit();
    DxfDocument staged = *this;
    staged.rebuild_indexes();
    if (report.before.count(AuditIssueCode::EntityIndexMismatch))
        report.actions.push_back({RepairActionKind::ReindexEntities, staged.entity_records_.size()});
    auto flags = staged.repair_attribute_definition_flags();
    if (flags)
        report.actions.push_back({RepairActionKind::ClearAttributeDefinitionFlags, flags});
    std::size_t tables = 0, entities = 0, objects = 0;
    std::string section;
    auto assign = [&](DxfRecord &r, std::size_t &count) {
        const auto h = parse_handle(r.handle());
        if ((h && !*h) || r.handle().empty()) {
            r.set_field("", r.type() == "DIMSTYLE" ? 105 : 5, hex_handle(staged.allocate_handle()));
            ++count;
        }
    };
    for (auto &v : staged.records_) {
        auto &r = std::visit([](auto &r) -> DxfRecord & { return r; }, v);
        auto type = r.type();
        if (type == "SECTION") {
            section = r.field("", 2);
            continue;
        }
        if (type == "ENDSEC") {
            section.clear();
            continue;
        }
        if (type == "TABLE" || type == "ENDTAB")
            continue;
        if (section == "TABLES")
            assign(r, tables);
        else if (section == "ENTITIES" || section == "BLOCKS")
            assign(r, entities);
        else if (section == "OBJECTS")
            assign(r, objects);
        if (auto *c = sequence(v)) {
            for (auto &child : c->children_)
                assign(child, entities);
            if (c->seqend_)
                assign(*c->seqend_, entities);
        }
    }
    // APPIDs are referenced by application name. Reassign only their duplicate
    // identities; retain every other object's handle and all incoming links.
    std::set<std::uint64_t> taken;
    for (const auto &[id, locations] : staged.handle_index_)
        for (auto &location : locations)
            if (staged.record_at(location).type() != "APPID")
                taken.insert(id);
    section.clear();
    for (auto &variant : staged.records_) {
        auto &r = std::visit([](auto &r) -> DxfRecord & { return r; }, variant);
        if (r.type() == "SECTION")
            section = r.field("", 2);
        else if (r.type() == "ENDSEC")
            section.clear();
        else if (section == "TABLES" && r.type() == "APPID") {
            auto handle = parse_handle(r.handle());
            if (handle && *handle && !taken.insert(*handle).second) {
                auto fresh = staged.allocate_handle();
                r.set_field("", 5, hex_handle(fresh));
                taken.insert(fresh);
                ++tables;
            }
        }
    }
    if (tables)
        report.actions.push_back({RepairActionKind::AssignTableHandles, tables});
    if (entities)
        report.actions.push_back({RepairActionKind::AssignEntityHandles, entities});
    if (objects)
        report.actions.push_back({RepairActionKind::AssignObjectHandles, objects});
    staged.rebuild_indexes();
    // An ambiguous missing owner is retained. Existing nonnull owners are never guessed.
    try {
        auto owners = staged.repair_entity_owners();
        if (owners)
            report.actions.push_back({RepairActionKind::RepairBlockOwners, owners});
    } catch (const std::invalid_argument &) {
    }
    if (report.before.count(AuditIssueCode::InvalidHandleSeed) || !report.actions.empty()) {
        if (staged.next_handle_ == std::numeric_limits<std::uint64_t>::max() &&
            staged.handle_index_.count(staged.next_handle_))
            throw std::overflow_error("Cannot repair exhausted handle allocator");
        staged.handle_reserved_ = true;
        report.actions.push_back({RepairActionKind::SynchronizeHandleAllocator, 1});
    }
    report.after = staged.audit();
    report.unresolved = report.after.issues;
    *this = std::move(staged);
    return report;
}
