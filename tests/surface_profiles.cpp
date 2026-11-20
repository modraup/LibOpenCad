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
    d.write(out, false);
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
    std::ifstream in(argv[1], std::ios::binary);
    const auto source = DxfDocument::read(in);
    auto original = serialized(source);
    std::vector<DxfModelerEntity> entities;
    source.for_each<DxfModelerEntity>([&](const auto &e) { entities.push_back(e); });
    for (std::string mode : {"edited", "body", "clear", "legacy"}) {
        auto d = source;
        unsigned changed = 0;
        for (const auto &e : entities) {
            auto handle = std::stoull(e.handle(), nullptr, 16);
            if (std::stoull(e.handle(), nullptr, 16) % 2)
                continue;
            if (e.kind() == ModelerKind::NurbSurface) {
                rejects([&] { e.surface_profiles(); });
                continue;
            }
            auto p = e.surface_profiles();
            if (e.kind() == ModelerKind::ExtrudedSurface) {
                const auto &poly = std::get<ProfilePolyline>(*p.profile);
                check(poly.closed && poly.vertices.size() == 4 && poly.vertices[3].bulge == -.5,
                      "polyline profile");
                check(poly.vertices[0].id == (d.version() >= DxfVersion::AC1024 ? 10 : 0),
                      "versioned vertex ids");
            }
            if (e.kind() == ModelerKind::RevolvedSurface) {
                auto a = std::get<ProfileArc>(*p.profile);
                check(a.center == Vector3{3, 4, 5} && a.normal == Vector3{0, 1, 0}, "arc OCS");
            }
            if (e.kind() == ModelerKind::LoftedSurface) {
                check(p.sections.size() == 13 && p.guides.size() == 2 && p.path, "loft lists");
                check(std::holds_alternative<ProfileRegion>(p.sections[10]), "region decoded");
                check(std::get<ProfileOpaque>(p.sections[11]).bit_count == 13,
                      "loft exact non-byte bit count");
                check(std::holds_alternative<ProfileOpaque>(p.sections[12]), "truncated curve retained");
            }
            auto before = serialized(d);
            auto invalid = p;
            if (e.kind() == ModelerKind::LoftedSurface)
                invalid.sections.push_back(ProfileLine{{INFINITY, 0, 0}, {}, {0, 0, 1}});
            else
                invalid.profile = ProfileLine{{INFINITY, 0, 0}, {}, {0, 0, 1}};
            rejects([&] { d.set_surface_profiles(handle, invalid); });
            check(before == serialized(d), "failed edit changed entity");
            if (mode == "edited") {
                change(p.profile);
                change(p.path);
                for (auto &v : p.sections)
                    change(v);
                for (auto &v : p.guides)
                    change(v);
                if (e.kind() == ModelerKind::LoftedSurface) {
                    p.sections.erase(p.sections.begin());
                    std::swap(p.sections[0], p.sections[1]);
                    p.sections.push_back(ProfileLine{{9, 8, 7}, {6, 5, 4}, {0, 0, 1}});
                    std::reverse(p.guides.begin(), p.guides.end());
                }
            } else {
                p = {};
                ProfileBody body{77, "700 0 0 0\nwire $-1 -1 $-1 $-1 #\n"};
                if (mode == "body") {
                    if (e.kind() == ModelerKind::LoftedSurface) {
                        p.sections = {body, body};
                        p.guides = {body};
                        p.path = body;
                    } else {
                        p.profile = body;
                        if (e.kind() == ModelerKind::SweptSurface)
                            p.path = body;
                    }
                }
                if (mode == "legacy") {
                    if (e.kind() == ModelerKind::RevolvedSurface) {
                        p.revolve_class_version = 7;
                        p.revolve_entity_id = 42;
                    }
                    if (e.kind() == ModelerKind::SweptSurface) {
                        p.swept_legacy_class_version = 11;
                        p.profile = body;
                        p.path = ProfileLine{{1, 2, 3}, {4, 5, 6}, {0, 0, 1}};
                    }
                }
            }
            d.set_surface_profiles(handle, p);
            auto read = d.surface_profiles(handle);
            if (mode == "clear")
                check(!read.profile && !read.path && read.sections.empty() && read.guides.empty(),
                      "clear profiles");
            if (mode == "legacy" && e.kind() == ModelerKind::SweptSurface)
                check(read.swept_legacy_class_version == 11, "old sweep header");
            if (mode == "legacy" && e.kind() == ModelerKind::RevolvedSurface)
                check(read.revolve_class_version == 7 && read.revolve_entity_id == 42, "old revolve header");
            ++changed;
        }
        check(changed == 4, "four edited surfaces");
        for (bool binary : {false, true}) {
            auto file = std::string(argv[2]) + "." + mode + (binary ? ".binary.dxf" : ".ascii.dxf");
            {
                std::ofstream out(file, std::ios::binary);
                d.write(out, binary);
            }
            std::ifstream check_input(file, std::ios::binary);
            auto reopened = DxfDocument::read(check_input);
            check(reopened.version() == source.version(), "reopen version");
        }
    }
    // Malformed framing remains available as raw data; edits reject atomically.
    for (int mode = 0; mode < 3; ++mode) {
        auto raw = records(original, false);
        for (auto &r : raw)
            if (r.front().second == "EXTRUDEDSURFACE") {
                auto marker = std::find(r.begin(), r.end(), Pair{100, "AcDbExtrudedSurface"});
                check(marker != r.end(), "subclass marker");
                auto pos = std::size_t(marker - r.begin()) + 1;
                if (mode == 0)
                    r.insert(r.begin() + pos, {{102, "{VENDOR"}, {90, "42"}, {102, "}"}});
                if (mode == 1)
                    r[pos + 1].second = "999999999";
                if (mode == 2)
                    r.insert(r.end(), {{102, "{VENDOR"}, {90, "42"}, {102, "}"}});
            }
        auto d = read(encode(raw, false));
        auto before = serialized(d);
        for (const auto &e : entities) {
            if (e.kind() != ModelerKind::ExtrudedSurface)
                continue;
            auto h = std::stoull(e.handle(), nullptr, 16);
            if (mode == 1) {
                rejects([&] { d.surface_profiles(h); });
                rejects([&] { d.set_surface_profiles(h, {}); });
            } else {
                auto p = d.surface_profiles(h);
                if (mode == 0)
                    rejects([&] { d.set_surface_profiles(h, p); });
                else {
                    change(p.profile);
                    d.set_surface_profiles(h, p);
                    auto mixed = p;
                    mixed.sections.push_back(ProfilePoint{});
                    rejects([&] { d.set_surface_profiles(h, mixed); });
                }
            }
        }
        if (mode != 2)
            check(before == serialized(d), "rejected edit modified raw data");
        else {
            auto got = records(serialized(d), false);
            for (auto &r : got)
                if (r.front().second == "EXTRUDEDSURFACE")
                    check(r[r.size() - 3] == Pair{102, "{VENDOR"} && r[r.size() - 2] == Pair{90, "42"} &&
                              r.back() == Pair{102, "}"},
                          "private option data preserved");
        }
    }
    // Resolve a legacy metadata-only loft and transcode known profiles from R2018.
    auto fallback_raw = records(original, false);
    std::string loft_handle;
    for (auto &r : fallback_raw)
        if (r.front().second == "LOFTEDSURFACE" && std::stoull(identity(r), nullptr, 16) % 2 == 0) {
            loft_handle = identity(r);
            auto marker = std::find(r.begin(), r.end(), Pair{100, "AcDbLoftedSurface"});
            auto begin = marker + 17;
            auto end = begin;
            for (auto it = begin; it != r.end(); ++it)
                if (it->first == 70)
                    end = it;
            check(end > begin, "loft options boundary");
            r.erase(begin, end);
        }
    check(!loft_handle.empty(), "model loft handle");
    auto fallback = read(encode(fallback_raw, false));
    auto lh = std::stoull(loft_handle, nullptr, 16);
    auto profiles = fallback.surface_profiles(lh);
    check(profiles.sections.size() == 13 && profiles.guides.size() == 2 && profiles.path,
          "metadata fallback geometry");
    check(std::get<ProfilePolyline>(profiles.sections[5]).vertices[0].id == 10,
          "fallback uses R2018 encoding");
    if (source.version() != DxfVersion::AC1032) {
        auto before = serialized(fallback);
        rejects([&] { fallback.set_surface_profiles(lh, profiles); });
        check(serialized(fallback) == before, "opaque fallback transcode atomic rejection");
        profiles.sections.erase(
            std::remove_if(profiles.sections.begin(), profiles.sections.end(),
                           [](auto &v) { return std::holds_alternative<ProfileOpaque>(v); }),
            profiles.sections.end());
    }
    std::get<ProfilePoint>(profiles.sections[0]).location.x += 2;
    fallback.set_surface_profiles(lh, profiles);
    check(std::get<ProfilePoint>(fallback.surface_profiles(lh).sections[0]).location.x == 4,
          "fallback edit persisted");
    for (bool binary : {false, true}) {
        std::ofstream out(std::string(argv[2]) + ".fallback" + (binary ? ".binary.dxf" : ".ascii.dxf"),
                          std::ios::binary);
        fallback.write(out, binary);
    }
    // The legacy sweep header and old group-90 path marker decode independently.
    auto old_raw = records(original, false);
    for (auto &r : old_raw)
        if (r.front().second == "SWEPTSURFACE") {
            auto marker = std::find(r.begin(), r.end(), Pair{100, "AcDbSweptSurface"});
            auto pos = marker - r.begin() + 1;
            r.insert(r.begin() + pos, {90, "11"});
            for (auto it = r.begin() + pos; it != r.end(); ++it)
                if (it->first == 91) {
                    it->first = 90;
                    break;
                }
        }
    auto old = read(encode(old_raw, false));
    for (auto &e : entities)
        if (e.kind() == ModelerKind::SweptSurface) {
            auto h = std::stoull(e.handle(), nullptr, 16);
            auto p = old.surface_profiles(h);
            check(p.swept_legacy_class_version == 11 && p.profile && p.path, "old sweep curve framing");
            old.set_surface_profiles(h, p);
            check(old.surface_profiles(h).swept_legacy_class_version == 11, "old sweep header retained");
        }
    // An external reactor to the fallback must prevent a partial native edit.
    auto dict = source.extension_dictionary_handle(loft_handle);
    auto entry = source.dictionary_entry(dict, "CADCODEC_LOFT_CURVES_V1");
    check(!entry.empty(), "loft fallback record");
    auto shared_raw = records(original, false);
    inject(shared_raw, entities.front().handle(), {entry});
    auto shared = read(encode(shared_raw, false));
    auto before = serialized(shared);
    rejects([&] { shared.set_surface_profiles(lh, {}); });
    check(serialized(shared) == before, "shared fallback rollback");
    auto alias_raw = records(original, false);
    for (auto &r : alias_raw)
        if (identity(r) == dict)
            r.insert(r.end(), {{3, "OTHER_ROLE"}, {360, entry}});
    auto alias = read(encode(alias_raw, false));
    auto alias_before = serialized(alias);
    rejects([&] { alias.set_surface_profiles(lh, {}); });
    check(serialized(alias) == alias_before, "aliased fallback rollback");
    rejects([&] { shared.surface_profiles(0xFFFFFFFF); });
    check(original == serialized(source), "source modified");
    std::cout << "Specialized surface profiles verified\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
