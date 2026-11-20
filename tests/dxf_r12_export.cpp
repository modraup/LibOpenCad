// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <libopencad/dxf_r12.hpp>
bool has(const R12ExportReport &r, const std::string &code) {
    return std::any_of(r.issues.begin(), r.issues.end(), [&](const auto &i) { return i.code == code; });
}
int main(int argc, char **argv) try {
    check(argc == 3, "format prefix");
    const bool binary = std::string(argv[1]) == "binary";
    const std::string prefix = argv[2];
    auto d = DxfDocument::create(binary, 4);
    d.add_layer("轮廓", 3);
    d.add_layer("图层", 5);
    // Source header uses GBK; modern source text remains UTF-8 until actual downgrade.
    auto raw = records(save(d), binary);
    for (auto &r : raw)
        for (std::size_t i = 0; i + 1 < r.size(); ++i)
            if (r[i].first == 9 && r[i].second == "$DWGCODEPAGE")
                r[i + 1].second = "ANSI_936";
    d = read(encode(raw, binary));
    LineTypeDefinition lt;
    lt.name = "虚线";
    lt.description = "旧版线型";
    for (int i = 0; i < 13; ++i)
        lt.elements.push_back({i % 2 ? -1.0 : 2.0, {}});
    LineTypeComplexData text_segment;
    text_segment.content = LineTypeComplexData::Content::Text;
    text_segment.text = "标";
    lt.elements.front().complex = text_segment;
    d.add_linetype(lt);
    TextStyleDefinition style;
    style.font_file = "txt.shx";
    d.add_text_style("中文字", style);
    EntityOptions o;
    o.layer = "轮廓";
    o.linetype = "虚线";
    auto line = hex(d.add_line({1, 2, 3}, {4, 5, 6}, o));
    d.for_each<DxfLine>([&](auto &e) {
        if (e.handle() == line) {
            e.set_color(Color::from_rgb(19, 135, 222));
            e.set_line_weight(LineWeight::from_value(35));
            e.set_invisible(true);
        }
    });
    d.add_circle({7, 8, 9}, 10, o);
    d.add_arc({1, 2, 3}, 4, .25, 1.5, o);
    d.add_point({3, 4, 5}, o);
    d.for_each<DxfPoint>([](auto &p) { p.set_x_axis_angle(.25); });
    auto text = hex(d.add_text("中文\\测试\n第二行", {2, 3, 4}, 2.5, o));
    d.for_each<DxfText>([&](auto &e) {
        if (e.handle() == text) {
            auto v = e.definition();
            v.style = "中文字";
            v.rotation = .25;
            v.horizontal_alignment = 2;
            v.vertical_alignment = 1;
            v.alignment_point = Point3{5, 6, 7};
            e.set_definition(v);
        }
    });
    std::vector<LwVertex> vertices{{0, 0, .5, .1, .2, 7}, {8, 0, 0, .3, .4, 8}, {8, 6, -.25, .5, .6, 9}};
    d.add_lwpolyline(vertices, true, o);
    d.for_each<DxfLwPolyline>([](auto &p) {
        p.set_elevation(2.5);
        p.set_constant_width(.75);
        p.set_thickness(.25);
        p.set_plinegen(true);
    });
    o.layer = "图层";
    d.add_polyline2d(vertices, true, 1.5, o);
    d.add_polyline3d({{1, 2, 3}, {4, 5, 9}, {8, 9, -2}}, true, o);
    d.add_polygon_mesh(2, 2, {{0, 0, 0}, {1, 0, 1}, {0, 1, 2}, {1, 1, 3}}, true, false, o);
    d.add_polyface_mesh({{0, 0, 0}, {2, 0, 1}, {2, 2, 2}, {0, 2, 3}}, {{1, -2, 3, 0}, {1, 2, -3, 4}}, o);
    d.add_solid({{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}, o);
    d.add_trace({{0, 0, 0}, {3, 0, 0}, {3, 3, 0}}, o);
    d.add_face3d({{0, 0, 0}, {1, 0, 1}, {1, 1, 2}}, 5, o);
    d.add_symbol(SymbolTable::BlockRecord, "部件");
    d.add_symbol(SymbolTable::BlockRecord, "构件");
    EntityOptions inside;
    inside.block_name = "部件";
    inside.layer = "轮廓";
    d.add_line({0, 0, 0}, {1, 2, 3}, inside);
    AttributeDefinition attribute;
    attribute.tag = "编号";
    attribute.prompt = "输入编号";
    attribute.text.text = "原值";
    attribute.text.height = 1.5;
    d.add_attribute_definition(attribute, inside);
    InsertDefinition ins;
    ins.block_name = "部件";
    ins.insertion = {5, 6, 7};
    ins.scale = {2, 3, 1};
    ins.rotation = .5;
    ins.columns = 2;
    ins.column_spacing = 10;
    d.add_insert(ins, {attribute}, o);
    inside.block_name = "构件";
    d.add_insert(ins, inside);
    o.space = EntitySpace::Paper;
    ins.block_name = "构件";
    d.add_insert(ins, o);
    d.add_circle({30, 40, 5}, 3, o);
    DimensionDefinition dim;
    dim.geometry = AlignedDimension{{1, 2, 3}, {4, 5, 6}, 0};
    dim.definition_point = {7, 8, 9};
    dim.text_middle_point = {1, 1, 0};
    dim.text_override = "尺寸<>";
    d.add_dimension(dim);
    for (int kind = 0; kind < 6; ++kind) {
        switch (kind) {
        case 0:
            dim.geometry = LinearDimension{{1, 2, 3}, {4, 5, 6}, .25, .1};
            break;
        case 1:
            dim.geometry = Angular2LineDimension{{1, 2, 3}, {4, 5, 6}, {2, 3, 4}, {8, 9, 10}};
            break;
        case 2:
            dim.geometry = DiameterDimension{{4, 5, 6}, 2};
            break;
        case 3:
            dim.geometry = RadiusDimension{{4, 5, 6}, 2};
            break;
        case 4:
            dim.geometry = Angular3PointDimension{{1, 2, 3}, {4, 5, 6}, {2, 3, 4}};
            break;
        default:
            dim.geometry = OrdinateDimension{{1, 2, 3}, {4, 5, 6}, true};
        }
        d.add_dimension(dim);
    }
    auto dict = hex(d.add_extension_dictionary(line));
    d.add_xrecord(dict, "记录", {{1, std::string("丢失报告")}});
    write(d, prefix + "-source.dxf");
    const auto before = save(d);
    auto denied = d.export_r12();
    check(!denied.report.can_write && denied.ascii.empty(), "loss rejected by default");
    check(has(denied.report, "symbol-renamed") && has(denied.report, "true-color-to-aci") &&
              has(denied.report, "linetype-truncated") && has(denied.report, "complex-linetype-simplified") &&
              has(denied.report, "object-removed") && has(denied.report, "application-data-removed"),
          "specific loss reporting");
    std::ostringstream sink;
    sink << "unchanged";
    bool threw = false;
    try {
        d.write_r12(sink);
    } catch (const R12ExportError &e) {
        threw = !e.report().can_write;
    }
    check(threw && sink.str() == "unchanged", "rejected output is untouched");
    R12ExportOptions options;
    options.allow_loss = true;
    auto result = d.export_r12(options);
    if (!result.report.can_write)
        for (const auto &i : result.report.issues)
            if (i.kind == R12IssueKind::Error)
                std::cerr << i.code << ' ' << i.record_type << ' ' << i.source_handle << ' ' << i.message
                          << '\n';
    check(result.report.can_write && result.report.converted_entities == 1, "real legacy conversion");
    check(before == save(d), "source document unchanged");
    auto legacy = read(result.ascii);
    check(legacy.version() == DxfVersion::AC1009 && !legacy.is_binary(), "R12 declaration and encoding");
    int lw = 0, traditional = 0;
    legacy.for_each<DxfLwPolyline>([&](auto &) { ++lw; });
    legacy.for_each<DxfPolyline2D>([&](auto &p) {
        ++traditional;
        if (traditional == 1) {
            check(p.is_closed() && p.vertex_count() == 3, "expanded closure and order");
            check(p.vertex(2).bulge == -.25 && p.vertex(2).start_width == .5, "closing bulge and widths");
        }
    });
    check(lw == 0 && traditional == 2, "LWPOLYLINE truly converted");
    auto legacy_records = records(result.ascii, false);
    for (const auto &r : legacy_records)
        for (const auto &p : r) {
            check(p.first != 5 && p.first != 105 && p.first != 100 && p.first != 102 && p.first != 330 &&
                      p.first != 360 && p.first < 1000,
                  "legacy structural invariants");
            if (p.first == 2)
                check(p.second != "CLASSES" && p.second != "OBJECTS" && p.second != "BLOCK_RECORD",
                      "no modern sections/table");
        }
    {
        std::ofstream out(prefix + "-r12.dxf", std::ios::binary);
        out << result.ascii;
    }
    {
        std::ofstream out(prefix + "-report.txt");
        for (const auto &i : result.report.issues)
            out << int(i.kind) << ' ' << i.code << ' ' << i.record_type << ' ' << i.source_handle << ' '
                << i.message << '\n';
    }
    auto unsupported = d;
    unsupported.add_shape(ShapeDefinition{});
    auto refusal = unsupported.export_r12(options);
    check(!refusal.report.can_write && has(refusal.report, "unsupported-entity"),
          "unsupported geometry rejects even with loss permission");
    options.omit_unsupported_entities = true;
    auto omitted = unsupported.export_r12(options);
    check(omitted.report.can_write && omitted.report.omitted_entities == 1, "explicit omission is reported");
    options.max_output_bytes = 16;
    check(!d.export_r12(options).report.can_write, "bounded output refuses atomically");
    auto broken = records(before, binary);
    for (auto &r : broken)
        if (r.front().second == "SEQEND") {
            r.front().second = "UNEXPECTED";
            break;
        }
    auto malformed = read(encode(broken, binary));
    options.max_output_bytes = 256 * 1024 * 1024;
    check(!malformed.export_r12(options).report.can_write, "broken compound always rejects");
    auto name_edges = DxfDocument::create(binary);
    name_edges.add_layer(std::string(40, 'A') + "B");
    name_edges.add_layer(std::string(40, 'A') + "C");
    auto names_result = name_edges.export_r12(options);
    check(names_result.report.can_write, "long name conversion");
    std::set<std::string> layer_targets;
    for (const auto &n : names_result.report.names)
        if (n.table == "LAYER")
            check(n.target.size() <= 31 && layer_targets.insert(n.target).second,
                  "bounded unique name mapping");
    auto orphan = records(before, binary);
    orphan.erase(std::remove_if(orphan.begin(), orphan.end(),
                                [](const auto &r) {
                                    return r.front().second == "BLOCK" &&
                                           std::any_of(r.begin(), r.end(), [](const auto &p) {
                                               return p.first == 2 && p.second == "部件";
                                           });
                                }),
                 orphan.end());
    // Corrupt BLOCK boundaries are rejected by the source reader or the exporter.
    bool orphan_rejected = false;
    try {
        orphan_rejected = !read(encode(orphan, binary)).export_r12(options).report.can_write;
    } catch (const std::exception &) {
        orphan_rejected = true;
    }
    check(orphan_rejected, "missing block definition not silently omitted");
    auto plain = read("0\nSECTION\n2\nHEADER\n9\n$"
                      "ACADVER\n1\nAC1009\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n1\n20\n2\n3"
                      "0\n3\n11\n4\n21\n5\n31\n6\n0\nENDSEC\n0\nEOF\n");
    check(plain.export_r12().report.can_write, "lossless legacy source accepted in strict mode");
    auto private_raw = records(save(plain), false);
    for (auto &r : private_raw)
        if (r.front().second == "LINE")
            r.insert(
                r.begin() + 1,
                {{102, "{VENDOR"}, {1001, "private-not-xdata"}, {101, "private-not-embedded"}, {102, "}"}});
    auto private_result = read(encode(private_raw, false)).export_r12(options);
    check(private_result.report.can_write && has(private_result.report, "application-data-removed"),
          "private scopes protect XDATA/embedded-looking group values");
    auto foreign = records(before, binary);
    for (auto &r : foreign)
        if (r.front().second == "VERTEX") {
            for (auto &p : r)
                if (p.first == 330) {
                    p.second = "FFFF";
                    break;
                }
            break;
        }
    auto foreign_doc = read(encode(foreign, binary));
    check(!foreign_doc.export_r12(options).report.can_write, "foreign compound ownership cannot be waived");
    for (std::size_t i = 0; i < legacy_records.size(); ++i)
        if (legacy_records[i].front().second == "TABLE") {
            int expected = -1;
            for (const auto &p : legacy_records[i])
                if (p.first == 70)
                    expected = std::stoi(p.second);
            int actual = 0;
            while (++i < legacy_records.size() && legacy_records[i].front().second != "ENDTAB")
                ++actual;
            check(expected == actual, "R12 table count rebuilt");
        }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
