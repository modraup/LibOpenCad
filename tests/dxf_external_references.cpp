// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
namespace {
RasterImageDefinition image(const DxfDocument &d, std::string_view h) {
    RasterImageDefinition v;
    d.for_each<DxfRasterImage>([&](const auto &i) {
        if (i.handle() == h)
            v = i.definition();
    });
    return v;
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "external reference rejection must be atomic");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        const std::string prefix = argv[3];
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        const auto model = d.model_space_entity_handles(), paper = d.paper_space_entity_handles();
        const auto h = model[0], neighbor = paper[0];
        auto v = image(d, h);
        const auto def = hex(v.definition);
        check(v.boundary.size() == 3 && v.size.x == 64, "image geometry");
        d.for_each<DxfRasterImage>([](auto &i) {
            auto v = i.definition();
            if (i.is_wipeout())
                check(v.clip_inside && v.boundary.size() == 4 && v.boundary.front() == v.boundary.back(),
                      "wipeout closed ring");
            i.set_definition(v);
        });
        d.for_each<DxfUnderlay>([](auto &u) { u.set_definition(u.definition()); });
        d.for_each<DxfUnderlayDefinition>([](auto &u) {
            check(u.definition().unloaded, "NOLOAD restored");
            u.set_definition(u.definition());
        });
        d.for_each<DxfImageDefinition>([](auto &o) { o.set_definition(o.definition()); });
        write(d, prefix + "-roundtrip.dxf");
        auto before = records(save(d), binary);
        v.insertion.z = 12;
        v.boundary[1].x = 42;
        v.brightness = 75;
        d.for_each<DxfRasterImage>([&](auto &i) {
            if (i.handle() == h)
                i.set_definition(v);
        });
        d.for_each<DxfUnderlay>([](auto &u) {
            auto v = u.definition();
            v.scale.x = 2;
            v.boundary[0].y = -7;
            u.set_definition(v);
        });
        write(d, prefix + "-edited.dxf");
        auto after = records(save(d), binary);
        check(at(before, neighbor) == at(after, neighbor) && at(before, def) == at(after, def),
              "neighbors and definition preserved");
        auto copy = hex(d.copy_entity(h, EntitySpace::Paper));
        auto copied = image(d, copy);
        check(copied.reactor != v.reactor && copied.definition == v.definition,
              "fresh reactor shared definition");
        d.for_each<DxfImageReactor>([&](const auto &r) {
            if (r.handle() == hex(copied.reactor))
                check(r.image_handle() == std::stoull(copy, nullptr, 16), "typed copied reactor");
        });
        write(d, prefix + "-copied.dxf");
        d.for_each<DxfRasterImage>([&](auto &i) {
            if (i.handle() == copy) {
                auto c = i.definition();
                c.brightness = 10;
                i.set_definition(c);
            }
        });
        check(image(d, h).brightness == 75, "copy editing independence");
        check(d.remove_entity(copy) && !d.get_record(hex(copied.reactor)) && d.get_record(def),
              "reactor cascade retains shared definition");
        write(d, prefix + "-deleted.dxf");
        auto scoped = d;
        scoped.for_each<DxfRasterImage>([&](auto &i) {
            if (i.handle() == h) {
                auto c = i.definition();
                c.extra_fields = {{102, "{PRIVATE"}, {14, "999"}, {24, "999"}, {102, "}"}};
                i.set_definition(c);
            }
        });
        auto reloaded = read(save(scoped));
        check(image(reloaded, h).boundary.size() == 3 && image(reloaded, h).extra_fields.size() == 4,
              "private clipping values preserved");
        auto fresh = DxfDocument::create(binary);
        ImageDefinition id;
        id.file_name = "新图像.png";
        id.width = 64;
        id.height = 32;
        id.loaded = true;
        id.pixel_size = {.5, .25};
        id.resolution_unit = 5;
        auto ih = fresh.add_image_definition("图片", id);
        v.definition = ih;
        v.reactor = 0;
        fresh.add_raster_image(v);
        EntityOptions opts;
        opts.space = EntitySpace::Paper;
        fresh.add_raster_image(v, opts);
        RasterImageDefinition w;
        w.clipping = true;
        w.flags = 7;
        w.clip_type = 2;
        w.clip_inside = true;
        w.boundary = {{0, 0}, {2, 0}, {1, 3}};
        fresh.add_wipeout(w);
        for (auto kind : {UnderlayType::Pdf, UnderlayType::Dwf, UnderlayType::Dgn}) {
            UnderlayFileDefinition file;
            file.file_name = "新底图.dat";
            file.page_name = "一";
            file.unloaded = true;
            auto uh = fresh.add_underlay_definition(kind, "底图", file);
            UnderlayDefinition u;
            u.definition = uh;
            u.rotation = .5;
            u.boundary = {{1, 2}, {5, 6}};
            fresh.add_underlay(kind, u);
        }
        RasterVariablesDefinition vars;
        vars.frame = 2;
        vars.units = 1;
        fresh.set_raster_variables(vars);
        fresh.for_each<DxfImageReactor>(
            [](auto &r) { check(r.class_version() == 2, "reactor version width"); });
        auto reread_reactors = read(save(fresh));
        int reactor_count = 0;
        reread_reactors.for_each<DxfImageReactor>([&](auto &r) {
            check(r.class_version() == 2 && r.image_handle() != 0, "saved reactor version and target");
            ++reactor_count;
        });
        check(reactor_count == 2, "reactor record boundaries");
        write(fresh, prefix + "-fresh.dxf");
        check(fresh.reference_issues().empty(), "fresh image graph");
        std::vector<std::string> underlays;
        fresh.for_each<DxfUnderlay>([&](const auto &e) { underlays.push_back(e.handle()); });
        std::vector<std::string> underlay_copies;
        for (auto h : underlays)
            underlay_copies.push_back(hex(fresh.copy_entity(h, EntitySpace::Paper)));
        write(fresh, prefix + "-underlay-copied.dxf");
        for (auto h : underlay_copies)
            check(fresh.remove_entity(h), "underlay copy removal");
        write(fresh, prefix + "-underlay-deleted.dxf");
        check(fresh.reference_issues().empty(), "underlay copy/delete graph");
        fails(fresh, [&] { fresh.add_image_definition("图片", id); });
        fails(fresh, [&] {
            auto bad = v;
            bad.definition = 0xfffffff;
            fresh.add_raster_image(bad);
        });
        fails(fresh, [&] {
            auto bad = v;
            bad.insertion.x = std::numeric_limits<double>::quiet_NaN();
            fresh.add_raster_image(bad);
        });
        fails(fresh, [&] {
            UnderlayDefinition bad;
            bad.definition = ih;
            fresh.add_underlay(UnderlayType::Pdf, bad);
        });
        for (auto id : model)
            check(d.remove_entity(id), "remove model external entity");
        for (auto id : paper)
            check(d.remove_entity(id), "remove paper image");
        write(d, prefix + "-removed.dxf");
        check(d.get_record(def), "unused definition stays");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
