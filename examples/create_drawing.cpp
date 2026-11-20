// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <exception>
#include <fstream>
#include <iostream>

int main() try {
    using namespace libopencad;
    auto doc = DxfDocument::create(false, 4); // ASCII, $INSUNITS = 4 (millimeters)
    doc.add_layer("Geometry", 3);
    doc.add_layer("Annotations", 2);

    EntityOptions geometry;
    geometry.layer = "Geometry";
    doc.add_line({0, 0, 0}, {100, 50, 0}, geometry);
    doc.add_circle({50, 25, 0}, 10, geometry);

    EntityOptions note;
    note.layer = "Annotations";
    note.space = EntitySpace::Paper;
    doc.add_text("Example drawing", {10, 10, 0}, 2.5, note);

    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open("drawing.dxf", std::ios::binary);
    doc.write(output);
    output.close();
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
