// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
namespace {
void configure(DxfLayout &l) {
    auto d = l.definition();
    d.min_limits = {-1, -2};
    d.max_limits = {300, 220};
    d.insertion_base = {1, 2, 3};
    d.min_extents = {-3, -4, -5};
    d.max_extents = {400, 300, 6};
    d.elevation = 7;
    d.ucs_origin = {8, 9, 10};
    d.ucs_ortho_type = 1;
    l.set_definition(d);
    auto p = l.plot_settings();
    p.paper_size = "ISO_A4_(210.00_x_297.00_MM)";
    p.paper_width = 210;
    p.paper_height = 297;
    p.paper_units = 1;
    p.rotation = 1;
    p.margin_left = 3;
    p.margin_bottom = 4;
    p.margin_right = 5;
    p.margin_top = 6;
    p.scale_denominator = 2;
    p.scale_factor = .5;
    p.style_sheet = "monochrome.ctb";
    p.shade_dpi = 600;
    l.set_plot_settings(p);
}
ViewportDefinition viewport() {
    ViewportDefinition v;
    v.center = {100, 120, 0};
    v.width = 180;
    v.height = 200;
    v.view_center = {1, 2, 0};
    v.view_direction = {1, 1, 1};
    v.view_target = {3, 4, 5};
    v.view_height = 25;
    v.snap_base = {2, 3, 0};
    v.snap_spacing = {4, 5, 0};
    v.grid_spacing = {6, 7, 0};
    v.snap_angle = .25;
    v.twist_angle = -.5;
    v.status_flags |= 0x4000 | (6 << 18);
    v.ucs_per_viewport = true;
    v.ucs_at_origin = true;
    v.ucs_origin = {7, 8, 9};
    v.elevation = 3;
    v.shade_plot_mode = 2;
    v.grid_major = 10;
    v.default_lighting = false;
    v.default_lighting_type = 2;
    v.brightness = .3;
    v.contrast = .7;
    v.ambient_color = Color::from_rgb(12, 34, 56);
    return v;
}
void verify(const DxfDocument &d) {
    check(d.get_layout("图纸二") && d.get_layout("sheet3") && !d.get_layout("missing"), "layout name lookup");
    for (auto name : {"图纸二", "Sheet3"}) {
        auto l = d.get_layout(name);
        auto p = l->plot_settings();
        check(p.paper_width == 210 && p.paper_height == 297 && p.paper_units == 1 && p.rotation == 1 &&
                  p.shade_dpi == 600,
              "plot fields");
        check(l->definition().min_limits == Vector2{-1, -2} && l->definition().elevation == 7,
              "layout subclass fields");
        auto members = d.entity_handles_in_layout(name);
        check(members.size() == 3 && d.get_record(l->viewport_handle()),
              "layout members and overall viewport");
    }
    int detail = 0;
    d.for_each<DxfViewport>([&](const auto &e) {
        auto v = e.definition();
        if (v.id != 2)
            return;
        ++detail;
        check(v.center == Point3{100, 120, 0} && v.view_height == 25 && v.view_direction == Point3{1, 1, 1} &&
                  v.snap_spacing == Point3{4, 5, 0} && std::abs(v.twist_angle + .5) < 1e-12 && v.is_on() &&
                  v.ucs_per_viewport && v.shade_plot_mode == 2 &&
                  v.ambient_color == Color::from_rgb(12, 34, 56) && v.frozen_layers.size() == 1 &&
                  v.clip_boundary,
              "viewport fields");
    });
    check(detail == 2, "detail viewports");
}
template <class F> void failure(DxfDocument &d, F f) {
    auto before = save(d);
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed && save(d) == before, "atomic layout failure");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        for (auto name : {"图纸二", "Sheet3"}) {
            made.add_layout(name);
            made.for_each<DxfLayout>([&](auto &l) {
                if (l.name() == name)
                    configure(l);
            });
            EntityOptions o;
            o.layout_name = name;
            auto circle = made.add_circle({100, 120, 0}, 80, o);
            auto v = viewport();
            v.clip_boundary = circle;
            v.status_flags |= 0x10000;
            v.frozen_layers = {std::stoull(made.get_layer("0")->handle(), nullptr, 16)};
            made.add_viewport(v, o);
        }
        auto gaps = made;
        gaps.for_each<DxfLayout>([](auto &layout) {
            if (layout.name() == "Sheet3") {
                auto value = layout.definition();
                value.tab_order = 42;
                layout.set_definition(value);
            }
        });
        gaps.add_layout("After gap");
        check(gaps.get_layout("After gap")->definition().tab_order == 43, "layout after sparse tab orders");
        write(gaps, prefix + "-gap.dxf");
        verify(made);
        check(made.reference_issues().empty(), "created layout references");
        write(made, prefix + "-created.dxf");
        failure(made, [&] { made.add_layout("sheet3"); });
        failure(made, [&] { made.remove_entity(made.get_layout("图纸二")->viewport_handle()); });
        failure(made, [&] {
            EntityOptions o;
            o.layout_name = "missing";
            made.add_circle({}, 1, o);
        });
        std::ifstream input(argv[2], std::ios::binary);
        auto d = DxfDocument::read(input);
        verify(d);
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        const auto first = d.get_layout("图纸二")->handle(),
                   owner = d.get_layout("图纸二")->block_record_handle();
        std::string target;
        d.for_each<DxfLayout>([&](auto &l) {
            if (l.handle() != first)
                return;
            auto p = l.plot_settings();
            p.paper_width = 420;
            p.paper_height = 297;
            p.rotation = 0;
            l.set_plot_settings(p);
            auto layout = l.definition();
            layout.min_limits = {11, 12};
            layout.elevation = 13;
            l.set_definition(layout);
        });
        d.for_each<DxfViewport>([&](auto &e) {
            auto v = e.definition();
            if (e.owner_handle() != owner || v.id != 2)
                return;
            target = e.handle();
            v.view_height = 50;
            v.twist_angle = .75;
            v.status_flags &= ~0x8000;
            v.dxf_status = 0;
            v.frozen_layers.clear();
            v.ambient_color = Color::from_index(3);
            e.set_definition(v);
        });
        check(!target.empty(), "target viewport");
        auto after = records(save(d), binary);
        for (auto &r : before) {
            auto h = identity(r);
            if (!h.empty() && h != first && h != target)
                check(at(after, h) == r, "neighbor layout record unchanged");
        }
        write(d, prefix + "-edited.dxf");
        check(d.remove_entity(target), "detail viewport deletion");
        check(d.entity_handles_in_layout("图纸二").size() == 2 &&
                  d.entity_handles_in_layout("Sheet3").size() == 3 && d.reference_issues().empty(),
              "layout memberships after delete");
        write(d, prefix + "-deleted.dxf");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
