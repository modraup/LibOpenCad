// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"

template<class F> void rejected(DxfDocument& d,F f) {
    const auto before=save(d);bool caught=false;
    try {f();} catch(const std::exception&) {caught=true;}
    check(caught && before==save(d),"invalid operation must fail atomically");
}
int main(int argc,char** argv) {try {
    check(argc==4,"case format output");const std::string scenario=argv[1];
    const bool binary=std::string(argv[2])=="binary";auto d=DxfDocument::create(binary);
    if(scenario=="names") {
        d.add_layer("OldLayer");d.add_linetype({"OldType","",{},0,false});
        EntityOptions options;options.layer="OldLayer";options.linetype="OldType";
        const auto line=hex(d.add_line({1,2,3},{4,5,6},options));
        auto data=records(save(d),binary);
        // A flat legacy entity is still graphical; an OBJECTS record is not.
        auto& entity=at(data,line);
        entity.erase(std::remove_if(entity.begin(),entity.end(),[](const auto& p){return p.first==100;}),entity.end());
        append(data,{{0,"CUSTOM"},{5,"800"},{330,"0"},{8,"OldLayer"},{6,"OldType"}},binary);
        const auto original=at(data,"800");d=read(encode(data,binary));
        d.rename_symbol(SymbolTable::Layer,"OldLayer","NewLayer");
        d.rename_symbol(SymbolTable::LineType,"OldType","NewType");
        auto result=records(save(d),binary);check(at(result,"800")==original,"rename changed opaque object strings");
        d.for_each<DxfLine>([](const auto& e){check(e.layer()=="NewLayer" && e.linetype()=="NewType","flat entity names not updated");});
        check(d.repair_symbol_references(SymbolTable::Layer,"OldLayer","NewLayer")==0,"repair changed opaque layer string");
        check(d.repair_symbol_references(SymbolTable::LineType,"OldType","NewType")==0,"repair changed opaque linetype string");
        const auto issues=d.reference_issues();
        check(issues.size()==1 && issues[0].source_handle=="800" && issues[0].status==ReferenceStatus::MissingOwner && !issues[0].table,
              "opaque strings ignored; null OBJECTS owner diagnosed");
    } else if(scenario=="extension") {
        const auto line=hex(d.add_line({1,2,3},{4,5,6}));auto data=records(save(d),binary);
        auto& entity=at(data,line);
        entity.erase(std::remove_if(entity.begin(),entity.end(),[](const auto& p){return p.first==100;}),entity.end());
        entity.insert(entity.end(),{{1001,"ACAD"},{1000,"keep"}});d=read(encode(data,binary));
        const auto dict=hex(d.add_extension_dictionary(line));d.add_xrecord(dict,"Review",{{1,"preserved"}});
        auto result=records(save(d),binary);auto& r=at(result,line);
        const auto marker=std::find(r.begin(),r.end(),Pair{102,"{ACAD_XDICTIONARY"});
        const auto xdata=std::find(r.begin(),r.end(),Pair{1001,"ACAD"});
        check(marker<xdata,"extension dictionary appended inside XDATA");
        rejected(d,[&]{d.add_extension_dictionary(line);});
        auto opaque=data;auto& raw=at(opaque,line);
        raw.erase(raw.end()-2,raw.end());raw.insert(raw.end(),{{101,"Embedded Object"},{1,"keep"}});
        auto with_tail=read(encode(opaque,binary));with_tail.add_extension_dictionary(line);
        auto tail_result=records(save(with_tail),binary);const auto& tail=at(tail_result,line);
        check(std::find(tail.begin(),tail.end(),Pair{102,"{ACAD_XDICTIONARY"})<
            std::find(tail.begin(),tail.end(),Pair{101,"Embedded Object"}),"extension inserted in embedded tail");
        raw.erase(raw.end()-2,raw.end());raw.push_back({102,"{UNCLOSED"});
        auto malformed=read(encode(opaque,binary));rejected(malformed,[&]{malformed.add_extension_dictionary(line);});
        auto removed=d;check(removed.remove_entity(line),"remove flat entity");
        check(!removed.get_record(dict),"flat entity dictionary not cascaded");
    } else if(scenario=="dictionary") {
        const auto line=hex(d.add_line({1,2,3},{4,5,6}));const auto dict=hex(d.add_extension_dictionary(line));
        auto data=records(save(d),binary);auto& r=at(data,dict);
        r.insert(r.end(),{{102,"{PRIVATE"},{3,"Review"},{102,"}"},{101,"Embedded Object"},{3,"opaque"}});
        d=read(encode(data,binary));const auto object=hex(d.add_xrecord(dict,"Review",{{1,"preserved"}}));
        auto result=records(save(d),binary);const auto& final=at(result,dict);
        const auto ref=std::find(final.begin(),final.end(),Pair{360,object});
        check(ref<std::find(final.begin(),final.end(),Pair{101,"Embedded Object"}),"dictionary entry inserted in opaque tail");
        auto unchanged=final;const auto offset=ref-final.begin();
        unchanged.erase(unchanged.begin()+offset-1,unchanged.begin()+offset+1);
        check(unchanged==r,"existing dictionary fields changed");
        rejected(d,[&]{d.add_dictionary(dict,"Review");});
        auto malformed_data=records(save(d),binary);auto& broken=at(malformed_data,dict);
        broken.erase(std::find(broken.begin(),broken.end(),Pair{101,"Embedded Object"}),broken.end());
        broken.push_back({102,"{UNCLOSED"});auto malformed=read(encode(malformed_data,binary));
        rejected(malformed,[&]{malformed.add_dictionary(dict,"Other");});
    } else if(scenario=="block") {
        const auto block=hex(d.add_symbol(SymbolTable::BlockRecord,"ReviewBlock"));
        const auto line=hex(d.add_line({1,2,3},{4,5,6}));auto data=records(save(d),binary);
        auto entity=at(data,line);drop(data,{line});
        entity.erase(std::remove_if(entity.begin(),entity.end(),[](const auto& p){return p.first==330;}),entity.end());
        const auto end=std::find_if(data.begin(),data.end(),[&](const auto& r){return r.front().second=="ENDBLK" && std::find(r.begin(),r.end(),Pair{330,block})!=r.end();});
        check(end!=data.end(),"block boundary");data.insert(end,entity);d=read(encode(data,binary));
        rejected(d,[&]{d.remove_symbol(SymbolTable::BlockRecord,"ReviewBlock");});
        d.repair_entity_owners();check(d.remove_entity(line),"remove block member");
        check(d.remove_symbol(SymbolTable::BlockRecord,"ReviewBlock"),"remove now empty block");
    } else throw std::runtime_error("unknown review case");
    write(d,argv[3]);return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
