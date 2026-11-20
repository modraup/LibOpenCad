// SPDX-License-Identifier: MPL-2.0
// Structural diagnostics and conservative repair, adapted from acadrust audit.rs.
#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace libopencad {
enum class AuditSeverity { Info, Warning, Error, Critical };
enum class AuditIssueCode {
    NullHandle,
    DuplicateHandle,
    StorageKeyMismatch,
    EntityIndexMismatch,
    ObjectMapMismatch,
    DanglingReference,
    OwnerMismatch,
    OwnerCycle,
    InvalidGeometry,
    InvalidHandleSeed,
    StaleTableKey,
    UnsupportedRecord,
    InvalidHandle
};
enum class AuditLocationKind { Document, TableEntry, Entity, Object, Reference };
struct AuditLocation {
    AuditLocationKind kind = AuditLocationKind::Document;
    std::string table, name;
    std::uint64_t handle = 0, target = 0;
};
struct AuditIssue {
    AuditIssueCode code;
    AuditSeverity severity;
    AuditLocation location;
    std::optional<std::uint64_t> handle;
    std::string message;
    bool repairable = false;
};
struct AuditReport {
    std::size_t scanned_records = 0, scanned_entities = 0, scanned_objects = 0, scanned_table_entries = 0;
    std::vector<AuditIssue> issues;
    bool is_clean() const { return issues.empty(); }
    std::size_t count(AuditIssueCode code) const {
        return std::count_if(issues.begin(), issues.end(), [&](const auto &i) { return i.code == code; });
    }
    std::size_t severity_count(AuditSeverity severity) const {
        return std::count_if(issues.begin(), issues.end(),
                             [&](const auto &i) { return i.severity == severity; });
    }
    std::size_t error_count() const {
        return severity_count(AuditSeverity::Error) + severity_count(AuditSeverity::Critical);
    }
    std::size_t warning_count() const { return severity_count(AuditSeverity::Warning); }
};
enum class RepairActionKind {
    AssignTableHandles,
    AssignEntityHandles,
    AssignObjectHandles,
    ReindexEntities,
    RepairBlockOwners,
    ClearAttributeDefinitionFlags,
    SynchronizeHandleAllocator
};
struct RepairAction {
    RepairActionKind kind;
    std::size_t count = 0;
};
struct RepairReport {
    AuditReport before, after;
    std::vector<RepairAction> actions;
    std::vector<AuditIssue> unresolved;
};
} // namespace libopencad
