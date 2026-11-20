// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/io/dxf/code_page.rs (page selection and CIF/MIF transport).
#include "dxf_code_page.hpp"

#include <algorithm>
#include <utility>
#include <cerrno>
#include <charconv>
#include <iconv.h>
#include <optional>
#include <stdexcept>

namespace libopencad::detail {
namespace {
#include "symbol_name_data.inc"

struct Alias { std::string_view dxf; const char* codec; };
constexpr Alias aliases[] = {
    {"ansi_936", "GBK"}, {"gb2312", "GBK"}, {"gbk", "GBK"}, {"cp936", "GBK"},
    {"big5", "BIG5"}, {"ansi_950", "BIG5"}, {"dos950", "BIG5"},
    {"korean", "CP949"}, {"ansi_949", "CP949"}, {"johab", "JOHAB"},
    {"ansi_932", "CP932"}, {"dos932", "CP932"}, {"mac-roman", "MACINTOSH"},
    {"ansi_874", "CP874"}, {"koi8-r", "KOI8-R"}, {"koi8-u", "KOI8-U"},
    {"ansi_1250", "CP1250"}, {"ansi_1251", "CP1251"}, {"ansi_1252", "CP1252"},
    {"ansi_1253", "CP1253"}, {"ansi_1254", "CP1254"}, {"ansi_1255", "CP1255"},
    {"ansi_1256", "CP1256"}, {"ansi_1257", "CP1257"}, {"ansi_1258", "CP1258"},
    {"dos437", "CP437"}, {"dos850", "CP850"}, {"dos852", "CP852"},
    {"dos855", "CP855"}, {"dos857", "CP857"}, {"dos860", "CP860"},
    {"dos861", "CP861"}, {"dos863", "CP863"}, {"dos864", "CP864"},
    {"dos865", "CP865"}, {"dos866", "CP866"}, {"dos869", "CP869"},
    {"iso8859-1", "CP1252"}, {"iso8859-2", "ISO-8859-2"}, {"iso8859-3", "ISO-8859-3"},
    {"iso8859-4", "ISO-8859-4"}, {"iso8859-5", "ISO-8859-5"}, {"iso8859-6", "ISO-8859-6"},
    {"iso8859-7", "ISO-8859-7"}, {"iso8859-8", "ISO-8859-8"}, {"iso8859-9", "CP1254"},
    {"iso8859-10", "ISO-8859-10"}, {"iso8859-13", "ISO-8859-13"},
    {"iso8859-14", "ISO-8859-14"}, {"iso8859-15", "ISO-8859-15"}
};

// iconv headers use either char** or const char** for the input cursor.
template<class Input>
size_t run_iconv(size_t (*fn)(iconv_t, Input**, size_t*, char**, size_t*),
                 iconv_t cd, const char*& in, size_t& left, char*& out, size_t& space) {
    Input* cursor = const_cast<Input*>(in);
    auto result = fn(cd, &cursor, &left, &out, &space);
    in = cursor;
    return result;
}

class Converter {
public:
    Converter(const char* to, const char* from) : cd_(iconv_open(to, from)) {
        if (cd_ == (iconv_t)-1)
            throw std::runtime_error(std::string("DXF converter unavailable: ") + from + " -> " + to);
    }
    ~Converter() { iconv_close(cd_); }
    Converter(const Converter&) = delete;
    Converter& operator=(const Converter&) = delete;

    // Without replacement, invalid or unmappable input is reported to the caller.
    std::optional<std::string> convert(std::string_view bytes, bool replace = false,
                                       bool gbk = false) const {
        iconv(cd_, nullptr, nullptr, nullptr, nullptr);
        const char* in = bytes.data();
        size_t left = bytes.size();
        std::string result;
        while (left) {
            char buffer[512];
            char* out = buffer;
            size_t space = sizeof(buffer);
            auto n = run_iconv(iconv, cd_, in, left, out, space);
            const int error = errno;
            result.append(buffer, out - buffer);
            if (n != size_t(-1)) continue;
            if (error == E2BIG) continue;
            if (error != EILSEQ && error != EINVAL) throw std::runtime_error("DXF conversion failed");
            // WHATWG GBK reserves the standalone byte 80 for the euro sign.
            if (gbk && static_cast<unsigned char>(*in) == 0x80) result += "\xE2\x82\xAC";
            else if (replace) result += "\xEF\xBF\xBD";
            else return std::nullopt;
            ++in;
            --left;
        }
        return result;
    }
private:
    iconv_t cd_;
};

std::optional<unsigned> hex4(std::string_view s) {
    if (s.size() < 4) return {};
    unsigned value;
    auto r = std::from_chars(s.data(), s.data() + 4, value, 16);
    if (r.ec != std::errc{} || r.ptr != s.data() + 4) return {};
    return value;
}

std::string decode_mif(std::string_view s) {
    constexpr const char* encodings[] = {"CP932", "BIG5", "CP949", "JOHAB", "GBK"};
    std::string result;
    for (size_t i = 0; i < s.size();) {
        if (s.substr(i, 3) == "\\M+" && s.size() - i >= 8 && s[i + 3] >= '1' && s[i + 3] <= '5') {
            if (auto code = hex4(s.substr(i + 4))) {
                const char pair[] = {static_cast<char>(*code >> 8), static_cast<char>(*code)};
                Converter decoder("UTF-8", encodings[s[i + 3] - '1']);
                if (auto text = decoder.convert({pair, 2}, false, s[i + 3] == '5')) {
                    result += *text;
                    i += 8;
                    continue;
                }
            }
        }
        result += s[i++];
    }
    return result;
}

std::string decode_cif(std::string_view s) {
    Converter unicode("UTF-8", "UTF-16BE");
    std::string result;
    for (size_t i = 0; i < s.size();) {
        if (s.substr(i, 3) == "\\U+") {
            if (auto unit = hex4(s.substr(i + 3))) {
                char bytes[4] = {static_cast<char>(*unit >> 8), static_cast<char>(*unit)};
                size_t length = 2;
                i += 7;
                if (*unit >= 0xd800 && *unit <= 0xdbff && s.substr(i, 3) == "\\U+") {
                    auto low = hex4(s.substr(i + 3));
                    if (low && *low >= 0xdc00 && *low <= 0xdfff) {
                        bytes[2] = static_cast<char>(*low >> 8);
                        bytes[3] = static_cast<char>(*low);
                        length = 4;
                        i += 7;
                    }
                }
                if (length == 4 || *unit < 0xd800 || *unit > 0xdfff)
                    result += unicode.convert({bytes, length}).value();
                continue;
            }
        }
        result += s[i++];
    }
    return result;
}

void append_cif(std::string& out, unsigned unit) {
    constexpr char hex[] = "0123456789ABCDEF";
    out += "\\U+";
    for (int shift : {12, 8, 4, 0}) out += hex[(unit >> shift) & 15];
}
} // namespace

bool known_code_page(std::string_view declaration) {
    std::string name(declaration);
    for(char& c:name)if(c>='A' && c<='Z')c+='a'-'A';
    if(name=="utf-8" || name=="utf8" || name=="ascii" || name=="unicode")return true;
    if(name.compare(0,4,"iso_")==0)name.erase(3,1);
    for(const auto& a:aliases)if(name==a.dxf)return true;
    return false;
}
const char* legacy_page(std::string_view version, std::string_view declaration) {
    if (version.size() != 6 || (version.substr(0, 2) != "AC" && version.substr(0, 2) != "AD") ||
        version.substr(2) >= "1021" || declaration.empty()) return nullptr;
    std::string name(declaration);
    for (char& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (name == "utf-8" || name == "utf8" || name == "ascii" || name == "unicode") return nullptr;
    if (name.compare(0, 4, "iso_") == 0) name.erase(3, 1);
    for (const auto& a : aliases) if (name == a.dxf) return a.codec;
    return "CP1252"; // Same fallback as encoding_from_code_page.
}

std::u32string utf8_scalars(std::string_view utf8) {
    Converter decoder("UTF-32LE", "UTF-8");
    const auto bytes = decoder.convert(utf8);
    if (!bytes) throw std::invalid_argument("Invalid UTF-8 text");
    std::u32string result;
    for (std::size_t i = 0; i < bytes->size(); i += 4) {
        char32_t c = 0;
        for (unsigned j = 0; j < 4; ++j)
            c |= static_cast<char32_t>(static_cast<unsigned char>((*bytes)[i + j])) << (8 * j);
        result.push_back(c);
    }
    return result;
}

std::string utf8_string(std::u32string_view scalars) {
    std::string bytes;
    for (auto c : scalars) {
        if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            throw std::invalid_argument("Invalid Unicode scalar");
        for (unsigned j = 0; j < 4; ++j) bytes.push_back(static_cast<char>((c >> (8 * j)) & 0xff));
    }
    auto result = Converter("UTF-8", "UTF-32LE").convert(bytes);
    if (!result) throw std::invalid_argument("Invalid Unicode text");
    return *result;
}

void valid_symbol_name(std::string_view name) {
    if(name.empty() || name.find_first_of("<>/\\\":;?*|,=`")!=name.npos ||
       std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32;}))
        throw std::invalid_argument("Invalid symbol name");
    const auto key=detail::symbol_name_key(name); // Also validate UTF-8.
    if(std::any_of(key.begin(),key.end(),[](char32_t c){return c<32 || (c>=127 && c<=159);}))
        throw std::invalid_argument("Control character in symbol name");
}
std::u32string symbol_name_key(std::string_view utf8) {
    auto key = utf8_scalars(utf8);
    for (auto& c : key) {
        const auto found = std::lower_bound(std::begin(name_uppercase), std::end(name_uppercase), c,
            [](const auto& mapping, char32_t value) { return mapping.first < value; });
        if (found != std::end(name_uppercase) && found->first == c) c = found->second;
    }
    return key;
}

namespace {
struct GbkUnit {
    std::size_t length;
    bool valid;
    unsigned pointer = 0;
};

// encoding_rs shares this framing between GBK and GB18030. An invalid
// ASCII trail is replayed; a non-ASCII trail belongs to the failed sequence.
GbkUnit gbk_unit(std::string_view s) {
    const auto lead = static_cast<unsigned char>(s[0]);
    if (s.size() == 1 || lead == 0xff) return {1, false};
    const auto trail = static_cast<unsigned char>(s[1]);
    if (trail == 0xff) return {2, false};
    if (trail >= 0x40 && trail != 0x7f) return {2, true};
    if (trail < '0' || trail > '9') return {1, false};
    if (s.size() == 2) return {2, false};
    const auto third = static_cast<unsigned char>(s[2]);
    if (third < 0x81 || third > 0xfe) return {1, false};
    if (s.size() == 3) return {3, false};
    if (s[3] < '0' || s[3] > '9') return {1, false};
    const unsigned pointer = (((lead - 0x81) * 10 + trail - '0') * 126 + third - 0x81) * 10 + s[3] - '0';
    return {4, pointer <= 39419 || (pointer >= 189000 && pointer <= 1237575), pointer};
}

std::string gbk_four_byte_text(std::string_view bytes, unsigned pointer) {
    if (pointer < 189000) {
        auto scalar = Converter("UTF-8", "GB18030").convert(bytes);
        if (!scalar) throw std::runtime_error("Unavailable GB18030 mapping");
        return *scalar;
    }
    const char32_t scalar = pointer - 189000 + 0x10000;
    return utf8_string({&scalar, 1});
}
} // namespace

// A trail byte in GBK, Big5 or Shift-JIS can look like a backslash. Inspect
// escapes only at character boundaries and retain the untouched source bytes.
void recover_legacy_text(std::string& bytes, const char* page,
                         const std::function<void(LegacyTextError, std::size_t)>& report) {
    const std::string_view input(bytes);
    const auto first = input.find("\\U+");
    const auto non_ascii = std::find_if(input.begin(), input.end(), [](unsigned char c) { return c >= 0x80; });
    if (first == input.npos && non_ascii == input.end()) return;

    const bool gbk = std::string_view(page) == "GBK";
    std::optional<Converter> decoder;
    if (!gbk && non_ascii != input.end()) decoder.emplace("UTF-8", page);
    std::string output;
    std::size_t copied = 0;
    auto replace = [&](std::size_t at, std::size_t length, std::string_view replacement) {
        output.append(input.substr(copied, at - copied));
        output.append(replacement);
        copied = at + length;
    };
    for (std::size_t i = 0; i < input.size();) {
        const auto rest = input.substr(i);
        if (rest.substr(0, 3) == "\\U+") {
            auto unit = hex4(rest.substr(3));
            if (!unit) {
                report(LegacyTextError::MalformedEscape, i);
                // Escape the backslash itself. One CIF decoding pass yields
                // the original literal, and the saved drawing is strict-readable.
                replace(i, 1, "\\U+005C");
                ++i;
            } else if (*unit >= 0xd800 && *unit <= 0xdfff) {
                if (*unit < 0xdc00 && rest.substr(7, 3) == "\\U+") {
                    auto low = hex4(rest.substr(10));
                    if (low && *low >= 0xdc00 && *low <= 0xdfff) {
                        i += 14;
                        continue;
                    }
                }
                report(LegacyTextError::UnpairedSurrogate, i);
                replace(i, 7, "");
                i += 7;
            } else {
                i += 7;
            }
            continue;
        }
        const auto lead = static_cast<unsigned char>(rest[0]);
        if (lead < 0x80 || (gbk && lead == 0x80)) {
            ++i;
            continue;
        }
        std::size_t length = 1;
        if (gbk) {
            const auto unit = gbk_unit(rest);
            length = unit.length;
            if (unit.valid) {
                // Four-byte input also needs to work with native two-byte GBK codecs.
                if (length == 4)
                    replace(i, length, to_legacy(gbk_four_byte_text(rest.substr(0, length), unit.pointer), page));
                i += length;
                continue;
            }
        } else {
            // The supported non-GBK legacy pages use one- or two-byte characters.
            if (decoder->convert(rest.substr(0, 1))) {
                ++i;
                continue;
            }
            if (rest.size() >= 2 && decoder->convert(rest.substr(0, 2))) {
                i += 2;
                continue;
            }
        }
        report(LegacyTextError::InvalidBytes, i);
        replace(i, length, "\\U+FFFD");
        i += length;
    }
    if (copied) {
        output.append(input.substr(copied));
        bytes = std::move(output);
    }
}

// Rust's undecorated text reader uses Latin-1 for an invalid UTF-8 line.
// Binary strings use Unicode maximal-subpart replacement.
std::string decode_undeclared_text(std::string_view bytes, bool binary) {
    std::string text;
    for (std::size_t i = 0; i < bytes.size();) {
        const auto c = static_cast<unsigned char>(bytes[i]);
        if (c < 128) {
            text += bytes[i++];
            continue;
        }
        const unsigned width = c < 0xc2 || c > 0xf4 ? 0 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        unsigned used = 1;
        while (used < width && used < bytes.size() - i) {
            const auto next = static_cast<unsigned char>(bytes[i + used]);
            if (next < 0x80 || next > 0xbf ||
                (used == 1 && ((c == 0xe0 && next < 0xa0) || (c == 0xed && next >= 0xa0) ||
                               (c == 0xf0 && next < 0x90) || (c == 0xf4 && next >= 0x90)))) break;
            ++used;
        }
        if (used == width) text.append(bytes.substr(i, used));
        else if (!binary) {
            text = *Converter("UTF-8", "ISO-8859-1").convert(bytes);
            break;
        } else text += "\xEF\xBF\xBD";
        i += used;
    }
    if (text.find("\\M+") != text.npos) text = decode_mif(text);
    if (text.find("\\U+") != text.npos) text = decode_cif(text);
    return text;
}

std::string from_legacy(std::string_view bytes, const char* page) {
    if (!page) return std::string(bytes);
    auto text = *Converter("UTF-8", page).convert(bytes, true, std::string_view(page) == "GBK");
    if (text.find("\\M+") != text.npos) text = decode_mif(text);
    if (text.find("\\U+") != text.npos) text = decode_cif(text);
    return text;
}

std::string to_legacy(std::string_view text, const char* page) {
    if (!page) return std::string(text);
    Converter encoder(page, "UTF-8"), decoder("UTF-8", page);
    const bool gbk = std::string_view(page) == "GBK";
    // Some iconv implementations substitute '?' or perform best-fit mappings.
    // Accept only encodings that reproduce the original scalar(s).
    auto reversible = [&](std::string_view s) -> std::optional<std::string> {
        auto bytes = encoder.convert(s);
        if (bytes && decoder.convert(*bytes, false, gbk) == s) return bytes;
        return {};
    };
    if (auto bytes = reversible(text)) return *bytes;
    std::string output;
    Converter to_units("UTF-16BE", "UTF-8"), from_units("UTF-8", "UTF-16BE");
    auto units = to_units.convert(text);
    if (!units || from_units.convert(*units) != text)
        throw std::invalid_argument("Invalid UTF-8 DXF text");
    auto unit_at = [&](size_t i) -> unsigned {
        return (static_cast<unsigned char>((*units)[i]) << 8) |
                static_cast<unsigned char>((*units)[i + 1]);
    };
    for (size_t i = 0; i < units->size();) {
        const auto high = unit_at(i);
        const size_t length = high >= 0xd800 && high <= 0xdbff ? 4 : 2;
        const auto scalar = from_units.convert(std::string_view(*units).substr(i, length)).value();
        if (gbk && high == 0x20ac) output += static_cast<char>(0x80);
        else if (auto bytes = reversible(scalar)) output += *bytes;
        else {
            append_cif(output, high);
            if (length == 4) append_cif(output, unit_at(i + 2));
        }
        i += length;
    }
    return output;
}
} // namespace libopencad::detail
