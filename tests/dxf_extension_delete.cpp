// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
namespace {
void fixture(const std::string& kind,const char* input,const std::string& prefix,bool binary) {
    auto doc=DxfDocument::create(binary);
    std::string first,second;
    std::vector<std::string> removed;
    if(kind=="line") {
        first=hex(doc.add_line({1,2,3},{4,5,6}));
        EntityOptions options; options.space=EntitySpace::Paper;
        second=hex(doc.add_circle({7,8,9},2.5,options)); removed.push_back(first);
    } else {
        std::ifstream in(input,std::ios::binary); doc=DxfDocument::read(in);
        doc.for_each<DxfPolyfaceMesh>([&](const auto& mesh){
            if(first.empty()) {
                first=mesh.handle(); removed.push_back(first);
                for(std::size_t i=0;i<mesh.vertex_count();++i) removed.push_back(mesh.vertex(i).handle);
                for(std::size_t i=0;i<mesh.face_count();++i) removed.push_back(mesh.face(i).handle);
                removed.push_back(mesh.seqend()->handle());
            } else second=mesh.handle();
        });
    }
    auto data=attach(doc,first,second);
    removed.insert(removed.end(),{"80","81","82","83","84","85"});
    // Surviving entity and dictionary reactors pointing into the deleted tree are cleaned.
    inject(data,second,{"81","90","85","90"}); inject(data,"90",{"80",second});
    doc=read(encode(data,binary));
    // Reconcile HANDSEED after adding raw fixture objects before the comparison baseline.
    doc.allocate_handle(); data=records(save(doc),binary);
    const auto copy=doc; const auto seed=doc.next_handle(); write(doc,prefix+"-before.dxf");
    check(doc.remove_entity(first),"cascade removal");
    auto expected=data; drop(expected,removed);
    for(const auto& h:{second,std::string("90")}) {
        auto& r=at(expected,h);
        auto start=std::find(r.begin(),r.end(),Pair{102,"{ACAD_REACTORS"});
        auto end=std::find(start+1,r.end(),Pair{102,"}"});
        r.erase(std::remove_if(start+1,end,[&](const auto& p){return std::find(removed.begin(),removed.end(),p.second)!=removed.end();}),end);
    }
    check(save(doc)==encode(expected,binary),"surviving raw records, payloads or order changed");
    for(const auto& h:removed) check(!doc.get_record(h) && doc.records_with_handle(h).empty(),"stale deleted identity");
    for(const auto& h:{second,std::string("90"),std::string("91"),std::string("92")})
        check(doc.get_record(h) && doc.ownership_chain(h).status==OwnershipStatus::Root,"surviving object ownership");
    check(doc.next_handle()==seed && copy.get_record(first) && copy.get_record("85"),"seed/copy integrity");
    check(!doc.remove_entity(first),"repeat delete"); write(doc,prefix+"-after.dxf");
    auto reopened=read(save(doc)); check(save(reopened)==save(doc),"reopen integrity");
    check(reopened.remove_entity(second) && !reopened.get_record("90") && !reopened.get_record("91") && !reopened.get_record("92"),"second cascade/index rebuild");
}
void boundaries() {
    for(bool binary:{false,true}) {
        auto base=DxfDocument::create(binary);
        auto first=hex(base.add_line({0,0,0},{1,1,1})); auto second=hex(base.add_circle({3,4,5},2));
        const auto valid=attach(base,first,second);
        auto reject=[&](Records data){
            auto doc=read(encode(data,binary)); auto before=save(doc); auto seed=doc.next_handle(); auto ptr=doc.get_record(first);
            bool failed=false; try {doc.remove_entity(first);} catch(const std::invalid_argument&) {failed=true;}
            check(failed && save(doc)==before && doc.next_handle()==seed && doc.get_record(first)==ptr,"invalid cascade must roll back");
        };
        {auto d=valid; at(d,"83")[2].second="90"; reject(d);} // wrong owner / shared object
        {auto d=valid; at(d,"83").insert(at(d,"83").begin()+3,{330,"82"}); reject(d);}
        {auto d=valid; drop(d,{"83"}); reject(d);} // dangling entry
        {auto d=valid; append(d,xrecord("83","82"),binary); reject(d);} // duplicate identity
        {auto d=valid; at(d,"83")[0].second="MATERIAL"; auto doc=read(encode(d,binary));check(doc.remove_entity(first)&&!doc.get_record("83")&&doc.get_record(second),"raw owned type cascades");}
        {auto d=valid; at(d,"82").back().second="80"; reject(d);} // cycle
        {auto d=valid; at(d,"82").back().second="81"; reject(d);} // duplicate/shared child
        {auto d=valid; at(d,"90").insert(at(d,"90").end(),{{3,"外部共享"},{350,"81"}}); reject(d);}
        {auto d=valid; at(d,second).push_back({340,"83"}); inject(d,second,{"80"}); reject(d);} // cleanup rollback
        {auto d=valid; append(d,xrecord("86","80"),binary); auto doc=read(encode(d,binary));check(doc.remove_entity(first)&&!doc.get_record("86")&&doc.get_record(second),"owner-chain child cascades");}
        {auto d=valid; extension(d,first,"90"); reject(d);} // duplicate extension
        {auto d=valid; at(d,"80").pop_back(); reject(d);} // malformed entry
        {auto d=valid; at(d,"80").insert(at(d,"80").end(),{{3,"中文数据"},{350,"83"}}); reject(d);}
        {auto d=valid; at(d,"81").push_back({360,"91"}); reject(d);} // foreign hard ownership payload
        {auto d=valid; at(d,"91").insert(at(d,"91").end(),{{102,"{ACAD_REACTORS"},{330,"81"},{102,"}"}}); reject(d);} // XRECORD payload is not metadata
        {auto d=valid; at(d,"91").push_back({340,"81"}); reject(d);} // external payload reference
        {auto d=valid; at(d,first)[3].second="0"; reject(d);} // null extension
        {auto d=valid; at(d,"80")[2].second="0"; reject(d);} // null owner
        {auto d=valid; at(d,"80").erase(at(d,"80").begin()+2); reject(d);} // missing owner
        {auto d=valid; at(d,first)[4].second="{OTHER"; reject(d);} // malformed extension group
        // An empty extension dictionary is a valid cascade; unrelated literal payload survives.
        {auto d=valid; auto& r=at(d,"91"); r.push_back({1,"80"}); auto doc=read(encode(d,binary));
         check(doc.remove_entity(first) && doc.get_record("91"),"literal payload preservation");}
    }
    // Compound child extension trees are included, not left orphaned.
    for (const auto& format : {"ascii", "binary"}) {
        std::ifstream in(std::string(TEST_DATA_DIR) + "/polyface_mesh-" + format + ".dxf",std::ios::binary);
        auto mesh=DxfDocument::read(in);
        std::string child; mesh.for_each<DxfPolyfaceMesh>([&](const auto& p){if(child.empty()) child=p.vertex(0).handle;});
        auto data=attach(mesh,child,"32"); auto doc=read(encode(data,mesh.is_binary()));
        check(doc.remove_entity("31") && !doc.get_record("85") && doc.get_record("91"),"child extension cascade");
    }
    // Deep ownership trees use an iterative work list rather than the C++ call stack.
    auto base=DxfDocument::create(); const auto entity=hex(base.add_line({0,0,0},{1,1,1}));
    auto data=records(save(base),false); extension(data,entity,"1000");
    for (unsigned i=0;i<1000;++i) {
        Record entries;
        if(i<999) entries={{3,"child"},{350,hex(0x1001+i)}};
        append(data,dictionary(hex(0x1000+i),i?hex(0xFFF+i):entity,entries),false);
    }
    auto deep=read(encode(data,false)); const auto seed=deep.next_handle();
    check(deep.remove_entity(entity) && !deep.get_record("13E7") && deep.next_handle()==seed,"deep cascade and seed");
}
}
int main(int argc,char** argv) {
    try {
        if(argc==1) boundaries();
        else {check(argc==5,"kind format input prefix"); fixture(argv[1],argv[3],argv[4],std::string_view(argv[2])=="binary");}
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
