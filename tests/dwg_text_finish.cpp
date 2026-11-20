// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <iostream>
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include <limits>
#include <sstream>
using namespace libopencad;
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static std::string bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static void save(const DwgDocument &d, const std::string &path) {
    std::ofstream o(path, std::ios::binary);
    d.write(o);
    o.close();
    check(bool(o), "Write failed");
}
int main(int argc, char **argv) try {
    if (argc != 4)
        return 2;
    std::string mode = argv[1], path = argv[3];
    if (mode == "create") {
        auto version = dxf_version_from_string(argv[2]);
        std::vector<DwgPrimitive> input = {
            {DwgTextGeometry{"新建中文 A Ω", {3, -4, 2}, 2.5}, false, -1.25, Vector3::UNIT_Y},
            {DwgLineGeometry{{0, 0, 0}, {1, 2, 3}}},
            {DwgTextGeometry{"Paper B", {-7, 9, 0}, 4}, true},
            {DwgCircleGeometry{{2, 3, 0}, 5}, true},
            {DwgTextGeometry{"", {0, 0, 0}, 1}},
            {DwgTextGeometry{std::string(2200, 'x') + "中文😀", {0, 0, 0}, 1}}};
        auto data = create_dwg(version, input);
        std::istringstream in(data);
        auto d = DwgDocument::read(in);
        check(d.objects().texts.size() == 4, "Missing created TEXT");
        for (auto i : {0u, 2u, 4u, 5u}) {
            const auto &expected = std::get<DwgTextGeometry>(input[i].geometry);
            const auto &t = d.objects().texts.at(0x100 + i);
            check(t.value == expected.value && t.insertion == expected.insertion &&
                      t.height == expected.height && t.normal == input[i].normal &&
                      t.thickness == input[i].thickness,
                  "Created TEXT mismatch");
        }
        save(d, path);
        d.move_entity(0x100, "*Paper_Space");
        auto t = d.text(0x100);
        t.set_text("创建后修改");
        d.update(t);
        save(d, path + ".edited");
        for (unsigned bad = 0; bad < 4; ++bad) {
            auto invalid = input;
            auto &t = std::get<DwgTextGeometry>(invalid[0].geometry);
            if (bad == 0)
                t.height = 0;
            if (bad == 1)
                t.insertion.x = std::numeric_limits<double>::infinity();
            if (bad == 2)
                t.value = std::string(1, char(255));
            if (bad == 3)
                t.value = std::string("a\0b", 3);
            bool refused = false;
            try {
                create_dwg(version, invalid);
            } catch (const std::exception &) {
                refused = true;
            }
            check(refused, "Accepted invalid TEXT");
        }
        return 0;
    }
    std::ifstream in(argv[2], std::ios::binary);
    auto d = DwgDocument::read(in);
    std::uint64_t target = 0, paper = 0;
    for (auto &[h, t] : d.objects().texts) {
        if (t.value == "原始中文 A")
            target = h;
        if (t.value == "图纸中文 C")
            paper = h;
    }
    check(target && paper, "Missing move sources");
    auto lineowner = d.objects().lines.begin()->second.properties.owner;
    auto layout = lineowner ? d.objects().symbols.at(lineowner).layout : 0;
    for (unsigned step = 0; step < 6; ++step) {
        auto h = step == 4 ? paper : target;
        const char *dest = step == 0 || step == 5   ? "*Model_Space"
                           : step == 1 || step == 2 ? "*Paper_Space"
                                                    : "detail";
        if (step == 1 && layout)
            d.move_entity(h, layout);
        else
            d.move_entity(h, dest);
        auto before = bytes(d);
        if (step == 1 && layout)
            d.move_entity(h, layout);
        else
            d.move_entity(h, dest);
        check(bytes(d) == before, "Repeated move changed bytes");
        for (auto bad : {std::uint64_t(0), std::uint64_t(0xffffffffffffffffULL)}) {
            bool refused = false;
            try {
                d.move_entity(bad, "*Model_Space");
            } catch (const std::exception &) {
                refused = true;
            }
            check(refused && bytes(d) == before, "Failed move was not atomic");
        }
        save(d, path + "." + std::to_string(step));
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
