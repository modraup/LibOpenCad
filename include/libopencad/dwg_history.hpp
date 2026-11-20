// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "solid_history.hpp"
namespace libopencad {
// Native DWG object semantics, separate from the containing entity's primary body.
struct DwgHistoryRecord {
    std::uint64_t handle{}, owner{}, extension_dictionary{};
    std::vector<std::uint64_t> reactors;
    std::string type;
    std::optional<SolidHistoryRoot> root;
    std::optional<SolidHistoryOperation> operation;
    std::optional<BlockEvaluationGraph> graph;
    SurfaceProfiles profiles;
    Vector3 sweep_vector; // DWG-only vector in sweep/extrusion operations.
    std::optional<ProfileRegion> brep;
    std::optional<std::string> color_name, color_book_name;
    std::vector<std::pair<std::string, std::uint64_t>> dictionary_entries;
    std::string metadata;
    std::vector<std::pair<std::size_t, std::size_t>> reference_slots;
    std::vector<std::uint64_t> reference_values;
    std::size_t graph_reference_begin{}, graph_reference_end{}, active_begin{}, active_end{},
        material_begin{}, material_end{};
    std::size_t parameters_begin_bit{}, parameters_end_bit{};
};
} // namespace libopencad
