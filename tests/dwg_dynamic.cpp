// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include <libopencad/dxf_document.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static std::string bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static void save(const DwgDocument &d, const std::string &p) {
    std::ofstream o(p, std::ios::binary);
    d.write(o);
    check(bool(o), "Cannot write DWG");
}
static std::uint64_t block_named(const DwgDocument &d, std::string_view name) {
    std::uint64_t id = 0;
    for (auto &[h, s] : d.objects().symbols)
        if (s.type == 49 && s.name == name) {
            check(!id, "Ambiguous block name");
            id = h;
        }
    check(id, "Missing fixture definition");
    return id;
}
int main(int argc, char **argv) try {
    if (argc < 3 || argc > 4)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto d = DwgDocument::read(in);
    if (!d.objects().dynamic_errors.empty()) {
        std::ostringstream o;
        o << "Dynamic decode errors";
        for (auto &[h, e] : d.objects().dynamic_errors)
            o << " [" << h << "] " << e;
        throw std::runtime_error(o.str());
    }
    if (!d.objects().insert_errors.empty()) {
        std::ostringstream o;
        o << "INSERT decode errors";
        for (auto &[h, e] : d.objects().insert_errors)
            o << " [" << h << "] " << e;
        throw std::runtime_error(o.str());
    }
    check(!d.objects().dynamic_records.empty(), "No dynamic objects");
    check(!d.objects().inserts.empty(), "No INSERT entities");
    auto def = block_named(d, "Dyn主块");
    check(d.objects().symbols.at(def).extension_dictionary, "Missing block extension dictionary");
    check(d.is_dynamic_block(def), "Definition classification");
    auto graph = d.dynamic_block_graph(def);
    check(graph && graph->nodes.size() == 23 && graph->edges.size() == 1, "Graph structure");
    auto props = d.dynamic_block_properties(def);
    check(props.size() == 20, "Default properties count");
    check(std::get<double>(props[0].value) == 13., "Definition 3D distance");
    check(props[0].allowed_values.size() == 3 && std::get<double>(props[0].allowed_values[2]) == 13.,
          "Allowed values");
    auto vis = d.block_visibility_parameter(def);
    check(vis && vis->states.size() == 2 && vis->states[1].visible_params.size() == 2,
          "Visibility membership");
    int failures = 0, nonuniform = 0, reflected = 0, anonymous = 0;
    for (auto &[h, ins] : d.objects().inserts) {
        auto resolved = d.dynamic_definition_for_insert(h);
        check(resolved == def, "Insert definition");
        check(d.dynamic_visibility_for_insert(h)->first == def, "Insert visibility definition");
        try {
            auto p = d.dynamic_block_properties(def, h);
            auto s = ins.scale;
            if (s.x == 2 && s.y == 3) {
                check(p.empty(), "Nonuniform scale");
                ++nonuniform;
            }
            if (s.x == -2) {
                ++reflected;
                check(std::get<double>(p[0].value) == 20., "Mirrored uniform length");
                auto area = std::find_if(p.begin(), p.end(), [](const auto &item) { return item.units == 3; });
                check(area != p.end() && std::get<double>(area->value) == 29., "Area scale");
                check(std::count_if(p.begin(), p.end(), [](const auto &item) { return item.visible; }) == 3,
                      "Saved visibility");
            }
            // Rust DWG stores anonymous BLOCK_HEADER names as the bare prefix
            // (`*U`); the numbered `*U1` name lives on the BLOCK marker.
            auto name = d.objects().symbols.at(ins.block).name;
            if (ins.block != def && name.rfind("*U", 0) == 0) {
                ++anonymous;
                check(std::get<double>(p[0].value) == 10., "Anonymous insertion cache");
            }
        } catch (const std::invalid_argument &) {
            ++failures;
        }
    }
    check(failures == 1 && nonuniform && reflected && anonymous, "Insert variants");
    if (argc == 4) {
        std::ifstream dxf(argv[3]);
        auto source = DxfDocument::read(dxf);
        auto dxf_def = std::stoull(source.get_symbol(SymbolTable::BlockRecord, "Dyn主块")->handle, nullptr, 16);
        auto dxf_props = source.dynamic_block_properties(dxf_def);
        check(dxf_props.size() == props.size(), "DXF/DWG property count");
        for (std::size_t i = 0; i < props.size(); ++i) {
            check(props[i].name == dxf_props[i].name, "DXF/DWG property name");
            check(props[i].type_code == dxf_props[i].type_code, "DXF/DWG property type");
        }
    }
    DwgDynamicRecord linear;
    for (auto &[h, r] : d.objects().dynamic_records)
        if (r.kind == DynamicBlockKind::LinearParameter) {
            linear = r;
            break;
        }
    check(linear.handle, "Missing linear parameter");
    auto original = bytes(d);
    d.update_dynamic_record(linear);
    check(bytes(d) != original, "Identity rewrite changed container");
    std::istringstream identity(bytes(d));
    auto again = DwgDocument::read(identity);
    check(again.dynamic_block_properties(def).size() == 20, "Identity rewrite properties");
    save(again, std::string(argv[2]) + ".identity.dwg");
    auto edited = std::get<BlockLinearParameter>(linear.data);
    edited.distance_name = "NativeLinear";
    linear.data = edited;
    again.update_dynamic_record(linear);
    check(std::get<BlockLinearParameter>(again.dynamic_record(linear.handle).data).distance_name ==
              "NativeLinear",
          "Updated linear name");
    check(again.dynamic_block_properties(def).size() == 20, "Edited properties");
    save(again, std::string(argv[2]) + ".edited.dwg");
    for (int mutation = 0; mutation < 7; ++mutation) {
        auto bad = again.dynamic_record(linear.handle);
        if (mutation == 0)
            bad.kind = DynamicBlockKind::EmptyEntity;
        if (mutation == 1)
            bad.dxf_name = "BLOCKALIGNMENTPARAMETER";
        if (mutation == 2) {
            bad.dxf_name = "BLOCKALIGNMENTPARAMETER";
            bad.kind = DynamicBlockKind::AlignmentParameter;
            bad.data = BlockAlignmentParameter{};
        }
        if (mutation == 3)
            bad.entity = true;
        if (mutation == 4)
            bad.cpp_name = "AcDbEntity";
        if (mutation == 5)
            bad.data = std::monostate{};
        if (mutation == 6)
            bad.kind = DynamicBlockKind::Unknown;
        for (bool insert : {false, true}) {
            auto reject = again;
            bool refused = false;
            try {
                if (insert)
                    reject.insert_dynamic_record(bad);
                else
                    reject.update_dynamic_record(bad);
            } catch (const std::invalid_argument &) {
                refused = true;
            }
            check(refused, "Inconsistent dynamic identity/payload accepted");
            check(bytes(reject) == bytes(again), "Rejected rewrite is atomic");
        }
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
