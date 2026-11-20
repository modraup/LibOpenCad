// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/io/import/obj.rs.
#include "import_internal.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace {
std::string join_tokens(std::istream &parts) {
    std::string out, token;
    while (parts >> token) {
        if (!out.empty())
            out.push_back(' ');
        out += token;
    }
    return out;
}
} // namespace

namespace libopencad {
namespace {

using import_detail::add_mesh_entity;
using import_detail::color_from_unit_rgb;
using import_detail::create_material_layer;
using import_detail::import_error;
using import_detail::mesh_from_indexed;
using import_detail::read_path_bytes;
using import_detail::read_stream_bytes;
using import_detail::resolve_relative;

struct ObjMaterial {
    std::string name;
    float diffuse[3] = {0.8f, 0.8f, 0.8f};
};

struct FaceGroup {
    std::string material;
    std::vector<std::vector<std::size_t>> faces;
};

std::optional<double> parse_f64_ok(std::string_view token) {
    const std::string text(token);
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size())
        return std::nullopt;
    return value;
}

std::optional<float> parse_f32_ok(std::string_view token) {
    auto v = parse_f64_ok(token);
    if (!v)
        return std::nullopt;
    return static_cast<float>(*v);
}

std::optional<std::int64_t> parse_i64_ok(std::string_view token) {
    const std::string text(token);
    char *end = nullptr;
    const auto value = std::strtoll(text.c_str(), &end, 10);
    if (text.empty() || end != text.c_str() + text.size())
        return std::nullopt;
    return value;
}

void parse_obj(std::string_view text, std::vector<std::array<double, 3>> &vertices, std::vector<FaceGroup> &groups,
               std::vector<std::string> &mtl_files) {
    groups.push_back(FaceGroup{});
    std::string current_material;
    std::istringstream in{std::string(text)};
    for (std::string raw; std::getline(in, raw);) {
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();
        auto line = raw;
        while (!line.empty() && import_detail::ascii_whitespace(static_cast<unsigned char>(line.front())))
            line.erase(line.begin());
        while (!line.empty() && import_detail::ascii_whitespace(static_cast<unsigned char>(line.back())))
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        std::istringstream parts(line);
        std::string keyword;
        if (!(parts >> keyword))
            continue;
        if (keyword == "v") {
            std::vector<double> coords;
            for (std::string token; parts >> token;)
                if (auto v = parse_f64_ok(token))
                    coords.push_back(*v);
            if (coords.size() >= 3)
                vertices.push_back({coords[0], coords[1], coords[2]});
        } else if (keyword == "f") {
            std::vector<std::size_t> face_indices;
            for (std::string token; parts >> token;) {
                const auto slash = token.find('/');
                const auto idx_str = slash == std::string::npos ? token : token.substr(0, slash);
                auto idx = parse_i64_ok(idx_str);
                if (!idx)
                    continue;
                if (*idx > 0)
                    face_indices.push_back(static_cast<std::size_t>(*idx - 1));
                else if (*idx < 0) {
                    const auto abs = static_cast<std::size_t>(-*idx);
                    if (abs <= vertices.size())
                        face_indices.push_back(vertices.size() - abs);
                }
            }
            if (face_indices.size() >= 3 && !groups.empty())
                groups.back().faces.push_back(std::move(face_indices));
        } else if (keyword == "usemtl") {
            current_material = join_tokens(parts);
            groups.push_back(FaceGroup{current_material, {}});
        } else if (keyword == "mtllib") {
            auto mtl = join_tokens(parts);
            if (!mtl.empty())
                mtl_files.push_back(mtl);
        }
    }
    groups.erase(std::remove_if(groups.begin(), groups.end(), [](const FaceGroup &g) { return g.faces.empty(); }),
                 groups.end());
    if (groups.empty() && !vertices.empty())
        import_error("OBJ file contains vertices but no faces");
}

void parse_mtl(std::string_view text, std::unordered_map<std::string, ObjMaterial> &materials) {
    std::optional<ObjMaterial> current;
    std::istringstream in{std::string(text)};
    for (std::string raw; std::getline(in, raw);) {
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();
        auto line = raw;
        while (!line.empty() && import_detail::ascii_whitespace(static_cast<unsigned char>(line.front())))
            line.erase(line.begin());
        while (!line.empty() && import_detail::ascii_whitespace(static_cast<unsigned char>(line.back())))
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        std::istringstream parts(line);
        std::string keyword;
        if (!(parts >> keyword))
            continue;
        if (keyword == "newmtl") {
            if (current)
                materials[current->name] = *current;
            current = ObjMaterial{join_tokens(parts), {0.8f, 0.8f, 0.8f}};
        } else if (keyword == "Kd" && current) {
            std::vector<float> vals;
            for (std::string token; parts >> token;)
                if (auto v = parse_f32_ok(token))
                    vals.push_back(*v);
            if (vals.size() >= 3) {
                current->diffuse[0] = vals[0];
                current->diffuse[1] = vals[1];
                current->diffuse[2] = vals[2];
            }
        }
    }
    if (current)
        materials[current->name] = *current;
}

MeshDefinition build_mesh_from_faces(const std::vector<std::array<double, 3>> &vertices,
                                     const std::vector<std::vector<std::size_t>> &faces, double scale, bool merge,
                                     double tolerance) {
    std::vector<std::array<std::size_t, 3>> triangles;
    for (const auto &face : faces) {
        if (face.size() == 3)
            triangles.push_back({face[0], face[1], face[2]});
        else if (face.size() > 3)
            for (std::size_t i = 1; i + 1 < face.size(); ++i)
                triangles.push_back({face[0], face[i], face[i + 1]});
    }
    return mesh_from_indexed(vertices, triangles, scale, merge, merge ? tolerance : 0.0);
}

} // namespace

ObjImporter::ObjImporter(std::vector<std::uint8_t> data, std::optional<std::filesystem::path> base_dir)
    : data_(std::move(data)), base_dir_(std::move(base_dir)) {}

ObjImporter ObjImporter::from_file(const std::filesystem::path &path) {
    return ObjImporter(read_path_bytes(path), path.parent_path());
}

ObjImporter ObjImporter::from_bytes(std::vector<std::uint8_t> data) {
    return ObjImporter(std::move(data), std::nullopt);
}

ObjImporter ObjImporter::from_bytes(std::string_view text) {
    return ObjImporter(std::vector<std::uint8_t>(text.begin(), text.end()), std::nullopt);
}

ObjImporter ObjImporter::from_reader(std::istream &input) {
    return ObjImporter(read_stream_bytes(input, "OBJ data"), std::nullopt);
}

ObjImporter &ObjImporter::with_config(ImportConfig config) {
    config_ = std::move(config);
    return *this;
}

DxfDocument ObjImporter::import() const {
    const std::string text(data_.begin(), data_.end());
    std::vector<std::array<double, 3>> vertices;
    std::vector<FaceGroup> groups;
    std::vector<std::string> mtl_files;
    parse_obj(text, vertices, groups, mtl_files);

    std::unordered_map<std::string, ObjMaterial> materials;
    if (base_dir_) {
        for (const auto &mtl_file : mtl_files) {
            const auto mtl_path = resolve_relative(*base_dir_, mtl_file);
            std::ifstream in(mtl_path);
            if (!in)
                continue;
            std::ostringstream body;
            body << in.rdbuf();
            parse_mtl(body.str(), materials);
        }
    }

    auto doc = DxfDocument::create();
    const double scale = config_.scale_factor;
    for (const auto &group : groups) {
        if (group.faces.empty())
            continue;
        std::string layer_name;
        Color color = config_.default_color;
        if (auto it = materials.find(group.material); it != materials.end()) {
            color = color_from_unit_rgb(it->second.diffuse[0], it->second.diffuse[1], it->second.diffuse[2]);
            layer_name = create_material_layer(doc, config_, it->second.name, color);
        } else {
            const auto name = group.material.empty() ? "default" : group.material;
            layer_name = create_material_layer(doc, config_, name, config_.default_color);
        }
        auto mesh =
            build_mesh_from_faces(vertices, group.faces, scale, config_.merge_vertices, config_.merge_tolerance);
        if (mesh.faces.empty())
            continue;
        add_mesh_entity(doc, std::move(mesh), layer_name, color);
    }
    return doc;
}

} // namespace libopencad
