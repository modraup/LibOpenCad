// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/count.rs block instance counting and similar matching.
#pragma once

#include "hatch.hpp"
#include "transform.hpp"
#include "vector.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace libopencad {

class DxfDocument;

struct BlockInstance {
    std::uint64_t handle = 0;
    std::string name;
    std::string layer;
    std::array<double, 3> scale{1, 1, 1};
    bool mirrored = false;
    std::optional<std::uint64_t> duplicate_of;
};

struct CountKey {
    std::optional<std::string> layer;
    std::optional<std::array<double, 3>> scale;
    std::optional<bool> mirror_state;
    static CountKey of(const BlockInstance &instance, bool layer, bool scale, bool mirror);
    bool matches(const BlockInstance &instance) const;
    std::string json() const;
    std::string label() const;
};

struct BlockCount {
    std::string name;
    std::vector<std::uint64_t> counted;
    std::vector<std::uint64_t> duplicates;
};

std::vector<BlockInstance> block_instances(const DxfDocument &doc,
                                           const std::optional<std::vector<std::array<double, 2>>> &area = {});
std::vector<BlockCount> block_counts(const std::vector<BlockInstance> &instances);
std::size_t count_of(const std::vector<BlockInstance> &instances, std::string_view name, const CountKey &key);
std::vector<std::pair<CountKey, std::size_t>> expanded_counts(const std::vector<BlockInstance> &instances,
                                                              std::string_view name, bool layer, bool scale,
                                                              bool mirror);
std::string block_count_json(std::string_view name, const CountKey &key);
std::string area_count_json(std::string_view name, const CountKey &key, std::uint64_t boundary);
std::string single_count_json(const std::vector<std::uint64_t> &targets);
std::string evaluate_count(const DxfDocument &doc, std::string_view code);

struct CountQuery {
    enum class Kind { Block, Single };
    Kind kind = Kind::Block;
    std::string name;
    CountKey key;
    std::optional<std::uint64_t> boundary;
    std::vector<std::uint64_t> targets;
};
std::optional<CountQuery> parse_query(std::string_view json);

struct CountPiece {
    enum class Kind { Segment, Conic, Bezier };
    Kind kind = Kind::Segment;
    Vector3 a{}, b{};
    Vector3 c{}, u{}, v{};
    double t0 = 0, t1 = 0;
    std::vector<std::array<double, 4>> bezier;
    static CountPiece segment(Vector3 p, Vector3 q);
    static CountPiece conic(Vector3 center, Vector3 major, Vector3 minor, double start, double end);
    Vector3 start() const;
    CountPiece placed(const Transform &transform, Vector3 base) const;
    std::vector<Vector3> points() const;
};

std::vector<CountPiece> insert_outline(const DxfDocument &doc, std::uint64_t insert, std::size_t depth = 0);
std::vector<CountPiece> entity_outline(const DxfDocument &doc, std::uint64_t handle);
std::vector<CountPiece> hatch_outline(const HatchDefinition &hatch);
std::optional<std::vector<Vector3>> insert_corners(const DxfDocument &doc, std::uint64_t insert,
                                                   std::size_t depth = 0);
std::vector<std::vector<std::uint64_t>> similar_matches(const DxfDocument &doc, std::uint64_t target,
                                                        const std::optional<std::vector<std::array<double, 2>>> &area = {});
std::vector<std::vector<std::uint64_t>> group_matches(const DxfDocument &doc,
                                                      const std::vector<std::uint64_t> &targets,
                                                      const std::optional<std::vector<std::array<double, 2>>> &area = {});

std::optional<std::vector<std::array<double, 2>>> boundary_polygon(const DxfDocument &doc, std::uint64_t handle);
bool valid_boundary(const DxfDocument &doc, std::uint64_t handle);
bool self_intersects(const std::vector<std::array<double, 2>> &ring);

} // namespace libopencad
