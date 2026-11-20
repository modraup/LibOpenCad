// SPDX-License-Identifier: MPL-2.0
// Shared import helpers ported from acadrust src/io/import/{mod,mesh,color_mapping}.rs.
#pragma once

#include <libopencad/import.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace libopencad {
namespace import_detail {

[[noreturn]] inline void import_error(const std::string &message) {
    throw std::runtime_error(message);
}

inline bool ascii_whitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline std::vector<std::uint8_t> read_path_bytes(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        import_error("Cannot open '" + path.string() + "'");
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0)
        import_error("Cannot read '" + path.string() + "'");
    in.seekg(0);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (size && !in.read(reinterpret_cast<char *>(data.data()), size))
        import_error("Cannot read '" + path.string() + "'");
    return data;
}

inline std::vector<std::uint8_t> read_stream_bytes(std::istream &input, const std::string &label) {
    std::vector<std::uint8_t> data;
    char buffer[4096];
    while (input) {
        input.read(buffer, sizeof buffer);
        const auto n = input.gcount();
        if (n > 0)
            data.insert(data.end(), reinterpret_cast<std::uint8_t *>(buffer),
                        reinterpret_cast<std::uint8_t *>(buffer) + n);
    }
    if (input.bad())
        import_error("Cannot read " + label);
    return data;
}

inline std::string sanitize_layer_name(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        switch (c) {
        case '<':
        case '>':
        case '/':
        case '\\':
        case '"':
        case ':':
        case ';':
        case '?':
        case '*':
        case '|':
        case ',':
        case '=':
        case '`':
            out.push_back('_');
            break;
        default:
            if (c < 0x20)
                out.push_back('_');
            else
                out.push_back(static_cast<char>(c));
        }
    }
    if (out.empty())
        out = "unnamed";
    return out;
}

inline std::string material_layer_name(const ImportConfig &config, std::string_view material) {
    auto sanitized = sanitize_layer_name(material);
    if (config.layer_prefix.empty())
        return sanitized;
    return config.layer_prefix + "_" + sanitized;
}

inline int layer_aci(Color color) {
    switch (color.kind()) {
    case Color::Kind::ByBlock:
        return 0;
    case Color::Kind::Index: {
        const auto index = color.index().value_or(7);
        return index == 0 ? 7 : index;
    }
    case Color::Kind::Rgb:
        return color.approximate_index();
    case Color::Kind::ByLayer:
    case Color::Kind::None:
        return 7;
    }
    return 7;
}

inline void ensure_material_layer(DxfDocument &doc, const std::string &name, Color color) {
    if (doc.get_layer(name))
        return;
    const int aci = layer_aci(color);
    doc.add_layer(name, aci == 0 ? 7 : aci);
    if (auto rgb = color.to_true_color_value())
        doc.set_symbol_properties(SymbolTable::Layer, name, {{420, std::to_string(*rgb)}});
}

inline std::string create_material_layer(DxfDocument &doc, const ImportConfig &config,
                                         std::string_view material, Color color) {
    auto name = material_layer_name(config, material);
    ensure_material_layer(doc, name, color);
    return name;
}

inline std::uint8_t unit_channel_to_u8(float value) {
    if (!std::isfinite(value))
        return 0;
    value = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::round(value * 255.0f));
}

inline Color color_from_unit_rgb(float r, float g, float b) {
    return Color::from_rgb(unit_channel_to_u8(r), unit_channel_to_u8(g), unit_channel_to_u8(b));
}

inline void add_mesh_entity(DxfDocument &doc, MeshDefinition mesh, const std::string &layer,
                            Color color) {
    EntityOptions options;
    options.layer = layer;
    options.color = color;
    doc.add_mesh(mesh, options);
}

inline std::filesystem::path resolve_relative(const std::filesystem::path &base_dir,
                                              std::string_view reference) {
    if (reference.empty() || reference.find('\0') != std::string_view::npos)
        import_error("Import reference is empty");
    const std::filesystem::path relative{std::string(reference)};
    if (relative.is_absolute())
        import_error("Import reference '" + std::string(reference) + "' escapes the source directory");
    for (const auto &part : relative) {
        if (part == "..")
            import_error("Import reference '" + std::string(reference) +
                         "' escapes the source directory");
    }
    return base_dir / relative;
}

inline std::string percent_decode(std::string_view input) {
    std::string decoded;
    decoded.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        if (input[i] == '%') {
            if (i + 2 >= input.size())
                import_error("Invalid percent-encoding in '" + std::string(input) + "'");
            auto hex = [](unsigned char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            const int high = hex(static_cast<unsigned char>(input[i + 1]));
            const int low = hex(static_cast<unsigned char>(input[i + 2]));
            if (high < 0 || low < 0)
                import_error("Invalid percent-encoding in '" + std::string(input) + "'");
            decoded.push_back(static_cast<char>((high << 4) | low));
            i += 3;
        } else {
            decoded.push_back(input[i]);
            ++i;
        }
    }
    return decoded;
}

constexpr double kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

inline void copy_identity(double *out) {
    std::memcpy(out, kIdentity, sizeof kIdentity);
}

inline bool is_identity(const double *matrix) {
    for (int i = 0; i < 16; ++i)
        if (std::fabs(matrix[i] - kIdentity[i]) >= 1e-15)
            return false;
    return true;
}

inline void mul_matrix(double *out, const double *a, const double *b) {
    double tmp[16];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            tmp[col * 4 + row] = a[row] * b[col * 4] + a[4 + row] * b[col * 4 + 1] +
                                 a[8 + row] * b[col * 4 + 2] + a[12 + row] * b[col * 4 + 3];
    std::memcpy(out, tmp, sizeof tmp);
}

inline Vector3 transform_point(const double *matrix, double x, double y, double z) {
    return {matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12],
            matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13],
            matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14]};
}

inline std::vector<std::array<double, 3>> transform_positions(const std::vector<std::array<double, 3>> &positions,
                                                              const double *matrix) {
    std::vector<std::array<double, 3>> out;
    out.reserve(positions.size());
    for (const auto &p : positions) {
        auto q = transform_point(matrix, p[0], p[1], p[2]);
        out.push_back({q.x, q.y, q.z});
    }
    return out;
}

inline void translation_matrix(double *out, double x, double y, double z) {
    copy_identity(out);
    out[12] = x;
    out[13] = y;
    out[14] = z;
}

inline void scale_matrix(double *out, double x, double y, double z) {
    copy_identity(out);
    out[0] = x;
    out[5] = y;
    out[10] = z;
}

inline void rotation_axis_angle(double *out, double ax, double ay, double az, double degrees) {
    const double len = std::sqrt(ax * ax + ay * ay + az * az);
    if (!std::isfinite(len) || len == 0.0) {
        copy_identity(out);
        return;
    }
    const double x = ax / len, y = ay / len, z = az / len;
    const double rad = degrees * (3.14159265358979323846 / 180.0);
    const double s = std::sin(rad), c = std::cos(rad), t = 1.0 - c;
    out[0] = t * x * x + c;
    out[1] = t * x * y + s * z;
    out[2] = t * x * z - s * y;
    out[3] = 0;
    out[4] = t * x * y - s * z;
    out[5] = t * y * y + c;
    out[6] = t * y * z + s * x;
    out[7] = 0;
    out[8] = t * x * z + s * y;
    out[9] = t * y * z - s * x;
    out[10] = t * z * z + c;
    out[11] = 0;
    out[12] = 0;
    out[13] = 0;
    out[14] = 0;
    out[15] = 1;
}

inline void trs_euler_xyz(double *out, const double translation[3], const double rotation_deg[3],
                          const double scale[3]) {
    double scaled[16], rx[16], ry[16], rz[16], tmp[16], rotated[16];
    scale_matrix(scaled, scale[0], scale[1], scale[2]);
    rotation_axis_angle(rx, 1, 0, 0, rotation_deg[0]);
    rotation_axis_angle(ry, 0, 1, 0, rotation_deg[1]);
    rotation_axis_angle(rz, 0, 0, 1, rotation_deg[2]);
    mul_matrix(tmp, rx, scaled);
    mul_matrix(rotated, ry, tmp);
    mul_matrix(tmp, rz, rotated);
    translation_matrix(rotated, translation[0], translation[1], translation[2]);
    mul_matrix(out, rotated, tmp);
}

inline void matrix_from_trs(double *out, const double translation[3], const double quaternion[4],
                            const double scale[3]) {
    const double x = quaternion[0], y = quaternion[1], z = quaternion[2], w = quaternion[3];
    const double x2 = x + x, y2 = y + y, z2 = z + z;
    const double xx = x * x2, xy = x * y2, xz = x * z2, yy = y * y2, yz = y * z2, zz = z * z2;
    const double wx = w * x2, wy = w * y2, wz = w * z2;
    out[0] = (1.0 - (yy + zz)) * scale[0];
    out[1] = (xy + wz) * scale[0];
    out[2] = (xz - wy) * scale[0];
    out[3] = 0;
    out[4] = (xy - wz) * scale[1];
    out[5] = (1.0 - (xx + zz)) * scale[1];
    out[6] = (yz + wx) * scale[1];
    out[7] = 0;
    out[8] = (xz + wy) * scale[2];
    out[9] = (yz - wx) * scale[2];
    out[10] = (1.0 - (xx + yy)) * scale[2];
    out[11] = 0;
    out[12] = translation[0];
    out[13] = translation[1];
    out[14] = translation[2];
    out[15] = 1;
}

struct CellKey {
    std::int64_t x = 0, y = 0, z = 0;
    bool operator<(const CellKey &o) const {
        return x < o.x || (x == o.x && (y < o.y || (y == o.y && z < o.z)));
    }
};

inline std::int64_t quantize_axis(double value, double inv_tol) {
    const double scaled = std::round(value * inv_tol);
    if (!std::isfinite(scaled))
        return 0;
    if (scaled >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        return std::numeric_limits<std::int64_t>::max();
    if (scaled <= static_cast<double>(std::numeric_limits<std::int64_t>::min()))
        return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(scaled);
}

inline CellKey quantize(Vector3 p, double inv_tol) {
    return {quantize_axis(p.x, inv_tol), quantize_axis(p.y, inv_tol), quantize_axis(p.z, inv_tol)};
}

inline bool finite_point(Vector3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

class TriangleAssembler {
public:
    TriangleAssembler(bool merge, double tolerance, std::size_t triangles) : merge_(merge) {
        tolerance_ = std::isfinite(tolerance) ? tolerance : 0.0;
        inv_tol_ = tolerance_ > 0.0 ? 1.0 / tolerance_ : 1e9;
        tol2_ = tolerance_ * tolerance_;
        faces_.reserve(triangles);
        vertices_.reserve(merge_ ? triangles : triangles * 3);
    }

    void push_triangle(Vector3 a, Vector3 b, Vector3 c) {
        if (!finite_point(a) || !finite_point(b) || !finite_point(c))
            return;
        if (merge_) {
            const auto i0 = weld(a);
            const auto i1 = weld(b);
            const auto i2 = weld(c);
            if (i0 != i1 && i1 != i2 && i0 != i2) {
                MeshFace face;
                face.vertices = {i0, i1, i2};
                faces_.push_back(std::move(face));
            }
        } else {
            const auto base = vertices_.size();
            vertices_.push_back(a);
            vertices_.push_back(b);
            vertices_.push_back(c);
            MeshFace face;
            face.vertices = {base, base + 1, base + 2};
            faces_.push_back(std::move(face));
        }
    }

    MeshDefinition finish() {
        MeshDefinition mesh;
        mesh.vertices = std::move(vertices_);
        mesh.faces = std::move(faces_);
        mesh.compute_edges();
        return mesh;
    }

private:
    std::size_t weld(Vector3 position) {
        const auto key = quantize(position, inv_tol_);
        if (tolerance_ > 0.0) {
            std::optional<std::pair<std::size_t, double>> best;
            auto sat = [](std::int64_t v, std::int64_t d) {
                if (d > 0 && v > std::numeric_limits<std::int64_t>::max() - d)
                    return std::numeric_limits<std::int64_t>::max();
                if (d < 0 && v < std::numeric_limits<std::int64_t>::min() - d)
                    return std::numeric_limits<std::int64_t>::min();
                return v + d;
            };
            for (std::int64_t dz = -1; dz <= 1; ++dz)
                for (std::int64_t dy = -1; dy <= 1; ++dy)
                    for (std::int64_t dx = -1; dx <= 1; ++dx) {
                        const CellKey neighbor{sat(key.x, dx), sat(key.y, dy), sat(key.z, dz)};
                        auto it = cells_.find(neighbor);
                        if (it == cells_.end())
                            continue;
                        for (auto index : it->second) {
                            const double distance = (vertices_[index] - position).length_squared();
                            if (distance <= tol2_ && (!best || distance < best->second))
                                best = std::pair<std::size_t, double>{index, distance};
                        }
                    }
            if (best)
                return best->first;
        } else if (auto it = cells_.find(key); it != cells_.end() && !it->second.empty()) {
            return it->second.front();
        }
        const auto index = vertices_.size();
        vertices_.push_back(position);
        cells_[key].push_back(index);
        return index;
    }

    bool merge_ = true;
    double tolerance_ = 0, inv_tol_ = 1e9, tol2_ = 0;
    std::vector<Vector3> vertices_;
    std::vector<MeshFace> faces_;
    std::map<CellKey, std::vector<std::size_t>> cells_;
};

inline MeshDefinition mesh_from_indexed(const std::vector<std::array<double, 3>> &positions,
                                        const std::vector<std::array<std::size_t, 3>> &triangles, double scale,
                                        bool merge, double tolerance) {
    TriangleAssembler assembler(merge, tolerance, triangles.size());
    for (const auto &tri : triangles) {
        if (tri[0] >= positions.size() || tri[1] >= positions.size() || tri[2] >= positions.size())
            continue;
        const auto &a = positions[tri[0]];
        const auto &b = positions[tri[1]];
        const auto &c = positions[tri[2]];
        assembler.push_triangle({a[0] * scale, a[1] * scale, a[2] * scale},
                               {b[0] * scale, b[1] * scale, b[2] * scale},
                               {c[0] * scale, c[1] * scale, c[2] * scale});
    }
    return assembler.finish();
}

inline std::uint32_t read_u32_le(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

inline float read_f32_le(const std::uint8_t *p) {
    std::uint32_t bits = read_u32_le(p);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

} // namespace import_detail
} // namespace libopencad
