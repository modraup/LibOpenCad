// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/classes/civil/create (COGO points, number-list groups,
// siteless alignments, TIN surfaces).
#include <libopencad/civil.hpp>
#include <array>
#include <cctype>
#include <functional>
#include <limits>
#include <map>
#include <utility>
namespace {
constexpr std::int16_t CIVIL_VERSION = 0x04BE;
constexpr std::int32_t KEY_POINT_GROUPS = 32;
constexpr std::int32_t KEY_POINTS = 34;
constexpr std::int32_t KEY_ALL_POINTS_QUERY = 75;
constexpr std::int32_t KEY_STYLES = 31;
constexpr std::int32_t KEY_CLASSES = 5;
constexpr std::int32_t KEY_SETTINGS = 45;
constexpr std::int32_t KEY_POINT_STYLES = 35;
constexpr std::int32_t KEY_ALIGNMENT_STYLES = 93;
constexpr std::int32_t KEY_SURFACE_STYLES = 77;
constexpr std::int32_t KEY_PROFILE_STYLES = 74;
constexpr std::int32_t KEY_SITELESS = 336;
constexpr std::int32_t KEY_DEFAULT_FEATURE = 30;
constexpr std::int32_t DEFAULT_STYLE_ID = 30;
constexpr std::int32_t KEY_CLASSIFICATION = 0xEA6;
constexpr std::uint32_t TEXT_POINT_CLASS = 0x80020001;
constexpr std::uint32_t TEXT_POINTS = 0x80020002;
constexpr std::uint32_t TEXT_CREATE_POINTS = 0x80020003;
constexpr std::uint32_t FIRST_COMPANION_TAG = 0x80030001;
constexpr std::int32_t ALIGNMENT_LINE = 257;
constexpr std::int32_t ALIGNMENT_STAMP = 0x0B001081;
constexpr std::int32_t ALL_POINTS_ID = 33;
constexpr std::int32_t QUERY_TAG = static_cast<std::int32_t>(0x80000001u);
constexpr std::uint32_t FIRST_DESCRIPTION_TAG = 0x80000002;
constexpr std::uint32_t FIRST_GROUP_TAG = 0x80010001;
constexpr std::int32_t ATTRIBUTE_POINT_NUMBER = 19;
constexpr std::int32_t ATTRIBUTE_ELEVATION = 5;
constexpr std::int32_t ATTRIBUTE_RAW_DESCRIPTION = 8;
constexpr std::uint8_t CIVIL_EQUAL = 1;
constexpr std::uint8_t CIVIL_GREATER = 3;
constexpr std::uint8_t CIVIL_LESS = 4;
constexpr std::uint8_t CIVIL_GTE = 5;
constexpr std::uint8_t CIVIL_LTE = 6;
constexpr char UNSUPPORTED_PATTERN[] = "#@.~[]`,";
constexpr unsigned SOFT_POINTER = 4;
constexpr unsigned HARD_POINTER = 5;
constexpr unsigned SOFT_OWNER = 2;
constexpr unsigned HARD_OWNER = 3;
constexpr std::uint32_t TIN_CORNER = 0x8440;
constexpr std::uint32_t TIN_POINT = 0x80;
constexpr double TIN_CORNER_MARGIN = 100;
constexpr const char *CIVIL_ROOT = "Root";
constexpr const char *GROUP_TAIL =
    "10100101010111101010101010100000000000000000000000000000000000000000000000000000000000000000101010000000000000000000000000000000000000000000000000000000000000000010";
struct CivilClass {
    const char *dxf;
    const char *cpp;
    const char *module;
    std::int16_t proxy_flags;
    bool entity;
};
constexpr CivilClass CIVIL_CLASSES[] = {
    {"AECC_TREE_NODE", "AeccDbTreeNode", "AeccvBase130", 0x81, false},
    {"AECC_POINT_NODE", "AeccDbPointsNode", "AeccLand130", 0x81, false},
    {"AECC_POINT_GROUP_NODE", "AeccDbPointGroupsNode", "AeccLand130", 0x81, false},
    {"AECC_POINT_PG", "AeccDbPG", "AeccLand130", 0x37d, false},
    {"AECC_QUERY_STATEMENT", "AeccDbQueryStatement", "AeccLand130", 0x81, false},
    {"AECC_SETTINGS_NODE", "AeccDbSettingsNode", "AeccvBase130", 0x81, false},
    {"AECC_CLASS_NODE", "AeccDbClassNode", "AeccvBase130", 0x81, false},
    {"AECC_ROOT_SETTINGS_NODE", "AeccDbRootSettingsNode", "AeccvBase130", 0x81, false},
    {"AECC_USER_DEFINED_ATTRIBUTE_CLASSIFICATION", "AeccDbUserDefinedAttributeClassification", "AeccvBase130", 0x81,
     false},
    {"AECC_POINT_STYLE", "AeccDbPointStyle", "AeccLand130", 0x81, false},
    {"AECC_ALIGNMENT_STYLE", "AeccDbAlignmentStyle", "AeccLand130", 0x81, false},
    {"AECC_ALIGNMENT_PARCEL_NODE", "AeccDbAlignmentParcelNode", "AeccLand130", 0x81, false},
    {"AECC_FEATURE_STYLE", "AeccDbFeatureStyle", "AeccLand130", 0x81, false},
    {"AECC_PROFILE_STYLE", "AeccDbVAlignmentStyle", "AeccLand130", 0x81, false},
    {"AECC_VALIGNMENT_STYLE_EXTENSION", "AeccDbVAlignmentStyleExtension", "AeccLand130", 0x77c, false},
    {"AECC_COGO_POINT", "AeccDbCogoPoint", "AeccLand130", 0x33c, true},
    {"AECC_ALIGNMENT", "AeccDbAlignment", "AeccLand130", 0x37d, true},
    {"AECC_PROFILE", "AeccDbVAlignment", "AeccLand130", 0x37d, true},
    {"AECC_NOTIFICATIONINFOS", "AeccDbVAlignNotificationInfos", "AeccLand130", 0x77c, false},
    {"AECC_ENTITYEXTENDPARAM", "AeccDbVAlignFreeParabolaPassPtInfos", "AeccLand130", 0x77c, false},
    {"AECC_ENTITYCIRCLEINFOS", "AeccDbVAlignFreeCircleInfos", "AeccLand130", 0x77c, false},
    {"AECC_DB_FIXED_PARABOLA_HLPT", "AeccDbVAlignFixedParabolaHLPtInfos", "AeccLand130", 0x77c, false},
    {"AECC_DB_FIXED_PARABOLA_HLPTPATSPEAK", "AeccDbVAlignFixedParabolaHLPtPatsPeakInfos", "AeccLand130", 0x77c, false},
    {"AECC_DB_FIXED_ARC_HLPT", "AeccDbVAlignFixedArcHLPtInfos", "AeccLand130", 0x77c, false},
    {"AECC_DB_FLOAT_TANGENT_GRADELEN", "AeccDbVAlignFloatTangentGradeLenInfos", "AeccLand130", 0x77c, false},
    {"AECC_SURFACE_STYLE", "AeccDbSurfaceStyle", "AeccLand130", 0x81, false},
    {"AECC_TIN_SURFACE", "AeccDbSurfaceTin", "AeccLand130", 0x37d, true},
};
struct CivilRef {
    unsigned code;
    std::uint64_t handle;
};
struct CivilDefaults {
    std::uint64_t point_style = 0, label_style = 0, create_settings = 0, class_node = 0;
};
struct CivilGroupEntry {
    std::int32_t id = 0;
    std::uint64_t handle = 0;
    std::string name;
};
struct CivilQuery {
    std::int32_t tag = 0;
    std::uint64_t handle = 0;
    std::string title;
};
struct CivilGroup {
    std::int32_t id = 0, serial = 0;
    std::optional<std::string> name;
    std::string description;
    std::vector<std::uint32_t> numbers;
    std::optional<std::pair<Vector3, Vector3>> extents;
    std::array<std::uint64_t, 6> refs{};
};
void write_bit_run(DwgBitWriter &w, std::string_view bits) {
    for (char c : bits)
        if (c == '0' || c == '1')
            w.bits(1, c == '1');
}
void bl_byte(DwgBitWriter &w, std::uint8_t value) {
    w.bits(2, 1);
    w.bits(8, value);
}
bool civil_ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}
int civil_class_number(const DwgR2000Objects &o, std::string_view name) {
    for (auto &c : o.classes)
        if (c.dxf_name == name)
            return c.number;
    throw std::invalid_argument("Missing civil class");
}
std::string register_civil_classes(std::string_view file, const DwgR2000Objects &o) {
    auto classes = o.classes;
    bool added = false;
    for (auto &item : CIVIL_CLASSES) {
        if (std::find_if(classes.begin(), classes.end(),
                         [&](const DwgClassRecord &c) { return c.dxf_name == item.dxf; }) != classes.end())
            continue;
        require(classes.size() < 32767 - 500, "Class table exhausted");
        DwgClassRecord c;
        c.number = static_cast<std::int16_t>(500 + classes.size());
        c.proxy_flags = item.proxy_flags;
        c.application = item.module;
        c.cpp_name = item.cpp;
        c.dxf_name = item.dxf;
        c.item_class_id = static_cast<std::int16_t>(item.entity ? 498 : 499);
        c.fields[1] = 25;
        classes.push_back(c);
        added = true;
    }
    if (!added)
        return std::string(file);
    return detail::replace_ac18_sections(file, {{"AcDb:Classes", lifecycle_classes(file, classes)}});
}
std::uint64_t civil_layer(const DwgR2000Objects &o, std::string_view name) {
    for (auto &[h, symbol] : o.symbols)
        if (symbol.type == 51 && symbol.name == name)
            return h;
    return 0;
}
std::uint64_t civil_linetype(const DwgR2000Objects &o, std::string_view name) {
    for (auto &[h, symbol] : o.symbols)
        if (symbol.type == 57 && symbol.name == name)
            return h;
    return 0;
}
std::uint64_t civil_text_style(const DwgR2000Objects &o, std::string_view name) {
    for (auto &[h, symbol] : o.symbols)
        if (symbol.type == 53 && symbol.name == name)
            return h;
    return 0;
}
std::uint64_t civil_file_time() { return 116444736000000000ull; }
std::string civil_companion_name(std::uint64_t seed) {
    char buf[33];
    auto hex16 = [](char *out, std::uint64_t v) {
        static constexpr char digits[] = "0123456789abcdef";
        for (int i = 15; i >= 0; --i)
            *out++ = digits[(v >> (4 * i)) & 15];
    };
    hex16(buf, civil_file_time());
    hex16(buf + 16, seed);
    buf[32] = 0;
    return buf;
}
bool civil_text_key(std::int32_t key) { return (static_cast<std::uint32_t>(key) & 0x80000000u) != 0; }
void write_byte(DwgBitWriter &w, std::uint8_t value) { w.bits(8, value); }
DynamicStream civil_object_stream(const ArchObject &object) {
    require(object.raw_dwg_data && object.raw_dwg_version == DxfVersion::AC1032, "civil object has no DWG body");
    auto rec = unframed_arch_record(*object.raw_dwg_data, object.raw_dwg_handle_bits, 1032, object.handle);
    auto s = open_dynamic(*object.raw_dwg_data, rec, object.handle, 1032, nullptr);
    skip_entity_prefix(s.main, s.end, false, 1032);
    s.href();
    auto reactors = s.main.bit_long();
    require(reactors >= 0 && reactors <= 1000000, "Invalid civil reactor count");
    for (int i = 0; i < reactors; ++i)
        s.href();
    if (!s.main.bits(1))
        s.href();
    s.main.bits(1);
    return s;
}
std::optional<DynamicStream> try_civil_object_stream(const ArchObject &object) {
    try {
        return civil_object_stream(object);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::string encode_civil_object(int type, std::uint64_t id, std::uint64_t owner,
                               const std::vector<std::uint64_t> &reactors, const std::function<void(ArchWriter &)> &fields,
                               const std::vector<CivilRef> &refs, const std::vector<std::string> &strings,
                               const char *page) {
    ArchWriter w;
    w.v = 1032;
    w.page = page;
    auto split = object_prefix(w.main, w.refs, w.v, type, id, owner, 0, reactors);
    w.bs(CIVIL_VERSION);
    fields(w);
    for (auto &r : refs)
        w.href(r.code, r.handle);
    for (auto &s : strings)
        w.str(s);
    return finish_arch(w, split);
}
std::string encode_cogo_entity(int type, std::uint64_t id, std::uint64_t layer, std::uint32_t number, const char *page) {
    ArchWriter w;
    w.v = 1032;
    w.page = page;
    auto split = arch_entity_common(w, type, id, layer, {});
    w.bs(CIVIL_VERSION);
    w.main.bit_long(static_cast<std::int32_t>(number));
    write_bit_run(w.main, "001010");
    return finish_arch(w, split);
}
#include "civil_alignment.inc"
#include "civil_tin.inc"
ArchObject packed_civil_object(std::string_view dxf, std::uint64_t id, std::uint64_t owner,
                               const std::vector<std::uint64_t> &reactors, const std::string &packed) {
    auto rec = read_record(packed, 0, id, 1032);
    ArchObject object;
    object.dxf_name = std::string(dxf);
    for (auto &item : CIVIL_CLASSES)
        if (item.dxf == object.dxf_name)
            object.cpp_class_name = item.cpp;
    object.handle = id;
    object.owner = owner;
    object.reactors = reactors;
    object.raw_dwg_data = packed.substr(rec.body_offset, rec.body_size);
    object.raw_dwg_handle_bits = static_cast<std::int64_t>(object.raw_dwg_data->size() * 8 - rec.main_bits);
    object.raw_dwg_version = DxfVersion::AC1032;
    return object;
}
std::vector<CogoPoint> parse_cogo_points(const ArchObject &object) {
    if (object.dxf_name != "AECC_POINT_NODE")
        return {};
    try {
        auto decoded = object.decode();
        auto s = civil_object_stream(object);
        if (s.main.bit_short() != CIVIL_VERSION)
            return {};
        if (s.main.bit_long() != KEY_POINTS || s.main.bit_long() != 0 || s.main.bit_long() != 0)
            return {};
        s.main.bits(3);
        auto count = s.main.bit_long();
        require(count >= 0 && count <= 1000000, "Invalid civil point count");
        std::vector<std::pair<std::uint32_t, Vector3>> records;
        for (int i = 0; i < count; ++i) {
            require(s.main.bit_long() == 1, "Invalid civil point record");
            Vector3 p{s.main.bit_double(), s.main.bit_double(), s.main.bit_double()};
            auto number = s.main.bit_long();
            require(number > 0, "Invalid civil point number");
            require(s.main.bit_short() == 0x0420, "Invalid civil point marker");
            records.push_back({static_cast<std::uint32_t>(number), p});
        }
        auto read_numbers = [&] {
            auto n = s.main.bit_long();
            require(n >= 0 && n <= 1000000, "Invalid civil style count");
            std::vector<std::uint32_t> out;
            for (int i = 0; i < n; ++i) {
                auto v = s.main.bit_long();
                require(v > 0, "Invalid civil style number");
                out.push_back(static_cast<std::uint32_t>(v));
            }
            return out;
        };
        auto styled = read_numbers();
        auto labelled = read_numbers();
        for (int i = 0; i < 3; ++i)
            require(s.main.bit_long() == 0, "Invalid civil point database");
        auto k = s.main.bit_long();
        require(k >= 0 && k <= 1000000, "Invalid civil description count");
        std::vector<std::uint32_t> described;
        for (int i = 0; i < k; ++i) {
            auto number = s.main.bit_long();
            s.main.bit_long();
            described.push_back(static_cast<std::uint32_t>(number));
        }
        auto handle_of = [&](const std::vector<std::uint32_t> &list, std::size_t offset, std::uint32_t number) {
            for (std::size_t i = 0; i < list.size(); ++i)
                if (list[i] == number && offset + i < decoded.handles.size())
                    return decoded.handles[offset + i];
            return std::uint64_t{0};
        };
        std::vector<CogoPoint> points;
        for (auto &[number, position] : records) {
            CogoPoint p;
            p.number = number;
            p.position = position;
            p.style = handle_of(styled, 0, number);
            p.label_style = handle_of(labelled, styled.size(), number);
            auto at = std::find(described.begin(), described.end(), number);
            if (at != described.end()) {
                auto i = static_cast<std::size_t>(at - described.begin());
                if (1 + i < decoded.strings.size())
                    p.description = decoded.strings[1 + i];
            }
            points.push_back(std::move(p));
        }
        return points;
    } catch (const std::exception &) {
        return {};
    }
}
std::vector<std::uint32_t> parse_group_numbers(const ArchObject &object) {
    if (object.dxf_name != "AECC_POINT_PG")
        return {};
    try {
        auto s = civil_object_stream(object);
        if (s.main.bit_short() != CIVIL_VERSION)
            return {};
        s.main.bit_long();
        for (int i = 0; i < 2; ++i) {
            auto n = s.main.bit_long();
            require(n >= 0 && n <= 1000000, "Invalid civil group list");
            for (int j = 0; j < n; ++j)
                s.main.bit_long();
        }
        auto probe = s.main;
        if (probe.bits(3) == 0b101) {
            auto n = probe.bits(8);
            for (unsigned i = 0; i < n; ++i) {
                (void)probe.bit_long();
                (void)probe.bit_long();
            }
            s.main = probe;
        }
        auto a = s.main.bit_long();
        s.main.bits(5);
        if (a == 0) {
            s.main.bits(6);
            s.main.bit_long();
        }
        auto n = s.main.bit_long();
        require(n >= 0 && n <= 1000000, "Invalid civil group count");
        std::vector<std::uint32_t> numbers;
        for (int i = 0; i < n; ++i)
            numbers.push_back(static_cast<std::uint32_t>(s.main.little_endian(4)));
        return numbers;
    } catch (const std::exception &) {
        return {};
    }
}
std::optional<std::uint32_t> parse_entity_number(const ArchEntityData &entity) {
    if (entity.dxf_name != "AECC_COGO_POINT" || !entity.raw_dwg_data ||
        entity.dwg_source_version != DxfVersion::AC1032)
        return std::nullopt;
    try {
        auto rec = unframed_arch_record(*entity.raw_dwg_data, entity.dwg_handle_bits, 1032, entity.handle);
        auto s = open_dynamic(*entity.raw_dwg_data, rec, entity.handle, 1032, nullptr);
        read_entity_properties(s.main, s.refs, entity.handle, s.end, 1032, rec, nullptr, nullptr, nullptr, nullptr);
        if (s.main.bit_short() != CIVIL_VERSION)
            return std::nullopt;
        auto number = s.main.bit_long();
        if (number <= 0)
            return std::nullopt;
        return static_cast<std::uint32_t>(number);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
bool skip_civil_key(DwgBitReader &main) {
    auto key = static_cast<std::uint32_t>(main.bit_long());
    switch (key >> 24) {
    case 0x01:
    case 0x0A:
    case 0x0D:
        main.bit_long();
        return true;
    case 0x02:
        main.bit_double();
        return true;
    case 0x03:
        main.bits(8);
        return true;
    case 0x04:
    case 0x0F:
    case 0x10:
    case 0x12:
        return true;
    case 0x0B:
    case 0x19: {
        auto n = main.bit_long();
        require(n >= 0 && n <= 1000000, "Invalid civil key list");
        for (int i = 0; i < n; ++i)
            main.bit_long();
        return true;
    }
    case 0x0C: {
        auto n = main.bit_long();
        require(n >= 0 && n <= 1000000, "Invalid civil key list");
        for (int i = 0; i < n; ++i)
            main.bit_double();
        return true;
    }
    case 0x0E:
        main.bit_short();
        main.bit_long();
        main.bits(8);
        return true;
    case 0x13:
    case 0x16:
        main.little_endian(8);
        return true;
    default:
        return false;
    }
}
bool skip_alignment_elements(DwgBitReader &main) {
    for (auto want : {80, 0, 80, 80, 0})
        if (main.bit_long() != want)
            return false;
    main.bit_long();
    if (main.bits(6) != 0b010101)
        return false;
    auto keys = main.bits(8);
    for (unsigned i = 0; i < keys; ++i)
        if (!skip_civil_key(main))
            return false;
    if (main.bit_long() != 0)
        return false;
    for (int i = 0; i < 65; ++i)
        main.bits(1);
    main.bit_double();
    main.bits(7);
    main.bit_long();
    main.bit_double();
    main.bit_double();
    main.bits(7);
    main.bit_double();
    main.bits(1);
    main.bit_long();
    return true;
}
#include "civil_profile.inc"
std::optional<std::string> parse_alignment_name(const ArchEntityData &entity) {
    if (entity.dxf_name != "AECC_ALIGNMENT")
        return std::nullopt;
    auto strings = entity.decode().strings;
    for (auto &s : strings) {
        auto at = s.rfind(".dwg:");
        if (at != std::string::npos && at + 5 < s.size())
            return s.substr(at + 5);
    }
    if (strings.size() > 3 && !strings[3].empty())
        return strings[3];
    return std::nullopt;
}
std::optional<double> parse_alignment_start_station(const ArchEntityData &entity) {
    if (entity.dxf_name != "AECC_ALIGNMENT" || !entity.raw_dwg_data ||
        entity.dwg_source_version != DxfVersion::AC1032)
        return std::nullopt;
    try {
        auto rec = unframed_arch_record(*entity.raw_dwg_data, entity.dwg_handle_bits, 1032, entity.handle);
        auto s = open_dynamic(*entity.raw_dwg_data, rec, entity.handle, 1032, nullptr);
        read_entity_properties(s.main, s.refs, entity.handle, s.end, 1032, rec, nullptr, nullptr, nullptr, nullptr);
        if (!skip_alignment_elements(s.main))
            return std::nullopt;
        return 0;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::optional<std::vector<AlignmentElement>> parse_alignment_elements(const ArchEntityData &entity) {
    if (entity.dxf_name != "AECC_ALIGNMENT" || !entity.raw_dwg_data ||
        entity.dwg_source_version != DxfVersion::AC1032)
        return std::nullopt;
    try {
        auto rec = unframed_arch_record(*entity.raw_dwg_data, entity.dwg_handle_bits, 1032, entity.handle);
        auto s = open_dynamic(*entity.raw_dwg_data, rec, entity.handle, 1032, nullptr);
        read_entity_properties(s.main, s.refs, entity.handle, s.end, 1032, rec, nullptr, nullptr, nullptr, nullptr);
        if (!skip_alignment_elements(s.main))
            return std::nullopt;
        return read_written_alignment_elements(s.main);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::optional<std::vector<AlignmentProfileRef>> parse_alignment_profiles(const ArchEntityData &entity) {
    if (entity.dxf_name != "AECC_ALIGNMENT" || !entity.raw_dwg_data ||
        entity.dwg_source_version != DxfVersion::AC1032)
        return std::nullopt;
    try {
        auto rec = unframed_arch_record(*entity.raw_dwg_data, entity.dwg_handle_bits, 1032, entity.handle);
        auto s = open_dynamic(*entity.raw_dwg_data, rec, entity.handle, 1032, nullptr);
        read_entity_properties(s.main, s.refs, entity.handle, s.end, 1032, rec, nullptr, nullptr, nullptr, nullptr);
        if (!skip_alignment_elements(s.main))
            return std::nullopt;
        if (!read_written_alignment_elements(s.main))
            return std::nullopt;
        auto handles = entity.decode().handles;
        std::vector<std::uint64_t> extra;
        if (handles.size() > 6)
            extra.assign(handles.begin() + 6, handles.end());
        return read_written_alignment_profiles(s.main, extra);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::vector<std::vector<Vector2>> parse_alignment_polylines(const ArchEntityData &entity) {
    auto elements = parse_alignment_elements(entity);
    if (!elements)
        return {};
    return alignment_solved_polylines(*elements);
}
std::vector<std::pair<AlignmentPiece, std::vector<Vector2>>> parse_alignment_pieces(const ArchEntityData &entity) {
    auto elements = parse_alignment_elements(entity);
    if (!elements)
        return {};
    return alignment_pieces_by_kind(*elements);
}
struct CivilNode {
    std::int32_t key = 0;
    std::vector<std::pair<std::int32_t, std::uint64_t>> children;
    std::vector<std::string> names, own, texts;
    std::string bits;
    std::vector<CivilRef> extra;
};
std::optional<CivilNode> parse_civil_node(const ArchObject &object) {
    try {
        auto s = civil_object_stream(object);
        if (s.main.bit_short() != CIVIL_VERSION)
            return std::nullopt;
        CivilNode n;
        n.key = s.main.bit_long();
        auto count = s.main.bit_long();
        require(count >= 0 && count <= 1000000, "Invalid civil node count");
        std::vector<std::int32_t> keys;
        for (int i = 0; i < count; ++i)
            keys.push_back(s.main.bit_long());
        while (s.main.position() < s.end)
            n.bits.push_back(s.main.bits(1) ? '1' : '0');
        auto decoded = object.decode();
        if (decoded.handles.size() < static_cast<std::size_t>(count))
            return std::nullopt;
        for (int i = 0; i < count; ++i)
            n.children.push_back({keys[static_cast<std::size_t>(i)], decoded.handles[static_cast<std::size_t>(i)]});
        auto own_len = civil_text_key(n.key) ? 2u : 1u;
        std::size_t named = 0;
        for (auto &[k, h] : n.children) {
            (void)h;
            if (civil_text_key(k))
                ++named;
        }
        if (decoded.strings.size() < own_len + named)
            return std::nullopt;
        n.own.assign(decoded.strings.begin(), decoded.strings.begin() + static_cast<std::ptrdiff_t>(own_len));
        n.names.assign(decoded.strings.begin() + static_cast<std::ptrdiff_t>(own_len),
                       decoded.strings.begin() + static_cast<std::ptrdiff_t>(own_len + named));
        n.texts.assign(decoded.strings.begin() + static_cast<std::ptrdiff_t>(own_len + named), decoded.strings.end());
        for (std::size_t i = static_cast<std::size_t>(count); i < decoded.handles.size(); ++i)
            n.extra.push_back({SOFT_POINTER, decoded.handles[i]});
        return n;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::optional<std::vector<std::pair<std::int32_t, std::uint64_t>>> parse_root_entries(const ArchObject &object) {
    if (object.dxf_name != "AECC_TREE_NODE")
        return std::nullopt;
    try {
        auto s = civil_object_stream(object);
        if (s.main.bit_short() != CIVIL_VERSION || s.main.bit_long() != 1)
            return std::nullopt;
        auto n = s.main.bit_long();
        require(n >= 0 && n <= 1000000, "Invalid civil root count");
        std::vector<std::int32_t> keys;
        for (int i = 0; i < n; ++i)
            keys.push_back(s.main.bit_long());
        auto handles = object.decode().handles;
        if (handles.size() < static_cast<std::size_t>(n))
            return std::nullopt;
        std::vector<std::pair<std::int32_t, std::uint64_t>> entries;
        for (int i = 0; i < n; ++i)
            entries.push_back({keys[static_cast<std::size_t>(i)], handles[static_cast<std::size_t>(i)]});
        return entries;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::optional<std::vector<CivilGroupEntry>> parse_group_entries(const ArchObject &object) {
    if (object.dxf_name != "AECC_POINT_GROUP_NODE")
        return std::nullopt;
    try {
        auto s = civil_object_stream(object);
        if (s.main.bit_short() != CIVIL_VERSION || s.main.bit_long() != KEY_POINT_GROUPS)
            return std::nullopt;
        auto n = s.main.bit_long();
        require(n >= 0 && n <= 1000000, "Invalid civil group-node count");
        std::vector<std::int32_t> ids;
        for (int i = 0; i < n; ++i)
            ids.push_back(s.main.bit_long());
        auto decoded = object.decode();
        if (decoded.handles.size() < static_cast<std::size_t>(n))
            return std::nullopt;
        std::size_t name_i = decoded.strings.empty() ? 0 : 1;
        std::vector<CivilGroupEntry> entries;
        for (int i = 0; i < n; ++i) {
            CivilGroupEntry e;
            e.id = ids[static_cast<std::size_t>(i)];
            e.handle = decoded.handles[static_cast<std::size_t>(i)];
            if (e.id != ALL_POINTS_ID) {
                if (name_i < decoded.strings.size())
                    e.name = decoded.strings[name_i++];
            }
            entries.push_back(std::move(e));
        }
        return entries;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::optional<std::vector<CivilQuery>> parse_queries(const ArchObject &object) {
    if (object.dxf_name != "AECC_QUERY_STATEMENT")
        return std::nullopt;
    try {
        auto s = civil_object_stream(object);
        if (s.main.bit_short() != CIVIL_VERSION || s.main.bit_long() != KEY_ALL_POINTS_QUERY)
            return std::nullopt;
        auto n = s.main.bit_long();
        require(n >= 0 && n <= 1000000, "Invalid civil query count");
        std::vector<std::int32_t> tags;
        for (int i = 0; i < n; ++i)
            tags.push_back(s.main.bit_long());
        auto decoded = object.decode();
        if (decoded.handles.size() < static_cast<std::size_t>(n) || decoded.strings.size() <= static_cast<std::size_t>(n))
            return std::nullopt;
        std::vector<CivilQuery> list;
        for (int i = 0; i < n; ++i)
            list.push_back({tags[static_cast<std::size_t>(i)], decoded.handles[static_cast<std::size_t>(i)],
                            decoded.strings[static_cast<std::size_t>(i) + 1]});
        return list;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
bool civil_plain_group(const ArchObject &object) {
    auto s = try_civil_object_stream(object);
    if (!s)
        return false;
    try {
        if (s->main.bit_short() != CIVIL_VERSION)
            return false;
        s->main.bit_long();
        if (s->main.bit_long() != 0 || s->main.bit_long() != 0)
            return false;
        return s->main.bits(3) != 0b101;
    } catch (const std::exception &) {
        return false;
    }
}
std::uint8_t comparison_code(Comparison c) {
    switch (c) {
    case Comparison::Greater:
        return CIVIL_GREATER;
    case Comparison::Less:
        return CIVIL_LESS;
    case Comparison::GreaterOrEqual:
        return CIVIL_GTE;
    case Comparison::LessOrEqual:
        return CIVIL_LTE;
    default:
        return CIVIL_EQUAL;
    }
}
const char *comparison_node(Comparison c) {
    switch (c) {
    case Comparison::Greater:
        return "AeccQueryNodeGT";
    case Comparison::Less:
        return "AeccQueryNodeLT";
    case Comparison::GreaterOrEqual:
        return "AeccQueryNodeGTE";
    case Comparison::LessOrEqual:
        return "AeccQueryNodeLTE";
    default:
        return "AeccQueryNodeEQ";
    }
}
std::optional<Comparison> comparison_from_node(std::string_view name) {
    if (name == "AeccQueryNodeEQ")
        return Comparison::Equal;
    if (name == "AeccQueryNodeGT")
        return Comparison::Greater;
    if (name == "AeccQueryNodeLT")
        return Comparison::Less;
    if (name == "AeccQueryNodeGTE")
        return Comparison::GreaterOrEqual;
    if (name == "AeccQueryNodeLTE")
        return Comparison::LessOrEqual;
    return std::nullopt;
}
bool wildcard_match(std::string_view pattern, std::string_view text) {
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::vector<char> t, p;
    for (unsigned char c : text)
        t.push_back(lower(c));
    for (unsigned char c : pattern)
        p.push_back(lower(c));
    std::vector<char> ok(t.size() + 1, 0);
    ok[0] = 1;
    for (char c : p) {
        std::vector<char> next(t.size() + 1, 0);
        for (std::size_t j = 0; j <= t.size(); ++j) {
            if (c == '*')
                next[j] = ok[j] || (j > 0 && next[j - 1]);
            else if (c == '?')
                next[j] = j > 0 && ok[j - 1];
            else
                next[j] = j > 0 && ok[j - 1] && t[j - 1] == c;
        }
        ok = std::move(next);
    }
    return ok[t.size()] != 0;
}
bool query_selects(const PointQuery &query, const CogoPoint &p) {
    switch (query.kind) {
    case PointQuery::Kind::Numbers:
        return std::find(query.numbers.begin(), query.numbers.end(), p.number) != query.numbers.end();
    case PointQuery::Kind::Elevation: {
        auto z = p.position.z, v = query.elevation;
        switch (query.comparison) {
        case Comparison::Greater:
            return z > v;
        case Comparison::Less:
            return z < v;
        case Comparison::GreaterOrEqual:
            return z >= v;
        case Comparison::LessOrEqual:
            return z <= v;
        default:
            return z == v;
        }
    }
    case PointQuery::Kind::RawDescription: {
        auto d = p.description ? std::string_view(*p.description) : std::string_view{};
        for (auto &pat : query.patterns)
            if (wildcard_match(pat, d))
                return true;
        return false;
    }
    }
    return false;
}
std::pair<std::vector<std::uint32_t>, std::optional<std::pair<Vector3, Vector3>>>
query_members(const std::vector<CogoPoint> &points, const PointQuery &query) {
    std::vector<std::uint32_t> numbers;
    std::optional<std::pair<Vector3, Vector3>> extents;
    for (auto &p : points) {
        if (!query_selects(query, p))
            continue;
        numbers.push_back(p.number);
        extents = extents ? std::pair<Vector3, Vector3>{{std::min(extents->first.x, p.position.x),
                                                         std::min(extents->first.y, p.position.y),
                                                         std::min(extents->first.z, p.position.z)},
                                                        {std::max(extents->second.x, p.position.x),
                                                         std::max(extents->second.y, p.position.y),
                                                         std::max(extents->second.z, p.position.z)}}
                          : std::pair<Vector3, Vector3>{p.position, p.position};
    }
    std::sort(numbers.begin(), numbers.end());
    return {numbers, extents};
}
bool unsupported_pattern(std::string_view p) {
    if (p.empty())
        return true;
    for (char c : p)
        for (char bad : std::string_view(UNSUPPORTED_PATTERN))
            if (c == bad)
                return true;
    return false;
}
std::string query_double_text(double v) {
    auto s = std::to_string(v);
    auto dot = s.find('.');
    if (dot != std::string::npos) {
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
    }
    return s.empty() ? "0" : s;
}
struct QueryValue {
    enum class Kind { Number, Double, Text } kind = Kind::Number;
    std::uint32_t number = 0;
    double real = 0;
    std::string text;
    std::string as_text() const {
        if (kind == Kind::Double)
            return query_double_text(real);
        if (kind == Kind::Text)
            return text;
        return std::to_string(number);
    }
};
struct QuerySpec {
    std::uint8_t value_type = 1, flags = 0x50, klass = 0;
    bool number_attr = true;
    std::int32_t attr_id = ATTRIBUTE_POINT_NUMBER;
    Comparison comparison = Comparison::Equal;
    std::vector<QueryValue> constants;
};
QuerySpec query_spec(const PointQuery &query) {
    QuerySpec spec;
    switch (query.kind) {
    case PointQuery::Kind::Numbers: {
        spec.constants.reserve(query.numbers.size());
        for (auto n : query.numbers) {
            if (!spec.constants.empty() && spec.constants.back().kind == QueryValue::Kind::Number &&
                spec.constants.back().number == n)
                continue;
            spec.constants.push_back({QueryValue::Kind::Number, n});
        }
        require(!spec.constants.empty(), "point group needs point numbers");
        for (auto &c : spec.constants)
            require(c.number != 0 && c.number <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
                    "point group needs point numbers");
        return spec;
    }
    case PointQuery::Kind::Elevation:
        require(std::isfinite(query.elevation), "elevation query needs a finite value");
        spec.value_type = 2;
        spec.klass = 0b001;
        spec.number_attr = false;
        spec.attr_id = ATTRIBUTE_ELEVATION;
        spec.comparison = query.comparison;
        spec.constants.push_back({QueryValue::Kind::Double, 0, query.elevation});
        return spec;
    case PointQuery::Kind::RawDescription:
        require(!query.patterns.empty(), "description query needs patterns of text, '*' and '?'");
        for (auto &p : query.patterns)
            require(!unsupported_pattern(p), "description query needs patterns of text, '*' and '?'");
        spec.value_type = 3;
        spec.klass = 0b101;
        spec.number_attr = false;
        spec.attr_id = ATTRIBUTE_RAW_DESCRIPTION;
        for (auto &p : query.patterns)
            spec.constants.push_back({QueryValue::Kind::Text, 0, 0, p});
        return spec;
    }
    require(false, "point group needs point numbers");
    return spec;
}
std::optional<PointQuery> parse_query(const ArchObject &object) {
    auto s = try_civil_object_stream(object);
    if (!s)
        return std::nullopt;
    try {
        if (s->main.bit_short() != CIVIL_VERSION)
            return std::nullopt;
        s->main.bit_long();
        if (s->main.bit_long() != 0 || s->main.bit_long() != 0)
            return std::nullopt;
        skip_bits(s->main, 5);
        auto k = s->main.bit_long();
        if (k <= 0)
            return std::nullopt;
        skip_bits(s->main, 7);
        auto value_type = s->main.bits(3);
        auto flags = s->main.bits(8);
        auto strings = object.decode().strings;
        if (strings.size() < 2 + static_cast<std::size_t>(k))
            return std::nullopt;
        std::vector<std::string> values(strings.begin() + 2, strings.begin() + 2 + k);
        std::vector<std::string> nodes;
        for (std::size_t i = 2 + static_cast<std::size_t>(k); i < strings.size(); ++i)
            if (strings[i].size() >= 13 && strings[i].compare(0, 13, "AeccQueryNode") == 0)
                nodes.push_back(strings[i]);
        std::optional<Comparison> comparison;
        for (auto c : {Comparison::Equal, Comparison::Greater, Comparison::Less, Comparison::GreaterOrEqual,
                       Comparison::LessOrEqual})
            for (auto &n : nodes)
                if (!comparison && comparison_from_node(n) == c)
                    comparison = c;
        if (!comparison)
            return std::nullopt;
        std::vector<std::string> expected{"AeccQueryNodePARENS"};
        for (int i = 1; i < k; ++i)
            expected.emplace_back("AeccQueryNodeOR");
        for (int i = 0; i < k; ++i) {
            expected.emplace_back(comparison_node(*comparison));
            expected.emplace_back("AeccQueryNodeATTRID");
            expected.emplace_back("AeccQueryNodeCONST");
        }
        if (nodes != expected)
            return std::nullopt;
        if (value_type == 1 && flags == 0x50 && *comparison == Comparison::Equal) {
            PointQuery q;
            q.kind = PointQuery::Kind::Numbers;
            for (auto &t : values) {
                auto n = std::stoul(t);
                q.numbers.push_back(static_cast<std::uint32_t>(n));
            }
            return q;
        }
        if (value_type == 2 && flags == 0x50 && k == 1) {
            return PointQuery::from_elevation(*comparison, std::stod(values[0]));
        }
        if (value_type == 3 && flags == 0x50 && *comparison == Comparison::Equal)
            return PointQuery::from_raw_description(values);
        return std::nullopt;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::optional<std::int32_t> parse_group_serial(const ArchObject &object) {
    if (!civil_plain_group(object))
        return std::nullopt;
    auto s = try_civil_object_stream(object);
    if (!s)
        return std::nullopt;
    try {
        if (s->main.bit_short() != CIVIL_VERSION)
            return std::nullopt;
        for (int i = 0; i < 4; ++i)
            s->main.bit_long();
        skip_bits(s->main, 11);
        return s->main.bit_long();
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
template <class T, class Key>
void bucket_insert(std::vector<T> &v, T item, Key key) {
    auto bucket = [](std::int32_t k) { return static_cast<std::uint32_t>(k) % 8; };
    auto b = bucket(key(&item));
    auto at = v.size();
    for (std::size_t i = 0; i < v.size(); ++i)
        if (bucket(key(&v[i])) > b) {
            at = i;
            break;
        }
    v.insert(v.begin() + static_cast<std::ptrdiff_t>(at), std::move(item));
}
struct CivilWork {
    std::string_view file;
    const DwgR2000Objects &o;
    LifecycleStorage storage;
    HandleSeed seed;
    LifecycleEdits added, edits;
    std::set<std::uint64_t> removed;
    std::map<std::uint64_t, ArchObject> objects;
    std::optional<std::uint64_t> root_handle;
    std::optional<std::uint64_t> new_entity;
    CivilWork(std::string_view bytes, const DwgR2000Objects &objects)
        : file(bytes), o(objects), storage(bytes), seed(bytes, objects, 1032) {
        require(storage.version == 1032, "Civil construction requires AC1032");
    }
    const ArchObject *object(std::uint64_t handle) const {
        auto i = objects.find(handle);
        if (i != objects.end())
            return &i->second;
        auto j = o.arch_objects.find(handle);
        return j == o.arch_objects.end() ? nullptr : &j->second;
    }
    std::uint64_t nod() const { return nod_handle(o); }
    std::optional<std::uint64_t> root() {
        if (root_handle)
            return root_handle;
        auto n = nod();
        auto rec = o.metadata_records.find(n);
        if (rec != o.metadata_records.end())
            for (auto &[name, target] : rec->second.dictionary_entries)
                if (name == CIVIL_ROOT) {
                    root_handle = target;
                    return root_handle;
                }
        return std::nullopt;
    }
    std::optional<std::vector<std::pair<std::int32_t, std::uint64_t>>> root_entries() {
        auto h = root();
        if (!h)
            return std::nullopt;
        auto obj = object(*h);
        return obj ? parse_root_entries(*obj) : std::nullopt;
    }
    std::optional<std::uint64_t> root_node(std::int32_t key) {
        auto entries = root_entries();
        if (!entries)
            return std::nullopt;
        for (auto &[k, h] : *entries)
            if (k == key)
                return h;
        return std::nullopt;
    }
    void store(std::uint64_t handle, std::string packed, ArchObject object) {
        objects[handle] = std::move(object);
        if (added.count(handle) || !o.records.count(handle))
            added[handle] = std::move(packed);
        else
            edits[handle] = std::move(packed);
    }
    void put_object(std::string_view dxf, std::uint64_t handle, std::uint64_t owner,
                    const std::vector<std::uint64_t> &reactors, const std::function<void(ArchWriter &)> &fields,
                    const std::vector<CivilRef> &refs, const std::vector<std::string> &strings) {
        auto packed = encode_civil_object(civil_class_number(o, dxf), handle, owner, reactors, fields, refs, strings,
                                          storage.page);
        store(handle, packed, packed_civil_object(dxf, handle, owner, reactors, packed));
    }
    void write_root(std::uint64_t handle, const std::vector<std::pair<std::int32_t, std::uint64_t>> &entries,
                    std::vector<std::string> strings) {
        auto n = nod();
        std::vector<CivilRef> refs;
        for (auto &[k, h] : entries) {
            (void)k;
            refs.push_back({SOFT_OWNER, h});
        }
        put_object(
            "AECC_TREE_NODE", handle, n, {n},
            [&](ArchWriter &w) {
                w.main.bit_long(1);
                w.main.bit_long(static_cast<std::int32_t>(entries.size()));
                for (auto &[k, h] : entries) {
                    (void)h;
                    w.main.bit_long(k);
                }
                w.main.bit_long(0);
                write_bit_run(w.main, "010");
            },
            refs, strings);
        root_handle = handle;
    }
    void attach_root(std::uint64_t handle) {
        auto n = nod();
        require(o.metadata_records.count(n), "Missing named objects dictionary");
        auto rec = o.metadata_records.at(n);
        auto entries = rec.dictionary_entries;
        bool found = false;
        for (auto &[name, target] : entries)
            if (name == CIVIL_ROOT) {
                target = handle;
                found = true;
            }
        if (!found)
            entries.emplace_back(CIVIL_ROOT, handle);
        edits[n] = encode_named_dictionary(1032, storage.page, n, rec.owner, rec.extension_dictionary, rec.reactors,
                                           entries);
    }
    std::uint64_t ensure_root() {
        if (auto h = root())
            return *h;
        auto handle = seed.next();
        write_root(handle, {}, {"", "Pipe Network Interference Set Collection"});
        attach_root(handle);
        return handle;
    }
    void set_root_entry(std::int32_t key, std::uint64_t node) {
        auto handle = ensure_root();
        auto entries = root_entries().value_or(std::vector<std::pair<std::int32_t, std::uint64_t>>{});
        bool found = false;
        for (auto &e : entries)
            if (e.first == key) {
                e.second = node;
                found = true;
            }
        if (!found)
            bucket_insert(entries, {key, node}, [](auto *e) { return e->first; });
        auto strings = object(handle) ? object(handle)->decode().strings
                                      : std::vector<std::string>{"", "Pipe Network Interference Set Collection"};
        write_root(handle, entries, strings);
    }
    std::optional<CivilDefaults> defaults() const {
        CivilDefaults d;
        bool points = false, create = false, cls = false;
        auto consider = [&](const ArchObject &a) {
            auto decoded = a.decode();
            auto name = decoded.strings.empty() ? std::string() : decoded.strings.front();
            if (a.dxf_name == "AECC_SETTINGS_NODE" && name == "AeccPoints" && decoded.handles.size() >= 2) {
                d.label_style = decoded.handles[decoded.handles.size() - 2];
                d.point_style = decoded.handles.back();
                points = true;
            }
            if (a.dxf_name == "AECC_SETTINGS_NODE" && name == "AeccCreatePoints") {
                d.create_settings = a.handle;
                create = true;
            }
            if (a.dxf_name == "AECC_CLASS_NODE" && name == "AeccDbCogoPoint") {
                d.class_node = a.handle;
                cls = true;
            }
        };
        for (auto &[h, a] : o.arch_objects) {
            (void)h;
            consider(a);
        }
        for (auto &[h, a] : objects) {
            (void)h;
            consider(a);
        }
        if (points && create && cls && d.point_style && d.label_style)
            return d;
        return std::nullopt;
    }
    std::vector<CogoPoint> points() {
        auto h = root_node(KEY_POINTS);
        auto obj = h ? object(*h) : nullptr;
        return obj ? parse_cogo_points(*obj) : std::vector<CogoPoint>{};
    }
    void write_database(std::uint64_t handle, std::uint64_t owner, const std::vector<CogoPoint> &points,
                        std::uint64_t class_node) {
        std::vector<const CogoPoint *> styled, labelled, described;
        for (auto &p : points) {
            if (p.style)
                styled.push_back(&p);
            if (p.label_style)
                labelled.push_back(&p);
            if (p.description)
                described.push_back(&p);
        }
        std::vector<std::string> texts;
        std::vector<std::int32_t> tags;
        for (auto *p : described) {
            auto text = p->description.value_or("");
            auto i = std::find(texts.begin(), texts.end(), text);
            if (i == texts.end()) {
                texts.push_back(text);
                i = texts.end() - 1;
            }
            tags.push_back(static_cast<std::int32_t>(FIRST_DESCRIPTION_TAG + static_cast<std::uint32_t>(i - texts.begin())));
        }
        std::vector<CivilRef> refs;
        for (auto *p : styled)
            refs.push_back({HARD_POINTER, p->style});
        for (auto *p : labelled)
            refs.push_back({HARD_POINTER, p->label_style});
        refs.push_back({SOFT_POINTER, class_node});
        std::vector<std::string> strings{""};
        for (auto *p : described)
            strings.push_back(p->description.value_or(""));
        put_object(
            "AECC_POINT_NODE", handle, owner, {},
            [&](ArchWriter &w) {
                w.main.bit_long(KEY_POINTS);
                w.main.bit_long(0);
                w.main.bit_long(0);
                write_bit_run(w.main, "010");
                w.main.bit_long(static_cast<std::int32_t>(points.size()));
                for (auto &p : points) {
                    w.main.bit_long(1);
                    w.main.bit_double(p.position.x);
                    w.main.bit_double(p.position.y);
                    w.main.bit_double(p.position.z);
                    w.main.bit_long(static_cast<std::int32_t>(p.number));
                    w.bs(0x0420);
                }
                auto list = [&](const std::vector<const CogoPoint *> &v) {
                    w.main.bit_long(static_cast<std::int32_t>(v.size()));
                    for (auto *p : v)
                        w.main.bit_long(static_cast<std::int32_t>(p->number));
                };
                list(styled);
                list(labelled);
                for (int i = 0; i < 3; ++i)
                    w.main.bit_long(0);
                w.main.bit_long(static_cast<std::int32_t>(described.size()));
                for (std::size_t i = 0; i < described.size(); ++i) {
                    w.main.bit_long(static_cast<std::int32_t>(described[i]->number));
                    w.main.bit_long(tags[i]);
                }
                for (int i = 0; i < 13; ++i)
                    w.main.bit_long(0);
            },
            refs, strings);
    }
    void write_group_node(std::uint64_t handle, std::uint64_t owner, const std::vector<CivilGroupEntry> &groups,
                          std::uint64_t label_style, std::uint64_t point_style) {
        std::vector<CivilRef> refs;
        for (auto &g : groups)
            refs.push_back({SOFT_OWNER, g.handle});
        refs.push_back({HARD_POINTER, label_style});
        refs.push_back({HARD_POINTER, point_style});
        std::vector<std::string> strings{""};
        for (auto &g : groups)
            if (g.id != ALL_POINTS_ID)
                strings.push_back(g.name);
        put_object(
            "AECC_POINT_GROUP_NODE", handle, owner, {},
            [&](ArchWriter &w) {
                w.main.bit_long(KEY_POINT_GROUPS);
                w.main.bit_long(static_cast<std::int32_t>(groups.size()));
                for (auto &g : groups)
                    w.main.bit_long(g.id);
                w.main.bit_long(0);
                write_bit_run(w.main, "010");
            },
            refs, strings);
    }
    void write_outer_query(std::uint64_t handle, std::uint64_t owner, const std::vector<CivilQuery> &list) {
        std::vector<std::string> strings{""};
        for (auto &q : list)
            strings.push_back(q.title);
        strings.emplace_back();
        std::vector<CivilRef> refs;
        for (auto &q : list)
            refs.push_back({SOFT_OWNER, q.handle});
        put_object(
            "AECC_QUERY_STATEMENT", handle, owner, {},
            [&](ArchWriter &w) {
                w.main.bit_long(KEY_ALL_POINTS_QUERY);
                w.main.bit_long(static_cast<std::int32_t>(list.size()));
                for (auto &q : list)
                    w.main.bit_long(q.tag);
                write_bit_run(w.main, "100100010");
            },
            refs, strings);
    }
    void write_query(std::uint64_t handle, std::uint64_t owner, std::int32_t tag, std::string_view title,
                     const QuerySpec &c) {
        require(c.constants.size() <= 255 && !c.constants.empty(), "civil query is too large");
        auto k = c.constants.size();
        auto code = comparison_code(c.comparison);
        auto op = comparison_node(c.comparison);
        std::vector<std::string> strings{std::string(title), ""};
        for (auto &v : c.constants)
            strings.push_back(v.as_text());
        strings.push_back("AeccQueryNodePARENS");
        strings.emplace_back();
        for (std::size_t i = 1; i < k; ++i)
            strings.emplace_back("AeccQueryNodeOR");
        for (auto &v : c.constants) {
            for (auto part : {op, "AeccQueryNodeATTRID", "", "", "AeccQueryNodeCONST", "", ""})
                strings.emplace_back(part);
            if (v.kind == QueryValue::Kind::Text)
                strings.push_back(v.text);
        }
        put_object(
            "AECC_QUERY_STATEMENT", handle, owner, {},
            [&](ArchWriter &w) {
                auto head = [&] {
                    w.main.bits(3, c.value_type);
                    w.main.bits(8, c.flags);
                    write_bit_run(w.main, std::string(12, '0'));
                    w.main.bits(3, c.klass);
                };
                auto byte = [&](std::uint8_t v) { w.main.bits(8, v); };
                w.main.bit_long(tag);
                w.main.bit_long(0);
                w.main.bit_long(0);
                write_bit_run(w.main, "01000");
                bl_byte(w.main, static_cast<std::uint8_t>(k));
                write_bit_run(w.main, std::string(7, '0'));
                head();
                w.main.bits(1, c.number_attr);
                w.main.bits(1, 1);
                w.main.bits(1, k == 1);
                byte(k == 1 ? code : CIVIL_EQUAL);
                write_bit_run(w.main, std::string(15, '0'));
                for (std::size_t j = 1; j < k; ++j) {
                    head();
                    w.main.bits(1, c.number_attr);
                    w.main.bits(1, 0);
                    w.main.bits(1, j == k - 1);
                    byte(1);
                    byte(3);
                    write_bit_run(w.main, std::string(7, '0'));
                }
                for (std::size_t i = 0; i < k; ++i) {
                    head();
                    w.main.bits(1, c.number_attr);
                    w.main.bit_long(c.attr_id);
                    auto &v = c.constants[i];
                    if (v.kind == QueryValue::Kind::Number)
                        w.main.bit_long(static_cast<std::int32_t>(v.number));
                    else if (v.kind == QueryValue::Kind::Double)
                        w.bd(v.real);
                    if (i + 1 < k)
                        write_bit_run(w.main, std::string(7, '0'));
                }
            },
            {}, strings);
    }
    std::uint64_t set_query(std::string_view title, std::int32_t tag, const QuerySpec &spec) {
        auto owner = ensure_root();
        std::uint64_t outer = 0;
        if (auto h = root_node(KEY_ALL_POINTS_QUERY))
            outer = *h;
        else {
            outer = seed.next();
            write_outer_query(outer, owner, {});
            set_root_entry(KEY_ALL_POINTS_QUERY, outer);
        }
        auto obj = object(outer);
        require(obj, "outer query statement cannot be read");
        auto list = parse_queries(*obj).value_or(std::vector<CivilQuery>{});
        std::uint64_t handle = 0;
        bool found = false;
        for (auto &q : list)
            if (q.tag == tag) {
                q.title = std::string(title);
                handle = q.handle;
                found = true;
            }
        if (!found) {
            handle = seed.next();
            bucket_insert(list, CivilQuery{tag, handle, std::string(title)}, [](auto *q) { return q->tag; });
        }
        write_query(handle, outer, tag, title, spec);
        write_outer_query(outer, owner, list);
        return handle;
    }
    void write_group(std::uint64_t handle, std::uint64_t owner, const CivilGroup &g) {
        Vector3 min{1e20, 1e20, 1e20}, max{-1e20, -1e20, -1e20};
        if (g.extents)
            std::tie(min, max) = *g.extents;
        std::vector<std::string> strings;
        if (g.name)
            strings = {*g.name, g.description, "AeccImpPG"};
        else
            strings = {"", "AeccImpAllPointsPG"};
        std::vector<CivilRef> refs;
        for (auto h : g.refs)
            refs.push_back({HARD_POINTER, h});
        refs.push_back({HARD_POINTER, 0});
        refs.push_back({HARD_POINTER, 0});
        put_object(
            "AECC_POINT_PG", handle, owner, {},
            [&](ArchWriter &w) {
                w.main.bit_long(g.id);
                w.main.bit_long(0);
                w.main.bit_long(0);
                bl_byte(w.main, 0);
                write_bit_run(w.main, "00000100000");
                w.main.bit_long(g.serial);
                w.main.bit_long(static_cast<std::int32_t>(g.numbers.size()));
                for (auto n : g.numbers)
                    w.main.little_endian(4, n);
                write_bit_run(w.main, "00");
                w.main.bit_double(0);
                for (auto v : {min.x, min.y, min.z, max.x, max.y, max.z})
                    w.main.bit_double(v);
                write_bit_run(w.main, GROUP_TAIL);
            },
            refs, strings);
    }
    struct Support {
        std::uint64_t database = 0, group_node = 0, all_points = 0, class_node = 0;
        CivilDefaults defaults;
    };
    Support support() {
        auto d = defaults();
        require(d.has_value(), "drawing has no civil point styles");
        ensure_root();
        if (auto database = root_node(KEY_POINTS))
            if (auto group_node = root_node(KEY_POINT_GROUPS)) {
                auto node = object(*group_node);
                require(node, "point group node cannot be read");
                auto handles = node->decode().handles;
                std::optional<std::uint64_t> all_points;
                for (auto h : handles)
                    if (auto g = object(h)) {
                        auto strings = g->decode().strings;
                        if (std::find(strings.begin(), strings.end(), "AeccImpAllPointsPG") != strings.end())
                            all_points = h;
                    }
                require(all_points.has_value(), "point group node has no _All Points group");
                auto class_node = object(*database) && !object(*database)->decode().handles.empty()
                                      ? object(*database)->decode().handles.back()
                                      : 0;
                return {*database, *group_node, *all_points, class_node, *d};
            }
        auto owner = ensure_root();
        Support s;
        s.defaults = *d;
        s.class_node = d->class_node;
        s.database = seed.next();
        s.group_node = seed.next();
        s.all_points = seed.next();
        write_database(s.database, owner, {}, s.class_node);
        write_group_node(s.group_node, owner, {{ALL_POINTS_ID, s.all_points, ""}}, d->label_style, d->point_style);
        QuerySpec all_query;
        all_query.comparison = Comparison::Greater;
        all_query.constants.push_back({QueryValue::Kind::Number, 0});
        auto inner = set_query("Point Group Query - _All Points", QUERY_TAG, all_query);
        write_all_points(s, inner, {});
        set_root_entry(KEY_POINTS, s.database);
        set_root_entry(KEY_POINT_GROUPS, s.group_node);
        return s;
    }
    void write_all_points(const Support &s, std::uint64_t query, std::vector<std::uint32_t> numbers) {
        std::sort(numbers.begin(), numbers.end());
        std::array<std::uint64_t, 6> refs{};
        if (auto old = object(s.all_points)) {
            auto handles = old->decode().handles;
            if (!handles.empty())
                refs[0] = handles[0];
            refs[1] = handles.size() > 1 ? handles[1] : s.defaults.point_style;
            refs[5] = handles.size() > 5 ? handles[5] : s.defaults.create_settings;
        } else
            refs[1] = s.defaults.point_style, refs[5] = s.defaults.create_settings;
        refs[2] = query;
        refs[3] = s.database;
        refs[4] = s.group_node;
        CivilGroup g;
        g.id = ALL_POINTS_ID;
        g.serial = 0;
        g.numbers = std::move(numbers);
        g.refs = refs;
        write_group(s.all_points, s.group_node, g);
    }
    void refresh_groups(const Support &s, const std::vector<CogoPoint> &points) {
        auto node = object(s.group_node);
        auto list = node ? parse_group_entries(*node).value_or(std::vector<CivilGroupEntry>{})
                         : std::vector<CivilGroupEntry>{};
        for (auto &e : list) {
            auto group = object(e.handle);
            if (e.handle == s.all_points || !group || !civil_plain_group(*group))
                continue;
            auto decoded = group->decode();
            if (decoded.handles.size() < 3)
                continue;
            auto query_obj = object(decoded.handles[2]);
            auto query = query_obj ? parse_query(*query_obj) : std::nullopt;
            if (!query)
                continue;
            auto serial = parse_group_serial(*group);
            if (!serial)
                continue;
            auto [numbers, extents] = query_members(points, *query);
            CivilGroup g;
            g.id = e.id;
            g.serial = *serial;
            g.name = e.name;
            g.description = decoded.strings.size() > 1 ? decoded.strings[1] : "";
            g.numbers = numbers;
            g.extents = extents;
            for (int j = 0; j < 6; ++j)
                g.refs[static_cast<std::size_t>(j)] =
                    static_cast<std::size_t>(j) < decoded.handles.size() ? decoded.handles[static_cast<std::size_t>(j)] : 0;
            write_group(e.handle, s.group_node, g);
        }
    }
    void store_points(const Support &s, const std::vector<CogoPoint> &points) {
        auto owner = root();
        require(owner.has_value(), "drawing has no root tree node");
        write_database(s.database, *owner, points, s.class_node);
        auto query = object(s.all_points) && object(s.all_points)->decode().handles.size() > 2
                         ? object(s.all_points)->decode().handles[2]
                         : 0;
        std::vector<std::uint32_t> numbers;
        for (auto &p : points)
            numbers.push_back(p.number);
        write_all_points(s, query, numbers);
        refresh_groups(s, points);
    }
    std::uint64_t put_point(std::optional<std::uint64_t> handle, std::uint32_t number, std::optional<std::string> layer) {
        auto name = layer.value_or("0");
        auto layer_handle = civil_layer(o, name);
        require(layer_handle, "layer does not exist");
        auto id = handle.value_or(seed.next());
        auto packed = encode_cogo_entity(civil_class_number(o, "AECC_COGO_POINT"), id, layer_handle, number, storage.page);
        if (added.count(id) || !o.records.count(id))
            added[id] = packed;
        else
            edits[id] = packed;
        if (!handle)
            new_entity = id;
        return id;
    }
    std::uint32_t point_number(std::uint64_t handle) {
        auto i = o.arch_entities.find(handle);
        require(i != o.arch_entities.end(), "handle is not a COGO point");
        auto n = parse_entity_number(i->second);
        require(n.has_value(), "handle is not a COGO point");
        return *n;
    }
    std::int32_t next_group_serial(const std::vector<CivilGroupEntry> &list) {
        std::int32_t read = 0;
        for (auto &e : list)
            if (auto obj = object(e.handle))
                if (auto serial = parse_group_serial(*obj))
                    read = std::max(read, *serial);
        return std::max(read, static_cast<std::int32_t>(list.size())) + 1;
    }
    void write_named_group(const Support &s, std::uint64_t handle, std::int32_t id, std::int32_t serial,
                           std::string_view name, std::string_view description, const PointQuery &query) {
        auto spec = query_spec(query);
        auto qh = set_query(std::string("Point Group Query - ") + std::string(name),
                            static_cast<std::int32_t>(static_cast<std::uint32_t>(id) + 1), spec);
        auto [numbers, extents] = query_members(points(), query);
        CivilGroup g;
        g.id = id;
        g.serial = serial;
        g.name = std::string(name);
        g.description = std::string(description);
        g.numbers = numbers;
        g.extents = extents;
        g.refs = {s.defaults.label_style, s.defaults.point_style, qh, s.database, s.group_node,
                  s.defaults.create_settings};
        write_group(handle, s.group_node, g);
    }
    enum class NodeTail { Tree, Settings };
    void write_node(std::string_view dxf, std::uint64_t handle, std::uint64_t owner, std::uint32_t key,
                    const std::vector<std::pair<std::uint32_t, std::uint64_t>> &children, NodeTail tail,
                    const std::vector<CivilRef> &extra, const std::vector<std::string> &strings) {
        std::vector<CivilRef> refs;
        for (auto &[k, h] : children) {
            (void)k;
            refs.push_back({SOFT_OWNER, h});
        }
        refs.insert(refs.end(), extra.begin(), extra.end());
        put_object(
            dxf, handle, owner, {},
            [&](ArchWriter &w) {
                w.main.bit_long(static_cast<std::int32_t>(key));
                w.main.bit_long(static_cast<std::int32_t>(children.size()));
                for (auto &[k, h] : children) {
                    (void)h;
                    w.main.bit_long(static_cast<std::int32_t>(k));
                }
                w.main.bit_long(0);
                if (tail == NodeTail::Tree)
                    write_bit_run(w.main, "010");
                else {
                    w.main.bits(1, 0);
                    w.main.bit_long(0);
                }
            },
            refs, strings);
    }
    std::vector<std::string> node_texts(std::string_view own, bool text_key, const std::vector<std::string> &named,
                                        const std::vector<std::string> &tail) {
        std::vector<std::string> v{std::string(own)};
        if (text_key)
            v.emplace_back();
        v.insert(v.end(), named.begin(), named.end());
        v.insert(v.end(), tail.begin(), tail.end());
        return v;
    }
    struct StyleComponent {
        std::int32_t index = 0;
        bool visible = true;
        std::uint32_t color = 0xC1000000;
        std::int16_t lineweight = -1;
        std::uint64_t linetype = 0;
        double scale = 1;
    };
    enum class StyleValue { Text, Reference, Long, Double, Byte, Time, Color };
    struct StyleKey {
        std::uint32_t key = 0;
        StyleValue kind = StyleValue::Text;
        std::int32_t long_v = 0;
        double double_v = 0;
        std::uint8_t byte_v = 0;
        std::uint32_t color_v = 0;
        std::uint64_t handle_v = 0;
        const char *text_v = "";
    };
    static StyleKey sk_d(std::uint32_t key, double v) { return {key, StyleValue::Double, 0, v}; }
    static StyleKey sk_b(std::uint32_t key, std::uint8_t v) { return {key, StyleValue::Byte, 0, 0, v}; }
    static StyleKey sk_l(std::uint32_t key, std::int32_t v) { return {key, StyleValue::Long, v}; }
    static StyleKey sk_t(std::uint32_t key, const char *text) {
        StyleKey k{key, StyleValue::Text};
        k.text_v = text;
        return k;
    }
    static StyleKey sk_c(std::uint32_t key, std::uint32_t color) {
        StyleKey k{key, StyleValue::Color};
        k.color_v = color;
        return k;
    }
    static StyleKey sk_r(std::uint32_t key, std::uint64_t handle) {
        StyleKey k{key, StyleValue::Reference};
        k.handle_v = handle;
        return k;
    }
    static StyleKey sk_time(std::uint32_t key) { return {key, StyleValue::Time}; }
#include "civil_surface_style.inc"
    void write_style(std::string_view dxf, std::uint64_t handle, std::uint64_t owner, std::int32_t id,
                     const std::vector<std::pair<std::int32_t, std::uint64_t>> &children,
                     const std::vector<StyleKey> &keys, std::uint64_t own, const std::array<std::vector<StyleComponent>, 4> &lists,
                     const std::vector<CivilRef> &class_refs, std::uint64_t layer,
                     const std::function<void(ArchWriter &)> &class_data,
                     const std::vector<std::uint32_t> &pointer_table = {}) {
        std::size_t count = 0;
        for (auto &list : lists)
            count += list.size();
        std::vector<CivilRef> refs;
        for (auto &[k, h] : children) {
            (void)k;
            refs.push_back({SOFT_OWNER, h});
        }
        for (auto &key : keys)
            if (key.kind == StyleValue::Reference) {
                auto code = (key.key >> 24) & 0x3F;
                auto kind = code == 0x0F ? SOFT_POINTER : code == 0x12 ? HARD_OWNER : HARD_POINTER;
                refs.push_back({kind, key.handle_v});
            }
        for (std::size_t i = 0; i < pointer_table.size(); ++i)
            refs.push_back({HARD_POINTER, 0});
        refs.push_back({SOFT_POINTER, own});
        for (auto &list : lists)
            for (auto &c : list) {
                refs.push_back({HARD_POINTER, layer});
                refs.push_back({HARD_POINTER, c.linetype});
                refs.push_back({HARD_POINTER, 0});
            }
        refs.insert(refs.end(), class_refs.begin(), class_refs.end());
        std::vector<std::string> strings{""};
        for (auto &key : keys)
            if (key.kind == StyleValue::Text)
                strings.emplace_back(key.text_v ? key.text_v : "");
        if (!pointer_table.empty())
            strings.emplace_back("class AeccAttributeTable<int,class AcDbHardPointerId>");
        strings.resize(strings.size() + 2 * count);
        auto now = civil_file_time();
        put_object(
            dxf, handle, owner, {},
            [&](ArchWriter &w) {
                w.main.bit_long(id);
                w.main.bit_long(static_cast<std::int32_t>(children.size()));
                for (auto &[k, h] : children) {
                    (void)h;
                    w.main.bit_long(k);
                }
                w.main.bit_long(0);
                w.main.bit_long(0);
                w.main.bits(1, 1);
                write_byte(w.main, static_cast<std::uint8_t>(keys.size()));
                for (auto &key : keys) {
                    w.main.bit_long(static_cast<std::int32_t>(key.key));
                    switch (key.kind) {
                    case StyleValue::Text:
                    case StyleValue::Reference:
                        break;
                    case StyleValue::Long:
                        w.main.bit_long(key.long_v);
                        break;
                    case StyleValue::Double:
                        w.bd(key.double_v);
                        break;
                    case StyleValue::Byte:
                        write_byte(w.main, key.byte_v);
                        break;
                    case StyleValue::Color:
                        w.bs(0);
                        w.main.bit_long(static_cast<std::int32_t>(key.color_v));
                        write_byte(w.main, 0);
                        break;
                    case StyleValue::Time:
                        w.main.little_endian(8, now);
                        break;
                    }
                }
                if (!pointer_table.empty()) {
                    w.main.bit_long(0);
                    w.main.bits(1, 1);
                    write_byte(w.main, static_cast<std::uint8_t>(pointer_table.size()));
                    for (auto key : pointer_table)
                        w.main.bit_long(static_cast<std::int32_t>(key));
                    w.main.bit_long(1);
                    w.main.bits(1, 1);
                }
                w.main.bit_long(32);
                write_bit_run(w.main, "001");
                for (auto &list : lists) {
                    w.main.bit_long(static_cast<std::int32_t>(list.size()));
                    for (auto &c : list) {
                        w.main.bit_long(c.index);
                        for (auto v : {80, 80, 0, 80})
                            w.bs(static_cast<std::int16_t>(v));
                        w.main.bits(1, c.visible);
                        w.bs(0);
                        w.main.bit_long(static_cast<std::int32_t>(c.color));
                        write_byte(w.main, 0);
                        w.bs(c.lineweight);
                        w.bd(c.scale);
                        for (auto v : {1, 0, 0x01000000, 0})
                            w.main.bit_long(v);
                    }
                }
                for (int i = 0; i < 4; ++i)
                    w.main.bit_long(0);
                class_data(w);
            },
            refs, strings);
    }
    void write_point_style(std::uint64_t handle, std::uint64_t owner, std::int32_t id,
                           const std::vector<std::pair<std::int32_t, std::uint64_t>> &children, std::uint64_t layer,
                           std::uint64_t by_block, std::uint64_t by_layer) {
        auto plan = [&](std::int32_t index, bool visible) {
            return StyleComponent{index, visible, 0xC1000000, -2, by_block, 1};
        };
        StyleComponent other{0, true, 0xC1000000, -1, by_layer, 1};
        std::array<std::vector<StyleComponent>, 4> lists{
            std::vector<StyleComponent>{plan(0, true), plan(1, true)},
            std::vector<StyleComponent>{plan(0, false), plan(1, false)},
            std::vector<StyleComponent>{other},
            std::vector<StyleComponent>{other}};
        std::vector<StyleKey> keys{{0x04001008, StyleValue::Text},
                                   {0x04001010, StyleValue::Text},
                                   {0x1600101F, StyleValue::Time},
                                   {0x1600101E, StyleValue::Time}};
        write_style(
            "AECC_POINT_STYLE", handle, owner, id, children, keys, 0, lists, {{HARD_POINTER, 0}}, layer,
            [](ArchWriter &w) {
                w.bs(3);
                write_bit_run(w.main, "10");
                w.bd(0.1 / 12.0);
                write_byte(w.main, 0);
                write_bit_run(w.main, "00000001010101000010010100000001000000001001");
            });
    }
    void write_alignment_style(std::uint64_t handle, std::uint64_t owner, std::int32_t id,
                               const std::vector<std::pair<std::int32_t, std::uint64_t>> &children, std::uint64_t own,
                               std::uint64_t layer, std::uint64_t by_block) {
        const std::int32_t indices[] = {0, 1, 2, 3, 4, 5, 11, 20};
        auto view = [&](bool shown) {
            std::vector<StyleComponent> list;
            for (auto i : indices)
                list.push_back({i, shown && i != 11, 0xC0000000, -1, by_block, 1});
            return list;
        };
        std::array<std::vector<StyleComponent>, 4> lists{
            view(true), view(false), {}, {{9, true, 0xC1000000, -1, by_block, 1}}};
        std::vector<StyleKey> keys{
            {0x1000E409, StyleValue::Reference}, {0x0D00E300, StyleValue::Long, 4096},
            {0x1000E406, StyleValue::Reference}, {0x04001008, StyleValue::Text},
            {0x1000E403, StyleValue::Reference}, {0x0200E122, StyleValue::Double, 0, 1.0},
            {0x1000E400, StyleValue::Reference}, {0x0300E11F, StyleValue::Byte, 0, 0, 0},
            {0x1000E40E, StyleValue::Reference}, {0x04001010, StyleValue::Text},
            {0x1000E40B, StyleValue::Reference}, {0x1000E408, StyleValue::Reference},
            {0x1000E405, StyleValue::Reference}, {0x0200E124, StyleValue::Double, 0, 1.0},
            {0x1000E402, StyleValue::Reference}, {0x0200E121, StyleValue::Double, 0, 0.18},
            {0x1000B052, StyleValue::Reference}, {0x1000E40D, StyleValue::Reference},
            {0x1600101F, StyleValue::Time},      {0x1000E40A, StyleValue::Reference},
            {0x0D00E301, StyleValue::Long, 256}, {0x1000E407, StyleValue::Reference},
            {0x1000E404, StyleValue::Reference}, {0x0200E123, StyleValue::Double, 0, 1.0},
            {0x1000E401, StyleValue::Reference}, {0x0200E120, StyleValue::Double, 0, 0.001},
            {0x1000E40F, StyleValue::Reference}, {0x1000E40C, StyleValue::Reference},
            {0x1600101E, StyleValue::Time}};
        write_style("AECC_ALIGNMENT_STYLE", handle, owner, id, children, keys, own, lists, {}, layer,
                    [](ArchWriter &) {});
    }
    void write_surface_style(std::uint64_t handle, std::uint64_t owner, std::int32_t id,
                             const std::vector<std::pair<std::int32_t, std::uint64_t>> &children, std::uint64_t own,
                             std::uint64_t layer, std::uint64_t by_block) {
        const std::uint32_t colors[] = {0xC3000001, 0xC3000004, 0xC3000002, 0xC3000003, 0xC300002A, 0xC0000000,
                                        0xC3000006, 0xC0000000, 0xC0000000, 0xC0000000, 0xC0000000, 0xC0000000};
        auto view = [&](int shown) {
            std::vector<StyleComponent> list;
            for (int i = 0; i < 12; ++i)
                list.push_back({i, i == shown, colors[static_cast<std::size_t>(i)], -2, by_block, 1});
            return list;
        };
        std::array<std::vector<StyleComponent>, 4> lists{
            view(2), view(1), {}, {{12, false, 0xC3000002, -2, by_block, 1}}};
        write_style("AECC_SURFACE_STYLE", handle, owner, id, children, surface_style_keys(civil_linetype(o, "Continuous"),
                                                                                         civil_text_style(o, "Standard")),
                    own, lists, {}, layer, [](ArchWriter &w) { write_byte(w.main, 0); });
    }
    void write_profile_style(std::uint64_t handle, std::uint64_t owner, std::int32_t id,
                             const std::vector<std::pair<std::int32_t, std::uint64_t>> &children, std::uint64_t own,
                             std::uint64_t layer, std::uint64_t by_block) {
        auto extension = seed.next();
        auto c = [&](std::int32_t index, std::uint32_t color, double scale) {
            return StyleComponent{index, true, color, -1, by_block, scale};
        };
        std::vector<StyleComponent> profile;
        for (auto i : {0, 1, 2, 3, 4, 5, 6, 11})
            profile.push_back(c(i, 0xC0000000, 2.0));
        std::array<std::vector<StyleComponent>, 4> lists{
            std::vector<StyleComponent>{}, std::vector<StyleComponent>{c(9, 0xC0000000, 2.0)}, std::move(profile),
            std::vector<StyleComponent>{c(10, 0xC1000000, 1.0)}};
        std::vector<StyleKey> keys{
            sk_r(0x10010130, 0),       sk_l(0x0D00E300, 4097), sk_b(0x03010108, 1), sk_d(0x02010105, 5.0),
            sk_t(0x04001008, ""),      sk_d(0x0200E122, 1.0),  sk_r(0x10010127, 0), sk_d(0x02010110, 1.0),
            sk_r(0x10010124, 0),       sk_t(0x04001010, ""),   sk_d(0x0200E124, 1.0), sk_r(0x10010129, 0),
            sk_d(0x0200E121, 0.2),     sk_r(0x10010126, 0),    sk_d(0x0201010F, 5.0), sk_r(0x10010123, 0),
            sk_r(0x1000B052, 0),       sk_time(0x1600101F),    sk_r(0x12010131, extension), sk_r(0x10010120, 0),
            sk_l(0x0D00E301, 258),     sk_b(0x03010109, 1),    sk_d(0x02010106, 5.0), sk_d(0x0200E123, 1.0),
            sk_r(0x10010128, 0),       sk_r(0x1000E401, 0),    sk_r(0x10010125, 0), sk_d(0x0201010E, 5.0),
            sk_r(0x10010122, 0),       sk_time(0x1600101E)};
        write_style("AECC_PROFILE_STYLE", handle, owner, id, children, keys, own, lists, {}, layer, [](ArchWriter &) {},
                    {0x10010101});
        std::array<std::vector<StyleComponent>, 4> ext{
            std::vector<StyleComponent>{}, std::vector<StyleComponent>{},
            std::vector<StyleComponent>{c(12, 0xC0000000, 1.0)}, std::vector<StyleComponent>{}};
        std::vector<StyleKey> ext_keys{{0x04001008, StyleValue::Text},
                                       {0x04001010, StyleValue::Text},
                                       {0x1600101F, StyleValue::Time},
                                       {0x1600101E, StyleValue::Time}};
        write_style("AECC_VALIGNMENT_STYLE_EXTENSION", extension, handle, 0, {}, ext_keys, 0, ext, {}, layer,
                    [](ArchWriter &) {});
    }
    void ensure_profile_tree() {
        if (!root_node(KEY_STYLES) && !ensure_point_tree())
            throw std::invalid_argument("drawing has a civil tree without a style tree");
        auto styles = root_node(KEY_STYLES);
        require(styles.has_value(), "drawing has no style tree");
        auto tree = object(*styles);
        require(tree, "style tree cannot be read");
        auto parsed = parse_civil_node(*tree);
        require(parsed.has_value(), "style tree cannot be read");
        for (auto &[k, h] : parsed->children) {
            (void)h;
            if (k == KEY_PROFILE_STYLES)
                return;
        }
        auto layer = civil_layer(o, "0");
        auto by_block = civil_linetype(o, "ByBlock");
        require(layer, "drawing has no layer 0");
        require(by_block, "drawing lacks the ByBlock linetype");
        auto head = seed.next(), def = seed.next();
        write_profile_style(head, *styles, KEY_PROFILE_STYLES, {{DEFAULT_STYLE_ID, def}}, def, layer, by_block);
        write_profile_style(def, head, DEFAULT_STYLE_ID, {}, 0, layer, by_block);
        add_node_child(*styles, KEY_PROFILE_STYLES, head, "");
    }
    std::optional<std::uint64_t> default_profile_style() {
        auto tree = root_node(KEY_STYLES);
        if (!tree)
            return std::nullopt;
        auto obj = object(*tree);
        if (!obj)
            return std::nullopt;
        auto parsed = parse_civil_node(*obj);
        if (!parsed)
            return std::nullopt;
        for (auto &[k, h] : parsed->children)
            if (k == KEY_PROFILE_STYLES) {
                auto head = object(h);
                if (!head)
                    return std::nullopt;
                auto handles = head->decode().handles;
                if (handles.empty() || !handles.front())
                    return std::nullopt;
                return handles.front();
            }
        return std::nullopt;
    }
    bool ensure_point_tree() {
        ensure_root();
        if (root_node(KEY_STYLES) || root_node(KEY_CLASSES) || root_node(KEY_SETTINGS))
            return false;
        auto layer = civil_layer(o, "0");
        auto by_block = civil_linetype(o, "ByBlock");
        auto by_layer = civil_linetype(o, "ByLayer");
        require(layer, "drawing has no layer 0");
        require(by_block && by_layer, "drawing lacks the ByBlock or ByLayer linetype");
        auto styles = seed.next(), head = seed.next(), style = seed.next(), classes = seed.next(), cls = seed.next(),
             classification = seed.next(), settings = seed.next(), points = seed.next(), create = seed.next();
        auto root = ensure_root();
        write_node("AECC_TREE_NODE", styles, root, KEY_STYLES, {{static_cast<std::uint32_t>(KEY_POINT_STYLES), head}},
                   NodeTail::Tree, {}, node_texts("", false, {}, {}));
        write_point_style(head, styles, KEY_POINT_STYLES, {{DEFAULT_STYLE_ID, style}}, layer, by_block, by_layer);
        write_point_style(style, head, DEFAULT_STYLE_ID, {}, layer, by_block, by_layer);
        const std::string name = "AeccDbCogoPoint";
        write_node("AECC_CLASS_NODE", classes, root, KEY_CLASSES, {{TEXT_POINT_CLASS, cls}}, NodeTail::Tree,
                   {{HARD_POINTER, 0}}, node_texts("", false, {name}, {}));
        write_node("AECC_CLASS_NODE", cls, classes, TEXT_POINT_CLASS,
                   {{static_cast<std::uint32_t>(KEY_CLASSIFICATION), classification}}, NodeTail::Tree,
                   {{HARD_POINTER, 0}}, node_texts(name, true, {}, {name}));
        write_node("AECC_USER_DEFINED_ATTRIBUTE_CLASSIFICATION", classification, cls,
                   static_cast<std::uint32_t>(KEY_CLASSIFICATION), {}, NodeTail::Tree, {}, node_texts("", false, {}, {}));
        write_node("AECC_ROOT_SETTINGS_NODE", settings, root, KEY_SETTINGS, {{TEXT_POINTS, points}}, NodeTail::Settings,
                   {{SOFT_POINTER, 0}}, node_texts("", false, {"AeccPoints"}, {}));
        write_node("AECC_SETTINGS_NODE", points, settings, TEXT_POINTS, {{TEXT_CREATE_POINTS, create}},
                   NodeTail::Settings, {{SOFT_POINTER, 0}, {SOFT_POINTER, style}},
                   node_texts("AeccPoints", true, {"AeccCreatePoints"}, {}));
        write_node("AECC_SETTINGS_NODE", create, points, TEXT_CREATE_POINTS, {}, NodeTail::Settings, {},
                   node_texts("AeccCreatePoints", true, {}, {}));
        set_root_entry(KEY_STYLES, styles);
        set_root_entry(KEY_CLASSES, classes);
        set_root_entry(KEY_SETTINGS, settings);
        return true;
    }
    void add_node_child(std::uint64_t node, std::int32_t key, std::uint64_t child, std::string_view name) {
        auto obj = object(node);
        require(obj, "node record cannot be read");
        auto parsed = parse_civil_node(*obj);
        require(parsed.has_value(), "node record cannot be read");
        auto n = *parsed;
        std::size_t name_i = 0;
        std::vector<std::tuple<std::int32_t, std::uint64_t, std::optional<std::string>>> list;
        for (auto &c : n.children) {
            std::optional<std::string> label;
            if (civil_text_key(c.first)) {
                label = name_i < n.names.size() ? n.names[name_i] : "";
                ++name_i;
            }
            list.push_back({c.first, c.second, label});
        }
        auto bucket = [](std::int32_t k) { return static_cast<std::uint32_t>(k) % 8; };
        auto at = list.size();
        for (std::size_t i = 0; i < list.size(); ++i)
            if (bucket(std::get<0>(list[i])) > bucket(key)) {
                at = i;
                break;
            }
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(at),
                    {key, child, civil_text_key(key) ? std::optional<std::string>(std::string(name)) : std::nullopt});
        std::vector<CivilRef> refs;
        for (auto &[k, h, label] : list) {
            (void)k;
            (void)label;
            refs.push_back({SOFT_OWNER, h});
        }
        std::vector<std::string> strings = n.own;
        for (auto &[k, h, label] : list) {
            (void)k;
            (void)h;
            if (label)
                strings.push_back(*label);
        }
        strings.insert(strings.end(), n.texts.begin(), n.texts.end());
        put_object(
            obj->dxf_name, node, obj->owner, obj->reactors,
            [&](ArchWriter &w) {
                w.main.bit_long(n.key);
                w.main.bit_long(static_cast<std::int32_t>(list.size()));
                for (auto &[k, h, label] : list) {
                    (void)h;
                    (void)label;
                    w.main.bit_long(k);
                }
                write_bit_run(w.main, n.bits);
            },
            refs, strings);
    }
    void ensure_alignment_tree() {
        if (!root_node(KEY_STYLES) && !ensure_point_tree())
            throw std::invalid_argument("drawing has a civil tree without a style tree");
        auto root = ensure_root();
        auto styles = root_node(KEY_STYLES);
        require(styles.has_value(), "drawing has no style tree");
        auto layer = civil_layer(o, "0");
        auto by_block = civil_linetype(o, "ByBlock");
        require(layer, "drawing has no layer 0");
        require(by_block, "drawing lacks the ByBlock or ByLayer linetype");
        auto tree = object(*styles);
        require(tree, "style tree cannot be read");
        auto parsed = parse_civil_node(*tree);
        require(parsed.has_value(), "style tree cannot be read");
        bool has_alignment = false;
        for (auto &[k, h] : parsed->children) {
            (void)h;
            if (k == KEY_ALIGNMENT_STYLES)
                has_alignment = true;
        }
        if (!has_alignment) {
            auto head = seed.next(), def = seed.next();
            write_alignment_style(head, *styles, KEY_ALIGNMENT_STYLES, {{DEFAULT_STYLE_ID, def}}, def, layer, by_block);
            write_alignment_style(def, head, DEFAULT_STYLE_ID, {}, 0, layer, by_block);
            add_node_child(*styles, KEY_ALIGNMENT_STYLES, head, "");
        }
        if (!root_node(KEY_SITELESS)) {
            auto siteless = seed.next(), feature = seed.next();
            write_node("AECC_ALIGNMENT_PARCEL_NODE", siteless, root, static_cast<std::uint32_t>(KEY_SITELESS),
                       {{static_cast<std::uint32_t>(KEY_DEFAULT_FEATURE), feature}}, NodeTail::Tree, {},
                       node_texts("", false, {}, {}));
            put_object(
                "AECC_FEATURE_STYLE", feature, siteless, {},
                [](ArchWriter &w) {
                    for (auto v : {KEY_DEFAULT_FEATURE, 0, 0, 32, 210})
                        w.main.bit_long(v);
                    write_bit_run(w.main, "000000110010000000110");
                    w.main.bit_long(static_cast<std::int32_t>(0x80000001u));
                    for (auto v : {1, 0, 0, 1, 0, 2, 0, 0, 1, 100, 1, 100, 1, 100, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0})
                        w.main.bit_long(v);
                },
                {}, {""});
            set_root_entry(KEY_SITELESS, siteless);
        }
    }
    void ensure_surface_tree() {
        if (!root_node(KEY_STYLES) && !ensure_point_tree())
            throw std::invalid_argument("drawing has a civil tree without a style tree");
        auto styles = root_node(KEY_STYLES);
        require(styles.has_value(), "drawing has no style tree");
        auto layer = civil_layer(o, "0");
        auto by_block = civil_linetype(o, "ByBlock");
        require(layer, "drawing has no layer 0");
        require(by_block, "drawing lacks the ByBlock linetype");
        auto tree = object(*styles);
        require(tree, "style tree cannot be read");
        auto parsed = parse_civil_node(*tree);
        require(parsed.has_value(), "style tree cannot be read");
        for (auto &[k, h] : parsed->children) {
            (void)h;
            if (k == KEY_SURFACE_STYLES)
                return;
        }
        auto head = seed.next(), def = seed.next();
        write_surface_style(head, *styles, KEY_SURFACE_STYLES, {{DEFAULT_STYLE_ID, def}}, def, layer, by_block);
        write_surface_style(def, head, DEFAULT_STYLE_ID, {}, 0, layer, by_block);
        add_node_child(*styles, KEY_SURFACE_STYLES, head, "");
    }
    std::optional<std::uint64_t> default_alignment_style() {
        auto tree = root_node(KEY_STYLES);
        if (!tree)
            return std::nullopt;
        auto obj = object(*tree);
        if (!obj)
            return std::nullopt;
        auto parsed = parse_civil_node(*obj);
        if (!parsed)
            return std::nullopt;
        for (auto &[k, h] : parsed->children)
            if (k == KEY_ALIGNMENT_STYLES) {
                auto head = object(h);
                if (!head)
                    return std::nullopt;
                auto handles = head->decode().handles;
                if (handles.empty() || !handles.front())
                    return std::nullopt;
                return handles.front();
            }
        return std::nullopt;
    }
    std::optional<std::uint64_t> default_surface_style() {
        auto tree = root_node(KEY_STYLES);
        if (!tree)
            return std::nullopt;
        auto obj = object(*tree);
        if (!obj)
            return std::nullopt;
        auto parsed = parse_civil_node(*obj);
        if (!parsed)
            return std::nullopt;
        for (auto &[k, h] : parsed->children)
            if (k == KEY_SURFACE_STYLES) {
                auto head = object(h);
                if (!head)
                    return std::nullopt;
                auto handles = head->decode().handles;
                if (handles.empty() || !handles.front())
                    return std::nullopt;
                return handles.front();
            }
        return std::nullopt;
    }
    std::optional<std::uint64_t> companion_node() {
        auto siteless = root_node(KEY_SITELESS);
        if (!siteless)
            return std::nullopt;
        auto obj = object(*siteless);
        if (!obj)
            return std::nullopt;
        auto parsed = parse_civil_node(*obj);
        if (!parsed)
            return std::nullopt;
        for (auto &[k, h] : parsed->children)
            if (k == KEY_DEFAULT_FEATURE)
                return h;
        return std::nullopt;
    }
    void write_companion(std::uint64_t handle, std::uint64_t owner, std::uint32_t tag, std::uint64_t alignment,
                         const std::vector<AlignmentPathPoint> &path, std::string_view name) {
        auto n = path.size();
        auto node_of = [&](std::size_t i) -> std::int32_t {
            if (i == 0)
                return 1;
            if (i + 1 == n)
                return 2;
            return static_cast<std::int32_t>(i + 2);
        };
        std::vector<std::size_t> order{0, n - 1};
        for (std::size_t i = 1; i + 1 < n; ++i)
            order.push_back(i);
        put_object(
            "AECC_FEATURE_STYLE", handle, owner, {},
            [&](ArchWriter &w) {
                for (auto v : {static_cast<std::int32_t>(tag), 0, 0, 32, 210})
                    w.main.bit_long(v);
                write_bit_run(w.main, "000000110010000000110");
                w.main.bit_long(static_cast<std::int32_t>(0x80000001u));
                w.main.bit_long(static_cast<std::int32_t>(n + 1));
                w.main.bit_long(static_cast<std::int32_t>(n));
                w.main.bit_long(1);
                for (std::size_t k = 0; k < order.size(); ++k) {
                    auto i = order[k];
                    w.bd(path[i].point.x);
                    w.bd(path[i].point.y);
                    w.bd(0);
                    write_bit_run(w.main, "010");
                    w.main.bit_long(k + 1 == n ? 0 : static_cast<std::int32_t>(k + 2));
                }
                for (auto v : {2, 1, 0, 1, 48, static_cast<std::int32_t>(n), 0})
                    w.main.bit_long(v);
                for (std::size_t i = 0; i < n; ++i) {
                    auto last = i + 1 == n;
                    auto arc = last ? std::optional<double>{} : path[i].bulge;
                    auto flags = last ? 10 : i + 2 < n ? 0x32 : 2;
                    auto face = last ? 0 : 1;
                    auto kind = last ? 2 : arc ? 1 : 0;
                    for (auto v : {kind, flags | (arc ? ALIGNMENT_ARC_EDGE : 0), node_of(i), face, face})
                        w.main.bit_long(v);
                    if (arc) {
                        w.bd(*arc);
                        w.main.bit_long(0);
                    }
                }
                for (int i = 0; i < 4; ++i)
                    w.main.bit_long(0);
                for (auto v : {2, 1, 1, 1, 7, 1, 1, 0, 0})
                    w.main.bit_long(v);
                for (int i = 0; i < 6; ++i) {
                    w.main.bit_long(1);
                    w.main.bit_long(100);
                }
                for (int i = 0; i < 6; ++i)
                    w.main.bit_long(0);
            },
            {{SOFT_POINTER, alignment}, {SOFT_POINTER, 0}, {SOFT_POINTER, 0}, {SOFT_POINTER, 0}},
            {std::string(name), ""});
    }
    std::string commit() {
        if (new_entity)
            lifecycle_membership(storage, o, o.model_space, 0, *new_entity, edits, added);
        for (auto h : removed)
            lifecycle_membership(storage, o, o.model_space, h, 0, edits, added);
        return lifecycle_rewrite(file, o, edits, removed, added, seed.header, seed.maximum + 1);
    }
};
} // namespace

std::vector<CogoPoint> cogo_points(const ArchObject &database) { return parse_cogo_points(database); }
std::vector<std::uint32_t> point_group_numbers(const ArchObject &group) { return parse_group_numbers(group); }
std::optional<std::uint32_t> cogo_point_number(const ArchEntityData &entity) { return parse_entity_number(entity); }
std::optional<std::string> alignment_name(const ArchEntityData &entity) { return parse_alignment_name(entity); }
std::optional<double> alignment_start_station(const ArchEntityData &entity) {
    return parse_alignment_start_station(entity);
}
std::vector<std::vector<Vector2>> alignment_polylines(const ArchEntityData &entity) {
    return parse_alignment_polylines(entity);
}
std::optional<std::vector<AlignmentElement>> alignment_elements(const ArchEntityData &entity) {
    return parse_alignment_elements(entity);
}
std::vector<std::pair<AlignmentPiece, std::vector<Vector2>>> alignment_pieces(const ArchEntityData &entity) {
    return parse_alignment_pieces(entity);
}
std::optional<std::string> profile_name(const ArchEntityData &entity) { return parse_profile_name(entity); }
std::vector<std::pair<ProfilePiece, std::vector<Vector2>>> profile_pieces(const ArchEntityData &entity) {
    return parse_profile_pieces(entity);
}
std::optional<TinSurface> tin_surface(const ArchEntityData &entity) { return parse_tin_surface(entity); }

std::string detail::ensure_dwg_civil(std::string_view file, const DwgR2000Objects &o) {
    auto registered = register_civil_classes(file, o);
    if (registered != file)
        return ensure_dwg_civil(registered, read_dwg_objects(registered));
    CivilWork w(file, o);
    w.ensure_root();
    return w.commit();
}
std::string detail::seed_dwg_civil_point_styles(std::string_view file, const DwgR2000Objects &o) {
    auto registered = register_civil_classes(file, o);
    if (registered != file)
        return seed_dwg_civil_point_styles(registered, read_dwg_objects(registered));
    CivilWork w(file, o);
    auto root = w.ensure_root();
    auto point_style = w.seed.next(), label_style = w.seed.next(), points = w.seed.next(), create = w.seed.next(),
         cls = w.seed.next();
    auto node = [&](std::string_view name, const std::vector<std::uint64_t> &refs) {
        std::vector<CivilRef> out;
        for (auto h : refs)
            out.push_back({HARD_POINTER, h});
        return std::pair<std::vector<CivilRef>, std::vector<std::string>>{out, {std::string(name)}};
    };
    auto style = node("style", {});
    w.put_object("AECC_SETTINGS_NODE", point_style, root, {}, [](ArchWriter &) {}, style.first, style.second);
    auto label = node("label", {});
    w.put_object("AECC_SETTINGS_NODE", label_style, root, {}, [](ArchWriter &) {}, label.first, label.second);
    auto points_node = node("AeccPoints", {label_style, point_style});
    w.put_object("AECC_SETTINGS_NODE", points, root, {}, [](ArchWriter &) {}, points_node.first, points_node.second);
    auto create_node = node("AeccCreatePoints", {});
    w.put_object("AECC_SETTINGS_NODE", create, root, {}, [](ArchWriter &) {}, create_node.first, create_node.second);
    auto class_node = node("AeccDbCogoPoint", {});
    w.put_object("AECC_CLASS_NODE", cls, root, {}, [](ArchWriter &) {}, class_node.first, class_node.second);
    return w.commit();
}
std::string detail::add_dwg_cogo_point(std::string_view file, const DwgR2000Objects &o,
                                      std::optional<std::uint32_t> number, Vector3 position,
                                      const std::optional<std::string> &description, std::optional<std::uint64_t> style,
                                      const std::optional<std::string> &layer, std::uint64_t &id) {
    auto registered = register_civil_classes(file, o);
    if (registered != file)
        return add_dwg_cogo_point(registered, read_dwg_objects(registered), number, position, description, style, layer,
                                  id);
    require(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z),
            "point position must be finite");
    if (layer)
        require(civil_layer(o, *layer), "layer does not exist");
    CivilWork w(file, o);
    auto points = w.points();
    auto n = number.value_or(0);
    if (!number) {
        for (auto &p : points)
            n = std::max(n, p.number);
        n += 1;
    }
    require(n != 0 && n <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
            "point number is zero or already used");
    for (auto &p : points)
        require(p.number != n, "point number is zero or already used");
    auto s = w.support();
    CogoPoint p;
    p.number = n;
    p.position = position;
    p.style = style.value_or(s.defaults.point_style);
    p.label_style = s.defaults.label_style;
    p.description = description;
    points.push_back(p);
    w.store_points(s, points);
    id = w.put_point(std::nullopt, n, layer);
    return w.commit();
}
std::string detail::set_dwg_cogo_point(std::string_view file, const DwgR2000Objects &o, std::uint64_t point,
                                      std::uint32_t number, Vector3 position,
                                      const std::optional<std::string> &description) {
    auto registered = register_civil_classes(file, o);
    if (registered != file)
        return set_dwg_cogo_point(registered, read_dwg_objects(registered), point, number, position, description);
    require(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) && number != 0 &&
                number <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),
            "point number or position is invalid");
    CivilWork w(file, o);
    auto old = w.point_number(point);
    auto points = w.points();
    if (number != old)
        for (auto &p : points)
            require(p.number != number, "point number already used");
    bool found = false;
    for (auto &p : points)
        if (p.number == old) {
            p.number = number;
            p.position = position;
            p.description = description;
            found = true;
        }
    require(found, "point is not in the database");
    auto s = w.support();
    w.store_points(s, points);
    w.put_point(point, number, std::nullopt);
    return w.commit();
}
std::string detail::remove_dwg_cogo_point(std::string_view file, const DwgR2000Objects &o, std::uint64_t point) {
    auto registered = register_civil_classes(file, o);
    if (registered != file)
        return remove_dwg_cogo_point(registered, read_dwg_objects(registered), point);
    CivilWork w(file, o);
    auto number = w.point_number(point);
    auto s = w.support();
    std::vector<CogoPoint> points;
    for (auto &p : w.points())
        if (p.number != number)
            points.push_back(p);
    w.store_points(s, points);
    w.removed.insert(point);
    return w.commit();
}
std::string detail::add_dwg_point_group(std::string_view file, const DwgR2000Objects &o, std::string_view name,
                                       const std::optional<std::string> &description,
                                       const std::vector<std::uint32_t> &numbers, std::uint64_t &id) {
    return add_dwg_point_group_query(file, o, name, description, PointQuery::from_numbers(numbers), id);
}
std::string detail::set_dwg_point_group(std::string_view file, const DwgR2000Objects &o, std::uint64_t group,
                                       std::string_view name, const std::optional<std::string> &description,
                                       const std::vector<std::uint32_t> &numbers) {
    return set_dwg_point_group_query(file, o, group, name, description, PointQuery::from_numbers(numbers));
}
std::string detail::add_dwg_point_group_query(std::string_view file, const DwgR2000Objects &o, std::string_view name,
                                             const std::optional<std::string> &description, const PointQuery &query,
                                             std::uint64_t &id) {
    auto registered = register_civil_classes(file, o);
    if (registered != file)
        return add_dwg_point_group_query(registered, read_dwg_objects(registered), name, description, query, id);
    auto spec = query_spec(query);
    (void)spec;
    CivilWork w(file, o);
    auto s = w.support();
    auto node = w.object(s.group_node);
    require(node, "point group node cannot be read");
    auto list = parse_group_entries(*node).value_or(std::vector<CivilGroupEntry>{});
    require(!name.empty(), "point group name is empty or already used");
    for (auto &e : list)
        require(!civil_ieq(e.name, name), "point group name is empty or already used");
    std::vector<std::uint32_t> tags;
    for (auto &e : list)
        tags.push_back(static_cast<std::uint32_t>(e.id));
    if (auto outer = w.root_node(KEY_ALL_POINTS_QUERY))
        if (auto obj = w.object(*outer))
            if (auto queries = parse_queries(*obj))
                for (auto &q : *queries)
                    tags.push_back(static_cast<std::uint32_t>(q.tag));
    std::uint32_t next = FIRST_GROUP_TAG;
    for (auto t : tags)
        if (t & 0x80000000)
            next = std::max(next, t + 1);
    next = std::max(next, FIRST_GROUP_TAG);
    auto gid = static_cast<std::int32_t>(next);
    auto serial = w.next_group_serial(list);
    id = w.seed.next();
    bucket_insert(list, CivilGroupEntry{gid, id, std::string(name)}, [](auto *e) { return e->id; });
    w.write_named_group(s, id, gid, serial, name, description.value_or(""), query);
    auto owner = w.root();
    require(owner.has_value(), "drawing has no root tree node");
    w.write_group_node(s.group_node, *owner, list, s.defaults.label_style, s.defaults.point_style);
    return w.commit();
}
std::string detail::set_dwg_point_group_query(std::string_view file, const DwgR2000Objects &o, std::uint64_t group,
                                             std::string_view name, const std::optional<std::string> &description,
                                             const PointQuery &query) {
    auto registered = register_civil_classes(file, o);
    if (registered != file)
        return set_dwg_point_group_query(registered, read_dwg_objects(registered), group, name, description, query);
    auto spec = query_spec(query);
    (void)spec;
    CivilWork w(file, o);
    auto s = w.support();
    auto node = w.object(s.group_node);
    require(node, "point group node cannot be read");
    auto list = parse_group_entries(*node).value_or(std::vector<CivilGroupEntry>{});
    std::size_t at = list.size();
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].handle == group)
            at = i;
    require(at != list.size() && group != s.all_points, "handle is not a named point group");
    auto obj = w.object(group);
    require(obj && civil_plain_group(*obj) && !name.empty(),
            "point group has overrides, or the name is empty or already used");
    for (auto &e : list)
        require(e.handle == group || !civil_ieq(e.name, name),
                "point group has overrides, or the name is empty or already used");
    auto serial = parse_group_serial(*obj);
    require(serial.has_value(), "point group record cannot be read");
    list[at].name = std::string(name);
    w.write_named_group(s, group, list[at].id, *serial, name, description.value_or(""), query);
    auto owner = w.root();
    require(owner.has_value(), "drawing has no root tree node");
    w.write_group_node(s.group_node, *owner, list, s.defaults.label_style, s.defaults.point_style);
    return w.commit();
}
std::string detail::add_dwg_alignment_with_curves(std::string_view file, const DwgR2000Objects &o, std::string_view name,
                                                 const std::vector<Vector2> &vertices,
                                                 const std::vector<VertexCurve> &curves,
                                                 const std::optional<std::string> &layer, std::uint64_t &id) {
    auto registered = register_civil_classes(file, o);
    if (registered != file)
        return add_dwg_alignment_with_curves(registered, read_dwg_objects(registered), name, vertices, curves, layer, id);
    require(vertices.size() >= 2, "an alignment needs two or more distinct finite vertices");
    for (auto &p : vertices)
        require(std::isfinite(p.x) && std::isfinite(p.y), "an alignment needs two or more distinct finite vertices");
    for (std::size_t i = 1; i < vertices.size(); ++i)
        require(vertices[i] != vertices[i - 1], "an alignment needs two or more distinct finite vertices");
    require(curves.size() + 2 == vertices.size(), "an alignment needs one curve per inner vertex");
    for (auto &c : curves)
        require(alignment_curve_valid(c), "a curve must be a positive radius, or two spirals around one");
    auto elements = alignment_build_elements(vertices, curves);
    auto path = alignment_solve_path(vertices, curves, elements);
    require(path.has_value(), "a curve has no turn or is not positive, or the curves do not fit");
    auto layer_name = layer.value_or("0");
    auto layer_handle = civil_layer(o, layer_name);
    require(layer_handle, "layer does not exist");
    CivilWork w(file, o);
    if (!w.default_alignment_style() || !w.companion_node())
        w.ensure_alignment_tree();
    auto style = w.default_alignment_style();
    auto node = w.companion_node();
    require(style.has_value(), "drawing has no alignment styles");
    require(node.has_value(), "drawing has no node for siteless alignments");
    auto feature = w.object(*node);
    require(feature, "feature style node cannot be read");
    auto used = parse_civil_node(*feature);
    require(used.has_value(), "feature style node cannot be read");
    std::uint32_t tag = FIRST_COMPANION_TAG;
    for (;; ++tag) {
        bool taken = false;
        for (auto &[k, h] : used->children) {
            (void)h;
            if (static_cast<std::uint32_t>(k) == tag)
                taken = true;
        }
        if (!taken)
            break;
    }
    id = w.seed.next();
    auto companion = w.seed.next();
    auto label = civil_companion_name(id);
    w.write_companion(companion, *node, tag, id, *path, label);
    w.add_node_child(*node, static_cast<std::int32_t>(tag), companion, label);
    auto packed = encode_alignment_entity(civil_class_number(w.o, "AECC_ALIGNMENT"), id, layer_handle, name, elements,
                                          *style, companion, {}, w.storage.page);
    w.added[id] = packed;
    w.new_entity = id;
    return w.commit();
}
std::string detail::add_dwg_alignment(std::string_view file, const DwgR2000Objects &o, std::string_view name,
                                      const std::vector<Vector2> &vertices, const std::optional<std::string> &layer,
                                      std::uint64_t &id) {
    std::vector<VertexCurve> curves;
    if (vertices.size() >= 2)
        curves.resize(vertices.size() - 2);
    return add_dwg_alignment_with_curves(file, o, name, vertices, curves, layer, id);
}
std::string detail::add_dwg_profile_with_curves(std::string_view file, const DwgR2000Objects &o, std::uint64_t alignment,
                                               std::string_view name, const std::vector<Vector2> &pvis,
                                               const std::vector<VerticalCurve> &curves,
                                               const std::optional<std::string> &layer, std::uint64_t &id) {
    auto registered = register_civil_classes(file, o);
    if (registered != file)
        return add_dwg_profile_with_curves(registered, read_dwg_objects(registered), alignment, name, pvis, curves, layer,
                                          id);
    require(pvis.size() >= 2, "a profile needs two or more finite PVIs with increasing stations");
    for (auto &p : pvis)
        require(std::isfinite(p.x) && std::isfinite(p.y), "a profile needs two or more finite PVIs with increasing stations");
    for (std::size_t i = 1; i < pvis.size(); ++i)
        require(pvis[i].x > pvis[i - 1].x, "a profile needs two or more finite PVIs with increasing stations");
    require(curves.size() + 2 == pvis.size(), "a profile needs one curve (or none) per inner PVI");
    auto grade = [&](std::size_t i) { return (pvis[i + 1].y - pvis[i].y) / (pvis[i + 1].x - pvis[i].x); };
    std::vector<std::pair<double, double>> reach;
    for (std::size_t j = 0; j < curves.size(); ++j) {
        auto r = vertical_curve_reach(curves[j], grade(j), grade(j + 1));
        require(r.has_value(), "a vertical curve needs positive values");
        require(curves[j].kind == VerticalCurve::Kind::None || grade(j) != grade(j + 1), "a curve's grades are equal");
        reach.push_back(*r);
    }
    for (std::size_t j = 0; j < curves.size(); ++j) {
        auto before = j == 0 ? 0.0 : reach[j - 1].second;
        auto after = j + 1 < reach.size() ? reach[j + 1].first : 0.0;
        require(reach[j].first + before <= pvis[j + 1].x - pvis[j].x &&
                    reach[j].second + after <= pvis[j + 2].x - pvis[j + 1].x,
                "a curve does not fit on its tangents");
    }
    require(!name.empty(), "a profile needs a name");
    auto it = o.arch_entities.find(alignment);
    require(it != o.arch_entities.end() && it->second.dxf_name == "AECC_ALIGNMENT",
            "the alignment is not one this crate writes");
    auto elements = parse_alignment_elements(it->second);
    require(elements.has_value(), "the alignment is not one this crate writes");
    auto al_name = parse_alignment_name(it->second);
    require(al_name.has_value(), "the alignment is not one this crate writes");
    auto before = parse_alignment_profiles(it->second);
    require(before.has_value(), "the alignment is not one this crate writes");
    auto handles = it->second.decode().handles;
    require(handles.size() >= 6, "the alignment is not one this crate writes");
    auto layer_name = layer.value_or("0");
    auto layer_handle = civil_layer(o, layer_name);
    require(layer_handle, "layer does not exist");
    CivilWork w(file, o);
    w.ensure_profile_tree();
    auto style = w.default_profile_style();
    require(style.has_value(), "drawing has no profile styles");
    id = w.seed.next();
    auto guid = civil_profile_guid(id);
    auto profiles = *before;
    profiles.push_back({id, guid});
    auto packed_al =
        encode_alignment_entity(civil_class_number(w.o, "AECC_ALIGNMENT"), alignment, it->second.layer, *al_name,
                                *elements, handles[4], handles[1], profiles, w.storage.page);
    w.edits[alignment] = packed_al;
    std::array<std::uint64_t, 7> helpers{};
    for (auto &h : helpers)
        h = w.seed.next();
    for (std::size_t i = 0; i < helpers.size(); ++i)
        w.put_object(
            PROFILE_HELPERS[i], helpers[i], id, {},
            [&](ArchWriter &writer) {
                writer.main.bit_long(0);
                if (i + 1 == helpers.size())
                    write_bit_run(writer.main, "010");
            },
            {}, {});
    auto parts = profile_build_parts(pvis, curves);
    auto packed = encode_profile_entity(civil_class_number(w.o, "AECC_PROFILE"), id, layer_handle, name, parts, helpers,
                                        *style, alignment, guid, w.storage.page);
    w.added[id] = packed;
    w.new_entity = id;
    return w.commit();
}
std::string detail::add_dwg_profile(std::string_view file, const DwgR2000Objects &o, std::uint64_t alignment,
                                    std::string_view name, const std::vector<Vector2> &pvis,
                                    const std::vector<double> &curves, const std::optional<std::string> &layer,
                                    std::uint64_t &id) {
    std::vector<VerticalCurve> list;
    list.reserve(curves.size());
    for (auto c : curves)
        list.push_back(c == 0.0 ? VerticalCurve::none() : VerticalCurve::parabola(c));
    return add_dwg_profile_with_curves(file, o, alignment, name, pvis, list, layer, id);
}
std::string detail::add_dwg_tin_surface(std::string_view file, const DwgR2000Objects &o, std::string_view name,
                                        const std::vector<Vector3> &points, const std::optional<std::string> &layer,
                                        std::uint64_t &id) {
    auto registered = register_civil_classes(file, o);
    if (registered != file)
        return add_dwg_tin_surface(registered, read_dwg_objects(registered), name, points, layer, id);
    bool finite = !points.empty();
    bool distinct = true;
    for (std::size_t i = 0; i < points.size(); ++i) {
        finite = finite && std::isfinite(points[i].x) && std::isfinite(points[i].y) && std::isfinite(points[i].z);
        for (std::size_t j = 0; j < i; ++j)
            if (points[i].x == points[j].x && points[i].y == points[j].y)
                distinct = false;
    }
    require(points.size() >= 3 && finite && distinct,
            "a surface needs three or more finite points, distinct in plan");
    auto layer_name = layer.value_or("0");
    auto layer_handle = civil_layer(o, layer_name);
    require(layer_handle, "layer does not exist");
    CivilWork w(file, o);
    if (!w.default_surface_style())
        w.ensure_surface_tree();
    auto style = w.default_surface_style();
    require(style.has_value(), "drawing has no surface styles");
    auto tin = triangulate_tin(points);
    id = w.seed.next();
    auto packed = encode_tin_entity(civil_class_number(w.o, "AECC_TIN_SURFACE"), id, layer_handle, name, points, tin,
                                    *style, w.storage.page);
    w.added[id] = packed;
    w.new_entity = id;
    return w.commit();
}
