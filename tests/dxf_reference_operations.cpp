// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <tuple>
template<class F>void fails(DxfDocument& d,F f){auto before=save(d);bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught&&save(d)==before,"atomic rejected operation");}
int main(int argc,char** argv){try{
    check(argc==3,"prefix format");std::string prefix=argv[1];bool binary=std::string(argv[2])=="binary";
    auto names=DxfDocument::create(binary);
    names.add_symbol(SymbolTable::TextStyle,"旧字体");names.add_linetype({"旧线型","",{},0,false});
    names.add_symbol(SymbolTable::View,"旧视图");names.add_symbol(SymbolTable::BlockRecord,"旧块");
    auto name_records=records(save(names),binary);
    for(auto& r:name_records){
        if(r.front().second=="SECTION" && r.size()>1 && r[1]==Pair{2,"HEADER"})r.insert(r.end(),{{9,"$DIMBLK"},{1,"旧块"}});
        std::string header;for(auto& p:r){
        if(p.first==9)header=p.second;
        if(r.front().second=="TABLESTYLE" && p.first==7)p.second="旧字体";
        if(r.front().second=="MLINESTYLE" && p.first==6)p.second="旧线型";
        if(r.front().second=="LAYOUT" && p.first==6)p.second="旧视图";
        if(r.front().second=="SECTION" && header=="$DIMBLK" && p.first==1)p.second="旧块";
    }}
    names=read(encode(name_records,binary));check(names.reference_issues().empty(),"named object references resolve");
    for(const auto& item:std::vector<std::tuple<SymbolTable,std::string,std::string>>{
        {SymbolTable::TextStyle,"旧字体","新字体"},{SymbolTable::LineType,"旧线型","新线型"},
        {SymbolTable::View,"旧视图","新视图"},{SymbolTable::BlockRecord,"旧块","新块"}}){
        auto [table,old_name,new_name]=item;names.rename_symbol(table,old_name,new_name);
        fails(names,[&,table=table,new_name=new_name]{names.remove_symbol(table,new_name);});
    }
    check(names.reference_issues().empty(),"object name references maintained");
    for(bool output:{false,true}){std::ofstream out(prefix+(output?"-names-binary.dxf":"-names-ascii.dxf"),std::ios::binary);names.write(out,output);}
    auto d=DxfDocument::create(binary);
    const auto arc=d.add_arc({2,3,4},5,0.25,4.5);const auto line=d.add_line({1,2,3},{4,5,6});
    const auto root=d.add_extension_dictionary(hex(arc));
    const auto nested=d.add_dictionary(hex(root),"子字典");
    const auto data=d.add_xrecord(hex(root),"中文数据",{{1,"文字 ^J"},{340,hex(arc)},{331,hex(line)},{105,"BEEF"},{320,"CAFE"},{1001,"literal-app"},{310,"00FF1234"},{160,"-9223372036854775808"},{290,"1"}});
    d.add_xrecord(hex(nested),"内部",{{1,"子记录"},{340,hex(data)}});
    const auto extra=d.add_extension_dictionary(hex(data));d.add_xrecord(hex(extra),"附加",{{90,"-12345"},{40,"1.25"}});
    // A non-dictionary object and an unnamed owned object exercise the generic owner graph.
    const auto placeholder=d.allocate_handle(), unnamed=d.allocate_handle();
    auto owned=records(save(d),binary);
    at(owned,hex(root)).insert(at(owned,hex(root)).end(),{{3,"关联占位"},{360,hex(placeholder)}});
    append(owned,{{0,"ACDBPLACEHOLDER"},{5,hex(placeholder)},{330,hex(root)},{100,"AcDbPlaceHolder"}},binary);
    append(owned,xrecord(hex(unnamed),hex(placeholder)),binary);
    d=read(encode(owned,binary));
    check(d.reference_issues().empty(),"new document reference integrity");
    fails(d,[&]{d.add_extension_dictionary(hex(arc));});fails(d,[&]{d.add_dictionary(hex(root),"中文数据");});
    fails(d,[&]{d.add_xrecord(hex(root),"错误",{{340,"DEAD"}});});fails(d,[&]{d.add_xrecord(hex(root),"错误",{{340,"not-hex"}});});
    // The pinned Rust reader treats payload group 5 as identity. Keep a native-only
    // regression for this legal literal value; the shared fixture uses 105/320.
    auto native=d;auto literal=native.add_xrecord(hex(root),"literal-5",{{5,"DEAD"}});
    check(native.get_record(hex(literal))->handle()==hex(literal),"payload 5 is not identity");
    auto native_copy=native.copy_entity(arc);check(native.remove_entity(native_copy),"literal payload copied/deleted safely");
    check(native.get_record(hex(literal))!=nullptr,"source literal survives");
    auto neighbor_dict=native.add_extension_dictionary(hex(line));
    auto neighbor_data=native.add_xrecord(hex(neighbor_dict),"literal-pointer",{{320,hex(arc)},{105,hex(data)}});
    check(native.remove_entity(arc)&&native.get_record(hex(neighbor_data)),"literal payload is not an incoming object reference");
    auto raw=records(save(d),binary);inject(raw,hex(line),{"DEAD",hex(arc)});
    for(auto& r:raw){
        if(identity(r)==hex(line)) for(auto& p:r) if(p.first==330 && p.second=="15")p.second="CAFE";
        if(identity(r)==hex(data)) for(auto& p:r) if(p.first==340)p.second="BEEF";
    }
    auto broken=read(encode(raw,binary));
    const auto issues=broken.reference_issues();check(issues.size()==3,"find three dangling references, ignore literal XRECORD handle-like strings");
    fails(broken,[&]{broken.repair_references({{"DEAD","FFFF"}});});
    fails(broken,[&]{broken.repair_references({{"CAFE","13"}});}); // A layer is not a space owner.
    check(broken.repair_references({{"DEAD",hex(line)},{"CAFE","15"},{"BEEF",hex(arc)}})==3,"repair exact pointer fields");
    check(broken.reference_issues().empty(),"all dangling references repaired");
    fails(broken,[&]{broken.repair_references({{"15",hex(arc)}});});
    auto opaque=records(save(broken),binary);auto& target=at(opaque,hex(line));
    target.insert(target.begin()+2,{{102,"{CUSTOM"},{330,"DEAD"},{102,"}"}});
    auto opaque_doc=read(encode(opaque,binary));fails(opaque_doc,[&]{opaque_doc.repair_references({{"DEAD",hex(line)}});});
    auto malformed=records(save(broken),binary);auto& malformed_record=at(malformed,hex(line));
    malformed_record.insert(malformed_record.end(),{{102,"{ACAD_REACTORS"},{330,"DEAD"}});
    auto malformed_doc=read(encode(malformed,binary));fails(malformed_doc,[&]{malformed_doc.repair_references({{"DEAD",hex(line)}});});
    auto duplicate=records(save(broken),binary);auto line_record=at(duplicate,hex(line));duplicate.insert(std::find_if(duplicate.begin(),duplicate.end(),[&](const auto& r){return identity(r)==hex(line);}),line_record);
    auto duplicate_doc=read(encode(duplicate,binary));check(duplicate_doc.records_with_handle(hex(line)).size()==2,"duplicate fixture is indexed");fails(duplicate_doc,[&]{duplicate_doc.repair_references({{"DEAD",hex(line)}});});
    // Fill absent/null owners from explicit space flags, including VERTEX and SEQEND.
    auto owner_test=broken;const auto path=owner_test.add_polyline3d({{0,0,1},{1,2,3}},false);
    auto missing=records(save(owner_test),binary);
    for(auto& r:missing) {
        const auto id=identity(r);
        if(id==hex(arc)) {for(auto& p:r)if(p.first==330 && p.second=="15")p.second="0";
            auto at=std::find(r.begin(),r.end(),Pair{100,"AcDbEntity"});r.insert(at+1,{67,binary?detail::binary_short(1):"1"});}
        if(id==hex(line))r.erase(std::remove(r.begin(),r.end(),Pair{330,"15"}),r.end());
        if(id==hex(path) || (!id.empty() && std::find(r.begin(),r.end(),Pair{330,hex(path)})!=r.end()))
            for(auto& p:r)if(p.first==330)p.second="0";
    }
    auto repaired_owners=read(encode(missing,binary));check(repaired_owners.reference_issues().size()==6,"missing owners diagnosed");
    check(repaired_owners.repair_entity_owners()==6 && repaired_owners.reference_issues().empty(),"parent and child owners repaired");
    check(repaired_owners.repair_entity_owners()==0,"owner repair idempotence");
    for(bool output:{false,true}){std::ofstream out(prefix+(output?"-owners-binary.dxf":"-owners-ascii.dxf"),std::ios::binary);repaired_owners.write(out,output);}
    auto in_blocks=records(save(repaired_owners),binary);std::vector<std::string> group{hex(path)};
    repaired_owners.for_each<DxfPolyline3D>([&](const auto& p){for(std::size_t i=0;i<p.vertex_count();++i)group.push_back(p.vertex(i).handle);group.push_back(p.seqend()->handle());});
    Records moved;for(const auto& h:group){auto r=at(in_blocks,h);for(auto& p:r)if(p.first==330)p.second="0";moved.push_back(std::move(r));}
    drop(in_blocks,group);in_blocks.insert(std::find_if(in_blocks.begin(),in_blocks.end(),[](const auto& r){return r.front().second=="ENDBLK";}),moved.begin(),moved.end());
    auto block_owners=read(encode(in_blocks,binary));check(block_owners.repair_entity_owners()==4 && block_owners.reference_issues().empty(),"BLOCKS enclosing owner and children repaired");
    for(bool output:{false,true}){std::ofstream out(prefix+(output?"-block-owners-binary.dxf":"-block-owners-ascii.dxf"),std::ios::binary);block_owners.write(out,output);}
    d=std::move(broken);
    for(bool output:{false,true}){std::ofstream out(prefix+(output?"-before-binary.dxf":"-before-ascii.dxf"),std::ios::binary);d.write(out,output);}
    auto copy=d.copy_entity(arc,EntitySpace::Paper);check(d.reference_issues().empty(),"copied object references resolve");
    check(d.remove_entity(arc),"cascade created tree");
    check(!d.get_record(hex(placeholder))&&!d.get_record(hex(unnamed))&&!d.get_record(hex(root))&&!d.get_record(hex(nested))&&!d.get_record(hex(data))&&!d.get_record(hex(extra)),"all owned dictionaries/xrecords deleted");
    check(d.get_record(hex(copy))&&d.get_record(hex(line)),"copy and neighbor survive");
    check(d.reference_issues().empty(),"deletion cleans reactors");
    for(bool output:{false,true}){std::ofstream out(prefix+(output?"-after-binary.dxf":"-after-ascii.dxf"),std::ios::binary);d.write(out,output);}
    std::cout<<"reference diagnostics/repair, constructed dictionaries and cascade verified\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
