// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/entities/acis/primitives.rs::build_planar_body.
#include "libopencad/acis.hpp"
#include <cmath>
#include <map>

namespace libopencad::acis {
std::optional<SatDocument> build_planar_body(const std::vector<Vector3> &vertices,
                                             const std::vector<std::vector<std::size_t>> &faces) {
    if (faces.empty() || vertices.size() < 4)
        return {};
    std::size_t co_count = 0;
    for (const auto &f : faces) {
        if (f.size() < 3 || f.size() > std::size_t(INT32_MAX) - co_count)
            return {};
        co_count += f.size();
        for (auto v : f)
            if (v >= vertices.size())
                return {};
    }
    for (auto v : vertices)
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            return {};
    SatDocument d = SatDocument::new_body();
    std::vector<std::int32_t> vert;
    for (auto v : vertices) {
        const auto point = d.add_point(v.x, v.y, v.z);
        vert.push_back(d.add_vertex({}, SatPointer{point}));
    }
    struct Edge {
        std::int32_t id;
        std::size_t start, end;
        std::vector<std::int32_t> coedges;
    };
    using Key = std::pair<std::size_t, std::size_t>;
    std::map<Key, Edge> edges;
    std::vector<Key> order;
    auto key = [](auto a, auto b) { return a < b ? Key{a, b} : Key{b, a}; };
    for (const auto &f : faces)
        for (std::size_t i = 0; i < f.size(); ++i) {
            auto a = f[i], b = f[(i + 1) % f.size()];
            if (a == b)
                return {};
            auto k = key(a, b);
            if (edges.count(k))
                continue;
            auto dir = vertices[b] - vertices[a];
            auto len = dir.length();
            if (len < 1e-12 || !std::isfinite(len))
                return {};
            auto curve = d.add_straight_curve(vertices[a], dir / len);
            auto e = d.add_edge({vert[a]}, 0, {vert[b]}, len, {}, SatPointer{curve}, Sense::Forward);
            edges.emplace(k, Edge{e, a, b, {}});
            order.push_back(k);
        }
    std::vector<std::int32_t> surfaces;
    for (const auto &f : faces) {
        auto root = vertices[f[0]], a = vertices[f[1]] - root;
        auto len = a.length();
        if (len < 1e-12 || !std::isfinite(len))
            return {};
        std::optional<Vector3> normal;
        for (std::size_t i = 2; i < f.size(); ++i) {
            auto n = a.cross(vertices[f[i]] - root);
            auto length = n.length();
            if (length > 1e-9 && std::isfinite(length)) {
                normal = n / length;
                break;
            }
        }
        if (!normal)
            return {};
        surfaces.push_back(d.add_plane_surface(root, *normal, a / len));
    }
    const auto total = std::uint64_t(d.records.size()) + co_count + std::uint64_t(faces.size()) * 2 + 2;
    if (total > INT32_MAX)
        return {};
    const auto co_base = std::int32_t(d.records.size()), loop_base = co_base + std::int32_t(co_count),
               face_base = loop_base + std::int32_t(faces.size()),
               shell = face_base + std::int32_t(faces.size()), lump = shell + 1;
    std::int32_t cursor = co_base;
    std::vector<std::int32_t> first;
    for (std::size_t fi = 0; fi < faces.size(); ++fi) {
        const auto &f = faces[fi];
        const auto start = cursor;
        first.push_back(start);
        for (std::size_t i = 0; i < f.size(); ++i) {
            auto a = f[i], b = f[(i + 1) % f.size()];
            auto &e = edges.at(key(a, b));
            const auto sense = e.start == a && e.end == b ? Sense::Forward : Sense::Reversed;
            auto next = start + std::int32_t((i + 1) % f.size()),
                 prev = start + std::int32_t((i + f.size() - 1) % f.size());
            auto id = d.add_coedge({next}, {prev}, {}, {e.id}, sense, {loop_base + std::int32_t(fi)});
            e.coedges.push_back(id);
            ++cursor;
        }
    }
    for (auto k : order) {
        const auto &e = edges.at(k);
        if (e.coedges.size() != 2)
            return {};
        d.record(e.coedges[0])->tokens[3] = SatToken(SatPointer{e.coedges[1]});
        d.record(e.coedges[1])->tokens[3] = SatToken(SatPointer{e.coedges[0]});
    }
    for (std::size_t i = 0; i < faces.size(); ++i)
        d.add_loop({}, {first[i]}, {face_base + std::int32_t(i)});
    for (std::size_t i = 0; i < faces.size(); ++i)
        d.add_face({i + 1 < faces.size() ? face_base + std::int32_t(i) + 1 : -1},
                   {loop_base + std::int32_t(i)}, {shell}, {surfaces[i]}, Sense::Forward, Sidedness::Single);
    d.add_shell({face_base}, {lump});
    d.add_lump({shell}, {0});
    d.records[0].tokens[1] = SatToken(SatPointer{lump});
    d.complete_brep_links();
    if (!d.validate().empty())
        return {};
    return d;
}
} // namespace libopencad::acis
