// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/document/semantic_inventory.rs. Visits logical document
// content without exposing caches, source paths, handle allocation, or raw
// DWG/DXF bytes as inventory components.
#pragma once

#include "proxy.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifndef LIBOPENCAD_IN_DXF_DOCUMENT
namespace libopencad {
#endif

class DxfDocument;
class DxfRecord;
struct XDataValue;

// Latest semantic-inventory contract supported by this library.
constexpr std::uint16_t SEMANTIC_INVENTORY_VERSION = 1;

enum class SemanticTableKindV1 {
    Layer,
    LineType,
    TextStyle,
    BlockRecord,
    DimStyle,
    AppId,
    View,
    VPort,
    Ucs,
    Vx
};

struct SemanticTableRecordV1 {
    SemanticTableKindV1 kind = SemanticTableKindV1::Layer;
    const DxfRecord *record = nullptr;
};

enum class SemanticNodeKindV1 { Document, Entity, Object, TableRecord };

struct SemanticNodeV1 {
    SemanticNodeKindV1 kind = SemanticNodeKindV1::Document;
    const DxfRecord *record = nullptr;
    SemanticTableKindV1 table = SemanticTableKindV1::Layer;
};

enum class SemanticReferenceKindV1 { Resolved, Unresolved };

struct SemanticReferenceV1 {
    SemanticReferenceKindV1 kind = SemanticReferenceKindV1::Unresolved;
    SemanticNodeV1 node{};
};

enum class SemanticRelationshipKindV1 { Ownership, ExtensionDictionary, Reactor };

struct SemanticEntityV1 {
    bool typed = true;
    const DxfRecord *record = nullptr;
};

struct SemanticObjectV1 {
    bool typed = true;
    const DxfRecord *record = nullptr;
};

// Document summary (DWGPROPS / DXF HEADER $TITLE…). Always present; empty
// strings are the rust SummaryInfo::default equivalent.
struct SummaryInfo {
    std::string title, subject, author, keywords, comments, last_saved_by, revision_number,
        hyperlink_base;
    std::vector<std::pair<std::string, std::string>> custom_properties;
};

struct SemanticHeaderV1 {
    const DxfDocument *document = nullptr;
};
struct SemanticClassV1 {
    ClassDefinition definition;
};
struct SemanticSummaryV1 {
    SummaryInfo info;
};
struct SemanticPreviewV1 {
    std::string format;
    std::vector<std::uint8_t> data;
};
struct SemanticRelationshipV1 {
    SemanticRelationshipKindV1 kind = SemanticRelationshipKindV1::Ownership;
    SemanticReferenceV1 source;
    SemanticReferenceV1 target;
};
struct SemanticNonEntityXDataV1 {
    SemanticReferenceV1 owner;
    const DxfRecord *application = nullptr;
    std::optional<std::vector<XDataValue>> values;
};

using SemanticPartV1 =
    std::variant<SemanticHeaderV1, SemanticTableRecordV1, SemanticClassV1, SemanticEntityV1,
                 SemanticObjectV1, SemanticSummaryV1, SemanticPreviewV1, SemanticRelationshipV1,
                 SemanticNonEntityXDataV1>;

// Read-only V1 view over logical document content. Pointers are valid for the
// lifetime of the source DxfDocument.
class SemanticInventoryV1 {
public:
    std::optional<SemanticNodeV1> resolve(std::uint64_t handle) const;
    template <class Fn> void visit(Fn &&fn) const {
        for (const auto &part : parts_)
            fn(part);
    }

private:
    friend class DxfDocument;
    const DxfDocument *document_ = nullptr;
    std::vector<SemanticPartV1> parts_;
};

#ifndef LIBOPENCAD_IN_DXF_DOCUMENT
} // namespace libopencad
#endif
