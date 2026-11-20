// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/document/semantic_inventory.rs. Included from dxf_document.cpp.

namespace {

bool inventory_structural(std::string_view type) {
    // ENDBLK is stored on DXF BLOCKS but rust's DXF reader keeps the handle on
    // the block record and does not put BlockEnd in CadDocument.entities, so
    // the V1 inventory does not emit it after a DXF read.
    return type == "SECTION" || type == "TABLE" || type == "ENDTAB" || type == "ENDSEC" ||
           type == "EOF" || type == "CLASS" || type == "ENDBLK";
}

std::optional<SemanticTableKindV1> inventory_table_kind(std::string_view type) {
    if (type == "LAYER")
        return SemanticTableKindV1::Layer;
    if (type == "LTYPE")
        return SemanticTableKindV1::LineType;
    if (type == "STYLE")
        return SemanticTableKindV1::TextStyle;
    if (type == "BLOCK_RECORD")
        return SemanticTableKindV1::BlockRecord;
    if (type == "DIMSTYLE")
        return SemanticTableKindV1::DimStyle;
    if (type == "APPID")
        return SemanticTableKindV1::AppId;
    if (type == "VIEW")
        return SemanticTableKindV1::View;
    if (type == "VPORT")
        return SemanticTableKindV1::VPort;
    if (type == "UCS")
        return SemanticTableKindV1::Ucs;
    if (type == "VPORT_ENTITY_HEADER" || type == "VX_TABLE_RECORD")
        return SemanticTableKindV1::Vx;
    return std::nullopt;
}

bool inventory_entity_type(std::string_view type) {
    return type == "POINT" || type == "LINE" || type == "CIRCLE" || type == "ARC" ||
           type == "ELLIPSE" || type == "POLYLINE" || type == "LWPOLYLINE" || type == "TEXT" ||
           type == "MTEXT" || type == "SPLINE" || type == "HELIX" || type == "DIMENSION" ||
           type == "ARC_DIMENSION" || type == "LARGE_RADIAL_DIMENSION" || type == "HATCH" ||
           type == "MPOLYGON" || type == "SOLID" || type == "TRACE" || type == "3DFACE" ||
           type == "INSERT" || type == "BLOCK" || type == "RAY" ||
           type == "XLINE" || type == "VIEWPORT" || type == "ATTDEF" || type == "ATTRIB" ||
           type == "LEADER" || type == "MULTILEADER" || type == "MLINE" || type == "MESH" ||
           type == "IMAGE" || type == "WIPEOUT" || type == "3DSOLID" || type == "REGION" ||
           type == "BODY" || type == "SURFACE" || type == "PLANESURFACE" ||
           type == "EXTRUDEDSURFACE" || type == "LOFTEDSURFACE" || type == "REVOLVEDSURFACE" ||
           type == "SWEPTSURFACE" || type == "NURBSURFACE" || type == "ACAD_TABLE" ||
           type == "TOLERANCE" || type == "SHAPE" || type == "PDFUNDERLAY" ||
           type == "DWFUNDERLAY" || type == "DGNUNDERLAY" || type == "SEQEND" ||
           type == "OLE2FRAME" || type == "OLEFRAME" || type == "LIGHT" || type == "SECTIONLINE" ||
           type == "DRAWINGVIEW" || type == "CAMERA" || type == "SECTIONOBJECT" ||
           type == "ARCALIGNEDTEXT" || type == "RTEXT" || type == "POSITIONMARKER" ||
           type == "COORDINATION_MODEL" || type == "LAYOUTPRINTCONFIG" || type == "Format" ||
           type == "ACDBPOINTCLOUD" || type == "ACDBPOINTCLOUDEX" ||
           type == "ACAD_PROXY_ENTITY";
}

bool inventory_typed_object(std::string_view type) {
    return type == "DICTIONARY" || type == "ACDBDICTIONARYWDFLT" || type == "LAYOUT" ||
           type == "XRECORD" || type == "GROUP" || type == "MLINESTYLE" || type == "IMAGEDEF" ||
           type == "PDFDEFINITION" || type == "DWFDEFINITION" || type == "DGNDEFINITION" ||
           type == "PLOTSETTINGS" || type == "MLEADERSTYLE" || type == "TABLESTYLE" ||
           type == "TABLECONTENT" || type == "TABLETEMPLATE" || type == "SCALE" ||
           type == "SORTENTSTABLE" || type == "DICTIONARYVAR" || type == "VISUALSTYLE" ||
           type == "MATERIAL" || type == "IMAGEDEF_REACTOR" || type == "GEODATA" ||
           type == "SPATIAL_FILTER" || type == "RASTERVARIABLES" || type == "DBCOLOR" ||
           type == "ACDBPLACEHOLDER" || type == "WIPEOUTVARIABLES" || type == "FIELD" ||
           type == "FIELDLIST" || type == "ACAD_PROXY_OBJECT" || type == "CELLSTYLEMAP" ||
           type == "TABLEGEOMETRY";
}

bool inventory_underlay_definition(std::string_view type) {
    return type == "PDFDEFINITION" || type == "DWFDEFINITION" || type == "DGNDEFINITION";
}

const DxfRecord *inventory_appid(const DxfDocument &doc, std::string_view name) {
    for (const auto &entry : doc.symbols(SymbolTable::AppId)) {
        if (ascii_equal(entry.name, name))
            return doc.get_record(entry.handle);
    }
    return nullptr;
}

} // namespace

SummaryInfo DxfDocument::summary_info() const {
    SummaryInfo info;
    if (!header_index_)
        return info;
    const auto &header = record_at({*header_index_, std::nullopt, false});
    std::string variable;
    std::optional<std::string> pending_tag;
    for (const auto &p : header.pairs_) {
        if (p.code == 9) {
            variable = std::string(trim(p.value));
            continue;
        }
        if (p.code != 1)
            continue;
        auto text = header.decode_string(p.value);
        if (variable == "$TITLE")
            info.title = text;
        else if (variable == "$SUBJECT")
            info.subject = text;
        else if (variable == "$AUTHOR")
            info.author = text;
        else if (variable == "$KEYWORDS")
            info.keywords = text;
        else if (variable == "$COMMENTS")
            info.comments = text;
        else if (variable == "$LASTSAVEDBY")
            info.last_saved_by = text;
        else if (variable == "$REVISIONNUMBER")
            info.revision_number = text;
        else if (variable == "$HYPERLINKBASE")
            info.hyperlink_base = text;
        else if (variable == "$CUSTOMPROPERTYTAG")
            pending_tag = text;
        else if (variable == "$CUSTOMPROPERTY") {
            info.custom_properties.emplace_back(pending_tag.value_or(std::string{}), text);
            pending_tag.reset();
        }
    }
    return info;
}

std::optional<SemanticNodeV1> SemanticInventoryV1::resolve(std::uint64_t handle) const {
    if (!document_)
        return std::nullopt;
    if (handle == 0)
        return SemanticNodeV1{SemanticNodeKindV1::Document, nullptr};
    const auto *record = document_->get_record(hex_handle(handle));
    if (!record)
        return std::nullopt;
    auto type = record->type();
    if (inventory_structural(type))
        return std::nullopt;
    if (auto table = inventory_table_kind(type))
        return SemanticNodeV1{SemanticNodeKindV1::TableRecord, record, *table};
    if (inventory_entity_type(type))
        return SemanticNodeV1{SemanticNodeKindV1::Entity, record};
    return SemanticNodeV1{SemanticNodeKindV1::Object, record};
}

SemanticInventoryV1 DxfDocument::semantic_inventory_v1() const {
    SemanticInventoryV1 inventory;
    inventory.document_ = this;
    auto reference = [&](std::uint64_t handle) {
        SemanticReferenceV1 ref;
        if (auto node = inventory.resolve(handle)) {
            ref.kind = SemanticReferenceKindV1::Resolved;
            ref.node = *node;
        } else
            ref.kind = SemanticReferenceKindV1::Unresolved;
        return ref;
    };
    auto emit_relationship = [&](SemanticRelationshipKindV1 kind, SemanticReferenceV1 source,
                                 std::uint64_t target) {
        inventory.parts_.push_back(SemanticRelationshipV1{kind, std::move(source), reference(target)});
    };
    auto group_handles = [](const DxfRecord &record, std::string_view group, int code) {
        std::vector<std::uint64_t> out;
        bool active = false;
        for (const auto &p : record.pairs_) {
            if (p.code == 102) {
                active = p.value == group;
                continue;
            }
            if (active && p.code == code)
                out.push_back(handle_value(p.value));
        }
        return out;
    };
    auto as_record = [](const Record &record) -> const DxfRecord & {
        return std::visit([](const DxfRecord &item) -> const DxfRecord & { return item; }, record);
    };
    auto node_of = [&](const DxfRecord &record) {
        auto type = record.type();
        if (auto table = inventory_table_kind(type))
            return SemanticNodeV1{SemanticNodeKindV1::TableRecord, &record, *table};
        if (inventory_entity_type(type))
            return SemanticNodeV1{SemanticNodeKindV1::Entity, &record};
        return SemanticNodeV1{SemanticNodeKindV1::Object, &record};
    };

    inventory.parts_.push_back(SemanticHeaderV1{this});

    static const SemanticTableKindV1 table_order[] = {
        SemanticTableKindV1::Layer,      SemanticTableKindV1::LineType, SemanticTableKindV1::TextStyle,
        SemanticTableKindV1::BlockRecord, SemanticTableKindV1::DimStyle, SemanticTableKindV1::AppId,
        SemanticTableKindV1::View,       SemanticTableKindV1::VPort,    SemanticTableKindV1::Ucs,
        SemanticTableKindV1::Vx};
    for (auto kind : table_order) {
        for (const auto &record : records_) {
            const auto &item = as_record(record);
            auto table = inventory_table_kind(item.type());
            if (table && *table == kind)
                inventory.parts_.push_back(SemanticTableRecordV1{kind, &item});
        }
    }

    for (const auto &cls : classes())
        inventory.parts_.push_back(SemanticClassV1{cls});

    auto visit_payload_relationships = [&](const DxfRecord &item, bool entity, bool object) {
        auto source = SemanticReferenceV1{SemanticReferenceKindV1::Resolved, node_of(item)};
        if (entity)
            emit_relationship(SemanticRelationshipKindV1::Ownership, source,
                              handle_value(item.owner_handle()));
        else if (object && !inventory_underlay_definition(item.type()))
            emit_relationship(SemanticRelationshipKindV1::Ownership, source,
                              handle_value(item.owner_handle()));
        auto xdic = group_handles(item, "{ACAD_XDICTIONARY", 360);
        if (!xdic.empty())
            emit_relationship(SemanticRelationshipKindV1::ExtensionDictionary, source, xdic.front());
        for (auto reactor : group_handles(item, "{ACAD_REACTORS", 330))
            emit_relationship(SemanticRelationshipKindV1::Reactor, source, reactor);
    };

    for (const auto &record : records_) {
        const auto &item = as_record(record);
        auto type = item.type();
        if (inventory_structural(type) || inventory_table_kind(type))
            continue;
        if (inventory_entity_type(type)) {
            bool typed = true;
            if (std::holds_alternative<DxfRecord>(record) && type != "ENDBLK")
                typed = false;
            inventory.parts_.push_back(SemanticEntityV1{typed, &item});
            visit_payload_relationships(item, true, false);
        }
    }

    for (const auto &record : records_) {
        const auto &item = as_record(record);
        auto type = item.type();
        if (inventory_structural(type) || inventory_table_kind(type) || inventory_entity_type(type))
            continue;
        bool typed = inventory_typed_object(type) || !std::holds_alternative<DxfRecord>(record);
        inventory.parts_.push_back(SemanticObjectV1{typed, &item});
        visit_payload_relationships(item, false, true);
        for (const auto &data : item.extended_data()) {
            SemanticNonEntityXDataV1 part;
            part.owner = SemanticReferenceV1{SemanticReferenceKindV1::Resolved, node_of(item)};
            part.application = inventory_appid(*this, data.application_name);
            part.values = data.values;
            inventory.parts_.push_back(std::move(part));
        }
    }

    for (const auto &record : records_) {
        const auto &item = as_record(record);
        auto type = item.type();
        if (!inventory_table_kind(type))
            continue;
        auto source = SemanticReferenceV1{SemanticReferenceKindV1::Resolved, node_of(item)};
        auto xdic = group_handles(item, "{ACAD_XDICTIONARY", 360);
        if (!xdic.empty())
            emit_relationship(SemanticRelationshipKindV1::ExtensionDictionary, source, xdic.front());
        for (auto reactor : group_handles(item, "{ACAD_REACTORS", 330))
            emit_relationship(SemanticRelationshipKindV1::Reactor, source, reactor);
        for (const auto &data : item.extended_data()) {
            SemanticNonEntityXDataV1 part;
            part.owner = source;
            part.application = inventory_appid(*this, data.application_name);
            part.values = data.values;
            inventory.parts_.push_back(std::move(part));
        }
    }

    inventory.parts_.push_back(SemanticSummaryV1{summary_info()});
    return inventory;
}
