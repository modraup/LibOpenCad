// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <iostream>
#include <libopencad/dwg_document.hpp>
#include <limits>
#include <sstream>

using namespace libopencad;
namespace {
std::string bytes(const DwgDocument &drawing) {
    std::ostringstream output;
    drawing.write(output);
    return output.str();
}
void check(bool result, const char *message) {
    if (!result)
        throw std::runtime_error(message);
}
template <class Edit> void rejects(Edit edit) {
    try {
        edit();
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error("Invalid linetype edit succeeded");
}
} // namespace
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream input(argv[1], std::ios::binary);
    auto d = DwgDocument::read(input);
    const bool legacy = d.objects().symbols.empty();
    auto id = d.add_linetype("测量-Dash", {4, -1.25, 0, -0.75}, "测量虚线");
    auto other = d.add_linetype("SurveySolid", {});
    check(other == id + 1, "Unexpected handle or table slot");
    if (std::string_view(argv[1]).find("layer-boundary") != std::string_view::npos)
        check(id == 255 && other == 256, "Handle width boundary");
    d.set_layer_linetype("0", "SurveySolid");
    d.set_layer_linetype("0", "测量-DASH");
    if (legacy) {
        check(d.objects().legacy_symbol_names.at(57).at(id) == "测量-Dash", "Legacy LTYPE name");
    } else {
        check(d.objects().symbols.at(id).name == "测量-Dash", "LTYPE name");
        for (const auto &[h, s] : d.objects().symbols)
            if (s.type == 51 && s.name == "0")
                check(s.linetype == id, "Layer reference");
    }
    auto before = bytes(d);
    d.set_layer_linetype("0", "测量-Dash");
    rejects([&] { d.add_linetype("测量-dash", {}); });
    rejects([&] { d.add_linetype("ByBlock", {}); });
    rejects([&] { d.add_linetype("bad/name", {}); });
    rejects([&] { d.add_linetype("Invalid", {std::numeric_limits<double>::quiet_NaN()}); });
    rejects([&] { d.add_linetype("Oversize", std::vector<double>(legacy ? 13 : 256, 1)); });
    rejects([&] { d.set_layer_linetype("Missing", "SurveySolid"); });
    rejects([&] { d.set_layer_linetype("0", "Missing"); });
    if (legacy)
        rejects([&] { d.add_linetype("Oversize", {}, std::string(49, 'x')); });
    check(before == bytes(d), "No-op or failed edit changed the drawing");
    std::ofstream out(argv[2], std::ios::binary);
    d.write(out);
    out.close();
    return !out;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
