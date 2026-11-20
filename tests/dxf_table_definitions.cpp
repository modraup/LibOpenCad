// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <limits>
namespace {
UcsDefinition ucs() {
    UcsDefinition u;
    u.origin = {1, 2, 3};
    u.x_axis = {0, 1, 0};
    u.y_axis = {-1, 0, 0};
    u.elevation = 4;
    u.ortho_type = 2;
    u.ortho_view_type = 3;
    u.flags = 0x40;
    return u;
}
ViewDefinition view() {
    ViewDefinition v;
    v.flags = 1;
    v.view_mode = 23;
    v.center = {2, 3, 0};
    v.direction = {1, 2, 3};
    v.target = {4, 5, 6};
    v.height = 17;
    v.width = 23;
    v.lens_length = 35;
    v.front_clip = 2;
    v.back_clip = 9;
    v.twist_angle = .5;
    v.ucs_associated = true;
    v.ucs_origin = {7, 8, 9};
    v.ucs_x_axis = {0, 1, 0};
    v.ucs_y_axis = {-1, 0, 0};
    v.ucs_elevation = 3;
    v.ucs_ortho_type = 2;
    v.render_mode = 3;
    v.camera_plottable = true;
    v.use_default_lights = false;
    v.default_lighting_type = 2;
    v.brightness = .2;
    v.contrast = .8;
    v.ambient_color = Color::from_rgb(10, 20, 30);
    return v;
}
VPortDefinition vport() {
    VPortDefinition v;
    v.lower_left = {.1, .2};
    v.upper_right = {.8, .9};
    v.view_center = {1, 2};
    v.snap_base = {3, 4};
    v.snap_spacing = {5, 6};
    v.grid_spacing = {7, 8};
    v.view_direction = {1, 2, 3};
    v.view_target = {4, 5, 6};
    v.view_height = 17;
    v.aspect_ratio = 2;
    v.lens_length = 35;
    v.front_clip = 2;
    v.back_clip = 9;
    v.snap_rotation = .25;
    v.view_twist = .5;
    v.view_mode = 31;
    v.circle_zoom = 80;
    v.fast_zoom = false;
    v.ucs_icon_flags = 1;
    v.snap_on = true;
    v.grid_on = true;
    v.snap_style = true;
    v.snap_isopair = 2;
    v.grid_flags = 7;
    v.grid_major = 9;
    v.ucs_per_viewport = false;
    v.ucs_origin = {7, 8, 9};
    v.ucs_x_axis = {0, 1, 0};
    v.ucs_y_axis = {-1, 0, 0};
    v.ucs_elevation = 3;
    v.ucs_ortho_type = 2;
    v.render_mode = 3;
    v.use_default_lights = false;
    v.default_lighting_type = 2;
    v.brightness = .2;
    v.contrast = .8;
    v.ambient_color = Color::from_rgb(10, 20, 30);
    return v;
}
void verify(const DxfDocument &d) {
    for (auto name : {"中文定义", "Neighbor"}) {
        auto u = d.get_ucs(name);
        auto v = d.get_view(name);
        auto p = d.get_vport(name);
        check(u && v && p, "typed table lookup");
        check(u->origin == Point3{1, 2, 3} && u->y_axis == Point3{-1, 0, 0} && u->elevation == 4 &&
                  u->ortho_view_type == 3 && u->flags == 0x40,
              "UCS fields");
        check(v->center == Point3{2, 3, 0} && v->height == 17 && v->view_mode == 23 &&
                  std::abs(v->twist_angle - .5) < 1e-12 && v->ambient_color == Color::from_rgb(10, 20, 30),
              "VIEW fields");
        check(p->lower_left == Vector2{.1, .2} && p->grid_spacing == Vector2{7, 8} && p->view_mode == 31 &&
                  p->grid_flags == 7 && p->snap_style && !p->use_default_lights,
              "VPORT fields");
    }
    check(!d.get_ucs("missing") && !d.get_view("missing") && !d.get_vport("missing"), "missing table names");
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "table atomic failure");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        for (auto name : {"中文定义", "Neighbor"}) {
            made.add_ucs(name, ucs());
            made.add_view(name, view());
            made.add_vport(name, vport());
        }
        verify(made);
        write(made, prefix + "-created.dxf");
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        verify(d);
        auto before = records(save(d), binary);
        auto u = *d.get_ucs("中文定义");
        u.origin = {10, 20, 30};
        u.elevation = -4;
        d.set_ucs("中文定义", u);
        auto v = *d.get_view("中文定义");
        v.height = 27;
        v.twist_angle = -.75;
        v.view_mode = 0;
        v.ambient_color = Color::by_block();
        v.named_ucs_handle = std::stoull(d.get_symbol(SymbolTable::Ucs, "中文定义")->handle, nullptr, 16);
        d.set_view("中文定义", v);
        auto p = *d.get_vport("中文定义");
        p.snap_spacing = {9, 10};
        p.view_twist = -.75;
        p.grid_flags = 0;
        p.ambient_color = Color::from_index(2);
        p.named_ucs_handle = v.named_ucs_handle;
        d.set_vport("中文定义", p);
        auto after = records(save(d), binary);
        for (auto table : {SymbolTable::Ucs, SymbolTable::View, SymbolTable::VPort}) {
            auto h = d.get_symbol(table, "Neighbor")->handle;
            check(at(before, h) == at(after, h), "neighbor table unchanged");
        }
        write(d, prefix + "-edited.dxf");
        check(d.reference_issues().empty(), "table reference integrity");
        fails(d, [&] {
            auto bad = p;
            bad.base_ucs_handle = 0xFFFFFF;
            d.set_vport("中文定义", bad);
        });
        fails(d, [&] {
            auto bad = u;
            bad.origin.z = std::numeric_limits<double>::infinity();
            d.set_ucs("中文定义", bad);
        });
        fails(d, [&] { d.add_view("neighbor"); });
        // A partial UCS point follows PointReader's all-or-default semantics; VPORT uses component defaults.
        auto raw = records(save(d), binary);
        auto uh = d.get_symbol(SymbolTable::Ucs, "Neighbor")->handle,
             ph = d.get_symbol(SymbolTable::VPort, "Neighbor")->handle;
        auto &ur = at(raw, uh);
        ur.erase(std::remove_if(ur.begin(), ur.end(), [](auto &q) { return q.first == 21; }), ur.end());
        auto &pr = at(raw, ph);
        pr.erase(std::remove_if(pr.begin(), pr.end(), [](auto &q) { return q.first == 26; }), pr.end());
        auto partial = read(encode(raw, binary));
        check(partial.get_ucs("Neighbor")->x_axis == Point3{1, 0, 0}, "UCS partial point fallback");
        check(partial.get_vport("Neighbor")->view_direction == Point3{1, 0, 3},
              "VPORT partial component fallback");
        write(partial, prefix + "-partial.dxf");
        // Preserve opaque application values and private duplicate field codes.
        raw = records(save(d), binary);
        auto &r = at(raw, uh);
        r.insert(r.begin() + 2,
                 {{102, "{PRIVATE"}, {10, binary ? detail::binary_double(999) : "999"}, {102, "}"}});
        auto protected_doc = read(encode(raw, binary));
        protected_doc.set_ucs("Neighbor", ucs());
        auto protected_raw = records(save(protected_doc), binary);
        check(at(protected_raw, uh)[3] == r[3], "private data preserved");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
