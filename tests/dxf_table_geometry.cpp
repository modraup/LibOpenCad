// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
namespace {
TableGeometryDefinition geometry(const DxfDocument &d, std::string_view h) {
    TableGeometryDefinition v;
    d.for_each<DxfTableGeometry>([&](const auto &g) {
        if (g.handle() == h)
            v = g.definition();
    });
    return v;
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "geometry rejection must be atomic");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        const std::string prefix = argv[3];
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        const auto model = d.model_space_entity_handles().front(),
                   paper = d.paper_space_entity_handles().front(), id = d.table_geometry_handle(model),
                   neighbor = d.table_geometry_handle(paper);
        auto initial = d.table_geometry_definition(model);
        check(initial.rows == 2 && initial.columns == 2 && initial.cells.size() == 4 &&
                  initial.cells[0].contents.size() == 2,
              "geometry dimensions and contents");
        check(initial.cells[0].contents[0].distance_to_top_left.z == 3 &&
                  initial.cells[0].contents[0].flags == -7,
              "coordinates and raw flags");
        auto content = d.table_content_definition(model);
        check(content.rows[0].cells[0].geometry->width_with_gap == 20, "restore row-major geometry");
        d.for_each<DxfTableGeometry>([](auto &g) { g.set_definition(g.definition()); });
        write(d, prefix + "-roundtrip.dxf");
        auto before = records(save(d), binary);
        auto edited = initial;
        edited.cells[0].data_flags = -123;
        edited.cells[0].width_with_gap = 99;
        edited.cells[0].contents[1].distance_to_center = {4, 5, 6};
        edited.cells[0].contents[1].outer_height = 88;
        edited.cells[0].table_geometry = std::stoull(id, nullptr, 16);
        d.set_table_geometry(model, edited);
        check(d.table_geometry_handle(model) == id, "geometry stable handle");
        auto after = records(save(d), binary);
        check(at(before, paper) == at(after, paper) && at(before, neighbor) == at(after, neighbor),
              "neighbor geometry preserved");
        write(d, prefix + "-edited.dxf");
        std::string root;
        d.for_each<DxfTableGeometry>([&](const auto &g) {
            if (d.get_record(g.owner_handle())->type() == "DICTIONARY")
                root = g.owner_handle();
        });
        check(!root.empty(), "standalone geometry dictionary");
        d.add_table_geometry(root, "C++几何", initial);
        write(d, prefix + "-created.dxf");
        content = d.table_content_definition(model);
        content.rows[1].cells[1].geometry->contents[0].distance_to_top_left = {7, 8, 9};
        d.set_table_content(model, content);
        d.synchronize_table_geometry(model);
        write(d, prefix + "-synchronized.dxf");
        auto copy = hex(d.copy_entity(model, EntitySpace::Paper));
        const auto copied = d.table_geometry_handle(copy);
        check(copied != id && geometry(d, copied).cells.size() == 4, "copied geometry stays typed");
        check(geometry(d, copied).cells[0].table_geometry == std::stoull(copied, nullptr, 16),
              "self reference remapped");
        write(d, prefix + "-copied.dxf");
        auto cv = d.table_geometry_definition(copy);
        cv.cells[0].contents[0].width = 123;
        d.set_table_geometry(copy, cv);
        write(d, prefix + "-copy-edited.dxf");
        check(d.table_geometry_definition(model).cells[0].contents[0].width == 6, "source cache independent");
        check(d.remove_entity(copy) && !d.get_record(copied), "geometry cascade");
        write(d, prefix + "-deleted.dxf");
        fails(d, [&] {
            auto v = initial;
            v.cells.pop_back();
            d.set_table_geometry(model, v);
        });
        fails(d, [&] {
            auto v = initial;
            v.cells[0].contents[1].width = std::numeric_limits<double>::quiet_NaN();
            d.set_table_geometry(model, v);
        });
        fails(d, [&] {
            auto v = initial;
            v.cells[0].table_geometry = 0xfffffff;
            d.set_table_geometry(model, v);
        });
        fails(d, [&] { d.add_table_geometry(root, "C++几何", initial); });
        auto scoped = initial;
        scoped.extra_fields = {{102, "{PRIVATE"}, {93, "999"}, {102, "}"}, {290, "1"}};
        scoped.cells[0].extra_fields = {{102, "{PRIVATE"}, {10, "999"}, {102, "}"}};
        scoped.cells[0].contents[0].extra_fields = {{102, "{PRIVATE"}, {93, "999"}, {102, "}"}, {291, "1"}};
        auto private_doc = d;
        private_doc.set_table_geometry(model, scoped);
        auto reloaded = read(save(private_doc));
        auto g = reloaded.table_geometry_definition(model);
        check(g.cells.size() == 4 && g.cells[0].contents.size() == 2 && g.extra_fields.size() == 4 &&
                  g.cells[0].contents[0].extra_fields.size() == 4,
              "private geometry scopes");
        // Declared counts are advisory; only real records allocate storage.
        auto raw = records(save(private_doc), binary);
        for (auto &[code, value] : at(raw, id))
            if (code == 92 || code == 94)
                value = binary ? detail::binary_value_from_text(code, "2147483647") : "2147483647";
        auto count_doc = read(encode(raw, binary));
        check(count_doc.table_geometry_definition(model).cells.size() == 4,
              "ignore untrusted geometry counts");
        auto no_content_raw = records(save(d), binary);
        const auto roundtrip_record = d.get_record(id)->owner_handle();
        auto &missing_link = at(no_content_raw, roundtrip_record);
        missing_link.erase(std::remove_if(missing_link.begin(), missing_link.end(),
                                          [](const auto &p) { return p.first == 360; }),
                           missing_link.end());
        auto no_content = read(encode(no_content_raw, binary));
        check(no_content.table_content_handle(model).empty() &&
                  no_content.table_content_definition(model).rows[0].cells[0].geometry.has_value(),
              "geometry restored independently of content");
        write(no_content, prefix + "-geometry-only.dxf");
        auto shared_raw = records(save(d), binary);
        const auto other_record = d.get_record(neighbor)->owner_handle();
        for (auto &[code, value] : at(shared_raw, other_record))
            if (code == 361)
                value = id;
        auto shared = read(encode(shared_raw, binary));
        fails(shared, [&] { shared.set_table_geometry(model, initial); });
        auto mismatch = d;
        mismatch.for_each<DxfTableGeometry>([&](auto &o) {
            if (o.handle() == id) {
                auto v = o.definition();
                v.columns = 1;
                o.set_definition(v);
            }
        });
        check(!mismatch.table_content_definition(model).rows[0].cells[0].geometry,
              "mismatched column cache is not restored");
        std::istringstream damaged(save(mismatch));
        DxfReadStatistics stats;
        DxfDocument::read(damaged, {}, stats);
        check(std::any_of(stats.diagnostics.begin(), stats.diagnostics.end(), [&](const auto &d) {
            return d.code == "invalid-table-adjunct" && d.record_handle == model && d.source_offset > 0;
        }), "mismatched table geometry diagnostic with source location");
        mismatch.synchronize_table_geometry(model);
        write(mismatch, prefix + "-mismatch-fixed.dxf");
        auto partial = d;
        partial.for_each<DxfTableGeometry>([&](auto &o) {
            if (o.handle() == id) {
                auto v = o.definition();
                v.cells.resize(2);
                o.set_definition(v);
            }
        });
        check(!partial.table_content_definition(model).rows[1].cells[0].geometry,
              "partial cache leaves unmatched cells unset");
        partial.synchronize_table_geometry(model);
        check(partial.table_geometry_definition(model).cells[2].contents.empty(),
              "partial cache padded on synchronization");
        write(partial, prefix + "-partial-fixed.dxf");
        // A changed column count cannot reinterpret the old flattened cache.
        auto resized = d.table_content_definition(model);
        resized.columns.resize(3);
        for (auto &row : resized.rows) {
            row.cells.resize(3);
            for (auto &c : row.cells)
                c.geometry.reset();
        }
        d.set_table_content(model, resized);
        write(d, prefix + "-resized.dxf");
        check(d.table_geometry_definition(model).cells.size() == 6, "resize geometry cache");
        auto fresh = DxfDocument::create(binary);
        TableDefinition table;
        table.row_heights = {10, 10};
        table.column_widths = {20, 20};
        table.cells.resize(2, std::vector<TableCell>(2));
        auto fresh_id = hex(fresh.add_table(table));
        fresh.set_table_geometry(fresh_id, initial);
        write(fresh, prefix + "-fresh.dxf");
        check(!fresh.table_content_handle(fresh_id).empty() && fresh.reference_issues().empty(),
              "new content and geometry links");
        // Insert a geometry link before another roundtrip section, retaining its 361.
        auto sections = DxfDocument::create(binary);
        auto sid = hex(sections.add_table(table));
        sections.synchronize_table_content(sid);
        const auto record = sections.get_record(sections.table_content_handle(sid))->owner_handle();
        auto sections_raw = records(save(sections), binary);
        auto &payload = at(sections_raw, record);
        payload.insert(payload.end(),
                       {{102, "OTHER_ROUNDTRIP"}, {361, sections.table_content_handle(sid)}, {1, "keep"}});
        sections = read(encode(sections_raw, binary));
        sections.set_table_geometry(sid, initial);
        auto sections_after = records(save(sections), binary);
        const auto &kept = at(sections_after, record);
        check(kept[kept.size() - 3] == Pair(102, "OTHER_ROUNDTRIP") && kept.back() == Pair(1, "keep") &&
                  kept[kept.size() - 2].second == sections.table_content_handle(sid),
              "other XRECORD section retained");
        write(sections, prefix + "-sections.dxf");
        // Reject conflicting ownership.

        auto malformed = records(save(fresh), binary);
        auto h = fresh.table_geometry_handle(fresh_id);
        for (auto &[code, value] : at(malformed, h)) {
            if (code == 100)
                break;
            if (code == 330)
                value = fresh_id;
        }
        auto bad = read(encode(malformed, binary));
        fails(bad, [&] { bad.set_table_geometry(fresh_id, initial); });
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
