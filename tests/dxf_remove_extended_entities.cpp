// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <type_traits>
#include <cmath>
namespace {
template<class Entity>
void fixture(const char* input,const std::string& prefix,bool paper) {
    std::ifstream in(input,std::ios::binary); auto doc=DxfDocument::read(in);
    const bool binary=doc.is_binary();
    std::vector<std::string> paths; std::string circle;
    doc.template for_each<Entity>([&](const auto& p){paths.push_back(p.handle());});
    doc.for_each<DxfCircle>([&](const auto& c){circle=c.handle();});
    check(paths.size()==3 && !circle.empty(),"source and two copies plus neighbor");
    const auto target=paths[paper?1:2];
    check(doc.get_record(target)->owner_handle()==(paper?"18":"15"),"target space");
    auto data=records(save(doc),binary);
    std::vector<std::string> removed;
    for(const auto& r:data) {
        const auto h=identity(r); if(h.empty()) continue;
        const auto chain=doc.ownership_chain(h);
        if(std::any_of(chain.records.begin(),chain.records.end(),[&](const auto* p){return p->handle()==target;}))
            removed.push_back(h);
    }
    check(removed.size()==7,"entity and six owned extension objects");
    // Only these injected reactor recipients are allowed to change outside the deleted tree.
    auto refs=removed; refs.insert(refs.end(),{"15",paths.front(),"15"});
    inject(data,circle,refs); inject(data,"C",refs);
    doc=read(encode(data,binary)); doc.allocate_handle();
    data=records(save(doc),binary); const auto seed=doc.next_handle(); const auto copy=doc;
    write(doc,prefix+"-before.dxf");
    auto model=doc.model_space_entity_handles(),layout=doc.paper_space_entity_handles();
    for(auto* list:{&model,&layout}) list->erase(std::remove(list->begin(),list->end(),target),list->end());
    check(doc.remove_entity(target),"remove specified entity copy");
    auto expected=data; drop(expected,removed);
    for(const auto& h:{circle,std::string("C")}) {
        auto& r=at(expected,h); auto start=std::find(r.begin(),r.end(),Pair{102,"{ACAD_REACTORS"});
        auto end=std::find(start+1,r.end(),Pair{102,"}"});
        r.erase(std::remove_if(start+1,end,[&](const auto& p){return std::find(removed.begin(),removed.end(),p.second)!=removed.end();}),end);
    }
    check(save(doc)==encode(expected,binary),"unrelated geometry, dictionaries, raw records or reference order changed");
    check(doc.model_space_entity_handles()==model && doc.paper_space_entity_handles()==layout,"space membership order");
    for(const auto& h:removed) check(!doc.get_record(h) && doc.records_with_handle(h).empty(),"stale deleted index");
    for(const auto& r:expected) if(!identity(r).empty()) check(doc.get_record(identity(r)),"surviving index");
    int count=0; doc.template for_each<Entity>([&](const auto& p){
        ++count;check(p.handle()!=target,"removed entity still indexed");
        if constexpr(std::is_same_v<Entity,DxfArc>) {
            const bool edited=p.handle()==paths[2];const auto pi=std::acos(-1.);
            check(p.center()==(edited?Point3{4,-2,3}:Point3{2,3,4}) && p.radius()==(edited?7.:5.),"surviving ARC center/radius");
            check(std::abs(p.start_angle_radians()-(edited?1.5*pi:11*pi/6))<1e-12 &&
                  std::abs(p.end_angle_radians()-(edited?pi/4:pi/6))<1e-12,"surviving ARC angles");
            check(p.normal()==Point3{0,1,0} && p.thickness()==0.5,"surviving ARC extrusion");
        } else if constexpr(std::is_same_v<Entity,DxfEllipse>) {
            const bool edited=p.handle()==paths[2];
            check(p.center()==(edited?Point3{1,2,3}:Point3{9,3,2}),"surviving ELLIPSE center");
            check(p.major_axis()==(edited?Point3{6,0,2}:Point3{4,3,0}) &&
                  p.minor_axis_ratio()==(edited?0.25:0.6),"surviving ELLIPSE axes");
            check(p.normal()==(edited?Point3{0,1,0}:Point3{0,0,-1}),"surviving ELLIPSE normal");
            check(p.start_parameter()==(edited?0.125:0.) &&
                  std::abs(p.end_parameter()-(edited?3.5:2*std::acos(-1.)))<1e-12,"surviving full/partial ELLIPSE parameters");
        } else check(p.is_closed() && p.plinegen() && p.vertices().size()==4,"remaining typed geometry");
    });
    check(count==2 && copy.get_record(target) && doc.next_handle()==seed,"copy independence or allocator");
    const auto bytes=save(doc); check(!doc.remove_entity(target) && save(doc)==bytes,"repeat deletion");
    write(doc,prefix+"-after.dxf");
    auto reopened=read(bytes); check(save(reopened)==bytes && reopened.next_handle()==seed,"reopen");
    check(reopened.allocate_handle()==seed,"deleted identity reused");
}
template<class Entity>
void boundaries() {
    for(bool binary:{false,true}) {
        auto base=DxfDocument::create(binary);
        auto root=hex(base.add_line({0,0,0},{1,1,1})), neighbor=hex(base.add_circle({2,3,4},2));
        auto raw=records(save(base),binary); auto& r=at(raw,root);
        r.front().second=std::is_same_v<Entity,DxfArc>?"ARC":(std::is_same_v<Entity,DxfEllipse>?"ELLIPSE":"LWPOLYLINE");
        auto sub=std::find(r.begin(),r.end(),Pair{100,"AcDbLine"}); r.erase(sub,r.end());
        Record shape;
        if constexpr(std::is_same_v<Entity,DxfArc>)
            shape={{100,"AcDbCircle"},{10,"128"},{20,"129"},{30,"130"},{40,"128"},{39,"0.5"},
                   {210,"0"},{220,"1"},{230,"0"},{100,"AcDbArc"},{50,"129"},{51,"405"}};
        else if constexpr(std::is_same_v<Entity,DxfEllipse>)
            shape={{100,"AcDbEllipse"},{10,"128"},{20,"129"},{30,"130"},{11,"128"},{21,"0"},{31,"129"},
                   {40,"0.25"},{41,"0.125"},{42,"3.5"},{210,"0"},{220,"1"},{230,"0"}};
        else shape={{100,"AcDbPolyline"},{90,"2"},{70,"129"},{43,"1.25"},{10,"0"},{20,"0"},{91,"128"},
                    {40,"0.25"},{41,"0.5"},{42,"0.5"},{10,"3"},{20,"4"},{91,"129"},{42,"-0.25"}};
        if(binary) for(auto& [code,value]:shape) value=detail::binary_value_from_text(code,value);
        r.insert(r.end(),shape.begin(),shape.end());
        auto plain=read(encode(raw,binary)); plain.allocate_handle();
        const auto original=records(save(plain),binary);
        auto expected=original; drop(expected,{root});
        check(plain.remove_entity(root) && save(plain)==encode(expected,binary),"standalone path removal");
        for(auto h:{root,std::string("0"),std::string("BAD-HANDLE"),std::string("FFFF")}) {
            const auto before=save(plain); check(!plain.remove_entity(h) && save(plain)==before,"no-op deletion");
        }
        // A raw path in a BLOCKS record uses the same atomic cascade.
        {auto d=original; auto path=at(d,root); drop(d,{root});
         auto end=std::find_if(d.begin(),d.end(),[](const auto& r){return r.front().second=="ENDBLK";}); d.insert(end,path);
         auto doc=read(encode(d,binary)); auto keep=d; drop(keep,{root});
         check(doc.remove_entity(root) && save(doc)==encode(keep,binary),"BLOCKS path deletion");}
        // Empty paths are whole entities too; no VERTEX/SEQEND scan is involved.
        if constexpr(std::is_same_v<Entity,DxfLwPolyline>) {auto d=original; auto& path=at(d,root); auto start=std::find(path.begin(),path.end(),Pair{100,"AcDbPolyline"});
         path.erase(start+1,path.end()); path.push_back({90,binary?detail::binary_value_from_text(90,"0"):"0"});
         auto doc=read(encode(d,binary)); check(doc.remove_entity(root) && doc.get_record(neighbor),"empty path removal");}
        auto valid=attach(read(encode(original,binary)),root,neighbor);
        auto reject=[&](Records data){
            auto doc=read(encode(data,binary)); const auto bytes=save(doc); const auto seed=doc.next_handle(); const auto* ptr=doc.get_record(root);
            bool failed=false; try{doc.remove_entity(root);}catch(const std::invalid_argument&){failed=true;}
            check(failed && save(doc)==bytes && doc.next_handle()==seed && doc.get_record(root)==ptr,"cascade failure must roll back");
        };
        {auto d=valid; at(d,"83")[2].second="90"; reject(d);}
        {auto d=valid; drop(d,{"85"}); reject(d);}
        {auto d=valid; append(d,xrecord("83","82"),binary); reject(d);}
        {auto d=valid; at(d,"83")[0].second="MATERIAL"; auto doc=read(encode(d,binary));check(doc.remove_entity(root)&&!doc.get_record("83"),"raw owned type deletion");}
        {auto d=valid; at(d,"82").back().second="80"; reject(d);}
        {auto d=valid; at(d,"90").insert(at(d,"90").end(),{{3,"共享"},{350,"81"}}); reject(d);}
        {auto d=valid; append(d,xrecord("86","80"),binary); auto doc=read(encode(d,binary));check(doc.remove_entity(root)&&!doc.get_record("86"),"owner-chain deletion");}
        if constexpr(std::is_same_v<Entity,DxfArc> || std::is_same_v<Entity,DxfEllipse>) {
            auto d=valid;auto curve=at(d,root);drop(d,{root});
            auto end=std::find_if(d.begin(),d.end(),[](const auto& r){return r.front().second=="ENDBLK";});
            check(end!=d.end(),"curve block end");d.insert(end,curve);
            auto doc=read(encode(d,binary));doc.allocate_handle();
            auto expected=records(save(doc),binary);drop(expected,{root,"80","81","82","83","84","85"});
            check(doc.remove_entity(root) && save(doc)==encode(expected,binary),"BLOCKS curve extension cascade");
            auto flat=original;auto& r=at(flat,root);
            r.erase(std::remove_if(r.begin(),r.end(),[](const auto& p){return p.first==100;}),r.end());
            auto legacy=read(encode(flat,binary));auto keep=flat;drop(keep,{root});
            check(legacy.remove_entity(root) && save(legacy)==encode(keep,binary),"flat curve removal");
        }
        // Late blockers undo any staged reactor cleanup.
        for(const auto& ref:Record{{340,root},{340,"85"},{1005,root}}) {
            auto d=valid; inject(d,neighbor,{root,"81",neighbor}); at(d,"91").push_back(ref); reject(d);
        }
        {auto d=valid; auto pos=std::find_if(d.begin(),d.end(),[&](const auto& r){return identity(r)==root;});
         auto duplicate=*pos; d.insert(pos,duplicate); reject(d);}
        // Numeric vertex IDs or curve geometry 128/129 coincide with handles 80/81; they are not references.
        {auto d=valid; auto other=at(d,root); other[1].second="99";
         other.erase(std::find(other.begin(),other.end(),Pair{102,"{ACAD_XDICTIONARY"}),
                     std::find(other.begin(),other.end(),Pair{102,"}"})+1);
         auto pos=std::find_if(d.begin(),d.end(),[&](const auto& r){return identity(r)==root;}); d.insert(pos,other);
         auto doc=read(encode(d,binary));check(doc.remove_entity(root) && doc.get_record("99"),"numeric geometry mistaken for handles");
         auto after=records(save(doc),binary);check(at(after,"99")==other,"numeric survivor geometry changed");}
    }
}
}
template<class Entity>
void run(int argc,char** argv) {
    if(argc==2)boundaries<Entity>();
    else {check(argc==5,"kind input prefix space");fixture<Entity>(argv[2],argv[3],std::string_view(argv[4])=="paper");}
}
int main(int argc,char** argv) {
    try {
        check(argc>=2,"expected entity kind");
        if(std::string_view(argv[1])=="arc")run<DxfArc>(argc,argv);
        else if(std::string_view(argv[1])=="ellipse")run<DxfEllipse>(argc,argv);
        else {check(std::string_view(argv[1])=="lwpolyline","unknown kind");run<DxfLwPolyline>(argc,argv);}
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
