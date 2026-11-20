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
    auto doc = DwgDocument::read(in);
    auto h = doc.objects().circles.begin()->first;
    auto circle = doc.circle(h);
    std::ostringstream initial;
    doc.write(initial);
    doc.update(circle);
    circle.set_layer("missing");
    try {
        doc.update(circle);
        return 1;
    } catch (const std::invalid_argument &) {
    }
    std::ostringstream unchanged;
    doc.write(unchanged);
    if (initial.str() != unchanged.str())
        return 1;
    auto color_edit = circle;
    color_edit.set_color(Color::from_index(2));
    try {
        doc.update(color_edit);
        return 1;
    } catch (const std::invalid_argument &) {
    }
    circle.set_layer("details");
    circle.set_center({3, -5, 0});
    circle.set_radius(1);
    doc.update(circle);
    auto arc = doc.arc(doc.objects().arcs.begin()->first);
    auto bh = doc.objects().arcs.begin()->second.properties.owner;
    if (bh &&
        (doc.objects().symbols.at(bh).name != "DETAIL" || std::stoull(arc.owner_handle(), nullptr, 16) != bh))
        return 1;
    if (arc.layer() != "DETAILS")
        return 1;
    arc.set_start_angle_radians(.5);
    auto center = arc.center();
    center.z = 9;
    arc.set_center(center);
    doc.update(arc);
    auto line = doc.line(doc.objects().lines.begin()->first);
    line.set_end({-8, 9, 2});
    doc.update(line);
    if (doc.circle(h).layer() != "DETAILS")
        return 1;
    std::ofstream out(argv[2], std::ios::binary);
    doc.write(out);
    out.close();
    return !out;
} catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
}
