// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <cstring>
#include <iomanip>
#include <limits>
#include <libopencad/dwg_sections.hpp>
#include "../src/dwg_sections_internal.hpp"
#include <algorithm>
using namespace libopencad;
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
                v.reference_point = {};
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
    if (auto p = std::get_if<HistoryLoft>(&op.parameters))
        p->settings.reset();
    return op;
}
static void check(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
static void malformed(const DwgDocument &doc) {
    std::ostringstream encoded;
    doc.write(encoded);
    const auto source = encoded.str();
    auto object_data = read_dwg_paged_section(source, "AcDb:AcDbObjects");
    auto it = std::find_if(
        doc.objects().history_records.begin(), doc.objects().history_records.end(), [](const auto &pair) {
            return pair.second.operation && pair.second.operation->base.kind == SolidHistoryKind::Sphere;
        });
    check(it != doc.objects().history_records.end(), "Sphere history fixture");
    auto h = it->first;
    const auto &rec = doc.objects().records.at(h);
    DwgBitReader r(std::string_view(object_data).substr(rec.body_offset, rec.body_size));
    r.seek(it->second.parameters_begin_bit);
    r.bit_long();
    r.bit_long();
    // First option is a noncompact BD (00 + IEEE double); replace it with +infinity.
    check(r.bits(2) == 0, "Noncompact draft angle");
    auto bit = rec.body_offset * 8 + r.position();
    const std::uint64_t inf = 0x7ff0000000000000ULL;
    for (unsigned byte = 0; byte < 8; ++byte)
        for (unsigned k = 0; k < 8; ++k) {
            auto pos = bit + byte * 8 + k;
            unsigned mask = 1u << (7 - pos % 8);
            auto c = static_cast<unsigned char>(object_data[pos / 8]);
            object_data[pos / 8] = char((c & ~mask) | (((inf >> (byte * 8 + 7 - k)) & 1) ? mask : 0));
        }
    auto end = rec.body_offset + rec.body_size;
    std::uint16_t crc = 0xc0c1;
    for (std::size_t i = rec.offset; i < end; ++i) {
        crc ^= static_cast<unsigned char>(object_data[i]);
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    object_data[end] = char(crc);
    object_data[end + 1] = char(crc >> 8);
    auto replace =
        source.substr(0, 6) == "AC1021" ? detail::replace_ac21_sections : detail::replace_ac18_sections;
    auto damaged = replace(source, {{"AcDb:AcDbObjects", object_data}});
    std::istringstream in(damaged);
    auto d = DwgDocument::read(in);
    check(d.objects().history_errors.count(h) &&
              d.objects().history_records.size() + 1 == doc.objects().history_records.size(),
          "Malformed history isolated by handle");
    std::ostringstream saved;
    d.write(saved);
    check(saved.str() == damaged, "Malformed typed payload retained verbatim");
}
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument("input prefix");
        std::ifstream in(argv[1], std::ios::binary);
        auto d = DwgDocument::read(in);
        auto prefix = std::string(argv[2]);
        for (auto &[h, e] : d.objects().history_errors)
            std::cerr << h << ": " << e << '\n';
        if (!d.objects().history_errors.empty())
            throw std::runtime_error("History decode errors");
        malformed(d);
        {
            std::ifstream metadata_source(std::string(argv[1]) + ".metadata.dwg", std::ios::binary);
            auto m = DwgDocument::read(metadata_source);
            std::ostringstream before;
            m.write(before);
            bool tested = false;
            for (const auto &[handle, record] : m.objects().history_records) {
                if (!record.operation || record.operation->base.kind != SolidHistoryKind::Loft)
                    continue;
                check(record.extension_dictionary != 0, "Loft metadata fixture");
                bool failed = false;
                try {
                    auto invalid=m.history_loft_settings(handle);invalid.start_bulge=-1;m.set_history_loft_settings(handle,invalid);
                } catch (const std::invalid_argument &) {
                    failed = true;
                }
                check(failed, "Invalid metadata mutation rejects");
                tested = true;
            }
            std::ostringstream after;
            m.write(after);
            check(tested && before.str() == after.str(), "Metadata failure rolls back");
        }

        std::ofstream metadata(prefix + ".metadata");
        auto number = [&](double x) {
            std::uint64_t bits;
            std::memcpy(&bits, &x, 8);
            metadata << ' ' << bits;
        };
        for (auto &[h, r] : d.objects().history_records) {
            metadata << h << ' ' << r.extension_dictionary << ' ' << r.reactors.size();
            for (auto v : r.reactors)
                metadata << ' ' << v;
            if (r.operation) {
                auto &b = r.operation->base;
                metadata << ' ' << r.operation->operation_major << ' ' << r.operation->operation_minor;
                for (double x : b.transform)
                    number(x);
                metadata << ' ' << b.color.index().value_or(-1) << ' '
                         << b.color.to_true_color_value().value_or(0);
                const auto value_handle = h;
                std::visit(
                    [&](const auto &value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, double>)
                            number(value);
                        else if constexpr (std::is_same_v<T, Vector2>) {
                            number(value.x);
                            number(value.y);
                        } else if constexpr (std::is_same_v<T, std::string>) {
                            std::ofstream text(prefix + ".value-" + std::to_string(value_handle));
                            text << value;
                        } else if constexpr (!std::is_same_v<T, std::monostate>)
                            metadata << ' ' << value;
                    },
                    b.value);
            }
            metadata << '\n';
        }
        std::set<SolidHistoryKind> kinds;
        std::ofstream out(prefix + ".txt"), graphs(prefix + ".graphs");
        for (auto &[h, r] : d.objects().history_records) {
            out << h << ' ' << r.owner;
            if (r.root) {
                auto &v = *r.root;
                out << " root " << v.major << ' ' << v.minor << ' ' << v.evaluation_graph << ' '
                    << v.history_node_id << ' ' << v.show_history << ' ' << v.record_history;
            } else if (r.operation) {
                auto &b = r.operation->base;
                kinds.insert(b.kind);
                out << " node " << b.major << ' ' << b.minor << ' ' << b.step_id << ' ' << b.eval_node_id
                    << ' ' << b.eval_major << ' ' << b.eval_minor << ' ' << b.parent_id << ' ' << b.value_code
                    << ' ' << b.material << ' ' << int(b.kind);
            } else if (r.graph) {
                auto &g = *r.graph;
                out << " graph " << g.first_node_id << ' ' << g.first_node_id_copy;
                for (auto &n : g.nodes) {
                    out << " n " << n.id << ' ' << n.edge_flags << ' ' << n.next_id << ' ' << n.expression;
                    for (auto x : n.node_data)
                        out << ' ' << x;
                }
                for (auto &e : g.edges) {
                    out << " e " << e.id << ' ' << e.next_id << ' ' << e.incoming_edge << ' ' << e.source_node
                        << ' ' << e.destination_node;
                    for (auto x : e.outgoing_edges)
                        out << ' ' << x;
                }
            }
            out << '\n';
        }
        if (kinds.size() != 15)
            throw std::runtime_error("Fifteen history operation kinds required");
        for (auto &[h, m] : d.objects().modelers) {
            auto g = d.modeler_history_graph(h);
            if (!g)
                continue;
            graphs << h << ' ' << g->root << ' ' << g->evaluation_graph.value_or(0);
            for (auto id : g->nodes)
                graphs << ' ' << id;
            graphs << '\n';
        }
        {
            std::ofstream saved(prefix + ".dwg", std::ios::binary);
            d.write(saved);
        }
        auto records = d.objects().history_records;
        for (auto &[h, r] : records)
            if (r.operation)
                d.update_history_inputs(h, r.operation->parameters, r.profiles);
        {
            std::ofstream roundtrip(prefix + ".parameters.dwg", std::ios::binary);
            d.write(roundtrip);
        }
        std::ofstream ids(prefix + ".edited");
        for (auto &[h, r] : records) {
            if (!r.operation)
                continue;
            auto owner = r.owner;
            bool paper = false;
            for (int n = 0; n < 16 && owner; ++n) {
                auto m = d.objects().modelers.find(owner);
                if (m != d.objects().modelers.end()) {
                    paper = m->second.properties.owner == d.objects().paper_space;
                    break;
                }
                auto i = records.find(owner);
                if (i == records.end())
                    break;
                owner = i->second.owner;
            }
            if (paper)
                continue;
            ids << h << '\n';
            auto op = changed(*r.operation);
            auto profiles = r.profiles;
            ProfileLine line;
            line.start = {3, 4, 5};
            line.end = {7, 8, 9};
            using K = SolidHistoryKind;
            auto kind = r.operation->base.kind;
            if (kind == K::Sweep || kind == K::Extrusion) {
                profiles.profile = line;
                profiles.path = line;
            } else if (kind == K::Revolve) {
                profiles.profile = line;
            } else if (kind == K::Loft) {
                profiles.sections = {line, line};
                profiles.guides = {line};
                profiles.path = line;
            }
            std::ostringstream before;
            d.write(before);
            auto invalid = op.parameters;
            bool rejected = false;
            if (auto p = std::get_if<HistorySphere>(&invalid)) {
                p->radius = std::numeric_limits<double>::infinity();
                try {
                    d.update_history_inputs(h, invalid, profiles);
                } catch (const std::invalid_argument &) {
                    rejected = true;
                }
                if (!rejected)
                    throw std::runtime_error("Nonfinite history must reject");
                std::ostringstream after;
                d.write(after);
                if (after.str() != before.str())
                    throw std::runtime_error("Failed history edit mutated drawing");
            }
            d.update_history_inputs(h, op.parameters, profiles);
        }
        {
            std::ofstream saved(prefix + ".edited.dwg", std::ios::binary);
            d.write(saved);
        }
        for (const auto &[h, r] : records)
            if (r.operation && r.operation->base.kind == SolidHistoryKind::Brep)
                d.update_history_inputs(h, HistoryBrep{}, {});
        {
            std::ofstream empty(prefix + ".empty-brep.dwg", std::ios::binary);
            d.write(empty);
        }
        for (const auto &[h, r] : records)
            if (r.operation && r.operation->base.kind == SolidHistoryKind::Brep)
                d.update_history_inputs(h, changed(*r.operation).parameters, {});
        {
            std::ofstream refilled(prefix + ".refilled-brep.dwg", std::ios::binary);
            d.write(refilled);
        }
        std::cout << "Read native DWG history graph and fifteen operation families\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
