// SPDX-License-Identifier: MPL-2.0
// Port of acadrust entities/mesh.rs. All indices are zero based.
#pragma once
#include "bounds.hpp"
#include "transform.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>
#include <utility>
namespace libopencad {
struct MeshEdge {
    std::size_t start = 0, end = 0;
    std::optional<double> crease;
    bool connects(std::size_t a, std::size_t b) const {
        return (start == a && end == b) || (start == b && end == a);
    }
    std::optional<std::size_t> other_vertex(std::size_t v) const {
        if (v == start)
            return end;
        if (v == end)
            return start;
        return {};
    }
};
struct MeshFace {
    std::vector<std::size_t> vertices;
    bool is_triangle() const { return vertices.size() == 3; }
    bool is_quad() const { return vertices.size() == 4; }
    void reverse() { std::reverse(vertices.begin(), vertices.end()); }
    std::vector<std::pair<std::size_t, std::size_t>> edges() const;
};
struct MeshDefinition {
    std::int16_t version = 2;
    bool blend_crease = true;
    std::int32_t subdivision_level = 0, override_option = 0;
    std::vector<Vector3> vertices;
    std::vector<MeshFace> faces;
    std::vector<MeshEdge> edges;
    // Stable sorted edges replace Rust HashSet's unspecified iteration order.
    // As upstream, recomputing edges clears all crease values.
    void compute_edges();
    std::size_t triangle_count() const;
    bool is_all_triangles() const;
    bool is_all_quads() const;
    std::optional<std::size_t> find_edge(std::size_t a, std::size_t b) const;
    bool set_edge_crease(std::size_t index, double crease);
    void clear_creases();
    bool has_creases() const;
    void translate(Vector3 offset);
    void scale(Vector3 center, double factor);
    void scale_uniform(double factor);
    std::optional<BoundingBox3D> bounding_box() const;
    std::optional<Vector3> center() const;
    void flip_normals();
    void merge(const MeshDefinition &other);
    static MeshDefinition create_box(Vector3 min, Vector3 max);
    static MeshDefinition create_unit_cube() { return create_box({-.5, -.5, -.5}, {.5, .5, .5}); }
    // Reject zero subdivision counts, which generate nonfinite upstream coordinates.
    static MeshDefinition create_plane(Vector3 origin, Vector3 u, Vector3 v, std::size_t us, std::size_t vs);
    // Upstream cylinder axis is +Y, not +Z.
    static MeshDefinition create_cylinder(Vector3 base, double radius, double height, std::size_t radial,
                                          std::size_t vertical, bool top = true, bool bottom = true);
};
} // namespace libopencad
