// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include <libopencad/dwg_sections.hpp>
#include "../src/dwg_modeler_internal.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace libopencad;
static void check(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("input prefix");
        std::ifstream in(argv[1], std::ios::binary);
        auto d = DwgDocument::read(in);
        check(d.objects().modelers.size() == 9, "Nine empty models");
        auto source = d.objects().modelers;
        for (auto &[h, m] : source)
            check(m.acis_empty, "Source models empty");
        std::ostringstream before;
        d.write(before);
        bool absent = false;
        try {
            (void)read_dwg_paged_section(before.str(), "AcDb:AcDsPrototype_1b");
        } catch (const std::invalid_argument &e) {
            absent = std::string_view(e.what()) == "DWG section not found";
        }
        check(absent, "Fixture has no datastore section");
        for (auto &[h, m] : source) {
            d.set_modeler_geometry(h, acis::build_sphere({1, 2, 3}, 2 + h % 3));
            check(!d.modeler(h).acis_empty && d.modeler(h).properties.has_ds_data, "Set entity DS flag");
            std::ostringstream bytes;
            d.write(bytes);
            auto section = read_dwg_paged_section(bytes.str(), "AcDb:AcDsPrototype_1b");
            check(detail::decode_dwg_acds(section, {h}).count(h), "Datastore handle index");
            std::ofstream out(std::string(argv[2]) + ".filled-" + std::to_string(h) + ".dwg",
                              std::ios::binary);
            d.write(out);
            // Both corrupt-index refusal and duplicate rejection happen before commit.
            auto damaged = section;
            damaged[36] = char(0xff);
            bool rejected = false;
            try {
                detail::insert_dwg_acds_body(damaged, 0xabcdef,
                                             acis::SabWriter::write(acis::build_box({0, 0, 0}, 1, 2, 3)));
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            check(rejected, "Unknown schema cannot be silently rebuilt");
            rejected = false;
            try {
                detail::insert_dwg_acds_body(section, h, {});
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            check(rejected, "Duplicate datastore owners reject");
        }
        for (auto &[h, m] : source)
            d.clear_modeler_geometry(h);
        {
            std::ofstream out(std::string(argv[2]) + ".cleared-all.dwg", std::ios::binary);
            d.write(out);
        }
        auto h = source.begin()->first;
        d.set_modeler_geometry(h, acis::build_sphere({1, 2, 3}, 2 + h % 3));
        {
            std::ofstream out(std::string(argv[2]) + ".refilled.dwg", std::ios::binary);
            d.write(out);
        }
        std::cout << "Verified creation of a missing datastore and nine sequential modeler records\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
