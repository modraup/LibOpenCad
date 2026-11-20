// SPDX-License-Identifier: MPL-2.0
#include "dwg_sat_internal.hpp"
#include <charconv>
#include <stdexcept>
#include <vector>
namespace libopencad::detail {
namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
void ascii(std::string_view s) {
    for (unsigned char c : s)
        if (c > 126 || (c < 32 && c != '\t' && c != '\r' && c != '\n'))
            throw std::invalid_argument("DWG SAT requires ASCII text; use SAB for Unicode or binary strings");
}
std::vector<std::string> strings(const acis::SatDocument &d) {
    std::vector<std::string> values;
    for (const auto &r : d.records)
        for (std::size_t i = 0; i < r.tokens.size(); ++i) {
            // SAT 400's edge schema omits the modern cached-curve classification
            // (the builder stores its default "unknown" as a string token).
            if (d.header.version.major < 7 && acis::base_entity_type(r.entity_type) == "edge" && i >= 8)
                continue;
            if (auto value = r.tokens[i].as_string())
                values.emplace_back(*value);
        }
    return values;
}
} // namespace
std::string dwg_sat_cipher(std::string_view text) {
    ascii(text);
    std::string out(text);
    for (char &c : out) {
        auto b = static_cast<unsigned char>(c);
        if (b >= 33 && b <= 126)
            c = char(159 - b);
    }
    return out;
}
std::string dwg_sat_transport(std::string_view text, bool crlf) {
    ascii(text);
    if (text.find_first_not_of(" \t\r\n") == text.npos)
        return {};
    // Unprefixed pre-v700 strings have schema-dependent lengths. Do not rewrite
    // any of their payload bytes. For modern SAT, skip counted strings verbatim.
    unsigned version = 0;
    auto first = text.find_first_not_of(" \t\r\n");
    auto parsed = std::from_chars(text.data() + first, text.data() + text.size(), version);
    bool modern = parsed.ec == std::errc{} && version >= 700;
    std::size_t at = 0;
    std::string out;
    bool record_start = true;
    // Header lines are not records (the product header uses unprefixed lengths).
    if (parsed.ec == std::errc{}) {
        auto line = text.find('\n', first);
        if (line != text.npos) {
            auto next = text.find('\n', line + 1);
            auto product =
                text.substr(line + 1, next == text.npos ? text.size() - line - 1 : next - line - 1);
            unsigned headers = product.find('$') == product.npos ? 3 : 1;
            for (unsigned i = 0; i < headers && at < text.size(); ++i) {
                auto end = text.find('\n', at);
                if (end == text.npos)
                    end = text.size();
                auto length = end - at;
                if (length && text[at + length - 1] == '\r')
                    --length;
                out.append(text.substr(at, length));
                if (end < text.size())
                    out += crlf ? "\r\n" : "\n";
                at = end == text.size() ? end : end + 1;
            }
        }
    }
    while (at < text.size()) {
        if (record_start && !space(text[at])) {
            bool end_marker = false;
            for (std::string_view marker : {"End-of-ACIS-data", "End-of-ASM-data"}) {
                if (text.substr(at, marker.size()) != marker)
                    continue;
                auto tail = text.substr(at + marker.size());
                if (!tail.empty() && !space(tail.front()) && tail.front() != '#')
                    continue;
                if (tail.find_first_not_of(" \t\r\n#") == tail.npos)
                    end_marker = true;
            }
            if (end_marker)
                break;
        }
        if (modern && text[at] == '@' && (at == 0 || space(text[at - 1]))) {
            std::size_t count = 0;
            auto number = std::from_chars(text.data() + at + 1, text.data() + text.size(), count);
            auto begin = std::size_t(number.ptr - text.data());
            if (number.ec != std::errc{} || begin >= text.size() || !space(text[begin]) ||
                count > text.size() - begin - 1)
                throw std::invalid_argument("Invalid DWG SAT counted string");
            auto end = begin + 1 + count;
            out.append(text.substr(at, end - at));
            at = end;
            record_start = false;
            continue;
        }
        char c = text[at++];
        if (modern && (c == '\r' || c == '\n')) {
            if (c == '\r' && at < text.size() && text[at] == '\n')
                ++at;
            out += crlf ? "\r\n" : "\n";
        } else
            out += c;
        if (c == '#')
            record_start = true;
        else if (!space(c))
            record_start = false;
    }
    return out;
}
std::string dwg_sat_from_document(const acis::SatDocument &document, unsigned dwg_version) {
    auto d = document;
    if (dwg_version <= 1015 && d.header.version.number() == 700)
        d.header.version = acis::SatVersion::from_number(400);
    for (auto *s : {&d.header.product_id, &d.header.product_version, &d.header.date})
        if (s->find_first_of("\r\n") != s->npos)
            throw std::invalid_argument("Multiline SAT product headers are not supported");
    auto text = d.to_sat_string();
    // Older schemas do not describe arbitrary string fields. Refuse a lossy
    // implicit downgrade instead of accepting a truncated or retyped attribute.
    if (!strings(d).empty() && d.header.version.major < 7) {
        auto decoded = acis::SatDocument::parse(text);
        if (decoded.records.size() != d.records.size() || strings(decoded) != strings(d))
            throw std::invalid_argument("SAT strings cannot be represented losslessly in the legacy schema");
    }
    return dwg_sat_cipher(dwg_sat_transport(text, true));
}
} // namespace libopencad::detail
