// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "../src/dxf_binary.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string save(const DxfDocument& doc) { std::ostringstream out; doc.write(out); return out.str(); }
void write_file(const std::string& path, const std::string& bytes) {
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(path, std::ios::binary); out << bytes; out.close();
}
void structure(const DxfDocument& doc) {
    const std::map<std::string, int> expected{
        {"VPORT",1}, {"LTYPE",3}, {"LAYER",1}, {"STYLE",1}, {"VIEW",0},
        {"UCS",0}, {"APPID",4}, {"DIMSTYLE",1}, {"BLOCK_RECORD",2}};
    std::istringstream input(save(doc));
    if (doc.is_binary()) input.ignore(detail::binary_signature.size());
    std::string type, table;
    int actual = 0, declared = -1;
    bool need_section_name = false;
    std::set<std::string> sections, tables;
    std::set<std::uint64_t> identities;
    while (input.peek() != std::char_traits<char>::eof()) {
        int code;
        std::string value;
        if (doc.is_binary()) {
            code = detail::read_binary_code(input, input.get(), false);
            value = detail::read_binary_value(input, code);
        } else {
            std::string code_text; std::getline(input, code_text); code = std::stoi(code_text);
            std::getline(input, value); if (!value.empty() && value.back() == '\r') value.pop_back();
        }
        if (code == 0) {
            type = value;
            need_section_name = type == "SECTION";
            if (type == "TABLE") { actual = 0; declared = -1; }
            else if (type == "ENDTAB") {
                check(actual == declared && actual == expected.at(table), "default table counts");
                tables.insert(table); table.clear();
            } else if (type == table) ++actual;
        }
        if (need_section_name && code == 2) { sections.insert(value); need_section_name = false; }
        if (type == "TABLE" && code == 2) table = value;
        if (type == "TABLE" && code == 70)
            declared = doc.is_binary() ? detail::binary_short(value) : std::stoi(value);
        if ((code == 5 || code == 105) && type != "SECTION") {
            const auto h = std::stoull(value, nullptr, 16);
            check(identities.insert(h).second && doc.get_record(value), "unique indexed identity");
        }
        // Check every explicit handle reference, including layout and dictionary links.
        if (code >= 330 && code <= 369) {
            const auto h = std::stoull(value, nullptr, 16);
            check(h == 0 || doc.get_record(value), "default reference target");
        }
    }
    check(tables.size() == expected.size(), "all default tables");
    check(sections == std::set<std::string>({"HEADER","CLASSES","TABLES","BLOCKS","ENTITIES","OBJECTS"}),
        "all default sections");
    check(identities.size() == 43, "all identities must exist before Rust can repair anything");
}
void verify(const DxfDocument& doc) {
    check(doc.version() == DxfVersion::AC1032, "default version");
    check(doc.next_handle() == 0x31, "initial allocator");
    check(doc.duplicate_handles().empty(), "default duplicate handles");
    check(doc.model_space_entity_handles().empty() && doc.paper_space_entity_handles().empty(), "empty spaces");
    check(doc.get_layer("0")->handle() == "10", "layer 0");
    check(doc.get_linetype("Continuous")->handle() == "11", "continuous");
    check(doc.get_linetype("ByLayer")->handle() == "12", "ByLayer");
    check(doc.get_linetype("ByBlock")->handle() == "13", "ByBlock");
    check(doc.block_record("15")->is_model_space(), "model block");
    check(doc.block_record("18")->is_paper_space(), "paper block");
    for (auto handle : {"10","11","12","13","14","15","16","17","18","19","1A","1B",
                        "1C","1D","1E","1F","20","21","22","23","24","25","26","27","28",
                        "29","2A","2B","2C","2D","2E","2F","30"}) {
        check(doc.get_record(handle), "default record");
        check(doc.ownership_chain(handle).status == OwnershipStatus::Root, "default owner chain");
    }
    check(doc.get_record("2E")->type() == "LAYOUT" && doc.get_record("2F")->type() == "LAYOUT", "layouts");
    check(doc.get_record("C")->type() == "DICTIONARY", "root dictionary");
    structure(doc);
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 3, "expected format prefix");
        const bool binary = std::string_view(argv[1]) == "binary";
        const std::int16_t units = binary ? 1 : 4;
        auto doc = DxfDocument::create(binary, units);
        check(doc.is_binary() == binary && doc.insertion_units() == units, "format and units");
        verify(doc);
        std::istringstream saved(save(doc));
        auto reopened = DxfDocument::read(saved);
        verify(reopened);
        check(reopened.insertion_units() == units && reopened.is_binary() == binary, "reopen");
        const std::string prefix = argv[2];
        write_file(prefix + ".dxf", save(doc));
        check(doc.add_linetype({"新增虚线", "from new document", {{0.5,{}},{-0.25,{}}}, 0.75}) == 0x31, "new linetype");
        check(doc.add_layer("新增图层", 3, "新增虚线") == 0x32, "new layer");
        check(doc.get_layer("新增图层")->owner_handle() == "2", "new layer owner");
        check(doc.get_linetype("新增虚线")->owner_handle() == "5", "new linetype owner");
        write_file(prefix + "-added.dxf", save(doc));
        auto independent = DxfDocument::create();
        verify(independent);
        check(!independent.get_layer("新增图层") && independent.insertion_units() == 0, "independent defaults");
        check(independent.allocate_handle() == 0x31, "fresh allocator");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
