// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "geometry_operations.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
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
// Expose decoded raw fields only in the test, so preservation includes values
// which Rust's typed reader does not retain.
struct InspectableTable : DxfTable {
    explicit InspectableTable(const DxfTable &e) : DxfTable(e) {}
    void remove_header_direction() {
        auto fields = subclass_fields("AcDbTable");
        std::vector<DxfRawField> kept;
        bool cells = false;
        for (const auto &f : fields) {
            if (f.code == 171)
                cells = true;
            if (cells || (f.code != 11 && f.code != 21 && f.code != 31))
                kept.push_back(f);
        }
        set_subclass_fields("AcDbTable", kept);
    }
    std::vector<DxfRawField> cell_payload() const {
        auto fields = subclass_fields("AcDbTable");
        auto begin = std::find_if(fields.begin(), fields.end(), [](const auto &f) { return f.code == 171; });
        return {begin, fields.end()};
    }
    std::vector<DxfRawField> payload() const {
        auto fields = subclass_fields("AcDbTable");
        unsigned depth = 0;
        bool cells = false;
        for (auto &f : fields) {
            if (f.code == 102) {
                if (!f.value.empty() && f.value.front() == '{')
                    ++depth;
                else if (depth && f.value == "}")
                    --depth;
            }
            if (!depth && f.code == 171)
                cells = true;
            if (!depth && !cells &&
                (f.code == 11 || f.code == 21 || f.code == 31 || f.code == 141 || f.code == 142))
                f.value.clear();
        }
        return fields;
    }
};
static bool same_payload(const DxfTable &a, const DxfTable &b) {
    auto x = InspectableTable(a).payload(), y = InspectableTable(b).payload();
    if (x.size() != y.size())
        return false;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (x[i].code != y[i].code || x[i].value != y[i].value)
            return false;
    return true;
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto source = DxfDocument::read(in);
    auto original = bytes(source);
    std::vector<std::string> targets;
    source.for_each<DxfTable>([&](const auto &e) {
        if (e.layer() != "Control")
            targets.push_back(e.handle());
    });
    check(targets.size() == 12, "Missing table targets");
    for (int step = 0; step < 16; ++step) {
        auto d = source;
        auto t = operation(step == 14 ? 4 : step == 15 ? 6 : step);
        d.for_each<DxfTable>([&](auto &e) {
            if (e.layer() == "Control")
                return;
            auto before = e;
            if (step == 0)
                e.translate({3, -4, 5});
            else if (step == 5 || step == 6 || step == 7 || step == 11 || step >= 14) {
                e.apply_mirror(t);
                if (step == 15)
                    e.apply_mirror(t);
            } else
                e.apply_transform(t);
            check(same_payload(e, before), "Transform changed table content or references");
        });
        if (step == 7 || step == 11)
            check(bytes(d) == original, "No-op mirror changed drawing");
        for (auto &h : targets)
            d.synchronize_table_dimensions(h);
        if (step == 7 || step == 11)
            check(bytes(d) == original, "No-op dimension sync changed drawing");
        auto path = std::string(argv[2]) + "." + std::to_string(step);
        std::ofstream out(path + ".dxf", std::ios::binary);
        d.write(out);
        out.close();
        check(bool(out), "Save failed");
        std::ofstream trace(path + ".trace");
        trace << std::setprecision(17);
        auto n = [&](double x) { trace << ' ' << x; };
        auto p = [&](Point3 p) {
            n(p.x);
            n(p.y);
            n(p.z);
        };
        d.for_each<DxfTable>([&](const auto &e) {
            auto v = e.definition();
            trace << e.handle();
            p(v.insertion);
            p(v.horizontal_direction);
            p(v.normal);
            n(v.row_heights.size());
            for (auto x : v.row_heights)
                n(x);
            n(v.column_widths.size());
            for (auto x : v.column_widths)
                n(x);
            trace << '\n';
            auto content = d.table_content_definition(e.handle());
            check(content.rows.size() == v.row_heights.size() &&
                      content.columns.size() == v.column_widths.size(),
                  "Resolved dimensions");
            auto linked = d.table_content_handle(e.handle());
            d.for_each<DxfTableContent>([&](const auto &c) {
                if (c.handle() != linked)
                    return;
                auto w = c.definition();
                for (std::size_t i = 0; i < w.rows.size(); ++i)
                    check(w.rows[i].height == v.row_heights[i], "Stale content row height");
                for (std::size_t i = 0; i < w.columns.size(); ++i)
                    check(w.columns[i].width == v.column_widths[i], "Stale content column width");
            });
        });
        trace.close();
        check(bool(trace), "Trace failed");
        auto once = bytes(d);
        for (auto &h : targets)
            d.synchronize_table_dimensions(h);
        check(bytes(d) == once, "Dimension sync not idempotent");
    }
    source.for_each<DxfTable>([](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(operation(11));
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Invalid matrix accepted");
    });
    check(bytes(source) == original, "Invalid matrix partially committed");
    source.for_each<DxfTable>([](auto &e) {
        bool refused = false;
        try {
            e.apply_transform(Transform::from_scale(std::numeric_limits<double>::max()));
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "Overflow accepted");
    });
    check(bytes(source) == original, "Overflow partially committed");
    auto legacy_only = source;
    TableDefinition legacy_definition;
    source.for_each<DxfTable>([&](const auto &e) { legacy_definition = e.definition(); });
    std::ostringstream legacy_id;
    legacy_id << std::hex << legacy_only.add_table(legacy_definition);
    auto legacy_handle = legacy_id.str();
    check(legacy_only.table_content_handle(legacy_handle).empty(), "Unexpected synthesized adjunct");
    auto legacy_bytes = bytes(legacy_only);
    legacy_only.synchronize_table_dimensions(legacy_handle);
    check(bytes(legacy_only) == legacy_bytes, "Missing adjunct synchronization changed drawing");
    auto broken = source;
    auto content = broken.table_content_handle(targets[0]);
    broken.for_each<DxfTableContent>([&](auto &e) {
        if (e.handle() == content) {
            auto v = e.definition();
            v.rows.pop_back();
            e.set_definition(v);
        }
    });
    auto before = bytes(broken);
    bool refused = false;
    try {
        broken.synchronize_table_dimensions(targets[0]);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused && bytes(broken) == before, "Mismatched dimensions did not refuse atomically");
    // Explicit world-X scaling, normal normalization, and untouched legacy cells.
    source.for_each<DxfTable>([](auto &e) {
        auto v = e.definition();
        auto a = e, b = e;
        a.translate({3, -4, 5});
        check(a.definition().horizontal_direction == v.horizontal_direction &&
                  a.definition().normal == v.normal,
              "Translation normalized authored axes");
        b.apply_transform(Transform::from_scaling({2, .5, 3}));
        auto w = b.definition();
        for (std::size_t i = 0; i < v.row_heights.size(); ++i)
            check(w.row_heights[i] == 2 * v.row_heights[i], "Row scale");
        for (std::size_t i = 0; i < v.column_widths.size(); ++i)
            check(w.column_widths[i] == 2 * v.column_widths[i], "Column scale");
        check((w.horizontal_direction - Vector3{2 * v.horizontal_direction.x, .5 * v.horizontal_direction.y,
                                                3 * v.horizontal_direction.z}
                                            .normalize())
                      .length() < 1e-12,
              "Direction normalization");
    });
    source.for_each<DxfTable>([](const auto &e) {
        auto probe = e;
        auto v = probe.definition();
        v.extra_fields.insert(v.extra_fields.end(),
                              {{102, "{PRIVATE"}, {141, "123"}, {142, "456"}, {11, "789"}, {102, "}"}});
        v.cells[0][0].extra_fields.push_back({141, "321"});
        v.cells[0][0].extra_fields.push_back({142, "654"});
        probe.set_definition(v);
        auto before = probe;
        probe.apply_transform(Transform::from_scale(3));
        check(same_payload(before, probe), "Private or cell dimensions were transformed");
    });
    source.for_each<DxfTable>([](const auto &e) {
        InspectableTable probe(e);
        auto v = probe.definition();
        v.cells[0][0].extra_fields.insert(v.cells[0][0].extra_fields.end(),
                                          {{11, "101"}, {21, "202"}, {31, "303"}});
        probe.set_definition(v);
        probe.remove_header_direction();
        auto cells = probe.cell_payload();
        check(probe.definition().horizontal_direction == Vector3::UNIT_X, "Missing header default");
        auto transform = Transform::from_rotation(Vector3::UNIT_Z, .5);
        probe.apply_transform(transform);
        check((probe.definition().horizontal_direction - transform.apply_rotation(Vector3::UNIT_X)).length() <
                  1e-12,
              "Direction inserted outside header");
        auto after = probe.cell_payload();
        check(cells.size() == after.size(), "Missing header changed cells");
        for (std::size_t i = 0; i < cells.size(); ++i)
            check(cells[i].code == after[i].code && cells[i].value == after[i].value,
                  "Cell point mistaken for header direction");
    });
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
