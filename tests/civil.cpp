// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include "../src/dwg_edit_internal.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static std::string dwg_bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static DwgDocument parse_dwg(const std::string &s) {
    std::istringstream i(s);
    return DwgDocument::read(i);
}
static void write_file(const std::string &path, const std::string &bytes) {
    std::ofstream(path, std::ios::binary) << bytes;
}
static DwgDocument seeded() {
    auto d = parse_dwg(create_dwg(DxfVersion::AC1032, {}));
    return parse_dwg(detail::seed_dwg_civil_point_styles(dwg_bytes(d), d.objects()));
}
static std::uint64_t handle_of(const DwgDocument &d, std::uint32_t number) {
    for (auto &[h, e] : d.objects().arch_entities)
        if (d.cogo_point_number(h) == number)
            return h;
    throw std::runtime_error("missing COGO point");
}
static std::uint64_t group_named(const DwgDocument &d, std::string_view name) {
    for (auto &[h, obj] : d.objects().arch_objects)
        if (obj.dxf_name == "AECC_POINT_PG") {
            auto strings = obj.decode().strings;
            if (!strings.empty() && strings.front() == name)
                return h;
        }
    throw std::runtime_error("missing point group");
}
static std::uint64_t all_points(const DwgDocument &d) {
    for (auto &[h, obj] : d.objects().arch_objects)
        if (obj.dxf_name == "AECC_POINT_PG") {
            auto strings = obj.decode().strings;
            if (std::find(strings.begin(), strings.end(), "AeccImpAllPointsPG") != strings.end())
                return h;
        }
    throw std::runtime_error("missing _All Points group");
}
static bool near(Vector3 a, Vector3 b) { return (a - b).length() < 1e-9; }
int main(int argc, char **argv) try {
    if (argc != 2)
        return 2;
    const std::string prefix = argv[1];
    {
        auto d = parse_dwg(create_dwg(DxfVersion::AC1032, {}));
        auto before = dwg_bytes(d);
        bool refused = false;
        try {
            d.add_cogo_point(std::nullopt, {1, 2, 3});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "blank drawing without styles is refused");
        check(dwg_bytes(d) == before, "failed add left a drawing without styles unchanged");
    }
    {
        auto d = parse_dwg(create_dwg(DxfVersion::AC1018, {}));
        auto before = dwg_bytes(d);
        bool refused = false;
        try {
            d.add_cogo_point(1, {1, 2, 3});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "AC1018 civil construction is refused");
        check(dwg_bytes(d) == before, "failed AC1018 add left the drawing unchanged");
    }
    auto d = seeded();
    d.add_cogo_point(1, {100, 200, 10}, std::string("TEST"));
    d.add_cogo_point(std::nullopt, {150, 250, 12});
    d.add_cogo_point(5, {200, 210, 14}, std::string("TEST"));
    auto before = dwg_bytes(d);
    bool refused = false;
    try {
        d.add_cogo_point(5, {});
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "duplicate point number is refused");
    check(dwg_bytes(d) == before, "duplicate add left the drawing unchanged");
    refused = false;
    try {
        d.add_cogo_point(std::nullopt, {std::numeric_limits<double>::quiet_NaN(), 0, 0});
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "NaN position is refused");
    check(dwg_bytes(d) == before, "NaN add left the drawing unchanged");
    auto loaded = parse_dwg(dwg_bytes(d));
    auto points = loaded.cogo_points();
    check(points.size() == 3, "created point count");
    check(points[0].number == 1 && near(points[0].position, {100, 200, 10}) && points[0].description == "TEST",
          "point 1");
    check(points[1].number == 2 && near(points[1].position, {150, 250, 12}) && !points[1].description, "point 2");
    check(points[2].number == 5 && near(points[2].position, {200, 210, 14}) && points[2].description == "TEST",
          "point 5");
    check(points[0].style && points[0].label_style && points[1].style && points[2].style, "default styles");
    check(loaded.cogo_point_number(handle_of(loaded, 1)) == 1, "COGO entity is classified as an architecture entity");
    auto all = loaded.point_group_numbers(all_points(loaded));
    check(all == std::vector<std::uint32_t>({1, 2, 5}), "_All Points members");
    write_file(prefix + "-created.dwg", dwg_bytes(loaded));

    auto p2 = handle_of(loaded, 2);
    loaded.set_cogo_point(p2, 7, {1, 2, 3}, std::string("X"));
    loaded.remove_cogo_point(handle_of(loaded, 1));
    auto updated = parse_dwg(dwg_bytes(loaded));
    points = updated.cogo_points();
    check(points.size() == 2, "updated point count");
    check(points[0].number == 7 && near(points[0].position, {1, 2, 3}) && points[0].description == "X", "renumbered 2");
    check(points[1].number == 5 && points[1].description == "TEST", "remaining 5");
    check(handle_of(updated, 7) == p2, "renumber keeps the entity handle");
    check(updated.point_group_numbers(all_points(updated)) == std::vector<std::uint32_t>({5, 7}),
          "_All Points after remove");
    write_file(prefix + "-updated.dwg", dwg_bytes(updated));

    auto groups = seeded();
    for (std::uint32_t n = 1; n <= 6; ++n)
        groups.add_cogo_point(n, {10.0 * n, 5, static_cast<double>(n)});
    auto g = groups.add_point_group("G", std::string("even"), {2, 4, 6, 8});
    before = dwg_bytes(groups);
    refused = false;
    try {
        groups.add_point_group("g", std::nullopt, {1});
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "case-insensitive duplicate group is refused");
    check(dwg_bytes(groups) == before, "duplicate group left the drawing unchanged");
    auto grouped = parse_dwg(dwg_bytes(groups));
    check(group_named(grouped, "G") == g, "group handle survives round-trip");
    check(grouped.point_group_numbers(g) == std::vector<std::uint32_t>({2, 4, 6}), "group G members without 8");
    write_file(prefix + "-groups.dwg", dwg_bytes(grouped));

    grouped.add_cogo_point(8, {1, 1, 1});
    grouped.remove_cogo_point(handle_of(grouped, 4));
    grouped.set_point_group(g, "H", std::nullopt, {1, 8});
    auto renamed = parse_dwg(dwg_bytes(grouped));
    check(renamed.point_group_numbers(g) == std::vector<std::uint32_t>({1, 8}), "renamed group H members");
    check(renamed.point_group_numbers(all_points(renamed)) == std::vector<std::uint32_t>({1, 2, 3, 5, 6, 8}),
          "_All Points after group edits");
    write_file(prefix + "-groups-updated.dwg", dwg_bytes(renamed));

    auto queries = seeded();
    const char *desc[] = {"TEST", "test", "TEXT", "ABC", "TE", "TESTS"};
    const double zs[] = {10, 11, 20, 26, 30, 12};
    for (int i = 0; i < 6; ++i)
        queries.add_cogo_point(static_cast<std::uint32_t>(i + 1), {10.0 * i, 0, zs[i]}, std::string(desc[i]));
    auto text = [](std::vector<std::string> p) { return PointQuery::from_raw_description(std::move(p)); };
    struct Case {
        const char *name;
        PointQuery query;
        std::vector<std::uint32_t> want;
    };
    Case cases[] = {
        {"GA", text({"TEST"}), {1, 2}},
        {"GB", text({"te*"}), {1, 2, 3, 5, 6}},
        {"GJ", text({"T?ST"}), {1, 2}},
        {"GK", text({"TEST", "ABC"}), {1, 2, 4}},
        {"GE", PointQuery::from_elevation(Comparison::Greater, 25), {4, 5}},
        {"GF", PointQuery::from_elevation(Comparison::Less, 12), {1, 2}},
        {"GG", PointQuery::from_elevation(Comparison::GreaterOrEqual, 26), {4, 5}},
        {"GH", PointQuery::from_elevation(Comparison::LessOrEqual, 11), {1, 2}},
        {"GI", PointQuery::from_elevation(Comparison::Equal, 20), {3}},
    };
    std::vector<std::uint64_t> qh;
    for (auto &c : cases)
        qh.push_back(queries.add_point_group_query(c.name, std::nullopt, c.query));
    {
        auto before_q = dwg_bytes(queries);
        bool bad = false;
        try {
            queries.add_point_group_query("X", std::nullopt, text({"A,B"}));
        } catch (const std::invalid_argument &) {
            bad = true;
        }
        check(bad, "comma in description pattern is refused");
        check(dwg_bytes(queries) == before_q, "bad description pattern left the drawing unchanged");
        bad = false;
        try {
            queries.add_point_group_query("X", std::nullopt, text({"A#"}));
        } catch (const std::invalid_argument &) {
            bad = true;
        }
        check(bad, "hash in description pattern is refused");
        check(dwg_bytes(queries) == before_q, "hash pattern left the drawing unchanged");
        bad = false;
        try {
            queries.add_point_group_query("X", std::nullopt,
                                         PointQuery::from_elevation(Comparison::Greater,
                                                                    std::numeric_limits<double>::quiet_NaN()));
        } catch (const std::invalid_argument &) {
            bad = true;
        }
        check(bad, "NaN elevation query is refused");
        check(dwg_bytes(queries) == before_q, "NaN elevation left the drawing unchanged");
    }
    auto loaded_q = parse_dwg(dwg_bytes(queries));
    for (std::size_t i = 0; i < 9; ++i)
        check(loaded_q.point_group_numbers(qh[i]) == cases[i].want, cases[i].name);
    write_file(prefix + "-queries.dwg", dwg_bytes(loaded_q));

    loaded_q.set_cogo_point(handle_of(loaded_q, 4), 4, {30, 0, 5}, std::string("ABC"));
    loaded_q.add_cogo_point(7, {70, 0, 40}, std::string("abc"));
    loaded_q.set_point_group_query(qh[0], "GA", std::nullopt, PointQuery::from_elevation(Comparison::Less, 10.5));
    auto updated_q = parse_dwg(dwg_bytes(loaded_q));
    check(updated_q.point_group_numbers(qh[4]) == std::vector<std::uint32_t>({5, 7}), "GE after elevation edit");
    check(updated_q.point_group_numbers(qh[3]) == std::vector<std::uint32_t>({1, 2, 4, 7}), "GK after description join");
    check(updated_q.point_group_numbers(qh[0]) == std::vector<std::uint32_t>({1, 4}), "GA became elevation < 10.5");
    write_file(prefix + "-queries-updated.dwg", dwg_bytes(updated_q));

    auto alignments = parse_dwg(create_dwg(DxfVersion::AC1032, {}));
    const std::vector<Vector2> path{{0, 0}, {100, 0}, {200, 50}};
    {
        auto before = dwg_bytes(alignments);
        bool refused = false;
        try {
            alignments.add_alignment("AL1", {path[0]});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "one vertex is refused");
        check(dwg_bytes(alignments) == before, "failed alignment left the drawing unchanged");
        refused = false;
        try {
            alignments.add_alignment("AL1", {{0, 0}, {0, 0}});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "coincident vertices are refused");
        check(dwg_bytes(alignments) == before, "coincident vertices left the drawing unchanged");
        refused = false;
        try {
            alignments.add_alignment("AL1", {{std::numeric_limits<double>::quiet_NaN(), 0}, {1, 0}});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "NaN vertex is refused");
        check(dwg_bytes(alignments) == before, "NaN alignment left the drawing unchanged");
        refused = false;
        try {
            alignments.add_alignment("AL1", path, std::string("missing"));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "missing layer is refused");
        check(dwg_bytes(alignments) == before, "missing-layer alignment left the drawing unchanged");
    }
    {
        auto early = parse_dwg(create_dwg(DxfVersion::AC1018, {}));
        auto before = dwg_bytes(early);
        bool refused = false;
        try {
            early.add_alignment("AL1", {{0, 0}, {1, 0}});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "AC1018 alignment construction is refused");
        check(dwg_bytes(early) == before, "failed AC1018 alignment left the drawing unchanged");
    }
    auto first = alignments.add_alignment("AL1", path);
    auto second = alignments.add_alignment("AL2", {path[1], path[2]});
    auto loaded_al = parse_dwg(dwg_bytes(alignments));
    check(loaded_al.alignment_name(first) == "AL1", "alignment AL1 name");
    check(loaded_al.alignment_start_station(first) == 0.0, "alignment start station");
    auto al1 = loaded_al.alignment_polylines(first);
    check(al1.size() == 1 && al1.front().size() == 3, "AL1 polyline size");
    check(al1.front()[0] == path[0] && al1.front()[1] == path[1] && al1.front()[2] == path[2], "AL1 vertices");
    auto al2 = loaded_al.alignment_polylines(second);
    check(al2.size() == 1 && al2.front().size() == 2, "AL2 polyline size");
    check(al2.front()[0] == path[1] && al2.front()[1] == path[2], "AL2 vertices");
    check(loaded_al.architecture_entity(first) && loaded_al.architecture_entity(second),
          "alignments are architecture entities");
    std::size_t companions = 0, styles = 0, siteless = 0, point_styles = 0;
    for (auto &[h, obj] : loaded_al.objects().arch_objects) {
        (void)h;
        if (obj.dxf_name == "AECC_FEATURE_STYLE") {
            auto strings = obj.decode().strings;
            if (!strings.empty() && strings.front().size() == 32)
                ++companions;
        }
        if (obj.dxf_name == "AECC_ALIGNMENT_STYLE")
            ++styles;
        if (obj.dxf_name == "AECC_ALIGNMENT_PARCEL_NODE")
            ++siteless;
        if (obj.dxf_name == "AECC_POINT_STYLE")
            ++point_styles;
    }
    check(companions == 2, "two siteless companions");
    check(styles >= 2, "alignment style collection");
    check(siteless == 1, "siteless node");
    check(point_styles >= 1, "new drawing got a point style tree");
    write_file(prefix + "-alignments.dwg", dwg_bytes(loaded_al));

    auto fresh = parse_dwg(create_dwg(DxfVersion::AC1032, {}));
    auto h = fresh.add_alignment("AL1", {{0, 0}, {100, 0}});
    auto loaded_new = parse_dwg(dwg_bytes(fresh));
    auto run = loaded_new.alignment_polylines(h);
    check(run.size() == 1 && run.front().size() == 2 && run.front()[0] == Vector2{0, 0} &&
              run.front()[1] == Vector2{100, 0},
          "new-drawing alignment polyline");
    bool has_style = false, has_siteless = false;
    for (auto &[id, obj] : loaded_new.objects().arch_objects) {
        (void)id;
        if (obj.dxf_name == "AECC_ALIGNMENT_STYLE")
            has_style = true;
        if (obj.dxf_name == "AECC_ALIGNMENT_PARCEL_NODE")
            has_siteless = true;
    }
    check(has_style && has_siteless, "new drawing got an alignment style and siteless node");
    write_file(prefix + "-alignments-new.dwg", dwg_bytes(loaded_new));

    const std::vector<Vector2> cv{{0, 0}, {100, 0}, {200, -50}, {300, -50}, {350, 50}};
    auto none = VertexCurve::none();
    auto arc50 = VertexCurve::arc(50);
    auto arc40 = VertexCurve::arc(40);
    auto scs = VertexCurve::spiral_curve_spiral(20, 50, 20);
    auto curves = parse_dwg(create_dwg(DxfVersion::AC1032, {}));
    {
        auto before = dwg_bytes(curves);
        bool refused = false;
        try {
            curves.add_alignment_with_curves("X", cv, {arc50});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "wrong curve count is refused");
        check(dwg_bytes(curves) == before, "wrong curve count left the drawing unchanged");
        refused = false;
        try {
            curves.add_alignment_with_curves("X", cv, {VertexCurve::arc(5000), none, none});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "too-large arc is refused");
        check(dwg_bytes(curves) == before, "too-large arc left the drawing unchanged");
        refused = false;
        try {
            curves.add_alignment_with_curves("X", cv, {VertexCurve::arc(-5), none, none});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "negative radius is refused");
        check(dwg_bytes(curves) == before, "negative radius left the drawing unchanged");
        refused = false;
        try {
            curves.add_alignment_with_curves("X", {cv[0], cv[1], {200, 0}}, {VertexCurve::arc(10)});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "collinear arc is refused");
        check(dwg_bytes(curves) == before, "collinear arc left the drawing unchanged");
    }
    auto curve_al1 = curves.add_alignment_with_curves("AL1", cv, {arc50, none, arc40});
    auto curve_al2 = curves.add_alignment_with_curves("AL2", {cv[0], cv[1], cv[2]}, {scs});
    auto loaded_cv = parse_dwg(dwg_bytes(curves));
    auto el1 = loaded_cv.alignment_elements(curve_al1);
    check(el1 && el1->size() == 6, "AL1 has six elements");
    check((*el1)[0].id == 1 && (*el1)[0].previous == 0 && (*el1)[0].next == 5, "AL1 line 1 links");
    check((*el1)[1].id == 2 && (*el1)[1].previous == 5 && (*el1)[1].next == 3, "AL1 line 2 links");
    check((*el1)[2].id == 3 && (*el1)[2].previous == 2 && (*el1)[2].next == 6, "AL1 line 3 links");
    check((*el1)[3].id == 4 && (*el1)[3].previous == 6 && (*el1)[3].next == 0, "AL1 line 4 links");
    check((*el1)[4].id == 5 && (*el1)[4].previous == 1 && (*el1)[4].next == 2, "AL1 arc 5 links");
    check((*el1)[5].id == 6 && (*el1)[5].previous == 3 && (*el1)[5].next == 4, "AL1 arc 6 links");
    check((*el1)[4].kind == AlignmentElementKind::free_arc(50), "AL1 first arc radius");
    check((*el1)[0].kind == AlignmentElementKind::fixed_line(cv[0], cv[1]), "AL1 first line");
    auto pieces1 = loaded_cv.alignment_pieces(curve_al1);
    check(pieces1.size() == 6, "AL1 six pieces");
    check(pieces1[0].first == AlignmentPiece::Line && pieces1[1].first == AlignmentPiece::Curve &&
              pieces1[2].first == AlignmentPiece::Line && pieces1[3].first == AlignmentPiece::Line &&
              pieces1[4].first == AlignmentPiece::Curve && pieces1[5].first == AlignmentPiece::Line,
          "AL1 piece kinds");
    auto t = 50.0 * std::tan(std::atan(0.5) / 2.0);
    check(!pieces1[1].second.empty() && std::abs(pieces1[1].second.front().x - (100.0 - t)) < 1e-9,
          "first arc starts at its tangent point");
    auto el2 = loaded_cv.alignment_elements(curve_al2);
    check(el2 && el2->size() == 3, "AL2 has three elements");
    check((*el2)[2].kind == AlignmentElementKind::free_scs(20, 50, 20), "AL2 SCS parameters");
    auto pieces2 = loaded_cv.alignment_pieces(curve_al2);
    check(pieces2.size() == 5, "AL2 five pieces");
    check(pieces2[0].first == AlignmentPiece::Line && pieces2[1].first == AlignmentPiece::Spiral &&
              pieces2[2].first == AlignmentPiece::Curve && pieces2[3].first == AlignmentPiece::Spiral &&
              pieces2[4].first == AlignmentPiece::Line,
          "AL2 SCS piece kinds");
    write_file(prefix + "-alignments-curves.dwg", dwg_bytes(loaded_cv));

    auto profiles = parse_dwg(create_dwg(DxfVersion::AC1032, {}));
    auto pal = profiles.add_alignment("AL1", {{0, 0}, {100, 0}});
    const std::vector<Vector2> pvis{{0, 10}, {50, 20}, {100, 15}};
    {
        auto before = dwg_bytes(profiles);
        bool refused = false;
        try {
            profiles.add_profile(pal, "X", pvis, {});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "wrong profile curve count is refused");
        check(dwg_bytes(profiles) == before, "wrong profile curve count left the drawing unchanged");
        refused = false;
        try {
            profiles.add_profile(pal, "X", pvis, {120});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "too-long parabola is refused");
        check(dwg_bytes(profiles) == before, "too-long parabola left the drawing unchanged");
        refused = false;
        try {
            profiles.add_profile(pal, "X", {pvis[1], pvis[0]}, {});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "decreasing stations are refused");
        check(dwg_bytes(profiles) == before, "decreasing stations left the drawing unchanged");
    }
    auto prof1 = profiles.add_profile(pal, "P1", pvis, {0});
    auto prof2 = profiles.add_profile(pal, "P2", pvis, {20});
    auto loaded_pr = parse_dwg(dwg_bytes(profiles));
    auto k1 = loaded_pr.profile_pieces(prof1);
    check(k1.size() == 2 && k1[0].first == ProfilePiece::Tangent && k1[1].first == ProfilePiece::Tangent,
          "P1 two tangents");
    auto k2 = loaded_pr.profile_pieces(prof2);
    check(k2.size() == 3 && k2[0].first == ProfilePiece::Tangent && k2[1].first == ProfilePiece::Parabola &&
              k2[2].first == ProfilePiece::Tangent,
          "P2 tangent-parabola-tangent");
    check(loaded_pr.profile_name(prof2) == "P2", "P2 name");
    check(!k2[0].second.empty() && std::abs(k2[0].second.back().x - 40.0) < 1e-9 &&
              std::abs(k2[0].second.back().y - 18.0) < 1e-9,
          "P2 first tangent ends at the parabola");
    auto al_handles = loaded_pr.architecture_entity(pal)->decode().handles;
    check(al_handles.size() >= 8 && al_handles[6] == prof1 && al_handles[7] == prof2, "alignment lists both profiles");
    check(loaded_pr.alignment_name(pal) == "AL1", "alignment name after profiles");
    {
        auto before = dwg_bytes(loaded_pr);
        bool refused = false;
        try {
            loaded_pr.add_profile(prof1, "P3", pvis, {0});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "profile handle is not an alignment");
        check(dwg_bytes(loaded_pr) == before, "non-alignment profile left the drawing unchanged");
    }
    write_file(prefix + "-profiles.dwg", dwg_bytes(loaded_pr));

    auto fresh_pr = parse_dwg(create_dwg(DxfVersion::AC1032, {}));
    auto fal = fresh_pr.add_alignment("AL1", {{0, 0}, {100, 0}});
    auto fp = fresh_pr.add_profile(fal, "P1", {{0, 10}, {100, 15}}, {});
    auto loaded_fp = parse_dwg(dwg_bytes(fresh_pr));
    check(loaded_fp.profile_name(fp) == "P1", "new-drawing profile name");
    bool has_profile_style = false, has_extension = false;
    std::uint64_t style_h = 0;
    for (auto &[hid, obj] : loaded_fp.objects().arch_objects) {
        if (obj.dxf_name == "AECC_PROFILE_STYLE") {
            has_profile_style = true;
            auto hs = obj.decode().handles;
            if (hs.size() > 7) {
                auto ext = loaded_fp.architecture_object(hs[7]);
                if (ext && ext->dxf_name == "AECC_VALIGNMENT_STYLE_EXTENSION") {
                    has_extension = true;
                    style_h = hid;
                }
            }
        }
    }
    check(has_profile_style && has_extension, "new drawing got a profile style and extension");
    auto ph = loaded_fp.architecture_entity(fp)->decode().handles;
    check(ph.size() > 11 && ph[11] == style_h, "profile refers to the default style");
    write_file(prefix + "-profiles-new.dwg", dwg_bytes(loaded_fp));

    auto pcurves = parse_dwg(create_dwg(DxfVersion::AC1032, {}));
    auto cal = pcurves.add_alignment("AL1", {{0, 0}, {300, 0}});
    const std::vector<Vector2> cpvis{{0, 10}, {50, 20}, {100, 15}, {150, 25}, {250, 18}};
    const std::vector<VerticalCurve> vcurves{VerticalCurve::circular(50), VerticalCurve::asymmetric(8, 12),
                                            VerticalCurve::parabola(20)};
    {
        auto before = dwg_bytes(pcurves);
        bool refused = false;
        try {
            pcurves.add_profile_with_curves(cal, "X", cpvis,
                                           {VerticalCurve::circular(2000), VerticalCurve::none(), VerticalCurve::none()});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "too-large circular curve is refused");
        check(dwg_bytes(pcurves) == before, "too-large circular curve left the drawing unchanged");
        refused = false;
        try {
            pcurves.add_profile_with_curves(cal, "X", cpvis,
                                           {VerticalCurve::asymmetric(0, 1), VerticalCurve::none(), VerticalCurve::none()});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "zero-length asymmetric curve is refused");
        check(dwg_bytes(pcurves) == before, "zero-length asymmetric curve left the drawing unchanged");
    }
    auto cp = pcurves.add_profile_with_curves(cal, "P1", cpvis, vcurves);
    auto loaded_pc = parse_dwg(dwg_bytes(pcurves));
    auto pieces = loaded_pc.profile_pieces(cp);
    check(pieces.size() == 7, "circular/asymmetric/parabola seven pieces");
    check(pieces[0].first == ProfilePiece::Tangent && pieces[1].first == ProfilePiece::Circular &&
              pieces[2].first == ProfilePiece::Tangent && pieces[3].first == ProfilePiece::AsymmetricParabola &&
              pieces[4].first == ProfilePiece::Tangent && pieces[5].first == ProfilePiece::Parabola &&
              pieces[6].first == ProfilePiece::Tangent,
          "mixed vertical-curve kinds");
    check(!pieces[0].second.empty() && std::abs(pieces[0].second.back().x - 42.663583344) < 1e-6,
          "circular curve start station");
    check(!pieces[3].second.empty() && std::abs(pieces[3].second.front().x - 92.0) < 1e-9 &&
              std::abs(pieces[3].second.back().x - 112.0) < 1e-9,
          "asymmetric parabola span");
    write_file(prefix + "-profiles-curves.dwg", dwg_bytes(loaded_pc));

    const std::vector<Vector3> tin_pts{{0, 0, 0}, {10, 0, 1}, {0, 10, 2}};
    auto surfaces = parse_dwg(create_dwg(DxfVersion::AC1032, {}));
    {
        auto before = dwg_bytes(surfaces);
        bool refused = false;
        try {
            surfaces.add_tin_surface("S", {tin_pts[0], tin_pts[1]});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "two TIN points are refused");
        check(dwg_bytes(surfaces) == before, "failed TIN left the drawing unchanged");
        refused = false;
        try {
            surfaces.add_tin_surface("S", {tin_pts[0], tin_pts[1], {0, 0, 5}});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "coincident-in-plan TIN points are refused");
        check(dwg_bytes(surfaces) == before, "coincident TIN left the drawing unchanged");
        refused = false;
        try {
            surfaces.add_tin_surface("S", {{std::numeric_limits<double>::quiet_NaN(), 0, 0}, tin_pts[1], tin_pts[2]});
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "NaN TIN point is refused");
        check(dwg_bytes(surfaces) == before, "NaN TIN left the drawing unchanged");
        refused = false;
        try {
            surfaces.add_tin_surface("S", tin_pts, std::string("missing"));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "missing-layer TIN is refused");
        check(dwg_bytes(surfaces) == before, "missing-layer TIN left the drawing unchanged");
    }
    {
        auto early = parse_dwg(create_dwg(DxfVersion::AC1018, {}));
        auto before = dwg_bytes(early);
        bool refused = false;
        try {
            early.add_tin_surface("S", tin_pts);
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "AC1018 TIN construction is refused");
        check(dwg_bytes(early) == before, "failed AC1018 TIN left the drawing unchanged");
    }
    auto tin_h = surfaces.add_tin_surface("S", tin_pts);
    auto loaded_tin = parse_dwg(dwg_bytes(surfaces));
    auto tin = loaded_tin.tin_surface(tin_h);
    check(tin.has_value(), "TIN surface decoded");
    check(tin->vertices.size() == 7, "TIN has four corners plus three points");
    auto drawn = tin->visible_triangles();
    check(drawn.size() == 1, "one visible TIN triangle");
    auto contains = [&](Vector3 p) {
        return drawn.front()[0] == p || drawn.front()[1] == p || drawn.front()[2] == p;
    };
    check(contains(tin_pts[0]) && contains(tin_pts[1]) && contains(tin_pts[2]), "visible triangle holds the points");
    check(static_cast<bool>(loaded_tin.architecture_entity(tin_h)), "TIN is an architecture entity");
    bool has_surface_style = false;
    std::size_t surface_style_strings = 0;
    bool has_hatch = false;
    for (auto &[id, obj] : loaded_tin.objects().arch_objects) {
        (void)id;
        if (obj.dxf_name == "AECC_SURFACE_STYLE") {
            has_surface_style = true;
            auto strings = obj.decode().strings;
            if (strings.size() > surface_style_strings)
                surface_style_strings = strings.size();
            for (auto &s : strings)
                if (s.size() >= 7 && s.compare(s.size() - 7, 7, ";ANSI31") == 0)
                    has_hatch = true;
        }
    }
    check(has_surface_style, "new drawing got a surface style");
    check(surface_style_strings == 1 + 17 + 2 * 25, "surface style string table");
    check(has_hatch, "surface style hatch ANSI31");
    write_file(prefix + "-tin.dwg", dwg_bytes(loaded_tin));

    auto dense = parse_dwg(create_dwg(DxfVersion::AC1032, {}));
    const std::vector<Vector3> ten{{0, 0, 0},   {10, 0, 1},  {0, 10, 2},  {10, 10, 3}, {20, 0, 1.5},
                                   {20, 10, 2.5}, {5, 5, 4},  {15, 5, 3.5}, {0, 20, 2},  {20, 20, 5}};
    auto dense_h = dense.add_tin_surface("T", ten);
    auto loaded_dense = parse_dwg(dwg_bytes(dense));
    auto dense_tin = loaded_dense.tin_surface(dense_h);
    check(dense_tin.has_value() && dense_tin->vertices.size() == 14, "ten-point TIN has fourteen vertices");
    check(dense_tin->visible_triangles().size() == 11, "ten-point TIN drawn triangles");
    write_file(prefix + "-tin-dense.dwg", dwg_bytes(loaded_dense));
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
