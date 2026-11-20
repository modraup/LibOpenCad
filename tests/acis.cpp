// SPDX-License-Identifier: MPL-2.0
#include <libopencad/acis.hpp>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
using namespace libopencad::acis;
static void check(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
static std::string read(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Missing SAT input");
    return {std::istreambuf_iterator<char>(f), {}};
}
static void save(const std::string &path, const std::string &data) {
    std::ofstream f(path, std::ios::binary);
    f << data;
    if (!f)
        throw std::runtime_error("Cannot save SAT output");
}
static void trace(const SatDocument &d, const std::string &path) {
    std::ostringstream out;
    out << std::setprecision(17);
    auto p = [&](SatPointer p) { out << p.value << ' '; };
    auto xyz = [&](Vector3 p) { out << p.x << ' ' << p.y << ' ' << p.z << ' '; };
    for (const auto &r : d.records) {
        out << r.index << ' ';
        if (auto v = SatBody::from_record(r)) {
            p(v->next_body);
            p(v->lump);
            p(v->wire_body);
            p(v->transform);
        }
        if (auto v = SatLump::from_record(r)) {
            p(v->next_lump);
            p(v->shell);
            p(v->body);
        }
        if (auto v = SatShell::from_record(r)) {
            p(v->next_shell);
            p(v->subshell);
            p(v->face);
            p(v->wire);
            p(v->lump);
        }
        if (auto v = SatFace::from_record(r)) {
            p(v->attribute);
            p(v->next_face);
            p(v->first_loop);
            p(v->shell);
            p(v->subshell);
            p(v->surface);
            out << (v->sense == Sense::Reversed) << ' ' << (v->sidedness == Sidedness::Double) << ' ';
        }
        if (auto v = SatLoop::from_record(r)) {
            p(v->next_loop);
            p(v->first_coedge);
            p(v->face);
        }
        if (auto v = SatCoedge::from_record(r)) {
            p(v->next);
            p(v->prev);
            p(v->partner);
            p(v->edge);
            out << (v->sense == Sense::Reversed) << ' ';
            p(v->owner_loop);
            p(v->pcurve);
        }
        if (auto v = SatEdge::from_record(r)) {
            p(v->start_vertex);
            out << v->start_param << ' ';
            p(v->end_vertex);
            out << v->end_param << ' ';
            p(v->coedge);
            p(v->curve);
            out << (v->sense == Sense::Reversed) << ' ';
        }
        if (auto v = SatVertex::from_record(r)) {
            p(v->edge);
            p(v->point);
        }
        if (auto v = SatPoint::from_record(r))
            xyz(v->position);
        if (auto v = SatStraightCurve::from_record(r)) {
            xyz(v->root_point);
            xyz(v->direction);
        }
        if (auto v = SatEllipseCurve::from_record(r)) {
            xyz(v->center);
            xyz(v->normal);
            xyz(v->major_axis);
            out << v->ratio << ' ';
        }
        if (auto v = SatPlaneSurface::from_record(r)) {
            xyz(v->root_point);
            xyz(v->normal);
            xyz(v->u_direction);
            out << (v->sense == Sense::Reversed) << ' ';
        }
        if (auto v = SatConeSurface::from_record(r)) {
            xyz(v->center);
            xyz(v->axis);
            xyz(v->major_axis);
            out << v->ratio << ' ' << v->sin_half_angle << ' ' << v->cos_half_angle << ' ' << v->radius << ' '
                << (v->sense == Sense::Reversed) << ' ';
        }
        if (auto v = SatSphereSurface::from_record(r)) {
            xyz(v->center);
            out << v->radius << ' ';
            xyz(v->u_direction);
            xyz(v->pole);
            out << (v->sense == Sense::Reversed) << ' ';
        }
        if (auto v = SatTorusSurface::from_record(r)) {
            xyz(v->center);
            xyz(v->normal);
            out << v->major_radius << ' ' << v->minor_radius << ' ';
            xyz(v->u_direction);
            out << (v->sense == Sense::Reversed) << ' ';
        }
        out << '\n';
    }
    const auto placement = d.placement();
    for (auto row : placement.matrix)
        for (auto v : row)
            out << v << ' ';
    xyz(placement.translation);
    out << placement.scale << ' ' << d.validate().size() << '\n';
    save(path, out.str());
}
static void boundaries() {
    std::vector<Vector3> tetra{{1, 2, 3}, {5, 2, 3}, {1, 8, 3}, {1, 2, 11}};
    const std::vector<std::vector<std::size_t>> faces{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};
    check(build_planar_body(tetra, faces).has_value(), "Closed planar B-rep construction");
    check(!build_planar_body(tetra, {{0, 1, 2}}), "Open planar body accepted");
    check(!build_planar_body(tetra, {{0, 1, 99}}), "Out-of-range planar vertex");
    check(!build_planar_body(tetra, {{0, 0, 1}}), "Repeated planar vertex");
    tetra[3].z = std::numeric_limits<double>::quiet_NaN();
    check(!build_planar_body(tetra, faces), "Nonfinite planar vertex");

    for (auto bad :
         {"", "not a SAT header", "700\n@1 X @1 X @1 X", "700\n-2147483648 point $-1 -1 $-1 0 0 0 #"}) {
        bool failed = false;
        try {
            SatDocument::parse(bad);
        } catch (const SatParseError &) {
            failed = true;
        }
        check(failed, "Malformed SAT accepted");
    }
    auto d =
        SatDocument::parse("700 0 0 0\n1 x 1 y 1 z\n1 1e-6\n-7 point $-1 -1 $-1 1 2 3 #\nEnd-of-ACIS-data");
    check(d.record(7) && !d.record(0), "Explicit record resolution");
    check(!d.resolve({-1}), "Negative pointer resolved");
    check(d.record(7)->token_float(3) == 3, "Point Z");
    auto legacy = SatDocument::parse("400 0 0 0\n1 x 1 y 1 z\n1 1e-6\neye_refinement $-1 5 grid  1 3 tri 1 "
                                     "10 end_fields #\nEnd-of-ACIS-data");
    check(legacy.records[0].tokens[0].as_string() == "grid ", "Legacy counted label");
    auto old = d.to_sat_string();
    auto invalid = d.placement();
    invalid.scale = std::numeric_limits<double>::infinity();
    bool failed = false;
    try {
        d.set_placement(invalid);
    } catch (const std::invalid_argument &) {
        failed = true;
    }
    check(failed && d.to_sat_string() == old, "Placement rollback");
    SatRecord malformed;
    malformed.index = 0;
    malformed.entity_type = "vertex";
    SatRecord edge;
    edge.index = 1;
    edge.entity_type = "edge";
    edge.tokens = {SatToken(SatPointer{}), SatToken(SatPointer{0}),
                   SatToken(0.0),          SatToken(SatPointer{}),
                   SatToken(0.0),          SatToken(SatPointer{}),
                   SatToken(SatPointer{}), SatToken(SatEnum{"forward"})};
    d.records = {malformed, edge};
    old = d.to_sat_string();
    failed = false;
    try {
        d.complete_brep_links();
    } catch (const std::invalid_argument &) {
        failed = true;
    }
    check(failed && d.to_sat_string() == old, "Malformed link completion rollback");
    d.records[1].tokens[1] = SatToken(SatPointer{100});
    auto issues = d.validate();
    check(issues.size() == 1 && issues[0].record_index == 1 && issues[0].context == "token[1]",
          "Pointer diagnostics");
    SatToken integer(std::int64_t(9007199254740993LL));
    check(integer.as_integer() == 9007199254740993LL, "Integer precision");
    SatToken high(double(INT64_MAX));
    check(high.as_integer() == INT64_MAX, "Saturating Rust float conversion");
    for (const auto &bytes : std::vector<std::vector<std::uint8_t>>{
             {}, {1, 2, 3}, {'A', 'C', 'I', 'S', ' ', 'B', 'i', 'n', 'a', 'r', 'y', 'F', 'i', 'l', 'e'}}) {
        bool rejected = false;
        try {
            SabReader::read(bytes);
        } catch (const SabError &e) {
            rejected = true;
            check(e.offset <= bytes.size(), "SAB error offset");
        }
        check(rejected, "Malformed SAB header accepted");
    }
    auto empty = SabWriter::write(SatDocument{});
    auto corrupt = empty;
    corrupt[31] = 255;
    bool rejected = false;
    try {
        SabReader::read(corrupt);
    } catch (const SabError &e) {
        rejected = e.offset == 31;
    }
    check(rejected, "Unknown SAB header tag location");
    for (std::size_t n = 15; n < 31; ++n) {
        auto short_header = std::vector<std::uint8_t>(empty.begin(), empty.begin() + n);
        bool failed = false;
        try {
            SabReader::read(short_header);
        } catch (const SabError &) {
            failed = true;
        }
        check(failed, "Truncated SAB header");
    }
    SatToken invalid_utf8(SabToken{7, {2, 255, 254}});
    check(!invalid_utf8.as_string() && !invalid_utf8.as_ident(), "Invalid SAB UTF-8 projection");
    SatDocument raw_document;
    SatRecord raw_record;
    raw_record.entity_type = "unknown";
    raw_record.tokens = {invalid_utf8};
    raw_document.add_record(raw_record);
    check(raw_document.to_sat_string().find("<sab:07:3>") != std::string::npos,
          "Raw malformed SAB text placeholder");
    auto raw_bytes = SabWriter::write(raw_document);
    auto decoded = SabReader::read(raw_bytes);
    check(std::get<SabToken>(decoded.records[0].tokens[0].value).data ==
              std::vector<std::uint8_t>({2, 255, 254}),
          "Malformed SAB string raw preservation");
    SatToken malformed_sab(SabToken{6, {1}});
    check(!malformed_sab.as_float(), "Malformed SAB projection");
    SatRecord v;
    v.entity_type = "vertex";
    v.tokens = {SatToken(SatPointer{}), SatToken(SatPointer{7}), SatToken(std::int64_t(123)),
                SatToken(SatPointer{9})};
    check(SatVertex::from_record(v)->point.value == 9, "ASM pointer ordinal");
    SatRecord grouped;
    grouped.entity_type = "point";
    grouped.tokens = {SatToken(SatPointer{}), SatToken(Vector3{1, 2, 3})};
    check(SatPoint::from_record(grouped)->position == Vector3{1, 2, 3}, "Grouped coordinate view");
}
int main(int argc, char **argv) {
    try {
        boundaries();
        if (argc == 1) {
            std::cout << "SAT parser and topology boundary cases passed\n";
            return 0;
        }
        check(argc == 3, "Expected input and output prefix");
        const std::string prefix = argv[2];
        if (std::filesystem::path(argv[1]).extension() == ".sab") {
            auto source = read(argv[1]);
            std::vector<std::uint8_t> bytes(source.begin(), source.end());
            auto [d, n] = SabReader::read_with_consumed(bytes);
            check(n == bytes.size(), "Raw SAB consumed bytes");
            trace(d, prefix + ".trace");
            save(prefix + ".sat", d.to_sat_string());
            auto b = SabWriter::write(d);
            save(prefix + ".sab", std::string(b.begin(), b.end()));
            std::cout << "Raw SAB token preservation verified\n";
            return 0;
        }
        auto d = SatDocument::parse(read(argv[1]));
        if (d.header.version.major < 7) {
            auto b = SabWriter::write(d);
            save(prefix + ".upgrade.sab", std::string(b.begin(), b.end()));
            auto upgraded = d;
            upgraded.upgrade_to_v700();
            save(prefix + ".upgrade.sat", upgraded.to_sat_string());
        }

        const auto name = std::filesystem::path(argv[1]).filename().string();
        std::optional<SatDocument> built;
        if (name.rfind("box-", 0) == 0)
            built = build_box({1, 2, 3}, 4, 6, 8);
        if (name.rfind("wedge-", 0) == 0)
            built = build_wedge({-2, 3, 1}, 5, 6, 7);
        if (name.rfind("pyramid-", 0) == 0)
            built = build_pyramid({2, -3, 4}, 8, 9);
        if (name.rfind("cylinder-", 0) == 0)
            built = build_cylinder({1, -2, 3}, 4, 5);
        if (name.rfind("cone-", 0) == 0)
            built = build_cone({2, 3, 4}, 5, 6);
        if (name.rfind("sphere-", 0) == 0)
            built = build_sphere({-1, 2, 3}, 4);
        if (name.rfind("torus-", 0) == 0)
            built = build_torus({1, 2, -3}, 7, 2);
        if (name.rfind("planar-", 0) == 0)
            built = build_planar_body({{1, 2, 3}, {5, 2, 3}, {1, 8, 3}, {1, 2, 11}},
                                      {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}});
        if (built) {
            check(built->validate().empty(), "Constructed B-rep pointers");
            built->header.version = d.header.version;
            save(prefix + ".built.sat", built->to_sat_string());
            if (built->header.version.major >= 7) {
                auto b = SabWriter::write(*built);
                save(prefix + ".built.sab", std::string(b.begin(), b.end()));
            }
        }

        if (d.header.version.major >= 7) {
            auto output = SabWriter::write(d);
            save(prefix + ".sab", std::string(output.begin(), output.end()));
            auto path = std::filesystem::path(argv[1]);
            path.replace_extension("sab");
            auto source = read(path.string());
            std::vector<std::uint8_t> bytes(source.begin(), source.end());
            auto [binary, consumed] = SabReader::read_with_consumed(bytes);
            check(consumed == bytes.size(), "SAB consumed bytes");
            bytes.push_back(0xde);
            bytes.push_back(0xad);
            check(SabReader::read_with_consumed(bytes).second == consumed, "SAB embedded payload boundary");
            output = SabWriter::write(binary);
            save(prefix + ".saved.sab", std::string(output.begin(), output.end()));
            save(prefix + ".sab.sat", binary.to_sat_string());
            trace(binary, prefix + ".sab.trace");
        }
        trace(d, prefix + ".trace");
        save(prefix + ".sat", d.to_sat_string());
        auto moved = d;
        SatPlacement place;
        place.matrix = {{{0, -1, 0}, {1, 0.25, 0}, {0, 0, -1}}};
        place.translation = {10, -20, 30};
        place.scale = 2;
        moved.set_placement(place);
        moved.complete_brep_links();
        check(moved.placement().apply({1, 2, 3}) == Vector3{14, -21, 24}, "SAT row vector placement");
        save(prefix + ".moved.sat", moved.to_sat_string());
        auto stripped = d;
        stripped.strip_for_sab();
        save(prefix + ".stripped.sat", stripped.to_sat_string());
        auto split = d.split_bodies();
        save(prefix + ".count", std::to_string(split.size()));
        for (std::size_t i = 0; i < split.size(); ++i)
            save(prefix + ".split-" + std::to_string(i) + ".sat", split[i].to_sat_string());
        std::cout << "SAT parse, typed topology, placement, completion and splitting verified\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
