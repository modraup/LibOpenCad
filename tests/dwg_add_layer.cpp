// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <iostream>
#include <libopencad/dwg_document.hpp>
#include <sstream>
using namespace libopencad;
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto d = DwgDocument::read(in);
    auto circle = d.objects().circles.begin()->first;
    auto count = d.objects().records.size();
    bool early = d.objects().symbols.empty();
    auto h = d.add_layer("测量-A", 3);
    auto h2 = d.add_layer("Review", 5);
    if (h2 != h + 1)
        return 1;
    if (std::string_view(argv[1]).find("layer-boundary") != std::string_view::npos && (h != 255 || h2 != 256))
        return 1;
    if (early) {
        if (d.objects().legacy_symbol_names.at(51).at(h) != "测量-A")
            return 1;
    } else if (d.objects().symbols.at(h).name != "测量-A" || d.objects().records.size() != count + 2)
        return 1;
    std::ostringstream before, after;
    d.write(before);
    try {
        d.add_layer("测量-a");
        return 1;
    } catch (const std::invalid_argument &) {
    }
    try {
        d.add_layer("bad/name");
        return 1;
    } catch (const std::invalid_argument &) {
    }
    try {
        d.add_layer("BadColor", 256);
        return 1;
    } catch (const std::invalid_argument &) {
    }
    if (early) {
        try {
            d.add_layer(std::string(33, 'L'));
            return 1;
        } catch (const std::invalid_argument &) {
        }
    }
    d.write(after);
    if (before.str() != after.str())
        return 1;
    auto e = d.circle(circle);
    e.set_layer("测量-a");
    d.update(e);
    std::ofstream out(argv[2], std::ios::binary);
    d.write(out);
    out.close();
    return !out;
} catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
}
