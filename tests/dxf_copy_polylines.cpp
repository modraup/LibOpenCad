// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <type_traits>
namespace {
// Physical sequence order, including interleaved faces and unclassified VERTEX.
std::vector<std::string> sequence_handles(const Records& data,const std::string& parent) {
    std::vector<std::string> result;
    auto begin=std::find_if(data.begin(),data.end(),[&](const auto& r){return identity(r)==parent;});
    check(begin!=data.end(),"sequence parent");
    for(auto it=begin;it!=data.end();++it) {
        result.push_back(identity(*it));
        if(it->front().second=="SEQEND")return result;
    }
    throw std::runtime_error("sequence end");
}
void space_fields(Record& r,bool paper,bool binary) {
    auto start=std::find(r.begin(),r.end(),Pair{100,"AcDbEntity"}); check(start!=r.end(),"common subclass");
    for(auto code:{67,410}) {
        auto end=std::find_if(start+1,r.end(),[](const auto& p){return p.first==100;});
        auto found=std::find_if(start+1,end,[&](const auto& p){return p.first==code;});
        std::string value=code==67?(paper?"1":"0"):(paper?"Layout1":"Model");
        if(binary) value=detail::binary_value_from_text(code,value);
        if(found!=end) found->second=value; else r.insert(end,{code,value});
        start=std::find(r.begin(),r.end(),Pair{100,"AcDbEntity"});
    }
}
template<class Path>
DxfDocument fixture_document(const char* input,std::vector<std::string>& source) {
    std::ifstream in(input,std::ios::binary); auto doc=DxfDocument::read(in);
    std::string parent,neighbor;
    doc.template for_each<Path>([&](const auto& p){if(parent.empty())parent=p.handle();else neighbor=p.handle();});
    check(!parent.empty() && !neighbor.empty(),"source and adjacent sequence");
    const auto binary=doc.is_binary();auto data=attach(doc,parent,neighbor);
    source=sequence_handles(data,parent);
    check(source.size()==(std::is_same_v<Path,DxfPolygonMesh>?14:(std::is_same_v<Path,DxfPolyfaceMesh>?10:6)),"fixture sequence size");
    extension(data,source[1],"A0");append(data,dictionary("A0",source[1],{{3,"顶点元数据"},{350,"A1"}}),binary);
    append(data,xrecord("A1","A0"),binary);
    extension(data,source.back(),"B0");append(data,dictionary("B0",source.back(),{{3,"终止元数据"},{360,"B1"}}),binary);
    append(data,xrecord("B1","B0"),binary);
    inject(data,source.front(),{source[1],source.back(),"80",neighbor,source.front()});
    inject(data,source[1],{source[4],"80",neighbor});inject(data,source.back(),{source.front(),source[1]});
    at(data,"81").insert(at(data,"81").end(),{{340,source[1]},{340,source.back()},{1005,source.front()}});
    if constexpr(std::is_same_v<Path,DxfPolyfaceMesh>) {
        const auto first_face=source[6];
        extension(data,first_face,"C0");
        append(data,dictionary("C0",first_face,{{3,"面元数据"},{350,"C1"}}),binary);
        append(data,xrecord("C1","C0"),binary);
        Record unknown{{0,"VERTEX"},{5,"C2"},{330,parent},{100,"AcDbEntity"},{8,"网面"},
                       {100,"AcDbVertex"},{70,"0"},{10,"99"},{20,"-2"},{30,"7"},{999,"unclassified vertex"}};
        if(binary)for(auto& [code,value]:unknown)value=detail::binary_value_from_text(code,value);
        auto face=at(data,first_face);drop(data,{first_face});
        auto position=std::find_if(data.begin(),data.end(),[&](const auto& r){return identity(r)==source[2];});
        data.insert(position,{face,unknown});
        extension(data,"C2","D0");
        append(data,dictionary("D0","C2",{{3,"保留子记录"},{350,"D1"}}),binary);
        append(data,xrecord("D1","D0"),binary);
        at(data,"81").insert(at(data,"81").end(),{{340,first_face},{340,"C2"}});
        at(data,"C1").insert(at(data,"C1").end(),{{340,source[1]},{340,first_face},{340,"C2"}});
        source=sequence_handles(data,parent);
        check(source.size()==11 && source[2]==first_face && source[3]=="C2","interleaved raw sequence");
    }
    for(const auto& h:source) space_fields(at(data,h),false,binary);
    doc=read(encode(data,binary));doc.allocate_handle();return doc;
}
template<class Path>
void fixture(const char* input,const std::string& prefix) {
    std::vector<std::string> source;auto doc=fixture_document<Path>(input,source);const bool binary=doc.is_binary();
    write(doc,prefix+"-before.dxf");
    std::vector<std::string> old=source;
    // Independent expected allocation order: sequence, then breadth-first extension objects.
    if constexpr(std::is_same_v<Path,DxfPolyfaceMesh>)
        old.insert(old.end(),{"80","A0","C0","D0","B0","81","82","A1","C1","D1","B1","84","83","85"});
    else old.insert(old.end(),{"80","A0","B0","81","82","A1","B1","84","83","85"});
    auto baseline=records(save(doc),binary);
    for(bool paper:{true,false}) {
        const auto seed=doc.next_handle();const auto fresh=doc.copy_entity(old.front(),paper?EntitySpace::Paper:EntitySpace::Model);
        check(fresh==seed && doc.next_handle()==seed+old.size() && doc.duplicate_handles().empty(),"fresh unique group handles");
        std::unordered_map<std::string,std::string> map;std::vector<std::string> copied;
        for(std::size_t i=0;i<old.size();++i){copied.push_back(hex(seed+i));map[old[i]]=copied.back();}
        auto actual=records(save(doc),binary);
        check(sequence_handles(actual,copied.front())==std::vector<std::string>(copied.begin(),copied.begin()+source.size()),"physical child order");
        for(const auto& r:baseline) if(!identity(r).empty()) check(at(actual,identity(r))==r,"source or adjacent record changed");
        for(std::size_t i=0;i<old.size();++i) {
            auto expected=at(baseline,old[i]);bool own=true;unsigned depth=0;
            for(auto& [code,value]:expected) {
                if(code==5 && own){value=copied[i];own=false;continue;}
                if(code==102){if(value=="}")--depth;else if(!value.empty()&&value.front()=='{')++depth;}
                if(i<source.size() && code==330 && !depth) value=i?copied.front():(paper?"18":"15");
                else if(code==330 || code==340 || code==350 || code==360 || code==1005)
                    if(auto m=map.find(value);m!=map.end())value=m->second;
            }
            if(i<source.size()) space_fields(expected,paper,binary);
            check(at(actual,copied[i])==expected,"record remap/geometry mismatch");
            check(doc.ownership_chain(copied[i]).status==OwnershipStatus::Root,"copied ownership chain");
        }
        const auto space=paper?doc.paper_space_entity_handles():doc.model_space_entity_handles();
        check(std::find(space.begin(),space.end(),copied.front())!=space.end(),"parent target space");
        for(std::size_t i=1;i<copied.size();++i)check(std::find(space.begin(),space.end(),copied[i])==space.end(),"child incorrectly exposed as space entity");
        write(doc,prefix+(paper?"-after.dxf":"-return.dxf"));
        auto removed=doc;check(removed.remove_entity(fresh),"independent copy deletion");
        for(const auto& h:copied)check(!removed.get_record(h),"copied subtree not removed");
        auto kept=records(save(removed),binary);for(const auto& r:baseline)if(!identity(r).empty())check(at(kept,identity(r))==r,"delete copy affected source");
        doc=read(save(doc));baseline=actual;old=copied;
    }
    const auto target=old.front();int count=0;
    doc.template for_each<Path>([&](auto& p){
        ++count;if(p.handle()!=target)return;
        if constexpr(std::is_same_v<Path,DxfPolyfaceMesh>) {
            check(p.vertex_count()==5 && p.face_count()==3,"polyface classified counts");
            const Point3 points[]={{0,0,0},{4,0,0},{4,4,0},{0,4,0},{2,2,3}};
            const std::size_t coordinates[]={1,4,5,6,7},faces[]={2,8,9};
            const std::array<int,4> indices[]={{1,-2,5,0},{-1,2,3,4},{3,4,-5,5}};
            for(std::size_t i=0;i<5;++i) {
                const auto v=p.vertex(i);
                check(v.position==points[i] && v.flags==192,"polyface coordinate order");
                check(v.handle==old[coordinates[i]] && v.owner_handle==target,"coordinate ownership");
            }
            for(std::size_t i=0;i<3;++i) {
                const auto f=p.face(i);
                check(f.indices==indices[i] && f.is_triangle()==(i!=1),"signed local indices and triangle encodings");
                check(f.handle==old[faces[i]] && f.owner_handle==target,"face ownership");
            }
            p.set_face_indices(0,{-1,3,-5,0});p.set_face_indices(1,{2,-3,4,-1});
        } else if constexpr(std::is_same_v<Path,DxfPolygonMesh>) {
            check(p.m_vertex_count()==3 && p.n_vertex_count()==4 && p.vertex_count()==12,"mesh dimensions");
            check(!p.is_closed_m() && p.is_closed_n(),"mesh closure");
            for(std::size_t m=0;m<3;++m)for(std::size_t n=0;n<4;++n) {
                const auto i=p.grid_index(m,n);const auto v=p.vertex(i);
                check(i==m*4+n && v.position==Point3{double(m),double(n),10.*m-double(n)},"mesh ordered XYZ");
                check(v.flags==64 && v.layer==(i==6?"测点":"网格"),"mesh vertex flags and layer");
                check(v.handle==old[i+1] && v.owner_handle==target,"mesh vertex identity and ownership");
            }
            p.set_vertex_position(p.grid_index(1,2),{1.5,2.25,-7.});
        } else {
        check(p.is_closed() && p.vertex_count()==4,"source geometry metadata");
        if constexpr(std::is_same_v<Path,DxfPolyline3D>) {
            const Point3 positions[]={{0,0,-2},{8,0,3},{8,6,7.5},{0,6,1}};
            for(std::size_t i=0;i<4;++i) {
                const auto v=p.vertex(i);
                check(v.position==positions[i] && v.flags==32 && v.layer==(i==1?"测点":"路径"),"ordered XYZ, flags and layers");
                check(v.handle==old[i+1] && v.owner_handle==target,"vertex identity and ownership");
            }
            p.set_vertex_position(1,{10,1.5,-6.25});
        } else {
            check(p.default_start_width()==0.25 && p.default_end_width()==0.5,"source default widths");
            p.set_vertex_position(1,10,1.5);p.set_vertex_widths(0,0,1.25);p.set_vertex_widths(2,2.5,3.5);p.set_bulge(3,0.75);
        }
        }
    });check(count==4,"source, neighbor and two copied paths");
    write(doc,prefix+"-edited.dxf");auto edited=records(save(doc),binary);
    std::set<std::string> changed;
    if constexpr(std::is_same_v<Path,DxfPolyfaceMesh>)changed={old[2],old[8]};
    else if constexpr(std::is_same_v<Path,DxfPolygonMesh>)changed={old[7]};
    else if constexpr(std::is_same_v<Path,DxfPolyline3D>)changed={old[2]};
    else changed={old[1],old[2],old[3],old[4]};
    for(const auto& r:baseline)if(!identity(r).empty() && !changed.count(identity(r)))
        check(at(edited,identity(r))==r,"editing children affected other records");
    auto reopened=read(save(doc));
    reopened.template for_each<Path>([&](const auto& p){if(p.handle()==target){
        if constexpr(std::is_same_v<Path,DxfPolyfaceMesh>) {
            check(p.face(0).indices==std::array<int,4>{-1,3,-5,0} && p.face(0).is_triangle(),"edited triangle hidden edges");
            check(p.face(1).indices==std::array<int,4>{2,-3,4,-1} && !p.face(1).is_triangle(),"edited quad hidden edges");
            check(p.face(2).indices==std::array<int,4>{3,4,-5,5} && p.face(2).is_triangle(),"repeated-last triangle unchanged");
        } else if constexpr(std::is_same_v<Path,DxfPolygonMesh>) {
            check(p.vertex(p.grid_index(1,2)).position==Point3{1.5,2.25,-7.},"edited mesh XYZ");
        } else if constexpr(std::is_same_v<Path,DxfPolyline3D>) {
            check(p.vertex(1).position==Point3{10,1.5,-6.25},"edited XYZ");
        } else {
        check(p.vertex(0).start_width==0 && p.vertex(0).end_width==1.25,"explicit zero widths");
        check(p.vertex(1).location.x==10 && p.vertex(1).location.y==1.5,"edited vertex");
        check(p.vertex(2).start_width==2.5 && p.vertex(2).end_width==3.5 && p.vertex(3).bulge==0.75,"edited widths/closing arc");
        }
    }});
}
template<class Path>
void boundaries(const char* input) {
    std::vector<std::string> ids;auto base=fixture_document<Path>(input,ids);const bool binary=base.is_binary();const auto valid=records(save(base),binary);
    auto reject=[&](Records data,std::string handle){auto doc=read(encode(data,binary));const auto bytes=save(doc);const auto seed=doc.next_handle();const auto* ptr=doc.get_record(ids[0]);
        bool failed=false;try{doc.copy_entity(handle,EntitySpace::Paper);}catch(const std::invalid_argument&){failed=true;}catch(const std::overflow_error&){failed=true;}
        check(failed && save(doc)==bytes && doc.next_handle()==seed && doc.get_record(ids[0])==ptr,"invalid sequence copy must roll back");};
    for(std::size_t i=1;i<ids.size();++i)reject(valid,ids[i]);
    {auto d=valid;drop(d,{ids.back()});reject(d,ids[0]);}
    {auto d=valid;at(d,ids[2])[1].second=ids[1];reject(d,ids[0]);}
    {auto d=valid;at(d,ids[2])[1].second="0";reject(d,ids[0]);}
    {auto d=valid;auto& r=at(d,ids[2]);r.erase(r.begin()+1);reject(d,ids[0]);}
    {auto d=valid;auto& r=at(d,ids[2]);for(auto& [code,value]:r)if(code==330)value="32";reject(d,ids[0]);}
    {auto d=valid;at(d,"A1")[2].second="90";reject(d,ids[0]);}
    {auto d=valid;at(d,"90").insert(at(d,"90").end(),{{3,"shared"},{350,"A1"}});reject(d,ids[0]);}
    {auto d=valid;auto& r=at(d,ids[2]);r.insert(r.begin()+2,{{102,"{PRIVATE"},{330,ids[1]},{102,"}"}});reject(d,ids[0]);}
    {auto d=valid;for(auto& r:d)for(std::size_t i=0;i+1<r.size();++i)if(r[i]==Pair{9,"$HANDSEED"})r[i+1].second="FFFFFFFFFFFFFFF8";reject(d,ids[0]);}
    if constexpr(std::is_same_v<Path,DxfPolyfaceMesh>) {
        for(const auto* object:{"C1","D1"}) {
            auto d=valid;at(d,object)[2].second="90";reject(d,ids[0]);
        }
        {auto d=valid;drop(d,{"D1"});reject(d,ids[0]);}
        {auto d=valid;at(d,ids[3])[1].second="0";reject(d,ids[0]);}
        {auto d=valid;for(auto& [code,value]:at(d,ids[3]))if(code==330)value="90";reject(d,ids[0]);}
    }
    // SPLINE remains outside this phase; BLOCKS sequences share the typed copy path.
    {auto d=valid;at(d,ids[0])[0].second="SPLINE";reject(d,ids[0]);}
    {auto d=valid;Records moved;for(const auto& id:ids)moved.push_back(at(d,id));drop(d,ids);
     auto end=std::find_if(d.begin(),d.end(),[](const auto& r){return r.front().second=="ENDBLK";});
     d.insert(end,moved.begin(),moved.end());
     auto doc=read(encode(d,binary));auto h=doc.copy_entity(ids[0],EntitySpace::Paper);
     auto after=records(save(doc),binary);check(sequence_handles(after,hex(h)).size()==ids.size(),"BLOCKS sequence copied in full");
     for(const auto& id:ids)check(at(after,id)==at(d,id),"BLOCKS source preserved");
     check(doc.remove_entity(h),"BLOCKS-derived copy independently removable");}
    // Missing/null child owners are populated in the copy; originals remain unchanged.
    {auto d=valid;auto& v=at(d,ids[2]);v.erase(std::find_if(v.begin(),v.end(),[](const auto& p){return p.first==330;}));
     auto& w=at(d,ids[3]);for(auto& [code,value]:w)if(code==330)value="0";
     auto doc=read(encode(d,binary));const auto before=records(save(doc),binary);
     auto h=doc.copy_entity(ids[0]);auto after=records(save(doc),binary);
     const auto children=sequence_handles(after,hex(h));
     for(std::size_t i=1;i<children.size();++i)check(doc.get_record(children[i])->owner_handle()==hex(h),"normalized child owner");
     for(const auto& r:before)if(!identity(r).empty())check(at(after,identity(r))==r,"normalizing copied owners affected source");}
    // A complete empty sequence still receives a fresh SEQEND handle.
    {auto d=valid;std::vector<std::string> removed(ids.begin()+1,ids.end()-1);removed.insert(removed.end(),{"A0","A1"});
     if constexpr(std::is_same_v<Path,DxfPolyfaceMesh>)removed.insert(removed.end(),{"C0","C1","D0","D1"});
     drop(d,removed);
     if constexpr(std::is_same_v<Path,DxfPolygonMesh> || std::is_same_v<Path,DxfPolyfaceMesh>)
         for(auto& [code,value]:at(d,ids[0]))if(code==71 || code==72)value=binary?detail::binary_short(0):"0";
     auto& x=at(d,"81");x.erase(std::remove_if(x.begin(),x.end(),[&](const auto& p){return p.first==340 && std::find(removed.begin(),removed.end(),p.second)!=removed.end();}),x.end());
     auto doc=read(encode(d,binary));auto h=doc.copy_entity(ids[0]);int count=0;doc.template for_each<Path>([&](const auto& p){if(p.handle()==hex(h)){++count;check(!p.vertex_count() && p.seqend() && p.seqend()->owner_handle()==hex(h),"empty sequence copy");}});check(count==1,"empty typed copy");}
    if constexpr(std::is_same_v<Path,DxfPolygonMesh>) {
        for(int closure:{0,1,32,33}) {
            auto d=valid;
            for(auto& [code,value]:at(d,ids[0]))if(code==70 || code==71 || code==72) {
                const auto number=code==70?16|closure:(code==71?4:3);
                value=binary?detail::binary_short(number):std::to_string(number);
            }
            auto doc=read(encode(d,binary));const auto before=records(save(doc),binary);
            auto h=doc.copy_entity(ids[0],EntitySpace::Paper);int count=0;
            auto reopened=read(save(doc));
            reopened.template for_each<Path>([&](const auto& p){if(p.handle()==hex(h)){
                ++count;check(p.m_vertex_count()==4 && p.n_vertex_count()==3 && p.vertex_count()==12,"alternate mesh dimensions");
                check(p.is_closed_m()==bool(closure&1) && p.is_closed_n()==bool(closure&32),"independent closure bits");
                for(std::size_t i=0;i<12;++i)check(p.grid_index(i/3,i%3)==i && p.vertex(i).owner_handle==hex(h),"alternate grid ordering/owner");
            }});
            check(count==1,"alternate mesh copy");
            auto after=records(save(doc),binary);
            for(const auto& r:before)if(!identity(r).empty())check(at(after,identity(r))==r,"alternate mesh source unchanged");
        }
    }

}
}
template<class Path>
void run(const char* input,const char* output) {
    if(std::string_view(output)=="--boundaries")boundaries<Path>(input);
    else fixture<Path>(input,output);
}
int main(int argc,char** argv) {
    try {
        check(argc==4,"kind input prefix (or --boundaries)");
        if(std::string_view(argv[1])=="polyline2d")run<DxfPolyline2D>(argv[2],argv[3]);
        else if(std::string_view(argv[1])=="polyline3d")run<DxfPolyline3D>(argv[2],argv[3]);
        else if(std::string_view(argv[1])=="polygon_mesh")run<DxfPolygonMesh>(argv[2],argv[3]);
        else {check(std::string_view(argv[1])=="polyface_mesh","unknown path kind");run<DxfPolyfaceMesh>(argv[2],argv[3]);}
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
