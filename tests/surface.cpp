// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "geometry_operations.hpp"
#include <fstream>
#include <algorithm>
#include <limits>
#include <sstream>
#include <iomanip>
#include <iostream>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream out;
    d.write(out);
    return out.str();
}
static void save(const DxfDocument &d, const std::string &path, bool binary) {
    std::ofstream out(path, std::ios::binary);
    d.write(out, binary);
}
static void append(std::vector<double> &v, Vector3 p) { v.insert(v.end(), {p.x, p.y, p.z}); }
static void append(std::vector<double> &v, const SurfaceMatrix &m) { v.insert(v.end(), m.begin(), m.end()); }
static void append(std::vector<double> &a, const SurfaceSweepOptions &v) {
    a.insert(a.end(), {v.draft_angle, v.draft_start_distance, v.draft_end_distance, v.twist_angle,
                       v.scale_factor, v.align_angle});
    append(a, v.sweep_entity_transform);
    append(a, v.path_entity_transform);
    a.insert(a.end(), {double(v.is_solid), double(v.sweep_alignment_flags), double(v.path_flags),
                       double(v.align_start), double(v.bank), double(v.base_point_set),
                       double(v.sweep_entity_transform_computed), double(v.path_entity_transform_computed)});
    append(a, v.reference_vector);
}
static std::vector<double> values(const DxfModelerEntity &e) {
    auto iso = e.isolines();
    std::vector<double> a{double(iso.first), double(iso.second)};
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ExtrudedSurfaceDefinition>) {
                append(a, v.sweep_vector);
                append(a, v.sweep_transform);
                append(a, v.options);
            } else if constexpr (std::is_same_v<T, LoftedSurfaceDefinition>) {
                append(a, v.loft_transform);
                a.insert(a.end(),
                         {double(v.plane_normal_lofting_type), v.start_draft_angle, v.end_draft_angle,
                          v.start_draft_magnitude, v.end_draft_magnitude,
                          double(v.arc_length_parameterization), double(v.no_twist),
                          double(v.align_direction), double(v.simple_surfaces), double(v.closed_surfaces),
                          double(v.solid), double(v.ruled_surface), double(v.virtual_guide)});
            } else if constexpr (std::is_same_v<T, RevolvedSurfaceDefinition>) {
                append(a, v.axis_point);
                append(a, v.axis_vector);
                a.insert(a.end(), {v.revolve_angle, v.start_angle});
                append(a, v.entity_transform);
                a.insert(a.end(), {v.draft_angle, v.draft_start_distance, v.draft_end_distance, v.twist_angle,
                                   double(v.solid), double(v.close_to_axis)});
            } else if constexpr (std::is_same_v<T, SweptSurfaceDefinition>) {
                append(a, v.sweep_transform);
                append(a, v.path_transform);
                append(a, v.options);
            } else if constexpr (std::is_same_v<T, NurbSurfaceDefinition>) {
                a.insert(a.end(), {double(v.short_170), double(v.cv_hull_display)});
                for (auto p : {v.u_vector1, v.v_vector1, v.u_vector2, v.v_vector2})
                    append(a, p);
            }
        },
        e.surface_definition());
    return a;
}
static void change(DxfModelerEntity &e) {
    auto d = e.surface_definition();
    std::visit(
        [](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ExtrudedSurfaceDefinition>) {
                v.sweep_vector = {9, 8, 7};
                v.options.scale_factor = 2.3;
                v.options.bank = false;
            } else if constexpr (std::is_same_v<T, LoftedSurfaceDefinition>) {
                v.start_draft_angle = .8;
                v.no_twist = false;
            } else if constexpr (std::is_same_v<T, RevolvedSurfaceDefinition>) {
                v.revolve_angle = 1.2;
                v.axis_point = {-1, -2, -3};
            } else if constexpr (std::is_same_v<T, SweptSurfaceDefinition>) {
                v.options.twist_angle = 1.4;
                v.options.align_start = false;
            } else if constexpr (std::is_same_v<T, NurbSurfaceDefinition>) {
                v.cv_hull_display = false;
                v.u_vector2 = {2, 4, 6};
            }
        },
        d);
    e.set_surface_definition(d);
    e.set_isolines(13, 17);
}
static void boundaries() {
    auto d = DxfDocument::create();
    auto h = d.add_modeler_entity(ModelerKind::LoftedSurface, acis::build_box({0, 0, 0}, 1, 2, 3));
    auto before = bytes(d);
    for (int n = 0; n < 3; ++n) {
        bool rejected = false;
        try {
            if (n == 2)
                d.transform_modeler(h, operation(11));
            else
                d.for_each<DxfModelerEntity>([&](auto &e) {
                    if (n == 0)
                        e.set_surface_definition(NurbSurfaceDefinition{});
                    else {
                        auto def = std::get<LoftedSurfaceDefinition>(e.surface_definition());
                        def.loft_transform[4] = std::numeric_limits<double>::infinity();
                        e.set_surface_definition(def);
                    }
                });
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && bytes(d) == before, "surface atomic rejection");
    }
    auto private_text = before;
    private_text.erase(std::remove(private_text.begin(), private_text.end(), '\r'), private_text.end());
    auto marker = private_text.rfind("AcDbLoftedSurface\n");
    check(marker != std::string::npos, "private fixture marker");
    private_text.insert(marker + std::string("AcDbLoftedSurface\n").size(),
                        "102\n{PRIVATE\n40\n999\n70\n777\n102\n}\n");
    auto padded = private_text.find("40\n1\n", marker);
    check(padded != std::string::npos, "matrix padding fixture");
    private_text.replace(padded, 5, "40\n 1  \n");
    std::istringstream private_input(private_text);
    auto private_doc = DxfDocument::read(private_input);
    private_doc.for_each<DxfModelerEntity>([](auto &e) {
        auto value = std::get<LoftedSurfaceDefinition>(e.surface_definition());
        check(value.plane_normal_lofting_type == 0 && value.loft_transform == surface_identity(),
              "private numeric fields leaked into surface definition");
        value.start_draft_angle = .8;
        e.set_surface_definition(value);
    });
    auto saved = bytes(private_doc);
    saved.erase(std::remove(saved.begin(), saved.end(), '\r'), saved.end());
    check(saved.find("{PRIVATE") != std::string::npos && saved.find("999") != std::string::npos &&
              saved.find("777") != std::string::npos,
          "private surface fields lost");
    auto corrupted = before;
    corrupted.erase(std::remove(corrupted.begin(), corrupted.end(), '\r'), corrupted.end());
    auto at = corrupted.rfind("AcDbLoftedSurface");
    check(at != std::string::npos, "loft marker");
    auto begin = corrupted.find("40\n", at);
    check(begin != std::string::npos, "matrix code");
    auto end = corrupted.find('\n', begin + 3);
    corrupted.erase(begin, end + 1 - begin);
    std::istringstream input(corrupted);
    auto bad = DxfDocument::read(input);
    bool rejected = false;
    try {
        bad.for_each<DxfModelerEntity>([](const auto &e) { (void)e.surface_definition(); });
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "truncated matrix accepted");
}
int main(int argc, char **argv) try {
    if (argc == 1) {
        boundaries();
        return 0;
    }
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto source = DxfDocument::read(in);
    std::string prefix = argv[2];
    std::vector<std::uint64_t> targets;
    std::size_t index = 0;
    source.for_each<DxfModelerEntity>([&](const auto &e) {
        if (index++ % 2 == 0)
            targets.push_back(std::stoull(e.handle(), nullptr, 16));
        std::ofstream out(prefix + ".before-" + e.handle() + ".txt");
        out << std::setprecision(17);
        for (double n : values(e))
            out << n << ' ';
    });
    check(index == 10, "surface fixture kinds");
    for (int step : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13}) {
        auto d = source;
        d.for_each<DxfModelerEntity>([&](auto &e) {
            if (std::find(targets.begin(), targets.end(), std::stoull(e.handle(), nullptr, 16)) !=
                targets.end())
                change(e);
        });
        if (step)
            for (auto h : targets)
                d.transform_modeler(h, operation(step));
        for (bool binary : {false, true})
            save(d, prefix + "." + std::to_string(step) + (binary ? ".binary.dxf" : ".ascii.dxf"), binary);
    }
    auto copied = source;
    std::ofstream manifest(prefix + ".copies");
    std::vector<std::uint64_t> copies;
    for (auto h : targets) {
        auto copy = copied.copy_entity(h, EntitySpace::Paper);
        manifest << std::hex << h << ' ' << copy << '\n';
        copies.push_back(copy);
    }
    manifest.close();
    for (bool binary : {false, true})
        save(copied, prefix + (binary ? ".copies.binary.dxf" : ".copies.ascii.dxf"), binary);
    for (auto h : copies)
        check(copied.remove_entity(h), "remove copied surface");
    for (bool binary : {false, true})
        save(copied, prefix + (binary ? ".deleted.binary.dxf" : ".deleted.ascii.dxf"), binary);
    auto created = DxfDocument::create();
    for (auto k : {ModelerKind::ExtrudedSurface, ModelerKind::LoftedSurface, ModelerKind::RevolvedSurface,
                   ModelerKind::SweptSurface, ModelerKind::NurbSurface})
        created.add_modeler_entity(k, acis::build_box({1, 2, 3}, 4, 5, 6));
    save(created, prefix + ".created.dxf", true);
    std::cout << "Specialized surface edits passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
