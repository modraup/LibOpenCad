// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
namespace {
SplineDefinition definition() {
    SplineDefinition s;
    s.degree = 2;
    s.flags = 0x80c;
    s.knots = {0, 0, 0, 1, 2, 2, 2};
    s.control_points = {{0, 0, 0}, {2, 3, 1}, {4, 2, 2}, {6, 0, 3}};
    s.weights = {1, 2, 3, 1};
    s.fit_points = {{0, 0, 0}, {6, 0, 3}};
    s.knot_tolerance = 1e-7;
    s.control_tolerance = 2e-7;
    s.fit_tolerance = 3e-7;
    s.begin_tangent = {1, 0, 0};
    s.end_tangent = {0, 1, 0};
    return s;
}
HelixParameters parameters() {
    HelixParameters h;
    h.radius = 3;
    h.turns = 4;
    h.turn_height = 2;
    h.axis_base_point = {1, 2, 3};
    h.start_point = {4, 5, 6};
    return h;
}
void verify_definition(const SplineDefinition &s) {
    auto e = definition();
    check(s.degree == e.degree && s.flags == e.flags && s.knots == e.knots && s.weights == e.weights &&
              s.control_points == e.control_points && s.fit_points == e.fit_points,
          "spline arrays");
    check(s.normal == e.normal && s.begin_tangent == e.begin_tangent && s.end_tangent == e.end_tangent &&
              s.knot_tolerance == e.knot_tolerance && s.control_tolerance == e.control_tolerance &&
              s.fit_tolerance == e.fit_tolerance,
          "spline defaults/tolerances");
}
void verify(const DxfDocument &d) {
    int n = 0;
    d.for_each<DxfSpline>([&](const auto &e) {
        ++n;
        verify_definition(e.definition());
    });
    d.for_each<DxfHelix>([&](const auto &e) {
        ++n;
        verify_definition(e.definition());
        auto h = e.parameters();
        check(h.major_version == 29 && h.maintenance_version == 0 && h.axis_base_point == Point3{1, 2, 3} &&
                  h.start_point == Point3{4, 5, 6} && h.axis_vector == Vector3::UNIT_Z && h.radius == 3 &&
                  h.turns == 4 && h.turn_height == 2 && h.handedness &&
                  h.constraint == HelixConstraint::TurnHeight,
              "helix parameters");
    });
    check(n == 4, "spline inventory");
}
void edit(DxfDocument &d, const std::vector<std::string> &ids, bool copy) {
    auto change = [&](auto &e) {
        if (std::find(ids.begin(), ids.end(), e.handle()) == ids.end())
            return;
        auto s = e.definition();
        if (copy)
            s.control_points[0] = {101, 102, 103};
        else {
            s.control_points[1].x += 2;
            s.knots[3] = .5;
            s.weights[1] = .75;
            s.fit_points[0].z = 4;
            s.flags |= 512;
        }
        e.set_definition(s);
    };
    d.for_each<DxfSpline>(change);
    d.for_each<DxfHelix>([&](auto &h) {
        auto p = h.parameters();
        change(h);
        check(h.parameters().start_point == p.start_point && h.parameters().radius == p.radius &&
                  h.parameters().turn_height == p.turn_height,
              "spline edit crossed helix subclass");
        if (copy || std::find(ids.begin(), ids.end(), h.handle()) == ids.end())
            return;
        auto s = h.definition();
        p.start_point = {7, 8, 9};
        p.radius = 9;
        p.turns = 1.5;
        p.maintenance_version = 1;
        p.handedness = false;
        p.constraint = HelixConstraint::Height;
        h.set_parameters(p);
        check(h.definition().control_points == s.control_points && h.definition().knots == s.knots &&
                  h.definition().knot_tolerance == s.knot_tolerance,
              "helix edit crossed spline subclass");
    });
}
void unchanged(Records a, Records b, const std::set<std::string> &excluded = {}) {
    for (const auto &r : a) {
        auto h = identity(r);
        if (!h.empty() && !excluded.count(h))
            check(at(b, h) == r, "unrelated spline record changed");
    }
}
template <class F> void fail(DxfDocument &d, F f) {
    auto before = save(d);
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught && save(d) == before, "atomic spline failure");
}
void boundaries(bool binary) {
    auto d = DxfDocument::create(binary);
    auto s = definition();
    auto h = hex(d.add_helix(s, parameters()));
    fail(d, [&] {
        auto invalid = s;
        invalid.degree = 0;
        d.add_spline(invalid);
    });
    fail(d, [&] {
        auto invalid = s;
        invalid.knots[1] = std::numeric_limits<double>::infinity();
        d.add_spline(invalid);
    });
    fail(d, [&] {
        d.for_each<DxfHelix>([&](auto &e) {
            auto invalid = e.definition();
            invalid.fit_points[0].z = std::numeric_limits<double>::quiet_NaN();
            e.set_definition(invalid);
        });
    });
    fail(d, [&] {
        d.for_each<DxfHelix>([&](auto &e) {
            auto invalid = e.parameters();
            invalid.turn_height = std::numeric_limits<double>::infinity();
            e.set_parameters(invalid);
        });
    });
    auto data = records(save(d), binary);
    auto &r = at(data, h);
    const auto marker = std::find(r.begin(), r.end(), Pair{100, "AcDbSpline"});
    check(marker != r.end(), "spline subclass");
    r.insert(marker + 1, {{102, "{PRIVATE"}, {40, binary ? detail::binary_double(777) : "777"}, {102, "}"}});
    auto protected_doc = read(encode(data, binary));
    protected_doc.for_each<DxfHelix>([&](auto &e) {
        verify_definition(e.definition());
        e.set_definition(e.definition());
    });
    auto result = records(save(protected_doc), binary);
    check(std::find(at(result, h).begin(), at(result, h).end(),
                    Pair{40, binary ? detail::binary_double(777) : "777"}) != at(result, h).end(),
          "private spline data retained");
    auto rational = definition();
    rational.weights.clear();
    auto normalized = DxfDocument::create(binary);
    normalized.add_spline(rational);
    normalized.for_each<DxfSpline>([](const auto &e) {
        check(e.definition().weights == std::vector<double>(4, 1), "missing rational weights default to one");
    });
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        for (auto space : {EntitySpace::Model, EntitySpace::Paper}) {
            EntityOptions o;
            o.space = space;
            made.add_spline(definition(), o);
            made.add_helix(definition(), parameters(), o);
        }
        verify(made);
        write(made, prefix + "-created.dxf");
        std::ifstream input(argv[2], std::ios::binary);
        auto d = DxfDocument::read(input);
        verify(d);
        auto model = d.model_space_entity_handles(), paper = d.paper_space_entity_handles();
        std::set<std::string> removed;
        for (auto &h : model) {
            auto dict = hex(d.add_extension_dictionary(h));
            removed.insert(dict);
            removed.insert(hex(d.add_xrecord(dict, "Data", {{340, h}})));
        }
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        edit(d, model, false);
        auto after = records(save(d), binary);
        unchanged(before, after, {model.begin(), model.end()});
        write(d, prefix + "-edited.dxf");
        std::vector<std::string> copies;
        for (auto &h : model)
            copies.push_back(hex(d.copy_entity(h, EntitySpace::Paper)));
        edit(d, copies, true);
        unchanged(after, records(save(d), binary));
        write(d, prefix + "-copied.dxf");
        for (auto &h : copies)
            check(d.remove_entity(h), "spline copy deletion");
        unchanged(after, records(save(d), binary));
        check(records(save(d), binary).size() == after.size(), "spline copy graph deleted");
        write(d, prefix + "-restored.dxf");
        for (auto &h : model) {
            check(d.remove_entity(h), "spline source deletion");
            removed.insert(h);
        }
        unchanged(before, records(save(d), binary), removed);
        for (auto &h : removed)
            check(!d.get_record(h), "spline removal index");
        check(d.paper_space_entity_handles() == paper && d.reference_issues().empty(), "spline graph valid");
        write(d, prefix + "-deleted.dxf");
        boundaries(binary);
        auto periodic = DxfDocument::create(binary);
        auto compact = definition();
        compact.degree = 3;
        compact.flags = 7;
        compact.knots = {0, 1, 2, 3, 4};
        compact.fit_points.clear();
        compact.weights = {1, 2, 3, 4};
        periodic.add_spline(compact);
        periodic.for_each<DxfSpline>([](const auto &e) {
            const auto s = e.definition();
            check(s.control_points.size() == 7 && s.knots.size() == 11 && s.weights.size() == 7,
                  "periodic cubic writer normalization");
        });
        write(periodic, prefix + "-periodic.dxf");
        auto fit = DxfDocument::create(binary);
        for (int method : {32, 64, 128}) {
            SplineDefinition s;
            s.degree = 3;
            s.flags = method;
            s.fit_points = {{0,0,0}, {1,3,0}, {4,2,1}, {7,0,0}};
            s.end_tangent = {1,-2,0};
            fit.add_spline(s);
        }
        SplineDefinition repeated;
        repeated.fit_points = {{0,0,0}, {1,2,3}, {1,2,3}};
        auto coincident = DxfDocument::create(binary);
        coincident.add_spline(repeated);
        coincident.for_each<DxfSpline>([](const auto &e) {
            auto s = e.definition();
            check(s.control_points.empty() && s.knots.empty() && (s.flags & 1056) == 1056,
                  "coincident fit points retain their method without a fabricated net");
        });
        // Strip the generated net to exercise the ordinary read/save entry point on older input.
        auto fit_only = records(save(fit), binary);
        for (auto &record : fit_only) {
            if (record.front().second != "SPLINE") continue;
            record.erase(std::remove_if(record.begin(), record.end(), [](const auto &p) {
                return p.first == 10 || p.first == 20 || p.first == 30 || p.first == 40;
            }), record.end());
            for (auto &[code, value] : record)
                if (code == 72 || code == 73) value = binary ? detail::binary_short(0) : "0";
        }
        auto compact_input = read(encode(fit_only, binary));
        write(compact_input, prefix + "-fit.dxf");
        compact_input.for_each<DxfSpline>([](const auto &s) {
            check(s.definition().control_points.empty(), "writing a fit spline leaves the source untouched");
        });
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
