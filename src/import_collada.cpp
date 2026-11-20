// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/io/import/collada/{mod,parser}.rs.
#include "import_internal.hpp"

#include <cctype>
#include <cstdlib>
#include <unordered_map>
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
using import_detail::mesh_from_indexed;
using import_detail::mul_matrix;
using import_detail::read_path_bytes;
using import_detail::read_stream_bytes;
using import_detail::rotation_axis_angle;
using import_detail::scale_matrix;
using import_detail::transform_positions;
using import_detail::translation_matrix;

enum class XmlKind { Start, Empty, End, Text, Eof };

struct XmlEvent {
    XmlKind kind = XmlKind::Eof;
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;
};

std::string local_name(std::string_view full) {
    const auto pos = full.find(':');
    return std::string(pos == std::string_view::npos ? full : full.substr(pos + 1));
}

std::string xml_unescape(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        if (in[i] != '&') {
            out.push_back(in[i]);
            ++i;
            continue;
        }
        const auto semi = in.find(';', i + 1);
        if (semi == std::string_view::npos)
            import_error("XML unescape error: unterminated entity");
        const auto ent = in.substr(i + 1, semi - (i + 1));
        if (ent == "amp")
            out.push_back('&');
        else if (ent == "lt")
            out.push_back('<');
        else if (ent == "gt")
            out.push_back('>');
        else if (ent == "quot")
            out.push_back('"');
        else if (ent == "apos")
            out.push_back('\'');
        else if (!ent.empty() && ent.front() == '#') {
            unsigned long code = 0;
            if (ent.size() >= 2 && (ent[1] == 'x' || ent[1] == 'X'))
                code = std::strtoul(std::string(ent.substr(2)).c_str(), nullptr, 16);
            else
                code = std::strtoul(std::string(ent.substr(1)).c_str(), nullptr, 10);
            if (code == 0 || code > 0x10FFFFul)
                import_error("XML unescape error: invalid numeric entity");
            if (code < 0x80)
                out.push_back(static_cast<char>(code));
            else if (code < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else if (code < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        } else
            import_error("XML unescape error: unknown entity");
        i = semi + 1;
    }
    return out;
}

class XmlReader {
public:
    explicit XmlReader(std::string_view src) : src_(src) {
        if (src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF &&
            static_cast<unsigned char>(src_[1]) == 0xBB && static_cast<unsigned char>(src_[2]) == 0xBF)
            i_ = 3;
    }

    XmlEvent next() {
        for (;;) {
            skip_ws();
            if (i_ >= src_.size())
                return {XmlKind::Eof, {}, {}, {}};
            if (src_[i_] != '<') {
                const auto start = i_;
                while (i_ < src_.size() && src_[i_] != '<')
                    ++i_;
                auto text = xml_unescape(src_.substr(start, i_ - start));
                std::size_t a = 0, b = text.size();
                while (a < b && import_detail::ascii_whitespace(static_cast<unsigned char>(text[a])))
                    ++a;
                while (b > a && import_detail::ascii_whitespace(static_cast<unsigned char>(text[b - 1])))
                    --b;
                text = text.substr(a, b - a);
                if (text.empty())
                    continue;
                return {XmlKind::Text, {}, {}, std::move(text)};
            }
            ++i_;
            if (i_ < src_.size() && src_[i_] == '!') {
                if (i_ + 2 < src_.size() && src_[i_ + 1] == '-' && src_[i_ + 2] == '-') {
                    i_ += 3;
                    const auto end = src_.find("-->", i_);
                    if (end == std::string_view::npos)
                        import_error("COLLADA XML parse error: unterminated comment");
                    i_ = end + 3;
                    continue;
                }
                const auto end = src_.find('>', i_);
                if (end == std::string_view::npos)
                    import_error("COLLADA XML parse error: unterminated declaration");
                i_ = end + 1;
                continue;
            }
            if (i_ < src_.size() && src_[i_] == '?') {
                const auto end = src_.find("?>", i_);
                if (end == std::string_view::npos)
                    import_error("COLLADA XML parse error: unterminated processing instruction");
                i_ = end + 2;
                continue;
            }
            const bool closing = i_ < src_.size() && src_[i_] == '/';
            if (closing)
                ++i_;
            const auto name_start = i_;
            while (i_ < src_.size() && src_[i_] != '>' && src_[i_] != '/' &&
                   !import_detail::ascii_whitespace(static_cast<unsigned char>(src_[i_])))
                ++i_;
            XmlEvent ev;
            ev.name = local_name(src_.substr(name_start, i_ - name_start));
            skip_ws();
            while (i_ < src_.size() && src_[i_] != '>' && src_[i_] != '/') {
                const auto key_start = i_;
                while (i_ < src_.size() && src_[i_] != '=' && src_[i_] != '>' && src_[i_] != '/' &&
                       !import_detail::ascii_whitespace(static_cast<unsigned char>(src_[i_])))
                    ++i_;
                std::string key(src_.substr(key_start, i_ - key_start));
                skip_ws();
                std::string value;
                if (i_ < src_.size() && src_[i_] == '=') {
                    ++i_;
                    skip_ws();
                    if (i_ < src_.size() && (src_[i_] == '"' || src_[i_] == '\'')) {
                        const char quote = src_[i_++];
                        const auto vstart = i_;
                        while (i_ < src_.size() && src_[i_] != quote)
                            ++i_;
                        value = std::string(src_.substr(vstart, i_ - vstart));
                        if (i_ < src_.size())
                            ++i_;
                    }
                }
                ev.attrs.emplace_back(std::move(key), std::move(value));
                skip_ws();
            }
            bool self_close = false;
            if (i_ < src_.size() && src_[i_] == '/') {
                self_close = true;
                ++i_;
            }
            if (i_ >= src_.size() || src_[i_] != '>')
                import_error("COLLADA XML parse error: malformed tag");
            ++i_;
            if (closing)
                ev.kind = XmlKind::End;
            else
                ev.kind = self_close ? XmlKind::Empty : XmlKind::Start;
            return ev;
        }
    }

private:
    void skip_ws() {
        while (i_ < src_.size() && import_detail::ascii_whitespace(static_cast<unsigned char>(src_[i_])))
            ++i_;
    }

    std::string_view src_;
    std::size_t i_ = 0;
};

std::optional<std::string> attr_string(const XmlEvent &e, const char *key) {
    for (const auto &a : e.attrs)
        if (a.first == key)
            return a.second;
    return std::nullopt;
}

std::string read_text_content(XmlReader &xml, std::uint32_t &depth) {
    std::string text;
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Text)
            text += ev.text;
        else if (ev.kind == XmlKind::End) {
            --depth;
            break;
        } else if (ev.kind == XmlKind::Eof)
            break;
        else if (ev.kind == XmlKind::Start)
            import_error("XML read error: nested element in text");
    }
    return text;
}

std::vector<double> parse_float_list(std::string_view text) {
    std::vector<double> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && import_detail::ascii_whitespace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i >= text.size())
            break;
        const auto start = i;
        while (i < text.size() && !import_detail::ascii_whitespace(static_cast<unsigned char>(text[i])))
            ++i;
        const std::string tok(text.substr(start, i - start));
        char *end = nullptr;
        const double v = std::strtod(tok.c_str(), &end);
        if (!tok.empty() && end == tok.c_str() + tok.size())
            out.push_back(v);
    }
    return out;
}

std::vector<std::size_t> parse_usize_list(std::string_view text) {
    std::vector<std::size_t> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && import_detail::ascii_whitespace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i >= text.size())
            break;
        const auto start = i;
        while (i < text.size() && !import_detail::ascii_whitespace(static_cast<unsigned char>(text[i])))
            ++i;
        const std::string tok(text.substr(start, i - start));
        char *end = nullptr;
        const auto v = std::strtoull(tok.c_str(), &end, 10);
        if (!tok.empty() && end == tok.c_str() + tok.size())
            out.push_back(static_cast<std::size_t>(v));
    }
    return out;
}

struct ColladaPrimitive {
    std::vector<std::size_t> indices;
    std::size_t input_count = 1;
    std::size_t vertex_offset = 0;
    std::string material_symbol;
};

struct ColladaGeometry {
    std::string id;
    std::string name;
    std::vector<std::array<double, 3>> vertices;
    std::vector<ColladaPrimitive> triangles;
};

struct ColladaMaterial {
    std::string id;
    std::string name;
    float diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
};

struct ColladaNode {
    std::string name;
    std::string geometry_url;
    double transform[16]{};
    std::unordered_map<std::string, std::string> material_bindings;
};

struct ColladaScene {
    std::vector<ColladaGeometry> geometries;
    std::unordered_map<std::string, ColladaMaterial> materials;
    std::vector<ColladaNode> nodes;
};

std::vector<double> parse_source_floats(XmlReader &xml, std::uint32_t &depth) {
    std::vector<double> floats;
    const auto entry_depth = depth;
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Start) {
            ++depth;
            if (ev.name == "float_array")
                floats = parse_float_list(read_text_content(xml, depth));
        } else if (ev.kind == XmlKind::End) {
            --depth;
            if (depth < entry_depth)
                break;
        } else if (ev.kind == XmlKind::Eof)
            break;
    }
    return floats;
}

std::string parse_vertices_element(XmlReader &xml, std::uint32_t &depth) {
    std::string source_ref;
    const auto entry_depth = depth;
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Empty || ev.kind == XmlKind::Start) {
            if (ev.kind == XmlKind::Start)
                ++depth;
            if (ev.name == "input" && attr_string(ev, "semantic") == "POSITION")
                source_ref = attr_string(ev, "source").value_or("");
        } else if (ev.kind == XmlKind::End) {
            --depth;
            if (depth < entry_depth)
                break;
        } else if (ev.kind == XmlKind::Eof)
            break;
    }
    return source_ref;
}

std::vector<std::size_t> extract_vertex_indices(const std::vector<std::size_t> &all, std::size_t input_count,
                                                std::size_t vertex_offset) {
    std::vector<std::size_t> out;
    if (input_count == 0)
        return out;
    for (std::size_t i = 0; i < all.size(); i += input_count) {
        const auto remain = all.size() - i;
        const auto n = remain < input_count ? remain : input_count;
        out.push_back(vertex_offset < n ? all[i + vertex_offset] : 0);
    }
    return out;
}

std::vector<std::size_t> polylist_to_triangles(const std::vector<std::size_t> &all, const std::vector<std::size_t> &vcounts,
                                               std::size_t input_count, std::size_t vertex_offset) {
    std::vector<std::size_t> result;
    std::size_t pos = 0;
    for (auto vc : vcounts) {
        std::vector<std::size_t> face_verts;
        face_verts.reserve(vc);
        for (std::size_t i = 0; i < vc; ++i) {
            const auto idx = pos + i * input_count + vertex_offset;
            face_verts.push_back(idx < all.size() ? all[idx] : 0);
        }
        if (face_verts.size() >= 3)
            for (std::size_t i = 1; i + 1 < face_verts.size(); ++i) {
                result.push_back(face_verts[0]);
                result.push_back(face_verts[i]);
                result.push_back(face_verts[i + 1]);
            }
        pos += vc * input_count;
    }
    return result;
}

ColladaPrimitive parse_primitives(XmlReader &xml, const std::string &prim_type, const XmlEvent &start,
                                  std::uint32_t &depth) {
    ColladaPrimitive prim;
    prim.material_symbol = attr_string(start, "material").value_or("");
    std::vector<std::pair<std::string, std::size_t>> inputs;
    std::string indices_text, vcount_text;
    const auto entry_depth = depth;
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Empty) {
            if (ev.name == "input") {
                const auto semantic = attr_string(ev, "semantic").value_or("");
                std::size_t offset = 0;
                if (auto s = attr_string(ev, "offset")) {
                    char *end = nullptr;
                    offset = static_cast<std::size_t>(std::strtoull(s->c_str(), &end, 10));
                }
                inputs.emplace_back(semantic, offset);
            }
        } else if (ev.kind == XmlKind::Start) {
            ++depth;
            if (ev.name == "input") {
                const auto semantic = attr_string(ev, "semantic").value_or("");
                std::size_t offset = 0;
                if (auto s = attr_string(ev, "offset")) {
                    char *end = nullptr;
                    offset = static_cast<std::size_t>(std::strtoull(s->c_str(), &end, 10));
                }
                inputs.emplace_back(semantic, offset);
            } else if (ev.name == "p")
                indices_text = read_text_content(xml, depth);
            else if (ev.name == "vcount")
                vcount_text = read_text_content(xml, depth);
        } else if (ev.kind == XmlKind::End) {
            --depth;
            if (depth < entry_depth)
                break;
        } else if (ev.kind == XmlKind::Eof)
            break;
    }
    prim.input_count = 1;
    if (!inputs.empty()) {
        prim.input_count = 1;
        for (const auto &in : inputs)
            prim.input_count = std::max(prim.input_count, in.second + 1);
    }
    prim.vertex_offset = 0;
    for (const auto &in : inputs)
        if (in.first == "VERTEX" || in.first == "POSITION") {
            prim.vertex_offset = in.second;
            break;
        }
    const auto all = parse_usize_list(indices_text);
    if (prim_type == "polylist" && !vcount_text.empty())
        prim.indices = polylist_to_triangles(all, parse_usize_list(vcount_text), prim.input_count, prim.vertex_offset);
    else
        prim.indices = extract_vertex_indices(all, prim.input_count, prim.vertex_offset);
    return prim;
}

ColladaGeometry parse_geometry(XmlReader &xml, const XmlEvent &start) {
    ColladaGeometry geom;
    geom.id = attr_string(start, "id").value_or("");
    geom.name = attr_string(start, "name").value_or(geom.id);
    std::unordered_map<std::string, std::vector<double>> sources;
    std::optional<std::pair<std::string, std::string>> vertices_source;
    std::uint32_t depth = 1;
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Start) {
            ++depth;
            if (ev.name == "source") {
                auto src_id = attr_string(ev, "id").value_or("");
                sources[src_id] = parse_source_floats(xml, depth);
            } else if (ev.name == "vertices") {
                auto vert_id = attr_string(ev, "id").value_or("");
                vertices_source = std::make_pair(vert_id, parse_vertices_element(xml, depth));
            } else if (ev.name == "triangles" || ev.name == "polylist" || ev.name == "polygons")
                geom.triangles.push_back(parse_primitives(xml, ev.name, ev, depth));
        } else if (ev.kind == XmlKind::End) {
            --depth;
            if (depth == 0)
                break;
        } else if (ev.kind == XmlKind::Eof)
            break;
    }
    if (vertices_source) {
        auto source_id = vertices_source->second;
        if (!source_id.empty() && source_id.front() == '#')
            source_id.erase(source_id.begin());
        if (auto it = sources.find(source_id); it != sources.end())
            for (std::size_t i = 0; i + 2 < it->second.size(); i += 3)
                geom.vertices.push_back({it->second[i], it->second[i + 1], it->second[i + 2]});
    } else {
        for (const auto &[sid, floats] : sources) {
            std::string lower = sid;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("position") != std::string::npos) {
                for (std::size_t i = 0; i + 2 < floats.size(); i += 3)
                    geom.vertices.push_back({floats[i], floats[i + 1], floats[i + 2]});
                break;
            }
        }
    }
    return geom;
}

std::array<float, 4> parse_effect(XmlReader &xml) {
    std::array<float, 4> diffuse{0.8f, 0.8f, 0.8f, 1.0f};
    std::uint32_t depth = 1;
    bool in_diffuse = false;
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Start) {
            ++depth;
            if (ev.name == "diffuse")
                in_diffuse = true;
            if (in_diffuse && ev.name == "color") {
                auto vals = parse_float_list(read_text_content(xml, depth));
                if (vals.size() >= 3) {
                    diffuse[0] = static_cast<float>(vals[0]);
                    diffuse[1] = static_cast<float>(vals[1]);
                    diffuse[2] = static_cast<float>(vals[2]);
                    if (vals.size() >= 4)
                        diffuse[3] = static_cast<float>(vals[3]);
                }
                in_diffuse = false;
            }
        } else if (ev.kind == XmlKind::End) {
            --depth;
            if (depth == 0)
                break;
            if (in_diffuse)
                in_diffuse = false;
        } else if (ev.kind == XmlKind::Eof)
            break;
    }
    return diffuse;
}

std::optional<std::string> parse_material_element(XmlReader &xml) {
    std::optional<std::string> effect_url;
    std::uint32_t depth = 1;
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Empty || ev.kind == XmlKind::Start) {
            if (ev.kind == XmlKind::Start)
                ++depth;
            if (ev.name == "instance_effect")
                effect_url = attr_string(ev, "url");
        } else if (ev.kind == XmlKind::End) {
            --depth;
            if (depth == 0)
                break;
        } else if (ev.kind == XmlKind::Eof)
            break;
    }
    return effect_url;
}

void row_to_col_major(double *out, const std::vector<double> &row) {
    out[0] = row[0];
    out[1] = row[4];
    out[2] = row[8];
    out[3] = row[12];
    out[4] = row[1];
    out[5] = row[5];
    out[6] = row[9];
    out[7] = row[13];
    out[8] = row[2];
    out[9] = row[6];
    out[10] = row[10];
    out[11] = row[14];
    out[12] = row[3];
    out[13] = row[7];
    out[14] = row[11];
    out[15] = row[15];
}

bool element_matrix(const std::string &kind, std::string_view text, double *out) {
    const auto values = parse_float_list(text);
    if (kind == "matrix" && values.size() == 16) {
        row_to_col_major(out, values);
        return true;
    }
    if (kind == "translate" && values.size() >= 3) {
        translation_matrix(out, values[0], values[1], values[2]);
        return true;
    }
    if (kind == "scale" && values.size() >= 3) {
        scale_matrix(out, values[0], values[1], values[2]);
        return true;
    }
    if (kind == "rotate" && values.size() >= 4) {
        rotation_axis_angle(out, values[0], values[1], values[2], values[3]);
        return true;
    }
    return false;
}

void parse_instance_geometry_bindings(XmlReader &xml, std::uint32_t &depth,
                                      std::unordered_map<std::string, std::string> &bindings) {
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Empty || ev.kind == XmlKind::Start) {
            if (ev.kind == XmlKind::Start)
                ++depth;
            if (ev.name == "instance_material") {
                auto symbol = attr_string(ev, "symbol").value_or("");
                auto target = attr_string(ev, "target").value_or("");
                if (!target.empty() && target.front() == '#')
                    target.erase(target.begin());
                if (!symbol.empty())
                    bindings[symbol] = target;
            }
        } else if (ev.kind == XmlKind::End) {
            --depth;
            break;
        } else if (ev.kind == XmlKind::Eof)
            break;
    }
}

std::vector<ColladaNode> parse_node(XmlReader &xml, const std::string &name, std::uint32_t &depth,
                                    const double *parent) {
    double local[16];
    copy_identity(local);
    std::string geometry_url;
    std::unordered_map<std::string, std::string> material_bindings;
    std::vector<ColladaNode> instances;
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Start) {
            ++depth;
            if (ev.name == "matrix" || ev.name == "translate" || ev.name == "rotate" || ev.name == "scale") {
                double element[16];
                if (element_matrix(ev.name, read_text_content(xml, depth), element)) {
                    double composed[16];
                    mul_matrix(composed, element, local);
                    std::memcpy(local, composed, sizeof local);
                }
            } else if (ev.name == "instance_geometry") {
                geometry_url = attr_string(ev, "url").value_or("");
                parse_instance_geometry_bindings(xml, depth, material_bindings);
            } else if (ev.name == "node") {
                auto child_name = attr_string(ev, "name").value_or("");
                double child_parent[16];
                mul_matrix(child_parent, parent, local);
                auto nested = parse_node(xml, child_name, depth, child_parent);
                instances.insert(instances.end(), nested.begin(), nested.end());
            }
        } else if (ev.kind == XmlKind::Empty) {
            if (ev.name == "instance_geometry")
                geometry_url = attr_string(ev, "url").value_or("");
        } else if (ev.kind == XmlKind::End) {
            --depth;
            break;
        } else if (ev.kind == XmlKind::Eof)
            break;
    }
    if (!geometry_url.empty()) {
        ColladaNode node;
        node.name = name;
        node.geometry_url = geometry_url;
        mul_matrix(node.transform, parent, local);
        node.material_bindings = std::move(material_bindings);
        instances.push_back(std::move(node));
    }
    return instances;
}

void parse_visual_scene(XmlReader &xml, std::vector<ColladaNode> &nodes) {
    std::uint32_t depth = 1;
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Start) {
            ++depth;
            if (ev.name == "node") {
                auto name = attr_string(ev, "name").value_or("");
                auto nested = parse_node(xml, name, depth, kIdentity);
                nodes.insert(nodes.end(), nested.begin(), nested.end());
            }
        } else if (ev.kind == XmlKind::End) {
            --depth;
            if (depth == 0)
                break;
        } else if (ev.kind == XmlKind::Eof)
            break;
    }
}

ColladaScene parse_collada(std::string_view text) {
    XmlReader xml(text);
    ColladaScene scene;
    std::unordered_map<std::string, std::array<float, 4>> effects;
    std::unordered_map<std::string, std::string> material_to_effect;
    for (;;) {
        auto ev = xml.next();
        if (ev.kind == XmlKind::Start) {
            if (ev.name == "geometry")
                scene.geometries.push_back(parse_geometry(xml, ev));
            else if (ev.name == "effect") {
                auto id = attr_string(ev, "id");
                auto color = parse_effect(xml);
                if (id)
                    effects[*id] = color;
            } else if (ev.name == "material") {
                auto id = attr_string(ev, "id");
                auto name = attr_string(ev, "name");
                if (!name)
                    name = id;
                auto effect_url = parse_material_element(xml);
                if (id && effect_url) {
                    material_to_effect[*id] = *effect_url;
                    ColladaMaterial mat;
                    mat.id = *id;
                    mat.name = name.value_or("");
                    scene.materials[*id] = mat;
                }
            } else if (ev.name == "visual_scene")
                parse_visual_scene(xml, scene.nodes);
        } else if (ev.kind == XmlKind::Eof)
            break;
    }
    for (auto &[mat_id, mat] : scene.materials) {
        auto it = material_to_effect.find(mat_id);
        if (it == material_to_effect.end())
            continue;
        auto effect_id = it->second;
        if (!effect_id.empty() && effect_id.front() == '#')
            effect_id.erase(effect_id.begin());
        if (auto e = effects.find(effect_id); e != effects.end()) {
            mat.diffuse[0] = e->second[0];
            mat.diffuse[1] = e->second[1];
            mat.diffuse[2] = e->second[2];
            mat.diffuse[3] = e->second[3];
        }
    }
    if (scene.nodes.empty()) {
        for (const auto &geom : scene.geometries) {
            ColladaNode node;
            node.name = geom.name;
            node.geometry_url = "#" + geom.id;
            copy_identity(node.transform);
            scene.nodes.push_back(std::move(node));
        }
    }
    return scene;
}

MeshDefinition build_mesh_from_indices(const std::vector<std::array<double, 3>> &positions,
                                       const std::vector<std::size_t> &vertex_indices, const double *transform,
                                       bool has_transform, double scale, bool merge, double tolerance) {
    std::vector<std::array<std::size_t, 3>> triangles;
    for (std::size_t i = 0; i + 2 < vertex_indices.size(); i += 3)
        triangles.push_back({vertex_indices[i], vertex_indices[i + 1], vertex_indices[i + 2]});
    if (has_transform) {
        auto transformed = transform_positions(positions, transform);
        return mesh_from_indexed(transformed, triangles, scale, merge, tolerance);
    }
    return mesh_from_indexed(positions, triangles, scale, merge, tolerance);
}

} // namespace

ColladaImporter::ColladaImporter(std::vector<std::uint8_t> data) : data_(std::move(data)) {}

ColladaImporter ColladaImporter::from_file(const std::filesystem::path &path) {
    return ColladaImporter(read_path_bytes(path));
}

ColladaImporter ColladaImporter::from_bytes(std::vector<std::uint8_t> data) {
    return ColladaImporter(std::move(data));
}

ColladaImporter ColladaImporter::from_bytes(std::string_view text) {
    return ColladaImporter(std::vector<std::uint8_t>(text.begin(), text.end()));
}

ColladaImporter ColladaImporter::from_reader(std::istream &input) {
    return ColladaImporter(read_stream_bytes(input, "COLLADA data"));
}

ColladaImporter &ColladaImporter::with_config(ImportConfig config) {
    config_ = std::move(config);
    return *this;
}

DxfDocument ColladaImporter::import() const {
    const auto scene = parse_collada(std::string_view(reinterpret_cast<const char *>(data_.data()), data_.size()));
    auto doc = DxfDocument::create();
    const double scale = config_.scale_factor;
    std::unordered_map<std::string, const ColladaGeometry *> geom_map;
    for (const auto &g : scene.geometries)
        geom_map[g.id] = &g;
    for (const auto &node : scene.nodes) {
        auto geom_id = node.geometry_url;
        if (!geom_id.empty() && geom_id.front() == '#')
            geom_id.erase(geom_id.begin());
        auto git = geom_map.find(geom_id);
        if (git == geom_map.end())
            continue;
        const auto *geom = git->second;
        const bool has_transform = !is_identity(node.transform);
        std::vector<std::pair<std::string, std::vector<std::size_t>>> groups;
        std::unordered_map<std::string, std::size_t> group_index;
        for (const auto &prim : geom->triangles) {
            auto bit = node.material_bindings.find(prim.material_symbol);
            std::string mat_id = bit == node.material_bindings.end() ? std::string() : bit->second;
            auto found = group_index.find(mat_id);
            if (found == group_index.end()) {
                group_index[mat_id] = groups.size();
                groups.push_back({mat_id, prim.indices});
            } else
                groups[found->second].second.insert(groups[found->second].second.end(), prim.indices.begin(),
                                                    prim.indices.end());
        }
        for (const auto &[mat_id, vertex_indices] : groups) {
            float diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
            std::string mat_name;
            if (auto mit = scene.materials.find(mat_id); mit != scene.materials.end()) {
                diffuse[0] = mit->second.diffuse[0];
                diffuse[1] = mit->second.diffuse[1];
                diffuse[2] = mit->second.diffuse[2];
                diffuse[3] = mit->second.diffuse[3];
                mat_name = mit->second.name;
            }
            auto mesh = build_mesh_from_indices(geom->vertices, vertex_indices, node.transform, has_transform, scale,
                                                config_.merge_vertices, config_.merge_tolerance);
            if (mesh.faces.empty())
                continue;
            const auto layer_color = color_from_unit_rgb(diffuse[0], diffuse[1], diffuse[2]);
            std::string layer_name;
            Color entity_color = Color::by_layer();
            if (mat_name.empty()) {
                layer_name = create_material_layer(doc, config_, node.name.empty() ? geom->name : node.name,
                                                   config_.default_color);
            } else {
                layer_name = create_material_layer(doc, config_, mat_name, layer_color);
                entity_color = layer_color;
            }
            add_mesh_entity(doc, std::move(mesh), layer_name, entity_color);
        }
    }
    return doc;
}

} // namespace libopencad
