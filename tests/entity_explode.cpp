// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
using namespace libopencad;
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto source = DxfDocument::read(in);
    auto original = bytes(source);
    std::vector<std::string> selected;
    source.for_each<DxfEntity>([&](const auto &e) {
        if (e.layer() != "Control")
            selected.push_back(e.handle());
    });
    auto output = source;
    EntityOptions target;
    target.space = EntitySpace::Paper;
    std::ofstream manifest(std::string(argv[2]) + ".parts");
    std::set<std::uint64_t> allocated;
    for (const auto &handle : selected) {
        auto parts = source.explode_entity(handle);
        check(bytes(source) == original, "Detached explode mutated source");
        for (const auto &p : parts)
            std::visit(
                [&](const auto &e) {
                    check(e.handle().empty(), "Detached part has identity");
                    check(e.extended_data().empty(), "Inherited XDATA unexpectedly");
                },
                p);
        auto added = output.explode_entity_into(handle, target);
        check(added.size() == parts.size(), "Insertion lost parts");
        manifest << handle << ' ' << added.size();
        for (auto h : added) {
            check(allocated.insert(h).second, "Reused handle");
            manifest << ' ' << std::hex << h << std::dec;
        }
        manifest << '\n';
    }
    check(!allocated.empty(), "No explosion targets");
    check(output.paper_space_entity_handles().size() == allocated.size(), "Wrong target ownership");
    check(output.duplicate_handles().empty(), "Duplicate output handles");
    std::ofstream out(argv[2], std::ios::binary);
    output.write(out);
    out.close();
    manifest.close();
    check(bool(out) && bool(manifest), "Save failed");
    bool removed = false;
    for (const auto &h : selected) {
        if (source.explode_entity(h).empty())
            continue;
        auto replaced = source;
        auto added = replaced.explode_entity_into(h, target, true);
        check(!replaced.get_record(h), "Source not deleted");
        check(added.size() == replaced.paper_space_entity_handles().size(), "Replacement members wrong");
        check(source.get_record(h) != nullptr, "Replacement altered source");
        auto fail = source;
        auto before = bytes(fail);
        auto bad = target;
        bad.block_name = "MissingBlock";
        bool refused = false;
        try {
            fail.explode_entity_into(h, bad, true);
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused && bytes(fail) == before, "Failed explosion partially edited drawing");
        removed = true;
    }
    check(removed, "No replacement tested");
    for (const auto h : {"0", "FFFFffffFFFF"}) {
        bool refused = false;
        try {
            source.explode_entity(h);
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Unknown identity accepted");
    }
    check(bytes(source) == original, "Source changed");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
