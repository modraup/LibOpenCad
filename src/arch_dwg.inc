// SPDX-License-Identifier: MPL-2.0
// Native AC1032 Arch constructors, same-version setters and opaque packers.
#include "arch_internal.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <limits>
#include <libopencad/dwg_create.hpp>
#include <libopencad/proxy.hpp>
namespace {
constexpr const char *AEC_APPLICATION =
    "AecArchBase80|Product Desc: Autodesk Architectural Desktop 2007 |Company: Autodesk,Inc. |WEB Address: "
    "www.autodesk.com";
struct ArchLayerInfo {
    const char *name;
    int color;
    std::int16_t line_weight;
};
constexpr ArchLayerInfo ARCH_LAYERS[] = {
    {"A-Wall", 113, 50},     {"A-Door", 31, 25},      {"A-Glaz", 151, 25},     {"A-Wall-Open", 31, 25},
    {"A-Flor-Strs", 31, 25}, {"A-Flor-Hral", 11, 25}, {"A-Glaz-Assm", 71, 25}, {"A-Glaz-Curt", 52, 35},
    {"A-Area-Spce", 32, 35}, {"A-Slab", 192, 35},     {"A-Roof", 132, 35},     {"A-Roof-Slab", 12, 35},
    {"S-Beam", 12, 50},      {"S-Cols", 52, 35}};
const ArchLayerInfo *default_arch_layer(ArchKind kind) {
    const char *name = kind == ArchKind::Wall ? "A-Wall" : kind == ArchKind::Door ? "A-Door" : "A-Glaz";
    for (auto &layer : ARCH_LAYERS)
        if (std::string_view(layer.name) == name)
            return &layer;
    return nullptr;
}
bool finite_arch_point(Vector3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
std::string pack_arch_raw(unsigned v, const std::string &raw, std::int64_t handle_bits) {
    require(raw.size() < (1u << 30), "DWG record too large");
    DwgBitWriter prefix;
    prefix.modular_short(static_cast<std::uint32_t>(raw.size()));
    std::string record = prefix.bytes();
    modular_delta(record, static_cast<std::uint64_t>(handle_bits), false);
    record += raw;
    record.resize(record.size() + 2);
    set_crc(record, 0, record.size() - 2);
    return record;
}
DwgR2000Record unframed_arch_record(std::string_view raw, std::int64_t handle_bits, unsigned v,
                                    std::uint64_t handle) {
    DwgBitReader body(raw);
    DwgR2000Record rec{};
    rec.body_size = raw.size();
    if (v >= 1024) {
        auto tag = body.bits(2);
        rec.type = static_cast<std::int16_t>(tag < 2 ? body.bits(8) + tag * 0x1f0 : body.little_endian(2));
    } else
        rec.type = body.bit_short();
    rec.type_end_bit = body.position();
    auto id = body.handle();
    require(id.code == 0 && id.value == handle, "Arch handle disagrees with record");
    rec.handle_end_bit = body.position();
    rec.main_bits = raw.size() * 8 - static_cast<std::size_t>(handle_bits);
    rec.split_field_bit = rec.type_end_bit;
    return rec;
}
struct ArchWriter {
    DwgBitWriter main, refs, strings;
    unsigned v = 1032;
    const char *page = nullptr;
    void str(std::string_view s) { symbol_string(main, strings, v, s, page); }
    void bs(std::int16_t n) { main.bit_short(n); }
    void bd(double n) { main.bit_double(n); }
    void editable_bd(double n) {
        main.bits(2, 0);
        raw_double(main, n);
    }
    void marker() { bs(80); }
    void class_tag(std::int16_t schema) {
        main.bits(1, 1);
        main.bits(1, 1);
        bs(schema);
    }
    void href(unsigned code, std::uint64_t h) { refs.absolute_handle(code, h); }
    void empty_group(std::int32_t count) {
        marker();
        str("");
        marker();
        main.bit_long(count);
    }
    void editable_vector(Vector3 p) {
        editable_bd(p.x);
        editable_bd(p.y);
        editable_bd(p.z);
    }
    void native_style_tail() {
        static const unsigned char tail[] = {0xD2, 0xD5, 0x41, 0x50, 0x40, 0x77, 0x11,
                                             0x50, 0x54, 0x15, 0x09, 0x50, 0x54, 0x00};
        unsigned remaining = 106;
        for (auto byte : tail) {
            auto count = std::min(8u, remaining);
            for (unsigned i = 0; i < count; ++i)
                main.bits(1, (byte >> (7 - i)) & 1);
            remaining -= count;
        }
        str("");
        str("");
    }
};
std::string finish_arch(ArchWriter &w, std::size_t split) {
    return finish_object(w.main, w.strings, w.refs, w.v, split);
}
void aec_object_prefix(ArchWriter &w, std::int16_t schema) {
    w.marker();
    w.class_tag(schema);
    w.marker();
    w.str("");
    w.marker();
    w.str("");
    w.main.bits(1, 0);
    w.main.bits(8, 0);
    w.marker();
}
void aec_object_tail(ArchWriter &w) {
    w.marker();
    w.marker();
}
void material_body(ArchWriter &w) {
    w.main.bit_long(0);
    w.main.bits(1, 0);
    aec_object_tail(w);
}
void cleanup_body(ArchWriter &w) {
    w.bs(2);
    w.main.bits(1, 0);
    aec_object_tail(w);
}
void frame_body(ArchWriter &w, std::uint64_t endcap) {
    for (int i = 0; i < 4; ++i)
        w.href(5, endcap);
    w.main.bits(1, 0);
    aec_object_tail(w);
}
void endcap_body(ArchWriter &w) {
    w.bd(0);
    w.empty_group(1);
    w.class_tag(30);
    w.marker();
    w.str("");
    w.marker();
    w.marker();
    w.str("");
    w.marker();
    w.main.bit_long(1);
    w.class_tag(33);
    w.marker();
    w.str("");
    w.marker();
    w.bs(8);
    w.main.bits(8, 0);
    w.main.bits(1, 0);
    w.bs(1);
    w.main.bits(1, 0);
    w.main.bits(1, 1);
    for (double value : {0.0, 0.0, 0.0, 1.0})
        w.bd(value);
    w.main.bits(1, 0);
    w.marker();
    for (int value : {1, 8, 8, 0})
        w.main.bit_long(value);
    w.main.bits(1, 0);
    aec_object_tail(w);
}
void opening_style_body(ArchWriter &w, ArchKind kind, std::uint64_t material) {
    w.main.bits(1, 0);
    for (double value : {2.0, 5.0, 0.5, 1.5, kind == ArchKind::Door ? 2.0 : 0.25})
        w.bd(value);
    w.bs(1);
    w.bs(0);
    w.href(5, 0);
    for (int i = 0; i < (kind == ArchKind::Door ? 5 : 4); ++i)
        w.href(5, material);
    if (kind == ArchKind::Door)
        w.bd(0.25);
    else {
        w.main.bits(1, 0);
        w.main.bits(1, 1);
        w.bd(0);
    }
    w.native_style_tail();
}
void wall_style_body(ArchWriter &w, std::uint64_t material, std::uint64_t endcap, std::uint64_t frame,
                     std::uint64_t cleanup) {
    w.href(5, endcap);
    for (int i = 0; i < 4; ++i)
        w.href(5, frame);
    w.href(5, material);
    w.empty_group(1);
    w.class_tag(652);
    w.marker();
    w.str("");
    w.marker();
    w.str("Unnamed");
    w.bs(1);
    w.class_tag(653);
    w.marker();
    w.str("");
    w.marker();
    w.marker();
    w.str("");
    w.marker();
    w.bd(0);
    w.bs(3);
    w.marker();
    w.str("");
    w.marker();
    w.bd(0);
    w.bs(2);
    w.marker();
    w.str("");
    w.marker();
    w.bd(0);
    w.bd(0);
    w.bs(0);
    w.main.bits(1, 1);
    w.marker();
    w.str("");
    w.marker();
    w.bd(0);
    w.bd(-0.5);
    w.bs(3);
    w.main.bits(1, 1);
    w.bs(100);
    w.href(5, material);
    w.bs(1);
    w.main.bits(8, 5);
    w.bs(0);
    for (double value : {6.0, 96.0, 0.0, 0.0, 0.0})
        w.bd(value);
    w.main.bits(1, 1);
    w.href(5, cleanup);
    w.main.bits(8, 3);
    w.main.bits(1, 1);
    w.main.bits(1, 0);
    w.native_style_tail();
}
void aec_entity_prefix(ArchWriter &w, std::int16_t schema, Vector3 location, Vector3 direction) {
    w.marker();
    w.class_tag(schema);
    w.marker();
    w.str("");
    w.marker();
    w.bs(0);
    w.main.bits(8, 1);
    w.main.bits(1, 0);
    w.marker();
    w.str("");
    w.editable_vector(location);
    w.editable_vector(direction);
    w.editable_vector({-direction.y, direction.x, 0});
    w.editable_vector(Vector3::UNIT_Z);
    w.str("");
    w.href(3, 0);
    w.marker();
}
void wall_body(ArchWriter &w, double width, double height, double length, std::uint64_t style,
               std::uint64_t cleanup) {
    w.href(5, style);
    w.href(5, cleanup);
    w.editable_bd(height);
    w.bd(-1);
    w.editable_bd(width);
    w.bd(12);
    w.main.bits(1, 0);
    w.main.bits(1, 0);
    for (int i = 0; i < 5; ++i)
        w.empty_group(0);
    w.bs(2);
    w.empty_group(0);
    w.marker();
    w.editable_bd(length);
}
void opening_body(ArchWriter &w, ArchKind kind, double width, double height, std::uint64_t style) {
    w.editable_bd(width);
    w.editable_bd(height);
    w.bd(0);
    w.marker();
    w.bd(50);
    w.bd(0);
    w.bs(0);
    w.marker();
    w.href(5, style);
    if (kind == ArchKind::Window) {
        w.main.bits(1, 0);
        w.bs(0);
        w.bd(18);
        w.bd(18);
        w.bd(1.5707963267948966);
        w.main.bits(1, 1);
    }
}
void aec_entity_tail(ArchWriter &w, ArchKind kind, std::uint64_t handle) {
    w.class_tag(45);
    w.empty_group(1);
    w.class_tag(196);
    w.marker();
    w.str("");
    w.marker();
    w.marker();
    w.bs(2);
    w.href(4, handle);
    for (int i = 0; i < (kind == ArchKind::Wall ? 2 : 4); ++i)
        w.marker();
}
std::size_t arch_object_common(ArchWriter &w, int type, std::uint64_t id, std::uint64_t owner) {
    return object_prefix(w.main, w.refs, w.v, type, id, owner, 0, {owner});
}
std::size_t arch_entity_common(ArchWriter &w, int type, std::uint64_t id, std::uint64_t layer,
                               const std::vector<std::uint8_t> &graphics) {
    write_object_type(w.main, w.v, type);
    auto split = w.main.position();
    w.main.absolute_handle(0, id);
    w.main.bit_short(0);
    w.main.bits(1, 1);
    w.main.bit_long_long(static_cast<std::int64_t>(graphics.size()));
    for (auto b : graphics)
        w.main.bits(8, b);
    w.main.bits(2, 2);
    w.main.bit_long(0);
    w.main.bits(1, 1);
    w.main.bits(1, 0);
    w.main.bit_short(256);
    w.main.bit_double(1);
    w.href(5, layer);
    w.main.bits(2, 0);
    w.main.bits(2, 0);
    w.main.bits(8, 0);
    w.main.bits(2, 0);
    w.main.bits(1, 0);
    w.main.bits(1, 0);
    w.main.bits(1, 0);
    w.main.bit_short(0);
    w.main.bits(8, 29);
    return split;
}
std::string encode_named_dictionary(unsigned v, const char *page, std::uint64_t id, std::uint64_t owner,
                                    std::uint64_t xdic, const std::vector<std::uint64_t> &reactors,
                                    const std::vector<std::pair<std::string, std::uint64_t>> &entries) {
    DwgBitWriter main, refs, strings;
    auto split = object_prefix(main, refs, v, 42, id, owner, xdic, reactors);
    require(entries.size() <= 1000000, "Invalid dictionary count");
    main.bit_long(static_cast<std::int32_t>(entries.size()));
    main.bit_short(1);
    main.bits(8, 0);
    for (auto &[name, target] : entries) {
        symbol_string(main, strings, v, name, page);
        refs.absolute_handle(2, target);
    }
    return finish_object(main, strings, refs, v, split);
}
int aec_class_number(const DwgR2000Objects &o, std::string_view name) {
    for (auto &c : o.classes)
        if (c.dxf_name == name)
            return c.number;
    throw std::invalid_argument("Missing Arch class");
}
std::string register_aec_classes(std::string_view file, const DwgR2000Objects &o, ArchKind kind) {
    struct Needed {
        const char *dxf;
        const char *cpp;
        bool entity;
        std::int32_t dwg_version;
        std::int32_t maintenance;
    };
    std::vector<Needed> needed{{"AEC_MATERIAL_DEF", "AecDbMaterialDef", false, 25, 0}};
    if (kind == ArchKind::Wall) {
        needed.push_back({"AEC_ENDCAP_STYLE", "AecDbEndcapStyle", false, 21, 0});
        needed.push_back({"AEC_FRAME_DEF", "AecDbOpeningEndcapStyle", false, 25, 0});
        needed.push_back({"AEC_CLEANUP_GROUP_DEF", "AecDbCleanupGroupDef", false, 23, 3});
        needed.push_back({"AEC_WALL_STYLE", "AecDbWallStyle", false, 21, 0});
        needed.push_back({"AEC_WALL", "AecDbWall", true, 21, 0});
    } else if (kind == ArchKind::Door) {
        needed.push_back({"AEC_DOOR_STYLE", "AecDbDoorStyle", false, 21, 0});
        needed.push_back({"AEC_DOOR", "AecDbDoor", true, 21, 0});
    } else {
        needed.push_back({"AEC_WINDOW_STYLE", "AecDbWindowStyle", false, 21, 0});
        needed.push_back({"AEC_WINDOW", "AecDbWindow", true, 21, 0});
    }
    auto classes = o.classes;
    bool added = false;
    for (auto &item : needed) {
        if (std::find_if(classes.begin(), classes.end(),
                         [&](const DwgClassRecord &c) { return c.dxf_name == item.dxf; }) != classes.end())
            continue;
        require(classes.size() < 32767 - 500, "Class table exhausted");
        DwgClassRecord c;
        c.number = static_cast<std::int16_t>(500 + classes.size());
        c.proxy_flags = static_cast<std::int16_t>(item.entity ? 893 : 129);
        c.application = std::string(item.dxf) == "AEC_MATERIAL_DEF"
                            ? std::string("AecBase80|Product Desc: Autodesk Architectural Desktop 2007 |Company: "
                                          "Autodesk,Inc. |WEB Address: www.autodesk.com")
                            : AEC_APPLICATION;
        c.cpp_name = item.cpp;
        c.dxf_name = item.dxf;
        c.item_class_id = static_cast<std::int16_t>(item.entity ? 498 : 499);
        c.fields[1] = item.dwg_version;
        c.fields[2] = item.maintenance;
        classes.push_back(c);
        added = true;
    }
    if (!added)
        return std::string(file);
    return detail::replace_ac18_sections(file, {{"AcDb:Classes", lifecycle_classes(file, classes)}});
}
struct NativePlanFrame {
    std::array<std::int64_t, 12> positions{};
    std::array<double, 12> values{};
    DynamicStream body;
};
std::optional<NativePlanFrame> native_plan_frame(const ArchEntityData &data, std::int16_t schema) {
    if (data.dwg_source_version != DxfVersion::AC1032 || !data.raw_dwg_data)
        return std::nullopt;
    try {
        auto rec = unframed_arch_record(*data.raw_dwg_data, data.dwg_handle_bits, 1032, data.handle);
        auto s = open_dynamic(*data.raw_dwg_data, rec, data.handle, 1032, nullptr);
        std::vector<std::uint8_t> graphics;
        read_entity_properties(s.main, s.refs, data.handle, s.end, 1032, rec, nullptr, nullptr, &graphics,
                               nullptr);
        if (s.main.bit_short() != 80 || !s.main.bits(1) || !s.main.bits(1) || s.main.bit_short() != schema ||
            s.main.bit_short() != 80 || !s.str().empty() || s.main.bit_short() != 80 ||
            s.main.bit_short() != 0 || s.main.bits(8) != 1 || s.main.bits(1) || s.main.bit_short() != 80)
            return std::nullopt;
        s.str();
        NativePlanFrame frame{ {}, {}, std::move(s) };
        for (int i = 0; i < 12; ++i) {
            frame.positions[i] = static_cast<std::int64_t>(frame.body.main.position());
            frame.values[i] = frame.body.main.bit_double();
            if (!std::isfinite(frame.values[i]))
                return std::nullopt;
        }
        if (!frame.body.str().empty())
            return std::nullopt;
        frame.body.href();
        if (frame.body.main.bit_short() != 80)
            return std::nullopt;
        Vector3 along{frame.values[3], frame.values[4], frame.values[5]};
        Vector3 across{frame.values[6], frame.values[7], frame.values[8]};
        Vector3 up{frame.values[9], frame.values[10], frame.values[11]};
        if (std::abs(along.z) > 1e-9 || std::abs(across.z) > 1e-9 || std::abs(along.length() - 1) > 1e-6 ||
            (across - Vector3{-along.y, along.x, 0}).length() > 1e-6 || (up - Vector3::UNIT_Z).length() > 1e-6)
            return std::nullopt;
        return frame;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
struct StraightWallFields {
    std::int64_t start_x{}, start_y{}, dir_x{}, dir_y{}, normal_x{}, normal_y{}, height{}, width{}, length{};
};
bool read_empty_group(DynamicStream &s) {
    return s.main.bit_short() == 80 && s.str().empty() && s.main.bit_short() == 80 && s.main.bit_long() == 0;
}
std::optional<StraightWallFields> find_straight_wall_fields(const ArchEntityData &data) {
    auto frame = native_plan_frame(data, 569);
    if (!frame || std::abs(frame->values[2]) > 1e-12)
        return std::nullopt;
    try {
        frame->body.href();
        frame->body.href();
        StraightWallFields fields;
        fields.start_x = frame->positions[0];
        fields.start_y = frame->positions[1];
        fields.dir_x = frame->positions[3];
        fields.dir_y = frame->positions[4];
        fields.normal_x = frame->positions[6];
        fields.normal_y = frame->positions[7];
        fields.height = static_cast<std::int64_t>(frame->body.main.position());
        auto height = frame->body.main.bit_double();
        if (!(std::isfinite(height) && height > 1e-15) || frame->body.main.bit_double() != -1)
            return std::nullopt;
        fields.width = static_cast<std::int64_t>(frame->body.main.position());
        auto width = frame->body.main.bit_double();
        if (!(std::isfinite(width) && width > 1e-15))
            return std::nullopt;
        auto layout = frame->body.main.bit_double();
        if ((layout != 0 && layout != 12) || frame->body.main.bits(1) || frame->body.main.bits(1))
            return std::nullopt;
        for (int i = 0; i < 5; ++i)
            if (!read_empty_group(frame->body))
                return std::nullopt;
        if (frame->body.main.bit_short() != 2 || !read_empty_group(frame->body) ||
            frame->body.main.bit_short() != 80)
            return std::nullopt;
        fields.length = static_cast<std::int64_t>(frame->body.main.position());
        auto length = frame->body.main.bit_double();
        if (!(std::isfinite(length) && length > 1e-15))
            return std::nullopt;
        return fields;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::int16_t opening_schema(ArchKind kind) {
    return kind == ArchKind::Door ? 558 : kind == ArchKind::Window ? 559 : kind == ArchKind::Opening ? 560 : 0;
}
bool arch_positive(double value) { return std::isfinite(value) && value > std::numeric_limits<double>::epsilon(); }
std::optional<std::int64_t> native_body_bit(const ArchEntityData &data) {
    if (data.dwg_source_version != DxfVersion::AC1032 || !data.raw_dwg_data)
        return std::nullopt;
    try {
        auto rec = unframed_arch_record(*data.raw_dwg_data, data.dwg_handle_bits, 1032, data.handle);
        auto s = open_dynamic(*data.raw_dwg_data, rec, data.handle, 1032, nullptr);
        std::vector<std::uint8_t> preview;
        std::size_t preview_bit = 0;
        read_entity_properties(s.main, s.refs, data.handle, s.end, 1032, rec, nullptr, nullptr, &preview,
                               &preview_bit);
        if (preview.empty() || decode_profiles(preview) != data.profiles)
            return std::nullopt;
        return static_cast<std::int64_t>(preview_bit + preview.size() * 8);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
struct MemberFrame {
    Vector3 origin, along, section_x, section_y;
};
std::optional<MemberFrame> member_frame(const ArchEntityData &data) {
    auto body = native_body_bit(data);
    if (!body || !data.raw_dwg_data)
        return std::nullopt;
    std::vector<std::uint8_t> raw(data.raw_dwg_data->begin(), data.raw_dwg_data->end());
    auto bit = *body + 453;
    double values[12];
    for (auto &value : values) {
        auto decoded = read_encoded_double(raw, bit);
        if (!decoded)
            return std::nullopt;
        value = decoded->first;
        bit = decoded->second;
    }
    MemberFrame frame{{values[0], values[1], values[2]},
                      {values[3], values[4], values[5]},
                      {values[6], values[7], values[8]},
                      {values[9], values[10], values[11]}};
    Vector3 axes[] = {frame.along, frame.section_x, frame.section_y};
    if (std::any_of(std::begin(axes), std::end(axes),
                    [](Vector3 axis) { return std::abs(axis.length() - 1) > 1e-6; }) ||
        std::abs(frame.along.dot(frame.section_x)) > 1e-6 || std::abs(frame.along.dot(frame.section_y)) > 1e-6 ||
        std::abs(frame.section_x.dot(frame.section_y)) > 1e-6 ||
        std::abs(frame.section_x.cross(frame.section_y).dot(frame.along) - 1) > 1e-6)
        return std::nullopt;
    return frame;
}
std::optional<OpeningSize> opening_pair(const ArchEntityData &data, ArchKind kind) {
    if (data.kind != kind || !data.raw_dwg_data)
        return std::nullopt;
    auto schema = opening_schema(kind);
    if (!schema)
        return std::nullopt;
    auto frame = native_plan_frame(data, static_cast<std::int16_t>(schema));
    if (!frame)
        return std::nullopt;
    OpeningSize size;
    size.width_bit = static_cast<std::int64_t>(frame->body.main.position());
    size.height_bit = size.width_bit + 66;
    std::vector<std::uint8_t> raw(data.raw_dwg_data->begin(), data.raw_dwg_data->end());
    auto width = read_bit_double_at(raw, size.width_bit);
    auto height = read_bit_double_at(raw, size.height_bit);
    if (!width || !height || *width <= 1e-15 || *height <= 1e-15)
        return std::nullopt;
    if (data.profiles.empty())
        return std::nullopt;
    for (auto &profile : data.profiles)
        for (auto p : profile)
            if (!finite_arch_point(p))
                return std::nullopt;
    size.width = *width;
    size.height = *height;
    return size;
}
std::string encode_style_object(unsigned v, const char *page, int type, std::uint64_t id, std::uint64_t owner,
                                std::int16_t schema, const std::function<void(ArchWriter &)> &body) {
    ArchWriter w;
    w.v = v;
    w.page = page;
    auto split = arch_object_common(w, type, id, owner);
    aec_object_prefix(w, schema);
    body(w);
    return finish_arch(w, split);
}
std::vector<std::uint8_t> encode_type6_graphics(const std::vector<Vector3> &profile) {
    std::vector<std::uint8_t> payload(4);
    auto count = static_cast<std::uint32_t>(profile.size());
    std::memcpy(payload.data(), &count, 4);
    for (auto p : profile) {
        std::uint8_t bytes[24];
        std::memcpy(bytes, &p.x, 8);
        std::memcpy(bytes + 8, &p.y, 8);
        std::memcpy(bytes + 16, &p.z, 8);
        payload.insert(payload.end(), bytes, bytes + 24);
    }
    ProxyGraphics graphics;
    graphics.records.push_back({6, std::move(payload)});
    return graphics.encode();
}
std::uint64_t nod_handle(const DwgR2000Objects &o) {
    std::uint64_t nod = 0;
    for (auto &[h, m] : o.metadata_records) {
        if (m.type != "DICTIONARY")
            continue;
        for (auto &[name, target] : m.dictionary_entries) {
            (void)target;
            if (name == "ACAD_GROUP" || name == "ACAD_MLINESTYLE") {
                require(!nod || nod == h, "Ambiguous named objects dictionary");
                nod = h;
            }
        }
    }
    require(nod, "Missing named objects dictionary");
    return nod;
}
void rewrite_preview(ArchEntityData &data, const std::function<Vector3(Vector3)> &map) {
    if (!data.raw_dwg_data)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    auto rec = unframed_arch_record(*data.raw_dwg_data, data.dwg_handle_bits, 1032, data.handle);
    auto s = open_dynamic(*data.raw_dwg_data, rec, data.handle, 1032, nullptr);
    std::vector<std::uint8_t> preview;
    std::size_t preview_bit = 0;
    read_entity_properties(s.main, s.refs, data.handle, s.end, 1032, rec, nullptr, nullptr, &preview,
                           &preview_bit);
    if (preview.empty() || decode_profiles(preview) != data.profiles)
        throw ArchEditException(ArchEditError::Rejected);
    auto graphics = ProxyGraphics::decode(preview);
    for (auto &record : graphics.records) {
        if (record.type != 6)
            continue;
        auto points = polyline_from_type6(record.data);
        if (!points)
            throw ArchEditException(ArchEditError::Rejected);
        for (std::size_t i = 0; i < points->size(); ++i) {
            auto p = map((*points)[i]);
            if (!finite_arch_point(p))
                throw ArchEditException(ArchEditError::InvalidValue);
            std::memcpy(record.data.data() + 4 + i * 24, &p.x, 8);
            std::memcpy(record.data.data() + 12 + i * 24, &p.y, 8);
            std::memcpy(record.data.data() + 20 + i * 24, &p.z, 8);
        }
    }
    auto encoded = graphics.encode();
    if (encoded.size() != preview.size())
        throw ArchEditException(ArchEditError::Rejected);
    std::vector<std::uint8_t> raw(data.raw_dwg_data->begin(), data.raw_dwg_data->end());
    if (!write_bytes_at_bit(raw, static_cast<std::int64_t>(preview_bit), encoded))
        throw ArchEditException(ArchEditError::Rejected);
    data.raw_dwg_data = std::string(raw.begin(), raw.end());
    data.graphic_data = encoded;
    data.profiles = decode_profiles(encoded);
}
std::vector<std::string> read_arch_strings(DynamicStream s) {
    std::vector<std::string> out;
    while (out.size() < 10000) {
        auto &reader = s.v >= 1021 ? s.strings : s.main;
        if (s.v >= 1021 && !s.text)
            break;
        if (reader.remaining() < 2)
            break;
        auto saved = reader.position();
        try {
            auto text = s.str();
            if (reader.position() <= saved)
                break;
            out.push_back(std::move(text));
        } catch (const std::exception &) {
            break;
        }
    }
    return out;
}
std::vector<std::uint64_t> read_arch_handles(DynamicStream s) {
    std::vector<std::uint64_t> out;
    while (out.size() < 10000 && s.refs.remaining() >= 8) {
        auto saved = s.refs.position();
        try {
            auto value = s.href();
            if (s.refs.position() <= saved)
                break;
            out.push_back(value);
        } catch (const std::exception &) {
            break;
        }
    }
    return out;
}
std::pair<std::optional<std::int16_t>, std::optional<AecPlacement>> read_arch_entity_tag(DynamicStream s) {
    try {
        if (s.main.bit_short() != 80 || !s.main.bits(1) || !s.main.bits(1))
            return {};
        auto schema = s.main.bit_short();
        if (s.main.bit_short() != 80)
            return {schema, std::nullopt};
        s.str();
        if (s.main.bit_short() != 80 || s.main.bit_short() != 0 || s.main.bits(8) != 1 || s.main.bits(1) ||
            s.main.bit_short() != 80)
            return {schema, std::nullopt};
        AecPlacement placement;
        placement.identifier = s.str();
        Vector3 axes[4];
        for (auto &axis : axes) {
            axis = {s.main.bit_double(), s.main.bit_double(), s.main.bit_double()};
            if (!finite_arch_point(axis))
                return {schema, std::nullopt};
        }
        s.str();
        placement.origin = axes[0];
        placement.x_axis = axes[1];
        placement.y_axis = axes[2];
        placement.z_axis = axes[3];
        placement.rigid = std::abs(placement.x_axis.length() - 1) <= 1e-6 &&
                          std::abs(placement.y_axis.length() - 1) <= 1e-6 &&
                          std::abs(placement.z_axis.length() - 1) <= 1e-6 &&
                          std::abs(placement.x_axis.dot(placement.y_axis)) <= 1e-6 &&
                          std::abs(placement.y_axis.dot(placement.z_axis)) <= 1e-6 &&
                          std::abs(placement.z_axis.dot(placement.x_axis)) <= 1e-6;
        return {schema, placement};
    } catch (const std::exception &) {
        return {};
    }
}
std::optional<std::int16_t> read_arch_object_tag(DynamicStream s) {
    try {
        if (s.main.bit_short() != 80 || !s.main.bits(1) || !s.main.bits(1))
            return std::nullopt;
        return s.main.bit_short();
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
DecodedAec identity_decoded(std::string dxf, std::string cpp, ArchKind kind, bool entity) {
    DecodedAec decoded;
    decoded.dxf_name = std::move(dxf);
    decoded.cpp_class_name = std::move(cpp);
    decoded.kind = kind;
    decoded.is_entity = entity;
    return decoded;
}
bool arch_near(double a, double b) { return std::abs(a - b) <= 1e-6; }
std::optional<std::uint64_t> nod_named_entry(const DwgR2000Objects &o, std::string_view dict_name,
                                            std::string_view entry) {
    auto nod = nod_handle(o);
    auto rec = o.metadata_records.find(nod);
    if (rec == o.metadata_records.end())
        return std::nullopt;
    std::uint64_t dict = 0;
    for (auto &[name, handle] : rec->second.dictionary_entries)
        if (name == dict_name)
            dict = handle;
    if (!dict)
        return std::nullopt;
    auto child = o.metadata_records.find(dict);
    if (child == o.metadata_records.end())
        return std::nullopt;
    for (auto &[name, handle] : child->second.dictionary_entries)
        if (name == entry)
            return handle;
    return std::nullopt;
}
} // namespace

std::optional<StraightWallGeometry> ArchEntityData::straight_wall_geometry() const {
    auto fields = find_straight_wall_fields(*this);
    if (!fields || !raw_dwg_data)
        return std::nullopt;
    std::vector<std::uint8_t> raw(raw_dwg_data->begin(), raw_dwg_data->end());
    auto sx = read_bit_double_at(raw, fields->start_x);
    auto sy = read_bit_double_at(raw, fields->start_y);
    auto dx = read_bit_double_at(raw, fields->dir_x);
    auto dy = read_bit_double_at(raw, fields->dir_y);
    auto height = read_bit_double_at(raw, fields->height);
    auto width = read_bit_double_at(raw, fields->width);
    auto length = read_bit_double_at(raw, fields->length);
    if (!sx || !sy || !dx || !dy || !height || !width || !length)
        return std::nullopt;
    StraightWallGeometry g;
    g.start = {*sx, *sy, 0};
    g.end = {*sx + *dx * *length, *sy + *dy * *length, 0};
    g.width = *width;
    g.height = *height;
    return g;
}
std::optional<OpeningSize> door_size(const ArchEntityData &data) { return opening_pair(data, ArchKind::Door); }
std::optional<OpeningSize> window_size(const ArchEntityData &data) {
    return opening_pair(data, ArchKind::Window);
}
std::optional<OpeningSize> opening_size(const ArchEntityData &data) {
    return opening_pair(data, ArchKind::Opening);
}
std::optional<MemberSize> member_size(const ArchEntityData &data) {
    if (data.kind != ArchKind::StructuralMember || !data.raw_dwg_data)
        return std::nullopt;
    auto body = native_body_bit(data);
    if (!body)
        return std::nullopt;
    std::vector<std::uint8_t> raw(data.raw_dwg_data->begin(), data.raw_dwg_data->end());
    MemberSize size;
    size.length_bit = *body + 232;
    size.breadth_bit = *body + 304;
    size.depth_bit = *body + 376;
    auto length = read_bit_double_at(raw, size.length_bit);
    auto breadth = read_bit_double_at(raw, size.breadth_bit);
    auto depth = read_bit_double_at(raw, size.depth_bit);
    auto copy = read_bit_double_at(raw, *body + 934);
    auto local_x = read_bit_double_at(raw, *body + 100);
    auto local_y = read_bit_double_at(raw, *body + 166);
    if (!length || !breadth || !depth || !copy || !local_x || !local_y || !arch_positive(*length) ||
        !arch_positive(*breadth) || !arch_positive(*depth) || !arch_near(*copy, *length) ||
        !arch_near(*local_x, -*breadth * 0.5) ||
        (!arch_near(*local_y, -*depth * 0.5) && !arch_near(*local_y, -*depth)))
        return std::nullopt;
    size.length = *length;
    size.breadth = *breadth;
    size.depth = *depth;
    return size;
}
void ArchEntityData::decode_fields() {
    fields.clear();
    if (!raw_dwg_data)
        return;
    std::vector<std::uint8_t> raw(raw_dwg_data->begin(), raw_dwg_data->end());
    std::vector<std::pair<std::int64_t, NumberRole>> named;
    if (kind == ArchKind::Door || kind == ArchKind::Window || kind == ArchKind::Opening) {
        if (auto size = opening_pair(*this, kind)) {
            named.push_back({size->width_bit, NumberRole::Width});
            named.push_back({size->height_bit, NumberRole::Height});
        }
    }
    if (kind == ArchKind::Wall && straight_wall_geometry()) {
        if (auto wall = find_straight_wall_fields(*this)) {
            named.push_back({wall->start_x, NumberRole::ProfileX});
            named.push_back({wall->start_y, NumberRole::ProfileY});
            named.push_back({wall->width, NumberRole::Width});
            named.push_back({wall->length, NumberRole::Length});
            named.push_back({wall->height, NumberRole::Height});
        }
    }
    for (auto [bit, role] : named) {
        auto value = read_encoded_double(raw, bit);
        if (!value)
            continue;
        fields.push_back({bit, value->first, role});
    }
}
DecodedAec ArchEntityData::decode() const {
    auto decoded = identity_decoded(dxf_name, cpp_class_name, kind, true);
    decoded.profiles = profiles;
    if (!raw_dwg_data || dwg_source_version != DxfVersion::AC1032)
        return decoded;
    try {
        auto rec = unframed_arch_record(*raw_dwg_data, dwg_handle_bits, 1032, handle);
        auto s = open_dynamic(*raw_dwg_data, rec, handle, 1032, nullptr);
        read_entity_properties(s.main, s.refs, handle, s.end, 1032, rec, nullptr, nullptr, nullptr, nullptr);
        decoded.strings = read_arch_strings(s);
        decoded.handles = read_arch_handles(s);
        auto tag = read_arch_entity_tag(s);
        decoded.schema = tag.first;
        decoded.placement = tag.second;
    } catch (const std::exception &) {
    }
    return decoded;
}
DecodedAec ArchObject::decode() const {
    auto decoded = identity_decoded(dxf_name, cpp_class_name, kind, false);
    if (!raw_dwg_data || raw_dwg_version != DxfVersion::AC1032)
        return decoded;
    try {
        auto rec = unframed_arch_record(*raw_dwg_data, raw_dwg_handle_bits, 1032, handle);
        auto s = open_dynamic(*raw_dwg_data, rec, handle, 1032, nullptr);
        skip_entity_prefix(s.main, s.end, false, 1032);
        s.href();
        auto reactors = s.main.bit_long();
        require(reactors >= 0 && reactors <= 1000000, "Invalid Arch reactor count");
        for (int i = 0; i < reactors; ++i)
            s.href();
        if (!s.main.bits(1))
            s.href();
        s.main.bits(1);
        decoded.strings = read_arch_strings(s);
        decoded.handles = read_arch_handles(s);
        decoded.schema = read_arch_object_tag(s);
    } catch (const std::exception &) {
    }
    return decoded;
}

namespace {
std::string create_architecture(ArchKind kind, Vector3 location, Vector3 direction, double width, double height,
                                double length, const std::vector<Vector3> &profile,
                                const std::optional<std::string> &layer_name) {
    require(kind == ArchKind::Wall || kind == ArchKind::Door || kind == ArchKind::Window,
            "Unsupported Arch constructor");
    auto file = create_dwg(DxfVersion::AC1032, {});
    auto o = read_dwg_objects(file);
    file = register_aec_classes(file, o, kind);
    o = read_dwg_objects(file);
    auto standard = default_arch_layer(kind);
    require(standard, "no ACAD Arch layer is defined for this class");
    std::string layer = standard->name;
    if (layer_name) {
        auto name = *layer_name;
        auto begin = name.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            throw std::invalid_argument("layer name must not be empty");
        name = name.substr(begin, name.find_last_not_of(" \t\r\n") - begin + 1);
        if (name.empty())
            throw std::invalid_argument("layer name must not be empty");
        layer = name;
        for (auto &known : ARCH_LAYERS)
            if (ascii_upper(known.name) == ascii_upper(name))
                layer = known.name;
    }
    const ArchLayerInfo *known = nullptr;
    for (auto &item : ARCH_LAYERS)
        if (std::string_view(item.name) == layer)
            known = &item;
    std::uint64_t layer_id = 0;
    if (auto existing = symbol_id(o, 1032, 51, layer))
        layer_id = *existing;
    else {
        auto color = known ? known->color : 7;
        auto lw = LineWeight::from_value(known ? known->line_weight : 25).to_dwg_index();
        file = detail::add_dwg_layer(file, o, layer, color, layer_id, lw);
        o = read_dwg_objects(file);
    }
    HandleSeed seed(file, o, 1032);
    LifecycleEdits added, edits;
    auto page = drawing_code_page(file, 1032);
    auto nod = nod_handle(o);
    auto nod_rec = o.metadata_records.at(nod);
    auto nod_entries = nod_rec.dictionary_entries;
    auto add_style = [&](const char *dict_name, const char *dxf, std::int16_t schema,
                         const std::function<void(ArchWriter &)> &body) {
        auto dict = seed.next();
        auto handle = seed.next();
        added[dict] =
            encode_named_dictionary(1032, page, dict, nod, 0, {nod}, {{"Standard", handle}});
        added[handle] = encode_style_object(1032, page, aec_class_number(o, dxf), handle, dict, schema, body);
        nod_entries.emplace_back(dict_name, dict);
        return handle;
    };
    auto material = add_style("AEC_MATERIAL_DEFS", "AEC_MATERIAL_DEF", 164, material_body);
    std::uint64_t cleanup = 0, style = 0;
    if (kind == ArchKind::Wall) {
        auto endcap = add_style("AEC_ENDCAP_STYLES", "AEC_ENDCAP_STYLE", 538, endcap_body);
        auto frame = add_style("AEC_OPENING_ENDCAP_STYLES", "AEC_FRAME_DEF", 674,
                               [&](ArchWriter &w) { frame_body(w, endcap); });
        cleanup = add_style("AEC_CLEANUP_GROUP_DEFS", "AEC_CLEANUP_GROUP_DEF", 536, cleanup_body);
        style = add_style("AEC_WALL_STYLES", "AEC_WALL_STYLE", 547,
                          [&](ArchWriter &w) { wall_style_body(w, material, endcap, frame, cleanup); });
    } else if (kind == ArchKind::Door)
        style = add_style("AEC_DOOR_STYLES", "AEC_DOOR_STYLE", 537,
                          [&](ArchWriter &w) { opening_style_body(w, kind, material); });
    else
        style = add_style("AEC_WINDOW_STYLES", "AEC_WINDOW_STYLE", 548,
                          [&](ArchWriter &w) { opening_style_body(w, kind, material); });
    edits[nod] = encode_named_dictionary(1032, page, nod, nod_rec.owner, nod_rec.extension_dictionary,
                                         nod_rec.reactors, nod_entries);
    auto entity = seed.next();
    auto graphics = encode_type6_graphics(profile);
    ArchWriter w;
    w.v = 1032;
    w.page = page;
    auto name = kind == ArchKind::Wall ? "AEC_WALL" : kind == ArchKind::Door ? "AEC_DOOR" : "AEC_WINDOW";
    auto schema = kind == ArchKind::Wall ? 569 : kind == ArchKind::Door ? 558 : 559;
    auto split = arch_entity_common(w, aec_class_number(o, name), entity, layer_id, graphics);
    aec_entity_prefix(w, static_cast<std::int16_t>(schema), location, direction);
    if (kind == ArchKind::Wall)
        wall_body(w, width, height, length, style, cleanup);
    else
        opening_body(w, kind, width, height, style);
    aec_entity_tail(w, kind, entity);
    added[entity] = finish_arch(w, split);
    LifecycleStorage s(file);
    lifecycle_membership(s, o, o.model_space, 0, entity, edits, added);
    return lifecycle_rewrite(file, o, edits, {}, added, seed.header, seed.maximum + 1);
}
ArchEntityData apply_wall_geometry(ArchEntityData data, Vector3 start, Vector3 end, double width, double height) {
    if (data.kind != ArchKind::Wall || !data.straight_wall_geometry())
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    if (data.profiles.size() != 1 || data.profiles.front().size() != 5 ||
        data.profiles.front().front() != data.profiles.front().back())
        throw ArchEditException(ArchEditError::Rejected);
    auto edge = end - start;
    auto length = edge.length();
    if (!finite_arch_point(start) || !finite_arch_point(end) || !(length > 1e-15) || !(width > 1e-15) ||
        !(height > 1e-15) || std::abs(start.z) > 1e-12 || std::abs(end.z) > 1e-12)
        throw ArchEditException(ArchEditError::InvalidValue);
    auto fields = find_straight_wall_fields(data);
    if (!fields)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    auto points = centered_wall_profile(start, end, width);
    if (!points)
        throw ArchEditException(ArchEditError::InvalidValue);
    auto along = edge * (1.0 / length);
    auto current = data.profiles.front();
    if (!data.raw_dwg_data)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    std::vector<std::uint8_t> raw(data.raw_dwg_data->begin(), data.raw_dwg_data->end());
    if (!write_bit_doubles_at(raw, {{fields->start_x, start.x},
                                    {fields->start_y, start.y},
                                    {fields->dir_x, along.x},
                                    {fields->dir_y, along.y},
                                    {fields->normal_x, -along.y},
                                    {fields->normal_y, along.x},
                                    {fields->height, height},
                                    {fields->width, width},
                                    {fields->length, length}}))
        throw ArchEditException(ArchEditError::Rejected);
    data.raw_dwg_data = std::string(raw.begin(), raw.end());
    rewrite_preview(data, [&](Vector3 point) {
        auto i = std::find(current.begin(), current.end(), point);
        return i == current.end() ? point : (*points)[static_cast<std::size_t>(i - current.begin())];
    });
    return data;
}
std::string pack_arch_entity(const ArchEntityData &data) {
    if (!data.raw_dwg_data)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    return pack_arch_raw(1032, *data.raw_dwg_data, data.dwg_handle_bits);
}
std::string set_architecture_wall(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle,
                                  Vector3 start, Vector3 end, double width, double height) {
    require(o.arch_entities.count(handle), "Missing Arch entity");
    auto data = apply_wall_geometry(o.arch_entities.at(handle), start, end, width, height);
    LifecycleEdits edits;
    edits[handle] = pack_arch_entity(data);
    return lifecycle_rewrite(file, o, edits, {}, {});
}
std::string set_architecture_opening(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle,
                                     ArchKind kind, double width, double height) {
    require(o.arch_entities.count(handle), "Missing Arch entity");
    auto data = o.arch_entities.at(handle);
    if (data.kind != kind)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    if (!arch_positive(width) || !arch_positive(height))
        throw ArchEditException(ArchEditError::InvalidValue);
    auto size = opening_pair(data, kind);
    if (!size)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    if (!data.raw_dwg_data)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    std::vector<std::uint8_t> raw(data.raw_dwg_data->begin(), data.raw_dwg_data->end());
    if (!write_bit_doubles_at(raw, {{size->width_bit, width}, {size->height_bit, height}}))
        throw ArchEditException(ArchEditError::Rejected);
    data.raw_dwg_data = std::string(raw.begin(), raw.end());
    auto origin = Vector3{};
    auto along = Vector3::UNIT_X;
    auto across = Vector3::UNIT_Y;
    if (auto frame = native_plan_frame(data, opening_schema(kind))) {
        origin = {frame->values[0], frame->values[1], frame->values[2]};
        along = {frame->values[3], frame->values[4], frame->values[5]};
        across = {frame->values[6], frame->values[7], frame->values[8]};
    }
    auto along_scale = width / size->width;
    auto across_scale = 1.0;
    if (kind == ArchKind::Door) {
        along_scale = (width + 100) / (size->width + 100);
        across_scale = (width + 120) / (size->width + 120);
    }
    rewrite_preview(data, [&](Vector3 point) {
        auto relative = point - origin;
        auto u = relative.dot(along);
        auto v = relative.dot(across);
        return point + along * (u * (along_scale - 1)) + across * (v * (across_scale - 1));
    });
    LifecycleEdits edits;
    edits[handle] = pack_arch_raw(1032, *data.raw_dwg_data, data.dwg_handle_bits);
    return lifecycle_rewrite(file, o, edits, {}, {});
}
std::string translate_plan_frame_architecture(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle,
                                              Vector3 delta) {
    auto data = o.arch_entities.at(handle);
    auto frame = native_plan_frame(data, 569);
    if (!frame)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    if (std::abs(frame->values[2]) > 1e-9)
        throw ArchEditException(ArchEditError::Rejected);
    auto x = frame->values[0] + delta.x;
    auto y = frame->values[1] + delta.y;
    if (!std::isfinite(x) || !std::isfinite(y))
        throw ArchEditException(ArchEditError::InvalidValue);
    if (!data.raw_dwg_data)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    std::vector<std::uint8_t> raw(data.raw_dwg_data->begin(), data.raw_dwg_data->end());
    if (!write_bit_doubles_at(raw, {{frame->positions[0], x}, {frame->positions[1], y}}))
        throw ArchEditException(ArchEditError::Rejected);
    data.raw_dwg_data = std::string(raw.begin(), raw.end());
    rewrite_preview(data, [&](Vector3 point) { return point + delta; });
    LifecycleEdits edits;
    edits[handle] = pack_arch_entity(data);
    return lifecycle_rewrite(file, o, edits, {}, {});
}
std::string translate_architecture(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle,
                                   Vector3 delta) {
    require(o.arch_entities.count(handle), "Missing Arch entity");
    auto data = o.arch_entities.at(handle);
    bool have = false;
    for (auto &profile : data.profiles)
        for (auto point : profile) {
            have = true;
            auto mapped = point + delta;
            if (!finite_arch_point(mapped) || (mapped - point - delta).length() > 1e-6)
                throw ArchEditException(ArchEditError::Rejected);
        }
    if (!have)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    if (!finite_arch_point(delta) || std::abs(delta.z) > 1e-12)
        throw ArchEditException(ArchEditError::InvalidValue);
    if (auto wall = data.straight_wall_geometry())
        return set_architecture_wall(file, o, handle, wall->start + delta, wall->end + delta, wall->width,
                                     wall->height);
    return translate_plan_frame_architecture(file, o, handle, delta);
}
std::string set_architecture_profile_bounds(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle,
                                            Vector3 min, Vector3 max) {
    require(o.arch_entities.count(handle), "Missing Arch entity");
    if (!finite_arch_point(min) || !finite_arch_point(max) || !(max.x > min.x) || !(max.y > min.y) ||
        std::abs(min.z) > 1e-12 || std::abs(max.z) > 1e-12)
        throw ArchEditException(ArchEditError::InvalidValue);
    auto wall = o.arch_entities.at(handle).straight_wall_geometry();
    if (!wall)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    auto direction = wall->end - wall->start;
    Vector3 start, end;
    double width = 0;
    if (std::abs(direction.x) < 1e-6) {
        auto x = (min.x + max.x) * 0.5;
        start = {x, min.y, 0};
        end = {x, max.y, 0};
        width = max.x - min.x;
    } else if (std::abs(direction.y) < 1e-6) {
        auto y = (min.y + max.y) * 0.5;
        start = {min.x, y, 0};
        end = {max.x, y, 0};
        width = max.y - min.y;
    } else
        throw ArchEditException(ArchEditError::Rejected);
    if ((end - start).dot(direction) < 0)
        std::swap(start, end);
    return set_architecture_wall(file, o, handle, start, end, width, wall->height);
}
std::string reshape_architecture_rectangle(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle,
                                           Vector3 start, Vector3 end, double width) {
    require(o.arch_entities.count(handle), "Missing Arch entity");
    auto wall = o.arch_entities.at(handle).straight_wall_geometry();
    if (!wall)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    auto points = rectangle_profile(start, end, width);
    if (!points)
        throw ArchEditException(ArchEditError::InvalidValue);
    auto offset = ((*points)[3] - (*points)[0]) * 0.5;
    return set_architecture_wall(file, o, handle, start + offset, end + offset, width, wall->height);
}
bool arch_positive_value(double value) { return std::isfinite(value) && value > 0; }
std::string set_architecture_continuous(std::string_view file, const DwgR2000Objects &o, const ContinuousWall &chain,
                                        const std::vector<Vector3> &points, double width, double height) {
    if (!arch_positive_value(width) || !arch_positive_value(height))
        throw ArchEditException(ArchEditError::InvalidValue);
    if (points.size() != chain.handles.size() + 1 || points.size() < 2)
        throw ArchEditException(ArchEditError::InvalidValue);
    for (auto point : points)
        if (!finite_arch_point(point) || std::abs(point.z) > 1e-12)
            throw ArchEditException(ArchEditError::InvalidValue);
    for (std::size_t i = 1; i < points.size(); ++i)
        if (!arch_positive_value((points[i] - points[i - 1]).length()))
            throw ArchEditException(ArchEditError::InvalidValue);
    LifecycleEdits edits;
    for (std::size_t i = 0; i < chain.handles.size(); ++i) {
        auto handle = chain.handles[i];
        if (std::find(chain.handles.begin(), chain.handles.begin() + static_cast<std::ptrdiff_t>(i), handle) !=
            chain.handles.begin() + static_cast<std::ptrdiff_t>(i))
            throw ArchEditException(ArchEditError::Rejected);
        if (!o.arch_entities.count(handle))
            throw ArchEditException(ArchEditError::UnsupportedLayout);
        auto data = apply_wall_geometry(o.arch_entities.at(handle), points[i], points[i + 1], width, height);
        edits[handle] = pack_arch_entity(data);
    }
    return lifecycle_rewrite(file, o, edits, {}, {});
}
std::string add_architecture_wall(std::string_view file, const DwgR2000Objects &o, Vector3 start, Vector3 end,
                                  double width, double height, const std::optional<std::string> &layer_name,
                                  std::uint64_t &id) {
    if (!std::isfinite(width) || width <= 0 || !std::isfinite(height) || height <= 0)
        throw std::invalid_argument("Arch dimensions must be finite and positive");
    if (!finite_arch_point(start) || !finite_arch_point(end) || start.z != 0 || end.z != 0)
        throw std::invalid_argument("wall baseline must be finite and in the world XY plane");
    auto length = (end - start).length();
    if (!std::isfinite(length) || length <= 0)
        throw std::invalid_argument("wall baseline must have positive finite length");
    auto profile = centered_wall_profile(start, end, width);
    if (!profile)
        throw std::invalid_argument("wall dimensions are too small for a supported profile");
    auto style = nod_named_entry(o, "AEC_WALL_STYLES", "Standard");
    auto cleanup = nod_named_entry(o, "AEC_CLEANUP_GROUP_DEFS", "Standard");
    require(style && cleanup, "add_wall requires an existing AC1032 wall drawing");
    auto standard = default_arch_layer(ArchKind::Wall);
    require(standard, "no ACAD Arch layer is defined for this class");
    std::string layer = standard->name;
    if (layer_name) {
        auto name = *layer_name;
        auto begin = name.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            throw std::invalid_argument("layer name must not be empty");
        name = name.substr(begin, name.find_last_not_of(" \t\r\n") - begin + 1);
        if (name.empty())
            throw std::invalid_argument("layer name must not be empty");
        layer = name;
        for (auto &known : ARCH_LAYERS)
            if (ascii_upper(known.name) == ascii_upper(name))
                layer = known.name;
    }
    auto layer_id = symbol_id(o, 1032, 51, layer);
    require(layer_id.has_value(), "add_wall layer is missing");
    HandleSeed seed(file, o, 1032);
    LifecycleEdits added, edits;
    auto page = drawing_code_page(file, 1032);
    id = seed.next();
    auto graphics = encode_type6_graphics(*profile);
    ArchWriter w;
    w.v = 1032;
    w.page = page;
    auto direction = (end - start) / length;
    auto split = arch_entity_common(w, aec_class_number(o, "AEC_WALL"), id, *layer_id, graphics);
    aec_entity_prefix(w, 569, start, direction);
    wall_body(w, width, height, length, *style, *cleanup);
    aec_entity_tail(w, ArchKind::Wall, id);
    added[id] = finish_arch(w, split);
    LifecycleStorage s(file);
    lifecycle_membership(s, o, o.model_space, 0, id, edits, added);
    return lifecycle_rewrite(file, o, edits, {}, added, seed.header, seed.maximum + 1);
}
} // namespace

namespace detail {
std::string create_dwg_architecture(ArchKind kind, Vector3 location, Vector3 direction, double width,
                                    double height, double length, const std::vector<Vector3> &profile,
                                    const std::optional<std::string> &layer) {
    return create_architecture(kind, location, direction, width, height, length, profile, layer);
}
std::string set_dwg_wall(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle, Vector3 start,
                         Vector3 end, double width, double height) {
    return set_architecture_wall(file, o, handle, start, end, width, height);
}
std::string set_dwg_door(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle, double width,
                         double height) {
    return set_architecture_opening(file, o, handle, ArchKind::Door, width, height);
}
std::string set_dwg_window(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle, double width,
                           double height) {
    return set_architecture_opening(file, o, handle, ArchKind::Window, width, height);
}
std::string set_dwg_opening(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle, double width,
                            double height) {
    return set_architecture_opening(file, o, handle, ArchKind::Opening, width, height);
}
std::string set_dwg_stair(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle, double run,
                          double width, double height) {
    require(o.arch_entities.count(handle), "Missing Arch entity");
    (void)file;
    if (!arch_positive(run) || !arch_positive(width) || !arch_positive(height))
        throw ArchEditException(ArchEditError::InvalidValue);
    throw ArchEditException(ArchEditError::UnsupportedLayout);
}
std::string set_architecture_member(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle,
                                    double length, double breadth, double depth) {
    require(o.arch_entities.count(handle), "Missing Arch entity");
    auto data = o.arch_entities.at(handle);
    auto size = member_size(data);
    if (!size)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    if (!arch_positive(length))
        throw ArchEditException(ArchEditError::InvalidValue);
    if (!arch_near(breadth, size->breadth) || !arch_near(depth, size->depth))
        throw ArchEditException(ArchEditError::Rejected);
    auto frame = member_frame(data);
    auto body = native_body_bit(data);
    if (!frame || !body || !data.raw_dwg_data)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    double minv[3] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity()};
    double maxv[3] = {-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity()};
    bool any = false;
    for (auto &profile : data.profiles) {
        for (auto point : profile) {
            if (!finite_arch_point(point))
                throw ArchEditException(ArchEditError::UnsupportedLayout);
            auto relative = point - frame->origin;
            double coordinates[] = {relative.dot(frame->along), relative.dot(frame->section_x),
                                    relative.dot(frame->section_y)};
            for (int axis = 0; axis < 3; ++axis) {
                minv[axis] = std::min(minv[axis], coordinates[axis]);
                maxv[axis] = std::max(maxv[axis], coordinates[axis]);
            }
            any = true;
        }
    }
    if (!any || !std::isfinite(minv[0]) || !std::isfinite(maxv[0]))
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    bool corners[8]{};
    for (auto &profile : data.profiles) {
        for (auto point : profile) {
            auto relative = point - frame->origin;
            double coordinates[] = {relative.dot(frame->along), relative.dot(frame->section_x),
                                    relative.dot(frame->section_y)};
            int index = 0;
            for (int axis = 0; axis < 3; ++axis) {
                if (arch_near(coordinates[axis], minv[axis]))
                    continue;
                if (arch_near(coordinates[axis], maxv[axis]))
                    index |= 1 << axis;
                else
                    throw ArchEditException(ArchEditError::Rejected);
            }
            corners[index] = true;
        }
    }
    if (!std::all_of(std::begin(corners), std::end(corners), [](bool found) { return found; }) ||
        !arch_near(minv[0], 0) || !arch_near(maxv[0], size->length) || !arch_near(minv[1], -size->breadth * 0.5) ||
        !arch_near(maxv[1], size->breadth * 0.5) || !arch_near(maxv[2] - minv[2], size->depth) ||
        !((arch_near(minv[2], -size->depth * 0.5) && arch_near(maxv[2], size->depth * 0.5)) ||
          (arch_near(minv[2], -size->depth) && arch_near(maxv[2], 0))))
        throw ArchEditException(ArchEditError::Rejected);
    std::vector<std::uint8_t> raw(data.raw_dwg_data->begin(), data.raw_dwg_data->end());
    if (!write_bit_doubles_at(raw, {{size->length_bit, length}, {*body + 934, length}}))
        throw ArchEditException(ArchEditError::Rejected);
    data.raw_dwg_data = std::string(raw.begin(), raw.end());
    rewrite_preview(data, [&](Vector3 point) {
        auto relative = point - frame->origin;
        return point + frame->along * (relative.dot(frame->along) * (length / size->length - 1));
    });
    LifecycleEdits edits;
    edits[handle] = pack_arch_raw(1032, *data.raw_dwg_data, data.dwg_handle_bits);
    return lifecycle_rewrite(file, o, edits, {}, {});
}
std::string set_dwg_member(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle, double length,
                           double breadth, double depth) {
    return set_architecture_member(file, o, handle, length, breadth, depth);
}
std::string set_dwg_arch_bounds(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle, ArchKind kind,
                                Vector3 min, Vector3 max) {
    require(o.arch_entities.count(handle), "Missing Arch entity");
    (void)file;
    auto data = o.arch_entities.at(handle);
    if (data.kind != kind)
        throw ArchEditException(ArchEditError::UnsupportedLayout);
    if (!finite_arch_point(min) || !finite_arch_point(max))
        throw ArchEditException(ArchEditError::InvalidValue);
    throw ArchEditException(ArchEditError::UnsupportedLayout);
}
std::string add_dwg_wall(std::string_view file, const DwgR2000Objects &o, Vector3 start, Vector3 end, double width,
                         double height, const std::optional<std::string> &layer, std::uint64_t &id) {
    return add_architecture_wall(file, o, start, end, width, height, layer, id);
}
std::string translate_dwg_architecture(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle,
                                       Vector3 delta) {
    return translate_architecture(file, o, handle, delta);
}
std::string set_dwg_profile_bounds(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle, Vector3 min,
                                   Vector3 max) {
    return set_architecture_profile_bounds(file, o, handle, min, max);
}
std::string reshape_dwg_rectangle(std::string_view file, const DwgR2000Objects &o, std::uint64_t handle, Vector3 start,
                                  Vector3 end, double width) {
    return reshape_architecture_rectangle(file, o, handle, start, end, width);
}
std::string set_dwg_continuous_wall(std::string_view file, const DwgR2000Objects &o, const ContinuousWall &chain,
                                    const std::vector<Vector3> &points, double width, double height) {
    return set_architecture_continuous(file, o, chain, points, width, height);
}
} // namespace detail
