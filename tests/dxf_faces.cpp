// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <limits>
namespace {
const std::vector<Point3> quad{{0, 0, 1}, {6, 0, 2}, {0, 4, 3}, {6, 4, 4}},
    tri{{0, 0, 1}, {6, 0, 2}, {0, 4, 3}};
template <class F> void fail(DxfDocument &d, F f) {
    auto before = save(d);
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught && save(d) == before, "atomic failure");
}
void verify(const DxfDocument &d) {
    int solid = 0, face = 0, trace = 0, triangle = 0;
    d.for_each<DxfSolid>([&](const auto &s) {
        ++solid;
        trace += s.is_trace();
        triangle += s.is_triangle();
        check(s.corner(0) == quad[0] && s.corner(1) == quad[1] && s.corner(2) == quad[2],
              "stored solid corners");
        check(s.normal() == Vector3::UNIT_Z && s.thickness() == 0, "solid defaults");
        if (!s.is_triangle())
            check(s.boundary_corners() == std::vector<Point3>{quad[0], quad[1], quad[3], quad[2]},
                  "SOLID boundary order");
    });
    d.for_each<DxfFace3D>([&](const auto &f) {
        ++face;
        triangle += f.is_triangle();
        check(f.invisible_edges() == 9, "hidden edges");
        check(f.area() > 0, "face area");
    });
    check(solid == 6 && face == 4 && trace == 2 && triangle == 4, "face inventory");
}
DxfDocument create(bool binary) {
    auto d = DxfDocument::create(binary);
    d.add_layer("面");
    for (auto space : {EntitySpace::Model, EntitySpace::Paper}) {
        EntityOptions o;
        o.layer = "面";
        o.space = space;
        d.add_solid(quad, o);
        d.add_solid(tri, o);
        d.add_trace(quad, o);
        d.add_face3d({quad[0], quad[1], quad[3], quad[2]}, 9, o);
        d.add_face3d(tri, 9, o);
    }
    verify(d);
    return d;
}
void edit(DxfDocument &d, const std::vector<std::string> &ids, bool copy) {
    auto matches = [&](const auto &e) { return std::find(ids.begin(), ids.end(), e.handle()) != ids.end(); };
    d.for_each<DxfSolid>([&](auto &s) {
        if (!matches(s))
            return;
        if (copy)
            s.set_corner(0, {101, 102, 103});
        else {
            const bool tri = s.is_triangle();
            s.set_corner(2, {0, 9, 3});
            if (tri)
                s.set_corner(3, {0, 9, 3});
            s.set_thickness(4);
            s.set_normal({0, 1, 0});
        }
    });
    d.for_each<DxfFace3D>([&](auto &f) {
        if (!matches(f))
            return;
        if (copy) {
            f.set_corner(0, {101, 102, 103});
            f.set_invisible_edges(7);
        } else {
            f.set_corner(1, {8, 0, 5});
            f.set_edge_invisible(0, false);
            f.set_edge_invisible(2, true);
        }
    });
}
void same_records(Records a, Records b, const std::set<std::string> &except = {}) {
    for (const auto &r : a) {
        auto h = identity(r);
        if (!h.empty() && !except.count(h))
            check(at(b, h) == r, "neighbor data changed");
    }
}
void boundaries(bool binary, const std::string &prefix) {
    auto d = create(binary);
    const auto all = d.model_space_entity_handles();
    fail(d, [&] { d.add_solid({}); });
    fail(d, [&] { d.add_trace({quad[0], quad[1]}); });
    fail(d, [&] { d.add_face3d(quad, 16); });
    auto invalid = quad;
    invalid[2].y = std::numeric_limits<double>::infinity();
    fail(d, [&] { d.add_solid(invalid); });
    fail(d, [&] { d.for_each<DxfSolid>([](auto &s) { s.set_corner(4, {}); }); });
    fail(d, [&] { d.for_each<DxfFace3D>([](auto &f) { f.set_edge_invisible(4, true); }); });
    fail(d, [&] {
        d.for_each<DxfFace3D>(
            [](auto &f) { f.set_corner(1, {0, 0, std::numeric_limits<double>::quiet_NaN()}); });
    });
    auto data = records(save(d), binary);
    for (auto &r : data)
        if (r.front().second == "SOLID" || r.front().second == "TRACE" || r.front().second == "3DFACE")
            r.erase(std::remove_if(r.begin(), r.end(),
                                   [](const auto &p) {
                                       return p.first == 13 || p.first == 23 || p.first == 33 ||
                                              p.first == 70 || p.first == 100;
                                   }),
                    r.end());
    auto defaults = read(encode(data, binary));
    write(defaults, prefix + "-defaults.dxf");
    defaults.for_each<DxfSolid>([](const auto &s) {
        check(s.is_triangle() && s.corner(2) == s.corner(3), "missing solid fourth corner");
    });
    defaults.for_each<DxfFace3D>([](const auto &f) {
        check(f.is_triangle() && f.invisible_edges() == 0, "missing face corner/flags");
    });
    // Adjacent repeated corners at any edge form a triangle in upstream Face3D.
    for (std::size_t i = 0; i < 4; ++i) {
        auto one = DxfDocument::create(binary);
        auto vertices = quad;
        vertices[(i + 1) % 4] = vertices[i];
        one.add_face3d(vertices);
        one.for_each<DxfFace3D>([](const auto &f) {
            check(f.is_triangle() && f.corners().size() == 3, "adjacent duplicate corner");
        });
    }
    auto flag_data = records(save(d), binary);
    auto &f = at(flag_data, all[3]);
    for (auto &p : f)
        if (p.first == 70)
            p.second = binary ? detail::binary_short(0x109) : "265";
    auto flags = read(encode(flag_data, binary));
    flags.for_each<DxfFace3D>([&](auto &e) {
        if (e.handle() == all[3]) {
            check(e.invisible_edges() == 9, "unknown flags masked");
            e.set_edge_invisible(2, true);
        }
    });
    auto result = records(save(flags), binary);
    auto pair = std::find_if(at(result, all[3]).begin(), at(result, all[3]).end(),
                             [](const auto &p) { return p.first == 70; });
    check(pair->second == (binary ? detail::binary_short(0x10d) : "269"), "reserved flag bits preserved");
    auto block = hex(d.add_symbol(SymbolTable::BlockRecord, "Faces"));
    data = records(save(d), binary);
    Records moved;
    for (const auto &h : all) {
        moved.push_back(at(data, h));
        for (auto &p : moved.back())
            if (p.first == 330)
                p.second = block;
    }
    drop(data, all);
    auto end = std::find_if(data.begin(), data.end(), [&](const auto &r) {
        return r.front().second == "ENDBLK" && std::find(r.begin(), r.end(), Pair{330, block}) != r.end();
    });
    check(end != data.end(), "block terminator");
    data.insert(end, moved.begin(), moved.end());
    d = read(encode(data, binary));
    check(d.entity_handles_in_block("Faces") == all, "block face membership");
    edit(d, all, false);
    for (const auto &h : all) {
        auto clone = d.copy_entity(h);
        check(d.remove_entity(clone) && d.remove_entity(h), "block face copy/delete");
    }
    check(d.remove_symbol(SymbolTable::BlockRecord, "Faces"), "empty face block cleanup");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = create(binary);
        write(made, prefix + "-created.dxf");
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        verify(d);
        auto model = d.model_space_entity_handles(), paper = d.paper_space_entity_handles();
        std::set<std::string> owned;
        for (const auto &h : model) {
            auto dict = hex(d.add_extension_dictionary(h));
            owned.insert(dict);
            owned.insert(hex(d.add_xrecord(dict, "Data", {{340, h}})));
        }
        auto other = hex(d.add_extension_dictionary(paper[0]));
        d.add_xrecord(other, "Keep", {{1, "保留"}});
        write(d, prefix + "-before.dxf");
        auto before = records(save(d), binary);
        edit(d, model, false);
        auto after = records(save(d), binary);
        same_records(before, after, {model.begin(), model.end()});
        write(d, prefix + "-edited.dxf");
        std::vector<std::string> copies;
        for (const auto &h : model)
            copies.push_back(hex(d.copy_entity(h, EntitySpace::Paper)));
        edit(d, copies, true);
        same_records(after, records(save(d), binary));
        write(d, prefix + "-copied.dxf");
        for (const auto &h : copies)
            check(d.remove_entity(h), "copy deletion");
        same_records(after, records(save(d), binary));
        check(records(save(d), binary).size() == after.size(), "copy cascade cleanup");
        write(d, prefix + "-restored.dxf");
        for (const auto &h : model) {
            check(d.remove_entity(h), "source deletion");
            owned.insert(h);
        }
        same_records(before, records(save(d), binary), owned);
        for (const auto &h : owned)
            check(!d.get_record(h), "removed identity index");
        check(d.paper_space_entity_handles() == paper && d.reference_issues().empty(),
              "preserved membership and valid graph");
        write(d, prefix + "-deleted.dxf");
        boundaries(binary, prefix);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
