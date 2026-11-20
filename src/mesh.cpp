// SPDX-License-Identifier: MPL-2.0
#include <libopencad/mesh.hpp>
#include <set>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace libopencad {
std::vector<std::pair<std::size_t, std::size_t>> MeshFace::edges() const {
    std::vector<std::pair<std::size_t, std::size_t>> r;
    for (std::size_t i = 0; i < vertices.size(); ++i)
        r.emplace_back(vertices[i], vertices[(i + 1) % vertices.size()]);
    return r;
}
void MeshDefinition::compute_edges() {
    std::set<std::pair<std::size_t, std::size_t>> unique;
    for (const auto &f : faces)
        for (auto e : f.edges())
            unique.emplace(std::min(e.first, e.second), std::max(e.first, e.second));
    edges.clear();
    for (auto e : unique)
        edges.push_back({e.first, e.second, {}});
}
std::size_t MeshDefinition::triangle_count() const {
    std::size_t n = 0;
    for (const auto &f : faces)
        if (f.vertices.size() > 2)
            n += f.vertices.size() - 2;
    return n;
}
bool MeshDefinition::is_all_triangles() const {
    return std::all_of(faces.begin(), faces.end(), [](const auto &f) { return f.is_triangle(); });
}
bool MeshDefinition::is_all_quads() const {
    return std::all_of(faces.begin(), faces.end(), [](const auto &f) { return f.is_quad(); });
}
std::optional<std::size_t> MeshDefinition::find_edge(std::size_t a, std::size_t b) const {
    for (std::size_t i = 0; i < edges.size(); ++i)
        if (edges[i].connects(a, b))
            return i;
    return {};
}
bool MeshDefinition::set_edge_crease(std::size_t i, double c) {
    if (i >= edges.size())
        return false;
    edges[i].crease = c;
    return true;
}
void MeshDefinition::clear_creases() {
    for (auto &e : edges)
        e.crease.reset();
}
bool MeshDefinition::has_creases() const {
    return std::any_of(edges.begin(), edges.end(), [](const auto &e) { return e.crease.has_value(); });
}
void MeshDefinition::translate(Vector3 offset) {
    for (auto &v : vertices)
        v = v + offset;
}
void MeshDefinition::scale(Vector3 center, double factor) {
    for (auto &v : vertices)
        v = center + (v - center) * factor;
}
void MeshDefinition::scale_uniform(double factor) {
    for (auto &v : vertices)
        v = v * factor;
}
std::optional<BoundingBox3D> MeshDefinition::bounding_box() const {
    return BoundingBox3D::from_points(vertices);
}
std::optional<Vector3> MeshDefinition::center() const {
    auto b = bounding_box();
    if (b)
        return b->center();
    return {};
}
void MeshDefinition::flip_normals() {
    for (auto &f : faces)
        f.reverse();
}
void MeshDefinition::merge(const MeshDefinition &other) {
    auto result = *this;
    auto offset = vertices.size();
    result.vertices.insert(result.vertices.end(), other.vertices.begin(), other.vertices.end());
    for (auto f : other.faces) {
        for (auto &i : f.vertices) {
            if (i > std::numeric_limits<std::size_t>::max() - offset)
                throw std::overflow_error("Mesh index overflow");
            i += offset;
        }
        result.faces.push_back(std::move(f));
    }
    for (auto e : other.edges) {
        if (e.start > std::numeric_limits<std::size_t>::max() - offset ||
            e.end > std::numeric_limits<std::size_t>::max() - offset)
            throw std::overflow_error("Mesh index overflow");
        e.start += offset;
        e.end += offset;
        result.edges.push_back(e);
    }
    *this = std::move(result);
}
MeshDefinition MeshDefinition::create_box(Vector3 a, Vector3 b) {
    MeshDefinition m;
    m.vertices = {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, b.y, a.z}, {a.x, b.y, a.z},
                  {a.x, a.y, b.z}, {b.x, a.y, b.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}};
    m.faces = {{{0, 3, 2, 1}}, {{4, 5, 6, 7}}, {{0, 1, 5, 4}},
               {{2, 3, 7, 6}}, {{0, 4, 7, 3}}, {{1, 2, 6, 5}}};
    m.compute_edges();
    return m;
}
namespace {
void grid_size(std::size_t a, std::size_t b) {
    constexpr auto max = std::size_t(std::numeric_limits<std::int32_t>::max());
    if (!a || !b || a >= max || b >= max || a + 1 > max / (b + 1))
        throw std::invalid_argument("Invalid mesh subdivision counts");
}
} // namespace
MeshDefinition MeshDefinition::create_plane(Vector3 origin, Vector3 u, Vector3 v, std::size_t us,
                                            std::size_t vs) {
    grid_size(us, vs);
    MeshDefinition m;
    for (std::size_t j = 0; j <= vs; ++j)
        for (std::size_t i = 0; i <= us; ++i)
            m.vertices.push_back(origin + u * (double(i) / us) + v * (double(j) / vs));
    auto cols = us + 1;
    for (std::size_t j = 0; j < vs; ++j)
        for (std::size_t i = 0; i < us; ++i) {
            auto a = j * cols + i;
            m.faces.push_back({{a, a + 1, a + cols + 1, a + cols}});
        }
    m.compute_edges();
    return m;
}
MeshDefinition MeshDefinition::create_cylinder(Vector3 base, double radius, double height, std::size_t radial,
                                               std::size_t vertical, bool top, bool bottom) {
    grid_size(radial, vertical);
    MeshDefinition m;
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t h = 0; h <= vertical; ++h) {
        auto y = (double(h) / vertical) * height;
        for (std::size_t r = 0; r < radial; ++r) {
            auto angle = (double(r) / radial) * 2 * pi;
            m.vertices.push_back(base + Vector3{radius * std::cos(angle), y, radius * std::sin(angle)});
        }
    }
    for (std::size_t h = 0; h < vertical; ++h)
        for (std::size_t r = 0; r < radial; ++r) {
            auto next = (r + 1) % radial;
            m.faces.push_back(
                {{h * radial + r, h * radial + next, (h + 1) * radial + next, (h + 1) * radial + r}});
        }
    if (bottom) {
        auto c = m.vertices.size();
        m.vertices.push_back(base);
        for (std::size_t r = 0; r < radial; ++r)
            m.faces.push_back({{c, (r + 1) % radial, r}});
    }
    if (top) {
        auto c = m.vertices.size();
        m.vertices.push_back(base + Vector3{0, height, 0});
        auto start = vertical * radial;
        for (std::size_t r = 0; r < radial; ++r)
            m.faces.push_back({{c, start + r, start + (r + 1) % radial}});
    }
    m.compute_edges();
    return m;
}
} // namespace libopencad
