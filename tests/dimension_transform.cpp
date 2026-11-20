// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "geometry_operations.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace libopencad;
static void check(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream s;
    d.write(s);
    return s.str();
}
static void trace(std::ostream &o, const DxfDimension &e) {
    auto d = e.definition();
    auto b = e.bounding_box();
    o << e.handle();
    auto n = [&](double x) { o << ' ' << x; };
    auto p = [&](Point3 v) {
        n(v.x);
        n(v.y);
        n(v.z);
    };
    p(b.min);
    p(b.max);
    n(e.measurement());
    n(e.stored_measurement());
    n(d.actual_measurement.value());
    n(d.insertion_rotation);
    p(d.definition_point);
    p(d.text_middle_point);
    p(d.insertion_point);
    p(d.normal);
    n(d.text_rotation);
    n(d.horizontal_direction);
    std::visit(
        [&](auto &g) {
            using T = std::decay_t<decltype(g)>;
            if constexpr (std::is_same_v<T, LinearDimension> || std::is_same_v<T, AlignedDimension>) {
                p(g.first_point);
                p(g.second_point);
                n(g.ext_line_rotation);
                if constexpr (std::is_same_v<T, LinearDimension>)
                    n(g.rotation);
            } else if constexpr (std::is_same_v<T, RadiusDimension>) {
                p(g.chord_point);
                n(g.leader_length);
            } else if constexpr (std::is_same_v<T, DiameterDimension>) {
                p(g.angle_vertex);
                n(g.leader_length);
            } else if constexpr (std::is_same_v<T, Angular2LineDimension> ||
                                 std::is_same_v<T, Angular3PointDimension>) {
                p(g.first_point);
                p(g.second_point);
                p(g.angle_vertex);
                if constexpr (std::is_same_v<T, Angular2LineDimension>)
                    p(g.dimension_arc);
            } else if constexpr (std::is_same_v<T, OrdinateDimension>) {
                p(g.feature_location);
                p(g.leader_endpoint);
            } else if constexpr (std::is_same_v<T, ArcDimension>) {
                p(g.first_extension_point);
                p(g.second_extension_point);
                p(g.center_point);
                p(g.first_leader_point);
                p(g.second_leader_point);
                n(g.start_parameter);
                n(g.end_parameter);
            } else {
                p(g.chord_point);
                p(g.override_center);
                p(g.jog_point);
                n(g.jog_angle);
            }
        },
        d.geometry);
    o << '\n';
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto source = DxfDocument::read(in);
    auto original = bytes(source);
    for (int step = 0; step < 14; ++step) {
        auto d = source;
        auto t = operation(step);
        std::size_t count = 0;
        d.for_each<DxfDimension>([&](auto &e) {
            if (e.layer() == "Control")
                return;
            ++count;
            if (step == 0)
                e.translate({3, -4, 5});
            else if (step == 5 || step == 6 || step == 7 || step == 11)
                e.apply_mirror(t);
            else
                e.apply_transform(t);
        });
        check(count == 72, "Missing dimension targets");
        if (step == 7 || step == 11)
            check(bytes(d) == original, "No-op mirror changed document");
        auto path = std::string(argv[2]) + "." + std::to_string(step);
        std::ofstream out(path + ".dxf", std::ios::binary);
        d.write(out);
        out.close();
        check(bool(out), "Save failed");
        std::ofstream values(path + ".trace");
        values << std::setprecision(17);
        d.for_each<DxfDimension>([&](const auto &e) { trace(values, e); });
        values.close();
        check(bool(values), "Trace failed");
    }
    source.for_each<DxfDimension>([&](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(operation(11));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Nonfinite transform accepted");
    });
    check(bytes(source) == original, "Invalid transform mutated document");
    source.for_each<DxfDimension>([&](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(Transform::from_scale(std::numeric_limits<double>::max()));
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "Overflow accepted");
    });
    check(bytes(source) == original, "Overflow partially committed");
    source.for_each<DxfDimension>([](auto &e) {
        e.refresh_measurement();
        auto a = e.definition().actual_measurement.value();
        check(std::abs(a - e.stored_measurement()) < 1e-10, "Refresh cache mismatch");
    });
    auto construct = [](const DimensionDefinition &v) {
        auto doc = DxfDocument::create();
        doc.add_dimension(v);
        DxfDimension result;
        doc.for_each<DxfDimension>([&](const auto &entity) { result = entity; });
        return result;
    };
    DxfDimension e;
    DimensionDefinition v;
    v.geometry = LinearDimension{{0, 0, 0}, {3, 4, 0}, 0, 0};
    e = construct(v);
    check(e.measurement() == 3, "Linear projection");
    v.geometry = AlignedDimension{{0, 0, 0}, {3, 4, 0}, 0};
    e = construct(v);
    check(e.measurement() == 5, "Aligned distance");
    v.geometry = Angular3PointDimension{{1, 0, 0}, {0, 1, 0}, {0, 0, 0}};
    v.definition_point = {1, 1, 0};
    e = construct(v);
    check(std::abs(e.measurement() - 90) < 1e-12, "Angular degrees");
    check(std::abs(e.stored_measurement() - std::acos(-1.) / 2) < 1e-12, "Angular radians");
    v.geometry = ArcDimension{{2, 0, 0}, {2, 0, 0}, {0, 0, 0}, false, false, 0, 2 * std::acos(-1.), {}, {}};
    e = construct(v);
    check(std::abs(e.measurement() - 4 * std::acos(-1.)) < 1e-12, "Full circle arc");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
