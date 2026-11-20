// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
using namespace libopencad;
static void change(EmbeddedProfile &entity) {
    std::visit(
        [](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ProfileRegion>) {
                v.geometry.sab = acis::SabWriter::write(acis::build_box({2, 3, 4}, 5, 6, 7));
                v.reference_point.x += 10;
                v.wires[0].points[0].z += 10;
                v.revision.major += 1;
            } else if constexpr (std::is_same_v<T, ProfilePoint>) {
                v.location.x += 10;
                v.thickness += .25;
            } else if constexpr (std::is_same_v<T, ProfileLine>)
                v.end.z += 10;
            else if constexpr (std::is_same_v<T, ProfileArc>) {
                v.center.x += 10;
                v.end_angle += .2;
            } else if constexpr (std::is_same_v<T, ProfileCircle>) {
                v.center.x += 10;
                v.radius += .5;
            } else if constexpr (std::is_same_v<T, ProfileEllipse>) {
                v.center.x += 10;
                v.major_axis.y += .5;
            } else if constexpr (std::is_same_v<T, ProfilePolyline>) {
                v.vertices[0].location.x += 10;
                v.vertices[3].bulge = -.75;
                v.vertices[0].start_width = 2.25;
            } else if constexpr (std::is_same_v<T, ProfileSpline>) {
                if (!v.control_points.empty()) {
                    v.control_points[0].x += 10;
                    v.weights[1] = .75;
                }
                if (!v.fit_points.empty())
                    v.fit_points[1].z += 10;
            } else if constexpr (std::is_same_v<T, ProfileRay> || std::is_same_v<T, ProfileXLine>)
                v.base_point.x += 10;
        },
        entity);
}
static void change(std::optional<EmbeddedProfile> &v) {
    if (v)
        change(*v);
}
static std::string serialized(const DxfDocument &d) {
    std::ostringstream out;
    d.write(out);
    return out.str();
}
template <class F> static void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "invalid input accepted");
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream input(argv[1], std::ios::binary);
    auto source = DxfDocument::read(input);
    auto edited = source;
    std::vector<std::uint64_t> modelers;
    source.for_each<DxfModelerEntity>(
        [&](const auto &e) { modelers.push_back(std::stoull(e.handle(), nullptr, 16)); });
    unsigned changed_count = 0;
    for (auto h : modelers) {
        auto graph = source.solid_history_graph(h);
        if (!graph)
            continue;
        for (auto node : graph->nodes) {
            auto op = *source.solid_history_definition(node);
            if (!std::holds_alternative<HistorySweep>(op.parameters) &&
                !std::holds_alternative<HistoryLoft>(op.parameters) &&
                !std::holds_alternative<HistoryRevolve>(op.parameters))
                continue;
            auto g = read_history_profiles(op, source.version());
            if (op.base.kind == SolidHistoryKind::Sweep) {
                auto &poly = std::get<ProfilePolyline>(*g.sweep);
                check(poly.closed && poly.vertices.size() == 4 && poly.vertices[3].bulge == -.5,
                      "sweep profile geometry");
                check(poly.vertices[0].id == (source.version() >= DxfVersion::AC1024 ? 10 : 0),
                      "vertex ID version boundary");
            }
            if (op.base.kind == SolidHistoryKind::Extrusion) {
                auto &arc = std::get<ProfileArc>(*g.sweep);
                check(arc.center == Vector3{3, 4, 5} && arc.normal == Vector3{0, 1, 0}, "arc WCS to OCS");
            }
            if (op.base.kind == SolidHistoryKind::Loft) {
                check(g.sections.size() == 13 && g.guides.size() == 2, "loft list order/count");
                check(std::holds_alternative<ProfileRegion>(g.sections[10]), "inline ACIS decoded");
                check(std::holds_alternative<ProfileOpaque>(g.sections[12]), "truncated curve retained");
                check(g.metadata_path.has_value(), "metadata path");
            }
            if (auto loft = std::get_if<HistoryLoft>(&op.parameters)) {
                auto without_path = op;
                std::get<HistoryLoft>(without_path.parameters).settings->path = {{92, "0"}};
                auto checked = source;
                checked.update_solid_history_step(h, without_path);
                auto retained =
                    read_history_profiles(*checked.solid_history_definition(node), source.version());
                check(retained.path.has_value() && !retained.metadata_path,
                      "empty metadata uses native path");
                (void)loft;
            }
            change(g.sweep);
            change(g.path);
            change(g.metadata_path);
            for (auto &v : g.sections)
                change(v);
            for (auto &v : g.guides)
                change(v);
            if (auto loft = std::get_if<HistoryLoft>(&op.parameters)) {
                ProfileSpline fresh;
                fresh.degree = 20;
                fresh.control_points = {{0, 0, 0}, {1, 2, 3}, {4, 5, 6}};
                g.sections.push_back(fresh);
                ProfileRegion fresh_region;
                fresh_region.geometry.sat = acis::build_box({5, 6, 7}, 2, 3, 4).to_sat_string();
                g.sections.push_back(fresh_region);
                g.sections.push_back(ProfileRegion{});
                loft->settings->section_counts.insert(loft->settings->section_counts.end(), 3, 1);
            }
            auto before = serialized(edited);
            auto invalid = g;
            if (op.base.kind == SolidHistoryKind::Loft)
                invalid.sections.push_back(
                    ProfileLine{{std::numeric_limits<double>::infinity(), 0, 0}, {}, {0, 0, 1}});
            else
                invalid.sweep = ProfileLine{{std::numeric_limits<double>::infinity(), 0, 0}, {}, {0, 0, 1}};
            rejects([&] { set_history_profiles(op, invalid, source.version()); });
            check(serialized(edited) == before, "failed edit changed document");
            set_history_profiles(op, g, source.version());
            edited.update_solid_history_step(h, op);
            ++changed_count;
        }
    }
    check(changed_count == 4, "four construction operations");
    for (bool binary : {false, true}) {
        std::ofstream out(std::string(argv[2]) + (binary ? ".binary.dxf" : ".ascii.dxf"), std::ios::binary);
        edited.write(out, binary);
    }
    auto wire = edited;
    for (auto h : modelers) {
        auto graph = source.solid_history_graph(h);
        if (!graph)
            continue;
        for (auto node : graph->nodes) {
            auto op = *edited.solid_history_definition(node);
            if (op.base.kind != SolidHistoryKind::Sweep)
                continue;
            auto geometry = read_history_profiles(op, source.version());
            geometry.sweep = ProfileBody{15, "700 0 0 0\nwire $-1 -1 $-1 $-1 #\n"};
            geometry.path = ProfileBody{16, "700 0 0 0\nwire $-1 -1 $-1 $-1 #\n"};
            set_history_profiles(op, geometry, source.version());
            wire.update_solid_history_step(h, op);
        }
    }
    for (bool binary : {false, true}) {
        std::ofstream out(std::string(argv[2]) + (binary ? ".wire.binary.dxf" : ".wire.ascii.dxf"),
                          std::ios::binary);
        wire.write(out, binary);
    }
    check(serialized(source) != serialized(edited), "edits persisted");
    // Invalid framing, bounded known-body fallback, opaque version safety, and exact bit lengths.
    for (int type : {19, 36, 37, 77, 777}) {
        auto v = decode_embedded_profile({type, 1, std::string(1, '\0')}, source.version());
        check(v && std::holds_alternative<ProfileOpaque>(*v), "truncated body fallback");
        auto e = encode_embedded_profile(*v, source.version());
        check(e.bit_count == 1 && e.bytes == std::string(1, '\0'), "opaque exact bits");
        rejects([&] {
            encode_embedded_profile(*v, source.version() == DxfVersion::AC1032 ? DxfVersion::AC1024
                                                                               : DxfVersion::AC1032);
        });
    }
    rejects([&] { decode_embedded_profile({19, 9, std::string(1, '\0')}, source.version()); });
    SolidHistoryOperation op(SolidHistoryKind::Sweep);
    HistoryProfileGeometry body;
    body.sweep = ProfileBody{15, "700 0 0 0\nwire $-1 -1 $-1 $-1 #\n"};
    body.path = ProfileLine{{1, 2, 3}, {4, 5, 6}, {0, 0, 1}};
    set_history_profiles(op, body, source.version());
    auto got = read_history_profiles(op, source.version());
    check(std::get<ProfileBody>(*got.sweep).sat == std::get<ProfileBody>(*body.sweep).sat,
          "wire SAT cipher roundtrip");
    auto canonical = encode_embedded_profile(ProfileRegion{}, source.version());
    canonical.bytes.push_back(char(0x80));
    canonical.bit_count = canonical.bytes.size() * 8;
    check(std::holds_alternative<ProfileOpaque>(*decode_embedded_profile(canonical, source.version())),
          "unknown REGION tail retained");
    auto bad_region = ProfileRegion{};
    bad_region.extra = ProfileAcis{};
    rejects([&] { encode_embedded_profile(bad_region, source.version()); });
    // Knot-less authoring uses Rust's effective degree and clamped knot rule.
    ProfileSpline spline;
    spline.degree = 20;
    spline.control_points = {{0, 0, 0}, {1, 2, 3}, {4, 5, 6}};
    auto generated =
        decode_embedded_profile(encode_embedded_profile(spline, source.version()), source.version());
    auto &sp = std::get<ProfileSpline>(*generated);
    check(sp.degree == 2 && sp.knots == std::vector<double>{0, 0, 0, 1, 1, 1},
          "generated knots/effective degree");
    std::cout << "Embedded history profiles verified\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
