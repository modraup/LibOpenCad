// SPDX-License-Identifier: MPL-2.0
#include <libopencad/compound_file.hpp>
#include <libopencad/count.hpp>
#include <libopencad/dxf_document.hpp>
#include <libopencad/fields.hpp>
#include <libopencad/semantic_inventory.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace libopencad;

static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}

static std::string hex(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << value;
    return out.str();
}

struct TestContext : FieldContext {
    double now_julian() const override { return 2460000.5; }
};

static void write_bytes(const std::string &path, const std::vector<std::uint8_t> &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

int main(int argc, char **argv) {
    try {
        check(argc == 2, "output prefix");
        std::string prefix = argv[1];

        CompoundPropertySetStream stream;
        stream.version = 0;
        CompoundPropertySet set;
        CompoundProperty code_page;
        code_page.id = 1;
        code_page.value.kind = CompoundPropertyValue::Kind::I32;
        code_page.value.signed_value = 1252;
        CompoundProperty hello;
        hello.id = 2;
        hello.value.kind = CompoundPropertyValue::Kind::AnsiString;
        hello.value.text = "Hello";
        hello.value.original_text = "Hello";
        hello.value.encoded = BinaryRecord::split(reinterpret_cast<const std::uint8_t *>("Hello"), 5, 4096);
        CompoundProperty uni;
        uni.id = 3;
        uni.value.kind = CompoundPropertyValue::Kind::UnicodeString;
        uni.value.text = "Hi";
        CompoundProperty sheets;
        sheets.id = 4;
        sheets.value.kind = CompoundPropertyValue::Kind::Vector;
        sheets.value.element_type = 30;
        CompoundPropertyValue a;
        a.kind = CompoundPropertyValue::Kind::AnsiString;
        a.text = "Sheet1";
        a.original_text = "Sheet1";
        CompoundPropertyValue b;
        b.kind = CompoundPropertyValue::Kind::AnsiString;
        b.text = "Sheet2";
        b.original_text = "Sheet2";
        sheets.value.values.push_back(a);
        sheets.value.values.push_back(b);
        set.properties = {code_page, hello, uni, sheets};
        stream.sets.push_back(set);
        auto encoded = stream.encode();
        auto decoded = CompoundPropertySetStream::decode(encoded.data(), encoded.size());
        check(decoded && decoded->sets.size() == 1, "property set round-trip");
        check(decoded->sets[0].properties.size() == 4, "four properties");
        std::string ansi;
        std::string unicode;
        std::vector<std::string> names;
        for (const auto &p : decoded->sets[0].properties) {
            if (p.id == 2)
                ansi = p.value.text;
            if (p.id == 3)
                unicode = p.value.text;
            if (p.id == 4)
                for (const auto &v : p.value.values)
                    names.push_back(v.text);
        }
        check(ansi == "Hello", "ansi Hello");
        check(unicode == "Hi", "unicode Hi");
        check(names.size() == 2 && names[0] == "Sheet1" && names[1] == "Sheet2", "vector LPSTR");
        write_bytes(prefix + "-props.bin", encoded);

        auto push32 = [](std::vector<std::uint8_t> &out, std::uint32_t v) {
            out.push_back(static_cast<std::uint8_t>(v));
            out.push_back(static_cast<std::uint8_t>(v >> 8));
            out.push_back(static_cast<std::uint8_t>(v >> 16));
            out.push_back(static_cast<std::uint8_t>(v >> 24));
        };
        std::vector<std::uint8_t> unpadded;
        push32(unpadded, 0x0000FFFE);
        push32(unpadded, 0);
        unpadded.insert(unpadded.end(), 16, 0);
        push32(unpadded, 1);
        unpadded.insert(unpadded.end(), 16, 0);
        push32(unpadded, 48);
        std::vector<std::uint8_t> section(24, 0);
        push32(section, 0x101E);
        push32(section, 2);
        push32(section, 2);
        section.push_back('A');
        section.push_back(0);
        push32(section, 2);
        section.push_back('B');
        section.push_back(0);
        auto i32_at = section.size();
        push32(section, 3);
        push32(section, 7);
        while (section.size() % 4)
            section.push_back(0);
        auto write32 = [](std::vector<std::uint8_t> &out, std::size_t at, std::uint32_t v) {
            out[at] = static_cast<std::uint8_t>(v);
            out[at + 1] = static_cast<std::uint8_t>(v >> 8);
            out[at + 2] = static_cast<std::uint8_t>(v >> 16);
            out[at + 3] = static_cast<std::uint8_t>(v >> 24);
        };
        write32(section, 0, static_cast<std::uint32_t>(section.size()));
        write32(section, 4, 2);
        write32(section, 8, 2);
        write32(section, 12, 24);
        write32(section, 16, 4);
        write32(section, 20, static_cast<std::uint32_t>(i32_at));
        unpadded.insert(unpadded.end(), section.begin(), section.end());
        auto raw = CompoundPropertySetStream::decode(unpadded.data(), unpadded.size());
        check(raw && !raw->sets.empty(), "unpadded header");
        bool found_vector = false;
        for (const auto &p : raw->sets[0].properties)
            if (p.value.kind == CompoundPropertyValue::Kind::Vector && p.value.values.size() == 2) {
                found_vector = p.value.values[0].text == "A" && p.value.values[1].text == "B";
            }
        check(found_vector, "unpadded VT_LPSTR vector");
        write_bytes(prefix + "-lpstr.bin", unpadded);

        auto expr_doc = DxfDocument::create();
        MTextDefinition mt;
        mt.text = "n";
        mt.insertion = {0, 0, 0};
        mt.height = 2.5;
        auto host = expr_doc.add_mtext(mt);
        auto dict = expr_doc.add_extension_dictionary(hex(host));
        FieldDefinition expr;
        expr.evaluator_id = "AcExpr";
        expr.code = "\\AcExpr 1+2";
        expr.value.type = 4;
        expr.value.text = "3";
        FieldDefinition container;
        container.evaluator_id = "_text";
        container.code = "%<\\_FldIdx 0>%";
        auto parent = expr_doc.add_field(hex(dict), "_text", container);
        expr_doc.add_field_child(hex(parent), expr);
        TestContext ctx;
        check(resolve_field(expr_doc, host, ctx) == std::optional<std::string>{"3"}, "1+2");
        expr.code = "\\AcExpr 1/2";
        expr_doc = DxfDocument::create();
        host = expr_doc.add_mtext(mt);
        dict = expr_doc.add_extension_dictionary(hex(host));
        parent = expr_doc.add_field(hex(dict), "_text", container);
        expr_doc.add_field_child(hex(parent), expr);
        auto half = resolve_field(expr_doc, host, ctx);
        check(half && *half == "0.500000", "1/2");
        expr.code = "\\AcExpr 1/0";
        expr_doc = DxfDocument::create();
        host = expr_doc.add_mtext(mt);
        dict = expr_doc.add_extension_dictionary(hex(host));
        parent = expr_doc.add_field(hex(dict), "_text", container);
        expr_doc.add_field_child(hex(parent), expr);
        check(resolve_field(expr_doc, host, ctx) == std::optional<std::string>{"####"}, "1/0");
        expr.code = "\\AcExpr abs(-4)";
        expr_doc = DxfDocument::create();
        host = expr_doc.add_mtext(mt);
        dict = expr_doc.add_extension_dictionary(hex(host));
        parent = expr_doc.add_field(hex(dict), "_text", container);
        expr_doc.add_field_child(hex(parent), expr);
        check(resolve_field(expr_doc, host, ctx) == std::optional<std::string>{"4.000000"}, "abs");

        auto circle_doc = DxfDocument::create();
        auto circle = circle_doc.add_circle({0, 0, 0}, 1);
        mt.text = "area";
        auto chost = circle_doc.add_mtext(mt);
        auto cdict = circle_doc.add_extension_dictionary(hex(chost));
        FieldDefinition area;
        area.evaluator_id = "AcObjProp";
        area.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(circle) + ">%).Area";
        FieldDefinition ccontainer;
        ccontainer.evaluator_id = "_text";
        ccontainer.code = "%<\\_FldIdx 0>%";
        auto cparent = circle_doc.add_field(hex(cdict), "_text", ccontainer);
        circle_doc.add_field_child(hex(cparent), area);
        auto shown = resolve_field(circle_doc, chost, ctx);
        check(shown && std::abs(std::stod(*shown) - std::acos(-1.0)) < 1e-5, "circle area");

        auto count_doc = DxfDocument::create();
        auto square = [](double s) {
            return std::vector<LwVertex>{{0, 0, 0, 0, 0, 0}, {s, 0, 0, 0, 0, 0}, {s, s, 0, 0, 0, 0}, {0, s, 0, 0, 0, 0}};
        };
        auto s1 = count_doc.add_lwpolyline(square(1), true);
        auto s2 = count_doc.add_lwpolyline(square(2), true);
        count_doc.add_lwpolyline({{0, 0, 0, 0, 0, 0}, {3, 0, 0, 0, 0, 0}, {3, 1, 0, 0, 0, 0}, {0, 1, 0, 0, 0, 0}}, true);
        auto similar = similar_matches(count_doc, s1);
        check(similar.size() == 2, "two similar squares");
        count_doc.add_polyline2d(square(4), true);
        auto similar2 = similar_matches(count_doc, s2);
        check(similar2.size() == 2, "lw squares only");
        auto l1 = count_doc.add_line({0, 0, 0}, {1, 0, 0});
        auto l2 = count_doc.add_line({0, 2, 0}, {1, 2, 0});
        auto grouped = group_matches(count_doc, {l1, l2});
        check(grouped.size() == 1 && grouped[0].size() == 2, "grouped equal lines");
        check(evaluate_count(count_doc, "\\AcCount {\"targets\":[\"" + hex(s1) + "\"],\"type\":\"single\"}") == "2",
              "count similar json");
        SplineDefinition spline;
        spline.degree = 3;
        spline.control_points = {{0, 0, 0}, {1, 1, 0}, {2, 0, 0}, {3, 1, 0}};
        auto sp1 = count_doc.add_spline(spline);
        spline.control_points = {{0, 0, 0}, {2, 2, 0}, {4, 0, 0}, {6, 2, 0}};
        count_doc.add_spline(spline);
        spline.degree = 2;
        spline.control_points = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
        count_doc.add_spline(spline);
        check(similar_matches(count_doc, sp1).size() == 2, "two similar splines");
        auto p3a = count_doc.add_polyline3d({{0, 0, 0}, {1, 0, 1}, {1, 1, 1}}, false);
        count_doc.add_polyline3d({{5, 5, 5}, {6, 5, 6}, {6, 6, 6}}, false);
        check(similar_matches(count_doc, p3a).size() == 2, "3d polylines");

        auto hatch_doc = DxfDocument::create();
        auto solid_square = [](double s) {
            HatchDefinition h;
            h.solid = true;
            h.pattern_name = "SOLID";
            HatchPath path;
            path.flags = 7;
            path.edges = {HatchPolylineEdge{{{0, 0, 0}, {s, 0, 0}, {s, s, 0}, {0, s, 0}}, true}};
            h.paths.push_back(path);
            return h;
        };
        auto hs1 = hatch_doc.add_hatch(solid_square(1));
        hatch_doc.add_hatch(solid_square(2));
        auto rect = solid_square(3);
        rect.paths[0].edges = {HatchPolylineEdge{{{0, 0, 0}, {3, 0, 0}, {3, 1, 0}, {0, 1, 0}}, true}};
        hatch_doc.add_hatch(rect);
        check(similar_matches(hatch_doc, hs1).size() == 2, "two similar solid hatches");
        check(evaluate_count(hatch_doc, "\\AcCount {\"targets\":[\"" + hex(hs1) + "\"],\"type\":\"single\"}") == "2",
              "count similar hatches");
        check(!hatch_outline(solid_square(1)).empty(), "hatch outline");

        auto insert_doc = DxfDocument::create();
        insert_doc.add_symbol(SymbolTable::BlockRecord, "Stick");
        EntityOptions in_block;
        in_block.block_name = "Stick";
        insert_doc.add_line({0, 0, 0}, {1, 0, 0}, in_block);
        InsertDefinition ins;
        ins.block_name = "Stick";
        ins.insertion = {10, 20, 0};
        auto ih = insert_doc.add_insert(ins);
        auto outline = insert_outline(insert_doc, ih);
        check(!outline.empty(), "insert outline");
        auto start = outline.front().start();
        check(std::abs(start.x - 10) < 1e-9 && std::abs(start.y - 20) < 1e-9, "placed outline");
        auto ring = insert_doc.add_lwpolyline(
            {{9, 19, 0, 0, 0, 0}, {12, 19, 0, 0, 0, 0}, {12, 22, 0, 0, 0, 0}, {9, 22, 0, 0, 0, 0}}, true);
        check(valid_boundary(insert_doc, ring), "closed square boundary");
        InsertDefinition far;
        far.block_name = "Stick";
        far.insertion = {100, 100, 0};
        insert_doc.add_insert(far);
        insert_doc.add_layer("A", 7, "Continuous");
        EntityOptions on_a;
        on_a.layer = "A";
        InsertDefinition layered;
        layered.block_name = "Stick";
        layered.insertion = {11, 21, 0};
        insert_doc.add_insert(layered, on_a);
        check(evaluate_count(insert_doc, "\\AcCount {\"name\":\"Stick\",\"type\":\"block\"}") == "3", "three sticks");
        check(evaluate_count(insert_doc, "\\AcCount {\"name\":\"Stick\",\"type\":\"block\",\"boundaryObjectHandle\":\"" +
                                            hex(ring) + "\"}") == "1",
              "boundary count");
        check(evaluate_count(insert_doc, "\\AcCount {\"name\":\"Stick\",\"type\":\"block\",\"key\":{\"layer\":\"A\"}}") ==
                  "1",
              "layer key");

        check(DxfDocument::create().linear_unit_format() == 2, "LUNITS default");
        check(DxfDocument::create().linear_unit_precision() == 4, "LUPREC default");
        check(DxfDocument::create().angular_unit_format() == 0, "AUNITS default");
        check(DxfDocument::create().plot_style_mode(), "PSTYLEMODE default");

        auto attach = [&](DxfDocument &d, FieldDefinition def) {
            MTextDefinition host_mt;
            host_mt.text = "f";
            auto field_host = d.add_mtext(host_mt);
            auto field_dict = d.add_extension_dictionary(hex(field_host));
            FieldDefinition field_container;
            field_container.evaluator_id = "_text";
            field_container.code = "%<\\_FldIdx 0>%";
            auto field_parent = d.add_field(hex(field_dict), "_text", field_container);
            d.add_field_child(hex(field_parent), def);
            return field_host;
        };

        auto picture_doc = DxfDocument::create();
        FieldDefinition half_pic;
        half_pic.evaluator_id = "AcExpr";
        half_pic.code = "\\AcExpr 1/2 \\f \"%lu2%pr2\"";
        auto pic_host = attach(picture_doc, half_pic);
        check(resolve_field(picture_doc, pic_host, ctx) == std::optional<std::string>{"0.50"}, "picture 1/2");
        FieldDefinition bool_pic;
        bool_pic.evaluator_id = "AcExpr";
        bool_pic.code = "\\AcExpr 1+2 \\f \"%bl1\"";
        auto bool_doc = DxfDocument::create();
        check(resolve_field(bool_doc, attach(bool_doc, bool_pic), ctx) == std::optional<std::string>{"True"},
              "picture boolean");
        FieldDefinition area_pic;
        area_pic.evaluator_id = "AcObjProp";
        area_pic.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(circle) + ">%).Area \\f \"%lu2%pr2\"";
        check(resolve_field(circle_doc, attach(circle_doc, area_pic), ctx) == std::optional<std::string>{"3.14"},
              "area picture");

        auto prop_doc = DxfDocument::create();
        auto text_handle = prop_doc.add_text("Hello", {2, 3, 0}, 1);
        FieldDefinition text_string;
        text_string.evaluator_id = "AcObjProp";
        text_string.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(text_handle) + ">%).TextString";
        auto prop_host = attach(prop_doc, text_string);
        check(resolve_field(prop_doc, prop_host, ctx) == std::optional<std::string>{"Hello"}, "TextString");
        FieldDefinition linetype;
        linetype.evaluator_id = "AcObjProp";
        auto lt_doc = DxfDocument::create();
        LineTypeDefinition hidden_type;
        hidden_type.name = "HIDDEN";
        hidden_type.elements = {{0.5, std::nullopt}, {-0.25, std::nullopt}};
        hidden_type.pattern_length = 0.75;
        lt_doc.add_linetype(hidden_type);
        EntityOptions hidden2;
        hidden2.linetype = "HIDDEN";
        auto hidden_line2 = lt_doc.add_line({0, 0, 0}, {1, 0, 0}, hidden2);
        linetype.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(hidden_line2) + ">%).Linetype";
        check(resolve_field(lt_doc, attach(lt_doc, linetype), ctx) == std::optional<std::string>{"HIDDEN"}, "Linetype");
        FieldDefinition bylayer;
        bylayer.evaluator_id = "AcObjProp";
        auto default_line = lt_doc.add_line({0, 1, 0}, {1, 1, 0});
        bylayer.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(default_line) + ">%).Linetype";
        check(resolve_field(lt_doc, attach(lt_doc, bylayer), ctx) == std::optional<std::string>{"ByLayer"},
              "Linetype ByLayer");
        InsertDefinition scaled;
        scaled.block_name = "Stick";
        scaled.scale = {2, 3, 4};
        auto scaled_insert = insert_doc.add_insert(scaled);
        FieldDefinition xscale;
        xscale.evaluator_id = "AcObjProp";
        xscale.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(scaled_insert) + ">%).XScaleFactor";
        check(resolve_field(insert_doc, attach(insert_doc, xscale), ctx) == std::optional<std::string>{"2.000000"},
              "XScaleFactor");

        auto units_doc = DxfDocument::create(false, 6);
        units_doc.add_symbol(SymbolTable::BlockRecord, "Stick", {{70, "4"}});
        std::int16_t stick_units = -1;
        units_doc.for_each<DxfBlockRecord>([&](const DxfBlockRecord &r) {
            if (r.name() == "Stick")
                stick_units = r.units();
        });
        check(stick_units == 4, "block units millimeters");
        EntityOptions in_stick;
        in_stick.block_name = "Stick";
        units_doc.add_line({0, 0, 0}, {1, 0, 0}, in_stick);
        InsertDefinition metric;
        metric.block_name = "Stick";
        metric.insertion = {11, 2, 3};
        metric.scale = {2, 3, 4};
        auto metric_insert = units_doc.add_insert(metric);
        check(evaluate_count(units_doc, "\\AcCount {\"name\":\"Stick\",\"type\":\"block\"}") == "1",
              "COUNT skips BLOCK flags not units");
        units_doc.set_model_space_ucs({10, 0, 0}, {1, 0, 0}, {0, 1, 0});
        FieldDefinition ins_units;
        ins_units.evaluator_id = "AcObjProp";
        ins_units.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(metric_insert) + ">%).InsUnits";
        check(resolve_field(units_doc, attach(units_doc, ins_units), ctx) == std::optional<std::string>{"Millimeters"},
              "InsUnits");
        FieldDefinition ins_factor;
        ins_factor.evaluator_id = "AcObjProp";
        ins_factor.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(metric_insert) + ">%).InsUnitsFactor";
        check(resolve_field(units_doc, attach(units_doc, ins_factor), ctx) == std::optional<std::string>{"0.001000"},
              "InsUnitsFactor");
        FieldDefinition xeff;
        xeff.evaluator_id = "AcObjProp";
        xeff.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(metric_insert) + ">%).XEffectiveScaleFactor";
        check(resolve_field(units_doc, attach(units_doc, xeff), ctx) == std::optional<std::string>{"2000.000000"},
              "XEffectiveScaleFactor");
        FieldDefinition wcs_ins;
        wcs_ins.evaluator_id = "AcObjProp";
        wcs_ins.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(metric_insert) + ">%).InsertionPoint";
        check(resolve_field(units_doc, attach(units_doc, wcs_ins), ctx) ==
                  std::optional<std::string>{"11.000000, 2.000000, 3.000000"},
              "InsertionPoint WCS");
        FieldDefinition ucs_ins;
        ucs_ins.evaluator_id = "AcObjProp.16.2";
        ucs_ins.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(metric_insert) + ">%).InsertionPoint";
        check(resolve_field(units_doc, attach(units_doc, ucs_ins), ctx) ==
                  std::optional<std::string>{"1.000000, 2.000000, 3.000000"},
              "InsertionPoint UCS");
        auto default_mat = units_doc.add_line({0, 4, 0}, {1, 4, 0});
        FieldDefinition bylayer_mat;
        bylayer_mat.evaluator_id = "AcObjProp";
        bylayer_mat.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(default_mat) + ">%).Material";
        check(resolve_field(units_doc, attach(units_doc, bylayer_mat), ctx) == std::optional<std::string>{"ByLayer"},
              "Material ByLayer");

        auto flag_doc = DxfDocument::create();
        auto byblock_line = flag_doc.add_line({0, 0, 0}, {1, 0, 0});
        EntityDisplayProperties byblock_props;
        byblock_props.material_flags = 1;
        flag_doc.set_entity_display_properties(hex(byblock_line), byblock_props);
        FieldDefinition byblock_mat;
        byblock_mat.evaluator_id = "AcObjProp";
        byblock_mat.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(byblock_line) + ">%).Material";
        check(resolve_field(flag_doc, attach(flag_doc, byblock_mat), ctx) == std::optional<std::string>{"ByBlock"},
              "Material ByBlock");
        MaterialDefinition steel;
        steel.name = "Steel";
        auto steel_id = flag_doc.add_material(steel);
        auto named_line = flag_doc.add_line({0, 2, 0}, {1, 2, 0});
        EntityDisplayProperties named_props;
        named_props.material = steel_id;
        named_props.material_flags = 3;
        flag_doc.set_entity_display_properties(hex(named_line), named_props);
        FieldDefinition named_mat;
        named_mat.evaluator_id = "AcObjProp";
        named_mat.code = "\\AcObjProp Object(%<\\_ObjId " + std::to_string(named_line) + ">%).Material";
        check(resolve_field(flag_doc, attach(flag_doc, named_mat), ctx) == std::optional<std::string>{"Steel"},
              "Material named");

        auto block_q = parse_query(R"({"name":"Stick","type":"block"})");
        check(block_q && block_q->kind == CountQuery::Kind::Block && block_q->name == "Stick" && !block_q->boundary,
              "parse block");
        auto single_q = parse_query(R"({"targets":["1A","2B"],"type":"single"})");
        check(single_q && single_q->kind == CountQuery::Kind::Single && single_q->targets.size() == 2 &&
                  single_q->targets[0] == 0x1A,
              "parse single");
        check(!parse_query(R"({"type":"nope"})"), "parse unknown");
        check(!parse_query(R"({"name":"Stick","type":"block","key":{"scale":1}})"), "parse bad scale");

        check(SEMANTIC_INVENTORY_VERSION == 1, "inventory version");
        auto inventory_doc = DxfDocument::create();
        auto inventory_line = inventory_doc.add_line({0, 0, 0}, {1, 0, 0});
        inventory_doc.add_extension_dictionary(hex(inventory_line));
        std::size_t expected_tables = 0;
        for (auto table : {SymbolTable::Layer, SymbolTable::LineType, SymbolTable::TextStyle,
                           SymbolTable::BlockRecord, SymbolTable::DimStyle, SymbolTable::AppId,
                           SymbolTable::View, SymbolTable::VPort, SymbolTable::Ucs})
            expected_tables += inventory_doc.symbols(table).size();
        std::size_t headers = 0, tables = 0, classes = 0, entities = 0, objects = 0, summaries = 0,
                    previews = 0, typed_lines = 0, extension_dicts = 0;
        auto inventory = inventory_doc.semantic_inventory_v1();
        inventory.visit([&](const SemanticPartV1 &part) {
            std::visit(
                [&](const auto &item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, SemanticHeaderV1>)
                        ++headers;
                    else if constexpr (std::is_same_v<T, SemanticTableRecordV1>)
                        ++tables;
                    else if constexpr (std::is_same_v<T, SemanticClassV1>)
                        ++classes;
                    else if constexpr (std::is_same_v<T, SemanticEntityV1>) {
                        ++entities;
                        if (item.typed && item.record && item.record->type() == "LINE")
                            ++typed_lines;
                    } else if constexpr (std::is_same_v<T, SemanticObjectV1>)
                        ++objects;
                    else if constexpr (std::is_same_v<T, SemanticSummaryV1>)
                        ++summaries;
                    else if constexpr (std::is_same_v<T, SemanticPreviewV1>)
                        ++previews;
                    else if constexpr (std::is_same_v<T, SemanticRelationshipV1>) {
                        if (item.kind == SemanticRelationshipKindV1::ExtensionDictionary)
                            ++extension_dicts;
                    }
                },
                part);
        });
        check(headers == 1, "one header");
        check(tables == expected_tables, "all table records");
        check(typed_lines == 1, "typed line");
        check(entities >= 1, "entities");
        check(objects >= 1, "objects");
        check(summaries == 1, "summary");
        check(previews == 0, "no preview");
        check(extension_dicts >= 1, "extension dictionary");
        auto line_node = inventory.resolve(inventory_line);
        check(line_node && line_node->kind == SemanticNodeKindV1::Entity && line_node->record &&
                  line_node->record->type() == "LINE",
              "resolve line");
        auto document_node = inventory.resolve(0);
        check(document_node && document_node->kind == SemanticNodeKindV1::Document, "resolve document");
        check(inventory_doc.summary_info().title.empty(), "empty title");

        std::ofstream fields_out(prefix + "-fields.dxf", std::ios::binary);
        circle_doc.write(fields_out);
        std::ofstream expr_out(prefix + "-expr.dxf", std::ios::binary);
        expr_doc.write(expr_out);
        std::ofstream count_out(prefix + "-count.dxf", std::ios::binary);
        count_doc.write(count_out);
        std::ofstream insert_out(prefix + "-insert.dxf", std::ios::binary);
        insert_doc.write(insert_out);
        std::ofstream picture_out(prefix + "-picture.dxf", std::ios::binary);
        picture_doc.write(picture_out);
        std::ofstream prop_out(prefix + "-objprop.dxf", std::ios::binary);
        prop_doc.write(prop_out);
        std::ofstream hatch_out(prefix + "-hatch.dxf", std::ios::binary);
        hatch_doc.write(hatch_out);
        std::ofstream units_out(prefix + "-units.dxf", std::ios::binary);
        units_doc.write(units_out);
        std::ofstream inventory_out(prefix + "-inventory.dxf", std::ios::binary);
        inventory_doc.write(inventory_out);
        std::ofstream inventory_counts(prefix + "-inventory.txt");
        inventory_counts << "headers=" << headers << "\n"
                         << "tables=" << tables << "\n"
                         << "classes=" << classes << "\n"
                         << "entities=" << entities << "\n"
                         << "objects=" << objects << "\n"
                         << "summaries=" << summaries << "\n"
                         << "previews=" << previews << "\n";

        auto table_grid = [](std::size_t rows, std::size_t cols) {
            TableDefinition t;
            t.insertion = {1, 2, 0};
            t.row_heights.assign(rows, 0.25);
            t.column_widths.assign(cols, 2.5);
            t.cells.assign(rows, std::vector<TableCell>(cols));
            return t;
        };
        auto table_number = [](double n, std::string formatted, std::int32_t type = 2) {
            TableValue v;
            v.type = type;
            v.number = n;
            v.formatted = std::move(formatted);
            return v;
        };
        auto table_doc = DxfDocument::create();
        auto numeric = table_grid(1, 3);
        numeric.cells[0][0].value = table_number(42, "42");
        numeric.cells[0][1].value = table_number(7, "7", 1);
        auto mm = table_number(3, "3.00 mm");
        numeric.cells[0][2].value = mm;
        auto numeric_id = table_doc.add_table(numeric);
        check(table_doc.refresh_table_block(numeric_id), "first table refresh");
        std::vector<std::string> mtexts;
        auto block_of = [&](std::uint64_t id) {
            std::string name;
            table_doc.for_each<DxfTable>([&](const auto &t) {
                if (std::stoull(t.handle(), nullptr, 16) == id)
                    name = t.definition().block_name;
            });
            return name;
        };
        auto numeric_block = block_of(numeric_id);
        auto numeric_owners = table_doc.entity_handles_in_block(numeric_block);
        table_doc.for_each<DxfMText>([&](const auto &m) {
            if (std::find(numeric_owners.begin(), numeric_owners.end(), m.handle()) != numeric_owners.end())
                mtexts.push_back(m.text());
        });
        check(std::find(mtexts.begin(), mtexts.end(), "42") != mtexts.end(), "mtext 42");
        check(std::find(mtexts.begin(), mtexts.end(), "7") != mtexts.end(), "mtext 7");
        check(std::find(mtexts.begin(), mtexts.end(), "3.00 mm") != mtexts.end(), "mtext formatted");
        check(!table_doc.refresh_table_block(numeric_id), "table refresh idempotent");

        auto colored = table_grid(1, 1);
        colored.cells[0][0].value = TableValue{};
        colored.cells[0][0].value->type = 4;
        colored.cells[0][0].value->text = "formatted text";
        colored.cells[0][0].value->formatted = "formatted text";
        TableCellStyle style;
        style.property_flags = 32 | 64;
        style.content_color = Color::from_index(1);
        style.text_style = "Standard";
        colored.cells[0][0].style = style;
        auto colored_id = table_doc.add_table(colored);
        check(table_doc.refresh_table_block(colored_id), "color refresh");
        bool saw_color = false;
        auto colored_block = block_of(colored_id);
        auto colored_owners = table_doc.entity_handles_in_block(colored_block);
        table_doc.for_each<DxfMText>([&](const auto &m) {
            if (std::find(colored_owners.begin(), colored_owners.end(), m.handle()) == colored_owners.end())
                return;
            if (m.text() == "formatted text" && m.color() == Color::from_index(1) && m.definition().style == "Standard")
                saw_color = true;
        });
        check(saw_color, "cell content color");

        auto field_table = table_grid(1, 1);
        auto field_id = table_doc.add_table(field_table);
        FieldDefinition cell_expr;
        cell_expr.evaluator_id = "AcExpr";
        cell_expr.code = "\\AcExpr 2+3";
        cell_expr.value.type = 2;
        cell_expr.value.number = 5;
        cell_expr.value.formatted = "5";
        cell_expr.value_string = "5";
        auto field = table_doc.set_table_cell_field(field_id, 0, 0, "%<\\_FldIdx 0>%", {cell_expr});
        check(field.has_value(), "set table cell field");
        check(cell_field_text(table_doc, *field, field_id, ctx) == std::optional<std::string>{"5"},
              "cell_field_text");
        auto updated = table_doc.update_table_cell_fields(ctx, field_id);
        check(updated.second >= 1, "update found AcExpr");
        check(table_doc.refresh_table_block(field_id), "field table refresh");
        check(table_doc.remove_table_cell_field(field_id, 0, 0), "remove cell field");
        bool kept = false;
        table_doc.for_each<DxfTable>([&](const auto &t) {
            if (std::stoull(t.handle(), nullptr, 16) != field_id)
                return;
            auto cell = t.definition().cells[0][0];
            kept = cell.field == 0 && cell.value && cell.value->text == "5";
        });
        check(kept, "remove keeps display text");
        check(!table_doc.remove_table_cell_field(field_id, 0, 0), "remove missing field");

        TableCellField built;
        built.template_code = "%<\\_FldIdx 0>%";
        built.children = {cell_expr};
        auto field2 = table_grid(1, 1);
        auto field2_id = table_doc.add_table(field2);
        auto built_handles = table_doc.build_table_block(field2_id, {built});
        check(built_handles && built_handles->size() == 1 && (*built_handles)[0], "build_table_block");
        check(cell_field_text(table_doc, *(*built_handles)[0], field2_id, ctx) == std::optional<std::string>{"5"},
              "build cell text");

        auto roundtrip = table_grid(1, 1);
        roundtrip.cells[0][0].value = table_number(1, "1");
        auto roundtrip_id = table_doc.add_table(roundtrip);
        auto xdict = table_doc.add_extension_dictionary(hex(roundtrip_id));
        table_doc.add_xrecord(hex(xdict), "ACAD_XREC_ROUNDTRIP", {{90, "0"}});
        constexpr auto recompose_key = "ACDB_RECOMPOSE_DATA";
        auto recompose_handle = [&](const DxfDocument &doc) {
            return doc.dictionary_entry(doc.root_dictionary_handle(), recompose_key);
        };
        auto source_seed = table_doc.next_handle();
        check(recompose_handle(table_doc).empty(), "recompose starts absent");
        for (bool binary : {false, true}) {
            std::stringstream bytes;
            table_doc.write(bytes, binary);
            auto saved = DxfDocument::read(bytes);
            auto entry = recompose_handle(saved);
            check(entry == hex(source_seed) && saved.next_handle() == source_seed + 1,
                  "write allocates recompose and persists the next handle");
            check(saved.get_record(entry)->type() == "XRECORD", "saved recompose is an XRECORD");
            std::stringstream repeated;
            saved.write(repeated);
            check(repeated.str() == bytes.str(), "recompose save is idempotent");

            check(saved.remove_entity(roundtrip_id), "remove a saved table referenced by recompose");
            std::stringstream after;
            saved.write(after);
            auto without_table = DxfDocument::read(after);
            check(recompose_handle(without_table).empty() && !without_table.get_record(entry),
                  "unused recompose is removed on save");
        }
        check(recompose_handle(table_doc).empty() && table_doc.next_handle() == source_seed,
              "write does not mutate the source document");

        auto conflict = table_doc;
        auto foreign = conflict.add_dictionary(conflict.root_dictionary_handle(), recompose_key);
        std::stringstream conflict_bytes;
        conflict.write(conflict_bytes);
        auto saved_conflict = DxfDocument::read(conflict_bytes);
        check(recompose_handle(saved_conflict) == hex(foreign) &&
                  saved_conflict.get_record(hex(foreign))->type() == "DICTIONARY",
              "write preserves a conflicting object");

        auto style_id = table_doc.add_table_style("Recompose", TableStyleDefinition{});
        auto style_dict = table_doc.add_extension_dictionary(hex(style_id));
        table_doc.add_cell_style_map(hex(style_dict), "ACAD_ROUNDTRIP_2008_TABLESTYLE_CELLSTYLEMAP", {});

        std::ofstream table_out(prefix + "-table.dxf", std::ios::binary);
        table_doc.write(table_out);
        table_doc.prepare_table_recompose_data();
        auto recompose = recompose_handle(table_doc);
        check(!recompose.empty(), "ACDB_RECOMPOSE_DATA");
        const auto *xrec = table_doc.get_record(recompose);
        check(xrec && xrec->type() == "XRECORD", "recompose xrecord");
        table_doc.prepare_table_recompose_data();
        check(recompose_handle(table_doc) == recompose, "recompose unchanged");

        auto host_doc = DxfDocument::create();
        MTextDefinition host_def;
        host_def.text = "cached";
        auto text_host = host_doc.add_mtext(host_def);
        FieldDefinition expr_child;
        expr_child.evaluator_id = "AcExpr";
        expr_child.code = "\\AcExpr 2+3";
        expr_child.value.type = 2;
        expr_child.value.number = 5;
        expr_child.value.formatted = "5";
        expr_child.value_string = "5";
        auto text_container = host_doc.set_text_field(text_host, "%<\\_FldIdx 0>%", {expr_child});
        check(text_container.has_value(), "set_text_field");
        check(resolve_field(host_doc, text_host, ctx) == std::optional<std::string>{"5"}, "hosted resolve");
        bool host_text = false;
        host_doc.for_each<DxfMText>([&](const auto &m) {
            if (std::stoull(m.handle(), nullptr, 16) == text_host)
                host_text = m.text() == "5";
        });
        check(host_text, "hosted mtext text");
        check(host_doc.format_with_picture(0.5, "%lu2%pr2") == "0.50", "format_with_picture");
        check(evaluate_code(host_doc, "\\AcExpr 2+3", {}, text_host, ctx) == std::optional<std::string>{"5"},
              "evaluate_code");
        auto restamp = host_doc.update_fields(ctx, 63);
        check(restamp.second >= 1, "update_fields found");
        FieldDefinition plot;
        plot.evaluator_id = "AcVar";
        plot.code = "\\AcVar PlotDate";
        plot.evaluation_option = 4;
        plot.value.type = 4;
        plot.value.text = "----";
        plot.value.formatted = "----";
        plot.value_string = "----";
        auto plot_host = host_doc.add_mtext(host_def);
        check(host_doc.set_text_field(plot_host, "%<\\_FldIdx 0>%", {plot}).has_value(), "plot field");
        auto stamped = host_doc.stamp_plot_fields(ctx);
        check(!stamped.empty(), "stamp_plot_fields");
        std::ofstream text_out(prefix + "-textfield.dxf", std::ios::binary);
        host_doc.write(text_out);
        check(host_doc.remove_text_field(text_host), "remove_text_field");
        check(!resolve_field(host_doc, text_host, ctx), "removed field");
        host_doc.for_each<DxfMText>([&](const auto &m) {
            if (std::stoull(m.handle(), nullptr, 16) == text_host)
                host_text = m.text() == "5";
        });
        check(host_text, "remove keeps display");

        auto orphan_grid = table_grid(1, 1);
        auto orphan_id = table_doc.add_table(orphan_grid);
        auto orphan_field = table_doc.set_table_cell_field(orphan_id, 0, 0, "%<\\_FldIdx 0>%", {cell_expr});
        check(orphan_field.has_value(), "orphan cell field");
        table_doc.for_each<DxfTable>([&](auto &t) {
            if (std::stoull(t.handle(), nullptr, 16) != orphan_id)
                return;
            auto v = t.definition();
            v.cells[0][0].field = 0;
            t.set_definition(v);
        });
        auto orphans = table_doc.orphan_table_fields(orphan_id);
        check(std::find(orphans.begin(), orphans.end(), *orphan_field) != orphans.end(), "orphan container");
        auto pruned = table_doc.prune_table_fields(orphan_id);
        check(pruned.size() == orphans.size(), "prune size");
        check(table_doc.orphan_table_fields(orphan_id).empty(), "pruned empty");
        bool orphan_gone = true;
        table_doc.for_each<DxfField>([&](const auto &f) {
            if (std::stoull(f.handle(), nullptr, 16) == *orphan_field)
                orphan_gone = false;
        });
        check(orphan_gone, "orphan field removed");

        auto inline_doc = DxfDocument::create();
        auto small = inline_doc.add_circle({0, 0, 0}, 1);
        auto large = inline_doc.add_circle({4, 0, 0}, 2);
        MTextDefinition formula_host_def;
        formula_host_def.text = "cached";
        auto formula_host = inline_doc.add_mtext(formula_host_def);
        const char *sum_code =
            "\\AcExpr (%<\\AcObjProp Object(%<\\_ObjIdx 0>%).Area>%+%<\\AcObjProp "
            "Object(%<\\_ObjIdx 1>%).Area>%) \\f \"%lu2%pr2\"";
        FieldDefinition sum;
        sum.evaluator_id = "AcExpr";
        sum.code = sum_code;
        sum.references = {small, large};
        sum.value.type = 4;
        sum.value.text = "15.71";
        sum.value.formatted = "15.71";
        sum.value_string = "15.71";
        auto formula_container = inline_doc.set_text_field(formula_host, "%<\\_FldIdx 0>%", {sum});
        check(formula_container.has_value(), "formula set_text_field");
        std::size_t objprop = 0, acexpr = 0;
        std::uint64_t formula_handle = 0;
        inline_doc.for_each<DxfField>([&](const auto &f) {
            auto e = f.definition().evaluator_id;
            if (e.rfind("AcObjProp", 0) == 0)
                ++objprop;
            if (e == "AcExpr") {
                ++acexpr;
                if (std::stoull(f.owner_handle(), nullptr, 16) == *formula_container)
                    formula_handle = std::stoull(f.handle(), nullptr, 16);
            }
        });
        check(objprop >= 2, "split operands");
        check(acexpr >= 1, "formula field");
        auto inlined = inline_code(inline_doc, formula_handle);
        check(inlined.has_value(), "inline_code");
        check(inlined->code.find("AcObjProp") != std::string::npos, "inline AcObjProp");
        check(inlined->code.find("_FldIdx") == std::string::npos, "inline no FldIdx");
        check(inlined->objects.size() == 2, "inline objects");
        auto expected_area = inline_doc.format_with_picture(5.0 * std::acos(-1.0), "%lu2%pr2");
        check(resolve_field(inline_doc, formula_host, ctx) == std::optional<std::string>{expected_area},
              "formula resolve");

        auto nested_host = inline_doc.add_mtext(formula_host_def);
        FieldDefinition nested;
        nested.evaluator_id = "AcExpr";
        nested.code =
            "\\AcExpr (%<\\AcExpr (%<\\AcObjProp Object(%<\\_ObjIdx 1>%).Area>%*2)>%+%<\\AcObjProp "
            "Object(%<\\_ObjIdx 0>%).Area>%) \\f \"%lu2%pr2\"";
        nested.references = {small, large};
        auto nested_container = inline_doc.set_text_field(nested_host, "%<\\_FldIdx 0>%", {nested});
        check(nested_container.has_value(), "nested set_text_field");
        std::uint64_t nested_formula = 0;
        inline_doc.for_each<DxfField>([&](const auto &f) {
            if (f.definition().evaluator_id == "AcExpr" &&
                std::stoull(f.owner_handle(), nullptr, 16) == *nested_container)
                nested_formula = std::stoull(f.handle(), nullptr, 16);
        });
        auto nested_inlined = inline_code(inline_doc, nested_formula);
        check(nested_inlined.has_value(), "nested inline_code");
        check(nested_inlined->code ==
                  "\\AcExpr (%<\\AcExpr (%<\\AcObjProp Object(%<\\_ObjIdx 0>%).Area>%*2)>%+%<\\AcObjProp "
                  "Object(%<\\_ObjIdx 1>%).Area>%) \\f \"%lu2%pr2\"",
              "nested inline text");
        check(nested_inlined->objects.size() == 2, "nested objects");
        check(nested_inlined->objects[0] == large && nested_inlined->objects[1] == small, "nested object order");

        auto span_host = inline_doc.add_mtext(formula_host_def);
        FieldDefinition one;
        one.evaluator_id = "AcExpr";
        one.code = "\\AcExpr 1";
        one.value.type = 2;
        one.value.number = 1;
        one.value.formatted = "1";
        one.value.text = "1";
        one.value_string = "1";
        FieldDefinition two = one;
        two.code = "\\AcExpr 2";
        two.value.number = 2;
        two.value.formatted = "2";
        two.value.text = "2";
        two.value_string = "2";
        check(inline_doc.set_text_field(span_host, "%<\\_FldIdx 0>%+%<\\_FldIdx 1>%", {one, two}).has_value(),
              "span field");
        std::string span_text;
        inline_doc.for_each<DxfMText>([&](const auto &m) {
            if (std::stoull(m.handle(), nullptr, 16) == span_host)
                span_text = m.text();
        });
        auto spans = field_spans(inline_doc, span_host, span_text);
        check(spans && spans->size() == 2, "field_spans count");
        check((*spans)[0] == std::make_pair(std::size_t{0}, std::size_t{1}) &&
                  (*spans)[1] == std::make_pair(std::size_t{2}, std::size_t{3}),
              "field_spans ranges");
        std::ofstream inline_out(prefix + "-inline.dxf", std::ios::binary);
        inline_doc.write(inline_out);

        auto attr_doc = DxfDocument::create();
        auto attr_small = attr_doc.add_circle({0, 0, 0}, 1);
        auto attr_large = attr_doc.add_circle({4, 0, 0}, 2);
        attr_doc.add_symbol(SymbolTable::BlockRecord, "ROOM");
        EntityOptions in_room;
        in_room.block_name = "ROOM";
        AttributeDefinition attdef;
        attdef.tag = "AREA";
        attdef.prompt = "Area";
        attdef.text.text = "?";
        auto def_handle = attr_doc.add_attribute_definition(attdef, in_room);
        FieldDefinition attr_sum = sum;
        attr_sum.references = {attr_small, attr_large};
        check(attr_doc.set_text_field(def_handle, "%<\\_FldIdx 0>%", {attr_sum}).has_value(), "attdef field");
        InsertDefinition room;
        room.block_name = "ROOM";
        AttributeDefinition attrib = attdef;
        attrib.text.text.clear();
        auto room_id = attr_doc.add_insert(room, {attrib});
        auto copied = attr_doc.attach_attribute_fields(room_id, ctx);
        check(copied.size() == 1, "attach_attribute_fields");
        check(resolve_field(attr_doc, copied[0], ctx) == std::optional<std::string>{expected_area},
              "attached resolve");
        auto mapped = attr_doc.attach_attribute_fields_mapped(
            room_id, ctx, [](std::string_view s) { return std::string(s); });
        check(mapped.size() == 1, "attach mapped");
        std::ofstream attr_out(prefix + "-attrfield.dxf", std::ios::binary);
        attr_doc.write(attr_out);

        auto group_doc = DxfDocument::create();
        auto line1 = group_doc.add_line({0, 0, 0}, {1, 0, 0});
        auto line2 = group_doc.add_line({1, 0, 0}, {1, 1, 0});
        auto group_h = group_doc.create_group("SteelBeams", {line1, line2});
        auto named = group_doc.get_group("steelbeams");
        check(named && named->name == "SteelBeams" && named->handle == group_h, "get_group");
        check(named->definition.entities == std::vector<std::uint64_t>({line1, line2}), "group members");
        check(group_doc.get_group_by_handle(group_h) && group_doc.get_group_by_handle(group_h)->name == "SteelBeams",
              "get_group_by_handle");
        auto line3 = group_doc.add_line({1, 1, 0}, {0, 0, 0});
        group_doc.add_entity_to_group("SteelBeams", line3);
        check(group_doc.get_group("SteelBeams")->definition.entities.size() == 3, "add_entity_to_group");
        group_doc.remove_entity_from_group("SteelBeams", line2);
        auto after_remove = group_doc.get_group("SteelBeams")->definition.entities;
        check(std::find(after_remove.begin(), after_remove.end(), line2) == after_remove.end() &&
                  after_remove.size() == 2,
              "remove_entity_from_group");
        auto unnamed_h = group_doc.create_unnamed_group({line1});
        auto unnamed = group_doc.get_group_by_handle(unnamed_h);
        check(unnamed && unnamed->definition.unnamed && unnamed->name.rfind("*A", 0) == 0, "unnamed group");
        auto removed = group_doc.remove_group("SteelBeams");
        check(removed && removed->name == "SteelBeams" && !group_doc.get_group("SteelBeams"), "remove_group");
        bool empty_name = false;
        try {
            group_doc.create_group("", {line1});
        } catch (const std::invalid_argument &) {
            empty_name = true;
        }
        check(empty_name, "empty group name");
        auto roundtrip_line = group_doc.add_line({2, 0, 0}, {3, 0, 0});
        group_doc.create_group("GroupRoundtrip", {roundtrip_line});
        std::ofstream group_out(prefix + "-groups.dxf", std::ios::binary);
        group_doc.write(group_out);

        auto recorder_doc = DxfDocument::create();
        auto recorder = recorder_doc.begin_entity_change_recording();
        auto recorded_line = recorder_doc.add_line({0, 0, 0}, {1, 0, 0});
        auto added = recorder->take_before_images();
        check(added.size() == 1 && added[0].first == recorded_line && !added[0].second, "add before-image");
        recorder_doc.record_all_entities_for_transaction();
        auto snapshot = recorder->take_before_images();
        check(snapshot.size() == 1 && snapshot[0].first == recorded_line && snapshot[0].second == std::optional<std::string>{"LINE"},
              "record_all type");
        auto remove_recorder = recorder_doc.begin_entity_change_recording();
        check(recorder_doc.remove_entity(recorded_line), "remove recorded");
        auto removed_images = remove_recorder->take_before_images();
        check(removed_images.size() == 1 && removed_images[0].second == std::optional<std::string>{"LINE"},
              "remove before-image");
        recorder_doc.end_entity_change_recording();

        auto mesh_doc = DxfDocument::create();
        auto mesh = mesh_doc.add_mesh(MeshDefinition::create_box({0, 0, 0}, {1, 1, 1}));
        mesh_doc.set_mesh_texture_coordinates(mesh, {{0.1, 0.2, 0.3}, {0.4, 0.5, 0.6}});
        auto uvs = mesh_doc.mesh_texture_coordinates(mesh);
        check(uvs.size() == 2 && std::abs(uvs[0].x - 0.1) < 1e-12 && std::abs(uvs[1].z - 0.6) < 1e-12, "mesh UVW");
        mesh_doc.set_mesh_texture_coordinates(mesh, {{0.7, 0.8, 0.9}});
        check(mesh_doc.mesh_texture_coordinates(mesh).size() == 1, "mesh UVW replace");
        mesh_doc.set_mesh_texture_coordinates(mesh, {{0.1, 0.2, 0.3}, {0.4, 0.5, 0.6}});
        std::ofstream mesh_out(prefix + "-meshuv.dxf", std::ios::binary);
        mesh_doc.write(mesh_out);

        auto vp_doc = DxfDocument::create();
        EntityOptions paper;
        paper.space = EntitySpace::Paper;
        auto viewport = vp_doc.add_viewport({}, paper);
        auto layer0 = std::stoull(vp_doc.get_symbol(SymbolTable::Layer, "0")->handle, nullptr, 16);
        check(vp_doc.set_layer_viewport_override(layer0, LayerViewportOverrideKind::Color, viewport,
                                                 XRecordOverrideValue::from_int32(1)),
              "color override");
        check(!vp_doc.set_layer_viewport_override(layer0, LayerViewportOverrideKind::Color, viewport,
                                                  XRecordOverrideValue::from_handle(viewport)),
              "color override type");
        check(vp_doc.set_layer_viewport_override(layer0, LayerViewportOverrideKind::Color, viewport,
                                                 XRecordOverrideValue::from_int32(2)),
              "color override update");
        auto color_ovr = vp_doc.layer_viewport_overrides(layer0, LayerViewportOverrideKind::Color);
        check(color_ovr.size() == 1 && color_ovr[0].viewport == viewport &&
                  color_ovr[0].value.kind == XRecordOverrideValue::Kind::Int32 && color_ovr[0].value.i32 == 2,
              "layer color override");
        auto continuous = std::stoull(vp_doc.get_symbol(SymbolTable::LineType, "Continuous")->handle, nullptr, 16);
        check(vp_doc.set_layer_viewport_override(layer0, LayerViewportOverrideKind::Linetype, viewport,
                                                 XRecordOverrideValue::from_handle(continuous)),
              "linetype override");
        check(vp_doc.set_layer_viewport_override(layer0, LayerViewportOverrideKind::Lineweight, viewport,
                                                 XRecordOverrideValue::from_int32(25)),
              "lineweight override");
        check(vp_doc.set_layer_viewport_override(layer0, LayerViewportOverrideKind::Alpha, viewport,
                                                 XRecordOverrideValue::from_int32(128)),
              "alpha override");
        ScaleDefinition scale;
        scale.name = "1:2";
        scale.paper_units = 1;
        scale.drawing_units = 2;
        auto scale_h = vp_doc.add_auxiliary_object(vp_doc.root_dictionary_handle(), "AnnoScale", scale);
        vp_doc.set_viewport_annotation_scale(viewport, scale_h);
        check(vp_doc.viewport_annotation_scale(viewport) == scale_h, "viewport annotation scale");
        std::ofstream vp_out(prefix + "-vpxrec.dxf", std::ios::binary);
        vp_doc.write(vp_out);

        auto recover_source = DxfDocument::create();
        recover_source.add_line({0, 0, 0}, {10, 0, 0});
        std::ostringstream recover_bytes;
        recover_source.write(recover_bytes);
        {
            std::istringstream in(recover_bytes.str());
            auto recovered = DxfDocument::recover(in);
            check(recovered.stats.has_usable_drawing_data(), "recover usable");
            check(recovered.repair.after.scanned_entities > 0, "recover repair scanned");
            std::size_t lines = 0;
            recovered.document.for_each<DxfLine>([&](const auto &) { ++lines; });
            check(lines == 1, "recover line");
        }
        std::ofstream recover_out(prefix + "-recover.dxf", std::ios::binary);
        recover_out << recover_bytes.str();

        const std::string malformed =
            "0\nSECTION\n2\nENTITIES\n0\nLINE\n5\n1\n10\n0\nBAD-CODE\nx\n0\nPOINT\n5\n2\n10\n3\n20\n4\n0\nENDSEC\n0\n"
            "EOF\n";
        std::ofstream malformed_out(prefix + "-malformed.dxf", std::ios::binary);
        malformed_out << malformed;
        {
            std::istringstream in(malformed);
            auto recovered = DxfDocument::recover(in);
            check(recovered.stats.has_usable_drawing_data() && recovered.stats.recovered(), "malformed recovered");
            check(recovered.stats.skipped_records == 1, "malformed skipped");
            check(recovered.document.get_record("2") && recovered.document.get_record("2")->type() == "POINT",
                  "malformed POINT");
            check(!recovered.document.get_record("1"), "malformed LINE skipped");
            std::ofstream recovered_out(prefix + "-recovered.dxf", std::ios::binary);
            recovered.document.write(recovered_out);
        }
        bool empty_failed = false;
        try {
            std::istringstream empty("");
            DxfDocument::recover(empty);
        } catch (const DxfRecoveryError &) {
            empty_failed = true;
        }
        check(empty_failed, "empty recover");
        std::ofstream empty_out(prefix + "-empty.dxf", std::ios::binary);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
