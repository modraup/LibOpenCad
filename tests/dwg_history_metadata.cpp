// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include "../src/dwg_metadata_internal.hpp"
#include <libopencad/dwg_sections.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>
#include <algorithm>
using namespace libopencad;
static void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
static std::string bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static void save(const DwgDocument &d, const std::string &p) {
    std::ofstream o(p, std::ios::binary);
    d.write(o);
}
template <class F> void rejects(DwgDocument &d, F f) {
    auto b = bytes(d);
    bool fail = false;
    try {
        f();
    } catch (const std::exception &) {
        fail = true;
    }
    check(fail && b == bytes(d), "Failure must preserve complete bytes");
}
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument("input prefix");
        std::ifstream in(argv[1], std::ios::binary);
        auto d = DwgDocument::read(in);
        std::string prefix = argv[2];
        check(d.objects().metadata_errors.empty(), "Metadata decode");
        auto source = d;
        std::uint64_t loft = 0, surface = 0, root = 0, graph = 0;
        for (auto &[h, m] : d.objects().modelers)
            if (m.surface) {
                auto g = d.modeler_history_graph(h);
                if (g) {
                    surface = h;
                    root = g->root;
                    graph = g->evaluation_graph.value();
                    break;
                }
            }
        check(surface && root && graph, "Surface history resolved from XRECORD");
        for (auto &[h, r] : d.objects().history_records)
            if (r.operation && r.operation->base.kind == SolidHistoryKind::Loft && r.owner == graph) {
                loft = h;
                break;
            }
        check(loft, "Loft in surface history graph");
        auto link =
            detail::metadata_record(d.objects(), surface, d.modeler(surface).properties.extension_dictionary,
                                    "CADCODEC_SURFACE_HISTORY_V1");
        auto rawlink = link->metadata;
        detail::metadata_put(rawlink, 330, 0, 8);
        auto rawbytes = detail::edit_dwg_metadata(bytes(d), d.objects(), link->handle, rawlink);
        std::istringstream rawstream(rawbytes);
        auto redirected = DwgDocument::read(rawstream);
        check(redirected.modeler_history_graph(surface)->root == root,
              "Handle stream overrides raw metadata handle");
        save(redirected, prefix + ".raw-reference.dwg");
        auto shared = d.objects();
        shared.history_records.at(root).extension_dictionary =
            d.modeler(surface).properties.extension_dictionary;
        bool shared_failed = false;
        try {
            (void)detail::metadata_record(shared, surface, d.modeler(surface).properties.extension_dictionary,
                                          "CADCODEC_SURFACE_HISTORY_V1");
        } catch (const std::invalid_argument &) {
            shared_failed = true;
        }
        check(shared_failed, "Shared dictionary rejected");
        const auto &root_record = d.history_record(root);
        auto legacy =
            detail::edit_dwg_history_slot(bytes(d), d.objects(), root, root_record.graph_reference_begin,
                                          root_record.graph_reference_end, graph, true, 4);
        std::istringstream legacy_stream(legacy);
        auto relinked = DwgDocument::read(legacy_stream);
        relinked.set_history_evaluation_graph(root, graph);
        auto object_bytes = read_dwg_paged_section(bytes(relinked), "AcDb:AcDbObjects");
        auto envelope = relinked.objects().records.at(root);
        DwgBitReader bits(std::string_view(object_bytes).substr(envelope.body_offset, envelope.body_size));
        bits.seek(relinked.history_record(root).graph_reference_begin);
        check(bits.handle(root).code == 3, "Graph relink restores hard ownership");
        auto original = d.history_loft_settings(loft);
        auto settings = original;
        settings.normals = 4;
        settings.start_continuity = 0;
        settings.end_continuity = 1;
        settings.start_draft_angle = .2;
        settings.end_draft_angle = .6;
        settings.start_magnitude = 2;
        settings.end_magnitude = 3;
        settings.start_bulge = .7;
        settings.end_bulge = .9;
        settings.closed = true;
        settings.periodic = false;
        settings.surface = true;
        settings.align_direction = false;
        settings.section_counts = {2, 3};
        SolidHistoryOperation probe(SolidHistoryKind::Loft);
        HistoryProfileGeometry geo;
        ProfileLine line;
        line.start = {7, 8, 9};
        line.end = {10, 11, 12};
        geo.metadata_path = line;
        set_history_profiles(probe, geo, DxfVersion::AC1032);
        settings.path = std::get<HistoryLoft>(probe.parameters).settings->path;
        d.set_history_loft_settings(loft, settings);
        auto read = d.history_loft_settings(loft);
        check(read.section_counts == settings.section_counts &&
                  (read.path.size() == settings.path.size() &&
                   std::equal(read.path.begin(), read.path.end(), settings.path.begin(),
                              [](const auto &a, const auto &b) {
                                  return a.code == b.code && a.value == b.value;
                              })) &&
                  read.start_bulge == .7,
              "Loft metadata reread");
        check(std::get<HistoryLoft>(d.history_record(loft).operation->parameters).start_draft_angle == .2,
              "Native loft synchronized");
        rejects(d, [&] {
            auto v = settings;
            v.start_magnitude = -1;
            d.set_history_loft_settings(loft, v);
        });
        rejects(d, [&] {
            auto v = settings;
            v.start_bulge = std::numeric_limits<double>::infinity();
            d.set_history_loft_settings(loft, v);
        });
        rejects(d, [&] {
            auto v = settings;
            v.section_counts = {0};
            d.set_history_loft_settings(loft, v);
        });
        rejects(d, [&] {
            auto v = settings;
            v.path = {{92, "19"}, {93, "100"}, {310, "00"}};
            d.set_history_loft_settings(loft, v);
        });
        save(d, prefix + ".options.dwg");
        auto cleared = settings;
        cleared.path.clear();
        d.set_history_loft_settings(loft, cleared);
        check(d.history_loft_settings(loft).path.empty() && !d.history_record(loft).profiles.path,
              "Direct options edit clears metadata and native path together");
        save(d, prefix + ".cleared.dwg");
        d.set_history_loft_settings(loft, settings);
        auto record = d.history_record(loft);
        auto p = record.operation->parameters;
        std::get<HistoryLoft>(p).start_draft_angle = .4;
        record.profiles.path.reset();
        d.update_history_inputs(loft, p, record.profiles);
        check(d.history_loft_settings(loft).path.empty() &&
                  d.history_loft_settings(loft).start_draft_angle == .4,
              "Generic edit updates metadata");
        save(d, prefix + ".inputs.dwg");
        auto metadata =
            detail::metadata_record(source.objects(), loft, source.history_record(loft).extension_dictionary,
                                    "CADCODEC_LOFT_OPTIONS_V1");
        auto malformed = metadata->metadata;
        detail::metadata_put(malformed, 90, 2, 4);
        auto damaged =
            detail::edit_dwg_metadata(bytes(source), source.objects(), metadata->handle, malformed);
        std::istringstream corrupt(damaged);
        auto unsupported = DwgDocument::read(corrupt);
        rejects(unsupported, [&] { unsupported.set_history_loft_settings(loft, settings); });
        d = source;
        auto nodes = d.history_record(graph).graph->nodes;
        check(nodes.size() > 1, "Graph nodes");
        auto active = d.history_record(nodes.front().expression).operation->base.node_id();
        check(active != nodes.front().id, "Operation and graph IDs differ in fixture");
        rejects(d, [&] { d.set_history_active_node(root, nodes.front().id); });
        d.set_history_active_node(root, active);
        d.set_history_evaluation_graph(root, graph);
        d.set_history_node_expression(graph, nodes.front().id, nodes.back().expression);
        std::uint64_t material = 0;
        for (auto &item : d.objects().records) {
            auto t = item.second.type;
            if (t >= 500 && std::size_t(t - 500) < d.objects().class_names.size() &&
                d.objects().class_names[t - 500] == "MATERIAL") {
                material = item.first;
                break;
            }
        }
        check(material, "Material fixture");
        d.set_history_material(loft, material);
        check(d.history_record(root).root->history_node_id == active &&
                  d.history_record(graph).graph->nodes.front().expression == nodes.back().expression,
              "Reference edits");
        rejects(d, [&] { d.set_history_active_node(root, -12345); });
        rejects(d, [&] { d.set_history_node_expression(graph, nodes.front().id, root); });
        rejects(d, [&] { d.set_history_evaluation_graph(root, loft); });
        rejects(d, [&] { d.set_history_material(loft, surface); });
        rejects(d, [&] { d.set_surface_history(surface, loft); });
        for (auto &[h, r] : source.objects().history_records)
            if (r.root && h != root) {
                auto foreign = h;
                rejects(d, [&] { d.set_surface_history(surface, foreign); });
                break;
            }
        save(d, prefix + ".references.dwg");
        d.set_surface_history(surface, 0);
        check(!d.modeler_history_graph(surface), "Detach surface history");
        save(d, prefix + ".detached.dwg");
        d.set_surface_history(surface, root);
        check(d.modeler_history_graph(surface)->root == root, "Restore history link");
        save(d, prefix + ".restored.dwg");
        std::ofstream ids(prefix + ".ids");
        ids << surface << ' ' << root << ' ' << graph << ' ' << loft << ' ' << active << ' '
            << nodes.back().expression << ' ' << material;
        std::cout << "DWG owned metadata, native synchronization, graph references and rollback verified\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
