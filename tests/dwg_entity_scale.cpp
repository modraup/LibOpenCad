// SPDX-License-Identifier: MPL-2.0
#include <cmath>
#include <fstream>
#include <iostream>
#include <libopencad/dwg_document.hpp>
#include <sstream>

using namespace libopencad;
static std::string bytes(const DwgDocument &doc) {
    std::ostringstream stream;
    doc.write(stream);
    return stream.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto doc = DwgDocument::read(in);
    const auto original = bytes(doc);
    const bool legacy = original.compare(0, 6, "AC1009") == 0;
    const auto line = doc.objects().lines.begin()->first;
    const auto circle = doc.objects().circles.begin()->first;
    const auto arc = doc.objects().arcs.begin()->first;
    const double targets[] = {std::nextafter(1., 2.), 0., -0.375};
    auto apply = [&](auto getter, auto handle, double scale) {
        auto e = (doc.*getter)(handle);
        e.set_linetype_scale(scale);
        doc.update(e);
        if ((doc.*getter)(handle).linetype_scale() != (legacy ? 1. : scale))
            throw std::runtime_error("Incorrect entity scale after update");
        auto saved = bytes(doc);
        doc.update(e);
        if (bytes(doc) != saved)
            throw std::runtime_error("Repeated scale edit changed DWG");
    };
    auto input_scale = [&](auto getter, const auto &records, auto handle) {
        if ((doc.*getter)(handle).linetype_scale() != records.at(handle).properties.linetype_scale)
            throw std::runtime_error("Typed getter lost original DWG scale");
    };
    input_scale(&DwgDocument::line, doc.objects().lines, line);
    input_scale(&DwgDocument::circle, doc.objects().circles, circle);
    input_scale(&DwgDocument::arc, doc.objects().arcs, arc);
    for (int step = 0; step < 7; ++step) {
        const auto scale = step < 3 ? targets[step] : 1.;
        if (step == 6) {
            auto e = doc.circle(circle);
            e.set_radius(12.25);
            e.set_linetype_scale(3.75);
            e.set_invisible(true);
            e.set_line_weight(LineWeight::from_value(53));
            doc.update(e);
        } else if (step % 3 == 0)
            apply(&DwgDocument::line, line, scale);
        else if (step % 3 == 1)
            apply(&DwgDocument::circle, circle, scale);
        else
            apply(&DwgDocument::arc, arc, scale);
        if (legacy && step < 6 && original != bytes(doc))
            throw std::runtime_error("AC1009 scale-only edit changed file bytes");
        std::ofstream out(std::string(argv[2]) + "." + std::to_string(step), std::ios::binary);
        doc.write(out);
        out.close();
        if (!out)
            return 1;
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
