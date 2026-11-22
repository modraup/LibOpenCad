// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/io/import/fbx.rs.
#include "import_internal.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <zlib.h>

namespace libopencad {
namespace {

using import_detail::add_mesh_entity;
using import_detail::color_from_unit_rgb;
using import_detail::copy_identity;
using import_detail::create_material_layer;
using import_detail::import_error;
using import_detail::is_identity;
using import_detail::kIdentity;
using import_detail::mesh_from_indexed;
using import_detail::mul_matrix;
using import_detail::read_path_bytes;
using import_detail::read_stream_bytes;
using import_detail::transform_positions;
using import_detail::trs_euler_xyz;

enum class FbxPropKind { Ignored, I16, I32, I64, F32, F64, Str, ArrayI32, ArrayF64 };

struct FbxProp {
    FbxPropKind kind = FbxPropKind::Ignored;
    std::int16_t i16 = 0;
    std::int32_t i32 = 0;
    std::int64_t i64 = 0;
    float f32 = 0;
    double f64 = 0;
    std::string str;
    std::vector<std::int32_t> array_i32;
    std::vector<double> array_f64;
};

struct FbxNode {
    std::string name;
    std::vector<FbxProp> properties;
    std::vector<FbxNode> children;

    const FbxNode *child(std::string_view n) const {
        for (const auto &c : children)
            if (c.name == n)
                return &c;
        return nullptr;
    }
    std::vector<const FbxNode *> children_named(std::string_view n) const {
        std::vector<const FbxNode *> out;
        for (const auto &c : children)
            if (c.name == n)
                out.push_back(&c);
        return out;
    }
    std::optional<std::string_view> prop_str(std::size_t index) const {
        if (index >= properties.size() || properties[index].kind != FbxPropKind::Str)
            return std::nullopt;
        return std::string_view(properties[index].str);
    }
    std::optional<std::int64_t> prop_i64(std::size_t index) const {
        if (index >= properties.size())
            return std::nullopt;
        const auto &p = properties[index];
        if (p.kind == FbxPropKind::I64)
            return p.i64;
        if (p.kind == FbxPropKind::I32)
            return p.i32;
        if (p.kind == FbxPropKind::I16)
            return p.i16;
        return std::nullopt;
    }
    std::optional<double> prop_f64(std::size_t index) const {
        if (index >= properties.size())
            return std::nullopt;
        const auto &p = properties[index];
        if (p.kind == FbxPropKind::F64)
            return p.f64;
        if (p.kind == FbxPropKind::F32)
            return p.f32;
        if (p.kind == FbxPropKind::I64)
            return static_cast<double>(p.i64);
        if (p.kind == FbxPropKind::I32)
            return p.i32;
        if (p.kind == FbxPropKind::I16)
            return p.i16;
        return std::nullopt;
    }
    const std::vector<double> *prop_f64_array(std::size_t index) const {
        if (index >= properties.size() || properties[index].kind != FbxPropKind::ArrayF64)
            return nullptr;
        return &properties[index].array_f64;
    }
    const std::vector<std::int32_t> *prop_i32_array(std::size_t index) const {
        if (index >= properties.size() || properties[index].kind != FbxPropKind::ArrayI32)
            return nullptr;
        return &properties[index].array_i32;
    }
};

struct FbxGeometry {
    std::int64_t id = 0;
    std::string name;
    std::vector<std::array<double, 3>> vertices;
    std::vector<std::int32_t> polygon_indices;
    std::vector<std::int32_t> material_slots;
    bool per_polygon = false;

    int material_slot(std::size_t polygon) const {
        if (!per_polygon && material_slots.empty()) return 0;
        const auto index = per_polygon ? polygon : 0;
        return index < material_slots.size() ? material_slots[index] : -1;
    }
};

struct FbxMaterialInfo {
    std::int64_t id = 0;
    std::string name;
    float diffuse[3] = {0.8f, 0.8f, 0.8f};
};

struct FbxModel {
    std::string name;
    double local[16]{};
};

struct Cursor {
    const std::uint8_t *data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;

    bool remaining(std::size_t n) const { return pos + n <= size; }
    std::uint8_t read_u8(std::uint8_t fallback = 0) {
        if (!remaining(1))
            return fallback;
        return data[pos++];
    }
    std::uint32_t read_u32(std::uint32_t fallback = 0) {
        if (!remaining(4))
            return fallback;
        auto v = import_detail::read_u32_le(data + pos);
        pos += 4;
        return v;
    }
    std::uint64_t read_u64(std::uint64_t fallback = 0) {
        if (!remaining(8))
            return fallback;
        std::uint64_t v = import_detail::read_u32_le(data + pos) |
                          (static_cast<std::uint64_t>(import_detail::read_u32_le(data + pos + 4)) << 32);
        pos += 8;
        return v;
    }
    std::int16_t read_i16() {
        if (!remaining(2))
            return 0;
        auto v = static_cast<std::int16_t>(data[pos] | (data[pos + 1] << 8));
        pos += 2;
        return v;
    }
    std::int32_t read_i32() { return static_cast<std::int32_t>(read_u32()); }
    std::int64_t read_i64() { return static_cast<std::int64_t>(read_u64()); }
    float read_f32() {
        if (!remaining(4))
            return 0;
        auto v = import_detail::read_f32_le(data + pos);
        pos += 4;
        return v;
    }
    double read_f64() {
        if (!remaining(8))
            return 0;
        std::uint64_t bits = import_detail::read_u32_le(data + pos) |
                             (static_cast<std::uint64_t>(import_detail::read_u32_le(data + pos + 4)) << 32);
        pos += 8;
        double v;
        std::memcpy(&v, &bits, sizeof v);
        return v;
    }
};

constexpr char kFbxMagic[21] = {'K', 'a', 'y', 'd', 'a', 'r', 'a', ' ', 'F', 'B', 'X',
                                ' ', 'B', 'i', 'n', 'a', 'r', 'y', ' ', ' ', '\0'};
constexpr std::size_t kMaxFbxArrayBytes = 64 * 1024 * 1024;

bool is_binary_fbx(const std::vector<std::uint8_t> &data) {
    return data.size() >= 27 && std::memcmp(data.data(), kFbxMagic, 21) == 0;
}

std::vector<std::uint8_t> decompress_array_data(Cursor &cursor, std::uint32_t encoding, std::uint32_t compressed_len,
                                                std::size_t element_size, std::uint32_t count) {
    if (element_size != 0 && count > std::numeric_limits<std::size_t>::max() / element_size)
        import_error("FBX array byte length overflow");
    const auto expected = static_cast<std::size_t>(count) * element_size;
    if (expected > kMaxFbxArrayBytes)
        import_error("FBX array size (" + std::to_string(expected) +
                     " bytes) exceeds maximum limit (" + std::to_string(kMaxFbxArrayBytes) + " bytes)");
    if (encoding == 1) {
        if (cursor.pos > std::numeric_limits<std::size_t>::max() - compressed_len)
            import_error("FBX compressed array offset overflow");
        const auto compressed_end = cursor.pos + compressed_len;
        if (compressed_end > cursor.size)
            import_error("FBX compressed array out of bounds");
        z_stream strm{};
        if (inflateInit(&strm) != Z_OK)
            import_error("FBX array decompression error: inflateInit failed");
        strm.next_in = const_cast<Bytef *>(cursor.data + cursor.pos);
        strm.avail_in = compressed_len;
        std::vector<std::uint8_t> decompressed(expected);
        strm.next_out = decompressed.data();
        strm.avail_out = static_cast<uInt>(expected);
        const auto rc = inflate(&strm, Z_FINISH);
        const auto produced = expected - strm.avail_out;
        inflateEnd(&strm);
        cursor.pos = compressed_end;
        if ((rc != Z_STREAM_END && rc != Z_OK) || produced != expected)
            import_error("FBX array decompression error: truncated or corrupt zlib stream");
        return decompressed;
    }
    if (cursor.pos > std::numeric_limits<std::size_t>::max() - expected)
        import_error("FBX array offset overflow");
    const auto uncompressed_end = cursor.pos + expected;
    if (uncompressed_end > cursor.size)
        import_error("FBX array out of bounds");
    std::vector<std::uint8_t> result(cursor.data + cursor.pos, cursor.data + uncompressed_end);
    cursor.pos = uncompressed_end;
    return result;
}

FbxProp read_array_f64(Cursor &cursor) {
    const auto count = cursor.read_u32();
    const auto encoding = cursor.read_u32();
    const auto compressed_len = cursor.read_u32();
    const auto raw = decompress_array_data(cursor, encoding, compressed_len, 8, count);
    const auto alloc_cap = std::min(static_cast<std::size_t>(count), raw.size() / 8);
    FbxProp prop;
    prop.kind = FbxPropKind::ArrayF64;
    prop.array_f64.reserve(alloc_cap);
    Cursor inner{raw.data(), raw.size(), 0};
    for (std::size_t i = 0; i < alloc_cap; ++i)
        prop.array_f64.push_back(inner.read_f64());
    return prop;
}

FbxProp read_array_i32(Cursor &cursor) {
    const auto count = cursor.read_u32();
    const auto encoding = cursor.read_u32();
    const auto compressed_len = cursor.read_u32();
    const auto raw = decompress_array_data(cursor, encoding, compressed_len, 4, count);
    const auto alloc_cap = std::min(static_cast<std::size_t>(count), raw.size() / 4);
    FbxProp prop;
    prop.kind = FbxPropKind::ArrayI32;
    prop.array_i32.reserve(alloc_cap);
    Cursor inner{raw.data(), raw.size(), 0};
    for (std::size_t i = 0; i < alloc_cap; ++i)
        prop.array_i32.push_back(inner.read_i32());
    return prop;
}

FbxProp read_ignored_array(Cursor &cursor, std::size_t element_size) {
    const auto count = cursor.read_u32();
    const auto encoding = cursor.read_u32();
    const auto compressed_len = cursor.read_u32();
    decompress_array_data(cursor, encoding, compressed_len, element_size, count);
    return {};
}

FbxProp read_fbx_property(Cursor &cursor) {
    if (!cursor.remaining(1))
        import_error("Cannot read FBX property type: unexpected end of file");
    const auto type_code = cursor.read_u8();
    FbxProp prop;
    switch (type_code) {
    case 'C':
        cursor.read_u8();
        return prop;
    case 'Y':
        prop.kind = FbxPropKind::I16;
        prop.i16 = cursor.read_i16();
        return prop;
    case 'I':
        prop.kind = FbxPropKind::I32;
        prop.i32 = cursor.read_i32();
        return prop;
    case 'L':
        prop.kind = FbxPropKind::I64;
        prop.i64 = cursor.read_i64();
        return prop;
    case 'F':
        prop.kind = FbxPropKind::F32;
        prop.f32 = cursor.read_f32();
        return prop;
    case 'D':
        prop.kind = FbxPropKind::F64;
        prop.f64 = cursor.read_f64();
        return prop;
    case 'S':
    case 'R': {
        const auto len = static_cast<std::size_t>(cursor.read_u32());
        if (cursor.pos + len > cursor.size)
            import_error("FBX string out of bounds");
        if (type_code == 'S') {
            prop.kind = FbxPropKind::Str;
            prop.str.assign(reinterpret_cast<const char *>(cursor.data + cursor.pos), len);
        }
        cursor.pos += len;
        return prop;
    }
    case 'i':
        return read_array_i32(cursor);
    case 'l':
        return read_ignored_array(cursor, 8);
    case 'f':
        return read_ignored_array(cursor, 4);
    case 'd':
        return read_array_f64(cursor);
    case 'b':
        return read_ignored_array(cursor, 1);
    default:
        import_error(std::string("Unknown FBX property type: '") + static_cast<char>(type_code) + "'");
    }
}

std::optional<FbxNode> read_fbx_node(Cursor &cursor, bool use_64bit);

std::optional<FbxNode> read_fbx_node(Cursor &cursor, bool use_64bit) {
    std::uint64_t end_offset = 0, num_properties = 0, property_list_len = 0;
    std::uint8_t name_len = 0;
    if (use_64bit) {
        end_offset = cursor.read_u64();
        num_properties = cursor.read_u64();
        property_list_len = cursor.read_u64();
        name_len = cursor.read_u8();
    } else {
        end_offset = cursor.read_u32();
        num_properties = cursor.read_u32();
        property_list_len = cursor.read_u32();
        name_len = cursor.read_u8();
    }
    if (end_offset == 0 && num_properties == 0 && property_list_len == 0 && name_len == 0)
        return std::nullopt;
    if (end_offset > 0) {
        if (end_offset > cursor.size)
            import_error("FBX node end_offset exceeds file length");
        if (end_offset < cursor.pos)
            import_error("FBX node end_offset is before current position");
    }
    if (cursor.pos + name_len > cursor.size)
        import_error("FBX node name out of bounds");
    FbxNode node;
    node.name.assign(reinterpret_cast<const char *>(cursor.data + cursor.pos), name_len);
    cursor.pos += name_len;
    const auto prop_start = cursor.pos;
    for (std::uint64_t i = 0; i < num_properties; ++i) {
        if (cursor.pos >= prop_start + property_list_len)
            break;
        try {
            node.properties.push_back(read_fbx_property(cursor));
        } catch (...) {
            break;
        }
    }
    cursor.pos = prop_start + static_cast<std::size_t>(property_list_len);
    if (end_offset > 0 && cursor.pos < end_offset) {
        while (cursor.pos < end_offset) {
            const auto child_pos = cursor.pos;
            auto child = read_fbx_node(cursor, use_64bit);
            if (!child)
                break;
            if (cursor.pos <= child_pos)
                import_error("Corrupted FBX file: child node offset did not advance");
            node.children.push_back(std::move(*child));
        }
    }
    cursor.pos = static_cast<std::size_t>(end_offset);
    return node;
}

std::vector<FbxNode> parse_binary_fbx(const std::vector<std::uint8_t> &data) {
    if (!is_binary_fbx(data))
        import_error("Not a binary FBX file");
    Cursor cursor{data.data(), data.size(), 23};
    const auto version = cursor.read_u32();
    const bool use_64bit = version >= 7500;
    std::vector<FbxNode> nodes;
    std::size_t pos = 27;
    for (;;) {
        cursor.pos = pos;
        auto node = read_fbx_node(cursor, use_64bit);
        if (!node)
            break;
        const auto next_pos = cursor.pos;
        if (next_pos <= pos)
            import_error("Corrupted FBX file: node offset did not advance");
        pos = next_pos;
        nodes.push_back(std::move(*node));
    }
    return nodes;
}

struct AsciiCursor {
    std::string text;
    std::size_t i = 0;
    std::optional<char> peek() const {
        if (i >= text.size())
            return std::nullopt;
        return text[i];
    }
    char next_char() { return i < text.size() ? text[i++] : '\0'; }
};

bool ascii_alnum(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

void skip_ws_and_comments(AsciiCursor &c) {
    for (;;) {
        auto p = c.peek();
        if (!p)
            return;
        if (import_detail::ascii_whitespace(static_cast<unsigned char>(*p))) {
            c.next_char();
            continue;
        }
        if (*p == ';') {
            while (c.peek()) {
                auto ch = c.next_char();
                if (ch == '\n')
                    break;
            }
            continue;
        }
        return;
    }
}

void skip_ws_no_newline(AsciiCursor &c) {
    while (auto p = c.peek()) {
        if (*p == ' ' || *p == '\t')
            c.next_char();
        else
            break;
    }
}

std::string parse_ascii_string(AsciiCursor &c) {
    c.next_char();
    std::string s;
    for (;;) {
        if (!c.peek())
            import_error("Unterminated FBX string");
        const auto ch = c.next_char();
        if (ch == '"')
            return s;
        if (ch == '\\') {
            if (c.peek())
                s.push_back(c.next_char());
        } else
            s.push_back(ch);
    }
}

std::string parse_ascii_number_str(AsciiCursor &c) {
    std::string s;
    while (auto p = c.peek()) {
        if (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.' || *p == '-' || *p == '+' || *p == 'e' ||
            *p == 'E') {
            s.push_back(*p);
            c.next_char();
        } else
            break;
    }
    return s;
}

std::vector<std::string> parse_ascii_array_values(AsciiCursor &c) {
    std::vector<std::string> values;
    std::string current;
    for (;;) {
        skip_ws_and_comments(c);
        auto p = c.peek();
        if (!p || *p == '}') {
            if (!current.empty())
                values.push_back(current);
            break;
        }
        if (*p == ',') {
            c.next_char();
            if (!current.empty()) {
                values.push_back(current);
                current.clear();
            }
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(*p)) || *p == '-' || *p == '+' || *p == '.') {
            current.push_back(*p);
            c.next_char();
            while (auto nc = c.peek()) {
                if (std::isdigit(static_cast<unsigned char>(*nc)) || *nc == '.' || *nc == '-' || *nc == '+' ||
                    *nc == 'e' || *nc == 'E') {
                    current.push_back(*nc);
                    c.next_char();
                } else
                    break;
            }
            continue;
        }
        c.next_char();
    }
    return values;
}

std::optional<FbxNode> parse_ascii_node(AsciiCursor &c);

std::optional<FbxNode> parse_ascii_node(AsciiCursor &c) {
    skip_ws_and_comments(c);
    std::string name;
    for (;;) {
        auto p = c.peek();
        if (!p)
            return std::nullopt;
        if (ascii_alnum(*p) || *p == '_') {
            name.push_back(*p);
            c.next_char();
            continue;
        }
        if (*p == ':') {
            c.next_char();
            break;
        }
        if (*p == '}')
            return std::nullopt;
        import_error(std::string("Unexpected character '") + *p + "' in FBX node name");
    }
    FbxNode node;
    node.name = name;
    skip_ws_no_newline(c);
    bool in_block = false;
    for (;;) {
        auto p = c.peek();
        if (!p)
            break;
        if (*p == '{') {
            c.next_char();
            in_block = true;
            break;
        }
        if (*p == '\n' || *p == '\r') {
            c.next_char();
            skip_ws_and_comments(c);
            if (c.peek() == '{') {
                c.next_char();
                in_block = true;
                break;
            }
            return node;
        }
        if (*p == ',') {
            c.next_char();
            skip_ws_no_newline(c);
            continue;
        }
        if (*p == '"') {
            FbxProp prop;
            prop.kind = FbxPropKind::Str;
            prop.str = parse_ascii_string(c);
            node.properties.push_back(std::move(prop));
            continue;
        }
        if (*p == '-' || *p == '+' || std::isdigit(static_cast<unsigned char>(*p))) {
            const auto num_str = parse_ascii_number_str(c);
            FbxProp prop;
            if (num_str.find('.') != std::string::npos || num_str.find('e') != std::string::npos ||
                num_str.find('E') != std::string::npos) {
                char *end = nullptr;
                prop.kind = FbxPropKind::F64;
                prop.f64 = std::strtod(num_str.c_str(), &end);
            } else {
                char *end = nullptr;
                prop.kind = FbxPropKind::I64;
                prop.i64 = std::strtoll(num_str.c_str(), &end, 10);
            }
            node.properties.push_back(std::move(prop));
            continue;
        }
        if (*p == '*') {
            c.next_char();
            parse_ascii_number_str(c);
            skip_ws_and_comments(c);
            if (c.peek() == '{') {
                c.next_char();
                skip_ws_and_comments(c);
                std::string prefix;
                for (;;) {
                    auto q = c.peek();
                    if (q == ':') {
                        c.next_char();
                        break;
                    }
                    if (q && ascii_alnum(*q)) {
                        prefix.push_back(*q);
                        c.next_char();
                    } else
                        break;
                }
                const auto values = parse_ascii_array_values(c);
                skip_ws_and_comments(c);
                if (c.peek() == '}')
                    c.next_char();
                if (prefix == "a") {
                    FbxProp prop;
                    // ASCII FBX permits whole-number spellings in floating-point arrays.
                    const bool real_values = node.name == "Vertices" ||
                        std::any_of(values.begin(), values.end(), [](const auto &v) {
                            return v.find_first_of(".eE") != std::string::npos;
                        });
                    if (real_values) {
                        prop.kind = FbxPropKind::ArrayF64;
                        for (const auto &v : values) {
                            char *end = nullptr;
                            const double n = std::strtod(v.c_str(), &end);
                            if (!v.empty() && end == v.c_str() + v.size())
                                prop.array_f64.push_back(n);
                        }
                    } else {
                        prop.kind = FbxPropKind::ArrayI32;
                        for (const auto &v : values) {
                            char *end = nullptr;
                            const auto n = std::strtoll(v.c_str(), &end, 10);
                            if (!v.empty() && end == v.c_str() + v.size())
                                prop.array_i32.push_back(static_cast<std::int32_t>(n));
                        }
                    }
                    node.properties.push_back(std::move(prop));
                }
            }
            continue;
        }
        c.next_char();
    }
    if (in_block) {
        for (;;) {
            skip_ws_and_comments(c);
            auto p = c.peek();
            if (!p)
                break;
            if (*p == '}') {
                c.next_char();
                break;
            }
            auto child = parse_ascii_node(c);
            if (child)
                node.children.push_back(std::move(*child));
            else {
                if (c.peek() == '}')
                    c.next_char();
                break;
            }
        }
    }
    return node;
}

std::vector<FbxNode> parse_ascii_fbx(const std::vector<std::uint8_t> &data) {
    AsciiCursor cursor;
    cursor.text.assign(data.begin(), data.end());
    std::vector<FbxNode> nodes;
    for (;;) {
        skip_ws_and_comments(cursor);
        if (!cursor.peek())
            break;
        auto node = parse_ascii_node(cursor);
        if (!node)
            break;
        nodes.push_back(std::move(*node));
    }
    return nodes;
}

std::string after_colon_colon(std::string_view raw) {
    const auto pos = raw.rfind("::");
    auto name = pos == std::string_view::npos ? std::string(raw) : std::string(raw.substr(pos + 2));
    while (!name.empty() && import_detail::ascii_whitespace(static_cast<unsigned char>(name.front())))
        name.erase(name.begin());
    while (!name.empty() && import_detail::ascii_whitespace(static_cast<unsigned char>(name.back())))
        name.pop_back();
    return name;
}

// Layer 0 selects its material element by typed index, independently of storage order.
const FbxNode *material_element(const FbxNode &geometry) {
    for (auto layer : geometry.children_named("Layer")) {
        if (layer->prop_i64(0) != 0) continue;
        for (auto element : layer->children_named("LayerElement")) {
            auto type = element->child("Type");
            auto index = element->child("TypedIndex");
            if (!type || type->prop_str(0) != "LayerElementMaterial" || !index) continue;
            for (auto material : geometry.children_named("LayerElementMaterial"))
                if (material->prop_i64(0) == index->prop_i64(0)) return material;
            return nullptr;
        }
    }
    return geometry.child("LayerElementMaterial");
}

std::optional<FbxGeometry> extract_geometry(const FbxNode &node) {
    auto id = node.prop_i64(0);
    if (!id)
        return std::nullopt;
    const auto raw_name = node.prop_str(1).value_or("geometry");
    auto name = after_colon_colon(raw_name);
    const auto subtype = node.prop_str(2).value_or("");
    if (subtype != "Mesh" && !subtype.empty())
        return std::nullopt;
    auto vertices_node = node.child("Vertices");
    auto vertices_arr = vertices_node ? vertices_node->prop_f64_array(0) : nullptr;
    if (!vertices_arr || vertices_arr->size() < 3)
        return std::nullopt;
    FbxGeometry geom;
    geom.id = *id;
    geom.name = std::move(name);
    for (std::size_t i = 0; i + 2 < vertices_arr->size(); i += 3)
        geom.vertices.push_back({(*vertices_arr)[i], (*vertices_arr)[i + 1], (*vertices_arr)[i + 2]});
    auto indices_node = node.child("PolygonVertexIndex");
    auto indices = indices_node ? indices_node->prop_i32_array(0) : nullptr;
    if (!indices)
        return std::nullopt;
    geom.polygon_indices = *indices;
    if (auto material = material_element(node)) {
        const auto mapping = material->child("MappingInformationType");
        geom.per_polygon = mapping && mapping->prop_str(0) == "ByPolygon";
        const auto slots = material->child("Materials");
        if (slots && (geom.per_polygon || (mapping && mapping->prop_str(0) == "AllSame")))
            if (auto values = slots->prop_i32_array(0)) geom.material_slots = *values;
    }
    return geom;
}

std::optional<FbxMaterialInfo> extract_material(const FbxNode &node) {
    auto id = node.prop_i64(0);
    if (!id)
        return std::nullopt;
    FbxMaterialInfo mat;
    mat.id = *id;
    mat.name = after_colon_colon(node.prop_str(1).value_or("material"));
    if (auto props70 = node.child("Properties70")) {
        for (auto p : props70->children_named("P")) {
            if (p->prop_str(0) != "DiffuseColor" || p->properties.size() < 7)
                continue;
            auto rf = p->prop_f64(4), gf = p->prop_f64(5), bf = p->prop_f64(6);
            if (!rf || !gf || !bf)
                continue;
            mat.diffuse[0] = *rf;
            mat.diffuse[1] = *gf;
            mat.diffuse[2] = *bf;
        }
    }
    return mat;
}

void model_local_matrix(const FbxNode &node, double *out) {
    double translation[3] = {0, 0, 0};
    double rotation[3] = {0, 0, 0};
    double scale[3] = {1, 1, 1};
    if (auto properties = node.child("Properties70")) {
        for (auto property : properties->children_named("P")) {
            auto name = property->prop_str(0);
            auto x = property->prop_f64(4);
            auto y = property->prop_f64(5);
            auto z = property->prop_f64(6);
            if (!name || !x || !y || !z)
                continue;
            if (*name == "Lcl Translation") {
                translation[0] = *x;
                translation[1] = *y;
                translation[2] = *z;
            } else if (*name == "Lcl Rotation") {
                rotation[0] = *x;
                rotation[1] = *y;
                rotation[2] = *z;
            } else if (*name == "Lcl Scaling") {
                scale[0] = *x;
                scale[1] = *y;
                scale[2] = *z;
            }
        }
    }
    trs_euler_xyz(out, translation, rotation, scale);
}

void model_world(std::int64_t id, const std::unordered_map<std::int64_t, FbxModel> &models,
                 const std::unordered_map<std::int64_t, std::int64_t> &parents, double *out) {
    std::vector<const double *> chain;
    std::optional<std::int64_t> current = id;
    std::unordered_set<std::int64_t> seen;
    while (current) {
        if (!seen.insert(*current).second || chain.size() >= 64)
            break;
        auto it = models.find(*current);
        if (it != models.end())
            chain.push_back(it->second.local);
        auto p = parents.find(*current);
        current = p == parents.end() ? std::nullopt : std::optional<std::int64_t>(p->second);
    }
    copy_identity(out);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        double next[16];
        mul_matrix(next, out, *it);
        std::memcpy(out, next, sizeof next);
    }
}

using TrianglesByMaterial = std::map<int, std::vector<std::array<std::size_t, 3>>>;

TrianglesByMaterial triangulate_by_material(const FbxGeometry &geometry, std::size_t slot_count) {
    TrianglesByMaterial groups;
    std::vector<std::size_t> polygon;
    std::size_t polygon_number = 0;
    for (auto index : geometry.polygon_indices) {
        polygon.push_back(static_cast<std::size_t>(index < 0 ? ~index : index));
        if (index >= 0) continue;
        const int slot = geometry.material_slot(polygon_number++);
        if (polygon.size() >= 3) {
            auto &triangles = groups[slot >= 0 && static_cast<std::size_t>(slot) < slot_count ? slot : -1];
            for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
                triangles.push_back({polygon.front(), polygon[i], polygon[i + 1]});
        }
        polygon.clear();
    }
    return groups;
}

// In the output XYZ frame, X is right, Y is forward and Z is up.
struct SceneCoordinates {
    std::array<int, 3> axis{0, 2, 1};
    std::array<int, 3> sign{1, -1, 1};
    double meters = 0.01;

    void apply(MeshDefinition &mesh) const {
        for (auto &p : mesh.vertices) {
            const std::array<double, 3> v{p.x, p.y, p.z};
            p = Vector3{sign[0] * v[axis[0]], sign[1] * v[axis[1]], sign[2] * v[axis[2]]} * meters;
        }
        const int parity = (axis[1] - axis[0]) * (axis[2] - axis[0]) * (axis[2] - axis[1]);
        if (parity * sign[0] * sign[1] * sign[2] < 0) mesh.flip_normals();
    }
};

SceneCoordinates scene_coordinates(const std::vector<FbxNode> &nodes) {
    SceneCoordinates frame;
    for (const auto &node : nodes) {
        if (node.name != "GlobalSettings") continue;
        const auto *props = node.child("Properties70");
        if (!props) continue;
        for (const auto *p : props->children_named("P")) {
            const auto name = p->prop_str(0);
            const auto value = p->prop_f64(4);
            if (!name || !value) continue;
            if (*name == "UnitScaleFactor") {
                frame.meters = *value / 100;
                if (!std::isfinite(frame.meters) || frame.meters <= 0) import_error("Invalid FBX unit scale");
            }
            static constexpr std::array<std::string_view, 3> axes{"CoordAxis", "FrontAxis", "UpAxis"};
            static constexpr std::array<std::string_view, 3> signs{"CoordAxisSign", "FrontAxisSign", "UpAxisSign"};
            for (std::size_t i = 0; i < axes.size(); ++i) {
                if (*name == axes[i]) {
                    if (*value != 0 && *value != 1 && *value != 2) import_error("Invalid FBX axis");
                    frame.axis[i] = static_cast<int>(*value);
                } else if (*name == signs[i]) {
                    if (*value != -1 && *value != 1) import_error("Invalid FBX axis sign");
                    frame.sign[i] = static_cast<int>(*value);
                }
            }
        }
        if (frame.axis[0] == frame.axis[1] || frame.axis[0] == frame.axis[2] || frame.axis[1] == frame.axis[2])
            import_error("Invalid FBX coordinate system");
        break;
    }
    return frame;
}

DxfDocument build_document(const std::vector<FbxNode> &nodes, const ImportConfig &config, const SceneCoordinates *frame = nullptr) {
    auto doc = DxfDocument::create();
    const FbxNode *objects = nullptr;
    for (const auto &n : nodes)
        if (n.name == "Objects") {
            objects = &n;
            break;
        }
    if (!objects)
        return doc;
    std::vector<FbxGeometry> geometries;
    std::unordered_map<std::int64_t, std::size_t> geometry_ids;
    for (auto node : objects->children_named("Geometry"))
        if (auto geom = extract_geometry(*node)) {
            geometry_ids[geom->id] = geometries.size();
            geometries.push_back(std::move(*geom));
        }
    std::unordered_map<std::int64_t, FbxMaterialInfo> materials;
    for (auto node : objects->children_named("Material"))
        if (auto mat = extract_material(*node))
            materials[mat->id] = std::move(*mat);
    std::unordered_map<std::int64_t, FbxModel> models;
    for (auto node : objects->children_named("Model")) {
        if (auto id = node->prop_i64(0)) {
            FbxModel model;
            model.name = after_colon_colon(node->prop_str(1).value_or("Model"));
            model_local_matrix(*node, model.local);
            models[*id] = std::move(model);
        }
    }
    const FbxNode *connections = nullptr;
    for (const auto &n : nodes)
        if (n.name == "Connections") {
            connections = &n;
            break;
        }
    std::unordered_map<std::int64_t, std::int64_t> geom_to_model;
    std::unordered_map<std::int64_t, std::vector<std::int64_t>> mat_to_model;
    std::unordered_map<std::int64_t, std::int64_t> model_parent;
    if (connections) {
        for (auto c : connections->children_named("C")) {
            if (c->properties.size() < 3 || c->prop_str(0) != "OO")
                continue;
            auto child_id = c->prop_i64(1);
            auto parent_id = c->prop_i64(2);
            if (!child_id || !parent_id)
                continue;
            if (geometry_ids.count(*child_id))
                geom_to_model[*child_id] = *parent_id;
            if (materials.count(*child_id))
                mat_to_model[*parent_id].push_back(*child_id);
            if (models.count(*child_id) && models.count(*parent_id))
                model_parent[*child_id] = *parent_id;
        }
    }
    for (const auto &geom : geometries) {
        std::optional<std::int64_t> model_id;
        if (auto it = geom_to_model.find(geom.id); it != geom_to_model.end())
            model_id = it->second;
        std::string model_name = geom.name;
        if (model_id)
            if (auto mit = models.find(*model_id); mit != models.end())
                model_name = mit->second.name;
        const auto slots = model_id ? mat_to_model.find(*model_id) : mat_to_model.end();
        const auto slot_count = slots == mat_to_model.end() ? 0 : slots->second.size();
        double world[16];
        copy_identity(world);
        if (model_id)
            model_world(*model_id, models, model_parent, world);
        std::vector<std::array<double, 3>> transformed;
        if (!is_identity(world)) transformed = transform_positions(geom.vertices, world);
        const auto &vertices = transformed.empty() ? geom.vertices : transformed;
        for (const auto &[slot, triangles] : triangulate_by_material(geom, slot_count)) {
            auto mesh = mesh_from_indexed(vertices, triangles, config.scale_factor, config.merge_vertices,
                                          config.merge_tolerance);
            if (mesh.faces.empty()) continue;
            if (frame) frame->apply(mesh);
            auto color = config.default_color;
            auto label = model_name;
            if (slot >= 0) {
                const auto &mat = materials.at(slots->second[slot]);
                color = color_from_unit_rgb(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
                label = mat.name;
            }
            const auto layer = create_material_layer(doc, config, label, color);
            add_mesh_entity(doc, std::move(mesh), layer, color);
        }
    }
    return doc;
}

} // namespace

FbxImporter::FbxImporter(std::vector<std::uint8_t> data) : data_(std::move(data)) {}

FbxImporter FbxImporter::from_file(const std::filesystem::path &path) { return FbxImporter(read_path_bytes(path)); }

FbxImporter FbxImporter::from_bytes(std::vector<std::uint8_t> data) { return FbxImporter(std::move(data)); }

FbxImporter FbxImporter::from_bytes(std::string_view text) {
    return FbxImporter(std::vector<std::uint8_t>(text.begin(), text.end()));
}

FbxImporter FbxImporter::from_reader(std::istream &input) {
    return FbxImporter(read_stream_bytes(input, "FBX data"));
}

FbxImporter &FbxImporter::with_config(ImportConfig config) {
    config_ = std::move(config);
    return *this;
}

DxfDocument FbxImporter::import() const {
    if (is_binary_fbx(data_))
        return build_document(parse_binary_fbx(data_), config_);
    return build_document(parse_ascii_fbx(data_), config_);
}

DxfDocument FbxImporter::import_z_up_meters() const {
    const auto nodes = is_binary_fbx(data_) ? parse_binary_fbx(data_) : parse_ascii_fbx(data_);
    const auto frame = scene_coordinates(nodes);
    return build_document(nodes, config_, &frame);
}

} // namespace libopencad
