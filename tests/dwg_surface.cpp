// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include <libopencad/dwg_sections.hpp>
#include "../src/dwg_sections_internal.hpp"
#include "../src/dwg_edit_internal.hpp"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <limits>
#include <iomanip>
using namespace libopencad;
static void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
static std::string bytes(const DwgDocument &d) {
    std::ostringstream out;
    d.write(out);
    return out.str();
}
static void save(const DwgDocument &d, const std::string &name) {
    std::ofstream out(name, std::ios::binary);
    d.write(out);
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
static std::vector<double> values(const DwgSurface &e) {
    auto iso = std::make_pair(e.u_isolines, e.v_isolines);
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
        e.definition);
    return a;
}
static void change(DwgSurface &e, DxfVersion version) {
    auto d = e.definition;
    std::visit(
        [&](auto &v) {
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
                if (version < DxfVersion::AC1027)
                    return;
                v.cv_hull_display = false;
                v.u_vector2 = {2, 4, 6};
            }
        },
        d);
    e.definition = d;
    e.u_isolines = 13;
    e.v_isolines = 17;
}

static void edit_profiles(SurfaceProfiles &p) {
    auto edit = [](EmbeddedProfile &e) {
        if (auto l = std::get_if<ProfileLine>(&e))
            l->start = {11, 12, 13};
    };
    if (p.profile)
        edit(*p.profile);
    if (p.path)
        edit(*p.path);
    for (auto &e : p.sections)
        edit(e);
    for (auto &e : p.guides)
        edit(e);
}
static void malformed(const DwgDocument &doc) {
    const auto source = bytes(doc);
    auto object_data = read_dwg_paged_section(source, "AcDb:AcDbObjects");
    auto it = std::find_if(doc.objects().modelers.begin(), doc.objects().modelers.end(),
                           [](const auto &pair) { return pair.second.surface->type == "EXTRUDEDSURFACE"; });
    check(it != doc.objects().modelers.end(), "Extruded fixture");
    auto h = it->first;
    const auto &rec = doc.objects().records.at(h);
    DwgBitReader r(std::string_view(object_data).substr(rec.body_offset, rec.body_size));
    r.seek(it->second.surface->parameters_begin_bit);
    r.bit_short();
    r.bit_short();
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
    check(d.objects().modeler_errors.count(h) && d.objects().modelers.size() == 13,
          "Malformed surface isolated by handle");
    check(bytes(d) == damaged, "Malformed typed payload retained verbatim");
}
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument("input prefix");
        std::ifstream in(argv[1], std::ios::binary);
        auto d = DwgDocument::read(in);
        auto source = bytes(d);
        auto version = dxf_version_from_string(source.substr(0, 6));
        auto prefix = std::string(argv[2]);
        for (auto &[h, error] : d.objects().modeler_errors)
            std::cerr << h << ": " << error << '\n';
        if (!d.objects().classes_error.empty())
            std::cerr << d.objects().classes_error << '\n';
        check(d.objects().modelers.size() == 14, "Fourteen surfaces decoded");
        std::vector<std::uint64_t> edited;
        std::ofstream ids(prefix + ".edited");
        malformed(d);
        // Force a valid metadata-only loft by clearing the native span alone.
        for (auto &item : d.objects().modelers)
            if (item.second.surface->type == "LOFTEDSURFACE") {
                auto fallback = d;
                auto h = item.first;
                auto value = fallback.surface(h);
                ProfileLine line;
                line.start = {1, 2, 3};
                line.end = {4, 5, 6};
                value.profiles = {};
                value.profiles.sections = {line};
                fallback.update_surface(h, value);
                value.profiles = {};
                auto raw = detail::edit_dwg_surface(bytes(fallback), fallback.objects(), h, value);
                std::istringstream stream(raw);
                fallback = DwgDocument::read(stream);
                check(fallback.surface(h).profiles.sections.size() == 1, "Metadata-only loft restored");
                save(fallback, prefix + ".fallback.dwg");
                std::ofstream id(prefix + ".fallback-id");
                id << h;
                break;
            }
        auto original = d.objects().modelers;
        for (auto &[h, m] : original) {
            auto s = d.surface(h);
            std::ofstream trace(prefix + ".before-" + std::to_string(h));
            trace << std::setprecision(17);
            for (auto v : values(s))
                trace << v << "\n";
            d.update_surface(h, s);
        }
        save(d, prefix + ".same.dwg");
        for (auto &[h, m] : original) {
            if (m.properties.owner != d.objects().model_space)
                continue;
            edited.push_back(h);
            ids << h << '\n';
            auto s = d.surface(h);
            change(s, version);
            edit_profiles(s.profiles);
            d.update_surface(h, s);
        }
        check(edited.size() == 7, "Seven model-space edits");
        save(d, prefix + ".edited.dwg");
        auto checkpoint = bytes(d);
        for (auto h : edited) {
            auto s = d.surface(h);
            s.type = "OTHER";
            bool fail = false;
            try {
                d.update_surface(h, s);
            } catch (const std::exception &) {
                fail = true;
            }
            check(fail && bytes(d) == checkpoint, "Class replacement is atomic");
            s = d.surface(h);
            if (s.definition.index())
                s.definition = std::monostate{};
            else
                s.definition = ExtrudedSurfaceDefinition{};
            fail = false;
            try {
                d.update_surface(h, s);
            } catch (const std::invalid_argument &) {
                fail = true;
            }
            check(fail && bytes(d) == checkpoint, "Parameter variant mismatch rejects atomically");
            s = d.surface(h);
            s = d.surface(h);
            s.profiles.revolve_class_version = 7;
            fail = false;
            try {
                d.update_surface(h, s);
            } catch (const std::exception &) {
                fail = true;
            }
            check(fail && bytes(d) == checkpoint, "Unrepresentable prefixes reject atomically");
            if (auto x = std::get_if<ExtrudedSurfaceDefinition>(&s.definition)) {
                s.profiles.revolve_class_version = 0;
                x->options.scale_factor = std::numeric_limits<double>::infinity();
                fail = false;
                try {
                    d.update_surface(h, s);
                } catch (const std::exception &) {
                    fail = true;
                }
                check(fail && bytes(d) == checkpoint, "Nonfinite parameters reject");
            }
            if (version < DxfVersion::AC1027 && s.type == "NURBSURFACE") {
                s.profiles.revolve_class_version = 0;
                std::get<NurbSurfaceDefinition>(s.definition).short_170 = 1;
                fail = false;
                try {
                    d.update_surface(h, s);
                } catch (const std::exception &) {
                    fail = true;
                }
                check(fail && bytes(d) == checkpoint, "Old NURB boundary");
            }
        }
        auto empty = d;
        for (auto h : edited)
            if (empty.surface(h).type == "LOFTEDSURFACE") {
                auto value = empty.surface(h);
                value.profiles = {};
                empty.update_surface(h, value);
            }
        save(empty, prefix + ".empty-profiles.dwg");
        for (auto h : edited)
            d.clear_modeler_geometry(h);
        save(d, prefix + ".cleared.dwg");
        for (auto h : edited)
            d.set_modeler_geometry(h, acis::build_sphere({3, 4, 5}, 6));
        save(d, prefix + ".refilled.dwg");
        std::cout << "DWG surface parameters, profiles and primary modeler edits verified\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
