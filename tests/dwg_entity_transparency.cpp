// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <iostream>
#include <libopencad/dwg_document.hpp>
#include <sstream>
#include <type_traits>

using namespace libopencad;
static std::string bytes(const DwgDocument &doc) {
    std::ostringstream out;
    doc.write(out);
    return out.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto doc = DwgDocument::read(in);
    const auto original = bytes(doc);
    const bool modern = original.substr(0, 6) >= "AC1018";
    const auto line = doc.objects().lines.begin()->first;
    const auto circle = doc.objects().circles.begin()->first;
    const auto arc = doc.objects().arcs.begin()->first;
    const Transparency values[] = {Transparency::by_block(), Transparency::explicit_alpha(0),
                                   Transparency::explicit_alpha(255), Transparency::explicit_alpha(71),
                                   Transparency::by_layer()};
    auto check = [&](auto getter, const auto &records, auto handle) {
        const auto e = (doc.*getter)(handle);
        const auto &p = records.at(handle).properties;
        if (e.transparency() != Transparency::from_alpha_value(p.transparency))
            throw std::runtime_error("Typed getter lost source transparency");
        if (modern) {
            if (p.transparency != e.transparency().to_alpha_value())
                throw std::runtime_error("Packed alpha mismatch");
        }
    };
    check(&DwgDocument::line, doc.objects().lines, line);
    check(&DwgDocument::circle, doc.objects().circles, circle);
    check(&DwgDocument::arc, doc.objects().arcs, arc);
    for (int step = 0; step < 16; ++step) {
        auto apply = [&](auto getter, auto handle) {
            auto e = (doc.*getter)(handle);
            const auto value = step == 15 ? Transparency::explicit_alpha(128) : values[step / 3];
            e.set_transparency(value);
            if (step == 15) {
                e.set_color(Color::from_index(3));
                e.set_linetype_scale(2.25);
                if constexpr (std::is_same_v<decltype(e), DxfCircle>)
                    e.set_radius(9.75);
            }
            doc.update(e);
            if ((doc.*getter)(handle).transparency() != (modern ? value : Transparency::by_layer()))
                throw std::runtime_error("Incorrect saved transparency");
            auto saved = bytes(doc);
            doc.update(e);
            if (saved != bytes(doc))
                throw std::runtime_error("Repeated update changed DWG bytes");
        };
        if (step % 3 == 0 && step != 15)
            apply(&DwgDocument::line, line);
        else if (step % 3 == 1 || step == 15)
            apply(&DwgDocument::circle, circle);
        else
            apply(&DwgDocument::arc, arc);
        if (!modern && step < 15 && bytes(doc) != original)
            throw std::runtime_error("Unsupported transparency changed old DWG");
        check(&DwgDocument::line, doc.objects().lines, line);
        check(&DwgDocument::circle, doc.objects().circles, circle);
        check(&DwgDocument::arc, doc.objects().arcs, arc);
        std::ofstream out(std::string(argv[2]) + "." + std::to_string(step), std::ios::binary);
        doc.write(out);
        out.close();
        if (!out)
            return 1;
    }
    auto before = bytes(doc);
    auto e = doc.circle(circle);
    e.set_transparency(Transparency::explicit_alpha(201));
    e.set_color(Color::from_index(9));
    e.set_layer("missing-transparency-test");
    try {
        doc.update(e);
        return 1;
    } catch (const std::invalid_argument &) {
    }
    if (before != bytes(doc))
        throw std::runtime_error("Failed combined update changed source");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
