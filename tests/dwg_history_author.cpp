// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include <libopencad/dwg_create.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <limits>
using namespace libopencad;
static void check(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
static std::string bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static void save(const DwgDocument &d, const std::string &p) {
    std::ofstream o(p, std::ios::binary);
    d.write(o);
}
static SolidHistoryOperation kind(SolidHistoryKind k) {
    SolidHistoryOperation o(k);
    switch (k) {
    case SolidHistoryKind::Box:
    case SolidHistoryKind::Wedge:
        o.parameters = HistoryBox{10, 20, 30};
        break;
    case SolidHistoryKind::Sphere:
        o.parameters = HistorySphere{4};
        break;
    case SolidHistoryKind::Cylinder:
        o.parameters = HistoryCylinder{5, 2, 3, 4};
        break;
    case SolidHistoryKind::Cone:
        o.parameters = HistoryCone{8, 4, 3, 1};
        break;
    case SolidHistoryKind::Pyramid:
        o.parameters = HistoryPyramid{6, 5, 3, 1};
        break;
    case SolidHistoryKind::Torus:
        o.parameters = HistoryTorus{8, 2};
        break;
    case SolidHistoryKind::Boolean:
        o.parameters = HistoryBoolean{2, 1, 2};
        break;
    case SolidHistoryKind::Brep:
        o.parameters = HistoryBrep{acis::build_box({0, 0, 0}, 1, 2, 3).to_sat_string()};
        break;
    case SolidHistoryKind::Fillet:
        o.parameters = HistoryFillet{1, {1, 2}, {.5, .5}, {}, {}};
        break;
    case SolidHistoryKind::Chamfer:
        o.parameters = HistoryChamfer{1, 2, 3, {4, 5}, 6};
        break;
    case SolidHistoryKind::Sweep:
    case SolidHistoryKind::Extrusion: {
        HistorySweep s;
        s.direction = {0, 0, 1};
        s.scale_factor = 1;
        o.parameters = s;
        break;
    }
    case SolidHistoryKind::Loft: {
        HistoryLoft l;
        l.settings = HistoryLoftSettings{};
        l.settings->normals = 3;
        l.settings->section_counts = {1};
        o.parameters = l;
        break;
    }
    case SolidHistoryKind::Revolve: {
        HistoryRevolve revolve;
        revolve.axis_point = {0, 0, 0};
        revolve.direction = {0, 0, 1};
        revolve.revolve_angle = 1.5;
        o.parameters = revolve;
        break;
    }
    }
    return o;
}
static std::uint64_t first_class_count(const DwgDocument &d, std::string_view name) {
    for (auto &c : d.objects().classes)
        if (c.dxf_name == name)
            return std::uint64_t(c.fields[0]);
    return 0;
}
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument("input prefix");
        std::ifstream in(argv[1], std::ios::binary);
        auto source = DwgDocument::read(in);
        auto prefix = std::string(argv[2]);
        auto version = dxf_version_from_string(bytes(source).substr(0, 6));
        std::uint64_t solid = 0, surface = 0;
        for (auto &[h, m] : source.objects().modelers) {
            if (!solid && m.type == 38)
                solid = h;
            if (m.surface && m.surface->type == "PLANESURFACE")
                surface = h;
        }
        check(solid && surface, "fixture 3DSOLID and PLANESURFACE");
        auto old_solid = source.modeler_history_graph(solid);
        auto old_surface = source.modeler_history_graph(surface);
        auto authored = source;
        auto g = authored.create_solid_history(solid, kind(SolidHistoryKind::Box));
        check(g.nodes.size() == 1, "created root");
        for (int i = 1; i < 15; ++i)
            g = authored.append_solid_history(solid, kind(static_cast<SolidHistoryKind>(i)));
        check(g.nodes.size() == 15, "appended all operation kinds");
        check(first_class_count(authored, "ACSH_HISTORY_CLASS") >= 1, "history class count");
        check(first_class_count(authored, "ACAD_EVALUATION_GRAPH") >= 1, "graph class count");
        save(authored, prefix + ".authored.dwg");
        std::ofstream kinds(prefix + ".kinds");
        for (auto h : g.nodes)
            kinds << int(authored.history_record(h).operation->base.kind) << '\n';
        auto loft_node = g.nodes.back();
        for (auto h : g.nodes)
            if (authored.history_record(h).operation->base.kind == SolidHistoryKind::Loft)
                loft_node = h;
        auto loft = authored.history_loft_settings(loft_node);
        check(loft.normals == 3, "created loft metadata");
        auto surface_new = source;
        auto sg = surface_new.create_solid_history(surface, kind(SolidHistoryKind::Box));
        surface_new.append_solid_history(surface, kind(SolidHistoryKind::Cone));
        check(surface_new.modeler_history_graph(surface)->nodes.size() == 2, "surface history");
        save(surface_new, prefix + ".surface.dwg");
        if (old_solid)
            check(!authored.objects().history_records.count(old_solid->root), "replaced solid history");
        if (old_surface)
            check(!surface_new.objects().history_records.count(old_surface->root),
                  "replaced surface history");
        check(authored.modeler_geometry(solid)->records.size() == source.modeler_geometry(solid)->records.size(),
              "solid ACIS unchanged");
        check(surface_new.modeler_geometry(surface)->records.size() ==
                  source.modeler_geometry(surface)->records.size(),
              "surface ACIS unchanged");
        std::istringstream empty(create_dwg(version, {}));
        auto fresh = DwgDocument::read(empty);
        auto h = fresh.add_modeler(ModelerKind::Solid3D, acis::build_box({0, 0, 0}, 1, 2, 3));
        fresh.create_solid_history(h, SolidHistoryOperation{});
        auto created = *fresh.modeler_history_graph(h);
        check(created.nodes.size() == 1, "fresh history");
        auto node = created.nodes.front();
        check(!fresh.history_record(node).extension_dictionary, "box has no loft dictionary");
        auto lofted = fresh;
        auto lg = lofted.create_solid_history(h, kind(SolidHistoryKind::Loft));
        check(lofted.history_loft_settings(lg.nodes.front()).normals == 3, "fresh loft metadata");
        auto settings = lofted.history_loft_settings(lg.nodes.front());
        settings.normals = 5;
        lofted.set_history_loft_settings(lg.nodes.front(), settings);
        check(lofted.history_loft_settings(lg.nodes.front()).normals == 5, "updated loft metadata");
        save(lofted, prefix + ".loft.dwg");
        auto options = fresh;
        options.create_solid_history(h, SolidHistoryOperation(SolidHistoryKind::Loft));
        auto loft_handle = options.modeler_history_graph(h)->nodes.front();
        bool had = false;
        try {
            (void)options.history_loft_settings(loft_handle);
            had = true;
        } catch (const std::invalid_argument &) {
        }
        check(!had, "loft without settings has no metadata");
        HistoryLoftSettings installed;
        installed.normals = 2;
        options.set_history_loft_settings(loft_handle, installed);
        check(options.history_loft_settings(loft_handle).normals == 2, "created missing loft XRECORD");
        save(options, prefix + ".options.dwg");
        fresh.append_solid_history(h, kind(SolidHistoryKind::Cone));
        check(fresh.modeler_history_graph(h)->nodes.size() == 2, "fresh append");
        save(fresh, prefix + ".fresh.dwg");
        auto before = bytes(authored);
        bool threw = false;
        try {
            auto bad = kind(SolidHistoryKind::Cone);
            std::get<HistoryCone>(bad.parameters).height = std::numeric_limits<double>::quiet_NaN();
            authored.append_solid_history(solid, bad);
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw && bytes(authored) == before, "nonfinite rollback");
        threw = false;
        try {
            authored.append_solid_history(solid, kind(SolidHistoryKind::Box));
            authored.append_solid_history(solid, kind(SolidHistoryKind::Sphere));
        } catch (const std::exception &) {
            threw = true;
        }
        check(!threw, "append to latest result");
        save(authored, prefix + ".appended.dwg");
        auto latest = authored;
        latest.set_history_active_node(authored.modeler_history_graph(solid)->root,
                                       authored.history_record(g.nodes.front()).operation->base.node_id());
        before = bytes(latest);
        threw = false;
        try {
            latest.append_solid_history(solid, kind(SolidHistoryKind::Sphere));
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw && bytes(latest) == before, "non-latest append rollback");
        std::istringstream blank(create_dwg(version, {}));
        auto region = DwgDocument::read(blank);
        auto rh = region.add_modeler(ModelerKind::Region, acis::build_box({0, 0, 0}, 1, 1, 1));
        before = bytes(region);
        threw = false;
        try {
            region.create_solid_history(rh, kind(SolidHistoryKind::Box));
        } catch (const std::exception &) {
            threw = true;
        }
        if (unsigned(version) >= 1027)
            check(threw && bytes(region) == before, "R2013+ REGION history rejected");
        else {
            check(!threw, "R2007–R2010 REGION history stored");
            check(region.modeler(rh).history != 0, "REGION history handle");
            save(region, prefix + ".region.dwg");
        }
        std::istringstream r2004(create_dwg(DxfVersion::AC1018, {}));
        auto old = DwgDocument::read(r2004);
        auto oh = old.add_modeler(ModelerKind::Solid3D, acis::build_box({0, 0, 0}, 1, 1, 1));
        before = bytes(old);
        threw = false;
        try {
            old.create_solid_history(oh, kind(SolidHistoryKind::Box));
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw && bytes(old) == before, "R2004 history authoring boundary");
        std::istringstream none(create_dwg(version, {}));
        auto missing_graph = DwgDocument::read(none);
        auto mh = missing_graph.add_modeler(ModelerKind::Solid3D, acis::build_box({0, 0, 0}, 1, 1, 1));
        before = bytes(missing_graph);
        threw = false;
        try {
            missing_graph.append_solid_history(mh, kind(SolidHistoryKind::Box));
        } catch (const std::exception &) {
            threw = true;
        }
        check(threw && bytes(missing_graph) == before, "graphless append rejected");
        std::ofstream manifest(prefix + ".manifest");
        manifest << solid << ' ' << surface << ' ' << h << ' ' << loft_node << '\n';
        std::cout << "DWG history authoring passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
