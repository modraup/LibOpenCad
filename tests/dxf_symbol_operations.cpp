// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
std::string save(const DxfDocument& d){std::ostringstream o;d.write(o);return o.str();}
template<class F>void fail(DxfDocument& d,F f){auto before=save(d);bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught&&save(d)==before,"atomic failure");}
int main(int argc,char** argv){try{
    check(argc==3,"prefix format");auto d=DxfDocument::create(std::string(argv[2])=="binary");
    std::vector<SymbolTable> types{SymbolTable::Layer,SymbolTable::LineType,SymbolTable::TextStyle,SymbolTable::DimStyle,SymbolTable::AppId,SymbolTable::Ucs,SymbolTable::View,SymbolTable::VPort,SymbolTable::BlockRecord};
    for(auto table:types){
        auto id=d.add_symbol(table,"测试");auto symbol=d.get_symbol(table,"测试");check(symbol.has_value(),"new symbol lookup");
        check(d.get_record(symbol->handle)!=nullptr && d.ownership_chain(symbol->handle).status==OwnershipStatus::Root,"symbol index/owner");
        fail(d,[&]{d.add_symbol(table,"测试");});
        check(d.rename_symbol(table,"测试","已改名")==id,"rename retains identity");
        check(!d.get_symbol(table,"测试") && d.get_symbol(table,"已改名"),"rename reindexes names");
        d.add_symbol(table,"临时");check(d.remove_symbol(table,"临时"),"remove unused symbol");check(!d.remove_symbol(table,"临时"),"idempotent deletion");
        fail(d,[&]{d.rename_symbol(table,"已改名","bad|name");});
        fail(d,[&]{d.add_symbol(table,"保留",{{5,"123"}});});
        d.add_symbol(table,"CaseFold");fail(d,[&]{d.add_symbol(table,"CASEFOLD");});
        d.rename_symbol(table,"casefold","casefold");check(d.get_symbol(table,"CaseFold")->name=="casefold","case-only rename");d.remove_symbol(table,"casefold");
    }
    EntityOptions o;o.layer="已改名";o.linetype="已改名";
    d.add_line({1,2,3},{4,5,6},o);
    auto table_owner=d.get_symbol(SymbolTable::Layer,"已改名")->owner_handle;
    auto extension=d.add_extension_dictionary(table_owner);
    auto to_hex=[](std::uint64_t h){std::ostringstream out;out<<std::hex<<std::uppercase<<h;return out.str();};
    auto states=d.add_dictionary(to_hex(extension),"ACAD_LAYERSTATES");
    d.add_xrecord(to_hex(states),"方案",{{302,"已改名"}});
    d.rename_symbol(SymbolTable::Layer,"已改名","几何");d.rename_symbol(SymbolTable::LineType,"已改名","虚线");
    d.for_each<DxfLine>([](const auto& e){check(e.layer()=="几何"&&e.linetype()=="虚线","name reference repair");});
    fail(d,[&]{d.remove_symbol(SymbolTable::Layer,"几何");});fail(d,[&]{d.remove_symbol(SymbolTable::LineType,"虚线");});
    fail(d,[&]{d.remove_symbol(SymbolTable::Layer,"0");});fail(d,[&]{d.remove_symbol(SymbolTable::BlockRecord,"*Model_Space");});
    d.add_symbol(SymbolTable::Ucs,"坐标",{{10,"3"},{20,"4"},{30,"5"}});
    d.add_symbol(SymbolTable::TextStyle,"字体",{{3,"simplex.shx"},{40,"2.5"},{41,"0.8"}});
    fail(d,[&]{d.add_symbol(SymbolTable::DimStyle,"错误",{{340,"DEAD"}});});
    d.set_symbol_properties(SymbolTable::BlockRecord,"已改名",{{70,"4"}});
    d.set_symbol_properties(SymbolTable::TextStyle,"字体",{{40,"3.5"}});
    fail(d,[&]{d.set_symbol_properties(SymbolTable::TextStyle,"字体",{{105,"BEEF"}});});
    fail(d,[&]{d.add_symbol(SymbolTable::AppId,"control\xC2\x85");});
    d.for_each<DxfLine>([](auto& e){e.set_layer("失效名称");});
    check(d.reference_issues().size()==1,"dangling name detected");
    fail(d,[&]{d.repair_symbol_references(SymbolTable::Layer,"失效名称","不存在");});
    check(d.repair_symbol_references(SymbolTable::Layer,"失效名称","几何")==1,"dangling name repaired");
    check(d.reference_issues().empty(),"all symbol references valid");
    check(d.duplicate_handles().empty(),"unique handles");
    for(bool binary:{false,true}){std::ofstream out(std::string(argv[1])+(binary?"-binary.dxf":"-ascii.dxf"),std::ios::binary);d.write(out,binary);}
    std::cout<<"symbol creation/query/rename/removal and ownership passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
