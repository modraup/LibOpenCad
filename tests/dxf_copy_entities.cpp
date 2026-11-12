// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
#include <cmath>
namespace {
void property(Record& r, int code, const std::string& value, bool binary) {
    auto sub=std::find(r.begin(),r.end(),Pair{100,"AcDbEntity"});
    check(sub!=r.end(),"entity subclass");
    auto end=std::find_if(sub+1,r.end(),[](const auto& p){return p.first==100;});
    auto old=std::find_if(sub+1,end,[&](const auto& p){return p.first==code;});
    const auto bytes=binary?detail::binary_value_from_text(code,value):value;
    if(old!=end) old->second=bytes; else r.insert(end,{code,bytes});
}
DxfDocument sample(const std::string& kind,bool binary,std::string& source,std::string& neighbor) {
    auto doc=DxfDocument::create(binary);
    if(kind=="lwpolyline" || kind=="arc" || kind=="ellipse") {
        const auto stem=kind=="lwpolyline"?"lwpolyline":"curves";
        std::ifstream input(std::string(TEST_DATA_DIR)+"/"+stem+"-"+(binary?"binary":"ascii")+".dxf",std::ios::binary);
        doc=DxfDocument::read(input);
    }
    doc.add_layer("复制图层",3);
    EntityOptions opts; opts.layer="复制图层"; opts.color=Color::from_rgb(12,34,56);
    opts.line_weight=LineWeight::from_value(35); opts.invisible=true;
    opts.space=kind=="circle"?EntitySpace::Paper:EntitySpace::Model;
    if(kind=="line") source=hex(doc.add_line({1,2,3},{4,5,6},opts));
    else if(kind=="circle") source=hex(doc.add_circle({7,-8,9},2.5,opts));
    else if(kind=="arc") doc.for_each<DxfArc>([&](auto& a){
        source=a.handle();a.set_layer(opts.layer);a.set_color(opts.color);
        a.set_line_weight(opts.line_weight);a.set_invisible(opts.invisible);
        a.set_linetype("Continuous");a.set_normal({0,1,0});a.set_thickness(0.5);
    });
    else if(kind=="ellipse") doc.for_each<DxfEllipse>([&](auto& e){
        source=e.handle();e.set_layer(opts.layer);e.set_color(opts.color);
        e.set_line_weight(opts.line_weight);e.set_invisible(opts.invisible);
        e.set_linetype("Continuous");e.set_normal({0,0,-1});
    });
    else if(kind=="lwpolyline") doc.for_each<DxfLwPolyline>([&](const auto& p){source=p.handle();});
    else source=hex(doc.add_text("中文复制 ^J",{10,20,30},4,opts));
    neighbor=hex(doc.add_circle({20,30,40},7));
    auto data=attach(doc,source,neighbor);
    property(at(data,source),67,kind=="circle"?"1":"0",binary);
    property(at(data,source),410,kind=="circle"?"Layout1":"Model",binary);
    inject(data,source,{source,"81",neighbor,"81"}); inject(data,"80",{source,"85",neighbor});
    inject(data,"83",{"81"});
    auto& x=at(data,"81");
    // Forward, backward, self and external references, plus handle-shaped strings.
    x.insert(x.end(),{{340,"85"},{330,source},{360,"83"},{1005,source},{480,"81"},{390,"82"},
                     {105,"81"},{320,"85"},{1,source}});
    // An incoming ordinary pointer is not redirected by copying its target.
    at(data,"91").push_back({340,source});
    // XDATA points into the tree; its ordinary string stays unchanged.
    at(data,source).insert(at(data,source).end(),{{1001,"ACAD"},{1000,"81"},{1005,"81"}});
    doc=read(encode(data,binary)); doc.allocate_handle(); return doc;
}
void fixture(const std::string& kind,bool binary,const std::string& prefix) {
    std::string source,neighbor; auto doc=sample(kind,binary,source,neighbor);
    const auto original=doc; write(doc,prefix+"-before.dxf");
    auto baseline=records(save(doc),binary);
    std::vector<std::string> old{source,"80","81","82","84","83","85"};
    for(int round=0;round<2;++round) {
        const auto space=((kind=="circle") != (round==1))?EntitySpace::Model:EntitySpace::Paper;
        const bool paper=space==EntitySpace::Paper;
        const auto seed=doc.next_handle();
        const auto copy=doc.copy_entity(old.front(),space); check(copy==seed,"fresh root handle");
        check(doc.next_handle()==seed+7 && doc.duplicate_handles().empty(),"one allocation per copied record");
        std::unordered_map<std::string,std::string> map;
        std::vector<std::string> fresh;
        for(std::size_t i=0;i<old.size();++i) {fresh.push_back(hex(seed+i)); map[old[i]]=fresh.back();}
        auto actual=records(save(doc),binary);
        // Entire original records must survive byte-for-byte, including neighboring dictionary data.
        for(const auto& r:baseline) if(!identity(r).empty()) check(at(actual,identity(r))==r,"source/neighbor record changed");
        for(std::size_t i=0;i<old.size();++i) {
            auto expected=at(baseline,old[i]);
            bool own=true;
            for(auto& [code,value]:expected) {
                if(code==5 && own) {value=fresh[i]; own=false; continue;}
                if(code==330 || code==340 || code==350 || code==360 || code==390 || code==480 || code==1005)
                    if(auto match=map.find(value);match!=map.end()) value=match->second;
            }
            if(i==0) {
                // First top-level 330 after the application groups is the entity owner.
                unsigned depth=0;
                for(auto& [code,value]:expected) {
                    if(code==102) {if(value=="}") --depth; else if(value.front()=='{') ++depth;}
                    if(code==330 && !depth) {value=paper?"18":"15"; break;}
                }
                property(expected,67,paper?"1":"0",binary);
                property(expected,410,paper?"Layout1":"Model",binary);
            }
            check(at(actual,fresh[i])==expected,"copy geometry, payload or remapping mismatch");
            check(doc.ownership_chain(fresh[i]).status==OwnershipStatus::Root,"copied ownership chain");
        }
        auto membership=paper?doc.paper_space_entity_handles():doc.model_space_entity_handles();
        check(std::find(membership.begin(),membership.end(),fresh.front())!=membership.end(),"target space membership");
        write(doc,prefix+(round?"-return.dxf":"-after.dxf"));
        // A copied entity and its extension tree can be removed independently.
        auto removed=doc;
        check(removed.remove_entity(copy),"copy can be independently deleted");
        for(const auto& h:fresh) check(!removed.get_record(h),"copy cascade cleanup");
        for(const auto& h:old) check(removed.get_record(h),"copy deletion affected source");
        doc=read(save(doc)); baseline=actual; old=fresh;
    }
    if(kind=="ellipse") {
        const auto edited_handle=old.front();const auto pi=std::acos(-1.);
        auto verify_ellipse=[&](const DxfEllipse& e,bool edited) {
            check(e.center()==(edited?Point3{1,2,3}:Point3{9,3,2}),"ELLIPSE center");
            check(e.major_axis()==(edited?Point3{6,0,2}:Point3{4,3,0}),"ELLIPSE relative major axis");
            check(e.minor_axis_ratio()==(edited?0.25:0.6),"ELLIPSE axis ratio");
            check(e.normal()==(edited?Point3{0,1,0}:Point3{0,0,-1}),"ELLIPSE normal");
            check(e.start_parameter()==(edited?0.125:0.) &&
                  std::abs(e.end_parameter()-(edited?3.5:2*pi))<1e-12,"ELLIPSE parameters in radians");
            check(e.layer()=="复制图层" && e.linetype()=="Continuous" && e.is_invisible() &&
                  e.color()==Color::from_rgb(12,34,56) && e.line_weight()==LineWeight::from_value(35),"ELLIPSE common data");
        };
        int count=0;
        doc.for_each<DxfEllipse>([&](auto& e){
            ++count;verify_ellipse(e,false);
            if(e.handle()==edited_handle) {
                e.set_center({1,2,3});e.set_major_axis({6,0,2});e.set_minor_axis_ratio(0.25);
                e.set_start_parameter(0.125);e.set_end_parameter(3.5);e.set_normal({0,1,0});
            }
        });
        check(count==3,"typed source and two ELLIPSE copies");
        write(doc,prefix+"-edited.dxf");auto edited=records(save(doc),binary);
        for(const auto& r:baseline)if(!identity(r).empty() && identity(r)!=edited_handle)
            check(at(edited,identity(r))==r,"ELLIPSE edit affected source, neighbor or dictionaries");
        auto expected=at(baseline,edited_handle);
        const std::unordered_map<int,std::string> geometry{{10,"1"},{20,"2"},{30,"3"},{11,"6"},{21,"0"},{31,"2"},
            {40,"0.25"},{41,"0.125"},{42,"3.5"},{210,"0"},{220,"1"},{230,"0"}};
        for(auto& [code,value]:expected)if(auto it=geometry.find(code);it!=geometry.end())
            value=binary?detail::binary_value_from_text(code,it->second):it->second;
        check(at(edited,edited_handle)==expected,"ELLIPSE geometry groups or radian parameters changed incorrectly");
        doc=read(save(doc));count=0;
        doc.for_each<DxfEllipse>([&](const auto& e){++count;verify_ellipse(e,e.handle()==edited_handle);});
        check(count==3,"reopened ELLIPSE copies");
    }
    if(kind=="arc") {
        const auto edited_handle=old.front();const auto pi=std::acos(-1.);
        auto verify_arc=[&](const DxfArc& a,bool edited) {
            check(a.center()==(edited?Point3{4,-2,3}:Point3{2,3,4}) && a.radius()==(edited?7.:5.),"ARC center and radius");
            check(std::abs(a.start_angle_radians()-(edited?1.5*pi:11*pi/6))<1e-12 &&
                  std::abs(a.end_angle_radians()-(edited?pi/4:pi/6))<1e-12,"ARC wraparound angles in radians");
            check(a.normal()==Point3{0,1,0} && a.thickness()==0.5,"ARC extrusion preserved");
            check(a.layer()=="复制图层" && a.linetype()=="Continuous" &&
                  a.color()==Color::from_rgb(12,34,56) && a.line_weight()==LineWeight::from_value(35) &&
                  a.is_invisible(),"ARC common properties");
        };
        int count=0;
        doc.for_each<DxfArc>([&](auto& a){
            ++count;verify_arc(a,false);
            if(a.handle()==edited_handle) {
                a.set_center({4,-2,3});a.set_radius(7);
                a.set_start_angle_radians(1.5*pi);a.set_end_angle_radians(pi/4);
            }
        });
        check(count==3,"typed source and two ARC copies");
        write(doc,prefix+"-edited.dxf");auto edited=records(save(doc),binary);
        for(const auto& r:baseline)if(!identity(r).empty() && identity(r)!=edited_handle)
            check(at(edited,identity(r))==r,"ARC edit affected source, neighbor or object tree");
        // The edited ARC may change only center, radius and its two angles.
        auto expected=at(baseline,edited_handle);
        for(auto& [code,value]:expected) {
            std::string replacement;
            if(code==10)replacement="4";else if(code==20)replacement="-2";else if(code==30)replacement="3";
            else if(code==40)replacement="7";else if(code==50)replacement="270";else if(code==51)replacement="45";
            else continue;
            value=binary?detail::binary_value_from_text(code,replacement):replacement;
        }
        check(at(edited,edited_handle)==expected,"ARC edit changed other fields or DXF degree values");
        doc=read(save(doc));count=0;
        doc.for_each<DxfArc>([&](const auto& a){++count;verify_arc(a,a.handle()==edited_handle);});
        check(count==3,"reopened ARC copies");
    }
    if(kind=="lwpolyline") {
        const auto edited_handle=old.front();
        auto verify_path=[&](const DxfLwPolyline& p,bool edited,bool closed) {
            check(p.is_closed()==closed && p.plinegen(),"closure or PLINEGEN flag");
            check(p.elevation()==2.5 && p.thickness()==0.25 && p.constant_width()==0 &&
                  p.normal().x==0 && p.normal().y==0 && p.normal().z==1,"polyline dimensions/normal");
            const auto v=p.vertices(); check(v.size()==4,"copied vertex count");
            const double x[]={0,edited?10.:8.,8,0}, y[]={0,edited?1.5:0.,6,6};
            const double bulge[]={edited?-0.5:0.5,0,0,edited?0.75:-0.25};
            for(std::size_t i=0;i<v.size();++i)
                check(v[i].x==x[i] && v[i].y==y[i] && v[i].bulge==bulge[i] &&
                      v[i].start_width==i*0.125 && v[i].end_width==i*0.125+0.25 &&
                      v[i].vertex_id==70001+static_cast<int>(i),"ordered vertices, widths, bulge and IDs");
        };
        int count=0;
        doc.for_each<DxfLwPolyline>([&](auto& p){
            ++count; verify_path(p,false,true);
            if(p.handle()==edited_handle) {p.set_vertex_position(1,10,1.5);p.set_bulge(0,-0.5);p.set_bulge(3,0.75);}
        });
        check(count==3,"typed source and two copies");
        auto edited=records(save(doc),binary);
        for(const auto& r:baseline) if(identity(r)!=edited_handle && !identity(r).empty())
            check(at(edited,identity(r))==r,"editing a copy affected source/object tree");
        write(doc,prefix+"-edited.dxf");
        doc=read(save(doc));
        doc.for_each<DxfLwPolyline>([&](const auto& p){verify_path(p,p.handle()==edited_handle,true);});
        const auto closed_bytes=save(doc);
        doc.for_each<DxfLwPolyline>([&](auto& p){if(p.handle()==edited_handle)p.set_closed(false);});
        write(doc,prefix+"-open.dxf"); doc=read(save(doc));
        doc.for_each<DxfLwPolyline>([&](const auto& p){verify_path(p,p.handle()==edited_handle,p.handle()!=edited_handle);});
        doc.for_each<DxfLwPolyline>([&](auto& p){if(p.handle()==edited_handle)p.set_closed(true);});
        // Editing ASCII flags canonicalizes only that numeric value's padding.
        auto restored=records(closed_bytes,binary);
        for(auto& [code,value]:at(restored,edited_handle)) if(code==70)
            value=binary?detail::binary_short(129):"129";
        check(save(doc)==encode(restored,binary),"closure toggle changed other fields");
    }
    check(save(original)==encode(records(save(original),binary),binary),"document copy independence");
}
void boundaries() {
    for(bool binary:{false,true}) {
        std::string source,neighbor; auto original=sample("line",binary,source,neighbor);
        const auto valid=records(save(original),binary);
        int rejection=0;
        auto reject=[&](Records data,std::string h,EntitySpace space=EntitySpace::Paper){
            auto doc=read(encode(data,binary)); const auto before=save(doc); const auto seed=doc.next_handle(); const auto* ptr=doc.get_record(source);
            bool failed=false; try{doc.copy_entity(h,space);}catch(const std::invalid_argument&){failed=true;}catch(const std::overflow_error&){failed=true;}
            ++rejection;
            if (!(failed && save(doc)==before && doc.next_handle()==seed && doc.get_record(source)==ptr))
                throw std::runtime_error("failed copy must be atomic: case "+std::to_string(rejection));
        };
        for(const auto& h:{"0","BAD-HANDLE","FFFF","80","10"}) reject(valid,h);
        reject(valid,source,static_cast<EntitySpace>(99));
        {auto d=valid; at(d,"83")[2].second="90"; reject(d,source);}
        {auto d=valid; drop(d,{"83"}); reject(d,source);}
        {auto d=valid; append(d,xrecord("83","82"),binary); reject(d,source);}
        {auto d=valid; at(d,"83")[0].second="MATERIAL"; auto doc=read(encode(d,binary));auto h=doc.copy_entity(source);check(doc.remove_entity(h)&&doc.get_record("83"),"raw owned type copy isolated");}
        {auto d=valid; at(d,"82").back().second="80"; reject(d,source);}
        {auto d=valid; append(d,xrecord("86","80"),binary); auto doc=read(encode(d,binary));auto h=doc.copy_entity(source);check(doc.remove_entity(h)&&doc.get_record("86"),"owner-chain copy isolated");}
        {auto d=valid; at(d,"90").insert(at(d,"90").end(),{{3,"shared"},{350,"81"}}); reject(d,source);}
        {auto d=valid; at(d,source).insert(at(d,source).begin()+2,{{102,"{PRIVATE"},{330,"81"},{102,"}"}}); reject(d,source);}
        {auto d=valid; at(d,"85").push_back({360,"91"}); reject(d,source);}
        {auto d=valid; drop(d,{"18"}); reject(d,source);}
        {auto d=valid; auto br=at(d,"18"); auto pos=std::find_if(d.begin(),d.end(),[](const auto& r){return identity(r)=="18";}); d.insert(pos,br); reject(d,source);}
        {auto d=valid; at(d,source).insert(at(d,source).begin()+2,{5,source}); reject(d,source);}
        {auto d=valid; at(d,source).insert(at(d,source).begin()+2,{330,"15"}); reject(d,source);}
        {auto d=valid; auto& r=at(d,"85"); auto pos=std::find(r.begin(),r.end(),Pair{100,"AcDbXrecord"}); r.erase(pos); reject(d,source);}
        // Exhaustion after some allocations must not consume any original handles.
        {auto d=valid; for(auto& r:d) if(r.front().second=="SECTION" && r[1].second=="HEADER")
            for(std::size_t i=1;i+1<r.size();++i) if(r[i]==Pair{9,"$HANDSEED"}) r[i+1].second="FFFFFFFFFFFFFFFC";
         reject(d,source);}
        // The pinned Rust DXF reader interprets payload code 5 and reactor-looking
        // strings as metadata. Verify these raw XRECORD cases independently here.
        {auto d=valid; auto& x=at(d,"81"); x.insert(x.end(),{{5,source},{102,"{ACAD_REACTORS"},{330,"85"},{102,"}"}});
         auto doc=read(encode(d,binary)); const auto seed=doc.next_handle(); doc.copy_entity(source);
         auto out=records(save(doc),binary); auto& copied=at(out,hex(seed+2));
         check(std::find(copied.begin(),copied.end(),Pair{5,source})!=copied.end(),"payload code 5 must stay a string");
         check(copied[copied.size()-2]==Pair{330,hex(seed+6)},"payload reference remaps without interpreting application group");}
        // Unknown application payloads without internal references remain byte-for-byte intact.
        {auto d=valid; const Record opaque{{102,"{PRIVATE"},{330,neighbor},{1,"81"},{102,"}"}};
         at(d,source).insert(at(d,source).begin()+2,opaque.begin(),opaque.end());
         auto doc=read(encode(d,binary)); auto fresh=doc.copy_entity(source);
         auto out=records(save(doc),binary); auto& r=at(out,hex(fresh));
         check(std::search(r.begin(),r.end(),opaque.begin(),opaque.end())!=r.end(),"external opaque group preservation");}
        // Raw basic entities in BLOCKS become typed copies in ENTITIES.
        {auto d=valid; auto r=at(d,source); drop(d,{source});
         auto end=std::find_if(d.begin(),d.end(),[](const auto& r){return r.front().second=="ENDBLK";});
         check(end!=d.end(),"source block end"); d.insert(end,r);
         auto doc=read(encode(d,binary)); const auto fresh=doc.copy_entity(source,EntitySpace::Paper);
         int count=0; doc.for_each<DxfLine>([&](const auto& line){if(line.handle()==hex(fresh))++count;});
         check(count==1 && doc.get_record(source),"raw block source typed copy");}
        // LWPOLYLINE sparse/default vertex fields, constant width, custom data and empty paths.
        for(bool empty:{false,true}) {
            std::string lw,adjacent; auto doc=sample("lwpolyline",binary,lw,adjacent);
            auto d=records(save(doc),binary); auto& r=at(d,lw);
            auto start=std::find(r.begin(),r.end(),Pair{100,"AcDbPolyline"});
            auto end=std::find_if(start+1,r.end(),[](const auto& p){return p.first==1001;});
            const auto index=static_cast<std::size_t>(start-r.begin()); r.erase(start,end);
            Record payload{{100,"AcDbPolyline"},{90,empty?"0":"2"},{70,"128"},{43,"1.25"},
                           {38,"-2.5"},{39,"0.5"},{210,"0"},{220,"1"},{230,"0"}};
            if(!empty) payload.insert(payload.end(),{{10,"1"},{20,"2"},{91,"128"},{10,"3"},{20,"4"},{42,"0.25"},
                {102,"{PRIVATE"},{10,"99"},{42,"99"},{102,"}"}});
            if(binary) for(auto& [code,value]:payload) value=detail::binary_value_from_text(code,value);
            r.insert(r.begin()+index,payload.begin(),payload.end());
            doc=read(encode(d,binary)); const auto before=records(save(doc),binary);
            const auto copy=doc.copy_entity(lw); int copies=0;
            doc.for_each<DxfLwPolyline>([&](auto& p){
                check(!p.is_closed() && p.plinegen() && p.constant_width()==1.25 &&
                    p.elevation()==-2.5 && p.thickness()==0.5 && p.normal().y==1,"sparse dimensions");
                auto v=p.vertices(); check(v.size()==(empty?0:2),"sparse/empty count");
                if(p.handle()==hex(copy)) {
                    ++copies;
                    if(!empty) {
                        check(v[0].vertex_id==128 && v[0].bulge==0 && v[1].bulge==0.25 &&
                            v[0].start_width==0 && v[1].end_width==0,"defaults and numeric vertex IDs");
                        p.set_vertex_position(0,5,6);p.set_bulge(0,-0.125);
                    }
                }
            });
            check(copies==1,"typed sparse/empty copy");
            auto after=records(save(doc),binary);
            for(const auto& old:before) if(!identity(old).empty())
                check(at(after,identity(old))==old,"sparse edit altered source/dictionary");
            const auto& copied=at(after,hex(copy));
            check(std::search(copied.begin(),copied.end(),payload.begin(),payload.begin()+9)!=copied.end(),"copied dimension bytes");
        }
        // LWPOLYLINE uses the same ownership and rollback protections as basic entities.
        {std::string lw,adjacent; auto doc=sample("lwpolyline",binary,lw,adjacent);
         auto d=records(save(doc),binary); at(d,"82").back().second="90";
         reject(d,lw);}
        // ARC uses AcDbCircle geometry plus AcDbArc angles, including raw BLOCKS sources.
        {
            std::string arc,adjacent;auto doc=sample("arc",binary,arc,adjacent);
            auto d=records(save(doc),binary);auto r=at(d,arc);
            r.erase(std::remove_if(r.begin(),r.end(),[](const auto& p){
                return p.first==39 || p.first==210 || p.first==220 || p.first==230;
            }),r.end());
            for(auto& [code,value]:r)if(code==50 || code==51)
                value=binary?detail::binary_value_from_text(code,code==50?"-45":"405"):(code==50?"-45":"405");
            drop(d,{arc});
            auto end=std::find_if(d.begin(),d.end(),[](const auto& item){return item.front().second=="ENDBLK";});
            check(end!=d.end(),"ARC source block end");d.insert(end,r);
            doc=read(encode(d,binary));const auto before=records(save(doc),binary);
            const auto fresh=doc.copy_entity(arc,EntitySpace::Paper);int count=0;
            doc.for_each<DxfArc>([&](const auto& a){if(a.handle()==hex(fresh)){
                ++count;const auto pi=std::acos(-1.);
                check(a.owner_handle()=="18" && a.normal()==Point3{0,0,1} && a.thickness()==0,"ARC default extrusion/target ownership");
                check(std::abs(a.start_angle_radians()+pi/4)<1e-12 &&
                      std::abs(a.end_angle_radians()-9*pi/4)<1e-12,"ARC angles must not normalize on copy");
                check(a.center()==Point3{2,3,4} && a.radius()==5,"raw BLOCKS ARC geometry");
            }});
            check(count==1,"raw BLOCKS ARC becomes typed copy");
            auto after=records(save(doc),binary);
            for(const auto& r:before)if(!identity(r).empty())check(at(after,identity(r))==r,"raw ARC source changed");
            // Failure after selecting ARC also preserves content and allocation state.
            d=before;at(d,"83")[2].second="90";doc=read(encode(d,binary));
            const auto bytes=save(doc);const auto seed=doc.next_handle();bool rejected=false;
            try{doc.copy_entity(arc);}catch(const std::invalid_argument&){rejected=true;}
            check(rejected && save(doc)==bytes && doc.next_handle()==seed,"ARC invalid extension rollback");
        }
        // Raw BLOCKS ELLIPSE: default full ellipse and explicit partial ellipse parameters.
        for(bool defaults:{false,true}) {
            std::string ellipse,adjacent;auto doc=sample("ellipse",binary,ellipse,adjacent);
            auto d=records(save(doc),binary);auto e=at(d,ellipse);
            if(defaults)e.erase(std::remove_if(e.begin(),e.end(),[](const auto& p){
                return p.first==41 || p.first==42 || p.first==210 || p.first==220 || p.first==230;
            }),e.end());
            else for(auto& [code,value]:e)if(code==41 || code==42)
                value=binary?detail::binary_value_from_text(code,code==41?"1.25":"5.5"):(code==41?"1.25":"5.5");
            drop(d,{ellipse});
            auto end=std::find_if(d.begin(),d.end(),[](const auto& r){return r.front().second=="ENDBLK";});
            check(end!=d.end(),"ELLIPSE source block end");d.insert(end,e);
            doc=read(encode(d,binary));const auto before=records(save(doc),binary);
            auto h=doc.copy_entity(ellipse,EntitySpace::Paper);int count=0;
            doc.for_each<DxfEllipse>([&](const auto& p){if(p.handle()==hex(h)) {
                ++count;check(p.owner_handle()=="18" && p.center()==Point3{9,3,2} &&
                    p.major_axis()==Point3{4,3,0} && p.minor_axis_ratio()==0.6,"raw ELLIPSE geometry/owner");
                check(p.normal()==(defaults?Point3{0,0,1}:Point3{0,0,-1}),"default/non-default ELLIPSE normal");
                check(p.start_parameter()==(defaults?0.:1.25) &&
                    std::abs(p.end_parameter()-(defaults?2*std::acos(-1.):5.5))<1e-12,"default/partial ELLIPSE parameters");
            }});
            check(count==1,"raw BLOCKS ELLIPSE typed copy");
            auto after=records(save(doc),binary);
            for(const auto& r:before)if(!identity(r).empty())check(at(after,identity(r))==r,"raw ELLIPSE source changed");
            d=before;at(d,"83")[2].second="90";doc=read(encode(d,binary));
            const auto bytes=save(doc);const auto seed=doc.next_handle();bool failed=false;
            try{doc.copy_entity(ellipse);}catch(const std::invalid_argument&){failed=true;}
            check(failed && save(doc)==bytes && doc.next_handle()==seed,"ELLIPSE invalid extension rollback");
        }
        // Plain entities, same-space copies, missing 67, and the default model destination.
        auto plain=DxfDocument::create(binary); auto a=plain.add_text("无字典",{1,2,3},2);
        auto b=plain.copy_entity(a); auto c=plain.copy_entity(b,EntitySpace::Paper);
        check(plain.model_space_entity_handles().size()==2 && plain.paper_space_entity_handles()==std::vector<std::string>{hex(c)},"plain entity copy spaces");
        int count=0; plain.for_each<DxfText>([&](const auto&){++count;}); check(count==3,"typed copy index");
    }
}
}
int main(int argc,char** argv) {
    try {if(argc==1) boundaries(); else {check(argc==4,"kind format prefix"); fixture(argv[1],std::string_view(argv[2])=="binary",argv[3]);}}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
