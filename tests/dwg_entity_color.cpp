// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <iostream>
#include <libopencad/dwg_document.hpp>
#include <sstream>

using namespace libopencad;

static std::string bytes(const DwgDocument &d) {
    std::ostringstream out;
    d.write(out);
    return out.str();
}

int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto d = DwgDocument::read(in);
    const auto version = bytes(d).substr(0, 6);
    const Color colors[] = {Color::by_block(),      Color::from_index(17), Color::from_rgb(12, 34, 56),
                            Color::from_index(255), Color::by_layer(),     Color::from_index(128)};
    const auto line = d.objects().lines.begin()->first, circle = d.objects().circles.begin()->first,
               arc = d.objects().arcs.begin()->first;
    auto write = [&](int step) {
        std::ofstream out(std::string(argv[2]) + "." + std::to_string(step), std::ios::binary);
        d.write(out);
        out.close();
        if (!out)
            throw std::runtime_error("DWG color fixture write failed");
    };
    if (d.objects().lines.at(line).properties.color_book) {
        auto saved = d;
        auto e = d.line(line);
        auto original = bytes(d);
        d.update(e);
        if (bytes(d) != original || e.color() != Color::from_index(4))
            return 1;
        e.set_color(e.color());
        d.update(e);
        write(7);
        d = std::move(saved);
    }
    for (int step = 0; step < 6; ++step) {
        auto set = [&](auto read, auto id, Color color) {
            auto e = (d.*read)(id);
            e.set_color(color);
            d.update(e);
            const auto expected = version < "AC1018" ? Color::from_index(color.approximate_index()) : color;
            if ((d.*read)(id).color() != expected)
                throw std::runtime_error("Incorrect entity color");
            auto saved = bytes(d);
            if (version == "AC1009") {
                auto r = d.objects().records.at(id);
                bool present = saved.at(r.offset + 1) & 1;
                if (present != (expected != Color::by_layer()) ||
                    (present &&
                     static_cast<unsigned char>(saved.at(r.offset + r.color_bit / 8)) != *expected.index()))
                    throw std::runtime_error("Incorrect AC1009 color byte");
            }
            d.update(e);
            if (saved != bytes(d))
                throw std::runtime_error("Color no-op changed DWG");
        };
        set(&DwgDocument::line, line, colors[step]);
        set(&DwgDocument::circle, circle, colors[(step + 1) % 6]);
        set(&DwgDocument::arc, arc, colors[(step + 2) % 6]);
        write(step);
    }
    auto e = d.circle(circle);
    e.set_color(Color::from_index(63));
    e.set_radius(13.25);
    e.set_linetype("missing");
    auto original = bytes(d);
    try {
        d.update(e);
        return 1;
    } catch (const std::invalid_argument &) {
    }
    if (bytes(d) != original)
        return 1;
    e.set_linetype("Continuous");
    d.update(e);
    write(6);
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
