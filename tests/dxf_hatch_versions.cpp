// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
int main(int argc, char **argv) {
    try {
        check(argc == 3, "format prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[2];
        auto source = DxfDocument::create(binary);
        auto boundary = source.add_line({0, 0, 0}, {3, 0, 0});
        HatchDefinition h;
        h.associative = true;
        HatchSplineEdge s;
        s.degree = 2;
        s.rational = true;
        s.knots = {0, 0, 0, 1, 1, 1};
        s.control_points = {{0, 0, 1}, {1, 2, .5}, {3, 0, 1}};
        HatchPath path;
        path.edges = {s};
        path.boundary_handles = {boundary};
        h.paths.push_back(path);
        s.fit_points = {{0, 0}, {3, 0}};
        s.start_tangent = {1, 1};
        s.end_tangent = {1, -1};
        path.edges = {s};
        h.paths.push_back(path);
        const auto hatch_handle = source.add_hatch(h);
        auto raw = records(save(source), binary);
        inject(raw, hex(boundary), {hex(hatch_handle)});
        source = read(encode(raw, binary));
        write(source, prefix + "-source.dxf");
        const auto before = save(source);
        DxfConversionOptions allow;
        allow.allow_loss = true;
        for (auto v : {DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015, DxfVersion::AC1018,
                       DxfVersion::AC1021, DxfVersion::AC1024, DxfVersion::AC1027, DxfVersion::AC1032}) {
            auto result = source.export_version(v, binary, allow);
            check(result.report.can_write, "hatch target output");
            auto d = read(result.bytes);
            d.for_each<DxfHatch>([&](const auto &hatch) {
                auto got = hatch.definition();
                check(got.paths.size() == 2, "two paths");
                for (std::size_t i = 0; i < 2; ++i) {
                    check(got.paths[i].boundary_handles == path.boundary_handles,
                          "boundary count not consumed as fit count");
                    auto e = std::get<HatchSplineEdge>(got.paths[i].edges.at(0));
                    check(e.knots == s.knots && e.control_points == s.control_points,
                          "rational control geometry");
                    check(e.fit_points.size() == (v >= DxfVersion::AC1024 && i == 1 ? 2 : 0),
                          "versioned fit data");
                }
            });
            check(d.reference_issues().empty(), "association graph");
            write(d, prefix + "-" + std::string(dxf_version_string(v)) + ".dxf");
            auto up = d.export_version(DxfVersion::AC1032, binary, allow);
            check(up.report.can_write, "hatch upgrade");
            read(up.bytes).for_each<DxfHatch>([&](const auto &e) {
                for (auto &p : e.definition().paths)
                    check(p.boundary_handles == path.boundary_handles, "upgrade boundary associations");
            });
            if (v < DxfVersion::AC1024) {
                auto strict = source.export_version(v, binary);
                check(!strict.report.can_write &&
                          std::any_of(strict.report.issues.begin(), strict.report.issues.end(),
                                      [](const auto &i) { return i.code == "hatch-fit-data-removed"; }),
                      "explicit fit loss refusal");
            }
        }
        check(save(source) == before, "immutable source");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
