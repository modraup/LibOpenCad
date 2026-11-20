// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <limits>
#include <algorithm>
using namespace libopencad;
static std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static void save(const DxfDocument &d, const std::string &prefix) {
    for (bool b : {false, true}) {
        std::ofstream o(prefix + (b ? ".binary.dxf" : ".ascii.dxf"), std::ios::binary);
        d.write(o, b);
    }
}
static SolidHistoryOperation changed(SolidHistoryOperation op) {
    op.operation_major += 3;
    op.operation_minor += 4;
    op.base.eval_major += 2;
    op.base.transform[12] += .5;
    op.base.color = Color::from_rgb(12, 34, 56);
    std::visit(
        [](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, HistoryBox>) {
                v.length += 1;
                v.width += 2;
                v.height += 3;
            } else if constexpr (std::is_same_v<T, HistorySphere>)
                v.radius += 2;
            else if constexpr (std::is_same_v<T, HistoryCylinder>) {
                v.height += 1;
                v.major_radius += 2;
                v.minor_radius += 3;
                v.x_radius += 4;
            } else if constexpr (std::is_same_v<T, HistoryCone>) {
                v.height += 1;
                v.base_x_radius += 2;
                v.base_y_radius += 3;
                v.top_radius += 4;
            } else if constexpr (std::is_same_v<T, HistoryPyramid>) {
                v.height += 1;
                v.sides = 7;
                v.radius += 2;
                v.top_radius += 3;
            } else if constexpr (std::is_same_v<T, HistoryTorus>) {
                v.major_radius += 2;
                v.minor_radius += 1;
            } else if constexpr (std::is_same_v<T, HistoryBoolean>)
                v.operation = 1;
            else if constexpr (std::is_same_v<T, HistoryBrep>)
                v.sat = acis::build_box({2, 3, 4}, 5, 6, 7).to_sat_string();
            else if constexpr (std::is_same_v<T, HistoryFillet>) {
                v.method = 2;
                v.edges = {1, 3, 5};
                v.radii = {.25, .5, 1};
                v.start_setbacks = {.1, .2};
                v.end_setbacks = {.3};
            } else if constexpr (std::is_same_v<T, HistoryChamfer>) {
                v.method = 1;
                v.base_distance = 3;
                v.other_distance = 4;
                v.edges = {2, 4, 6};
                v.base_face = 7;
            } else if constexpr (std::is_same_v<T, HistorySweep>) {
                v.direction = {1, 2, 3};
                v.draft_angle = .2;
                v.start_draft_distance = 2;
                v.end_draft_distance = 3;
                v.twist_angle = .3;
                v.scale_factor = 2;
                v.align_angle = .4;
                v.sweep_entity_transform[12] = 4;
                v.path_entity_transform[13] = 5;
                v.has_align_start = true;
                v.align_option = 2;
                v.miter_option = 1;
                v.align_start = false;
                v.bank = true;
                v.flags_294_296 = {true, false, true};
                v.reference_point = {4, 5, 6};
            } else if constexpr (std::is_same_v<T, HistoryLoft>) {
                v.surface_option = 3;
                v.start_draft_angle = .4;
                v.end_draft_angle = .5;
                v.start_magnitude = 2;
                v.end_magnitude = 3;
                v.flags = {true, false, true, false, true, false, true, false};
                if (!v.settings)
                    v.settings = HistoryLoftSettings{};
                v.settings->normals = 6;
                v.settings->start_draft_angle = .6;
                v.settings->end_draft_angle = .7;
                v.settings->start_magnitude = 4;
                v.settings->end_magnitude = 5;
                v.settings->start_bulge = .8;
                v.settings->end_bulge = .9;
                v.settings->section_counts = {1, 1};
            } else if constexpr (std::is_same_v<T, HistoryRevolve>) {
                v.axis_point = {1, 2, 3};
                v.direction = {0, 1, 0};
                v.revolve_angle = 1.2;
                v.start_angle = .3;
                v.draft_angle = .4;
                v.field_44 = 2;
                v.field_45 = 3;
                v.twist_angle = .5;
                v.flag_290 = true;
                v.close_to_axis = true;
            }
        },
        op.parameters);
    return op;
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream input(argv[1], std::ios::binary);
    auto source = DxfDocument::read(input);
    std::string prefix = argv[2];
    std::uint64_t solid = 0, surface = 0;
    source.for_each<DxfModelerEntity>([&](const auto &e) {
        auto h = std::stoull(e.handle(), nullptr, 16);
        if (!solid && e.kind() == ModelerKind::Solid3D)
            solid = h;
        if (e.kind() == ModelerKind::PlaneSurface)
            surface = h;
    });
    check(solid && surface, "fixture modelers");
    auto source_graph = *source.solid_history_graph(surface);
    auto edited = source;
    for (auto h : source_graph.nodes) {
        auto op = *source.solid_history_definition(h);
        edited.update_solid_history_step(surface, changed(op));
    }
    save(edited, prefix + ".edited");
    // Replace the selected node's type while retaining its handle/owner and existing graph.
    auto current = source;
    SolidHistoryOperation replacement(SolidHistoryKind::Cone);
    replacement.parameters = HistoryCone{8, 4, 3, 1};
    auto old = current.update_solid_history(solid, replacement);
    check(old.base.kind == SolidHistoryKind::Boolean, "old operation returned");
    save(current, prefix + ".current");
    auto identity_current = source;
    identity_current.update_solid_history(
        solid, *source.solid_history_definition(source.solid_history_operation(solid)->handle));
    check(!identity_current.solid_history_tree(solid), "Rust current-update step/evaluation ID distinction");
    save(identity_current, prefix + ".identity-current");
    auto authored = source;
    auto initial = *source.solid_history_definition(source.solid_history_operations(surface)->front().handle);
    initial.base.step_id = 5;
    auto g = authored.create_solid_history(solid, initial);
    check(g.nodes.size() == 1, "created root");
    std::vector<SolidHistoryOperation> ops;
    for (auto h : source_graph.nodes)
        ops.push_back(*source.solid_history_definition(h));
    std::sort(ops.begin(), ops.end(),
              [](auto &a, auto &b) { return a.base.eval_node_id < b.base.eval_node_id; });
    for (std::size_t i = 1; i < ops.size(); ++i)
        g = authored.append_solid_history(solid, ops[i]);
    check(g.nodes.size() == 15, "appended all operation kinds");
    save(authored, prefix + ".authored");
    auto surface_new = source;
    surface_new.create_solid_history(surface, initial);
    surface_new.append_solid_history(surface, replacement);
    save(surface_new, prefix + ".surface");
    // Only well-formed reactor handle lists may be cleaned during root replacement.
    auto old_root = source.solid_history_graph(solid)->root;
    for (bool opaque : {false, true}) {
        auto data = records(bytes(source), source.is_binary());
        inject(data, hex(surface), {hex(old_root)});
        if (opaque) {
            auto &rec = at(data, hex(surface));
            auto end = std::find(rec.begin(), rec.end(), Pair{102, "}"});
            rec.insert(end, {300, "private reactor payload"});
        }
        auto with_reactor = read(encode(data, source.is_binary()));
        auto before = bytes(with_reactor);
        bool refused = false;
        try {
            with_reactor.create_solid_history(solid, replacement);
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused == opaque, "only complete reactor lists are removable");
        if (opaque)
            check(bytes(with_reactor) == before, "opaque reactor rollback");
        else
            for (auto ref : with_reactor.solid_history_references(surface))
                check(ref.target != old_root, "removed history reactor cleanup");
    }
    auto rejected = source;
    for (int i = 0; i < 7; ++i) {
        auto before = bytes(rejected);
        bool threw = false;
        try {
            auto op = replacement;
            if (i == 0)
                std::get<HistoryCone>(op.parameters).height = std::numeric_limits<double>::quiet_NaN();
            if (i == 1)
                op.base.material = solid;
            if (i == 2)
                op.base.kind = SolidHistoryKind::Box;
            if (i == 3) {
                op.base.value_code = 91;
                op.base.value = std::uint64_t(solid);
            }
            if (i == 4)
                op.base.eval_node_id = INT32_MAX;
            if (i < 4)
                rejected.append_solid_history(solid, op);
            else if (i == 4)
                rejected.update_solid_history_step(solid, op);
            else if (i == 5) {
                rejected.set_solid_history_active_node(
                    solid, source.solid_history_operations(solid)->front().node_id());
                before = bytes(rejected);
                rejected.append_solid_history(solid, op);
            } else {
                auto region =
                    rejected.add_modeler_entity(ModelerKind::Region, acis::build_box({0, 0, 0}, 1, 1, 1));
                before = bytes(rejected);
                rejected.create_solid_history(region, op);
            }
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw && bytes(rejected) == before, "history authoring rollback");
    }
    // Pure C++ authoring, with no imported history objects or classes.
    auto fresh = DxfDocument::create();
    auto h = fresh.add_modeler_entity(ModelerKind::Solid3D, acis::build_box({0, 0, 0}, 1, 2, 3));
    fresh.create_solid_history(h, SolidHistoryOperation{});
    auto legacy_graph = *fresh.solid_history_graph(h);
    auto raw = records(bytes(fresh), false);
    drop(raw, {hex(*legacy_graph.evaluation_graph)});
    for (auto &[code, value] : at(raw, hex(legacy_graph.root)))
        if (code == 360)
            value = "0";
    for (auto &[code, value] : at(raw, hex(legacy_graph.nodes[0])))
        if (code == 330)
            value = hex(legacy_graph.root);
    auto legacy = read(encode(raw, false));
    if (source.is_binary()) {
        std::ostringstream binary;
        legacy.write(binary, true);
        legacy = read(binary.str());
    }
    check(!legacy.solid_history_graph(h)->evaluation_graph, "graphless fixture");
    save(legacy, prefix + ".legacy-before");
    legacy.append_solid_history(h, replacement);
    check(legacy.solid_history_operations(h)->size() == 2, "legacy graph promotion");
    save(legacy, prefix + ".legacy");

    auto overflow = fresh;
    auto huge = replacement;
    huge.base.step_id = INT32_MAX;
    overflow.update_solid_history(h, huge);
    auto unchanged = bytes(overflow);
    bool refused = false;
    try {
        overflow.append_solid_history(h, replacement);
    } catch (const std::overflow_error &) {
        refused = true;
    }
    check(refused && bytes(overflow) == unchanged, "history id overflow rollback");

    auto conflict = fresh;
    ClassDefinition bad_class;
    bad_class.name = "ACSH_CONE_CLASS";
    bad_class.cpp_name = "ForeignClass";
    conflict.set_class(bad_class);
    unchanged = bytes(conflict);
    refused = false;
    try {
        conflict.append_solid_history(h, replacement);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused && bytes(conflict) == unchanged, "history class conflict rollback");
    auto legacy_version = records(bytes(fresh), false);
    for (auto &rec : legacy_version)
        if (rec.front().second == "SECTION") {
            bool version = false;
            for (auto &[code, value] : rec) {
                if (code == 9)
                    version = value == "$ACADVER";
                else if (version && code == 1)
                    value = "AC1018";
            }
        }
    auto r2004 = read(encode(legacy_version, false));
    unchanged = bytes(r2004);
    refused = false;
    try {
        r2004.append_solid_history(h, replacement);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused && bytes(r2004) == unchanged, "R2004 history authoring boundary");
    fresh.append_solid_history(h, replacement);
    save(fresh, prefix + ".fresh");
    std::ofstream manifest(prefix + ".manifest");
    manifest << std::hex << solid << ' ' << surface << ' ' << h << '\n';
    std::cout << "History authoring passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
