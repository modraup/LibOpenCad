// SPDX-License-Identifier: MPL-2.0
#pragma once
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
inline void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
inline std::string hex(std::uint64_t h) { std::ostringstream out; out << std::hex << std::uppercase << h; return out.str(); }
inline std::string save(const DxfDocument& doc) { std::ostringstream out; doc.write(out); return out.str(); }
inline DxfDocument read(const std::string& bytes) { std::istringstream in(bytes); return DxfDocument::read(in); }
inline Records records(const std::string& bytes, bool binary) {
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
inline std::string encode(const Records& data, bool binary) {
    std::ostringstream out;
    if (binary) out.write(detail::binary_signature.data(), detail::binary_signature.size());
    for (const auto& record : data) for (const auto& [code,value] : record) {
        if (binary) detail::write_binary_pair(out, false, code, value);
        else out << std::setw(3) << code << "\r\n" << value << "\r\n";
    }
    return out.str();
}
inline std::string identity(const Record& record) {
    if (record.front().second == "SECTION") return {};
    for (const auto& [code,value] : record) {
        if (code == 100) break;
        if (code == 5) return value;
    }
    return {};
}
inline void inject(Records& data, const std::string& handle, const std::vector<std::string>& refs) {
    if (refs.empty()) return;
    auto it = std::find_if(data.begin(), data.end(), [&](const auto& r) { return identity(r) == handle; });
    check(it != data.end(), "reactor recipient");
    auto at = std::find_if(it->begin(), it->end(), [](const auto& p) { return p.first == 5; }) + 1;
    Record group{{102,"{ACAD_REACTORS"}};
    for (const auto& h : refs) group.emplace_back(330,h);
    group.emplace_back(102,"}");
    it->insert(at, group.begin(), group.end());
}
inline void drop(Records& data, const std::vector<std::string>& handles) {
    data.erase(std::remove_if(data.begin(), data.end(), [&](const auto& r) {
        return std::find(handles.begin(), handles.end(), identity(r)) != handles.end();
    }), data.end());
}
inline void write(const DxfDocument& doc, const std::string& path) {
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(path,std::ios::binary); doc.write(out); out.close();
}
inline Record& at(Records& data, const std::string& h) {
    auto i=std::find_if(data.begin(),data.end(),[&](const auto& r){return identity(r)==h;});
    check(i!=data.end(),"missing fixture record"); return *i;
}
inline void extension(Records& data,const std::string& owner,const std::string& dictionary) {
    auto& r=at(data,owner);
    r.insert(r.begin()+2,{{102,"{ACAD_XDICTIONARY"},{360,dictionary},{102,"}"}});
}
inline void append(Records& data, Record r, bool binary) {
    if(binary) for(auto& [code,value]:r) value=detail::binary_value_from_text(code,value);
    auto section=std::find_if(data.begin(),data.end(),[](const auto& r){return r.front().second=="SECTION" && r.size()>1 && r[1].second=="OBJECTS";});
    check(section!=data.end(),"OBJECTS section");
    auto end=std::find_if(section,data.end(),[](const auto& r){return r.front().second=="ENDSEC";});
    data.insert(end,std::move(r));
}
Record dictionary(std::string h,std::string owner,Record entries={}) {
    Record r{{0,"DICTIONARY"},{5,h},{330,owner},{100,"AcDbDictionary"},{280,"1"},{281,"1"}};
    r.insert(r.end(),entries.begin(),entries.end()); return r;
}
Record xrecord(std::string h,std::string owner) {
    return {{0,"XRECORD"},{5,h},{330,owner},{100,"AcDbXrecord"},{280,"1"},
            {1,"中文扩展数据"},{90,"123456"},{40,"3.125"},{10,"1.25"},{20,"-2.5"},{30,"7"},{310,"1234ABCD"},{340,"15"}};
}
inline Records attach(DxfDocument doc,const std::string& first,const std::string& second) {
    bool binary=doc.is_binary(); auto data=records(save(doc),binary);
    extension(data,first,"80"); extension(data,second,"90");
    append(data,dictionary("80",first,{{3,"中文数据"},{350,"81"},{3,"嵌套"},{360,"82"}}),binary);
    append(data,xrecord("81","80"),binary);
    append(data,dictionary("82","80",{{3,"四维信息"},{350,"83"}}),binary);
    append(data,xrecord("83","82"),binary);
    // An XRECORD can itself have an extension dictionary.
    extension(data,"81","84");
    append(data,dictionary("84","81",{{3,"附加"},{360,"85"}}),binary);
    append(data,xrecord("85","84"),binary);
    append(data,dictionary("90",second,{{3,"保留数据"},{350,"91"},{3,"空字典"},{360,"92"}}),binary);
    append(data,xrecord("91","90"),binary); append(data,dictionary("92","90"),binary);
    return data;
}
}
