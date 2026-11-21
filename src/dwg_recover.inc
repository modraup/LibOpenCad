// SPDX-License-Identifier: MPL-2.0
// Follows acadrust DwgReader::recover and the conservative repairs in audit.rs.
namespace {
template <class Visit> void visit_recovery_entities(const DwgR2000Objects &objects, Visit visit) {
    auto visit_map = [&](const auto &entities) {
        for (const auto &[handle, entity] : entities)
            visit(handle, entity.properties);
    };
    visit_map(objects.lines);
    visit_map(objects.circles);
    visit_map(objects.arcs);
    visit_map(objects.texts);
    visit_map(objects.inserts);
    visit_map(objects.modelers);
}

AuditReport audit_dwg_recovery(const DwgR2000Objects &objects, unsigned version) {
    AuditReport report;
    report.scanned_records = objects.records.size();
    report.scanned_table_entries = objects.symbols.size();
    if (version == 1009) {
        report.scanned_entities = report.scanned_records;
        for (const auto &[type, table] : objects.legacy_symbol_names)
            report.scanned_table_entries += table.size();
        return report;
    }
    auto reference = [&](std::uint64_t from, std::uint64_t target, const char *message) {
        if (target && !objects.records.count(target))
            report.issues.push_back({AuditIssueCode::DanglingReference, AuditSeverity::Warning,
                                     {AuditLocationKind::Reference, {}, {}, from, target}, from, message});
    };
    std::set<std::uint64_t> claimed;
    for (const auto &[handle, symbol] : objects.symbols) {
        for (auto member : symbol.members) {
            reference(handle, member, "Missing DWG block member");
            claimed.insert(member);
        }
        reference(handle, symbol.first, "Missing first DWG block member");
        reference(handle, symbol.last, "Missing last DWG block member");
    }
    visit_recovery_entities(objects, [&](auto handle, const DwgR2000EntityProperties &properties) {
        ++report.scanned_entities;
        reference(handle, lifecycle_owner(objects, properties), "Missing DWG entity owner");
        if (!properties.mode && !properties.owner && claimed.count(handle))
            report.issues.push_back({AuditIssueCode::OwnerMismatch, AuditSeverity::Warning,
                                     {AuditLocationKind::Entity, {}, {}, handle}, handle,
                                     "DWG block member is missing its explicit owner"});
        reference(handle, properties.layer, "Missing DWG entity layer");
        reference(handle, properties.extension_dictionary, "Missing DWG extension dictionary");
        if (version < 1018) {
            reference(handle, properties.previous, "Missing previous DWG block member");
            reference(handle, properties.next, "Missing next DWG block member");
        }
    });
    report.scanned_objects = report.scanned_records - report.scanned_table_entries - report.scanned_entities;
    return report;
}

// A linear block stores only its first and last members. A skipped member can
// be bypassed when exactly one survivor's previous link identifies the continuation.
void repair_linear_block(const LifecycleStorage &storage, const DwgR2000Objects &objects,
                         const std::set<std::uint64_t> &dropped, std::uint64_t block,
                         const DwgSymbolRecord &symbol, const BlockBits &bits,
                         const std::vector<std::uint64_t> &owned,
                         LifecycleEdits &edits, std::size_t &pruned) {
    if (!symbol.first || !symbol.last || bits.external)
        return;
    std::map<std::uint64_t, OwnershipRecord> entries;
    std::map<std::uint64_t, std::vector<std::uint64_t>> following;
    for (auto id : owned) {
        auto entry = ownership_record(storage.body(objects, id), objects.records.at(id), id, storage.version);
        following[entry.props.previous].push_back(id);
        entries.emplace(id, std::move(entry));
    }
    std::vector<std::uint64_t> members;
    std::set<std::uint64_t> visited;
    std::size_t removed = 0;
    for (auto handle = symbol.first; handle && visited.insert(handle).second;) {
        if (dropped.count(handle)) {
            ++removed;
            if (handle == symbol.last)
                break;
            const auto next = following.find(handle);
            if (next == following.end() || next->second.size() != 1)
                return;
            handle = next->second.front();
        } else {
            const auto entry = entries.find(handle);
            if (entry == entries.end())
                return;
            members.push_back(handle);
            if (handle == symbol.last)
                break;
            handle = entry->second.props.next;
        }
    }
    if (!removed || !visited.count(symbol.last))
        return;

    LifecycleEdits repaired;
    repaired[block] = with_members(storage.body(objects, block), bits, storage.version, members);
    for (std::size_t i = 0; i < members.size(); ++i) {
        auto handle = members[i];
        const auto &entry = entries.at(handle);
        auto prev = i ? members[i - 1] : 0;
        auto next = i + 1 < members.size() ? members[i + 1] : 0;
        if (prev != entry.props.previous || next != entry.props.next)
            repaired[handle] = with_owner(storage.body(objects, handle), entry, handle, storage.version,
                                          entry.props.mode, block, prev, next);
    }
    edits.merge(repaired);
    pruned += removed;
}

LifecycleEdits recovery_edits(const LifecycleStorage &storage, const DwgR2000Objects &objects,
                             const std::set<std::uint64_t> &dropped, std::size_t &pruned, std::size_t &owners) {
    LifecycleEdits edits;
    std::map<std::uint64_t, std::uint64_t> membership;
    std::map<std::uint64_t, std::vector<std::uint64_t>> linear_owners;
    if (storage.version < 1018 && !dropped.empty())
        visit_recovery_entities(objects, [&](auto handle, const DwgR2000EntityProperties &properties) {
            linear_owners[lifecycle_owner(objects, properties)].push_back(handle);
        });
    for (const auto &[block, symbol] : objects.symbols) {
        if (symbol.type != 49)
            continue;
        auto members = symbol.members;
        auto count = members.size();
        members.erase(std::remove_if(members.begin(), members.end(), [&](auto id) { return dropped.count(id); }),
                      members.end());
        if (members.size() != count || (storage.version < 1018 && !dropped.empty())) {
            try {
                BlockBits bits;
                auto record = objects.records.at(block);
                auto body = storage.body(objects, block);
                read_symbol(body, record, block, storage.version, storage.page, &bits);
                if (storage.version < 1018)
                    repair_linear_block(storage, objects, dropped, block, symbol, bits, linear_owners[block],
                                        edits, pruned);
                else if (!bits.external) {
                    edits[block] = with_members(body, bits, storage.version, members);
                    pruned += count - members.size();
                }
            } catch (const std::invalid_argument &) {
                // Keep ambiguous or unrecognized ownership data for the audit.
            }
        }
        for (auto member : members) {
            auto [entry, inserted] = membership.emplace(member, block);
            if (!inserted && entry->second != block)
                entry->second = 0;
        }
    }
    if (storage.version >= 1018)
        visit_recovery_entities(objects, [&](auto handle, const DwgR2000EntityProperties &properties) {
            auto member = membership.find(handle);
            if (properties.mode || properties.owner || member == membership.end() || !member->second)
                return;
            try {
                const auto body = storage.body(objects, handle);
                const auto entry = ownership_record(body, objects.records.at(handle), handle, storage.version);
                edits[handle] = with_owner(body, entry, handle, storage.version, 0, member->second, 0, 0);
                ++owners;
            } catch (const std::invalid_argument &) {
                // Leave the stored owner untouched if the layout cannot be rewritten safely.
            }
        });
    return edits;
}

void recovery_failure(DwgReadStatistics &stats) {
    if (stats.has_usable_drawing_data())
        return;
    std::string details = "the reader produced no usable drawing records";
    if (!stats.diagnostics.empty())
        details += " (" + std::to_string(stats.diagnostics.size()) + " diagnostics)";
    throw DwgRecoveryError(std::move(details), std::move(stats));
}
} // namespace

detail::DwgReadData detail::recover_dwg_bytes(std::string &bytes, DwgReadStatistics &stats, RepairReport &repair,
                                              std::optional<std::string_view> password) {
    auto read = read_dwg_source(bytes, stats, password);
    recovery_failure(stats);

    const auto version = unsigned(dxf_version_from_string(stats.source_version));
    auto &result = read.data;
    auto &objects = result.objects;
    if (version == 1009) {
        repair.before = repair.after = audit_dwg_recovery(objects, version);
        return std::move(result);
    }

    const auto source = std::string_view(bytes);
    const auto &index = read.index;
    auto report = [&](const char *code, const char *section, DwgReadStage stage, const std::exception &error) {
        read_diagnostic(stats, {code, error.what(), section, stage, {}, {}, {}, {}});
    };
    repair.before = audit_dwg_recovery(objects, version);
    std::set<std::uint64_t> dropped;
    for (const auto &[handle, offset] : index.object_offsets)
        if (!objects.records.count(handle))
            dropped.insert(handle);
    std::size_t pruned = 0, owners = 0;
    LifecycleStorage storage(source, std::move(read.object_section));
    auto edits = recovery_edits(storage, objects, dropped, pruned, owners);
    std::optional<std::string> header;
    std::uint64_t next = 0;
    try {
        auto changed = read.header ? update_symbol_seed(*read.header, version, index.object_offsets.rbegin()->first, next, false)
                                     : std::string{};
        if (!changed.empty()) {
            header = std::move(changed);
            repair.before.issues.push_back({AuditIssueCode::InvalidHandleSeed, AuditSeverity::Error, {}, {},
                                            "DWG handle seed is below live or discarded handles", true});
        }
    } catch (const std::logic_error &error) {
        report("header-decode-failed", "AcDb:Header", DwgReadStage::Header, error);
    }
    if (!dropped.empty() || read.handle_checksum_damaged || !edits.empty() || header) {
        try {
            require(!read.password_protected, "Secured DWG rewrite unsupported");
            const LifecycleRecoverySource recovered_source{storage, index};
            auto repaired = lifecycle_rewrite(source, objects, edits, {}, {}, header, next, &recovered_source);
            auto verified = read_dwg_objects(repaired);
            require(verified.records.size() == objects.records.size(), "DWG recovery changed the survivor count");
            bytes = std::move(repaired);
            result.objects = std::move(verified);
            if (!dropped.empty() || read.handle_checksum_damaged)
                repair.actions.push_back({RepairActionKind::RebuildDwgHandleMap, dropped.size()});
            if (pruned)
                repair.actions.push_back({RepairActionKind::RemoveInvalidBlockMembers, pruned});
            if (owners)
                repair.actions.push_back({RepairActionKind::RepairBlockOwners, owners});
            if (header)
                repair.actions.push_back({RepairActionKind::SynchronizeHandleAllocator, 1});
        } catch (const std::logic_error &error) {
            result.writable = false;
            report("recovery-rewrite-failed", "", DwgReadStage::Objects, error);
        }
    }
    repair.after = audit_dwg_recovery(result.objects, version);
    if (!result.writable && header)
        repair.after.issues.push_back(repair.before.issues.back());
    repair.unresolved = repair.after.issues;
    return std::move(result);
}
