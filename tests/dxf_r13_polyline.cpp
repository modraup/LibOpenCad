// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <map>

namespace {
bool has_issue(const DxfConversionReport &r, const std::string &code) {
    return std::any_of(r.issues.begin(), r.issues.end(), [&](const auto &i) { return i.code == code; });
}
DxfDocument fixture(bool binary) {
    auto d = DxfDocument::create(binary);
    d.add_layer("轮廓", 3);
    EntityOptions o;
    o.layer = "轮廓";
    o.color = Color::from_index(3);
    const auto first =
        hex(d.add_lwpolyline({{0, 0, .25, 1, 2, 11}, {8, 0, 0, 2, 3, 12}, {8, 6, -.5, 0, 4, 13}}, true, o));
    d.add_lwpolyline({{12, 1, 0, 0, 0, 0}, {16, 2, .75, 0, 0, 0}, {21, 4, 0, 0, 0, 0}}, false, o);
    const auto heavy = hex(d.add_polyline2d({{30, 0, 0, .2, .3, 0}, {32, 3, .5, .4, .5, 0}}, true, 3, o));
    o.space = EntitySpace::Paper;
    d.add_lwpolyline({{1, 2, .5, 0, 0, 0}, {4, 6, -.25, 0, 3, 0}, {7, 5, .75, 2, 0, 0}}, true, o);
    d.add_symbol(SymbolTable::BlockRecord, "子块");
    o = {};
    o.layer = "轮廓";
    o.block_name = "子块";
    d.add_lwpolyline({{1, 1, 0, 0, 0, 0}, {3, 1, -1, 0, 0, 0}}, false, o);
    const auto dict = hex(d.add_extension_dictionary(first));
    d.add_xrecord(dict, "引用", {{1, "中文数据"}, {340, first}, {440, "33554560"}});
    d.set_extended_data(
        first, {"测试应用", {{1000, std::string("中文扩展")}, {1005, std::stoull(heavy, nullptr, 16)}}});
    d.for_each<DxfLwPolyline>([&](auto &e) {
        e.set_elevation(4.5);
        e.set_thickness(.75);
        e.set_normal({0, .6, .8});
        e.set_plinegen(true);
        if (e.handle() != first)
            e.set_constant_width(1.25);
    });
    auto data = records(save(d), binary);
    // A high existing object identity and a stale HANDSEED exercise reconciliation.
    const auto high = hex(d.add_line({100, 0, 0}, {101, 0, 0}));
    data = records(save(d), binary);
    for (auto &p : at(data, high))
        if (p.first == 5)
            p.second = "F000";
    for (auto &r : data)
        for (std::size_t i = 1; i < r.size(); ++i)
            if (r[i - 1] == Pair{9, "$HANDSEED"})
                r[i].second = "10";
    inject(data, first, {heavy});
    auto &p = at(data, first);
    // Embedded application group values resemble geometry; they must survive.
    Record hidden{{102, "{PRIVATE"}, {90, "999"}, {42, "12.5"}, {102, "}"}};
    if (binary)
        for (auto &value : hidden)
            value.second = detail::binary_value_from_text(value.first, value.second);
    auto xdata = std::find_if(p.begin(), p.end(), [](const auto &f) { return f.first == 1001; });
    p.insert(xdata, hidden.begin(), hidden.end());
    return read(encode(data, binary));
}
void verify(const DxfDocument &source, const DxfDocument &got) {
    check(got.version() == DxfVersion::AC1012 && got.reference_issues().empty(), "R13 version and graph");
    check(got.duplicate_handles().empty(), "unique handles");
    check(source.model_space_entity_handles() == got.model_space_entity_handles() &&
              source.paper_space_entity_handles() == got.paper_space_entity_handles(),
          "space member order");
    std::map<std::string, DxfLwPolyline> light;
    source.for_each<DxfLwPolyline>([&](const auto &e) { light.emplace(e.handle(), e); });
    std::set<std::string> new_handles;
    std::size_t expanded = 0;
    got.for_each<DxfPolyline2D>([&](const auto &e) {
        auto it = light.find(e.handle());
        if (it == light.end()) {
            source.for_each<DxfPolyline2D>([&](const auto &old) {
                if (old.handle() != e.handle())
                    return;
                check(old.vertex_count() == e.vertex_count() && old.flags() == e.flags() &&
                          old.elevation() == e.elevation(),
                      "neighbor header unchanged");
                for (std::size_t i = 0; i < e.vertex_count(); ++i) {
                    auto a = old.vertex(i), b = e.vertex(i);
                    check(a.location == b.location && a.bulge == b.bulge && a.start_width == b.start_width &&
                              a.end_width == b.end_width && a.handle == b.handle &&
                              a.owner_handle == b.owner_handle,
                          "neighbor vertex unchanged");
                }
                check(old.seqend()->handle() == e.seqend()->handle(), "neighbor seqend unchanged");
            });
            return;
        }
        ++expanded;
        const auto &old = it->second;
        const auto vertices = old.vertices();
        check(e.vertex_count() == vertices.size() && e.is_closed() == old.is_closed() && (e.flags() & 128),
              "ordered vertices and flags");
        check(e.owner_handle() == old.owner_handle() && e.layer() == old.layer() && e.color() == old.color(),
              "parent identity owner display");
        check(e.elevation() == old.elevation() && e.thickness() == old.thickness() &&
                  e.normal() == old.normal(),
              "OCS header retained");
        check(e.default_start_width() == old.constant_width() &&
                  e.default_end_width() == old.constant_width(),
              "constant width defaults");
        auto new_identity = [&](const std::string &h, const std::string &owner) {
            check(owner == e.handle() && source.get_record(h) == nullptr && new_handles.insert(h).second,
                  "new child identity owner");
            check(std::stoull(h, nullptr, 16) >= source.next_handle() &&
                      std::stoull(h, nullptr, 16) < got.next_handle(),
                  "reconciled seed bounds");
        };
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const auto &a = vertices[i];
            auto b = e.vertex(i);
            check(b.location == Point3{a.x, a.y, 0} && b.bulge == a.bulge && b.flags == 0 && b.id == 0,
                  "vertex OCS XY and bulge");
            const bool per_vertex = a.start_width != 0 || a.end_width != 0;
            check(b.start_width == (per_vertex ? a.start_width : old.constant_width()) &&
                      b.end_width == (per_vertex ? a.end_width : old.constant_width()),
                  "effective widths");
            new_identity(b.handle, b.owner_handle);
        }
        check(e.seqend() != nullptr, "sequence terminated");
        new_identity(e.seqend()->handle(), e.seqend()->owner_handle());
    });
    check(expanded == light.size() && expanded == 4, "all lightweight polylines expanded");
    std::size_t remain = 0;
    got.for_each<DxfLwPolyline>([&](const auto &) { ++remain; });
    check(remain == 0, "no R13 lightweight records");
    check(got.next_handle() == source.next_handle() + new_handles.size(), "HANDSEED advanced exactly");
    // Every unchanged non-polyline record keeps its pairs; HEADER/table gates
    // are verified by the existing target-version matrix.
    std::ostringstream a, b;
    source.write(a, false);
    DxfConversionOptions canonical_options;
    canonical_options.allow_loss = true;
    const auto canonical = got.export_version(DxfVersion::AC1032, false, canonical_options);
    check(canonical.report.can_write, "canonical UTF-8 record comparison");
    b << canonical.bytes;
    auto before = records(a.str(), false), after = records(b.str(), false);
    for (const auto &r : before)
        if (r.front().second == "XRECORD" || r.front().second == "LINE" || r.front().second == "VERTEX" ||
            r.front().second == "SEQEND")
            check(at(after, identity(r)) == r, "neighbor or extension literal record unchanged");
    auto &parent = at(after, light.begin()->first);
    check(std::find(parent.begin(), parent.end(), Pair{90, "999"}) != parent.end() &&
              std::find(parent.begin(), parent.end(), Pair{42, "12.5"}) != parent.end(),
          "private geometry-like fields preserved");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 3, "format prefix");
        const bool binary = std::string(argv[1]) == "binary";
        const std::string prefix = argv[2];
        const auto source = fixture(binary);
        const auto original = save(source);
        write(source, prefix + "-source.dxf");
        DxfConversionOptions allow;
        allow.allow_loss = true;
        for (bool output_binary : {false, true}) {
            const auto result = source.export_version(DxfVersion::AC1012, output_binary, allow);
            if (!result.report.can_write)
                for (const auto &i : result.report.issues)
                    std::cerr << i.code << ": " << i.message << '\n';
            check(result.report.can_write && has_issue(result.report, "lwpolyline-to-polyline") &&
                      has_issue(result.report, "vertex-id-removed"),
                  "expansion and ID loss report");
            const auto got = read(result.bytes);
            verify(source, got);
            write(got, prefix + (output_binary ? "-binary.dxf" : "-ascii.dxf"));
            check(save(source) == original, "source never mutated");
        }
        const auto strict = source.export_version(DxfVersion::AC1012, binary);
        check(!strict.report.can_write && strict.bytes.empty() &&
                  has_issue(strict.report, "vertex-id-removed"),
              "strict vertex ID refusal");
        std::string first;
        source.for_each<DxfLwPolyline>([&](const auto &e) {
            if (first.empty())
                first = e.handle();
        });
        for (int fault = 0; fault < 6; ++fault) {
            auto data = records(original, binary);
            auto &r = at(data, first);
            auto geometry = std::find(r.begin(), r.end(), Pair{100, "AcDbPolyline"});
            if (fault == 0) {
                auto p = std::find_if(geometry, r.end(), [](const auto &p) { return p.first == 90; });
                p->second = binary ? detail::binary_int32_bits(99) : "99";
            }
            if (fault == 1)
                r.erase(std::find_if(geometry, r.end(), [](const auto &p) { return p.first == 20; }));
            if (fault == 2)
                r.insert(geometry + 1, {92, binary ? detail::binary_int32_bits(42) : "42"});
            if (fault == 3) {
                for (auto &p : r) {
                    if (p.first == 330) {
                        p.second = "0";
                        break;
                    }
                }
            }
            if (fault == 4)
                for (auto &record : data)
                    for (std::size_t i = 1; i < record.size(); ++i)
                        if (record[i - 1] == Pair{9, "$HANDSEED"})
                            record[i].second = "FFFFFFFFFFFFFFFF";
            if (fault == 5)
                for (auto &pair : r)
                    if (pair.first == 5) {
                        pair.second = "000";
                        break;
                    }
            const auto bad = read(encode(data, binary));
            std::ostringstream output;
            output << "sentinel";
            bool refused = false;
            try {
                bad.write_version(output, DxfVersion::AC1012, binary, allow);
            } catch (const DxfConversionError &error) {
                refused = true;
                check(std::any_of(error.report().issues.begin(), error.report().issues.end(),
                                  [&](const auto &i) {
                                      return i.kind == DxfConversionIssueKind::Error &&
                                             i.code == "lwpolyline-expansion-failed" &&
                                             i.source_handle == (fault == 5 ? "000" : first) &&
                                             i.record_type == "LWPOLYLINE";
                                  }),
                      "expansion error identifies source record");
            }
            check(refused && output.str() == "sentinel", "invalid expansion is atomic");
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
