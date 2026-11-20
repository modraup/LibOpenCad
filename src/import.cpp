// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/io/import/{mod,stl}.rs.
#include "import_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>

namespace libopencad {
namespace {

using import_detail::ascii_whitespace;
using import_detail::ensure_material_layer;
using import_detail::import_error;
using import_detail::material_layer_name;
using import_detail::read_f32_le;
using import_detail::read_path_bytes;
using import_detail::read_stream_bytes;
using import_detail::read_u32_le;
using import_detail::TriangleAssembler;

bool equals_ignore_ascii(const std::uint8_t *data, std::size_t n, const char *literal) {
    const auto length = std::strlen(literal);
    if (n < length)
        return false;
    for (std::size_t i = 0; i < length; ++i)
        if (std::tolower(data[i]) != std::tolower(static_cast<unsigned char>(literal[i])))
            return false;
    return true;
}

struct StlTriangle {
    double normal[3]{};
    double vertices[3][3]{};
    std::optional<std::array<std::uint8_t, 3>> color;
};

struct StlMesh {
    std::string name;
    std::vector<StlTriangle> triangles;
};

std::string trim_copy(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && ascii_whitespace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && ascii_whitespace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return std::string(s.substr(a, b - a));
}

double parse_f64(std::string_view token, const std::string &context) {
    const std::string text(token);
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size())
        import_error("Invalid float '" + text + "' in line: '" + context + "'");
    return value;
}

std::vector<std::string> split_ws(const std::string &line) {
    std::vector<std::string> parts;
    std::istringstream in(line);
    std::string part;
    while (in >> part)
        parts.push_back(part);
    return parts;
}

std::array<double, 3> parse_normal(const std::string &line) {
    const auto parts = split_ws(line);
    if (parts.size() < 5)
        import_error("Invalid facet normal line: '" + line + "'");
    return {parse_f64(parts[2], line), parse_f64(parts[3], line), parse_f64(parts[4], line)};
}

std::array<double, 3> parse_vertex(const std::string &line) {
    const auto parts = split_ws(line);
    if (parts.size() < 4)
        import_error("Invalid vertex line: '" + line + "'");
    if (parts[0].size() != 6 ||
        !equals_ignore_ascii(reinterpret_cast<const std::uint8_t *>(parts[0].data()), 6, "vertex"))
        import_error("Expected 'vertex', got: '" + parts[0] + "'");
    return {parse_f64(parts[1], line), parse_f64(parts[2], line), parse_f64(parts[3], line)};
}

StlMesh parse_ascii_stl(const std::vector<std::uint8_t> &data) {
    std::string text(data.begin(), data.end());
    std::istringstream in(text);
    std::string first;
    if (!std::getline(in, first))
        import_error("Empty STL file");
    if (!first.empty() && first.back() == '\r')
        first.pop_back();
    auto name = trim_copy(first);
    if (name.size() >= 5 && name.compare(0, 5, "solid") == 0)
        name = trim_copy(name.substr(5));
    else
        name.clear();
    if (name.empty())
        name = "stl_import";

    std::vector<std::string> remaining;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto trimmed = trim_copy(line);
        if (!trimmed.empty())
            remaining.push_back(std::move(trimmed));
    }

    StlMesh mesh;
    mesh.name = std::move(name);
    for (std::size_t i = 0; i < remaining.size(); ++i) {
        std::string lower = remaining[i];
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.compare(0, 8, "endsolid") == 0)
            break;
        if (lower.compare(0, 12, "facet normal") != 0)
            continue;
        StlTriangle tri;
        const auto normal = parse_normal(remaining[i]);
        tri.normal[0] = normal[0];
        tri.normal[1] = normal[1];
        tri.normal[2] = normal[2];
        ++i;
        if (i >= remaining.size())
            import_error("Expected 'outer loop' after 'facet normal'");
        std::string loop = remaining[i];
        std::transform(loop.begin(), loop.end(), loop.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (loop.compare(0, 10, "outer loop") != 0)
            import_error("Expected 'outer loop' after 'facet normal'");
        for (int v = 0; v < 3; ++v) {
            ++i;
            if (i >= remaining.size())
                import_error("Unexpected end of file while reading vertices");
            const auto vertex = parse_vertex(remaining[i]);
            tri.vertices[v][0] = vertex[0];
            tri.vertices[v][1] = vertex[1];
            tri.vertices[v][2] = vertex[2];
        }
        ++i;
        if (i >= remaining.size())
            import_error("Expected 'endloop'");
        std::string endloop = remaining[i];
        std::transform(endloop.begin(), endloop.end(), endloop.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (endloop.compare(0, 7, "endloop") != 0)
            import_error("Expected 'endloop'");
        ++i;
        if (i >= remaining.size())
            import_error("Expected 'endfacet'");
        std::string endfacet = remaining[i];
        std::transform(endfacet.begin(), endfacet.end(), endfacet.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (endfacet.compare(0, 8, "endfacet") != 0)
            import_error("Expected 'endfacet'");
        mesh.triangles.push_back(tri);
    }
    return mesh;
}

std::string extract_header_name(const std::uint8_t header[80]) {
    std::size_t end = 0;
    while (end < 80 && header[end] != 0 && header[end] >= 0x20)
        ++end;
    auto name = trim_copy(std::string(reinterpret_cast<const char *>(header),
                                      reinterpret_cast<const char *>(header) + end));
    return name.empty() ? "stl_import" : name;
}

StlMesh parse_binary_stl(const std::vector<std::uint8_t> &data) {
    if (data.size() < 84)
        import_error("Failed to read STL header: unexpected end of file");
    const auto count = static_cast<std::size_t>(read_u32_le(data.data() + 80));
    if (count > (std::numeric_limits<std::size_t>::max() - 84) / 50)
        import_error("Binary STL file too small: triangle count overflows");
    const auto expected = 84ull + 50ull * static_cast<unsigned long long>(count);
    if (static_cast<unsigned long long>(data.size()) < expected)
        import_error("Binary STL file too small: expected at least " + std::to_string(expected) +
                     " bytes for " + std::to_string(count) + " triangles, got " +
                     std::to_string(data.size()));
    StlMesh mesh;
    mesh.name = extract_header_name(data.data());
    mesh.triangles.reserve(count);
    auto cursor = data.data() + 84;
    for (std::size_t i = 0; i < count; ++i, cursor += 50) {
        StlTriangle tri;
        tri.normal[0] = read_f32_le(cursor);
        tri.normal[1] = read_f32_le(cursor + 4);
        tri.normal[2] = read_f32_le(cursor + 8);
        for (int v = 0; v < 3; ++v)
            for (int c = 0; c < 3; ++c)
                tri.vertices[v][c] = read_f32_le(cursor + 12 + (v * 3 + c) * 4);
        const auto attribute = static_cast<std::uint16_t>(cursor[48] | (cursor[49] << 8));
        if (attribute & 0x8000) {
            const auto b = static_cast<std::uint8_t>(static_cast<float>(attribute & 0x001F) / 31.0f * 255.0f);
            const auto g =
                static_cast<std::uint8_t>(static_cast<float>((attribute >> 5) & 0x001F) / 31.0f * 255.0f);
            const auto r =
                static_cast<std::uint8_t>(static_cast<float>((attribute >> 10) & 0x001F) / 31.0f * 255.0f);
            tri.color = std::array<std::uint8_t, 3>{r, g, b};
        }
        mesh.triangles.push_back(tri);
    }
    return mesh;
}

bool is_ascii_stl(const std::vector<std::uint8_t> &data) {
    const bool starts_solid = data.size() >= 5 && equals_ignore_ascii(data.data(), data.size(), "solid") &&
                              (data.size() == 5 || ascii_whitespace(data[5]));
    if (data.size() >= 84) {
        const auto count = static_cast<unsigned long long>(read_u32_le(data.data() + 80));
        const auto expected = 84ull + count * 50ull;
        if (static_cast<unsigned long long>(data.size()) == expected)
            return false;
    }
    if (!starts_solid)
        return false;
    if (data.size() < 84)
        return true;
    const auto sample = data.size() < 65536 ? data.size() : 65536;
    for (std::size_t i = 0; i + 5 <= sample; ++i)
        if (equals_ignore_ascii(data.data() + i, 5, "facet"))
            return true;
    return false;
}

StlMesh parse_stl(const std::vector<std::uint8_t> &data) {
    if (is_ascii_stl(data))
        return parse_ascii_stl(data);
    return parse_binary_stl(data);
}

MeshDefinition build_mesh(const StlMesh &stl, const std::vector<std::size_t> &indices, double scale, bool merge,
                          double tolerance) {
    TriangleAssembler assembler(merge, tolerance, indices.size());
    for (auto ti : indices) {
        const auto &tri = stl.triangles[ti];
        assembler.push_triangle({tri.vertices[0][0] * scale, tri.vertices[0][1] * scale, tri.vertices[0][2] * scale},
                               {tri.vertices[1][0] * scale, tri.vertices[1][1] * scale, tri.vertices[1][2] * scale},
                               {tri.vertices[2][0] * scale, tri.vertices[2][1] * scale, tri.vertices[2][2] * scale});
    }
    return assembler.finish();
}

std::map<std::array<std::uint8_t, 3>, std::vector<std::size_t>> group_by_color(const std::vector<StlTriangle> &triangles) {
    std::map<std::array<std::uint8_t, 3>, std::vector<std::size_t>> grouped;
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        const auto key = triangles[i].color.value_or(std::array<std::uint8_t, 3>{255, 255, 255});
        grouped[key].push_back(i);
    }
    return grouped;
}

void add_stl_mesh(DxfDocument &doc, MeshDefinition mesh, const std::string &layer, std::optional<Color> color) {
    EntityOptions options;
    options.layer = layer;
    if (color)
        options.color = *color;
    doc.add_mesh(mesh, options);
}

DxfDocument build_document(const StlMesh &stl, const ImportConfig &config) {
    auto doc = DxfDocument::create();
    if (stl.triangles.empty())
        return doc;
    const double scale = config.scale_factor;
    const bool has_colors =
        std::any_of(stl.triangles.begin(), stl.triangles.end(), [](const auto &t) { return t.color.has_value(); });
    if (has_colors) {
        for (const auto &[rgb, indices] : group_by_color(stl.triangles)) {
            const auto color = Color::from_rgb(rgb[0], rgb[1], rgb[2]);
            const auto layer = material_layer_name(
                config, stl.name + "_" + std::to_string(rgb[0]) + "_" + std::to_string(rgb[1]) + "_" +
                            std::to_string(rgb[2]));
            ensure_material_layer(doc, layer, color);
            auto mesh = build_mesh(stl, indices, scale, config.merge_vertices, config.merge_tolerance);
            if (mesh.faces.empty())
                continue;
            add_stl_mesh(doc, std::move(mesh), layer, color);
        }
    } else {
        const auto layer = material_layer_name(config, stl.name);
        ensure_material_layer(doc, layer, config.default_color);
        std::vector<std::size_t> all(stl.triangles.size());
        for (std::size_t i = 0; i < all.size(); ++i)
            all[i] = i;
        auto mesh = build_mesh(stl, all, scale, config.merge_vertices, config.merge_tolerance);
        if (mesh.faces.empty())
            return doc;
        add_stl_mesh(doc, std::move(mesh), layer, std::nullopt);
    }
    return doc;
}

} // namespace

ImportFormat detect_format(const std::filesystem::path &path) {
    auto ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == "stl")
        return ImportFormat::Stl;
    if (ext == "dae")
        return ImportFormat::Collada;
    if (ext == "obj")
        return ImportFormat::Obj;
    if (ext == "gltf")
        return ImportFormat::Gltf;
    if (ext == "glb")
        return ImportFormat::Glb;
    if (ext == "fbx")
        return ImportFormat::Fbx;
    import_error("Unsupported import format: ." + ext);
}

DxfDocument import_file(const std::filesystem::path &path, const ImportConfig &config) {
    switch (detect_format(path)) {
    case ImportFormat::Stl:
        return StlImporter::from_file(path).with_config(config).import();
    case ImportFormat::Collada:
        return ColladaImporter::from_file(path).with_config(config).import();
    case ImportFormat::Obj:
        return ObjImporter::from_file(path).with_config(config).import();
    case ImportFormat::Gltf:
    case ImportFormat::Glb:
        return GltfImporter::from_file(path).with_config(config).import();
    case ImportFormat::Fbx:
        return FbxImporter::from_file(path).with_config(config).import();
    }
    import_error("Unsupported import format");
}

StlImporter::StlImporter(std::vector<std::uint8_t> data) : data_(std::move(data)) {}

StlImporter StlImporter::from_file(const std::filesystem::path &path) {
    return StlImporter(read_path_bytes(path));
}

StlImporter StlImporter::from_bytes(std::vector<std::uint8_t> data) { return StlImporter(std::move(data)); }

StlImporter StlImporter::from_bytes(std::string_view text) {
    return StlImporter(std::vector<std::uint8_t>(text.begin(), text.end()));
}

StlImporter StlImporter::from_reader(std::istream &input) {
    return StlImporter(read_stream_bytes(input, "STL data"));
}

StlImporter &StlImporter::with_config(ImportConfig config) {
    config_ = std::move(config);
    return *this;
}

DxfDocument StlImporter::import() const { return build_document(parse_stl(data_), config_); }

} // namespace libopencad
