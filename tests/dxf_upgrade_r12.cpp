// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <libopencad/dxf_convert.hpp>
#include <libopencad/dxf_r12.hpp>
void bytes(const std::string &path, const std::string &content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
    check(bool(out), "fixture write");
}
void legacy_layout(bool binary, const std::string &prefix) {
    Records records{{{0, "SECTION"}, {2, "HEADER"}, {9, "$ACADVER"}, {1, "AC1009"}},
                    {{0, "ENDSEC"}},
                    {{0, "SECTION"}, {2, "TABLES"}},
                    {{0, "TABLE"}, {2, "STYLE"}, {70, "2"}},
                    {{0, "STYLE"}, {2, ""}, {70, "1"}, {3, "first.shx"}},
                    {{0, "STYLE"}, {2, ""}, {70, "1"}, {3, "second.shx"}},
                    {{0, "ENDTAB"}},
                    {{0, "ENDSEC"}},
                    {{0, "SECTION"}, {2, "BLOCKS"}},
                    {{0, "BLOCK"}, {2, "XREF"}, {70, "4"}, {1, "assets/part.dwg"}},
                    {{0, "ENDBLK"}},
                    {{0, "ENDSEC"}},
                    {{0, "SECTION"}, {2, "ENTITIES"}},
                    {{0, "SHAPE"}, {2, "GLYPH"}, {10, "3"}, {20, "4"}, {40, "5"}},
                    {{0, "INSERT"}, {2, "XREF"}, {10, "1"}, {20, "2"}},
                    {{0, "VIEWPORT"},
                     {67, "1"},
                     {40, "30"},
                     {41, "20"},
                     {68, "1"},
                     {69, "2"},
                     {1001, "ACAD"},
                     {1000, "MVIEW"},
                     {1002, "{"},
                     {1070, "16"},
                     {1010, "1"},
                     {1020, "2"},
                     {1030, "3"},
                     {1010, "0"},
                     {1020, "0"},
                     {1030, "1"}}};
    auto &view = records.back();
    for (auto x : {0.25, 55., 4., 5., 50., 1., 100.})
        view.emplace_back(1040, std::to_string(x));
    for (int n : {1, 64, 1, 3, 1, 1, 0, 2})
        view.emplace_back(1070, std::to_string(n));
    for (auto x : {0.5, 2., 3., 4., 5., 6., 7.})
        view.emplace_back(1040, std::to_string(x));
    view.insert(view.end(), {{1070, "1"}, {1002, "{"}, {1003, "0"}, {1002, "}"}, {1002, "}"}});
    records.insert(records.end(), {{{0, "ENDSEC"}}, {{0, "EOF"}}});
    auto input = read(encode(records, false));
    std::ostringstream stream;
    input.write(stream, binary);
    const auto source = read(stream.str());
    DxfConversionOptions options;
    options.allow_loss = true;
    for (auto v : {DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015, DxfVersion::AC1018,
                   DxfVersion::AC1021, DxfVersion::AC1024, DxfVersion::AC1027, DxfVersion::AC1032}) {
        for (bool target_binary : {false, true}) {
            auto result = source.export_version(v, target_binary, options);
            if (!result.report.can_write)
                for (auto &i : result.report.issues)
                    if (i.kind == DxfConversionIssueKind::Error)
                        std::cerr << i.message << '\n';
            check(result.report.can_write, "upgrade R12 layout");
            auto saved = read(result.bytes);
            check(saved.paper_space_entity_handles().size() == 1 &&
                      saved.model_space_entity_handles().size() == 2,
                  "layout ownership");
            saved.for_each<DxfViewport>([&](const auto &e) {
                auto view = e.definition();
                check(view.view_height == 55 && view.view_target.z == 3 &&
                          std::abs(view.twist_angle - .25) < 1e-10 && view.circle_sides == 64 &&
                          view.frozen_layers.size() == 1,
                      "MVIEW restoration");
            });
            bytes(prefix + "-layout-" + std::string(dxf_version_string(v)) +
                      (target_binary ? "-binary.dxf" : "-ascii.dxf"),
                  result.bytes);
        }
    }
    auto cached = records;
    const auto end_block = std::find_if(cached.begin(), cached.end(),
                                        [](const auto &r) { return r.front().second == "ENDBLK"; });
    cached.insert(end_block, {{0, "LINE"}, {5, "A2"}, {10, "0"}, {20, "0"}, {11, "1"}, {21, "1"}});
    const auto xref = read(encode(cached, false));
    check(!xref.export_version(DxfVersion::AC1032, binary).report.can_write,
          "external cached geometry requires loss permission");
    auto projected = xref.export_version(DxfVersion::AC1032, binary, options);
    check(projected.report.can_write &&
              std::any_of(
                  projected.report.issues.begin(), projected.report.issues.end(),
                  [](const auto &i) { return i.code == "xref-cache-omitted" && i.source_handle == "A2"; }),
          "identify omitted xref cache");
}
int main(int argc, char **argv) try {
    check(argc == 3, "input format prefix");
    const bool binary = std::string(argv[1]) == "binary";
    const std::string prefix = argv[2];
    legacy_layout(binary, prefix);
    const auto minimal = read("0\nSECTION\n2\nHEADER\n9\n$"
                              "ACADVER\n1\nAC1009\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n1\n"
                              "20\n2\n30\n3\n11\n4\n21\n5\n31\n6\n0\nENDSEC\n0\nEOF\n");
    for (auto v : {DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015, DxfVersion::AC1018,
                   DxfVersion::AC1021, DxfVersion::AC1024, DxfVersion::AC1027, DxfVersion::AC1032})
        for (bool format : {false, true}) {
            auto result = minimal.export_version(v, format);
            if (!result.report.can_write)
                for (const auto &i : result.report.issues)
                    if (i.kind != DxfConversionIssueKind::Information)
                        std::cerr << i.code << " " << i.record_type << " " << i.message << '\n';
            check(result.report.can_write, "strict minimal upgrade does not lose synthesized defaults");
            check(read(result.bytes).model_space_entity_handles().size() == 1, "handleless minimal geometry");
        }
    auto modern = DxfDocument::create(false);
    modern.add_layer("Geometry", 3);
    modern.add_layer("Labels", 5);
    LineTypeDefinition lt;
    lt.name = "Dashed";
    lt.description = "Legacy dash";
    lt.elements = {{2, {}}, {-1, {}}};
    modern.add_linetype(lt);
    TextStyleDefinition st;
    st.font_file = "txt.shx";
    modern.add_text_style("LegacyFont", st);
    modern.add_symbol(SymbolTable::AppId, "TESTAPP");
    modern.add_symbol(SymbolTable::BlockRecord, "Part");
    EntityOptions o;
    o.layer = "Geometry";
    o.linetype = "Dashed";
    modern.add_line({1, 2, 3}, {7, 8, 9}, o);
    modern.add_circle({4, 5, 6}, 7, o);
    modern.add_arc({2, 3, 4}, 5, .3, 2.4, o);
    modern.add_point({8, 9, 2}, o);
    modern.add_solid({{0, 0, 0}, {3, 0, 0}, {3, 3, 0}}, o);
    modern.add_trace({{0, 0, 1}, {2, 0, 1}, {2, 2, 1}, {0, 2, 1}}, o);
    modern.add_face3d({{0, 0, 0}, {2, 0, 1}, {2, 2, 2}}, 5, o);
    std::vector<LwVertex> verts{{0, 0, .5, .1, .2, 0}, {6, 0, 0, .3, .4, 0}, {6, 4, -.25, .5, .6, 0}};
    modern.add_polyline2d(verts, true, 2, o);
    modern.add_polyline3d({{1, 2, 3}, {4, 5, 6}, {7, 8, 10}}, true, o);
    modern.add_polygon_mesh(2, 2, {{0, 0, 0}, {2, 0, 1}, {0, 2, 2}, {2, 2, 3}}, true, false, o);
    modern.add_polyface_mesh({{0, 0, 0}, {2, 0, 0}, {2, 2, 1}, {0, 2, 0}}, {{1, -2, 3, 0}, {1, 2, -3, 4}}, o);
    o.layer = "Labels";
    modern.add_text("中文升级", {10, 20, 3}, 2.5, o);
    modern.for_each<DxfText>([](auto &t) {
        auto v = t.definition();
        v.style = "LegacyFont";
        v.horizontal_alignment = 2;
        v.vertical_alignment = 1;
        v.alignment_point = Point3{11, 21, 3};
        v.rotation = .25;
        t.set_definition(v);
    });
    o.block_name = "Part";
    modern.add_line({0, 0, 0}, {3, 4, 5}, o);
    AttributeDefinition attr;
    attr.tag = "ID";
    attr.prompt = "编号";
    attr.text.text = "值";
    attr.text.height = 2;
    modern.add_attribute_definition(attr, o);
    o.block_name.clear();
    InsertDefinition insert;
    insert.block_name = "Part";
    insert.insertion = {10, 20, 30};
    insert.scale = {2, 3, 1};
    insert.rotation = .4;
    insert.columns = 2;
    insert.column_spacing = 12;
    modern.add_insert(insert, {attr}, o);
    o.space = EntitySpace::Paper;
    modern.add_circle({9, 8, 7}, 2, o);
    modern.add_text("Paper", {2, 3, 0}, 1, o);
    for (unsigned i = 0; i < 7; ++i) {
        DimensionDefinition dim;
        switch (i) {
        case 0:
            dim.geometry = LinearDimension{{1, 2, 0}, {4, 6, 0}, .2, .1};
            break;
        case 1:
            dim.geometry = AlignedDimension{{1, 2, 0}, {4, 6, 0}, .2};
            break;
        case 2:
            dim.geometry = Angular2LineDimension{{0, 0, 0}, {3, 0, 0}, {0, 0, 0}, {0, 4, 0}};
            break;
        case 3:
            dim.geometry = DiameterDimension{{5, 0, 0}, 2};
            break;
        case 4:
            dim.geometry = RadiusDimension{{3, 0, 0}, 2};
            break;
        case 5:
            dim.geometry = Angular3PointDimension{{1, 0, 0}, {0, 1, 0}, {0, 0, 0}};
            break;
        case 6:
            dim.geometry = OrdinateDimension{{2, 3, 0}, {4, 5, 0}, true};
            break;
        }
        modern.add_dimension(dim);
    }
    R12ExportOptions loss;
    loss.allow_loss = true;
    auto legacy = modern.export_r12(loss);
    check(legacy.report.can_write, "legacy fixture");
    auto raw = records(legacy.ascii, false);
    for (std::size_t i = 0; i < raw.size(); ++i)
        if (raw[i].front().second == "TABLE" &&
            std::find(raw[i].begin(), raw[i].end(), Pair{2, "APPID"}) != raw[i].end()) {
            for (auto &p : raw[i])
                if (p.first == 70)
                    p.second = "2";
            auto end = i + 1;
            while (end < raw.size() && raw[end].front().second != "ENDTAB")
                ++end;
            raw.insert(raw.begin() + end, Record{{0, "APPID"}, {2, "TESTAPP"}, {70, "0"}});
            break;
        }
    // Assign real legacy identities, including table/block markers and child records.
    std::uint64_t next = 0x10;
    std::string sec, first_line, first_vertex;
    for (auto &r : raw) {
        auto type = r.front().second;
        if (type == "SECTION") {
            sec = r[1].second;
            continue;
        }
        if (type == "ENDSEC") {
            sec.clear();
            continue;
        }
        if (type == "ENDTAB" || type == "EOF")
            continue;
        if (sec == "TABLES" || sec == "BLOCKS" || sec == "ENTITIES") {
            auto h = hex(next++);
            r.insert(r.begin() + (type == "TABLE" ? 2 : 1), {type == "DIMSTYLE" ? 105 : 5, h});
            if (sec == "ENTITIES" && type == "LINE" && first_line.empty())
                first_line = h;
            if (type == "VERTEX" && first_vertex.empty())
                first_vertex = h;
        }
    }
    auto &line = at(raw, first_line);
    line.insert(line.end(), {{1001, "TESTAPP"}, {1000, "legacy payload"}, {1005, first_vertex}});
    auto source = read(encode(raw, false));
    std::ostringstream source_bytes;
    source.write(source_bytes, binary);
    source = read(source_bytes.str());
    const auto before = save(source);
    bytes(prefix + "-source.dxf", before);
    auto verify_outputs = [&](const DxfDocument &source, const std::string &output_prefix, bool has_handles) {
        bytes(output_prefix + "-source.dxf", save(source));
        for (bool output_binary : {false, true})
            for (auto v : {DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015, DxfVersion::AC1018,
                           DxfVersion::AC1021, DxfVersion::AC1024, DxfVersion::AC1027, DxfVersion::AC1032}) {
                DxfConversionOptions allow;
                allow.allow_loss = true;
                auto result = source.export_version(v, output_binary, allow);
                if (!result.report.can_write)
                    for (auto &i : result.report.issues)
                        std::cerr << i.code << " " << i.record_type << " " << i.message << '\n';
                check(result.report.can_write, "R12 upgrade");
                auto d = read(result.bytes);
                check(d.version() == v && d.is_binary() == output_binary, "target version and transport");
                check(d.duplicate_handles().empty(), "all identities unique");
                check(!d.root_dictionary_handle().empty(), "modern object defaults");
                check(d.get_symbol(SymbolTable::BlockRecord, has_handles ? "Part" : "*U1").has_value(),
                      "named/anonymous block record");
                std::string mapped_line, mapped_vertex;
                std::set<std::string> mapped;
                for (auto &m : result.report.handle_mappings) {
                    check(d.get_record(m.target_handle) != nullptr, "remapped identity exists");
                    check(mapped.insert(m.target_handle).second,
                          "distinct source identities remain distinct");
                    if (m.source_handle == first_line)
                        mapped_line = m.target_handle;
                    if (m.source_handle == first_vertex)
                        mapped_vertex = m.target_handle;
                }
                if (has_handles)
                    check(!mapped_line.empty() && !mapped_vertex.empty(), "entity/child mappings reported");
                else
                    check(result.report.handle_mappings.empty(), "handleless source has no old identities");
                auto encoded = records(result.bytes, output_binary);
                if (has_handles) {
                    bool pointer = false;
                    for (auto p : at(encoded, mapped_line))
                        if (p.first == 1005) {
                            check(p.second == mapped_vertex, "XDATA pointer remapped");
                            pointer = true;
                        }
                    check(pointer, "XDATA kept");
                }
                std::uint64_t largest = 0, seed = 0;
                std::size_t expected_count = 0, actual_count = 0;
                bool in_table = false;
                for (const auto &r : records(
                         [&] {
                             std::ostringstream ascii;
                             d.write(ascii, false);
                             return ascii.str();
                         }(),
                         false)) {
                    const auto type = r.front().second;
                    if (type == "SECTION") {
                        for (std::size_t i = 1; i + 1 < r.size(); ++i)
                            if (r[i] == Pair{9, "$HANDSEED"})
                                seed = std::stoull(r[i + 1].second, nullptr, 16);
                        continue;
                    }
                    for (const auto &p : r)
                        if (p.first == (type == "DIMSTYLE" ? 105 : 5)) {
                            largest = std::max(
                                largest, static_cast<std::uint64_t>(std::stoull(p.second, nullptr, 16)));
                            break;
                        }
                    if (type == "TABLE") {
                        in_table = true;
                        actual_count = 0;
                        for (const auto &p : r)
                            if (p.first == 70)
                                expected_count = std::stoul(p.second);
                    } else if (type == "ENDTAB") {
                        check(actual_count == expected_count, "modern table count");
                        in_table = false;
                    } else if (in_table)
                        ++actual_count;
                }
                check(seed > largest, "HANDSEED exceeds generated identities");
                unsigned texts = 0;
                d.for_each<DxfText>([&](auto &t) {
                    if (t.text() == "中文升级") {
                        ++texts;
                        check(t.definition().style == "LegacyFont", "text style");
                    }
                });
                check(texts == 1, "Chinese decoded");
                check(d.model_space_entity_handles().size() == 20, "model membership exists");
                check(d.paper_space_entity_handles().size() == 2, "paper membership");
                std::string parent;
                for (const auto &r : encoded) {
                    const auto type = r.front().second;
                    if (type == "VERTEX" || type == "ATTRIB" || type == "SEQEND") {
                        auto owner =
                            std::find_if(r.begin(), r.end(), [](const auto &p) { return p.first == 330; });
                        check(!parent.empty() && owner != r.end() && owner->second == parent,
                              "child owns exact enclosing parent");
                        if (type == "SEQEND")
                            parent.clear();
                    } else
                        parent = (type == "POLYLINE" || type == "INSERT") ? identity(r) : "";
                }
                auto suffix = std::string(dxf_version_string(v)) + (output_binary ? "-binary" : "-ascii");
                bytes(output_prefix + "-" + suffix + ".dxf", result.bytes);
                std::ofstream mappings(output_prefix + "-" + suffix + "-handles.txt");
                for (auto &m : result.report.handle_mappings)
                    mappings << m.record_type << ' ' << m.source_handle << ' ' << m.target_handle << '\n';
            }
    };
    verify_outputs(source, prefix, true);
    auto handleless = raw;
    for (auto &r : handleless) {
        const auto type = r.front().second;
        if (type == "SECTION")
            continue;
        r.erase(std::remove_if(r.begin(), r.end(),
                               [&](const auto &p) {
                                   return p.first == (type == "DIMSTYLE" ? 105 : 5) || p.first == 1005;
                               }),
                r.end());
        for (auto &p : r)
            if ((p.first == 2 || p.first == 3) && p.second == "Part")
                p.second = "*U1";
        if (type == "BLOCK" && std::find(r.begin(), r.end(), Pair{2, "*U1"}) != r.end())
            for (auto &p : r)
                if (p.first == 10)
                    p.second = "12.5";
    }
    auto no_ids = read(encode(handleless, false));
    std::ostringstream no_id_bytes;
    no_ids.write(no_id_bytes, binary);
    verify_outputs(read(no_id_bytes.str()), prefix + "-handleless", false);
    check(save(source) == before, "source unchanged");
    DxfConversionOptions bounded;
    bounded.allow_loss = true;
    bounded.max_output_bytes = 16;
    auto denied = source.export_version(DxfVersion::AC1032, false, bounded);
    check(!denied.report.can_write && denied.bytes.empty(), "bounded atomic refusal");
    bounded.max_output_bytes = 1 << 24;
    auto missing = raw;
    for (auto &p : at(missing, first_line))
        if (p.first == 1005)
            p.second = "FFFF";
    check(!read(encode(missing, false)).export_version(DxfVersion::AC1032, false, bounded).report.can_write,
          "unresolved pointer rejects");
    for (auto type : {"TEXT", "ATTRIB", "LAYER"}) {
        auto bad = raw;
        auto r =
            std::find_if(bad.begin(), bad.end(), [&](const auto &r) { return r.front().second == type; });
        check(r != bad.end(), "bad reference fixture");
        const int code = std::string(type) == "LAYER" ? 6 : 7;
        r->erase(std::remove_if(r->begin(), r->end(), [&](const auto &p) { return p.first == code; }),
                 r->end());
        r->push_back({code, "MISSING_SYMBOL"});
        auto result = read(encode(bad, false)).export_version(DxfVersion::AC1032, false, bounded);
        check(!result.report.can_write && result.bytes.empty(), "unresolved symbol refuses atomically");
    }
    auto unknown = raw;
    at(unknown, first_line).front().second = "PRIVATE_ENTITY";
    bounded.max_output_bytes = 1 << 24;
    check(!read(encode(unknown, false)).export_version(DxfVersion::AC1032, false, bounded).report.can_write,
          "unknown legacy entity rejects");
    std::cout << "R12 identities, defaults, tables, geometry and both output encodings verified\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
