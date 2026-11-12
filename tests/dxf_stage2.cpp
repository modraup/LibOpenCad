// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
namespace {
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
std::string save(const DxfDocument& d, bool binary) { std::ostringstream o; d.write(o,binary); return o.str(); }
DxfDocument read(const std::string& s) { std::istringstream i(s); return DxfDocument::read(i); }
void write(const DxfDocument& d, const std::string& path, bool binary) { std::ofstream o(path,std::ios::binary); d.write(o,binary); }
template<class F> void fails(DxfDocument& d,F f) {
    auto bytes=save(d,d.is_binary()); auto seed=d.next_handle(); bool failed=false;
    try { f(); } catch(const std::exception&) { failed=true; }
    check(failed && save(d,d.is_binary())==bytes && d.next_handle()==seed,"atomic rejection");
}
void check_ownership(const DxfDocument& d) {
    check(d.duplicate_handles().empty(),"unique identities");
    for(const auto& h:d.model_space_entity_handles()) check(d.ownership_chain(h).status==OwnershipStatus::Root,"model ownership");
    for(const auto& h:d.paper_space_entity_handles()) check(d.ownership_chain(h).status==OwnershipStatus::Root,"paper ownership");
    auto compound=[&](const auto& p) {
        for(std::size_t i=0;i<p.vertex_count();++i) {
            check(p.vertex(i).owner_handle==p.handle(),"vertex owner");
            check(d.get_record(p.vertex(i).handle)!=nullptr,"indexed vertex");
        }
        check(p.seqend() && p.seqend()->owner_handle()==p.handle(),"terminator owner");
    };
    d.for_each<DxfPolyline2D>(compound); d.for_each<DxfPolyline3D>(compound);
    d.for_each<DxfPolygonMesh>(compound); d.for_each<DxfPolyfaceMesh>([&](const auto& p){
        compound(p); for(std::size_t i=0;i<p.face_count();++i) check(p.face(i).owner_handle==p.handle(),"face owner");
    });
}
}
int main(int argc,char** argv) { try {
    check(argc==3,"usage: prefix source-format");
    const std::string prefix=argv[1]; bool binary=std::string(argv[2])=="binary";
    auto d=DxfDocument::create(binary,4); d.add_layer("几何");
    std::vector<LwVertex> flat{{0,0,0.25,1,2,7},{8,0,0,2,3,8},{4,6,-0.5,3,1,9}};
    std::vector<Point3> points{{0,0,1},{8,0,2},{4,6,3},{0,6,4}};
    std::vector<std::uint64_t> originals;
    for(auto space:{EntitySpace::Model,EntitySpace::Paper}) {
        EntityOptions o; o.space=space; o.layer="几何";
        originals.push_back(d.add_line({1,2,3},{4,5,6},o));
        originals.push_back(d.add_circle({2,3,4},5,o));
        originals.push_back(d.add_text("中文 ^J",{3,4,5},2.5,o));
        originals.push_back(d.add_arc({2,3,4},5,0.25,4.5,o));
        originals.push_back(d.add_ellipse({3,4,5},{6,0,2},0.5,0.125,3.5,{0,1,0},o));
        originals.push_back(d.add_lwpolyline(flat,true,o));
        originals.push_back(d.add_polyline2d(flat,true,2,o));
        originals.push_back(d.add_polyline3d(points,true,o));
        originals.push_back(d.add_polygon_mesh(2,2,points,true,false,o));
        originals.push_back(d.add_polyface_mesh(points,{{1,-2,3,0},{1,2,-3,4}},o));
    }
    check_ownership(d);
    for(bool target:{false,true}) {
        auto bytes=save(d,target); auto reopened=read(bytes); check_ownership(reopened);
        check(reopened.model_space_entity_handles()==d.model_space_entity_handles(),"conversion model membership");
        check(reopened.paper_space_entity_handles()==d.paper_space_entity_handles(),"conversion paper membership");
        write(d,prefix+(target?"-binary.dxf":"-ascii.dxf"),target);
        auto twice=read(save(reopened,binary)); check_ownership(twice);
        twice.for_each<DxfText>([](const auto& t){check(t.text()=="中文 ^J","literal caret and Chinese survive");});
    }
    // Every supported type participates in the create -> copy -> delete -> save contract.
    for(auto h:originals) { auto copy=d.copy_entity(h,EntitySpace::Paper); check(d.remove_entity(copy),"copy deletion"); }
    check_ownership(d);
    for(bool target:{false,true}) write(d,prefix+(target?"-cycled-binary.dxf":"-cycled-ascii.dxf"),target);
    auto nan=std::numeric_limits<double>::quiet_NaN();
    fails(d,[&]{d.add_arc({},nan,0,1);});
    fails(d,[&]{d.add_ellipse({}, {1,0,0},0.5,0,nan);});
    fails(d,[&]{d.add_polygon_mesh(3,2,points);});
    fails(d,[&]{d.add_polyface_mesh(points,{{1,-2,5,0}});});
    fails(d,[&]{d.add_polyface_mesh(points,{{0,2,3,0}});});
    auto bad=flat; bad[2].bulge=nan;
    fails(d,[&]{d.add_lwpolyline(bad);}); fails(d,[&]{d.add_polyline2d(bad);});
    auto badpoints=points; badpoints[3].z=nan;
    fails(d,[&]{d.add_polyline3d(badpoints);});
    check(d.model_space_entity_handles().size()==10 && d.paper_space_entity_handles().size()==10,"originals unchanged");
    std::cout<<"stage 2 constructors, ownership, copy/delete, transport conversion passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
