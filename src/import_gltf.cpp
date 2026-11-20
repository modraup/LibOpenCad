// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/io/import/gltf/{mod,json_parser}.rs.
#include "import_internal.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <tuple>
#include <utility>

namespace libopencad {
namespace {

using import_detail::add_mesh_entity;
using import_detail::color_from_unit_rgb;
using import_detail::copy_identity;
using import_detail::create_material_layer;
using import_detail::import_error;
using import_detail::is_identity;
using import_detail::kIdentity;
using import_detail::matrix_from_trs;
using import_detail::mesh_from_indexed;
using import_detail::mul_matrix;
using import_detail::percent_decode;
using import_detail::read_path_bytes;
using import_detail::read_stream_bytes;
using import_detail::read_u32_le;
using import_detail::resolve_relative;
using import_detail::transform_positions;

struct JsonValue {
    enum class Kind { Null, Bool, Number, Str, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    const JsonValue *get(std::string_view key) const {
        if (kind != Kind::Object)
            return nullptr;
        for (const auto &p : object)
            if (p.first == key)
                return &p.second;
        return nullptr;
    }
    const JsonValue *at(std::size_t index) const {
        if (kind != Kind::Array || index >= array.size())
            return nullptr;
        return &array[index];
    }
    std::optional<std::string_view> as_str() const {
        if (kind != Kind::Str)
            return std::nullopt;
        return std::string_view(str);
    }
    std::optional<double> as_f64() const {
        if (kind != Kind::Number)
            return std::nullopt;
        return number;
    }
    std::optional<std::size_t> as_usize() const {
        if (kind != Kind::Number)
            return std::nullopt;
        if (number < 0.0)
            return std::nullopt;
        const auto v = static_cast<std::size_t>(number);
        if (number != static_cast<double>(v))
            return std::nullopt;
        return v;
    }
    const std::vector<JsonValue> *as_array() const {
        if (kind != Kind::Array)
            return nullptr;
        return &array;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view src) : src_(src) {}

    JsonValue parse() {
        skip_ws();
        auto value = parse_value(0);
        skip_ws();
        if (i_ != src_.size())
            import_error("JSON parse error: trailing characters");
        return value;
    }

private:
    JsonValue parse_value(int depth) {
        if (depth >= 128)
            import_error("JSON parse error: recursion limit exceeded");
        skip_ws();
        if (i_ >= src_.size())
            import_error("JSON parse error: unexpected end of input");
        const char c = src_[i_];
        if (c == 'n')
            return parse_literal("null", JsonValue{});
        if (c == 't') {
            JsonValue v;
            v.kind = JsonValue::Kind::Bool;
            v.boolean = true;
            return parse_literal("true", v);
        }
        if (c == 'f') {
            JsonValue v;
            v.kind = JsonValue::Kind::Bool;
            v.boolean = false;
            return parse_literal("false", v);
        }
        if (c == '"')
            return parse_string();
        if (c == '[')
            return parse_array(depth);
        if (c == '{')
            return parse_object(depth);
        if (c == '-' || (c >= '0' && c <= '9'))
            return parse_number();
        import_error(std::string("JSON parse error: unexpected character '") + c + "'");
    }

    JsonValue parse_literal(const char *lit, JsonValue v) {
        const auto n = std::strlen(lit);
        if (src_.substr(i_, n) != lit)
            import_error("JSON parse error: invalid literal");
        i_ += n;
        return v;
    }

    JsonValue parse_string() {
        ++i_;
        std::string out;
        while (i_ < src_.size()) {
            const char c = src_[i_++];
            if (c == '"') {
                JsonValue v;
                v.kind = JsonValue::Kind::Str;
                v.str = std::move(out);
                return v;
            }
            if (c == '\\') {
                if (i_ >= src_.size())
                    import_error("JSON parse error: unterminated string");
                const char e = src_[i_++];
                switch (e) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(e);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    if (i_ + 4 > src_.size())
                        import_error("JSON parse error: invalid unicode escape");
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        const auto h = hex(src_[i_++]);
                        if (h < 0)
                            import_error("JSON parse error: invalid unicode escape");
                        code = (code << 4) | static_cast<unsigned>(h);
                    }
                    if (code < 0x80)
                        out.push_back(static_cast<char>(code));
                    else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default:
                    import_error("JSON parse error: invalid escape");
                }
            } else if (static_cast<unsigned char>(c) < 0x20)
                import_error("JSON parse error: control character in string");
            else
                out.push_back(c);
        }
        import_error("JSON parse error: unterminated string");
    }

    JsonValue parse_array(int depth) {
        ++i_;
        JsonValue v;
        v.kind = JsonValue::Kind::Array;
        skip_ws();
        if (i_ < src_.size() && src_[i_] == ']') {
            ++i_;
            return v;
        }
        for (;;) {
            v.array.push_back(parse_value(depth + 1));
            skip_ws();
            if (i_ < src_.size() && src_[i_] == ',') {
                ++i_;
                continue;
            }
            if (i_ < src_.size() && src_[i_] == ']') {
                ++i_;
                return v;
            }
            import_error("JSON parse error: expected ',' or ']'");
        }
    }

    JsonValue parse_object(int depth) {
        ++i_;
        JsonValue v;
        v.kind = JsonValue::Kind::Object;
        skip_ws();
        if (i_ < src_.size() && src_[i_] == '}') {
            ++i_;
            return v;
        }
        for (;;) {
            skip_ws();
            if (i_ >= src_.size() || src_[i_] != '"')
                import_error("JSON parse error: expected object key");
            auto key = parse_string();
            skip_ws();
            if (i_ >= src_.size() || src_[i_] != ':')
                import_error("JSON parse error: expected ':'");
            ++i_;
            auto value = parse_value(depth + 1);
            bool replaced = false;
            for (auto &p : v.object)
                if (p.first == key.str) {
                    p.second = std::move(value);
                    replaced = true;
                    break;
                }
            if (!replaced)
                v.object.emplace_back(std::move(key.str), std::move(value));
            skip_ws();
            if (i_ < src_.size() && src_[i_] == ',') {
                ++i_;
                continue;
            }
            if (i_ < src_.size() && src_[i_] == '}') {
                ++i_;
                return v;
            }
            import_error("JSON parse error: expected ',' or '}'");
        }
    }

    JsonValue parse_number() {
        const auto start = i_;
        if (src_[i_] == '-')
            ++i_;
        if (i_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[i_])))
            import_error("JSON parse error: invalid number");
        if (src_[i_] == '0') {
            ++i_;
            if (i_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[i_])))
                import_error("JSON parse error: invalid number");
        } else {
            while (i_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[i_])))
                ++i_;
        }
        if (i_ < src_.size() && src_[i_] == '.') {
            ++i_;
            if (i_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[i_])))
                import_error("JSON parse error: invalid number");
            while (i_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[i_])))
                ++i_;
        }
        if (i_ < src_.size() && (src_[i_] == 'e' || src_[i_] == 'E')) {
            ++i_;
            if (i_ < src_.size() && (src_[i_] == '+' || src_[i_] == '-'))
                ++i_;
            if (i_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[i_])))
                import_error("JSON parse error: invalid number");
            while (i_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[i_])))
                ++i_;
        }
        const std::string tok(src_.substr(start, i_ - start));
        char *end = nullptr;
        JsonValue v;
        v.kind = JsonValue::Kind::Number;
        v.number = std::strtod(tok.c_str(), &end);
        return v;
    }

    static int hex(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    void skip_ws() {
        while (i_ < src_.size() && import_detail::ascii_whitespace(static_cast<unsigned char>(src_[i_])))
            ++i_;
    }

    std::string_view src_;
    std::size_t i_ = 0;
};

JsonValue parse_json(std::string_view text) { return JsonParser(text).parse(); }

std::vector<std::uint8_t> base64_decode(std::string_view input) {
    static const auto decode = [] {
        std::array<unsigned char, 128> t{};
        t.fill(255);
        for (unsigned char i = 0; i < 26; ++i) {
            t[static_cast<std::size_t>('A' + i)] = i;
            t[static_cast<std::size_t>('a' + i)] = static_cast<unsigned char>(i + 26);
        }
        for (unsigned char i = 0; i < 10; ++i)
            t[static_cast<std::size_t>('0' + i)] = static_cast<unsigned char>(i + 52);
        t[static_cast<std::size_t>('+')] = 62;
        t[static_cast<std::size_t>('/')] = 63;
        return t;
    }();
    std::vector<std::uint8_t> bytes;
    bytes.reserve(input.size());
    for (unsigned char b : input)
        if (b != '=' && b != '\n' && b != '\r' && b != ' ')
            bytes.push_back(b);
    std::vector<std::uint8_t> output;
    output.reserve(bytes.size() * 3 / 4);
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        const auto n = std::min<std::size_t>(4, bytes.size() - i);
        std::uint32_t buf[4] = {0, 0, 0, 0};
        for (std::size_t k = 0; k < n; ++k) {
            const auto b = bytes[i + k];
            if (b >= 128 || decode[b] == 255)
                import_error("Invalid base64 character");
            buf[k] = decode[b];
        }
        const auto packed = (buf[0] << 18) | (buf[1] << 12) | (buf[2] << 6) | buf[3];
        output.push_back(static_cast<std::uint8_t>(packed >> 16));
        if (n > 2)
            output.push_back(static_cast<std::uint8_t>(packed >> 8));
        if (n > 3)
            output.push_back(static_cast<std::uint8_t>(packed));
    }
    return output;
}

std::vector<std::uint8_t> resolve_uri(const std::optional<std::filesystem::path> &base_dir, std::string_view uri) {
    constexpr std::string_view data_prefix = "data:";
    if (uri.size() >= data_prefix.size() && uri.substr(0, data_prefix.size()) == data_prefix) {
        const auto comma = uri.find(',');
        if (comma == std::string_view::npos)
            import_error("glTF data URI is missing a payload");
        return base64_decode(uri.substr(comma + 1));
    }
    if (!base_dir)
        import_error("glTF buffer '" + std::string(uri) + "' needs a file directory to resolve");
    const auto decoded = percent_decode(uri);
    const auto path = resolve_relative(*base_dir, decoded);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        import_error("Cannot read buffer '" + path.string() + "'");
    return read_stream_bytes(in, "'" + path.string() + "'");
}

std::pair<JsonValue, std::vector<std::vector<std::uint8_t>>> parse_glb(const std::vector<std::uint8_t> &data) {
    if (data.size() < 12)
        import_error("GLB file too short");
    std::size_t pos = 0;
    auto read_u32 = [&]() {
        if (pos + 4 > data.size())
            import_error("truncated GLB container: unexpected end of file");
        const auto v = read_u32_le(data.data() + pos);
        pos += 4;
        return v;
    };
    read_u32();
    const auto version = read_u32();
    read_u32();
    if (version != 2)
        import_error("Unsupported GLB version: " + std::to_string(version) + " (only v2 supported)");
    std::optional<JsonValue> json_value;
    std::vector<std::vector<std::uint8_t>> bin_data;
    while (pos + 8 <= data.size()) {
        const auto chunk_length = static_cast<std::size_t>(read_u32());
        const auto chunk_type = read_u32();
        if (pos + chunk_length > data.size())
            break;
        if (chunk_type == 0x4E4F534A) {
            std::string json_str(data.begin() + static_cast<std::ptrdiff_t>(pos),
                                 data.begin() + static_cast<std::ptrdiff_t>(pos + chunk_length));
            json_value = parse_json(json_str);
        } else if (chunk_type == 0x004E4942)
            bin_data.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(pos),
                                  data.begin() + static_cast<std::ptrdiff_t>(pos + chunk_length));
        pos += chunk_length;
    }
    if (!json_value)
        import_error("GLB missing JSON chunk");
    return {std::move(*json_value), std::move(bin_data)};
}

struct PlacedMesh {
    std::size_t index = 0;
    double world[16]{};
    std::string name;
};

std::array<double, 3> json_vec3(const JsonValue *value, std::array<double, 3> def) {
    auto array = value ? value->as_array() : nullptr;
    if (!array || array->size() < 3)
        return def;
    return {(*array)[0].as_f64().value_or(def[0]), (*array)[1].as_f64().value_or(def[1]),
            (*array)[2].as_f64().value_or(def[2])};
}

std::array<double, 4> json_vec4(const JsonValue *value, std::array<double, 4> def) {
    auto array = value ? value->as_array() : nullptr;
    if (!array || array->size() < 4)
        return def;
    return {(*array)[0].as_f64().value_or(def[0]), (*array)[1].as_f64().value_or(def[1]),
            (*array)[2].as_f64().value_or(def[2]), (*array)[3].as_f64().value_or(def[3])};
}

void gltf_node_matrix(const JsonValue &node, double *out) {
    if (auto values = node.get("matrix") ? node.get("matrix")->as_array() : nullptr) {
        if (values->size() >= 16) {
            for (int i = 0; i < 16; ++i)
                out[i] = (*values)[static_cast<std::size_t>(i)].as_f64().value_or(0.0);
            return;
        }
    }
    const auto translation = json_vec3(node.get("translation"), {0, 0, 0});
    const auto rotation = json_vec4(node.get("rotation"), {0, 0, 0, 1});
    const auto scale = json_vec3(node.get("scale"), {1, 1, 1});
    matrix_from_trs(out, translation.data(), rotation.data(), scale.data());
}

std::string mesh_name(const JsonValue &mesh) {
    if (auto n = mesh.get("name"); n && n->as_str() && !n->as_str()->empty())
        return std::string(*n->as_str());
    return "mesh";
}

std::string placed_name(const JsonValue &node, std::size_t mesh_index, const std::vector<JsonValue> &meshes) {
    if (auto n = node.get("name"); n && n->as_str() && !n->as_str()->empty())
        return std::string(*n->as_str());
    if (mesh_index < meshes.size())
        return mesh_name(meshes[mesh_index]);
    return "mesh";
}

void collect_placed(std::size_t index, const double *parent, const std::vector<JsonValue> &nodes,
                    const std::vector<JsonValue> &meshes, std::vector<std::size_t> &path,
                    std::vector<PlacedMesh> &placed) {
    if (path.size() >= 128 || std::find(path.begin(), path.end(), index) != path.end() || index >= nodes.size())
        return;
    path.push_back(index);
    const auto &node = nodes[index];
    double world[16];
    double local[16];
    gltf_node_matrix(node, local);
    mul_matrix(world, parent, local);
    if (auto mesh_index = node.get("mesh") ? node.get("mesh")->as_usize() : std::nullopt) {
        if (*mesh_index < meshes.size()) {
            PlacedMesh p;
            p.index = *mesh_index;
            std::memcpy(p.world, world, sizeof world);
            p.name = placed_name(node, *mesh_index, meshes);
            placed.push_back(std::move(p));
        }
    }
    if (auto children = node.get("children") ? node.get("children")->as_array() : nullptr)
        for (const auto &child : *children)
            if (auto child_index = child.as_usize())
                collect_placed(*child_index, world, nodes, meshes, path, placed);
    path.pop_back();
}

std::optional<std::vector<PlacedMesh>> placed_meshes(const JsonValue &json, const std::vector<JsonValue> &meshes) {
    auto nodes = json.get("nodes") ? json.get("nodes")->as_array() : nullptr;
    if (!nodes)
        return std::nullopt;
    if (nodes->empty())
        return std::nullopt;
    auto scenes = json.get("scenes") ? json.get("scenes")->as_array() : nullptr;
    if (!scenes || scenes->empty())
        return std::nullopt;
    const auto scene_index = json.get("scene") && json.get("scene")->as_usize() ? *json.get("scene")->as_usize() : 0;
    const JsonValue *scene = scene_index < scenes->size() ? &(*scenes)[scene_index] : &(*scenes)[0];
    std::vector<PlacedMesh> placed;
    std::vector<std::size_t> path;
    if (auto roots = scene->get("nodes") ? scene->get("nodes")->as_array() : nullptr)
        for (const auto &root : *roots)
            if (auto index = root.as_usize())
                collect_placed(*index, kIdentity, *nodes, meshes, path, placed);
    return placed;
}

std::vector<std::array<std::size_t, 3>> triangle_indices(const std::vector<std::size_t> &indices, std::size_t mode) {
    std::vector<std::array<std::size_t, 3>> triangles;
    if (mode == 5) {
        if (indices.size() >= 3)
            for (std::size_t offset = 0; offset + 2 < indices.size(); ++offset) {
                const auto a = indices[offset], b = indices[offset + 1], c = indices[offset + 2];
                if (a == b || b == c || a == c)
                    continue;
                if (offset % 2 == 0)
                    triangles.push_back({a, b, c});
                else
                    triangles.push_back({b, a, c});
            }
    } else if (mode == 6) {
        if (indices.size() >= 3)
            for (std::size_t offset = 1; offset + 1 < indices.size(); ++offset)
                triangles.push_back({indices[0], indices[offset], indices[offset + 1]});
    } else if (mode == 4) {
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            triangles.push_back({indices[i], indices[i + 1], indices[i + 2]});
    }
    return triangles;
}

std::vector<std::array<float, 3>> read_accessor_vec3(std::size_t accessor_idx, const std::vector<JsonValue> *accessors,
                                                     const std::vector<JsonValue> *buffer_views,
                                                     const std::vector<std::vector<std::uint8_t>> &buffers) {
    if (!accessors)
        import_error("Missing accessors array");
    if (accessor_idx >= accessors->size())
        import_error("Accessor " + std::to_string(accessor_idx) + " out of range");
    const auto &accessor = (*accessors)[accessor_idx];
    const auto count = accessor.get("count") && accessor.get("count")->as_usize() ? *accessor.get("count")->as_usize() : 0;
    const auto component_type =
        accessor.get("componentType") && accessor.get("componentType")->as_usize() ? *accessor.get("componentType")->as_usize()
                                                                                   : 0;
    auto bv_idx = accessor.get("bufferView") ? accessor.get("bufferView")->as_usize() : std::nullopt;
    const auto byte_offset_acc =
        accessor.get("byteOffset") && accessor.get("byteOffset")->as_usize() ? *accessor.get("byteOffset")->as_usize() : 0;
    if (component_type != 5126)
        import_error("Unsupported POSITION component type: " + std::to_string(component_type) +
                     " (expected FLOAT/5126)");
    if (!bv_idx)
        return {};
    if (!buffer_views)
        import_error("Missing bufferViews array");
    if (*bv_idx >= buffer_views->size())
        import_error("BufferView " + std::to_string(*bv_idx) + " out of range");
    const auto &bv = (*buffer_views)[*bv_idx];
    const auto buffer_idx = bv.get("buffer") && bv.get("buffer")->as_usize() ? *bv.get("buffer")->as_usize() : 0;
    const auto byte_offset_bv =
        bv.get("byteOffset") && bv.get("byteOffset")->as_usize() ? *bv.get("byteOffset")->as_usize() : 0;
    const auto byte_stride = bv.get("byteStride") && bv.get("byteStride")->as_usize() ? *bv.get("byteStride")->as_usize() : 12;
    if (buffer_idx >= buffers.size())
        import_error("Buffer " + std::to_string(buffer_idx) + " not loaded");
    const auto &buffer = buffers[buffer_idx];
    if (byte_offset_bv > std::numeric_limits<std::size_t>::max() - byte_offset_acc)
        import_error("Buffer offset overflow in glTF accessor");
    const auto start = byte_offset_bv + byte_offset_acc;
    const auto max_possible = (start >= buffer.size() || byte_stride == 0) ? 0 : (buffer.size() - start) / byte_stride;
    const auto alloc_cap = std::min(count, max_possible);
    std::vector<std::array<float, 3>> positions;
    positions.reserve(alloc_cap);
    for (std::size_t i = 0; i < count; ++i) {
        if (byte_stride != 0 && i > std::numeric_limits<std::size_t>::max() / byte_stride)
            break;
        const auto stride_offset = i * byte_stride;
        if (start > std::numeric_limits<std::size_t>::max() - stride_offset)
            break;
        const auto offset = start + stride_offset;
        if (offset > std::numeric_limits<std::size_t>::max() - 12 || offset + 12 > buffer.size())
            break;
        positions.push_back({import_detail::read_f32_le(buffer.data() + offset),
                             import_detail::read_f32_le(buffer.data() + offset + 4),
                             import_detail::read_f32_le(buffer.data() + offset + 8)});
    }
    return positions;
}

std::vector<std::size_t> read_accessor_scalar(std::size_t accessor_idx, const std::vector<JsonValue> *accessors,
                                              const std::vector<JsonValue> *buffer_views,
                                              const std::vector<std::vector<std::uint8_t>> &buffers) {
    if (!accessors)
        import_error("Missing accessors array");
    if (accessor_idx >= accessors->size())
        import_error("Accessor " + std::to_string(accessor_idx) + " out of range");
    const auto &accessor = (*accessors)[accessor_idx];
    const auto count = accessor.get("count") && accessor.get("count")->as_usize() ? *accessor.get("count")->as_usize() : 0;
    const auto component_type =
        accessor.get("componentType") && accessor.get("componentType")->as_usize() ? *accessor.get("componentType")->as_usize()
                                                                                   : 0;
    auto bv_idx = accessor.get("bufferView") ? accessor.get("bufferView")->as_usize() : std::nullopt;
    const auto byte_offset_acc =
        accessor.get("byteOffset") && accessor.get("byteOffset")->as_usize() ? *accessor.get("byteOffset")->as_usize() : 0;
    if (!bv_idx)
        return {};
    if (!buffer_views)
        import_error("Missing bufferViews array");
    if (*bv_idx >= buffer_views->size())
        import_error("BufferView " + std::to_string(*bv_idx) + " out of range");
    const auto &bv = (*buffer_views)[*bv_idx];
    const auto buffer_idx = bv.get("buffer") && bv.get("buffer")->as_usize() ? *bv.get("buffer")->as_usize() : 0;
    const auto byte_offset_bv =
        bv.get("byteOffset") && bv.get("byteOffset")->as_usize() ? *bv.get("byteOffset")->as_usize() : 0;
    if (buffer_idx >= buffers.size())
        import_error("Buffer " + std::to_string(buffer_idx) + " not loaded");
    const auto &buffer = buffers[buffer_idx];
    if (byte_offset_bv > std::numeric_limits<std::size_t>::max() - byte_offset_acc)
        import_error("Buffer offset overflow in glTF accessor");
    const auto start = byte_offset_bv + byte_offset_acc;
    const std::size_t item_size = component_type == 5121 ? 1 : component_type == 5123 ? 2 : component_type == 5125 ? 4 : 1;
    const auto max_possible = start >= buffer.size() ? 0 : (buffer.size() - start) / item_size;
    const auto alloc_cap = std::min(count, max_possible);
    std::vector<std::size_t> indices;
    indices.reserve(alloc_cap);
    if (component_type == 5121) {
        for (std::size_t i = 0; i < count; ++i) {
            if (start > std::numeric_limits<std::size_t>::max() - i)
                break;
            const auto offset = start + i;
            if (offset >= buffer.size())
                break;
            indices.push_back(buffer[offset]);
        }
    } else if (component_type == 5123) {
        for (std::size_t i = 0; i < count; ++i) {
            if (i > std::numeric_limits<std::size_t>::max() / 2)
                break;
            const auto offset = start + i * 2;
            if (offset > std::numeric_limits<std::size_t>::max() - 2 || offset + 2 > buffer.size())
                break;
            indices.push_back(static_cast<std::uint16_t>(buffer[offset] | (buffer[offset + 1] << 8)));
        }
    } else if (component_type == 5125) {
        for (std::size_t i = 0; i < count; ++i) {
            if (i > std::numeric_limits<std::size_t>::max() / 4)
                break;
            const auto offset = start + i * 4;
            if (offset > std::numeric_limits<std::size_t>::max() - 4 || offset + 4 > buffer.size())
                break;
            indices.push_back(read_u32_le(buffer.data() + offset));
        }
    } else
        import_error("Unsupported index component type: " + std::to_string(component_type));
    return indices;
}

std::pair<std::string, std::array<float, 4>> get_material_color(std::size_t mat_idx,
                                                                const std::vector<JsonValue> *materials) {
    const std::pair<std::string, std::array<float, 4>> def{"", {0.8f, 0.8f, 0.8f, 1.0f}};
    if (!materials || mat_idx >= materials->size())
        return def;
    const auto &mat = (*materials)[mat_idx];
    std::string name;
    if (auto n = mat.get("name"); n && n->as_str())
        name = std::string(*n->as_str());
    std::array<float, 4> color{0.8f, 0.8f, 0.8f, 1.0f};
    if (auto pbr = mat.get("pbrMetallicRoughness")) {
        if (auto bc = pbr->get("baseColorFactor") ? pbr->get("baseColorFactor")->as_array() : nullptr) {
            if (bc->size() >= 3) {
                color[0] = static_cast<float>((*bc)[0].as_f64().value_or(0.8));
                color[1] = static_cast<float>((*bc)[1].as_f64().value_or(0.8));
                color[2] = static_cast<float>((*bc)[2].as_f64().value_or(0.8));
                if (bc->size() >= 4)
                    color[3] = static_cast<float>((*bc)[3].as_f64().value_or(1.0));
            }
        }
    }
    return {name, color};
}

std::vector<std::array<double, 3>> transform_positions_f32(const std::vector<std::array<float, 3>> &positions,
                                                           const double *world) {
    std::vector<std::array<double, 3>> widened;
    widened.reserve(positions.size());
    for (const auto &p : positions)
        widened.push_back({static_cast<double>(p[0]), static_cast<double>(p[1]), static_cast<double>(p[2])});
    if (is_identity(world))
        return widened;
    return transform_positions(widened, world);
}

struct GltfTables {
    const std::vector<JsonValue> *accessors = nullptr;
    const std::vector<JsonValue> *buffer_views = nullptr;
    const std::vector<JsonValue> *materials = nullptr;
    const std::vector<std::vector<std::uint8_t>> *buffers = nullptr;
    const ImportConfig *config = nullptr;
};

void append_gltf_mesh(DxfDocument &doc, const JsonValue &gltf_mesh, const PlacedMesh &instance, const GltfTables &tables) {
    auto primitives = gltf_mesh.get("primitives") ? gltf_mesh.get("primitives")->as_array() : nullptr;
    if (!primitives)
        return;
    for (const auto &primitive : *primitives) {
        auto attributes = primitive.get("attributes");
        if (!attributes)
            continue;
        auto position_accessor = attributes->get("POSITION") ? attributes->get("POSITION")->as_usize() : std::nullopt;
        if (!position_accessor)
            continue;
        auto positions = read_accessor_vec3(*position_accessor, tables.accessors, tables.buffer_views, *tables.buffers);
        if (positions.empty())
            continue;
        std::vector<std::size_t> indices;
        if (auto index = primitive.get("indices") ? primitive.get("indices")->as_usize() : std::nullopt)
            indices = read_accessor_scalar(*index, tables.accessors, tables.buffer_views, *tables.buffers);
        else
            for (std::size_t i = 0; i < positions.size(); ++i)
                indices.push_back(i);
        const auto mode = primitive.get("mode") && primitive.get("mode")->as_usize() ? *primitive.get("mode")->as_usize() : 4;
        auto triangles = triangle_indices(indices, mode);
        if (triangles.empty())
            continue;
        std::string material_name;
        std::array<float, 4> color{0.8f, 0.8f, 0.8f, 1.0f};
        if (auto material_index = primitive.get("material") ? primitive.get("material")->as_usize() : std::nullopt)
            std::tie(material_name, color) = get_material_color(*material_index, tables.materials);
        auto transformed = transform_positions_f32(positions, instance.world);
        auto mesh = mesh_from_indexed(transformed, triangles, tables.config->scale_factor, tables.config->merge_vertices,
                                      tables.config->merge_tolerance);
        if (mesh.faces.empty())
            continue;
        const auto layer_label = material_name.empty() ? instance.name : material_name;
        const auto entity_color =
            material_name.empty() ? tables.config->default_color : color_from_unit_rgb(color[0], color[1], color[2]);
        const auto layer = create_material_layer(doc, *tables.config, layer_label, entity_color);
        add_mesh_entity(doc, std::move(mesh), layer, entity_color);
    }
}

std::pair<JsonValue, std::vector<std::vector<std::uint8_t>>> parse_gltf_data(const std::vector<std::uint8_t> &data,
                                                                            const std::optional<std::filesystem::path> &base_dir) {
    if (data.size() >= 4 && std::memcmp(data.data(), "glTF", 4) == 0)
        return parse_glb(data);
    const std::string text(data.begin(), data.end());
    auto json = parse_json(text);
    std::vector<std::vector<std::uint8_t>> buffers;
    if (auto buffer_descs = json.get("buffers") ? json.get("buffers")->as_array() : nullptr) {
        for (const auto &buf_desc : *buffer_descs) {
            auto uri = buf_desc.get("uri") ? buf_desc.get("uri")->as_str() : std::nullopt;
            if (!uri)
                import_error("glTF buffer is missing a uri");
            buffers.push_back(resolve_uri(base_dir, *uri));
        }
    }
    return {std::move(json), std::move(buffers)};
}

} // namespace

GltfImporter::GltfImporter(std::vector<std::uint8_t> data, std::optional<std::filesystem::path> base_dir)
    : data_(std::move(data)), base_dir_(std::move(base_dir)) {}

GltfImporter GltfImporter::from_file(const std::filesystem::path &path) {
    return GltfImporter(read_path_bytes(path), path.parent_path());
}

GltfImporter GltfImporter::from_bytes(std::vector<std::uint8_t> data) {
    return GltfImporter(std::move(data), std::nullopt);
}

GltfImporter GltfImporter::from_bytes(std::string_view text) {
    return GltfImporter(std::vector<std::uint8_t>(text.begin(), text.end()), std::nullopt);
}

GltfImporter GltfImporter::from_reader(std::istream &input) {
    return GltfImporter(read_stream_bytes(input, "glTF data"), std::nullopt);
}

GltfImporter &GltfImporter::with_config(ImportConfig config) {
    config_ = std::move(config);
    return *this;
}

DxfDocument GltfImporter::import() const {
    auto [json, buffers] = parse_gltf_data(data_, base_dir_);
    auto doc = DxfDocument::create();
    auto meshes = json.get("meshes") ? json.get("meshes")->as_array() : nullptr;
    if (!meshes)
        return doc;
    GltfTables tables;
    tables.accessors = json.get("accessors") ? json.get("accessors")->as_array() : nullptr;
    tables.buffer_views = json.get("bufferViews") ? json.get("bufferViews")->as_array() : nullptr;
    tables.materials = json.get("materials") ? json.get("materials")->as_array() : nullptr;
    tables.buffers = &buffers;
    tables.config = &config_;
    auto instances = placed_meshes(json, *meshes);
    if (!instances) {
        instances = std::vector<PlacedMesh>{};
        for (std::size_t index = 0; index < meshes->size(); ++index) {
            PlacedMesh p;
            p.index = index;
            copy_identity(p.world);
            p.name = mesh_name((*meshes)[index]);
            instances->push_back(std::move(p));
        }
    }
    for (const auto &instance : *instances) {
        if (instance.index >= meshes->size())
            continue;
        append_gltf_mesh(doc, (*meshes)[instance.index], instance, tables);
    }
    return doc;
}

} // namespace libopencad
