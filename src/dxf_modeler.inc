// SPDX-License-Identifier: MPL-2.0
// Port of acadrust DXF modeler geometry and ACDSDATA reader/writer paths.
namespace {
const char *modeler_type(ModelerKind kind) {
    switch (kind) {
    case ModelerKind::Solid3D:
        return "3DSOLID";
    case ModelerKind::Region:
        return "REGION";
    case ModelerKind::Body:
        return "BODY";
    case ModelerKind::Surface:
        return "SURFACE";
    case ModelerKind::PlaneSurface:
        return "PLANESURFACE";
    case ModelerKind::ExtrudedSurface:
        return "EXTRUDEDSURFACE";
    case ModelerKind::LoftedSurface:
        return "LOFTEDSURFACE";
    case ModelerKind::RevolvedSurface:
        return "REVOLVEDSURFACE";
    case ModelerKind::SweptSurface:
        return "SWEPTSURFACE";
    case ModelerKind::NurbSurface:
        return "NURBSURFACE";
    }
    throw std::invalid_argument("Invalid modeler kind");
}
bool modeler_type(std::string_view type) {
    return type == "3DSOLID" || type == "REGION" || type == "BODY" || type == "SURFACE" ||
           type == "PLANESURFACE" || type == "EXTRUDEDSURFACE" || type == "LOFTEDSURFACE" ||
           type == "REVOLVEDSURFACE" || type == "SWEPTSURFACE" || type == "NURBSURFACE";
}
// Involution used by ACIS Version1. Transport caret escaping is handled once
// by DxfRecord::encode_string/decode_string, independently of this cipher.
std::string modeler_cipher(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t' || c >= 128)
            out += char(c);
        else if (c >= '@' && c <= '_')
            out += char(159 - c);
        else
            out += char(c ^ 0x5f);
    }
    return out;
}
std::vector<std::uint8_t> modeler_unhex(std::string_view text) {
    text = trim(text);
    if (text.size() % 2)
        throw std::invalid_argument("Odd modeler binary chunk length");
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw std::invalid_argument("Invalid modeler binary chunk");
    };
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2)
        out.push_back(std::uint8_t(digit(text[i]) * 16 + digit(text[i + 1])));
    return out;
}
std::string modeler_hex(const std::uint8_t *data, std::size_t size) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}
} // namespace
ModelerKind DxfModelerEntity::kind() const {
    auto t = type();
    for (auto k : {ModelerKind::Solid3D, ModelerKind::Region, ModelerKind::Body, ModelerKind::Surface,
                   ModelerKind::PlaneSurface, ModelerKind::ExtrudedSurface, ModelerKind::LoftedSurface,
                   ModelerKind::RevolvedSurface, ModelerKind::SweptSurface, ModelerKind::NurbSurface})
        if (t == modeler_type(k))
            return k;
    throw std::invalid_argument("Not a modeler entity");
}
std::string DxfModelerEntity::uid() const { return text_field("AcDbModelerGeometry", 2); }
std::string DxfModelerEntity::history_handle() const { return field("AcDb3dSolid", 350); }
std::pair<std::int16_t, std::int16_t> DxfModelerEntity::isolines() const {
    return {short_field("AcDbSurface", 71, 0), short_field("AcDbSurface", 72, 0)};
}
void DxfModelerEntity::set_isolines(std::int16_t u, std::int16_t v) {
    if (kind() < ModelerKind::Surface)
        throw std::invalid_argument("Isolines require a surface");
    auto copy = *this;
    copy.set_short_field("AcDbSurface", 71, u);
    copy.set_short_field("AcDbSurface", 72, v);
    *this = std::move(copy);
}
std::optional<acis::SatDocument> DxfDocument::modeler_geometry(std::uint64_t handle) const {
    auto found = handle_index_.find(handle);
    if (found == handle_index_.end())
        return {};
    if (!handle || found->second.size() != 1)
        throw std::invalid_argument("Ambiguous modeler entity handle");
    const auto &location = found->second.front();
    if (location.vertex || location.seqend)
        return {};
    const auto &r = record_at(location);
    if (!modeler_type(r.type()))
        return {};
    std::optional<std::vector<std::uint8_t>> sab;
    std::string section;
    for (const auto &entry : records_) {
        const auto &data = std::visit([](const DxfRecord &r) -> const DxfRecord & { return r; }, entry);
        if (data.type() == "SECTION") {
            section = data.text_field("", 2);
            continue;
        }
        if (data.type() == "ENDSEC") {
            section.clear();
            continue;
        }
        if (section != "ACDSDATA" || data.type() != "ACDSRECORD")
            continue;
        bool target = false;
        for (const auto &p : data.pairs_)
            if (p.code == 320 && parse_handle(p.value) == handle)
                target = true;
        if (!target)
            continue;
        std::optional<std::uint64_t> owner;
        std::optional<std::size_t> declared;
        std::vector<std::uint8_t> bytes;
        bool active = false, has_asm = false;
        for (const auto &p : data.pairs_) {
            if (p.code == 320) {
                auto h = parse_handle(p.value);
                if (!h || owner)
                    throw std::invalid_argument("Ambiguous ACDSDATA owner");
                owner = *h;
            } else if (p.code == 2) {
                active = data.decode_string(p.value) == "ASM_Data";
                if (active) {
                    if (has_asm)
                        throw std::invalid_argument("Duplicate ASM_Data property");
                    has_asm = true;
                }
            } else if (active && p.code == 94) {
                auto size = detail::as_i32_bits(p.value, data.binary_);
                if (!size || *size > INT32_MAX || declared)
                    throw std::invalid_argument("Invalid ACDSDATA length");
                declared = *size;
            } else if (active && p.code == 310) {
                auto chunk = data.binary_ ? std::vector<std::uint8_t>(p.value.begin(), p.value.end())
                                          : modeler_unhex(p.value);
                bytes.insert(bytes.end(), chunk.begin(), chunk.end());
            }
        }
        if (owner != handle || !has_asm)
            continue;
        if (sab)
            throw std::invalid_argument("Multiple modeler payloads for one entity");
        if (!declared || *declared != bytes.size())
            throw std::invalid_argument("ACDSDATA modeler length mismatch");
        sab = std::move(bytes);
    }
    if (sab) {
        if (sab->empty())
            return {};
        auto result = acis::SabReader::read_with_consumed(*sab);
        if (result.second != sab->size())
            throw std::invalid_argument("Trailing modeler SAB data");
        return std::move(result.first);
    }
    std::string text;
    bool active = false;
    unsigned depth = 0;
    for (const auto &p : r.pairs_) {
        if (p.code == 102) {
            if (p.value == "}") {
                if (depth)
                    --depth;
            } else if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            continue;
        }
        if (depth)
            continue;
        if (p.code == 1001 || p.code == 101)
            break;
        if (p.code == 100) {
            active = p.value == "AcDbModelerGeometry";
            continue;
        }
        if (active && (p.code == 1 || p.code == 3)) {
            if (p.code == 3 && !text.empty() && text.back() == '\n')
                text.pop_back();
            text += r.decode_string(p.value);
            text += '\n';
        }
    }
    if (text.empty())
        return {};
    if (r.short_field("AcDbModelerGeometry", 70, 1) == 1)
        text = modeler_cipher(text);
    std::istringstream lines(text);
    std::string line, content;
    while (std::getline(lines, line)) {
        if (line.rfind("End-of-ACIS-data", 0) == 0 || line.rfind("End-of-ASM-data", 0) == 0)
            break;
        content += line + '\n';
    }
    if (content.find_first_not_of(" \t\r\n") == std::string::npos)
        return {};
    return acis::SatDocument::parse(text);
}
std::optional<std::size_t> DxfDocument::modeler_payload_record(std::uint64_t handle) const {
    (void)modeler_geometry(handle); // Validate cardinality, lengths and SAB framing first.
    std::optional<std::size_t> result;
    std::string section;
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto &r = record_at({i, std::nullopt, false});
        if (r.type() == "SECTION")
            section = r.text_field("", 2);
        else if (r.type() == "ENDSEC")
            section.clear();
        else if (section == "ACDSDATA" && r.type() == "ACDSRECORD") {
            bool match = false, asm_data = false;
            for (const auto &p : r.pairs_) {
                if (p.code == 320 && parse_handle(p.value) == handle)
                    match = true;
                if (p.code == 2 && r.decode_string(p.value) == "ASM_Data")
                    asm_data = true;
            }
            if (match && asm_data) {
                if (result)
                    throw std::invalid_argument("Duplicate modeler payload");
                result = i;
            }
        }
    }
    return result;
}
void DxfDocument::set_modeler_geometry(std::uint64_t handle, const acis::SatDocument &geometry) {
    auto copy = *this;
    auto found = copy.handle_index_.find(handle);
    if (!handle || found == copy.handle_index_.end() || found->second.size() != 1)
        throw std::invalid_argument("Missing or ambiguous modeler handle");
    auto location = found->second.front();
    if (location.vertex || location.seqend)
        throw std::invalid_argument("Modeler handle is a compound child");
    auto *entity = std::get_if<DxfModelerEntity>(&copy.records_[location.record]);
    if (!entity)
        throw std::invalid_argument("Not a supported modeler entity");
    if (copy.version_ < DxfVersion::AC1012)
        throw std::invalid_argument("Modeler geometry requires R13 or newer");
    // Validate association cardinality and payload lengths before changing any record.
    (void)copy.modeler_geometry(handle);
    const bool modern = copy.version_ >= DxfVersion::AC1027;
    if (!modern) {
        std::string section;
        for (const auto &entry : copy.records_) {
            const auto &r = std::visit([](const DxfRecord &r) -> const DxfRecord & { return r; }, entry);
            if (r.type() == "SECTION")
                section = r.text_field("", 2);
            else if (r.type() == "ENDSEC")
                section.clear();
            else if (section == "ACDSDATA" && r.type() == "ACDSRECORD")
                for (const auto &p : r.pairs_)
                    if (p.code == 320 && parse_handle(p.value) == handle)
                        throw std::invalid_argument("ACDSDATA modeler payload requires R2013 or newer");
        }
    }
    std::vector<DxfRecord::Pair> fields;
    if (modern) {
        fields = {{290, copy.binary_ ? detail::binary_value_from_text(290, "0") : "0"},
                  {2, entity->encode_string(entity->uid().empty() ? "{00000000-0000-0000-0000-000000000000}"
                                                                  : entity->uid())}};
    } else {
        fields.push_back({70, copy.binary_ ? detail::binary_short(1) : "1"});
        std::istringstream lines(geometry.to_sat_string());
        std::string line;
        while (std::getline(lines, line)) {
            // Group 3 continues a group 1 line, preserving counted strings and headers.
            // Keep chunk boundaries on UTF-8 code points.
            std::size_t at = 0;
            do {
                auto end = std::min(at + 2049, line.size());
                while (end < line.size() && end > at &&
                       (static_cast<unsigned char>(line[end]) & 0xc0) == 0x80)
                    --end;
                if (end == at && end < line.size())
                    throw std::invalid_argument("Invalid SAT UTF-8 boundary");
                fields.push_back({at == 0 ? 1 : 3, entity->encode_string(modeler_cipher(
                                                       std::string_view(line).substr(at, end - at)))});
                at = end;
            } while (at < line.size());
        }
    }
    // Replace only modeler payload fields, retaining private groups and every
    // later subclass (solid history, surface construction data, XDATA).
    std::vector<DxfRecord::Pair> pairs;
    bool active = false, inserted = false;
    unsigned depth = 0;
    for (const auto &p : entity->pairs_) {
        if (p.code == 102) {
            pairs.push_back(p);
            if (p.value == "}") {
                if (depth)
                    --depth;
            } else if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            continue;
        }
        if (depth) {
            pairs.push_back(p);
            continue;
        }
        if (p.code == 100) {
            active = p.value == "AcDbModelerGeometry";
            pairs.push_back(p);
            if (active) {
                if (inserted)
                    throw std::invalid_argument("Duplicate modeler subclass");
                pairs.insert(pairs.end(), fields.begin(), fields.end());
                inserted = true;
            }
            continue;
        }
        if (p.code == 1001 || p.code == 101)
            active = false;
        if (active && (p.code == 1 || p.code == 3 || p.code == 70 || p.code == 290 || p.code == 2))
            continue;
        pairs.push_back(p);
    }
    if (!inserted)
        throw std::invalid_argument("Missing modeler subclass");
    entity->pairs_ = std::move(pairs);
    if (modern) {
        auto bytes = acis::SabWriter::write(geometry);
        if (bytes.size() > INT32_MAX)
            throw std::length_error("Modeler SAB is too large");
        std::string section;
        std::optional<std::size_t> existing, end_section;
        std::optional<std::int32_t> asm_schema;
        std::unordered_set<std::uint32_t> schema_ids;
        for (std::size_t i = 0; i < copy.records_.size(); ++i) {
            auto &r = std::visit([](DxfRecord &r) -> DxfRecord & { return r; }, copy.records_[i]);
            if (r.type() == "SECTION")
                section = r.text_field("", 2);
            else if (r.type() == "ENDSEC") {
                if (section == "ACDSDATA")
                    end_section = i;
                section.clear();
            } else if (section == "ACDSDATA" && r.type() == "ACDSSCHEMA") {
                auto id = detail::as_i32_bits(r.field("", 90), r.binary_);
                if (!id || *id > INT32_MAX || !schema_ids.insert(*id).second)
                    throw std::invalid_argument("Missing, invalid or duplicate ACDS schema index");
                if (r.text_field("", 1) == "AcDb3DSolid_ASM_Data") {
                    if (asm_schema)
                        throw std::invalid_argument("Duplicate ASM schema");
                    asm_schema = static_cast<std::int32_t>(*id);
                }
            } else if (section == "ACDSDATA" && r.type() == "ACDSRECORD") {
                for (const auto &p : r.pairs_)
                    if (p.code == 320 && parse_handle(p.value) == handle) {
                        if (existing)
                            throw std::invalid_argument("Duplicate modeler ACDS record");
                        existing = i;
                    }
            }
        }
        auto property = [&](DxfRecord &r, int code, std::string value) {
            r.pairs_.push_back({code, copy.binary_ && !detail::is_string_value(code)
                                          ? detail::binary_value_from_text(code, value)
                                          : r.encode_string(value)});
        };
        if (end_section && !existing && !asm_schema)
            throw std::invalid_argument("Existing ACDSDATA has no ASM schema");
        auto blob = copy.make_record("ACDSRECORD");
        property(blob, 90, std::to_string(asm_schema.value_or(1)));
        property(blob, 2, "AcDbDs::ID");
        property(blob, 280, "10");
        property(blob, 320, hex_handle(handle));
        property(blob, 2, "ASM_Data");
        property(blob, 280, "15");
        property(blob, 94, std::to_string(bytes.size()));
        for (std::size_t i = 0; i < bytes.size(); i += 127) {
            auto n = std::min<std::size_t>(127, bytes.size() - i);
            blob.pairs_.push_back({310, copy.binary_
                                            ? std::string(reinterpret_cast<const char *>(bytes.data() + i), n)
                                            : modeler_hex(bytes.data() + i, n)});
        }
        if (existing) {
            auto &old = std::visit([](DxfRecord &r) -> DxfRecord & { return r; }, copy.records_[*existing]);
            // Retain other named properties and metadata on the same record.
            bool asm_field = false, done = false;
            std::vector<DxfRecord::Pair> merged;
            for (const auto &p : old.pairs_) {
                if (p.code == 2) {
                    asm_field = old.decode_string(p.value) == "ASM_Data";
                    if (asm_field) {
                        if (done)
                            throw std::invalid_argument("Duplicate ASM_Data");
                        done = true;
                        merged.insert(merged.end(), blob.pairs_.begin() + 5, blob.pairs_.end());
                        continue;
                    }
                }
                if (!asm_field)
                    merged.push_back(p);
            }
            if (!done)
                throw std::invalid_argument("Missing ASM_Data property");
            old.pairs_ = std::move(merged);
        } else if (end_section)
            copy.records_.insert(copy.records_.begin() + *end_section, std::move(blob));
        else {
            std::vector<Record> additions;
            auto start = copy.make_record("SECTION");
            property(start, 2, "ACDSDATA");
            property(start, 70, "2");
            property(start, 71, "2");
            additions.push_back(std::move(start));
            for (int schema = 0; schema < 6; ++schema) {
                auto r = copy.make_record("ACDSSCHEMA");
                property(r, 90, std::to_string(schema));
                if (schema < 2) {
                    property(r, 1, schema ? "AcDb3DSolid_ASM_Data" : "AcDb_Thumbnail_Schema");
                    property(r, 2, "AcDbDs::ID");
                    property(r, 280, "10");
                    property(r, 91, "8");
                    property(r, 2, schema ? "ASM_Data" : "Thumbnail_Data");
                    property(r, 280, "15");
                    property(r, 91, "0");
                    for (int k = 2; k < 6; ++k) {
                        property(r, 101, "ACDSRECORD");
                        if (k < 4)
                            property(r, 95, std::to_string(schema));
                        else
                            property(r, 1, "AcDbDs::ID");
                        property(r, 90, std::to_string(k));
                        property(r, 2,
                                 k == 2   ? "AcDbDs::TreatedAsObjectData"
                                 : k == 3 ? "AcDbDs::Legacy"
                                 : k == 4 ? "AcDs:Indexable"
                                          : "AcDbDs::HandleAttribute");
                        property(r, 280, k == 5 ? "7" : "1");
                        property(r, k == 5 ? 282 : 291, "1");
                    }
                } else {
                    property(r, 1,
                             schema == 2   ? "AcDbDs::TreatedAsObjectDataSchema"
                             : schema == 3 ? "AcDbDs::LegacySchema"
                             : schema == 4 ? "AcDbDs::IndexedPropertySchema"
                                           : "AcDbDs::HandleAttributeSchema");
                    property(r, 2,
                             schema == 2   ? "AcDbDs::TreatedAsObjectData"
                             : schema == 3 ? "AcDbDs::Legacy"
                             : schema == 4 ? "AcDs:Indexable"
                                           : "AcDbDs::HandleAttribute");
                    property(r, 280, schema == 5 ? "7" : "1");
                    property(r, 91, schema == 5 ? "1" : "0");
                    if (schema == 5)
                        property(r, 284, "1");
                }
                additions.push_back(std::move(r));
            }
            additions.push_back(std::move(blob));
            additions.push_back(copy.make_record("ENDSEC"));
            auto pos = copy.records_.size();
            for (std::size_t i = 0; i < copy.records_.size(); ++i)
                if (std::visit([](const DxfRecord &r) { return r.type() == "EOF"; }, copy.records_[i])) {
                    pos = i;
                    break;
                }
            copy.records_.insert(copy.records_.begin() + pos, std::make_move_iterator(additions.begin()),
                                 std::make_move_iterator(additions.end()));
        }
    }
    copy.rebuild_indexes();
    *this = std::move(copy);
}
void DxfDocument::transform_modeler(std::uint64_t handle, const Transform &transform) {
    auto g = modeler_geometry(handle);
    if (!g)
        throw std::invalid_argument("Missing modeler geometry");
    g->apply_transform(transform);
    auto staged = *this;
    staged.set_modeler_geometry(handle, *g);
    staged.for_each<DxfModelerEntity>([&](auto &e) {
        if (parse_handle(e.handle()) != handle || e.kind() != ModelerKind::LoftedSurface)
            return;
        auto definition = std::get<LoftedSurfaceDefinition>(e.surface_definition());
        const auto previous = definition.loft_transform;
        for (std::size_t i = 0; i < 16; ++i) {
            double value = 0;
            for (std::size_t inner = 0; inner < 4; ++inner)
                value += transform.matrix.m[i % 4][inner] * previous[(i / 4) * 4 + inner];
            definition.loft_transform[i] = value;
        }
        e.set_surface_definition(definition);
    });
    *this = std::move(staged);
}
std::uint64_t DxfDocument::add_modeler_entity(ModelerKind kind, const acis::SatDocument &geometry,
                                              const EntityOptions &options) {
    if (version_ < DxfVersion::AC1012)
        throw std::invalid_argument("Modeler geometry requires R13 or newer");
    if ((kind >= ModelerKind::Surface) && version_ < DxfVersion::AC1021)
        throw std::invalid_argument("Surface entities require R2007 or newer");
    auto copy = *this;
    DxfModelerEntity e;
    static_cast<DxfEntity &>(e) = copy.make_entity(modeler_type(kind), "AcDbModelerGeometry", options);
    if (kind == ModelerKind::Solid3D && version_ >= DxfVersion::AC1021) {
        e.pairs_.push_back({100, "AcDb3dSolid"});
        e.pairs_.push_back({350, "0"});
    }
    if (kind >= ModelerKind::Surface) {
        e.pairs_.push_back({100, "AcDbSurface"});
        e.pairs_.push_back({71, binary_ ? detail::binary_short(4) : "4"});
        e.pairs_.push_back({72, binary_ ? detail::binary_short(4) : "4"});
    }
    switch (kind) {
    case ModelerKind::ExtrudedSurface:
        e.set_surface_definition(ExtrudedSurfaceDefinition{});
        break;
    case ModelerKind::LoftedSurface:
        e.set_surface_definition(LoftedSurfaceDefinition{});
        break;
    case ModelerKind::RevolvedSurface:
        e.set_surface_definition(RevolvedSurfaceDefinition{});
        break;
    case ModelerKind::SweptSurface:
        e.set_surface_definition(SweptSurfaceDefinition{});
        break;
    case ModelerKind::NurbSurface:
        e.set_surface_definition(NurbSurfaceDefinition{});
        break;
    default:
        break;
    }
    auto h = copy.insert_entity(std::move(e), options);
    auto prepared = geometry;
    if (version_ >= DxfVersion::AC1027)
        prepared.strip_for_sab();
    copy.set_modeler_geometry(h, prepared);
    *this = std::move(copy);
    return h;
}
