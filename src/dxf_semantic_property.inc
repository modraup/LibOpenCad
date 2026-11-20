// SPDX-License-Identifier: MPL-2.0
// acadrust objects/semantic_property.rs and registered-class DXF I/O.
namespace {
int semantic_kind(int code) {
    auto range = [&](int a, int b) { return code >= a && code <= b; };
    if (range(310, 319))
        return 8;
    if (range(320, 369) || range(390, 399) || code == 480 || code == 481)
        return 7;
    if (range(290, 299))
        return 1;
    if (range(280, 289))
        return 2;
    if (range(60, 79) || range(170, 179) || range(270, 279) || range(370, 389) || range(400, 409))
        return 3;
    if (range(90, 99) || range(420, 429) || range(440, 459))
        return 4;
    if (range(160, 169))
        return 5;
    if (range(10, 59) || range(110, 149) || range(210, 239) || range(460, 469))
        return 6;
    return 0;
}
std::string semantic_text(const SemanticProperty &p) {
    if (int(p.value.index()) != semantic_kind(p.code))
        throw std::invalid_argument("Semantic property value does not match group type");
    return std::visit(
        [&](const auto &v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>)
                return v;
            else if constexpr (std::is_same_v<T, std::uint64_t>)
                return hex_handle(v);
            else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) {
                if (v.size() > 255)
                    throw std::invalid_argument("Semantic binary chunk exceeds 255 bytes");
                constexpr char hex[] = "0123456789ABCDEF";
                std::string out;
                for (auto b : v) {
                    out += hex[b >> 4];
                    out += hex[b & 15];
                }
                return out;
            } else if constexpr (std::is_same_v<T, double>) {
                MLFields f;
                f.put_r(40, v);
                return f.values[0].value;
            } else
                return std::to_string(v);
        },
        p.value);
}
ClassDefinition semantic_class(const DxfDocument &d, std::string_view name, bool entity) {
    static const std::unordered_set<std::string> builtins = {"3DFACE",
                                                             "ACAD_PROXY_ENTITY",
                                                             "ACAD_PROXY_OBJECT",
                                                             "ACAD_TABLE",
                                                             "ACDBCURVEPATH",
                                                             "ACDBDICTIONARYWDFLT",
                                                             "ACDBDATATABLE",
                                                             "ACDBDETAILVIEWSTYLE",
                                                             "ACDBMOTIONPATH",
                                                             "ACDBPLACEHOLDER",
                                                             "ACDBPOINTCLOUD",
                                                             "ACDBPOINTCLOUDCOLORMAP",
                                                             "ACDBPOINTCLOUDDEF",
                                                             "ACDBPOINTCLOUDDEF_EX",
                                                             "ACDBPOINTCLOUDDEF_REACTOR",
                                                             "ACDBPOINTCLOUDDEF_REACTOR_EX",
                                                             "ACDBPOINTCLOUDEX",
                                                             "ACDBPOINTPATH",
                                                             "ACDBSECTIONVIEWSTYLE",
                                                             "ACDBSYMODELSPACEVIEWSELSET",
                                                             "ACDBVIEWREP",
                                                             "ACDBVIEWREPMODELSPACESOURCE",
                                                             "ACDBVIEWREPORIENTATION",
                                                             "ACDBVIEWREPORIENTATIONDEF",
                                                             "ACDBVIEWREPSECTIONDEFINITION",
                                                             "ACDBVIEWREPSOURCEMGR",
                                                             "ACDBVIEWREPSTANDARD",
                                                             "ACDSRECORD",
                                                             "ACDSSCHEMA",
                                                             "APPID",
                                                             "ARC",
                                                             "ARCALIGNEDTEXT",
                                                             "ARC_DIMENSION",
                                                             "ATTDEF",
                                                             "ATTRIB",
                                                             "BLOCK",
                                                             "BLOCK_RECORD",
                                                             "BREAKDATA",
                                                             "BREAKPOINTREF",
                                                             "CAMERA",
                                                             "CELLSTYLEMAP",
                                                             "CIRCLE",
                                                             "CONTEXTDATAMANAGER",
                                                             "COORDINATION_MODEL",
                                                             "CURVEPATH",
                                                             "DATALINK",
                                                             "DATATABLE",
                                                             "DBCOLOR",
                                                             "DETAILVIEWSTYLE",
                                                             "DGNDEFINITION",
                                                             "DGNUNDERLAY",
                                                             "DICTIONARY",
                                                             "DICTIONARYVAR",
                                                             "DIMENSION",
                                                             "DIMSTYLE",
                                                             "DRAWINGVIEW",
                                                             "DUMMY",
                                                             "DWFDEFINITION",
                                                             "DWFUNDERLAY",
                                                             "ELLIPSE",
                                                             "ENDSEC",
                                                             "ENDTAB",
                                                             "EOF",
                                                             "FIELD",
                                                             "FIELDLIST",
                                                             "GEODATA",
                                                             "GEOMAPIMAGE",
                                                             "GRADIENT_BACKGROUND",
                                                             "GROUND_PLANE_BACKGROUND",
                                                             "GROUP",
                                                             "HATCH",
                                                             "HELIX",
                                                             "IDBUFFER",
                                                             "IBL_BACKGROUND",
                                                             "IMAGE",
                                                             "IMAGEDEF",
                                                             "IMAGEDEF_REACTOR",
                                                             "IMAGE_BACKGROUND",
                                                             "INDEX",
                                                             "INSERT",
                                                             "IRD_DSC_DICT",
                                                             "IRD_DSC_RECORD",
                                                             "IRD_OBJ_RECORD",
                                                             "LARGE_RADIAL_DIMENSION",
                                                             "LAYER",
                                                             "LAYERFILTER",
                                                             "LAYER_INDEX",
                                                             "LAYOUT",
                                                             "LAYOUTPRINTCONFIG",
                                                             "LEADER",
                                                             "LIGHT",
                                                             "LIGHTLIST",
                                                             "LINE",
                                                             "LONG_TRANSACTION",
                                                             "LTYPE",
                                                             "LWPOLYLINE",
                                                             "MATERIAL",
                                                             "MENTALRAYRENDERSETTINGS",
                                                             "MLEADERSTYLE",
                                                             "MLINE",
                                                             "MLINESTYLE",
                                                             "MOTIONPATH",
                                                             "MPOLYGON",
                                                             "MTEXT",
                                                             "MULTILEADER",
                                                             "NAVISWORKSMODELDEF",
                                                             "OBJECT_PTR",
                                                             "OLE2FRAME",
                                                             "OLEFRAME",
                                                             "PARTIAL_VIEWING_FILTER",
                                                             "PARTIAL_VIEWING_INDEX",
                                                             "PDFDEFINITION",
                                                             "PDFUNDERLAY",
                                                             "PLOTSETTINGS",
                                                             "POINT",
                                                             "POINTPATH",
                                                             "POLYLINE",
                                                             "POSITIONMARKER",
                                                             "RASTERVARIABLES",
                                                             "RAPIDRTRENDERENVIRONMENT",
                                                             "RAPIDRTRENDERSETTINGS",
                                                             "RAY",
                                                             "RENDERENTRY",
                                                             "RENDERENVIRONMENT",
                                                             "RENDERGLOBAL",
                                                             "RENDERSETTINGS",
                                                             "RTEXT",
                                                             "SCALE",
                                                             "SECTION",
                                                             "SECTION_MANAGER",
                                                             "SECTION_SETTINGS",
                                                             "SECTIONLINE",
                                                             "SECTIONOBJECT",
                                                             "SECTIONVIEWSTYLE",
                                                             "SEQEND",
                                                             "SHAPE",
                                                             "SKYLIGHT_BACKGROUND",
                                                             "SOLID",
                                                             "SOLID_BACKGROUND",
                                                             "SORTENTSTABLE",
                                                             "SPATIAL_FILTER",
                                                             "SPLINE",
                                                             "STYLE",
                                                             "SUN",
                                                             "SUNSTUDY",
                                                             "TABLE",
                                                             "TABLECONTENT",
                                                             "TABLEGEOMETRY",
                                                             "TABLESTYLE",
                                                             "TABLETEMPLATE",
                                                             "TEXT",
                                                             "TOLERANCE",
                                                             "TRACE",
                                                             "TVDEVICEPROPERTIES",
                                                             "UCS",
                                                             "VERTEX",
                                                             "VIEW",
                                                             "VIEWPORT",
                                                             "VISUALSTYLE",
                                                             "VPORT",
                                                             "WIPEOUT",
                                                             "WIPEOUTVARIABLES",
                                                             "XLINE",
                                                             "XRECORD"};
    if (builtins.count(std::string(name)) || name == "Format" || known_object_context(name))
        throw std::invalid_argument("Use the built-in DXF class interface");
    for (auto c : d.classes())
        if (c.name == name) {
            if (c.is_entity != entity)
                throw std::invalid_argument("Registered class entity/object mismatch");
            return c;
        }
    throw std::invalid_argument("Missing registered DXF class");
}
void semantic_references(const DxfDocument &d, const std::vector<SemanticProperty> &properties) {
    for (const auto &p : properties)
        if (auto h = std::get_if<std::uint64_t>(&p.value)) {
            if (*h && (p.code >= 350 && p.code <= 369))
                throw std::invalid_argument("Cannot adopt existing object through registered class");
            if (*h && d.records_with_handle(hex_handle(*h)).size() != 1)
                throw std::invalid_argument("Missing registered class reference");
        }
}
} // namespace
std::vector<SemanticProperty> DxfRecord::semantic_properties() const {
    std::vector<SemanticProperty> out;
    std::string scope;
    unsigned depth = 0;
    for (std::size_t i = 1; i < pairs_.size(); ++i) {
        const auto &p = pairs_[i];
        if (!depth && p.code == 1001)
            break;
        if (!depth && p.code == 100) {
            scope = p.value;
            continue;
        }
        if (scope.empty() || scope == "AcDbEntity" || scope == "AcDbObject")
            continue;
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (depth && p.value == "}")
                --depth;
        }
        auto text = detail::is_string_value(p.code)
                        ? decode_string(p.value)
                        : (binary_ ? detail::text_value_from_binary(p.code, p.value) : p.value);
        SemanticProperty value{scope, p.code, {}};
        auto kind = semantic_kind(p.code);
        switch (kind) {
        case 0:
            value.value = text;
            break;
        case 1:
            value.value = integer(text) != 0;
            break;
        case 2:
            value.value = static_cast<std::uint8_t>(integer(text));
            break;
        case 3:
            value.value = static_cast<std::int16_t>(integer(text));
            break;
        case 4:
            value.value = static_cast<std::int32_t>(integer(text));
            break;
        case 5:
            value.value = static_cast<std::int64_t>(std::stoll(text));
            break;
        case 6: {
            MLFields f({{p.code, text}});
            value.value = f.r(p.code, 0);
            break;
        }
        case 7:
            value.value = handle_value(text);
            break;
        case 8:
            value.value = table_bytes(text);
            break;
        }
        out.push_back(std::move(value));
    }
    return out;
}
void DxfRecord::set_semantic_properties(const std::vector<SemanticProperty> &value) {
    std::size_t begin = pairs_.size(), end = pairs_.size();
    unsigned depth = 0;
    for (std::size_t i = 1; i < pairs_.size(); ++i) {
        const auto &p = pairs_[i];
        if (!depth && p.code == 1001) {
            end = i;
            break;
        }
        if (!depth && p.code == 100 && p.value != "AcDbEntity" && p.value != "AcDbObject" &&
            begin == pairs_.size())
            begin = i;
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{')
                ++depth;
            else if (depth && p.value == "}")
                --depth;
        }
    }
    begin = std::min(begin, end);
    std::vector<Pair> body;
    std::string scope;
    depth = 0;
    for (const auto &p : value) {
        if (p.subclass.empty() || p.subclass == "AcDbEntity" || p.subclass == "AcDbObject" ||
            p.subclass.find_first_of("\r\n") != std::string::npos || p.code <= 0 || p.code == 5 ||
            p.code == 105 || p.code == 100 || p.code == 101 || p.code >= 1000)
            throw std::invalid_argument("Invalid registered subclass/property");
        if (scope != p.subclass) {
            if (depth)
                throw std::invalid_argument("Subclass inside private group");
            scope = p.subclass;
            body.push_back({100, encode_string(scope)});
        }
        auto text = semantic_text(p);
        if (p.code == 102) {
            if (!text.empty() && text.front() == '{')
                ++depth;
            else if (text == "}") {
                if (!depth)
                    throw std::invalid_argument("Unbalanced private group");
                --depth;
            }
        }
        auto encoded = detail::binary_value_from_text(p.code, text);
        body.push_back(
            {p.code, detail::is_string_value(p.code) ? encode_string(text) : (binary_ ? encoded : text)});
    }
    if (depth)
        throw std::invalid_argument("Unclosed private group");
    auto staged = pairs_;
    staged.erase(staged.begin() + begin, staged.begin() + end);
    staged.insert(staged.begin() + begin, body.begin(), body.end());
    pairs_ = std::move(staged);
}
std::uint64_t DxfDocument::add_registered_entity(std::string_view name,
                                                 const std::vector<SemanticProperty> &properties,
                                                 const EntityOptions &options) {
    if (version_ < DxfVersion::AC1027)
        throw std::invalid_argument("Registered construction requires R2013+");
    auto c = semantic_class(*this, name, true);
    semantic_references(*this, properties);
    DxfSemanticEntity entity;
    static_cast<DxfEntity &>(entity) = make_entity(c.name, c.cpp_name, options);
    entity.set_properties(properties);
    return insert_entity(std::move(entity), options);
}
std::uint64_t DxfDocument::add_registered_object(std::string_view dictionary, std::string name,
                                                 std::string_view class_name,
                                                 const std::vector<SemanticProperty> &properties) {
    if (version_ < DxfVersion::AC1027)
        throw std::invalid_argument("Registered construction requires R2013+");
    auto c = semantic_class(*this, class_name, false);
    semantic_references(*this, properties);
    DxfSemanticObject object;
    static_cast<DxfRecord &>(object) = make_record(c.name);
    object.set_properties(properties);
    return add_dictionary_object(dictionary, std::move(name), object);
}
