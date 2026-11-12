// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
std::string serialize(const DxfDocument& d,bool binary){std::ostringstream o;d.write(o,binary);return o.str();}
DxfDocument parse(const std::string& b){std::istringstream i(b);return DxfDocument::read(i);}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("data-dir output-prefix manifest");
    std::vector<std::string> names{"chinese-gbk.dxf","chinese-binary.dxf","layers-entities.dxf"};
    for(const auto* kind:{"curves","colors","entity_properties","lwpolyline","polyline2d","polyline3d","polygon_mesh","polyface_mesh","spaces","symbols","header"})
        for(const auto* format:{"ascii","binary"})names.push_back(std::string(kind)+"-"+format+".dxf");
    std::ofstream manifest(argv[3]);
    for(const auto& name:names){
        const auto path=std::string(argv[1])+"/"+name;std::ifstream in(path,std::ios::binary);auto d=DxfDocument::read(in);
        const auto native=serialize(d,d.is_binary());const auto target=serialize(d,!d.is_binary());auto converted=parse(target);
        if(converted.is_binary()==d.is_binary() || converted.version()!=d.version() || converted.insertion_units()!=d.insertion_units())throw std::runtime_error("encoding metadata");
        // Canonical binary pairs compare every record, including unknown data and compound children.
        if(serialize(d,true)!=serialize(converted,true))throw std::runtime_error("transport round trip lost raw field data: "+name);
        if(serialize(d,d.is_binary())!=native)throw std::runtime_error("conversion mutated source");
        const auto output=std::string(argv[2])+"-"+name;std::ofstream out(output,std::ios::binary);out.write(target.data(),target.size());
        manifest<<path<<'\t'<<output<<'\n';
    }
    std::cout<<names.size()<<" fixtures converted, including GBK and all compound types\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
