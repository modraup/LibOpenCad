// SPDX-License-Identifier: MPL-2.0
// Port of acadrust entities/mtext_format. Formatting values use drawing units
// unless described as factors; oblique angles are degrees, as in inline MTEXT.
#pragma once
#include "color.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
namespace libopencad::mtext {
struct Font {
    std::string name, style;
    bool bold = false, italic = false, bold_explicit = false, italic_explicit = false;
    std::optional<std::uint16_t> charset;
    std::optional<std::uint8_t> pitch;
    std::vector<std::string> extra;
    bool operator==(const Font &b) const {
        return std::tie(name, style, bold, italic, bold_explicit, italic_explicit, charset, pitch, extra) ==
               std::tie(b.name, b.style, b.bold, b.italic, b.bold_explicit, b.italic_explicit, b.charset,
                        b.pitch, b.extra);
    }
};
struct Scalar {
    double value = 1;
    bool relative = true;
    bool operator==(const Scalar &b) const { return value == b.value && relative == b.relative; }
};
struct SpanProperties {
    std::optional<Color> color, second_color;
    std::optional<Color::Rgb> color_rgb;
    std::optional<Font> font;
    std::optional<Scalar> height;
    bool underline = false, overline = false, strikethrough = false;
    std::optional<std::uint8_t> line_align; // 0 bottom, 1 middle, 2 top.
    std::optional<double> tracking, width_factor, oblique_angle;
    bool empty() const;
    void merge(const SpanProperties &other);
    bool operator==(const SpanProperties &b) const;
};
struct Stacking {
    std::string numerator, denominator;
    enum class Kind { Horizontal, Diagonal, Limit, Decimal } kind = Kind::Horizontal;
    std::string plain_text() const;
};
struct Span {
    std::string text;
    SpanProperties properties;
    std::optional<Stacking> stacking;
    bool plain() const { return !stacking && properties.empty(); }
};
struct LineSpacing {
    enum class Kind { Default, Exact, Multiple, AtLeast } kind = Kind::Default;
    double factor = 1;
    double line_pitch(double height, double entity_factor = 1) const;
    double baseline_offset(double height) const;
};
struct TabStop {
    enum class Kind { Left, Center, Right, Decimal, DecimalWithCharacter } kind = Kind::Left;
    double position = 0;
    char32_t character = U'.';
};
struct ParagraphProperties {
    // '*' attachment default, 'l' left, 'r' right, 'c' center, 'j' justified, 'd' distributed.
    std::optional<char> alignment;
    std::optional<double> first_line_indent, left_margin, right_margin, spacing_before, spacing_after;
    std::optional<LineSpacing> line_spacing;
    std::vector<TabStop> tab_stops;
    bool empty() const;
    std::optional<double> spacing_before_distance(double height) const;
    std::optional<double> spacing_after_distance(double height) const;
};
struct Paragraph {
    ParagraphProperties properties;
    std::vector<Span> spans;
    bool starts_column = false;
    SpanProperties end_properties;
    std::string plain_text() const;
    void push_span(Span span, bool merge = false);
};
struct Document {
    std::vector<Paragraph> paragraphs;
    static Document from_text(std::string text);
    std::string plain_text() const;
    std::size_t span_count() const;
    bool all_plain() const;
    // Matches upstream serialization, including its omission of end_properties.
    std::string to_mtext_string() const;
};
Document parse(std::string_view input, bool merge_spans = true);
// TEXT/ATTRIB/ATTDEF: only %% codes and backslash P/p are interpreted.
Document parse_plain_text(std::string_view input);
// Multiplies every absolute `\H<h>;` height by `factor`. Relative `\H<k>x;`
// codes already follow the entity height and are left unchanged. Matches
// acadrust `entities/modification/transform.rs` `scale_inline_heights`.
void scale_inline_heights(std::string& contents, double factor);
} // namespace libopencad::mtext
