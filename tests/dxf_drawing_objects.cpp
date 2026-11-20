// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        std::string prefix = argv[3];
        d.for_each<DxfDrawingObject>([](auto &o) { o.set_definition(o.definition()); });
        write(d, prefix + "-roundtrip.dxf");
        auto root = d.root_dictionary_handle(), style = d.dictionary_entry(root, "视觉"),
             geo = d.dictionary_entry(root, "坐标2");
        d.for_each<DxfDrawingObject>([&](auto &o) {
            auto v = o.definition();
            if (o.handle() == style) {
                auto &p = std::get<VisualStyleDefinition>(v).properties[4];
                p.value = .9;
                p.enabled = 2;
            } else if (o.handle() == geo) {
                auto &g = std::get<GeoDataDefinition>(v);
                check(g.coordinate_system.find('\n') != std::string::npos, "geodata text decoding");
                g.points[0].destination.x = 99;
                g.faces[0][2] = 1;
            } else
                return;
            o.set_definition(v);
        });
        write(d, prefix + "-edited.dxf");
        auto fresh = DxfDocument::create(binary);
        fresh.add_drawing_object(fresh.root_dictionary_handle(), "新视觉", VisualStyleDefinition::defaults());
        GeoDataDefinition g;
        g.coordinate_system = "中文坐标\n多行^J";
        g.points = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
        g.faces = {{0, 1, 0}};
        fresh.add_drawing_object(fresh.root_dictionary_handle(), "新坐标", g);
        std::vector<LwVertex> vertices{{0, 0, .5, 1, 1, 0}, {10, 0, 0, 1, 1, 0}, {10, 10, 0, 1, 1, 0}};
        fresh.add_lwpolyline(vertices);
        fresh.add_polyline2d(vertices);
        fresh.add_polyline3d({{1, 2, 3}, {4, 5, 6}});
        fresh.add_polygon_mesh(2, 2, {{0, 0, 0}, {1, 0, 1}, {0, 1, 2}, {1, 1, 3}});
        fresh.add_polyface_mesh({{0, 0, 0}, {1, 0, 1}, {1, 1, 2}}, {{1, -2, 3, 0}});
        fresh.for_each<DxfLwPolyline>([](auto &p) {
            auto v = p.vertices();
            v.push_back({0, 10, 0, 0, 0, 0});
            p.set_vertices(v);
            p.set_vertex_widths(0, 2, 3);
            p.set_vertex_id(0, 7);
            p.set_closed(true);
            p.set_plinegen(true);
            p.set_thickness(4);
            p.set_constant_width(5);
            p.set_normal({0, 1, 0});
        });
        fresh.for_each<DxfPolyline2D>([](auto &p) {
            p.set_closed(true);
            p.set_elevation(6);
            p.set_thickness(7);
            p.set_normal({0, 1, 0});
            p.set_default_widths(2, 3);
            p.set_vertex_metadata(0, 2, .25, 8);
            check(p.vertex(0).id == 8 && p.vertex(0).curve_tangent == .25, "2D vertex metadata native");
        });
        fresh.for_each<DxfPolyline3D>([](auto &p) {
            p.set_closed(true);
            p.set_vertex_flags(0, 32);
            auto h = p.polyline_header();
            h.elevation = 12;
            h.normal = {0, 1, 0};
            h.start_width = 2;
            h.end_width = 3;
            h.smooth_type = 6;
            h.m_density = 4;
            h.n_density = 5;
            p.set_polyline_header(h);
        });
        fresh.for_each<DxfPolygonMesh>([](auto &p) {
            auto h = p.polyline_header();
            h.flags |= 33;
            h.m_density = 3;
            h.n_density = 4;
            h.smooth_type = 6;
            h.elevation = 13;
            h.normal = {0, 1, 0};
            p.set_polyline_header(h);
        });
        fresh.for_each<DxfPolyfaceMesh>([](auto &p) {
            auto h = p.polyline_header();
            h.elevation = 14;
            h.thickness = 3;
            h.start_width = 2;
            h.end_width = 4;
            h.smooth_type = 6;
            p.set_polyline_header(h);
            auto v = p.vertex(0);
            v.start_width = 5;
            v.end_width = 6;
            v.bulge = .5;
            v.curve_tangent = .25;
            v.id = 9;
            p.set_vertex_metadata(0, v);
            p.set_vertex_position(0, {7, 8, 9});
            p.set_face_color(0, Color::from_true_color_value(0x123456));
            check(p.face(0).color == Color::from_true_color_value(0x123456), "face RGB");
        });
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "drawing references");
        auto loaded = read(save(fresh));
        loaded.for_each<DxfPolyfaceMesh>([](auto &p) {
            auto h = p.polyline_header();
            auto v = p.vertex(0);
            check(h.thickness == 3 && h.start_width == 2 && h.end_width == 4 && v.id == 9 &&
                      v.start_width == 5 && v.end_width == 6 && v.bulge == .5 && v.curve_tangent == .25,
                  "polyface metadata saved");
        });
        loaded.for_each<DxfPolyline3D>([](auto &p) {
            auto h = p.polyline_header();
            check(h.m_density == 4 && h.n_density == 5 && h.smooth_type == 6, "3D smoothing metadata");
        });
        loaded.for_each<DxfDrawingObject>([](auto &o) {
            if (o.type() == "GEODATA")
                check(std::get<GeoDataDefinition>(o.definition()).coordinate_system == "中文坐标\n多行^J",
                      "native geodata controls");
        });
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
