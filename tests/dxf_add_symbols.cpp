// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "../src/dxf_binary.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
DxfDocument read(const std::string& bytes) { std::istringstream in(bytes); return DxfDocument::read(in); }
std::string save(const DxfDocument& doc) { std::ostringstream out; doc.write(out); return out.str(); }
std::string hex(std::uint64_t n) { std::ostringstream out; out << std::hex << std::uppercase << n; return out.str(); }
template<class Fn> void fails_atomically(DxfDocument& doc, Fn action) {
    const auto before = save(doc);
    const auto seed = doc.next_handle();
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    check(failed && save(doc) == before && doc.next_handle() == seed, "failure changed document");
}
// Count actual entries independently of the HEADER's declared count.
void counts(const DxfDocument& doc, int layers, int linetypes) {
    std::istringstream in(save(doc));
    if (doc.is_binary()) in.ignore(detail::binary_signature.size());
    std::string kind, table;
    int declared = -1, actual = 0, checked = 0;
    while (in.peek() != std::char_traits<char>::eof()) {
        int code;
        std::string value;
        if (doc.is_binary()) {
            code = detail::read_binary_code(in, in.get(), false);
            value = detail::read_binary_value(in, code);
        } else {
            std::string code_text;
            std::getline(in, code_text); code = std::stoi(code_text);
            std::getline(in, value);
            if (!value.empty() && value.back() == '\r') value.pop_back();
        }
        if (code == 0) {
            kind = value;
            if (kind == "TABLE") { table.clear(); actual = 0; declared = -1; }
            else if (kind == "ENDTAB") {
                if (table == "LAYER" || table == "LTYPE") {
                    check(declared == actual, "table count must equal real entry count");
                    check(actual == (table == "LAYER" ? layers : linetypes), "unexpected table entry count");
                    ++checked;
                }
                table.clear();
            } else if (kind == table) ++actual;
        }
        if (kind == "TABLE" && code == 2) table = value;
        if (kind == "TABLE" && code == 70)
            declared = doc.is_binary() ? detail::binary_short(value) : std::stoi(value);
    }
    check(checked == 2, "LAYER and LTYPE tables");
}
LineTypeDefinition definition() {
    LineTypeComplexData text;
    text.content = LineTypeComplexData::Content::Text;
    text.text = "给水";
    text.scale = 0.5; text.rotation = 0.25;
    text.offset = {0.1, -0.2}; text.upright = true;
    LineTypeComplexData shape;
    shape.shape_number = 7; shape.scale = 2; shape.absolute_rotation = true; shape.source_flags = 0x80;
    return {"新增线型A", "新增划点文字", {{2, {}}, {-0.5, {}}, {0, {}}, {-0.5, text}, {0, shape}}, 3, false};
}
void boundaries() {
    auto empty = read("0\nEOF\n");
    const auto h = empty.add_layer("建筑A", 3);
    check(empty.get_layer("建筑a")->handle() == hex(h), "new layer");
    check(empty.get_linetype("continuous"), "automatic Continuous");
    check(empty.get_record(hex(h)) == empty.get_layer("建筑A"), "new entry index");
    check(empty.ownership_chain(hex(h)).status == OwnershipStatus::Root, "new table owner");
    counts(empty, 1, 1);
    fails_atomically(empty, [&] { empty.add_layer("建筑a"); });
    fails_atomically(empty, [&] { empty.add_linetype({"CONTINUOUS", "", {}, 0}); });
    fails_atomically(empty, [&] { empty.add_layer("bad", 0); });
    fails_atomically(empty, [&] { empty.add_layer("bad", 7, "missing"); });
    auto invalid = definition();
    invalid.elements.back().complex->scale = std::numeric_limits<double>::quiet_NaN();
    fails_atomically(empty, [&] { empty.add_linetype(invalid); });
    auto invalid_shape = definition();
    invalid_shape.elements.back().complex->shape_number = 32768;
    fails_atomically(empty, [&] { empty.add_linetype(invalid_shape); });
    auto ambiguous = read("0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n5\n20\n0\nENDTAB\n"
        "0\nENDSEC\n0\nSECTION\n2\nOBJECTS\n0\nXRECORD\n5\n20\n0\nENDSEC\n0\nEOF\n");
    fails_atomically(ambiguous, [&] { ambiguous.add_linetype({"test", "", {}, 0}); });
    auto exhausted = read("0\nSECTION\n2\nHEADER\n9\n$HANDSEED\n5\nFFFFFFFFFFFFFFFE\n0\nENDSEC\n0\nEOF\n");
    fails_atomically(exhausted, [&] { exhausted.add_linetype({"test", "", {}, 0}); });
    auto no_handle = read("0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n70\n99\n"
        "0\nLTYPE\n5\nFF\n2\nContinuous\n0\nENDTAB\n0\nENDSEC\n0\nEOF\n");
    no_handle.add_layer("新层");
    no_handle.add_linetype({"dash", "", {{1,{}},{-1,{}}}, 2});
    counts(no_handle, 1, 2);
    auto legacy = read("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1018\n9\n$DWGCODEPAGE\n3\nANSI_936\n"
        "0\nENDSEC\n0\nEOF\n");
    legacy.add_layer("中文", -3);
    check(read(save(legacy)).get_layer("中文")->signed_color_index() == -3, "GBK new entry");
    auto source = read("0\nSECTION\n2\nENTITIES\n0\nPOLYLINE\n5\n100\n0\nVERTEX\n5\n101\n"
        "0\nSEQEND\n5\n102\n0\nENDSEC\n0\nEOF\n");
    source.add_layer("test");
    check(source.get_record("101")->type() == "VERTEX" && source.get_record("102")->type() == "SEQEND",
        "child indexes after insert");
    auto reopened = read(save(source));
    check(reopened.next_handle() == source.next_handle(), "seed after reopen");
    check(reopened.get_record("101")->type() == "VERTEX", "child after reopen");
}
void fixture(const char* format, const char* path, const char* output) {
    std::ifstream in(path, std::ios::binary);
    check(in.is_open(), "input");
    auto doc = DxfDocument::read(in);
    check(doc.is_binary() == (std::string_view(format) == "binary"), "format");
    const auto original_model = doc.model_space_entity_handles();
    const auto original_paper = doc.paper_space_entity_handles();
    const auto layer_owner = doc.get_layer("0")->owner_handle();
    const auto linetype_owner = doc.get_linetype("Continuous")->owner_handle();
    const auto seed = doc.next_handle();
    const auto lt = doc.add_linetype(definition());
    check(lt == seed, "first allocation");
    fails_atomically(doc, [&] { auto duplicate = definition(); duplicate.name = "新增线型a"; doc.add_linetype(duplicate); });
    const auto layer = doc.add_layer("新增图层A", -5, "新增线型A");
    check(layer == lt + 1 && doc.next_handle() == layer + 1, "unique continuous allocation");
    fails_atomically(doc, [&] { doc.add_layer("新增图层a"); });
    check(doc.get_linetype("新增线型a")->handle() == hex(lt), "linetype lookup");
    check(doc.get_layer("新增图层a")->handle() == hex(layer), "layer lookup");
    check(doc.get_layer("新增图层A")->owner_handle() == layer_owner, "LAYER table owner");
    check(doc.get_linetype("新增线型A")->owner_handle() == linetype_owner, "LTYPE table owner");
    check(doc.ownership_chain(hex(lt)).status == OwnershipStatus::Root, "linetype owner");
    check(doc.ownership_chain(hex(layer)).status == OwnershipStatus::Root, "layer owner");
    check(doc.model_space_entity_handles() == original_model && doc.paper_space_entity_handles() == original_paper,
        "space indexes after insertion");
    check(doc.duplicate_handles().empty(), "unique identities");
    counts(doc, 2, 4);
    bool changed = false;
    doc.for_each<DxfLine>([&](auto& line) {
        if (line.handle() == "31") { line.set_layer("新增图层A"); line.set_linetype("新增线型A"); changed = true; }
    });
    check(changed, "target entity");
    const auto bytes = save(doc);
    const auto reopened = read(bytes);
    check(reopened.get_layer("新增图层A")->handle() == hex(layer), "layer persisted");
    counts(reopened, 2, 4);
    std::ofstream out;
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out.open(output, std::ios::binary); out << bytes; out.close();
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) boundaries();
        else { check(argc == 4, "expected format input output"); fixture(argv[1], argv[2], argv[3]); }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
