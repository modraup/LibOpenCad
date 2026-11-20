// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include <libopencad/dwg_create.hpp>
#include "../src/dwg_metadata_internal.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>
using namespace libopencad;
static void check(bool value, const char *message) {
    if (!value)
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
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument("input prefix");
        std::ifstream input(argv[1], std::ios::binary);
        auto d = DwgDocument::read(input);
        auto prefix = std::string(argv[2]);
        auto initial = d;
        auto shape = acis::build_box({2, 3, 4}, 5, 6, 7);
        std::ofstream ids(prefix + ".ids"), mapping(prefix + ".map");
        auto version = dxf_version_from_string(bytes(d).substr(0, 6));
        std::istringstream empty(create_dwg(version, {}));
        auto fresh = DwgDocument::read(empty);
        for (auto kind : {ModelerKind::Solid3D, ModelerKind::Region, ModelerKind::Body})
            fresh.add_modeler(kind, shape);
        save(fresh, prefix + ".fresh.dwg");
        for (auto kind : {ModelerKind::Solid3D, ModelerKind::Region, ModelerKind::Body}) {
            auto h = d.add_modeler(kind, shape);
            check(d.modeler_geometry(h).has_value(), "Created geometry");
            ids << "new " << h << ' ' << d.modeler(h).type << '\n';
        }
        auto original = initial.objects().modelers;
        std::set<std::string> types;
        for (auto &[h, m] : original)
            if (m.surface && types.insert(m.surface->type).second) {
                auto id = d.add_surface(*m.surface, shape);
                fresh.add_surface(*m.surface, shape);
                ids << "surface " << id << ' ' << h << '\n';
            }

        save(fresh, prefix + ".fresh.dwg");
        std::uint64_t first_copy = 0;
        for (auto &[h, m] : original) {
            std::map<std::uint64_t, std::uint64_t> remap;
            auto copied = d.copy_modeler(h, d.objects().paper_space, &remap);
            if (!first_copy)
                first_copy = copied;
            for (auto &[old, new_id] : remap)
                mapping << h << ' ' << old << ' ' << new_id << '\n';
            check(d.modeler(copied).properties.owner == d.objects().paper_space, "Copy ownership");
            ids << "copy " << h << ' ' << copied << '\n';
        }
        save(d, prefix + ".copied.dwg");
        auto snapshot = d;
        for (const auto &item : initial.objects().metadata_records)
            if (item.second.type == "XRECORD") {
                auto raw = item.second.metadata;
                detail::metadata_append(raw, 340, first_copy, 8);
                auto guarded_bytes = detail::edit_dwg_metadata(bytes(d), d.objects(), item.first, raw);
                std::istringstream guarded_input(guarded_bytes);
                auto guarded = DwgDocument::read(guarded_input);
                bool rejected = false;
                try {
                    guarded.remove_modeler(first_copy);
                } catch (const std::invalid_argument &) {
                    rejected = true;
                }
                check(rejected && bytes(guarded) == guarded_bytes,
                      "Surviving XRECORD reference refuses deletion atomically");
                break;
            }
        auto removed_source = d;
        removed_source.remove_modeler(original.begin()->first);
        save(removed_source, prefix + ".deleted-source.dwg");
        auto edited = d;
        edited.set_modeler_geometry(first_copy, acis::build_sphere({10, 20, 30}, 4));
        save(edited, prefix + ".edited.dwg");
        ids << "edit " << first_copy << " 0\n";

        std::vector<std::uint64_t> copies;
        for (auto &[h, m] : d.objects().modelers)
            if (!original.count(h))
                copies.push_back(h);
        std::uint64_t named = 0, layout = 0, layout_block = 0;
        for (auto &[h, s] : d.objects().symbols)
            if (s.type == 49) {
                if (s.name == "MODEL_DETAIL")
                    named = h;
                else if (h != d.objects().model_space && h != d.objects().paper_space && s.layout) {
                    layout = s.layout;
                    layout_block = h;
                }
            }
        if (named) {
            auto moved = d;
            for (auto h : copies)
                moved.move_entity(h, named);
            save(moved, prefix + ".block.dwg");
            ids << "block " << named << " 0\n";
            if (layout) {
                moved.move_entity(first_copy, layout);
                save(moved, prefix + ".layout.dwg");
                ids << "layout " << layout_block << ' ' << layout << '\n';
            }
        }
        for (auto h : copies)
            d.move_entity(h, d.objects().model_space);
        save(d, prefix + ".moved.dwg");
        for (auto h : copies)
            d.remove_modeler(h);
        save(d, prefix + ".removed.dwg");
        check(d.objects().modelers.size() == initial.objects().modelers.size(), "Delete original count");
        for (auto &[h, r] : initial.objects().records)
            check(d.objects().records.count(h), "Original object preserved");
        auto before = bytes(d);
        bool failed = false;
        try {
            d.copy_modeler(original.begin()->first, UINT64_MAX);
        } catch (const std::exception &) {
            failed = true;
        }
        check(failed && bytes(d) == before, "Bad target rollback");
        failed = false;
        try {
            d.add_modeler(ModelerKind::Solid3D, acis::SatDocument{});
        } catch (const std::exception &) {
            failed = true;
        }
        check(failed && bytes(d) == before, "Failed geometry must not consume a handle or append a record");
        std::cout << "DWG modeler lifecycle verified\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
