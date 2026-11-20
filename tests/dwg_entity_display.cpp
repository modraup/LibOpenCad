// SPDX-License-Identifier: MPL-2.0
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
    const auto initial = bytes(doc);
    const auto version = initial.substr(0, 6);
    const auto line = doc.objects().lines.begin()->first;
    const auto circle = doc.objects().circles.begin()->first;
    const auto arc = doc.objects().arcs.begin()->first;
    const bool reserved = std::string_view(argv[1]).find("display-flags") != std::string_view::npos;
    const int weights[] = {0, 53, 211, -1, -2, -3};
    const int standard[] = {0,  5,  9,  13, 15, 18,  20,  25,  30,  35,  40,  50,
                            53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};
    for (unsigned i = 0; i < std::size(standard); ++i) {
        auto weight = LineWeight::from_value(standard[i]);
        if (weight.to_dwg_index() != i || LineWeight::from_dwg_index(i) != weight)
            throw std::runtime_error("Incorrect standard DWG lineweight");
    }
    for (int step = 0; step < 10; ++step) {
        const auto before = bytes(doc);
        auto apply = [&](auto e) {
            if (step < 3)
                e.set_invisible(true);
            else {
                e.set_line_weight(LineWeight::from_value(weights[step - 3]));
                if (step >= 6)
                    e.set_invisible(false);
            }
            doc.update(e);
        };
        if (step == 9) {
            auto e = doc.circle(circle);
            e.set_radius(8.25);
            e.set_invisible(true);
            e.set_line_weight(LineWeight::from_value(42)); // upstream maps unlisted values to Default
            doc.update(e);
        } else if (step % 3 == 0)
            apply(doc.line(line));
        else if (step % 3 == 1)
            apply(doc.circle(circle));
        else
            apply(doc.arc(arc));
        const auto saved = bytes(doc);
        if (step < 9 && (version == "AC1009" || (version < "AC1015" && step >= 3 && step < 6)) &&
            before != saved)
            throw std::runtime_error("Unsupported field changed original bytes");
        if (reserved) {
            auto r = doc.objects().records.at(line);
            DwgBitReader bits(std::string_view(saved).substr(r.body_offset, r.body_size));
            bits.seek(r.visibility_bit);
            if (bits.bit_short() != (step < 6 ? 257 : 256) || bits.bits(8) != (step < 3   ? 28
                                                                               : step < 6 ? 0
                                                                                          : 29))
                throw std::runtime_error("Reserved visibility bits or original weight slot lost");
        }
        doc.update(doc.line(line));
        doc.update(doc.circle(circle));
        doc.update(doc.arc(arc));
        if (bytes(doc) != saved)
            throw std::runtime_error("No-op changed DWG");
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
