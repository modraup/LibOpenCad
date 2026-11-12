// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
DxfDocument read(const std::string& bytes) {
    std::istringstream input(bytes);
    return DxfDocument::read(input);
}
void boundaries() {
    auto doc = read("0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n"
        "0\nLAYER\n5\nA\n2\nNotes\n0\nLAYER\n5\nB\n2\nnOtEs\n"
        "0\nLAYER\n5\nC\n2\n𐐀\n0\nENDTAB\n0\nTABLE\n2\nLTYPE\n"
        "0\nLTYPE\n5\nD\n2\nDashed\n0\nLTYPE\n5\nE\n2\nDASHED\n"
        "73\n99\n40\n3\n49\n2\n74\n0\n49\n-1\n74\n0\n"
        "102\n{APP\n49\n999\n102\n}\n1001\nAPP\n49\n999\n"
        "0\nENDTAB\n0\nENDSEC\n0\nEOF\n");
    check(doc.get_layer("NOTES")->handle() == "B", "last matching layer");
    check(doc.get_layer("𐐨")->handle() == "C", "non-BMP case mapping");
    check(!doc.get_layer(" Notes") && !doc.get_layer("Notes "), "names retain whitespace");
    check(doc.get_linetype("dashed")->handle() == "E", "last matching linetype");
    auto elements = doc.get_linetype("dashed")->elements();
    check(elements.size() == 2 && elements[0].length == 2 && elements[1].length == -1, "actual elements and opaque data");
    check(!elements[0].complex && !elements[1].complex, "plain flag zero");
    check(!doc.get_layer("") && !doc.get_linetype("missing"), "unknown name");
    try {
        doc.get_layer(std::string(1, char(0xff)));
        throw std::runtime_error("invalid UTF-8 accepted");
    } catch (const std::invalid_argument&) {}
    auto legacy = read("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1018\n"
        "9\n$DWGCODEPAGE\n3\nANSI_936\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n"
        "0\nTABLE\n2\nLAYER\n0\nLAYER\n5\n10\n2\n\xBD\xA8\xD6\xFE" "A\n"
        "0\nENDTAB\n0\nENDSEC\n0\nEOF\n");
    check(legacy.get_layer("建筑a")->handle() == "10", "GBK decoded name");
}
void fixture(const char* format, const char* file, const char* result) {
    std::ifstream input(file, std::ios::binary);
    check(input.is_open(), "input");
    std::string bytes(std::istreambuf_iterator<char>{input}, {});
    auto doc = read(bytes);
    check(doc.is_binary() == (std::string_view(format) == "binary"), "format");
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(result);
    out << std::setprecision(17);
    for (auto name : {"建筑a", "NOTES", "étage", "straße", "STRASSE", "ｓ", "S", "ſ", "未知", " Notes", ""}) {
        const auto* layer = doc.get_layer(name);
        out << "L\t" << name;
        if (layer) {
            out << '\t' << layer->name() << '\t' << layer->handle();
            check(doc.get_record(layer->handle()) == layer, "layer identity index");
        }
        out << '\n';
    }
    check(doc.get_layer("建筑a") && doc.get_layer("étage") && !doc.get_layer("STRASSE"), "Unicode lookup");
    for (auto name : {"continuous", "BYLAYER", "byblock", "中心线a", "文字线", "未知", ""}) {
        const auto* lt = doc.get_linetype(name);
        out << "T\t" << name;
        if (!lt) { out << '\n'; continue; }
        check(doc.get_record(lt->handle()) == lt, "linetype identity index");
        const auto elements = lt->elements();
        out << '\t' << lt->name() << '\t' << lt->handle() << '\t' << lt->description()
            << '\t' << lt->pattern_length() << '\t' << lt->xref_dependent() << '\t' << elements.size() << '\n';
        for (const auto& e : elements) {
            out << "E\t" << e.length;
            if (e.complex) {
                const auto& c = *e.complex;
                out << '\t' << (c.content == LineTypeComplexData::Content::Text ? "text" : "shape")
                    << '\t' << (c.content == LineTypeComplexData::Content::Text ? c.text : std::to_string(c.shape_number))
                    << '\t' << c.style_handle << '\t' << c.scale << '\t' << c.rotation
                    << '\t' << c.offset[0] << '\t' << c.offset[1]
                    << '\t' << c.absolute_rotation << '\t' << c.upright << '\t' << unsigned(c.source_flags);
            }
            out << '\n';
        }
    }
    out.close();
    auto dash = doc.get_linetype("中心线a")->elements();
    check(dash.size() == 4 && dash[2].length == 0 && !dash[2].complex, "dot definition");
    check(doc.get_linetype("文字线")->elements().size() == 2, "complex segments");
    std::ostringstream saved;
    doc.write(saved);
    check(bytes == saved.str(), "query roundtrip");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) boundaries();
        else { check(argc == 4, "expected format input result"); fixture(argv[1], argv[2], argv[3]); }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
