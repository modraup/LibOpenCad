// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <limits>

namespace {
bool near(Vector3 a, Vector3 b) { return (a - b).length() < 1e-12; }
template <class F> void fails(DxfDocument &d, F f) {
    const auto before = save(d);
    const auto seed = d.next_handle();
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught && save(d) == before && d.next_handle() == seed, "atomic rejection");
}
template <class T, class F> void edit_one(DxfDocument &d, const std::string &h, F f) {
    int count = 0;
    d.for_each<T>([&](auto &e) {
        if (e.handle() == h) {
            f(e);
            ++count;
        }
    });
    check(count == 1, "typed entity lookup");
}
void validate_initial(const DxfDocument &d) {
    int points = 0, rays = 0, xlines = 0;
    d.for_each<DxfPoint>([&](const auto &p) {
        const int n = points++;
        check(near(p.location(), {1. + 10 * n, 2, 3}) && p.thickness() == -2.5 &&
                  near(p.normal(), {0, 1, 0}) && p.x_axis_angle() == 1.25,
              "POINT read fields");
    });
    d.for_each<DxfRay>([&](const auto &r) {
        const int n = rays++;
        check(near(r.base_point(), {4. + 10 * n, 5, 6}) && near(r.direction(), {0.6, 0.8, 0}),
              "RAY read fields");
    });
    d.for_each<DxfXLine>([&](const auto &x) {
        const int n = xlines++;
        check(near(x.base_point(), {7. + 10 * n, 8, 9}) && near(x.direction(), {0, 0, -1}),
              "XLINE read fields");
    });
    check(points == 2 && rays == 2 && xlines == 2, "typed geometry inventory");
    check(d.duplicate_handles().empty(), "unique geometry identities");
    auto common = [&](const auto &e) {
        check(e.layer() == "几何" && e.color() == Color::from_rgb(12, 34, 56) &&
                  e.linetype() == "Continuous" && e.line_weight() == LineWeight::from_value(35),
              "common properties");
        const auto paper = d.paper_space_entity_handles();
        check(e.is_invisible() == (std::find(paper.begin(), paper.end(), e.handle()) != paper.end()),
              "visibility and ownership");
    };
    d.for_each<DxfPoint>(common);
    d.for_each<DxfRay>(common);
    d.for_each<DxfXLine>(common);
    check(d.model_space_entity_handles().size() == 3 && d.paper_space_entity_handles().size() == 3,
          "space memberships");
}
DxfDocument create(bool binary) {
    auto d = DxfDocument::create(binary);
    d.add_layer("几何");
    for (int n = 0; n < 2; ++n) {
        EntityOptions o;
        o.layer = "几何";
        o.linetype = "Continuous";
        o.color = Color::from_rgb(12, 34, 56);
        o.line_weight = LineWeight::from_value(35);
        o.invisible = n != 0;
        o.space = n ? EntitySpace::Paper : EntitySpace::Model;
        auto h = hex(d.add_point({1. + 10 * n, 2, 3}, o));
        edit_one<DxfPoint>(d, h, [](auto &p) {
            check(p.normal() == Vector3::UNIT_Z && p.thickness() == 0 && p.x_axis_angle() == 0,
                  "POINT new defaults");
            p.set_thickness(-2.5);
            p.set_normal({0, 1, 0});
            p.set_x_axis_angle(1.25);
        });
        d.add_ray({4. + 10 * n, 5, 6}, {3, 4, 0}, o);
        d.add_xline({7. + 10 * n, 8, 9}, {0, 0, -5}, o);
    }
    validate_initial(d);
    return d;
}
void change(DxfDocument &d, const std::vector<std::string> &h, bool copy) {
    edit_one<DxfPoint>(d, h[0], [&](auto &p) {
        p.set_location(copy ? Point3{101, 102, 103} : Point3{-1, -2, -3});
        if (!copy) {
            p.set_thickness(4);
            p.set_normal({1, 0, 0});
            p.set_x_axis_angle(-0.75);
        }
    });
    edit_one<DxfRay>(d, h[1], [&](auto &r) {
        if (!copy)
            r.set_base_point({-4, -5, -6});
        r.set_direction(copy ? Vector3{} : Vector3{0, 5, 12});
    });
    edit_one<DxfXLine>(d, h[2], [&](auto &x) {
        if (!copy)
            x.set_base_point({-7, -8, -9});
        x.set_direction(copy ? Vector3{1, 2, 2} : Vector3{});
    });
}
void unchanged(Records before, Records after, const std::set<std::string> &excluded = {}) {
    for (auto &r : before) {
        const auto h = identity(r);
        if (h.empty() || excluded.count(h))
            continue;
        check(at(after, h) == r, "unrelated record changed");
    }
}
void boundaries(bool binary, const std::string &prefix) {
    auto d = DxfDocument::create(binary);
    const auto p = hex(d.add_point({1, 2, 3})), r = hex(d.add_ray({4, 5, 6}, {3, 4, 0})),
               x = hex(d.add_xline({7, 8, 9}, {0, 0, -5}));
    const auto nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    fails(d, [&] { d.add_point({1, nan, 3}); });
    fails(d, [&] { d.add_ray({}, {inf, 1, 2}); });
    fails(d, [&] { d.add_xline({}, {1, nan, 2}); });
    fails(d, [&] { edit_one<DxfPoint>(d, p, [&](auto &e) { e.set_location({4, 5, inf}); }); });
    fails(d, [&] { edit_one<DxfPoint>(d, p, [&](auto &e) { e.set_thickness(nan); }); });
    fails(d, [&] { edit_one<DxfPoint>(d, p, [&](auto &e) { e.set_normal({1, inf, 3}); }); });
    fails(d, [&] { edit_one<DxfPoint>(d, p, [&](auto &e) { e.set_x_axis_angle(inf); }); });
    fails(d, [&] { edit_one<DxfRay>(d, r, [&](auto &e) { e.set_direction({1, nan, 2}); }); });
    fails(d, [&] { edit_one<DxfXLine>(d, x, [&](auto &e) { e.set_base_point({1, 2, inf}); }); });
    auto data = records(save(d), binary);
    for (const auto &h : {p, r, x}) {
        auto &raw = at(data, h);
        raw.erase(
            std::remove_if(raw.begin(), raw.end(),
                           [](const auto &p) { return p.first == 100 || (p.first >= 10 && p.first <= 59); }),
            raw.end());
        // Partial coordinates should fall back as in Rust PointReader.
        raw.push_back({10, binary ? detail::binary_double(123) : "123"});
    }
    auto defaults = read(encode(data, binary));
    write(defaults, prefix + "-defaults.dxf");
    edit_one<DxfPoint>(defaults, p, [](auto &e) {
        check(e.location() == Vector3::ZERO && e.normal() == Vector3::UNIT_Z, "POINT missing fields");
    });
    edit_one<DxfRay>(defaults, r, [](auto &e) {
        check(e.base_point() == Vector3::ZERO && e.direction() == Vector3::UNIT_X, "RAY missing fields");
    });
    edit_one<DxfXLine>(defaults, x, [](auto &e) {
        check(e.base_point() == Vector3::ZERO && e.direction() == Vector3::UNIT_X, "XLINE missing fields");
    });
    at(data, p).push_back({50, binary ? detail::binary_double(90) : "90"});
    for (const auto &h : {r, x})
        for (const auto &v : Record{{11, "3"}, {21, "4"}, {31, "0"}})
            at(data, h).push_back(
                {v.first, binary ? detail::binary_value_from_text(v.first, v.second) : v.second});
    const auto raw_bytes = encode(data, binary);
    auto raw = read(raw_bytes);
    edit_one<DxfRay>(raw, r,
                     [](auto &e) { check(near(e.direction(), {0.6, 0.8, 0}), "raw RAY normalization"); });
    edit_one<DxfXLine>(raw, x,
                       [](auto &e) { check(near(e.direction(), {0.6, 0.8, 0}), "raw XLINE normalization"); });
    check(records(save(raw), binary) == data, "getters must preserve raw direction bytes");
    write(raw, prefix + "-raw.dxf");
    // Private groups and XDATA must not be used as geometry fields.
    auto protected_data = data;
    auto &ray = at(protected_data, r);
    const auto begin = ray.begin() + 2;
    ray.insert(begin, {{102, "{PRIVATE"}, {11, binary ? detail::binary_double(777) : "777"}, {102, "}"}});
    ray.insert(ray.end(), {{1001, "ACAD"}, {1010, binary ? detail::binary_double(88) : "88"}});
    auto protected_doc = read(encode(protected_data, binary));
    edit_one<DxfRay>(protected_doc, r, [](auto &e) {
        check(near(e.direction(), {0.6, 0.8, 0}), "ignore private fields");
        e.set_direction({0, 0, 0});
    });
    auto protected_result = records(save(protected_doc), binary);
    check(at(protected_result, r)[3] == ray[3] && at(protected_result, r).back() == ray.back(),
          "preserve private fields and XDATA");
    // Match the existing creation policy: do not silently emit newer entities as R12.
    auto old_data = records(save(d), binary);
    for (auto &record : old_data)
        for (auto &pair : record)
            if (pair.first == 1 && pair.second == "AC1032")
                pair.second = "AC1009";
    auto old = read(encode(old_data, binary));
    fails(old, [&] { old.add_ray({}, Vector3::UNIT_X); });
    fails(old, [&] { old.add_xline({}, Vector3::UNIT_X); });
    const auto old_point = hex(old.add_point({9, 8, 7}));
    edit_one<DxfPoint>(old, old_point,
                       [](auto &e) { check(e.location() == Point3{9, 8, 7}, "legacy POINT creation"); });
    // New geometry is also typed inside named BLOCKS, with the same copy/delete path.
    auto blocks = create(binary);
    const auto block = hex(blocks.add_symbol(SymbolTable::BlockRecord, "GeometryBlock"));
    const auto members = blocks.model_space_entity_handles();
    auto br = records(save(blocks), binary);
    Records moving;
    for (const auto &h : members) {
        moving.push_back(at(br, h));
        for (auto &pair : moving.back())
            if (pair.first == 330)
                pair.second = block;
    }
    drop(br, members);
    const auto end = std::find_if(br.begin(), br.end(), [&](const auto &r) {
        return r.front().second == "ENDBLK" && std::find(r.begin(), r.end(), Pair{330, block}) != r.end();
    });
    check(end != br.end(), "named block boundary");
    br.insert(end, moving.begin(), moving.end());
    blocks = read(encode(br, binary));
    check(blocks.entity_handles_in_block("GeometryBlock") == members, "block membership order");
    change(blocks, members, false);
    for (const auto &h : members) {
        const auto clone = blocks.copy_entity(h);
        check(blocks.remove_entity(clone), "remove block copy");
        check(blocks.remove_entity(h), "remove block source");
    }
    check(blocks.remove_symbol(SymbolTable::BlockRecord, "GeometryBlock"), "remove emptied geometry block");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        const bool binary = std::string(argv[1]) == "binary";
        const std::string prefix = argv[3];
        auto created = create(binary);
        write(created, prefix + "-created.dxf");
        std::ifstream input(argv[2], std::ios::binary);
        auto d = DxfDocument::read(input);
        validate_initial(d);
        const auto model = d.model_space_entity_handles();
        const auto paper = d.paper_space_entity_handles();
        std::vector<std::string> owned;
        for (const auto &h : model) {
            const auto dict = hex(d.add_extension_dictionary(h));
            owned.push_back(dict);
            owned.push_back(hex(d.add_xrecord(dict, "Data", {{1, "扩展数据"}, {340, h}})));
            owned.push_back(hex(d.add_dictionary(dict, "Nested")));
        }
        // Neighbors have their own dictionary and must survive all source operations.
        const auto neighbor = hex(d.add_extension_dictionary(paper[0]));
        d.add_xrecord(neighbor, "Keep", {{1, "保留"}});
        write(d, prefix + "-before.dxf");
        const auto before = records(save(d), binary);
        change(d, model, false);
        const auto edited = records(save(d), binary);
        unchanged(before, edited, {model.begin(), model.end()});
        write(d, prefix + "-edited.dxf");
        {
            std::ofstream out(prefix + "-converted.dxf", std::ios::binary);
            out.exceptions(std::ios::badbit | std::ios::failbit);
            d.write(out, !binary);
        }
        std::vector<std::string> copies;
        for (const auto &h : model)
            copies.push_back(hex(d.copy_entity(h, EntitySpace::Paper)));
        change(d, copies, true);
        unchanged(edited, records(save(d), binary));
        write(d, prefix + "-copied.dxf");
        for (const auto &h : copies)
            check(d.remove_entity(h), "remove copied geometry");
        unchanged(edited, records(save(d), binary));
        check(records(save(d), binary).size() == edited.size(), "copied owned objects removed");
        write(d, prefix + "-restored.dxf");
        for (const auto &h : model) {
            check(d.remove_entity(h), "delete original geometry");
            check(!d.remove_entity(h), "idempotent deletion");
        }
        std::set<std::string> removed(model.begin(), model.end());
        removed.insert(owned.begin(), owned.end());
        unchanged(before, records(save(d), binary), removed);
        for (const auto &h : removed)
            check(!d.get_record(h), "deleted identity left indexed");
        check(d.paper_space_entity_handles() == paper && d.model_space_entity_handles().empty(),
              "survivor membership");
        check(d.reference_issues().empty() && d.duplicate_handles().empty(), "valid final graph");
        write(d, prefix + "-deleted.dxf");
        boundaries(binary, prefix);
        std::cout << "POINT/RAY/XLINE read/create/edit/copy/delete and boundary checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
