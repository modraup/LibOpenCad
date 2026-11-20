// SPDX-License-Identifier: MPL-2.0
// Adapted from acadrust entities/mtext_format/{types,parser}.rs.
#include "dxf_code_page.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <libopencad/mtext_format.hpp>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace libopencad::mtext {
namespace {
std::u32string unicode(std::string_view s) { return detail::utf8_scalars(s); }
std::string utf8(std::u32string_view s) { return detail::utf8_string(s); }
std::string utf8(char32_t c) { return utf8(std::u32string_view(&c, 1)); }
std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    return first == s.npos ? std::string_view{} : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
bool digit(char32_t c) { return c >= U'0' && c <= U'9'; }
std::optional<std::int64_t> integer(std::string_view s, std::int64_t lo, std::int64_t hi) {
    if (s.empty())
        return {};
    if (s.front() == '+')
        s.remove_prefix(1);
    if (s.empty())
        return {};
    std::int64_t n = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || n < lo || n > hi)
        return {};
    return n;
}
std::optional<double> real(std::string_view s) {
    s = trim(s);
    if (s.empty())
        return {};
    std::istringstream in{std::string(s)};
    in.imbue(std::locale::classic());
    double n;
    if (!(in >> n) || in.peek() != EOF)
        return {};
    return n;
}
std::string number(double n) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, n);
    if (r.ec != std::errc{})
        throw std::runtime_error("MTEXT number conversion");
    return {buf, r.ptr};
}
std::optional<char32_t> special(char32_t c) {
    switch (c) {
    case U'd':
    case U'D':
        return U'°';
    case U'p':
    case U'P':
        return U'±';
    case U'c':
    case U'C':
        return U'Ø';
    case U'%':
        return U'%';
    default:
        return {};
    }
}
std::string escape(std::string_view s) {
    std::string r;
    for (auto c : s) {
        if (c == '\\' || c == '{' || c == '}')
            r += '\\';
        r += c;
    }
    return r;
}
std::string emit_properties(const SpanProperties &p) {
    std::string r;
    auto color = [](Color c) {
        return std::to_string(c.kind() == Color::Kind::None ? 0
                                                            : c.index().value_or(static_cast<int>(
                                                                  c.to_true_color_value().value_or(0))));
    };
    if (p.color)
        r += "\\C" + color(*p.color) + ";";
    if (p.second_color)
        r += (p.second_color->kind() == Color::Kind::None ? "" : color(*p.second_color)) + ";";
    if (p.color_rgb) {
        const auto c = *p.color_rgb;
        const auto bgr = std::uint32_t(c[0]) | (std::uint32_t(c[1]) << 8) | (std::uint32_t(c[2]) << 16);
        r += "\\c" + std::to_string(bgr) + ";";
    }
    if (p.font && !p.font->name.empty()) {
        const auto &f = *p.font;
        r += "\\f" + f.name;
        if (f.bold_explicit)
            r += f.bold ? "|b1" : "|b0";
        if (f.italic_explicit)
            r += f.italic ? "|i1" : "|i0";
        if (f.charset)
            r += "|c" + std::to_string(*f.charset);
        if (f.pitch)
            r += "|p" + std::to_string(*f.pitch);
        if (!f.style.empty())
            r += '|' + f.style;
        for (auto &extra : f.extra)
            r += '|' + extra;
        r += ';';
    }
    if (p.height)
        r += "\\H" + number(p.height->value) + (p.height->relative ? "x;" : ";");
    if (p.underline)
        r += "\\L";
    if (p.overline)
        r += "\\O";
    if (p.strikethrough)
        r += "\\K";
    if (p.line_align)
        r += "\\A" + std::to_string(*p.line_align) + ";";
    if (p.tracking)
        r += "\\T" + number(*p.tracking) + ";";
    if (p.width_factor)
        r += "\\W" + number(*p.width_factor) + ";";
    if (p.oblique_angle)
        r += "\\Q" + number(*p.oblique_angle) + ";";
    return r;
}
} // namespace
bool SpanProperties::empty() const {
    return !color && !color_rgb && !second_color && !font && !height && !underline && !overline &&
           !strikethrough && !line_align && !tracking && !width_factor && !oblique_angle;
}
bool SpanProperties::operator==(const SpanProperties &b) const {
    return std::tie(color, color_rgb, second_color, font, height, underline, overline, strikethrough,
                    line_align, tracking, width_factor, oblique_angle) ==
           std::tie(b.color, b.color_rgb, b.second_color, b.font, b.height, b.underline, b.overline,
                    b.strikethrough, b.line_align, b.tracking, b.width_factor, b.oblique_angle);
}
void SpanProperties::merge(const SpanProperties &b) {
    if (b.color)
        color = b.color;
    if (b.color_rgb)
        color_rgb = b.color_rgb;
    if (b.second_color)
        second_color = b.second_color;
    if (b.font)
        font = b.font;
    if (b.height)
        height = b.height;
    underline |= b.underline;
    overline |= b.overline;
    strikethrough |= b.strikethrough;
    if (b.line_align)
        line_align = b.line_align;
    if (b.tracking)
        tracking = b.tracking;
    if (b.width_factor)
        width_factor = b.width_factor;
    if (b.oblique_angle)
        oblique_angle = b.oblique_angle;
}
std::string Stacking::plain_text() const {
    return numerator + ((kind == Kind::Limit || kind == Kind::Decimal) ? " " : "/") + denominator;
}
double LineSpacing::line_pitch(double height, double entity_factor) const {
    return (kind == Kind::Default ? entity_factor : factor) * (5. / 3.) * height;
}
double LineSpacing::baseline_offset(double height) const {
    return (kind == Kind::Exact ? factor : 1) * height;
}
bool ParagraphProperties::empty() const {
    return !alignment && !first_line_indent && !left_margin && !right_margin && !spacing_before &&
           !spacing_after && !line_spacing && tab_stops.empty();
}
std::optional<double> ParagraphProperties::spacing_before_distance(double height) const {
    return spacing_before ? std::optional<double>(*spacing_before * height) : std::nullopt;
}
std::optional<double> ParagraphProperties::spacing_after_distance(double height) const {
    return spacing_after ? std::optional<double>(*spacing_after * height) : std::nullopt;
}
std::string Paragraph::plain_text() const {
    std::string r;
    for (const auto &s : spans)
        r += s.stacking ? s.stacking->plain_text() : s.text;
    return r;
}
void Paragraph::push_span(Span s, bool merge) {
    if (merge && !spans.empty() && !spans.back().stacking && !s.stacking &&
        spans.back().properties == s.properties)
        spans.back().text += s.text;
    else
        spans.push_back(std::move(s));
}
Document Document::from_text(std::string text) {
    Document d;
    Paragraph p;
    p.spans.push_back({std::move(text), {}, {}});
    d.paragraphs.push_back(std::move(p));
    return d;
}
std::string Document::plain_text() const {
    std::string r;
    for (std::size_t i = 0; i < paragraphs.size(); ++i) {
        if (i)
            r += '\n';
        r += paragraphs[i].plain_text();
    }
    return r;
}
std::size_t Document::span_count() const {
    std::size_t n = 0;
    for (auto &p : paragraphs)
        n += p.spans.size();
    return n;
}
bool Document::all_plain() const {
    for (auto &p : paragraphs)
        for (auto &s : p.spans)
            if (!s.plain())
                return false;
    return true;
}
std::string Document::to_mtext_string() const {
    if (paragraphs.size() == 1 && all_plain() && paragraphs[0].properties.empty()) {
        auto s = plain_text();
        return s.find_first_of("{}\\") != s.npos ? "{" + escape(s) + "}" : s;
    }
    std::string r = "{";
    for (std::size_t i = 0; i < paragraphs.size(); ++i) {
        const auto &p = paragraphs[i];
        if (i)
            r += p.starts_column ? "\\N" : "\\P";
        const auto &v = p.properties;
        if (!v.empty()) {
            r += "\\p";
            if (v.alignment) {
                r += 'q';
                r += *v.alignment;
            }
            for (auto [code, value] : {std::pair{'i', v.first_line_indent},
                                       {'l', v.left_margin},
                                       {'r', v.right_margin},
                                       {'b', v.spacing_before},
                                       {'a', v.spacing_after}})
                if (value) {
                    r += ',';
                    r += code;
                    r += number(*value);
                }
            if (v.line_spacing) {
                auto s = *v.line_spacing;
                r += s.kind == LineSpacing::Kind::Default
                         ? ",s*"
                         : (s.kind == LineSpacing::Kind::Exact ? ",se" : s.kind == LineSpacing::Kind::AtLeast ? ",sa" : ",sm") + number(s.factor);
            }
            if (!v.tab_stops.empty()) {
                r += ",t";
                for (std::size_t j = 0; j < v.tab_stops.size(); ++j) {
                    if (j)
                        r += ',';
                    const auto &t = v.tab_stops[j];
                    switch (t.kind) {
                    case TabStop::Kind::Center:
                        r += 'c';
                        break;
                    case TabStop::Kind::Right:
                        r += 'r';
                        break;
                    case TabStop::Kind::Decimal:
                        r += 'D';
                        break;
                    case TabStop::Kind::DecimalWithCharacter:
                        r += 'D';
                        r += utf8(t.character);
                        break;
                    default:
                        break;
                    }
                    r += number(t.position);
                }
            }
            r += ';';
        }
        for (const auto &span : p.spans) {
            const bool scoped = !span.properties.empty();
            if (scoped)
                r += "{" + emit_properties(span.properties);
            if (span.stacking) {
                const auto &s = *span.stacking;
                r += "\\S" + s.numerator +
                     (s.kind == Stacking::Kind::Horizontal ? "/"
                      : s.kind == Stacking::Kind::Diagonal ? "#"
                      : s.kind == Stacking::Kind::Decimal ? "~." : "^") +
                     s.denominator + ";";
            } else
                r += escape(span.text);
            if (scoped)
                r += '}';
        }
    }
    return r + "}";
}
namespace {
class Parser {
    std::u32string chars;
    std::size_t pos = 0;
    std::vector<SpanProperties> contexts{SpanProperties{}};
    std::u32string buffer;
    Document document;
    Paragraph paragraph;
    SpanProperties &props() { return contexts.back(); }
    void flush(bool merge) {
        if (buffer.empty())
            return;
        paragraph.push_span({utf8(buffer), props(), {}}, merge);
        buffer.clear();
    }
    void end() {
        paragraph.end_properties = props();
        document.paragraphs.push_back(std::move(paragraph));
        paragraph = {};
    }
    std::string value(bool relative = false) {
        auto start = pos;
        while (pos < chars.size() && chars[pos] != U';' &&
               (!relative || (chars[pos] != U'x' && chars[pos] != U'X')))
            ++pos;
        if (relative && pos < chars.size() && (chars[pos] == U'x' || chars[pos] == U'X'))
            ++pos;
        auto r = utf8(std::u32string_view(chars).substr(start, pos - start));
        if (pos < chars.size() && chars[pos] == U';')
            ++pos;
        return r;
    }
    std::optional<std::int64_t> numeric() {
        if (pos < chars.size() && chars[pos] == U';') {
            ++pos;
            return {};
        }
        auto start = pos;
        auto n = integer(value(), INT32_MIN, INT32_MAX);
        if (!n)
            pos = start;
        return n;
    }
    void parse_paragraph();
    void control();
    void percent() {
        if (pos + 1 >= chars.size() || chars[pos + 1] != U'%') {
            buffer += chars[pos++];
            return;
        }
        pos += 2;
        if (pos == chars.size()) {
            buffer += U"%%";
            return;
        }
        if (chars[pos] == U'%' && pos + 1 < chars.size() && chars[pos + 1] == U'%') {
            pos += 2;
            buffer += U'%';
            return;
        }
        auto c = chars[pos++];
        if (auto s = special(c))
            buffer += *s;
        else {
            buffer += U"%%";
            buffer += c;
        }
    }

  public:
    explicit Parser(std::string_view s) : chars(unicode(s)) {}
    Document formatted(bool merge) {
        if (chars.empty())
            return {};
        if (chars[0] == U'{') {
            pos = 1;
            contexts.emplace_back();
        }
        while (pos < chars.size()) {
            auto c = chars[pos];
            switch (c) {
            case U'\\':
                flush(merge);
                control();
                break;
            case U'{':
                contexts.push_back(props());
                ++pos;
                break;
            case U'}':
                flush(merge);
                if (contexts.size() > 1)
                    contexts.pop_back();
                ++pos;
                break;
            case U'%':
                percent();
                break;
            case U'^':
                if (pos + 1 < chars.size() && chars[pos + 1] == U'I') {
                    buffer += U'\t';
                    pos += 2;
                } else if (pos + 1 < chars.size() && chars[pos + 1] == U'J') {
                    pos += 2;
                    flush(merge);
                    end();
                } else if (pos + 1 < chars.size() && chars[pos + 1] == U'M')
                    pos += 2;
                else {
                    buffer += c;
                    ++pos;
                }
                break;
            case U'\n':
                ++pos;
                flush(merge);
                end();
                break;
            default:
                buffer += (c < U' ' && c != U'\t') ? U' ' : c;
                ++pos;
                break;
            }
        }
        flush(merge);
        if (!paragraph.spans.empty())
            end();
        return std::move(document);
    }
    Document plain() {
        if (chars.empty())
            return {};
        bool underline = false, overline = false;
        auto flush_plain = [&] {
            props() = {};
            props().underline = underline;
            props().overline = overline;
            flush(false);
        };
        while (pos < chars.size()) {
            auto c = chars[pos];
            if (c == U'%' && pos + 1 < chars.size() && chars[pos + 1] == U'%') {
                if (pos + 3 < chars.size() && chars[pos + 2] == U'%' && chars[pos + 3] == U'%') {
                    buffer += U'%';
                    pos += 4;
                    continue;
                }
                if (pos + 2 < chars.size()) {
                    auto code = chars[pos + 2];
                    if (code == U'u' || code == U'U') {
                        pos += 3;
                        flush_plain();
                        underline = !underline;
                        continue;
                    }
                    if (code == U'o' || code == U'O') {
                        pos += 3;
                        flush_plain();
                        overline = !overline;
                        continue;
                    }
                    if (code != U'%')
                        if (auto s = special(code)) {
                            pos += 3;
                            buffer += *s;
                            continue;
                        }
                    if (digit(code)) {
                        pos += 2;
                        unsigned n = 0, count = 0;
                        while (count++ < 3 && pos < chars.size() && digit(chars[pos]))
                            n = n * 10 + (chars[pos++] - U'0');
                        buffer += static_cast<char32_t>(n);
                        continue;
                    }
                }
                pos += 2;
                buffer += U"%%";
                continue;
            }
            if (c == U'\\' && pos + 1 < chars.size() && (chars[pos + 1] == U'P' || chars[pos + 1] == U'p')) {
                flush_plain();
                end();
                pos += 2;
                continue;
            }
            buffer += c;
            ++pos;
        }
        flush_plain();
        paragraph.end_properties = {};
        document.paragraphs.push_back(std::move(paragraph));
        return std::move(document);
    }
};
void Parser::control() {
    ++pos;
    if (pos >= chars.size()) {
        buffer += U'\\';
        return;
    }
    auto code = chars[pos++];
    switch (code) {
    case U'\\':
    case U'{':
    case U'}':
    case U';':
        buffer += code;
        return;
    case U'~':
        buffer += U'\u00a0';
        return;
    case U'U':
        if (pos < chars.size() && chars[pos] == U'+') {
            ++pos;
            auto start = pos;
            char32_t n = 0;
            bool valid = pos + 4 <= chars.size();
            for (int i = 0; valid && i < 4; ++i) {
                auto c = chars[pos + i];
                if (digit(c))
                    n = n * 16 + c - U'0';
                else if (c >= U'a' && c <= U'f')
                    n = n * 16 + c - U'a' + 10;
                else if (c >= U'A' && c <= U'F')
                    n = n * 16 + c - U'A' + 10;
                else
                    valid = false;
            }
            if (valid && !(n >= 0xd800 && n <= 0xdfff)) {
                buffer += n;
                pos = start + 4;
            } else
                buffer += U"\\U+";
            return;
        }
        break;
    case U'M':
        if (pos < chars.size() && chars[pos] == U'+' && pos + 5 < chars.size()) {
            auto token = utf8(std::u32string_view(chars).substr(pos - 2, 8));
            auto decoded = detail::from_legacy(token, "CP1252");
            if (decoded != token) {
                buffer += unicode(decoded);
                pos += 6;
                return;
            }
        }
        break;
    case U'P':
        end();
        return;
    case U'N':
        end();
        paragraph.starts_column = true;
        return;
    case U'p':
        parse_paragraph();
        return;
    case U'X':
        return;
    case U'L':
        props().underline = true;
        return;
    case U'l':
        props().underline = false;
        return;
    case U'O':
        props().overline = true;
        return;
    case U'o':
        props().overline = false;
        return;
    case U'K':
        props().strikethrough = true;
        return;
    case U'k':
        props().strikethrough = false;
        return;
    case U'C':
        props().color.reset();
        props().second_color.reset();
        props().color_rgb.reset();
        if (auto n = numeric()) {
            if (*n >= 0 && *n <= 256)
                props().color = Color::from_index(static_cast<std::int16_t>(*n));
            if (auto m = numeric())
                if (*m >= 0 && *m <= 256)
                    props().second_color = Color::from_index(static_cast<std::int16_t>(*m));
        }
        return;
    case U'c': {
        auto token = value();
        if (auto n = integer(trim(token), 0, UINT32_MAX)) {
            auto rgb = static_cast<std::uint32_t>(*n);
            props().color_rgb =
                Color::Rgb{static_cast<std::uint8_t>(rgb), static_cast<std::uint8_t>(rgb >> 8),
                           static_cast<std::uint8_t>(rgb >> 16)};
            props().color.reset();
        }
        return;
    }
    case U'f':
    case U'F': {
        bool shx = code == U'F' && pos < chars.size() && chars[pos] == U'N';
        if (shx)
            ++pos;
        auto token = value();
        Font font;
        if (shx) {
            font.name = trim(token);
            if (!font.name.empty())
                props().font = font;
            return;
        }
        std::size_t start = 0;
        bool first = true;
        while (start <= token.size()) {
            auto end = token.find_first_of(",|", start);
            if (end == token.npos)
                end = token.size();
            auto part = trim(std::string_view(token).substr(start, end - start));
            if (first) {
                font.name = part;
                first = false;
            } else if (part == "b0" || part == "b1") {
                font.bold_explicit = true;
                font.bold = part == "b1";
            } else if (part == "i0" || part == "i1") {
                font.italic_explicit = true;
                font.italic = part == "i1";
            } else if (!part.empty()) {
                bool parsed = false;
                if (part.front() == 'c') {
                    if (auto n = integer(trim(part.substr(1)), 0, 65535)) {
                        font.charset = static_cast<std::uint16_t>(*n);
                        parsed = true;
                    }
                } else if (part.front() == 'p') {
                    if (auto n = integer(trim(part.substr(1)), 0, 255)) {
                        font.pitch = static_cast<std::uint8_t>(*n);
                        parsed = true;
                    }
                }
                if (!parsed)
                    font.extra.emplace_back(part);
            }
            if (end == token.size())
                break;
            start = end + 1;
        }
        if (!font.name.empty())
            props().font = std::move(font);
        return;
    }
    case U'H':
    case U'W':
    case U'w':
    case U'T': {
        auto token = value(true);
        bool relative = !token.empty() && (token.back() == 'x' || token.back() == 'X');
        if (relative)
            token.pop_back();
        if (auto n = real(token)) {
            if (code == U'H') {
                if (relative) {
                    auto h = props().height.value_or(Scalar{});
                    h.value *= std::abs(*n);
                    props().height = h;
                } else
                    props().height = Scalar{std::abs(*n), false};
            } else if (code == U'T')
                props().tracking = relative ? props().tracking.value_or(1) * std::abs(*n) : *n;
            else
                props().width_factor =
                    relative ? props().width_factor.value_or(1) * std::abs(*n) : std::abs(*n);
        }
        return;
    }
    case U'Q':
        if (auto n = real(value()))
            props().oblique_angle = *n;
        return;
    case U'A': {
        auto n = integer(value(), 0, 255).value_or(0);
        props().line_align = static_cast<std::uint8_t>(n == 1 || n == 2 ? n : 0);
        return;
    }
    case U'S':
    case U's': {
        auto expression = value();
        Stacking s;
        auto at = expression.find_first_of("/#^~");
        if (at == expression.npos)
            s.numerator = expression;
        else {
            s.numerator = expression.substr(0, at);
            s.kind = expression[at] == '~' ? Stacking::Kind::Decimal
                     : expression[at] == '^'   ? Stacking::Kind::Limit
                     : expression[at] == '#' ? Stacking::Kind::Diagonal
                                             : Stacking::Kind::Horizontal;
            auto first = at + 1;
            if (expression[at] == '^' && first < expression.size() && expression[first] == ' ')
                ++first;
            if (expression[at] == '~' && first < expression.size() && (expression[first] == '.' || expression[first] == ','))
                ++first;
            s.denominator = expression.substr(first);
        }
        paragraph.spans.push_back({"", props(), s});
        return;
    }
    case U'b': {
        auto token = value();
        if (auto n = integer(trim(token), 0, 255))
            props().strikethrough = *n == 1;
        return;
    }
    case U't':
    case U'B':
        (void)value();
        return;
    }
    buffer += U'\\';
    buffer += code;
}
void Parser::parse_paragraph() {
    auto start = pos;
    auto token = value();
    if (trim(token).empty()) {
        pos = start;
        end();
        props() = {};
        return;
    }
    auto expr = unicode(token);
    auto &p = paragraph.properties;
    std::size_t i = 0;
    auto read_number = [&](std::size_t &index) {
        auto start = index;
        while (index < expr.size() && (digit(expr[index]) || expr[index] == U'.' || expr[index] == U'-'))
            ++index;
        return real(utf8(std::u32string_view(expr).substr(start, index - start)));
    };
    while (i < expr.size()) {
        auto c = expr[i++];
        if (i == expr.size())
            break;
        if (c == U'q') {
            auto a = expr[i++];
            if (a >= U'A' && a <= U'Z')
                a += U'a' - U'A';
            p.alignment =
                (a == U'l' || a == U'r' || a == U'c' || a == U'j' || a == U'd') ? static_cast<char>(a) : '*';
        } else if (c == U'i' || c == U'b' || c == U'a' || ((c == U'l' || c == U'r') && digit(expr[i]))) {
            auto n = read_number(i);
            if (n) {
                switch (c) {
                case U'i':
                    p.first_line_indent = n;
                    break;
                case U'l':
                    p.left_margin = n;
                    break;
                case U'r':
                    p.right_margin = n;
                    break;
                case U'b':
                    p.spacing_before = n;
                    break;
                case U'a':
                    p.spacing_after = n;
                    break;
                }
            }
        } else if (c == U's') {
            auto sub = expr[i];
            if (sub == U'*') {
                ++i;
                p.line_spacing = LineSpacing{};
            } else if (sub == U'e' || sub == U'm' || sub == U'a') {
                ++i;
                if (auto n = read_number(i))
                    p.line_spacing =
                        LineSpacing{sub == U'e' ? LineSpacing::Kind::Exact : sub == U'a' ? LineSpacing::Kind::AtLeast : LineSpacing::Kind::Multiple, *n};
            }
        } else if (c == U't' && i < expr.size() && expr[i] == U'z') {
            p.tab_stops = {TabStop{}};
            ++i;
        } else if (c == U't') {
            auto start = i;
            while (i < expr.size() && (digit(expr[i]) || std::u32string_view(U".-,cCrRdD").find(expr[i]) !=
                                                             std::u32string_view::npos))
                ++i;
            auto segment = utf8(std::u32string_view(expr).substr(start, i - start));
            std::vector<std::string> parts;
            std::size_t first = 0;
            while (first <= segment.size()) {
                auto last = segment.find(',', first);
                if (last == segment.npos)
                    last = segment.size();
                parts.push_back(segment.substr(first, last - first));
                if (last == segment.size())
                    break;
                first = last + 1;
            }
            for (std::size_t j = 0; j < parts.size(); ++j) {
                auto part = std::string_view(parts[j]);
                TabStop stop;
                if (part.empty())
                    continue;
                if (part.front() == 'c' || part.front() == 'C') {
                    stop.kind = TabStop::Kind::Center;
                    part.remove_prefix(1);
                } else if (part.front() == 'r' || part.front() == 'R') {
                    stop.kind = TabStop::Kind::Right;
                    part.remove_prefix(1);
                } else if (part.front() == 'd' || part.front() == 'D') {
                    stop.kind = TabStop::Kind::Decimal;
                    part.remove_prefix(1);
                    if (part.empty() && j + 1 < parts.size() && real(parts[j + 1])) {
                        stop.kind = TabStop::Kind::DecimalWithCharacter;
                        stop.character = U',';
                        part = parts[++j];
                    } else if (!part.empty() && part.front() == '.') {
                        stop.kind = TabStop::Kind::DecimalWithCharacter;
                        stop.character = U'.';
                        part.remove_prefix(1);
                    }
                }
                if (auto n = real(part)) {
                    stop.position = *n;
                    p.tab_stops.push_back(stop);
                }
            }
        }
    }
}
} // namespace
Document parse(std::string_view input, bool merge_spans) { return Parser(input).formatted(merge_spans); }
Document parse_plain_text(std::string_view input) { return Parser(input).plain(); }
void scale_inline_heights(std::string &contents, double factor) {
    if (std::abs(factor - 1) < 1e-12 || contents.find("\\H") == std::string::npos)
        return;
    std::string out;
    out.reserve(contents.size());
    std::string_view rest = contents;
    while (true) {
        const auto i = rest.find('\\');
        if (i == std::string_view::npos) {
            out.append(rest);
            break;
        }
        out.append(rest.substr(0, i));
        rest.remove_prefix(i);
        const auto next = rest.size() > 1 ? rest.substr(1) : std::string_view{};
        if (!next.empty() && next.front() == 'H') {
            const auto end = rest.find(';');
            if (end != std::string_view::npos) {
                const auto value = rest.substr(2, end - 2);
                if (value.empty() || (value.back() != 'x' && value.back() != 'X')) {
                    if (auto h = real(value)) {
                        out += "\\H";
                        out += number(*h * factor);
                        out += ';';
                        rest.remove_prefix(end + 1);
                        continue;
                    }
                }
            }
        }
        unsigned char lead = next.empty() ? 0 : static_cast<unsigned char>(next.front());
        std::size_t n = next.empty() ? 0 : lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        n = std::min(n, next.size());
        out.append(rest.substr(0, 1 + n));
        rest.remove_prefix(1 + n);
    }
    contents = std::move(out);
}
} // namespace libopencad::mtext
