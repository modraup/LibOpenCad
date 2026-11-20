// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <libopencad/dxf_convert.hpp>
#include <libopencad/dxf_r12.hpp>
void bytes(const std::string &path, const std::string &data) {
    std::ofstream out(path, std::ios::binary);
    out << data;
    check(bool(out), "write fixture");
}
// Exercise the transport-independent R12 flattening contract on a separate drawing.
void verify_leaders(bool binary, const std::string &prefix) {
    auto d = DxfDocument::create(binary);
    EntityOptions o;
    o.layer = "Leaders";
    o.color = Color::from_index(3);
    d.add_layer(o.layer, 5);
    auto annotation = d.add_text("关联文字", {7, 8, 9}, 2, o);
    LeaderDefinition v;
    v.vertices = {{1, -2, 3}, {4, 1, 6}, {7, 8, 9}};
    v.normal = {0, 1, 0};
    v.annotation_handle = annotation;
    v.hookline_enabled = true;
    auto attached = hex(d.add_leader(v, o));
    o.space = EntitySpace::Paper;
    v.annotation_handle = 0;
    v.arrow_enabled = false;
    v.hookline_enabled = false;
    v.path_type = 1;
    v.vertices = {{9, 2, 5}, {9, 2, 5}, {3, 4, 7}};
    auto free = hex(d.add_leader(v, o));
    auto before = save(d);
    bytes(prefix + "-leader-source.dxf", before);
    auto source = read(before);
    auto strict = source.export_r12();
    check(!strict.report.can_write && strict.ascii.empty(), "lossy LEADER requires opt-in");
    R12ExportOptions allow;
    allow.allow_loss = true;
    auto result = source.export_r12(allow);
    check(result.report.can_write && result.report.converted_entities == 2 &&
              result.report.output_entities == 5 && result.report.omitted_entities == 0,
          "LEADER output inventory");
    auto has = [&](const std::string &handle, const std::string &code) {
        return std::any_of(result.report.issues.begin(), result.report.issues.end(), [&](const auto &issue) {
            return issue.kind == R12IssueKind::Loss && issue.source_handle == handle && issue.code == code;
        });
    };
    check(has(attached, "leader-arrow-omitted") && has(attached, "leader-annotation-detached") &&
              has(attached, "leader-hookline-omitted") && !has(attached, "leader-spline-linearized"),
          "attached LEADER losses");
    check(has(free, "leader-spline-linearized") && !has(free, "leader-arrow-omitted") &&
              !has(free, "leader-annotation-detached") && !has(free, "leader-hookline-omitted"),
          "unattached LEADER losses");
    check(save(source) == before, "R12 decomposition leaves annotation and source intact");
    bytes(prefix + "-leader-r12.dxf", result.ascii);
    v.vertices.resize(1);
    auto bad = hex(source.add_leader(v, o));
    allow.omit_unsupported_entities = true;
    auto refused = source.export_r12(allow);
    check(refused.ascii.empty() && std::any_of(refused.report.issues.begin(), refused.report.issues.end(),
                                               [&](const auto &i) {
                                                   return i.kind == R12IssueKind::Error &&
                                                          i.source_handle == bad;
                                               }),
          "invalid LEADER refuses atomically");
}
void verify_multileaders(bool binary, const std::string &prefix) {
    auto d = DxfDocument::create(binary);
    d.add_layer("Callouts", 2);
    auto block = d.add_symbol(SymbolTable::BlockRecord, "Badge");
    MultiLeaderDefinition ml;
    ml.context.arrowhead_size = 2;
    ml.context.has_text_contents = true;
    ml.context.text_string = "左侧\\P右侧";
    ml.context.text_location = {24, 12, 0};
    ml.context.text_height = 2;
    ml.context.text_rotation = .25;
    EntityOptions options;
    options.layer = "Callouts";
    options.space = EntitySpace::Paper;
    options.color = Color::from_index(3);
    MultiLeaderRoot root;
    root.connection_point = {10, 0, 0};
    root.direction = {2, 0, 0};
    root.landing_distance = 3;
    MultiLeaderLine branch;
    branch.points = {{0, 0, 0}};
    branch.breaks = {{0, {{{4, 0, 0}, {6, 0, 0}}, {{7, 0, 0}, {5, 0, 0}}}}};
    root.lines.push_back(branch);
    branch.breaks.clear();
    branch.index = 1;
    branch.points = {{10, 10, 0}, {10, 0, 0}};
    branch.override_flags = 16;
    branch.arrowhead_size = 1;
    root.lines.push_back(branch);
    ml.context.leader_roots.push_back(root);
    root.lines.clear();
    root.leader_index = 1;
    root.connection_point = {20, 12, 0};
    root.direction = {0, -3, 0};
    branch.index = 2;
    branch.points = {{15, 10, 1}, {16, 14, 2}};
    branch.override_flags = 32;
    branch.arrowhead_handle = block;
    branch.path_type = 2;
    root.lines.push_back(branch);
    ml.context.leader_roots.push_back(root);
    auto text = hex(d.add_multileader(ml, options));
    ml.context.leader_roots.resize(1);
    ml.context.leader_roots[0].lines.resize(1);
    ml.context.leader_roots[0].lines[0].breaks.clear();
    ml.context.arrowhead_size = 100;
    ml.enable_dogleg = false;
    ml.context.has_text_contents = false;
    ml.context.has_block_contents = true;
    ml.context.block_content_handle = block;
    ml.content_type = 1;
    options.space = EntitySpace::Model;
    auto badge = hex(d.add_multileader(ml, options));
    ml.path_type = 0;
    ml.context.has_block_contents = false;
    ml.context.has_text_contents = true;
    ml.content_type = 2;
    ml.context.text_string = "仅文字";
    d.add_multileader(ml, options);
    auto before = save(d);
    bytes(prefix + "-multi-source.dxf", before);
    auto source = read(before);
    check(source.export_r12().ascii.empty(), "MULTILEADER requires loss acceptance");
    R12ExportOptions allow;
    allow.allow_loss = true;
    auto result = source.export_r12(allow);
    if (!result.report.can_write)
        for (auto &i : result.report.issues)
            if (i.kind == R12IssueKind::Error)
                std::cerr << i.message << '\n';
    check(result.report.can_write && result.report.converted_entities == 3, "MULTILEADER conversion");
    auto has = [&](const std::string &h, const char *code) {
        return std::any_of(result.report.issues.begin(), result.report.issues.end(), [&](const auto &i) {
            return i.kind == R12IssueKind::Loss && i.source_handle == h && i.code == code;
        });
    };
    check(has(text, "multileader-custom-arrow-omitted") && has(text, "multileader-spline-sampled") &&
              has(text, "multileader-text-decomposed") && has(badge, "multileader-block-content-omitted") &&
              has(badge, "multileader-short-arrow-omitted"),
          "specific MULTILEADER loss reports");
    check(save(source) == before, "source unchanged");
    bytes(prefix + "-multi-r12.dxf", result.ascii);
    ml.context.has_text_contents = false;
    d.add_multileader(ml, options);
    check(!d.export_r12(allow).report.can_write, "empty decomposition requires explicit omission");
}
void verify_mlines(bool binary, const std::string &prefix) {
    auto d = DxfDocument::create(binary);
    d.add_layer("Rails", 4);
    EntityOptions o;
    o.layer = "Rails";
    o.color = Color::from_index(3);
    MLineStyleDefinition style;
    style.flags = 1 | 2 | 16 | 1024;
    style.elements = {{3, Color::from_index(2), "Continuous"},
                      {0, Color::by_block(), "ByLayer"},
                      {-2, Color::from_index(5), "Continuous"}};
    d.add_mline_style("Section", style);
    MLineDefinition m;
    m.scale_factor = 4;
    for (auto pos : {Point3{0, 0, 0}, Point3{10, 0, 0}, Point3{10, 10, 0}}) {
        MLineVertex v;
        v.position = pos;
        v.segments.resize(2);
        m.vertices.push_back(v);
    }
    m.vertices[0].miter = {0, 2, 0};
    m.vertices[1].miter = {-3, 3, 0};
    m.vertices[2].miter = {-2, 0, 0};
    const auto standard = hex(d.add_mline(m, o)); // two open offset tracks
    m.flags = 3;
    m.scale_factor = -4;
    o.space = EntitySpace::Paper;
    d.add_mline(m, o); // closing-edge order
    o.space = EntitySpace::Model;
    m.style_name = "Section";
    m.flags = 1;
    m.style_element_count = 3;
    m.normal = {0, 1, 0};
    for (auto &v : m.vertices) {
        v.position.z = 5;
        v.segments.resize(3);
        v.segments[1] = {{0, 1}, {1, 2}};
    }
    auto capped = hex(d.add_mline(m, o));
    m.flags |= 4 | 8;
    auto suppressed = hex(d.add_mline(m, o));
    auto before = save(d);
    bytes(prefix + "-mline-source.dxf", before);
    auto source = read(before);
    auto strict = source.export_r12();
    check(!strict.report.can_write && strict.ascii.empty(), "MLINE strict refusal");
    R12ExportOptions allow;
    allow.allow_loss = true;
    auto result = source.export_r12(allow);
    check(result.report.can_write && result.report.converted_entities == 4 &&
              result.report.output_entities == 14,
          "MLINE segment count");
    auto has = [&](const std::string &h, const char *code) {
        return std::any_of(result.report.issues.begin(), result.report.issues.end(), [&](const auto &i) {
            return i.source_handle == h && i.code == code && i.kind == R12IssueKind::Loss;
        });
    };
    for (auto code :
         {"mline-start-cap-omitted", "mline-end-cap-omitted", "mline-element-style-lost",
          "mline-fill-omitted", "mline-joints-omitted", "mline-parameters-lost", "mline-centerline"})
        check(has(capped, code), "missing MLINE style loss");
    check(!has(suppressed, "mline-start-cap-omitted") && !has(suppressed, "mline-end-cap-omitted") &&
              !has(standard, "mline-centerline"),
          "conditional cap/style diagnostics");
    check(save(source) == before, "source MLINE unchanged");
    bytes(prefix + "-mline-r12.dxf", result.ascii);
    m.vertices.resize(1);
    auto invalid = hex(d.add_mline(m, o));
    allow.omit_unsupported_entities = true;
    auto refused = d.export_r12(allow);
    check(refused.ascii.empty() && std::any_of(refused.report.issues.begin(), refused.report.issues.end(),
                                               [&](const auto &i) {
                                                   return i.source_handle == invalid &&
                                                          i.kind == R12IssueKind::Error;
                                               }),
          "invalid MLINE must not silently disappear");
}
void verify_subdivision_mesh(bool binary, const std::string& prefix) {
    auto doc=DxfDocument::create(binary);
    const auto id=hex(doc.add_line({0,0,0},{1,1,1}));
    auto data=records(save(doc),binary);
    auto& mesh=at(data,id);
    mesh.front().second="MESH";
    auto start=std::find(mesh.begin(),mesh.end(),Pair{100,"AcDbLine"});
    check(start!=mesh.end(),"fixture mesh body");
    mesh.erase(start,mesh.end());
    Record body{{100,"AcDbSubDMesh"},{71,"2"},{72,"1"},{91,"2"},{92,"5"}};
    for (const Point3 p:std::vector<Point3>{{0,0,0},{2,0,0},{3,2,1},{1,4,2},{0,1,3}}) {
        body.emplace_back(10,std::to_string(p.x));body.emplace_back(20,std::to_string(p.y));body.emplace_back(30,std::to_string(p.z));
    }
    body.insert(body.end(),{{93,"10"},{90,"3"},{90,"0"},{90,"1"},{90,"2"},{90,"5"},{90,"0"},{90,"1"},{90,"2"},{90,"3"},{90,"4"},{94,"0"},{95,"0"},{90,"0"}});
    for(auto [code,v]:body) mesh.emplace_back(code,binary?detail::binary_value_from_text(code,v):v);
    const auto input=encode(data,binary);
    auto source=read(input);
    R12ExportOptions allow;allow.allow_loss=true;
    auto result=source.export_r12(allow);
    check(result.report.can_write && result.report.output_entities==4,"mesh triangle fan");
    bytes(prefix+"-mesh-source.dxf",input);bytes(prefix+"-mesh-r12.dxf",result.ascii);
    const auto last=std::find(mesh.begin(),mesh.end(),Pair{94,binary?detail::binary_value_from_text(94,"0"):"0"});
    (last-1)->second=binary?detail::binary_value_from_text(90,"5"):"5";
    check(!read(encode(data,binary)).export_r12(allow).report.can_write,"invalid face index refuses export");
}
void verify_blockless_dimensions(bool binary, const std::string &prefix) {
    auto doc = DxfDocument::create(binary);
    doc.add_layer("Dims", 2);
    EntityOptions options;
    options.layer = "Dims";
    options.color = Color::from_index(3);
    DimensionDefinition d;
    ArcDimension arc;
    arc.center_point = {1, 2, 3};
    arc.first_extension_point = {6, 2, 3};
    arc.second_extension_point = {1, 7, 3};
    arc.has_leader = true;
    arc.first_leader_point = {4, 5, 3};
    arc.second_leader_point = {8, 9, 3};
    d.geometry = arc;
    d.actual_measurement = 999.;
    d.text_middle_point = {10, 15, 3};
    d.text_rotation = .25;
    d.text_override = "弧长 <> / <>";
    const auto arc_handle = hex(doc.add_dimension(d, options));
    arc.has_leader = false;
    arc.end_parameter = 2 * std::acos(-1.);
    d.geometry = arc;
    d.text_override.reset();
    doc.add_dimension(d, options);
    d.geometry = LargeRadialDimension{{3, 4, 4}, {-9, -9, 4}, {6, 8, 4}, .5};
    d.definition_point = {0, 0, 4};
    d.normal = {0, 1, 0};
    d.text_override = "R<>";
    options.space = EntitySpace::Paper;
    const auto radial_handle = hex(doc.add_dimension(d, options));
    d.text_override = " ";
    doc.add_dimension(d, options);
    // R12 natively supports linear dimensions without a graphics block.
    d.geometry = LinearDimension{{1, 0, 0}, {3, 0, 0}, 0, 0};
    d.text_override = "native";
    const auto native_handle = hex(doc.add_dimension(d, options));
    // Model a real dangling block name on the jogged radius without creating a block.
    auto raw = records(save(doc), binary);
    auto &r = at(raw, radial_handle);
    auto mark = std::find(r.begin(), r.end(), Pair{100, "AcDbDimension"});
    check(mark != r.end(), "radial base subclass");
    r.insert(mark + 1, {2, "MissingGraphics"});
    auto before = encode(raw, binary);
    auto source = read(before);
    const auto unchanged = save(source);
    bytes(prefix + "-dim-source.dxf", before);
    auto strict = source.export_r12();
    check(strict.ascii.empty() && !strict.report.can_write, "dimension loss requires opt-in");
    R12ExportOptions allow;
    allow.allow_loss = true;
    auto result = source.export_r12(allow);
    check(result.report.can_write && result.report.converted_entities == 4 &&
              result.report.output_entities == 13,
          "blockless dimension inventory");
    auto reported = [&](const std::string &handle, const char *code) {
        return std::any_of(result.report.issues.begin(), result.report.issues.end(), [&](const auto &i) {
            return i.kind == R12IssueKind::Loss && i.source_handle == handle && i.code == code;
        });
    };
    for (const auto &h : {arc_handle, radial_handle})
        check(reported(h, "dimension-arrows-omitted") && reported(h, "dimension-decomposed"),
              "loss report on source dimension");
    check(reported(arc_handle, "dimension-arc-omitted") &&
              !reported(radial_handle, "dimension-arc-omitted") &&
              !reported(native_handle, "dimension-arrows-omitted"),
          "dimension-specific losses");
    check(save(source) == unchanged, "blockless dimension export preserves source");
    bytes(prefix + "-dim-r12.dxf", result.ascii);
}
int main(int argc, char **argv) try {
    check(argc == 3, "format prefix");
    bool binary = std::string(argv[1]) == "binary";
    std::string prefix = argv[2];
    verify_leaders(binary, prefix);
    verify_multileaders(binary, prefix);
    verify_mlines(binary, prefix);
    verify_blockless_dimensions(binary, prefix);
    verify_subdivision_mesh(binary,prefix);
    auto d = DxfDocument::create(binary);
    d.add_ellipse({1, 2, 3}, {5, 0, 0}, .4);
    d.add_ellipse({-1, 4, 2}, {3, 0, 0}, .6, 5.5, .5, {0, 1, 1});
    SplineDefinition s;
    s.degree = 2;
    s.knots = {0, 0, 0, 1, 1, 1};
    s.control_points = {{0, 0, 1}, {4, 8, 3}, {9, 1, 5}};
    s.weights = {1, .5, 1};
    d.add_spline(s);
    s.flags = 1;
    s.knots = {0, 0, 0, 2, 1, 1};
    d.add_spline(s); // Upstream uniform-knot fallback.
    s.control_points.clear();
    s.knots.clear();
    s.weights.clear();
    s.fit_points = {{0, 0, 0}, {4, 2, 0}, {8, 0, 0}};
    d.add_spline(s);
    s.control_points = {{0, 0, 0}, {2, 3, 4}, {5, 2, 8}};
    s.fit_points.clear();
    s.flags = 0;
    d.add_helix(s);
    d.add_symbol(SymbolTable::BlockRecord, "DIMGRAPH");
    EntityOptions graphics;
    graphics.block_name = "DIMGRAPH";
    d.add_line({0, 0, 0}, {3, 4, 0}, graphics);
    DimensionDefinition dim;
    dim.block_name = "DIMGRAPH";
    dim.geometry = ArcDimension{};
    d.add_dimension(dim);
    dim.geometry = LargeRadialDimension{};
    d.add_dimension(dim);
    d.add_ray({2, 3, 4}, {2, 1, 0});
    EntityOptions paper;
    paper.space = EntitySpace::Paper;
    d.add_xline({-2, 3, 4}, {0, 3, 4}, paper);
    auto mdoc = DxfDocument::create(binary);
    mdoc.add_layer("文字", 3);
    MTextDefinition m;
    m.text = "第一行\\Psecond line\\P\\P　\\Pfourth line";
    m.insertion = {10, 20, 3};
    m.height = 2;
    m.rectangle_width = 12;
    m.rotation = .4;
    m.attachment = 5;
    m.columns.type = 2;
    m.columns.count = 2;
    m.columns.width = 12;
    m.columns.gutter = 3;
    m.columns.heights = {15, 18};
    m.columns.flow_reversed = true;
    EntityOptions opt;
    opt.layer = "文字";
    auto first = hex(mdoc.add_mtext(m, opt));
    m.text = "{\\C3;Green} and {\\H2x;Tall}\\P{\\W1.5;Wide}\\P\\S1/2;";
    m.insertion = {50, 20, 1};
    m.normal = {0, 1, 1};
    m.x_direction = Point3{1, 0, 0};
    opt.space = EntitySpace::Paper;
    mdoc.add_mtext(m, opt);
    auto dict = hex(mdoc.add_extension_dictionary(first));
    mdoc.add_xrecord(dict, "Keep", {{1, std::string("metadata")}});
    auto untouched = hex(mdoc.add_line({1, 2, 3}, {4, 5, 6}));
    auto before = save(mdoc);
    write(mdoc, prefix + "-mtext-source.dxf");
    DxfConversionOptions allow;
    allow.allow_loss = true;
    for (auto v : {DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015, DxfVersion::AC1018}) {
        check(!mdoc.export_version(v, binary).report.can_write, "approximate columns require loss opt-in");
        auto converted = mdoc.export_version(v, binary, allow);
        if (!converted.report.can_write)
            for (auto &i : converted.report.issues)
                std::cerr << i.code << " " << i.message << '\n';
        check(converted.report.can_write, "columns convert");
        auto out = read(converted.bytes);
        check(out.duplicate_handles().empty(), "new identities unique");
        check(out.extension_dictionary_handle(first) == dict, "dictionary stays with first text");
        unsigned count = 0;
        out.for_each<DxfText>([&](auto &t) {
            ++count;
            if (t.handle() != first)
                check(out.extension_dictionary_handle(t.handle()).empty(),
                      "other texts do not share dictionary");
        });
        check(count >= 7, "column text pieces");
        check(out.get_record(untouched) != nullptr, "neighbor remains");
        unsigned remaining = 0;
        out.for_each<DxfMText>([&](auto &) { ++remaining; });
        check(!remaining, "columns fully decomposed");
        bytes(prefix + "-mtext-" + std::string(dxf_version_string(v)) + ".dxf", converted.bytes);
    }
    auto private_records = records(before, binary);
    auto &private_text = at(private_records, first);
    auto private_pos = std::find(private_text.begin(), private_text.end(), Pair{100, "AcDbMText"});
    check(private_pos != private_text.end(), "MTEXT subclass fixture");
    private_text.insert(private_pos + 1, {{102, "{UNREVIEWED"}, {1, "private payload"}, {102, "}"}});
    auto private_doc = read(encode(private_records, binary));
    check(!private_doc.export_version(DxfVersion::AC1018, binary, allow).report.can_write,
          "unreviewed body extension refuses instead of dropping data");
    auto stale_records = records(before, binary);
    for (auto &record : stale_records)
        for (std::size_t i = 1; i < record.size(); ++i)
            if (record[i - 1] == Pair{9, "$HANDSEED"})
                record[i].second = "1";
    auto stale = read(encode(stale_records, binary)).export_version(DxfVersion::AC1018, binary, allow);
    check(stale.report.can_write && read(stale.bytes).duplicate_handles().empty(),
          "column allocator reconciles stale seed");
    check(save(mdoc) == before, "source remains unchanged");
    // Exercise exactly the same plain/rich layout through the dedicated R12 path.
    mdoc.for_each<DxfMText>([&](auto &t) { d.add_mtext(t.definition()); });
    write(d, prefix + "-source.dxf");
    auto source = save(d);
    R12ExportOptions options;
    options.allow_loss = true;
    auto result = d.export_r12(options);
    if (!result.report.can_write)
        for (auto &i : result.report.issues)
            std::cerr << i.code << " " << i.message << '\n';
    check(result.report.can_write && result.report.converted_entities == 12, "all geometric fallbacks");
    check(save(d) == source, "R12 source unchanged");
    check(!d.export_r12().report.can_write, "R12 approximation requires opt-in");
    auto legacy = read(result.ascii);
    unsigned polys = 0, lines = 0, texts = 0;
    legacy.for_each<DxfPolyline2D>([&](auto &p) {
        ++polys;
        check(p.vertex_count() > 2, "sample count");
    });
    legacy.for_each<DxfPolyline3D>([&](auto &p) {
        ++polys;
        check(p.vertex_count() > 2, "3D sample count");
    });
    legacy.for_each<DxfLine>([&](auto &) { ++lines; });
    legacy.for_each<DxfText>([&](auto &) { ++texts; });
    check(polys == 6 && lines == 3 && texts >= 7, "legacy types");
    bytes(prefix + "-r12.dxf", result.ascii);
    options.max_output_bytes = 200;
    check(!d.export_r12(options).report.can_write, "bounded rejection");
    auto empty = DxfDocument::create(binary);
    SplineDefinition insufficient;
    insufficient.fit_points = {{0, 0, 0}};
    empty.add_spline(insufficient);
    options.max_output_bytes = 1 << 20;
    check(!empty.export_r12(options).report.can_write, "empty geometry must not silently vanish");
    auto hd = DxfDocument::create(binary);
    hd.add_symbol(SymbolTable::BlockRecord, "X1");
    auto hx = records(save(hd), binary);
    for (auto &record : hx)
        for (auto &p : record)
            if ((p.first == 2 || p.first == 3) && p.second == "X1")
                p.second = "*X1";
    hd = read(encode(hx, binary));
    HatchDefinition hatch;
    HatchPath outer;
    outer.flags = 3;
    outer.edges.push_back(HatchPolylineEdge{{{0, 0, 0}, {12, 0, 0}, {12, 10, 0}, {0, 10, 0}}, true});
    HatchPath island;
    island.flags = 2;
    island.edges.push_back(HatchPolylineEdge{{{3, 3, 0}, {6, 3, 0}, {6, 6, 0}, {3, 6, 0}}, true});
    hatch.paths = {outer, island};
    hd.add_hatch(hatch);
    hatch.solid = false;
    hatch.pattern_name = "CUSTOM";
    hatch.elevation = 2;
    hatch.normal = {0, 1, 1};
    hatch.pattern_lines = {{.4, {0, 0}, {0, 2}, {2, -1, 0, -1}}};
    hd.add_hatch(hatch, paper);
    hatch.normal = {0, 0, 1};
    hatch.pattern_lines = {{0, {0, 0}, {0, .000001}, {}}};
    hd.add_hatch(hatch); // Boundary-only fallback.
    hatch.solid = true;
    hatch.pattern_lines.clear();
    HatchPath curved;
    curved.flags = 1;
    curved.edges.push_back(HatchArcEdge{{20, 20}, 5, 0, 3.141592653589793, true});
    curved.edges.push_back(HatchLineEdge{{15, 20}, {25, 20}});
    hatch.paths = {curved};
    hd.add_hatch(hatch);
    HatchPath ellipse;
    ellipse.flags = 1;
    ellipse.edges.push_back(HatchEllipseEdge{{40, 30}, {6, 0}, .5, 0, 6.283185307179586, false});
    hatch.paths = {ellipse};
    hd.add_hatch(hatch);
    HatchPath bulged;
    bulged.flags = 3;
    bulged.edges.push_back(HatchPolylineEdge{{{40, 0, .5}, {50, 0, -.25}, {50, 10, 0}, {40, 10, 0}}, true});
    hatch.paths = {bulged};
    hd.add_hatch(hatch);
    HatchPath curve;
    curve.flags = 1;
    HatchSplineEdge edge;
    edge.degree = 2;
    edge.rational = true;
    edge.knots = {0, 0, 0, 1, 1, 1};
    edge.control_points = {{60, 0, 1}, {65, 12, .5}, {70, 0, 1}};
    curve.edges = {edge, HatchLineEdge{{70, 0}, {60, 0}}};
    hatch.paths = {curve};
    hd.add_hatch(hatch);
    auto hbefore = save(hd);
    write(hd, prefix + "-hatch-source.dxf");
    auto hr = hd.export_r12(options);
    if (!hr.report.can_write)
        for (auto &i : hr.report.issues)
            std::cerr << i.code << " " << i.message << '\n';
    check(hr.report.can_write && hr.report.converted_entities == 7, "hatch fallback");
    check(std::any_of(hr.report.issues.begin(), hr.report.issues.end(),
                      [](auto &i) { return i.code == "hatch-boundary-only"; }),
          "dense pattern loss reported");
    check(save(hd) == hbefore, "hatch source unchanged");
    auto hraw = records(hr.ascii, false);
    bool generated = false;
    unsigned inserts = 0;
    for (const auto &record : hraw) {
        if (record.front().second == "BLOCK")
            for (auto p : record)
                if (p.first == 2 && p.second == "*X2")
                    generated = true;
        if (record.front().second == "INSERT")
            ++inserts;
    }
    check(generated && inserts == 7, "anonymous names avoid collision and keep seven source memberships");
    bytes(prefix + "-hatch-r12.dxf", hr.ascii);
    // Blank columns keep an addressable first TEXT rather than dangling external references.
    auto blank = DxfDocument::create(binary);
    m.text = "\\P ";
    m.columns.heights = {12, 12};
    auto blank_id = hex(blank.add_mtext(m));
    auto br = blank.export_version(DxfVersion::AC1018, binary, allow);
    check(br.report.can_write && read(br.bytes).get_record(blank_id), "blank column identity retained");
    // Complexity caps reject atomically, even when geometric loss was accepted.
    auto expensive = DxfDocument::create(binary);
    SplineDefinition high;
    high.degree = 257;
    high.control_points.resize(260);
    for (std::size_t i = 0; i < 260; ++i)
        high.control_points[i] = {double(i), double(i % 3), 0};
    expensive.add_spline(high);
    check(!expensive.export_r12(options).report.can_write, "spline work budget");
    auto huge = DxfDocument::create(binary);
    huge.add_ellipse({}, {3, 0, 0}, .5, 1e308, -1e308);
    check(!huge.export_r12(options).report.can_write, "overflowing wrapped sweep rejects without looping");
    std::cout << "Approximate geometry, columns, identities, loss reporting and atomicity verified\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
