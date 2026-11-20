// SPDX-License-Identifier: MPL-2.0
#include <fstream>
#include <iostream>
#include <libopencad/mtext_format.hpp>
#include <stdexcept>
namespace {
using namespace libopencad::mtext;
std::string hex(std::string_view s) {
    std::string r;
    for (unsigned char c : s) {
        r += "0123456789abcdef"[c >> 4];
        r += "0123456789abcdef"[c & 15];
    }
    return r;
}
std::string unhex(std::string_view s) {
    std::string r;
    for (std::size_t i = 0; i < s.size(); i += 2)
        r += static_cast<char>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return r;
}
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void boundaries() {
    auto fresh = parse("\\psa1.5,t2,4;A\\ptz;B\\S12~.34;");
    auto &new_paragraph = fresh.paragraphs[0];
    check(new_paragraph.properties.line_spacing->kind == LineSpacing::Kind::AtLeast, "at-least spacing kind");
    check(new_paragraph.properties.line_spacing->line_pitch(2) == 5 && new_paragraph.properties.line_spacing->baseline_offset(2) == 2,
          "at-least spacing measurements");
    check(new_paragraph.properties.tab_stops.size() == 1 && new_paragraph.properties.tab_stops[0].position == 0, "tz clears inherited tabs");
    check(new_paragraph.spans.back().stacking->kind == Stacking::Kind::Decimal && new_paragraph.spans.back().stacking->denominator == "34",
          "decimal stack separator");
    check(fresh.plain_text() == "AB12 34", "decimal plain text");
    auto d = parse("\\H5;\\H2x;A{\\H.5x;B}C");
    check(d.span_count() == 3, "format scoped spans");
    check(d.paragraphs[0].spans[0].properties.height == std::optional<Scalar>({10, false}),
          "absolute height compounded");
    check(d.paragraphs[0].spans[1].properties.height == std::optional<Scalar>({5, false}), "scoped height");
    auto p = parse("\\pqc,b2,a3,se2,t2,D.4,D,6;Text").paragraphs[0].properties;
    check(p.spacing_before_distance(5) == 10 && p.spacing_after_distance(5) == 15, "paragraph spacing units");
    check(p.line_spacing->baseline_offset(5) == 10 && p.line_spacing->line_pitch(3) == 10,
          "line spacing units");
    check(p.tab_stops.size() == 3 && p.tab_stops[1].position == 4 && p.tab_stops[2].character == U',',
          "decimal tabs");
    auto rgb = parse("\\c255;red").paragraphs[0].spans[0].properties.color_rgb;
    check(rgb == std::optional<libopencad::Color::Rgb>({255, 0, 0}), "inline BGR");
    auto a = SpanProperties{}, b = SpanProperties{};
    a.underline = true;
    b.overline = true;
    b.height = Scalar{2, true};
    a.merge(b);
    check(a.underline && a.overline && a.height == b.height, "property merge");
    auto t = parse_plain_text("%%uunder%%u \\C1;literal");
    check(t.paragraphs[0].spans[0].properties.underline && t.plain_text() == "under \\C1;literal",
          "TEXT differs from MTEXT");
    auto scale = [](std::string s, double f) {
        scale_inline_heights(s, f);
        return s;
    };
    check(scale(R"(A\H2;B\H2x;C\H2X;D\\H2;E\H 2.5 ;F\H2e1;G\P中\H3;)", 2) ==
              R"(A\H4;B\H2x;C\H2X;D\\H2;E\H5;F\H40;G\P中\H6;)",
          "absolute inline heights scale; relative and escaped codes do not");
    check(scale("\\H2;", 1) == "\\H2;" && scale("plain", 2) == "plain" && scale("\\H2", 2) == "\\H2" &&
              scale("\\Hjunk;", 2) == "\\Hjunk;" && scale("\\H;", 2) == "\\H;",
          "identity, missing codes, and malformed heights stay unchanged");
    check(scale("\\H2;", 0.5) == "\\H1;", "half-scale absolute height");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 3, "input output");
        std::ifstream in(argv[1]);
        std::ofstream out(argv[2]);
        in.exceptions(std::ios::badbit);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        std::string line;
        boundaries();
        while (std::getline(in, line)) {
            auto input = unhex(line);
            for (int mode = 0; mode < 3; ++mode) {
                auto d = mode == 2 ? parse_plain_text(input) : parse(input, mode == 1);
                out << mode << '\t' << d.paragraphs.size() << '\t' << d.span_count() << '\t' << d.all_plain()
                    << '\t' << hex(d.plain_text()) << '\t' << hex(d.to_mtext_string());
                for (auto &p : d.paragraphs) {
                    auto end = Document::from_text("END");
                    end.paragraphs[0].spans[0].properties = p.end_properties;
                    out << '\t' << hex(end.to_mtext_string());
                }
                out << '\n';
            }
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
