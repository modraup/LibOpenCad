// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "../src/dxf_binary.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
namespace {
using Pair = std::pair<int,std::string>;
using Record = std::vector<Pair>;
using Records = std::vector<Record>;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string hex(std::uint64_t h) { std::ostringstream out; out << std::hex << std::uppercase << h; return out.str(); }
std::string save(const DxfDocument& doc) { std::ostringstream out; doc.write(out); return out.str(); }
DxfDocument read(const std::string& bytes) { std::istringstream in(bytes); return DxfDocument::read(in); }
Records records(const std::string& bytes, bool binary) {
    std::istringstream in(bytes);
    if (binary) in.ignore(detail::binary_signature.size());
    Records result;
    while (in.peek() != std::char_traits<char>::eof()) {
        int code; std::string value;
        if (binary) {
            code = detail::read_binary_code(in, in.get(), false);
            value = detail::read_binary_value(in, code);
        } else {
            std::string text; std::getline(in, text); code = std::stoi(text);
            std::getline(in, value); if (!value.empty() && value.back() == '\r') value.pop_back();
        }
        if (code == 0) result.emplace_back();
        check(!result.empty(), "record start");
        result.back().emplace_back(code, value);
    }
    return result;
}
std::string encode(const Records& data, bool binary) {
    std::ostringstream out;
    if (binary) out.write(detail::binary_signature.data(), detail::binary_signature.size());
    for (const auto& record : data) for (const auto& [code,value] : record) {
        if (binary) detail::write_binary_pair(out, false, code, value);
        else out << std::setw(3) << code << "\r\n" << value << "\r\n";
    }
    return out.str();
}
std::string identity(const Record& record) {
    if (record.front().second == "SECTION") return {};
    for (const auto& [code,value] : record) {
        if (code == 100) break;
        if (code == 5) return value;
    }
    return {};
}
void inject(Records& data, const std::string& handle, const std::vector<std::string>& refs) {
    if (refs.empty()) return;
    auto it = std::find_if(data.begin(), data.end(), [&](const auto& r) { return identity(r) == handle; });
    check(it != data.end(), "reactor recipient");
    auto at = std::find_if(it->begin(), it->end(), [](const auto& p) { return p.first == 5; }) + 1;
    Record group{{102,"{ACAD_REACTORS"}};
    for (const auto& h : refs) group.emplace_back(330,h);
    group.emplace_back(102,"}");
    it->insert(at, group.begin(), group.end());
}
void drop(Records& data, const std::vector<std::string>& handles) {
    data.erase(std::remove_if(data.begin(), data.end(), [&](const auto& r) {
        return std::find(handles.begin(), handles.end(), identity(r)) != handles.end();
    }), data.end());
}
void write(const DxfDocument& doc, const std::string& path) {
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(path,std::ios::binary); doc.write(out); out.close();
}
template<class Path> std::vector<std::string> handles(const Path& path) {
    std::vector<std::string> list{path.handle()};
    for (std::size_t i = 0; i < path.vertex_count(); ++i) list.push_back(path.vertex(i).handle);
    if constexpr (std::is_same_v<Path,DxfPolyfaceMesh>)
        for (std::size_t i = 0; i < path.face_count(); ++i) list.push_back(path.face(i).handle);
    list.push_back(path.seqend()->handle()); return list;
}
template<class Path> void fixture(const char* input, const std::string& prefix) {
    std::ifstream in(input,std::ios::binary);
    auto doc = DxfDocument::read(in);
    const bool binary = doc.is_binary();
    std::vector<std::vector<std::string>> groups;
    doc.for_each<Path>([&](const auto& p) { groups.push_back(handles(p)); });
    check(groups.size() == 2, "two groups");
    const auto line = hex(doc.add_line({1,2,3},{4,5,6}));
    EntityOptions paper; paper.space = EntitySpace::Paper;
    const auto circle = hex(doc.add_circle({7,8,9},2.5,paper));
    const auto seed = doc.next_handle();
    const auto base = records(save(doc),binary);
    // Cover entity, raw object, table, block record, coordinate, face and SEQEND storage.
    std::vector<std::string> recipients{circle,"C","10","15","2E",groups[1].front(),groups[1].back()};
    recipients.insert(recipients.end(),groups[1].begin()+1,groups[1].end()-1);
    const std::vector<std::string> keep{groups[1].front(),"15",groups[1].front()};
    auto full = keep;
    full.insert(full.begin()+1,line);
    full.insert(full.end(),groups[0].begin(),groups[0].end());
    full.push_back(line); // duplicate deleted identity
    auto expected_before = base;
    for (const auto& recipient : recipients) inject(expected_before,recipient,full);
    // A second group containing only doomed references must disappear entirely.
    inject(expected_before,circle,{line});
    doc = read(encode(expected_before,binary));
    write(doc,prefix+"-before.dxf");
    auto original = doc;
    check(doc.remove_entity(line), "delete standalone entity");
    auto expected_line = base;
    drop(expected_line,{line});
    auto after_line = keep;
    after_line.insert(after_line.end(),groups[0].begin(),groups[0].end());
    for (const auto& recipient : recipients) inject(expected_line,recipient,after_line);
    check(save(doc) == encode(expected_line,binary), "line deletion changed unrelated pairs/references");
    check(doc.next_handle() == seed && doc.paper_space_entity_handles() == std::vector<std::string>{circle}, "line deletion indexes");
    write(doc,prefix+"-line.dxf");
    check(doc.remove_entity(groups[0].front()), "delete compound entity");
    auto expected_group = base;
    drop(expected_group,{line}); drop(expected_group,groups[0]);
    for (const auto& recipient : recipients) inject(expected_group,recipient,keep);
    check(save(doc) == encode(expected_group,binary), "group deletion changed unrelated pairs/references");
    auto reopened = read(save(doc));
    check(reopened.next_handle() == seed && reopened.duplicate_handles().empty(), "seed/identity after reopen");
    for (const auto& h : groups[0]) check(!reopened.get_record(h), "deleted child still indexed");
    for (const auto& h : groups[1]) check(reopened.get_record(h), "remaining child index missing");
    check(reopened.model_space_entity_handles() == std::vector<std::string>{groups[1].front()}, "remaining model membership");
    check(reopened.paper_space_entity_handles() == std::vector<std::string>{circle}, "remaining paper membership");
    write(doc,prefix+"-group.dxf");
    check(save(original) == encode(expected_before,binary), "copy mutated");
}
DxfDocument source(const std::string& ascii, bool binary) {
    auto data = records(ascii,false);
    if (binary) for (auto& r : data) for (auto& [c,v] : r) v = detail::binary_value_from_text(c,v);
    return read(encode(data,binary));
}
void rejects(DxfDocument& doc) {
    const auto before = save(doc); const auto seed = doc.next_handle(); const auto* pointer = doc.get_record("CD");
    bool rejected = false;
    try { doc.remove_entity("AB"); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && save(doc) == before && doc.next_handle() == seed && pointer == doc.get_record("CD"), "cleanup/delete must roll back together");
}
void boundaries() {
    const std::string start = "0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nAB\n330\n15\n";
    const std::string survivor = "0\nCIRCLE\n5\nCD\n330\n15\n";
    const std::string end = "0\nENDSEC\n0\nEOF\n";
    const std::string only = "102\n{ACAD_REACTORS\n330\nAB\n102\n}\n";
    for (bool binary : {false,true}) {
        const std::string other = "102\n{OTHER_APP\n330\nEF\n102\n}\n1\nAB\n1001\nAPP\n1005\nEF\n";
        const std::string mixed = "102\n{ACAD_REACTORS\n330\n0\n330\nDEAD\n330\n00ef\n330\n00aB\n330\nEF\n330\nAB\n102\n}\n";
        const std::string kept = "102\n{ACAD_REACTORS\n330\n0\n330\nDEAD\n330\n00ef\n330\nEF\n102\n}\n";
        const std::string empty = "102\n{ACAD_REACTORS\n102\n}\n";
        auto good = source(start+survivor+only+mixed+empty+other+end,binary);
        check(good.remove_entity("ab"), "mixed reactors removal");
        const auto after = records(save(good),binary);
        auto expected_record = records(survivor+kept+empty+other,false).front();
        if (binary) for (auto& [c,v] : expected_record) v = detail::binary_value_from_text(c,v);
        const auto found = std::find_if(after.begin(),after.end(),[](const auto& r) { return identity(r)=="CD"; });
        check(found != after.end() && *found == expected_record, "unrelated/duplicate/zero/stale references changed");
        const auto once = save(good);
        check(!good.remove_entity("AB") && !good.remove_entity("BAD-HANDLE") && save(good)==once, "no-op cleaned unrelated data");
        // Both early and late non-reactor references prevent any cleanup from committing.
        for (const auto& blocker : {"340\nAB\n", "1001\nAPP\n1005\nAB\n", "102\n{ACAD_XDICTIONARY\n360\nAB\n102\n}\n"}) {
            auto blocked = source(start+survivor+only+"0\nCUSTOM\n5\nEF\n"+blocker+end,binary);
            rejects(blocked);
        }
        for (const auto& unsafe : {
            "102\n{OTHER_APP\n330\nAB\n102\n}\n",
            "102\n{OTHER_APP\n102\n{ACAD_REACTORS\n330\nAB\n102\n}\n102\n}\n",
            "102\n{ACAD_REACTORS\n330\nAB\n",
            "102\n{ACAD_REACTORS\n330\nAB\n1\nunknown\n102\n}\n",
            "102\n{ACAD_REACTORS\n102\n{NESTED\n330\nAB\n102\n}\n102\n}\n",
            "1001\nAPP\n102\n{ACAD_REACTORS\n330\nAB\n102\n}\n",
            "101\nEmbedded Object\n102\n{ACAD_REACTORS\n330\nAB\n102\n}\n",
            "102\n{acad_reactors\n330\nAB\n102\n}\n"}) {
            auto blocked = source(start+survivor+only+unsafe+end,binary); rejects(blocked);
        }
        auto ownership = source(start+survivor+only+"0\nCUSTOM\n5\nEF\n330\nAB\n"+end,binary); rejects(ownership);
        // A valid group is cleaned even after a subclass; malformed unrelated data is retained.
        auto late = source(start+survivor+"100\nAcDbEntity\n"+only+"102\n{OTHER\n330\nEF\n"+end,binary);
        check(late.remove_entity("AB") && save(late).find("{OTHER") != std::string::npos, "scope preservation");
    }
}
}
int main(int argc,char** argv) {
    try {
        if (argc==1) boundaries();
        else {
            check(argc==4,"expected kind input prefix");
            if (std::string_view(argv[1])=="polyline3d") fixture<DxfPolyline3D>(argv[2],argv[3]);
            else fixture<DxfPolyfaceMesh>(argv[2],argv[3]);
        }
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
