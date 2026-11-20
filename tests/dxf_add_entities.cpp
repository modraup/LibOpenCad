// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string save(const DxfDocument& doc) { std::ostringstream out; doc.write(out); return out.str(); }
DxfDocument read(const std::string& bytes) { std::istringstream in(bytes); return DxfDocument::read(in); }
std::string hex(std::uint64_t n) { std::ostringstream out; out << std::hex << std::uppercase << n; return out.str(); }
template<class Fn> void fails(DxfDocument& doc, Fn fn) {
    const auto bytes = save(doc);
    const auto seed = doc.next_handle();
    bool failed = false;
    try { fn(); } catch (const std::exception&) { failed = true; }
    check(failed && save(doc) == bytes && doc.next_handle() == seed, "failure must be atomic");
}
void replace(std::string& text, const std::string& from, const std::string& to) {
    const auto pos = text.find(from);
    check(pos != std::string::npos, "fixture replacement");
    text.replace(pos, from.size(), to);
}
void boundaries() {
    for (bool binary : {false, true}) {
        auto doc = DxfDocument::create(binary);
        EntityOptions invalid;
        invalid.layer = "unknown";
        fails(doc, [&] { doc.add_line({}, {}, invalid); });
        invalid.layer = "0"; invalid.linetype = "unknown";
        fails(doc, [&] { doc.add_circle({}, 1, invalid); });
        invalid.linetype.clear(); invalid.space = static_cast<EntitySpace>(99);
        fails(doc, [&] { doc.add_text("text", {}, 1, invalid); });
        const double nan = std::numeric_limits<double>::quiet_NaN();
        fails(doc, [&] { doc.add_line({}, {1, nan, 2}); });
        fails(doc, [&] { doc.add_circle({}, nan); });
        fails(doc, [&] { doc.add_text("text", {}, nan); });
        // Name lookup uses the same canonical symbol-table spelling as queries.
        doc.add_layer("Geometry");
        EntityOptions options; options.layer = "gEOMETRY"; options.linetype = "continuous";
        const auto h = doc.add_line({}, {1, 2, 3}, options);
        doc.for_each<DxfLine>([&](const auto& line) {
            check(line.layer() == "Geometry" && line.linetype() == "Continuous", "canonical names");
        });
        auto copy = doc;
        copy.add_circle({}, 1);
        check(doc.get_record(hex(h)) && doc.model_space_entity_handles().size() == 1, "copy isolation");
        check(copy.model_space_entity_handles().size() == 2, "copy insertion index");
    }
    const auto original = save(DxfDocument::create());
    const std::string empty_entities = "  0\r\nSECTION\r\n  2\r\nENTITIES\r\n  0\r\nENDSEC\r\n";
    auto bytes = original;
    replace(bytes, empty_entities, "");
    auto no_section = read(bytes);
    no_section.add_line({}, {1, 2, 3});
    check(read(save(no_section)).model_space_entity_handles().size() == 1, "create ENTITIES");
    bytes = original;
    replace(bytes, empty_entities, empty_entities + empty_entities);
    auto duplicate_section = read(bytes);
    fails(duplicate_section, [&] { duplicate_section.add_line({}, {}); });
    bytes = original;
    replace(bytes, "*Model_Space", "*Missing_Space");
    auto missing = read(bytes);
    fails(missing, [&] { missing.add_line({}, {}); });
    bytes = original;
    replace(bytes, "  5\r\n18\r\n", "  5\r\n15\r\n");
    auto duplicate_owner = read(bytes);
    fails(duplicate_owner, [&] { duplicate_owner.add_line({}, {}); });
    bytes = original;
    replace(bytes, "$HANDSEED\r\n  5\r\n32\r\n", "$HANDSEED\r\n  5\r\nFFFFFFFFFFFFFFFF\r\n");
    auto exhausted = read(bytes);
    fails(exhausted, [&] { exhausted.add_circle({}, 1); });
    bytes = original;
    replace(bytes, "AcDbTextStyleTableRecord\r\n  2\r\nStandard", "AcDbTextStyleTableRecord\r\n  2\r\nOther");
    auto no_style = read(bytes);
    fails(no_style, [&] { no_style.add_text("text", {}, 1); });
    // Preserve unknown records and compound children when rebuilding indexes.
    bytes = original;
    replace(bytes, empty_entities, "0\nSECTION\n2\nENTITIES\n0\nPOLYLINE\n5\n100\n330\n15\n"
        "0\nVERTEX\n5\n101\n330\n100\n0\nSEQEND\n5\n102\n330\n100\n"
        "0\nCUSTOM\n5\n103\n330\n18\n1\nopaque\n0\nENDSEC\n");
    auto existing = read(bytes);
    existing.add_line({}, {});
    check(existing.get_record("101")->owner_handle() == "100", "child index retained");
    check(existing.get_record("103")->type() == "CUSTOM", "unknown retained");
    check(read(save(existing)).get_record("102")->type() == "SEQEND", "sequence retained");
    // The same constructor must use the source code page in an existing legacy drawing.
    bytes = original;
    replace(bytes, "AC1032", "AC1018");
    replace(bytes, "ANSI_1252", "ANSI_936");
    auto gbk = read(bytes);
    gbk.add_layer("中文");
    EntityOptions chinese; chinese.layer = "中文";
    gbk.add_text("文字", {1,2,3}, 2, chinese);
    check(save(gbk).find(std::string("\xCE\xC4\xD7\xD6")) != std::string::npos, "new text GBK bytes");
    read(save(gbk)).for_each<DxfText>([](const auto& t) { check(t.text() == "文字" && t.layer() == "中文", "new GBK text"); });
}
void verify(const DxfDocument& doc) {
    check(doc.next_handle() == 0x3A && doc.duplicate_handles().empty(), "unique handles and seed");
    check(doc.model_space_entity_handles() == std::vector<std::string>({"34","35","36"}), "model members");
    check(doc.paper_space_entity_handles() == std::vector<std::string>({"37","38","39"}), "paper members");
    int layers = 0, entities = 0, lines = 0, circles = 0, texts = 0;
    doc.for_each<DxfLayer>([&](const auto&) { ++layers; });
    check(layers == 2 && doc.get_layer("标注"), "exactly two layers including 0");
    doc.for_each<DxfEntity>([&](const auto& entity) {
        ++entities;
        const auto handle = std::stoull(entity.handle(), nullptr, 16);
        check(entity.owner_handle() == (handle < 0x37 ? "15" : "18"), "explicit owner");
        check(doc.get_record(entity.handle()) == &entity, "entity index");
        check(doc.ownership_chain(entity.handle()).status == OwnershipStatus::Root, "owner chain");
    });
    doc.for_each<DxfLine>([&](const auto& line) {
        const int n = lines++;
        check(line.start() == Point3{1. + 100*n, 2, 3. + n} && line.end() == Point3{10. + 100*n, 20, 6. + n}, "line XYZ");
        check(line.color() == Color::from_rgb(12,34,56) && line.linetype() == "建筑虚线" &&
            line.line_weight().value() == 35, "line properties");
    });
    doc.for_each<DxfCircle>([&](const auto& circle) {
        const int n = circles++;
        check(circle.center() == Point3{-2. + 100*n,4,1. + n} && circle.radius() == 3.5+n, "circle geometry");
        check(circle.color() == Color::by_block() && circle.is_invisible() == bool(n), "circle properties");
    });
    doc.for_each<DxfText>([&](const auto& text) {
        const int n = texts++;
        check(text.insertion_point() == Point3{3. + 100*n,5,7. + n} && text.height() == 2.5+n, "text geometry");
        check(text.text() == (n ? "图纸：说明" : "模型：尺寸 ^J") && text.layer() == "标注", "text and layer");
    });
    check(entities == 6 && lines == 2 && circles == 2 && texts == 2, "entity counts");
}
void fixture(bool binary, const char* path) {
    auto doc = DxfDocument::create(binary, 4);
    check(doc.add_layer("标注", 2) == 0x32, "layer handle");
    check(doc.add_linetype({"建筑虚线", "dash gap", {{1,{}},{-0.5,{}}}, 1.5}) == 0x33, "linetype handle");
    for (int n = 0; n < 2; ++n) {
        EntityOptions options;
        options.space = n ? EntitySpace::Paper : EntitySpace::Model;
        options.color = Color::from_rgb(12,34,56); options.linetype = "建筑虚线";
        options.line_weight = LineWeight::from_value(35);
        check(doc.add_line({1. + 100*n,2,3. + n}, {10. + 100*n,20,6. + n}, options) == std::uint64_t(0x34 + 3*n), "line handle");
        options.color = Color::by_block(); options.linetype.clear();
        options.line_weight = LineWeight::default_weight(); options.invisible = n != 0;
        check(doc.add_circle({-2. + 100*n,4,1. + n}, 3.5+n, options) == std::uint64_t(0x35 + 3*n), "circle handle");
        options.layer = "标注"; options.color = Color::from_index(4);
        options.line_weight = LineWeight::by_layer(); options.invisible = false;
        check(doc.add_text(n ? "图纸：说明" : "模型：尺寸 ^J", {3. + 100*n,5,7. + n}, 2.5+n, options) == std::uint64_t(0x36 + 3*n), "text handle");
    }
    verify(doc);
    verify(read(save(doc)));
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(path, std::ios::binary); doc.write(out); out.close();
    // The default overload needs no options; editing remains available after insertion.
    auto editable = DxfDocument::create(binary);
    editable.add_line({}, {1,2,3});
    editable.add_circle({}, 2);
    editable.add_text("original", {}, 1);
    editable.for_each<DxfLine>([](auto& line) { line.set_end({4,5,6}); });
    editable.for_each<DxfCircle>([](auto& circle) { circle.set_radius(7); });
    editable.for_each<DxfText>([](auto& text) { text.set_text("修改"); });
    auto reopened = read(save(editable));
    reopened.for_each<DxfLine>([](const auto& line) { check(line.end() == Point3{4,5,6}, "edit new line"); });
    reopened.for_each<DxfCircle>([](const auto& circle) { check(circle.radius() == 7, "edit new circle"); });
    reopened.for_each<DxfText>([](const auto& text) { check(text.text() == "修改", "edit new text"); });
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) boundaries();
        else { check(argc == 3, "expected format output"); fixture(std::string_view(argv[1]) == "binary", argv[2]); }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
