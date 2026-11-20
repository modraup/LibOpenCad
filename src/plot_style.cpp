// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/plot_style.rs.
#include <libopencad/plot_style.hpp>
#include <libopencad/color.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

namespace libopencad {
namespace {

constexpr std::uint64_t kMaxInflatedBytes = 64ull * 1024 * 1024;
constexpr char kCtbPrefix[] = "PIAFILEVERSION_2.0,CTBVER1,compress\r\npmzlibcodec";
constexpr char kStbPrefix[] = "PIAFILEVERSION_2.0,STBVER1,compress\r\npmzlibcodec";
constexpr char kTtbPrefix[] = "PIAFILEVERSION_2.0,TTBVER1,compress\r\npmzlibcodec";

bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool starts_with(const std::uint8_t *data, std::size_t size, std::string_view prefix) {
    return size >= prefix.size() && std::memcmp(data, prefix.data(), prefix.size()) == 0;
}

std::string full_real(float value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << value;
    auto text = out.str();
    if (std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) || c == '-'; }))
        text += ".0";
    return text;
}

std::int32_t packed_rgb(std::array<std::uint8_t, 3> rgb) {
    return static_cast<std::int32_t>(
        (0xC2u << 24) | (static_cast<std::uint32_t>(rgb[0]) << 16) | (static_cast<std::uint32_t>(rgb[1]) << 8) | rgb[2]);
}

std::optional<std::array<std::uint8_t, 3>> unpack_plot_color(std::int32_t packed) {
    if (packed == -1 || packed == -1006632961 || packed == -1056964608)
        return std::nullopt;
    const auto bytes = static_cast<std::uint32_t>(packed);
    return std::array<std::uint8_t, 3>{
        static_cast<std::uint8_t>((bytes >> 16) & 0xff),
        static_cast<std::uint8_t>((bytes >> 8) & 0xff),
        static_cast<std::uint8_t>(bytes & 0xff),
    };
}

std::uint32_t adler32_bytes(const std::uint8_t *data, std::size_t size) {
    constexpr std::uint32_t kMod = 65521;
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % kMod;
        b = (b + a) % kMod;
    }
    return (b << 16) | a;
}

void append_windows_1252(std::string &out, std::uint8_t byte) {
    static constexpr char32_t high[32] = {
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
        0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
    };
    char32_t cp = byte;
    if (byte >= 0x80 && byte <= 0x9F)
        cp = high[byte - 0x80];
    if (cp <= 0x7F)
        out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool is_utf8(const std::vector<std::uint8_t> &bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        auto c = bytes[i];
        std::size_t need = 0;
        if (c <= 0x7F)
            need = 1;
        else if ((c & 0xE0) == 0xC0)
            need = 2;
        else if ((c & 0xF0) == 0xE0)
            need = 3;
        else if ((c & 0xF8) == 0xF0)
            need = 4;
        else
            return false;
        if (i + need > bytes.size())
            return false;
        for (std::size_t n = 1; n < need; ++n) {
            if ((bytes[i + n] & 0xC0) != 0x80)
                return false;
        }
        i += need;
    }
    return true;
}

std::vector<std::uint8_t> inflate_capped(const std::uint8_t *data, std::size_t size, int window_bits,
                                         const char *what) {
    z_stream strm{};
    if (inflateInit2(&strm, window_bits) != Z_OK)
        throw std::runtime_error(std::string(what) + ": inflateInit failed");
    strm.next_in = const_cast<Bytef *>(data);
    strm.avail_in = static_cast<uInt>(size);
    std::vector<std::uint8_t> out;
    out.resize(std::min<std::size_t>(size * 4 + 64, static_cast<std::size_t>(kMaxInflatedBytes + 1)));
    std::size_t produced = 0;
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        if (produced >= kMaxInflatedBytes + 1) {
            inflateEnd(&strm);
            throw std::runtime_error(std::string(what) + ": inflated data exceeds the " +
                                     std::to_string(kMaxInflatedBytes) + "-byte limit");
        }
        if (produced == out.size()) {
            auto next = out.size() * 2;
            if (next > kMaxInflatedBytes + 1)
                next = static_cast<std::size_t>(kMaxInflatedBytes + 1);
            if (next <= out.size())
                next = out.size() + 4096;
            if (next > kMaxInflatedBytes + 1)
                next = static_cast<std::size_t>(kMaxInflatedBytes + 1);
            out.resize(next);
        }
        strm.next_out = out.data() + produced;
        strm.avail_out = static_cast<uInt>(out.size() - produced);
        rc = inflate(&strm, Z_NO_FLUSH);
        produced = strm.total_out;
        if (rc == Z_STREAM_END)
            break;
        if (rc != Z_OK && rc != Z_BUF_ERROR) {
            inflateEnd(&strm);
            throw std::runtime_error(std::string(what) + ": inflate failed");
        }
        if (rc == Z_BUF_ERROR && strm.avail_in == 0)
            break;
    }
    inflateEnd(&strm);
    if (produced > kMaxInflatedBytes)
        throw std::runtime_error(std::string(what) + ": inflated data exceeds the " +
                                 std::to_string(kMaxInflatedBytes) + "-byte limit");
    out.resize(produced);
    return out;
}

std::vector<std::uint8_t> inflate_lenient(const std::uint8_t *payload, std::size_t size,
                                          std::vector<std::string> &warnings) {
    try {
        return inflate_capped(payload, size, 15, "CTB zlib decompress");
    } catch (const std::runtime_error &zlib_error) {
        if (size > 2) {
            try {
                auto raw = inflate_capped(payload + 2, size - 2, -15, "CTB zlib decompress");
                warnings.emplace_back("the zlib stream is damaged; its deflate data was read directly");
                return raw;
            } catch (...) {
                throw zlib_error;
            }
        }
        throw;
    }
}

std::vector<std::uint8_t> compress_ctb(const std::uint8_t *text, std::size_t size) {
    std::vector<std::uint8_t> body(text, text + size);
    body.push_back(0);
    z_stream strm{};
    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK)
        throw std::runtime_error("CTB zlib compress: deflateInit failed");
    strm.next_in = body.data();
    strm.avail_in = static_cast<uInt>(body.size());
    std::vector<std::uint8_t> compressed(deflateBound(&strm, static_cast<uLong>(body.size())));
    strm.next_out = compressed.data();
    strm.avail_out = static_cast<uInt>(compressed.size());
    const auto rc = deflate(&strm, Z_FINISH);
    const auto produced = compressed.size() - strm.avail_out;
    deflateEnd(&strm);
    if (rc != Z_STREAM_END)
        throw std::runtime_error("CTB zlib compress: deflate failed");
    compressed.resize(produced);

    std::vector<std::uint8_t> out(reinterpret_cast<const std::uint8_t *>(kCtbPrefix),
                                  reinterpret_cast<const std::uint8_t *>(kCtbPrefix) + std::strlen(kCtbPrefix));
    const auto checksum = adler32_bytes(compressed.data(), compressed.size());
    const auto text_len = static_cast<std::uint32_t>(body.size());
    const auto compressed_len = static_cast<std::uint32_t>(compressed.size());
    out.insert(out.end(), reinterpret_cast<const std::uint8_t *>(&checksum),
               reinterpret_cast<const std::uint8_t *>(&checksum) + 4);
    out.insert(out.end(), reinterpret_cast<const std::uint8_t *>(&text_len),
               reinterpret_cast<const std::uint8_t *>(&text_len) + 4);
    out.insert(out.end(), reinterpret_cast<const std::uint8_t *>(&compressed_len),
               reinterpret_cast<const std::uint8_t *>(&compressed_len) + 4);
    out.insert(out.end(), compressed.begin(), compressed.end());
    return out;
}

std::pair<std::vector<std::uint8_t>, std::vector<std::string>> decompress_ctb(const std::uint8_t *data,
                                                                             std::size_t size) {
    std::vector<std::string> warnings;
    std::vector<std::uint8_t> decoded;
    const bool headed = starts_with(data, size, kCtbPrefix) || starts_with(data, size, kStbPrefix) ||
                        starts_with(data, size, kTtbPrefix);
    if (headed) {
        if (size < 60)
            throw std::runtime_error("CTB header is truncated");
        std::uint32_t checksum = 0, text_len = 0, compressed_len = 0;
        std::memcpy(&checksum, data + 48, 4);
        std::memcpy(&text_len, data + 52, 4);
        std::memcpy(&compressed_len, data + 56, 4);
        const auto available = size - 60;
        const std::uint8_t *payload = data + 60;
        std::size_t payload_size = available;
        if (compressed_len > available) {
            warnings.push_back("the header announces " + std::to_string(compressed_len) +
                               " compressed bytes but the file holds " + std::to_string(available));
        } else {
            payload_size = compressed_len;
        }
        if (adler32_bytes(payload, payload_size) != checksum)
            warnings.emplace_back("the compressed payload's checksum does not match");
        decoded = inflate_lenient(payload, payload_size, warnings);
        constexpr std::size_t kToleratedShortfall = 16;
        if (decoded.size() + kToleratedShortfall < text_len)
            throw std::runtime_error("CTB text is incomplete: the header announces " + std::to_string(text_len) +
                                     " bytes but only " + std::to_string(decoded.size()) + " could be read");
        if (decoded.size() != text_len)
            warnings.push_back("the header announces " + std::to_string(text_len) + " bytes of text but " +
                               std::to_string(decoded.size()) + " were read");
    } else {
        std::size_t split = 0;
        for (std::size_t i = 0; i < size; ++i) {
            if (data[i] == '\n') {
                split = i + 1;
                break;
            }
        }
        const auto *payload = data + split;
        const auto payload_size = size - split;
        if (payload_size > 0 && payload[0] == 0x78)
            decoded = inflate_lenient(payload, payload_size, warnings);
        else
            decoded = inflate_capped(payload, payload_size, -15, "legacy CTB deflate decompress");
    }
    while (!decoded.empty() && decoded.back() == 0)
        decoded.pop_back();
    return {std::move(decoded), std::move(warnings)};
}

std::string trim(std::string_view s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    auto last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

PlotStyleTable parse_legacy_plot_style_text(const std::string &text, std::string name, bool is_stb) {
    auto table = PlotStyleTable::identity(std::move(name));
    table.is_stb = is_stb;
    std::size_t style_index = 1;
    std::optional<PlotStyleEntry> current;
    std::string current_name;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        auto line = trim(raw);
        if (line == "begin_plot_style") {
            current = PlotStyleEntry{};
            current_name = "Color_" + std::to_string(style_index);
            continue;
        }
        if (line == "end_plot_style") {
            if (current) {
                if (is_stb)
                    table.named_entries[current_name] = *current;
                else if (style_index <= 255) {
                    table.aci_entries[style_index] = *current;
                    ++style_index;
                }
                current.reset();
            }
            continue;
        }
        if (!current)
            continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        auto key = trim(line.substr(0, eq));
        auto value = trim(line.substr(eq + 1));
        if (key == "description") {
            current->description = value;
            if (!value.empty())
                current_name = value;
        } else if (key == "screen") {
            current->screening = static_cast<std::uint8_t>(std::min(100, std::stoi(value.empty() ? "100" : value)));
        } else if (key == "lineweight") {
            current->lineweight = static_cast<std::uint8_t>(value.empty() ? 0 : std::stoi(value));
        } else if (key == "color1") {
            if (!value.empty() && value[0] == '#' && value.size() == 7) {
                auto pair = [&](std::size_t a, std::size_t b) -> std::uint8_t {
                    try {
                        return static_cast<std::uint8_t>(std::stoul(value.substr(a, b - a), nullptr, 16));
                    } catch (...) {
                        return 0;
                    }
                };
                current->color = std::array<std::uint8_t, 3>{pair(1, 3), pair(3, 5), pair(5, 7)};
            } else {
                try {
                    current->color = unpack_plot_color(std::stoi(value));
                } catch (...) {
                }
            }
        }
    }
    return table;
}

PlotStyleTable parse_plot_style_text(const std::string &text, std::string name, bool is_stb) {
    {
        std::istringstream probe(text);
        std::string line;
        while (std::getline(probe, line)) {
            if (trim(line) == "begin_plot_style")
                return parse_legacy_plot_style_text(text, std::move(name), is_stb);
        }
    }

    struct PendingStyle {
        std::size_t index = 0;
        std::string name;
        PlotStyleEntry entry;
        std::optional<std::int32_t> color;
        std::optional<std::int32_t> mode_color;
    };
    enum class Section { Root, Other, PlotStyles, Lineweights };
    std::vector<PlotStyleEntry> aci_entries(256);
    std::unordered_map<std::string, PlotStyleEntry> named_entries;
    std::string description;
    float scale_factor = 1.0f;
    bool apply_factor = false;
    std::uint8_t custom_lineweight_display_units = 0;
    std::vector<float> lineweights;
    auto section = Section::Root;
    std::optional<PendingStyle> current;

    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        auto line = trim(raw);
        if (line.empty())
            continue;
        if (line == "plot_style{") {
            section = Section::PlotStyles;
            continue;
        }
        if (line == "custom_lineweight_table{") {
            section = Section::Lineweights;
            continue;
        }
        if (!line.empty() && line.back() == '{') {
            if (section == Section::PlotStyles && !current) {
                auto index_text = trim(line.substr(0, line.size() - 1));
                try {
                    auto index = static_cast<std::size_t>(std::stoul(index_text));
                    auto default_name = "Color_" + std::to_string(index + 1);
                    PendingStyle style;
                    style.index = index;
                    style.name = default_name;
                    style.entry.name = default_name;
                    style.entry.localized_name = default_name;
                    current = std::move(style);
                    continue;
                } catch (...) {
                }
            }
            section = Section::Other;
            continue;
        }
        if (line == "}") {
            if (current) {
                auto packed = current->mode_color ? current->mode_color : current->color;
                current->entry.color = packed ? unpack_plot_color(*packed) : std::nullopt;
                if (is_stb)
                    named_entries[current->name] = current->entry;
                else if (current->index < 255)
                    aci_entries[current->index + 1] = current->entry;
                current.reset();
            } else {
                section = Section::Root;
            }
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        auto key = trim(line.substr(0, eq));
        auto value = trim(line.substr(eq + 1));
        if (!value.empty() && value.front() == '"')
            value.erase(value.begin());
        if (current) {
            if (key == "name") {
                current->name = value;
                current->entry.name = value;
            } else if (key == "localized_name")
                current->entry.localized_name = value;
            else if (key == "description")
                current->entry.description = value;
            else if (key == "screen") {
                try {
                    current->entry.screening =
                        static_cast<std::uint8_t>(std::min(100, std::stoi(value)));
                } catch (...) {
                }
            } else if (key == "lineweight") {
                try {
                    current->entry.lineweight = static_cast<std::uint8_t>(std::stoi(value));
                } catch (...) {
                }
            } else if (key == "color") {
                try {
                    current->color = std::stoi(value);
                } catch (...) {
                }
            } else if (key == "mode_color") {
                try {
                    current->mode_color = std::stoi(value);
                } catch (...) {
                }
            } else if (key == "color_policy") {
                try {
                    current->entry.color_policy = static_cast<std::uint8_t>(std::stoi(value));
                } catch (...) {
                    current->entry.color_policy = 1;
                }
            } else if (key == "physical_pen_number") {
                try {
                    current->entry.physical_pen_number = static_cast<std::uint16_t>(std::stoi(value));
                } catch (...) {
                    current->entry.physical_pen_number = 0;
                }
            } else if (key == "virtual_pen_number") {
                try {
                    current->entry.virtual_pen_number = static_cast<std::uint16_t>(std::stoi(value));
                } catch (...) {
                    current->entry.virtual_pen_number = 0;
                }
            } else if (key == "linepattern_size") {
                try {
                    current->entry.linepattern_size = std::stof(value);
                } catch (...) {
                    current->entry.linepattern_size = 0.5f;
                }
            } else if (key == "linetype") {
                try {
                    current->entry.linetype = static_cast<std::uint8_t>(std::stoi(value));
                } catch (...) {
                    current->entry.linetype = 31;
                }
            } else if (key == "adaptive_linetype")
                current->entry.adaptive_linetype = ieq(value, "TRUE");
            else if (key == "fill_style") {
                try {
                    current->entry.fill_style = static_cast<std::uint8_t>(std::stoi(value));
                } catch (...) {
                    current->entry.fill_style = 73;
                }
            } else if (key == "end_style") {
                try {
                    current->entry.end_style = static_cast<std::uint8_t>(std::stoi(value));
                } catch (...) {
                    current->entry.end_style = 4;
                }
            } else if (key == "join_style") {
                try {
                    current->entry.join_style = static_cast<std::uint8_t>(std::stoi(value));
                } catch (...) {
                    current->entry.join_style = 5;
                }
            }
            continue;
        }
        if (section == Section::Root) {
            if (key == "description")
                description = value;
            else if (key == "scale_factor") {
                try {
                    scale_factor = std::stof(value);
                } catch (...) {
                    scale_factor = 1.0f;
                }
            } else if (key == "apply_factor")
                apply_factor = ieq(value, "TRUE");
            else if (key == "custom_lineweight_display_units") {
                try {
                    custom_lineweight_display_units = static_cast<std::uint8_t>(std::stoi(value));
                } catch (...) {
                    custom_lineweight_display_units = 0;
                }
            }
        } else if (section == Section::Lineweights) {
            try {
                auto index = static_cast<std::size_t>(std::stoul(key));
                auto weight = std::stof(value);
                if (index <= 255) {
                    if (lineweights.size() <= index)
                        lineweights.resize(index + 1, 0.0f);
                    lineweights[index] = weight;
                }
            } catch (...) {
            }
        }
    }
    if (lineweights.empty())
        lineweights.assign(kPlotStyleLineweights.begin(), kPlotStyleLineweights.end());
    PlotStyleTable table;
    table.name = std::move(name);
    table.is_stb = is_stb;
    table.description = std::move(description);
    table.scale_factor = scale_factor;
    table.apply_factor = apply_factor;
    table.custom_lineweight_display_units = custom_lineweight_display_units;
    table.lineweights = std::move(lineweights);
    table.aci_entries = std::move(aci_entries);
    table.named_entries = std::move(named_entries);
    return table;
}

} // namespace

std::pair<std::string, std::optional<std::string>> decode_plot_style_text(std::vector<std::uint8_t> bytes) {
    if (is_utf8(bytes))
        return {std::string(bytes.begin(), bytes.end()), std::nullopt};
    std::string text;
    text.reserve(bytes.size());
    for (auto byte : bytes)
        append_windows_1252(text, byte);
    return {std::move(text),
            std::string("descriptions were read as Windows-1252 text (the file is not UTF-8)")};
}

PlotStyleTable PlotStyleTable::identity(std::string name) {
    PlotStyleTable table;
    table.name = std::move(name);
    table.lineweights.assign(kPlotStyleLineweights.begin(), kPlotStyleLineweights.end());
    table.aci_entries.assign(256, PlotStyleEntry{});
    return table;
}

PlotStyleTable PlotStyleTable::from_bytes(std::string name, const std::uint8_t *data, std::size_t size) {
    const bool is_stb = [&] {
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".stb") == 0;
    }();
    auto [decoded, warnings] = decompress_ctb(data, size);
    auto [text, note] = decode_plot_style_text(std::move(decoded));
    auto table = parse_plot_style_text(text, std::move(name), is_stb);
    table.load_warnings = std::move(warnings);
    if (note)
        table.load_warnings.push_back(*note);
    return table;
}

std::string PlotStyleTable::to_text() const {
    std::string s;
    auto description = this->description;
    std::replace(description.begin(), description.end(), '\r', ' ');
    std::replace(description.begin(), description.end(), '\n', ' ');
    s += "description=\"" + description + "\n";
    s += std::string("aci_table_available=") + (is_stb ? "FALSE" : "TRUE") + "\n";
    s += "scale_factor=" + full_real(scale_factor) + "\n";
    s += std::string("apply_factor=") + (apply_factor ? "TRUE" : "FALSE") + "\n";
    s += "custom_lineweight_display_units=" + std::to_string(custom_lineweight_display_units) + "\n";
    std::vector<std::pair<std::string, const PlotStyleEntry *>> styles;
    if (is_stb) {
        std::vector<std::pair<std::string, const PlotStyleEntry *>> named;
        named.reserve(named_entries.size());
        for (const auto &entry : named_entries)
            named.emplace_back(entry.first, &entry.second);
        std::sort(named.begin(), named.end(), [](const auto &a, const auto &b) {
            const auto a_normal = a.first != "Normal";
            const auto b_normal = b.first != "Normal";
            if (a_normal != b_normal)
                return a_normal < b_normal;
            return a.first < b.first;
        });
        styles = std::move(named);
    } else {
        s += "aci_table{\n";
        for (int index = 0; index < 255; ++index)
            s += " " + std::to_string(index) + "=\"Color_" + std::to_string(index + 1) + "\n";
        s += "}\n";
        for (std::size_t index = 1; index < aci_entries.size() && index <= 255; ++index) {
            const auto &entry = aci_entries[index];
            auto name = entry.name.empty() ? "Color_" + std::to_string(index) : entry.name;
            styles.emplace_back(std::move(name), &entry);
        }
    }
    s += "plot_style{\n";
    for (std::size_t style_index = 0; style_index < styles.size(); ++style_index) {
        auto style_name = styles[style_index].first;
        std::replace(style_name.begin(), style_name.end(), '\r', ' ');
        std::replace(style_name.begin(), style_name.end(), '\n', ' ');
        const auto &entry = *styles[style_index].second;
        auto localized_name = entry.localized_name.empty() ? style_name : entry.localized_name;
        std::replace(localized_name.begin(), localized_name.end(), '\r', ' ');
        std::replace(localized_name.begin(), localized_name.end(), '\n', ' ');
        auto desc = entry.description;
        std::replace(desc.begin(), desc.end(), '\r', ' ');
        std::replace(desc.begin(), desc.end(), '\n', ' ');
        s += " " + std::to_string(style_index) + "{\n";
        s += "  name=\"" + style_name + "\n";
        s += "  localized_name=\"" + localized_name + "\n";
        s += "  description=\"" + desc + "\n";
        if (entry.color) {
            auto packed = packed_rgb(*entry.color);
            s += "  color=" + std::to_string(packed) + "\n  mode_color=" + std::to_string(packed) + "\n";
        } else {
            s += "  color=-1\n";
        }
        s += "  color_policy=" + std::to_string(entry.color_policy) + "\n";
        s += "  physical_pen_number=" + std::to_string(entry.physical_pen_number) +
             "\n  virtual_pen_number=" + std::to_string(entry.virtual_pen_number) + "\n";
        s += "  screen=" + std::to_string(entry.screening) + "\n";
        s += "  linepattern_size=" + full_real(entry.linepattern_size) +
             "\n  linetype=" + std::to_string(entry.linetype) + "\n  adaptive_linetype=" +
             std::string(entry.adaptive_linetype ? "TRUE" : "FALSE") + "\n";
        s += "  lineweight=" + std::to_string(entry.lineweight) + "\n";
        s += "  fill_style=" + std::to_string(entry.fill_style) + "\n  end_style=" +
             std::to_string(entry.end_style) + "\n  join_style=" + std::to_string(entry.join_style) +
             "\n }\n";
    }
    s += "}\ncustom_lineweight_table{\n";
    for (std::size_t index = 0; index < lineweights.size(); ++index)
        s += " " + std::to_string(index) + "=" + full_real(lineweights[index]) + "\n";
    s += "}\n";
    return s;
}

std::vector<std::uint8_t> PlotStyleTable::to_bytes() const {
    auto text = to_text();
    auto bytes = compress_ctb(reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
    if (is_stb && bytes.size() >= 22)
        std::memcpy(bytes.data() + 19, "STB", 3);
    return bytes;
}

std::optional<std::array<float, 3>> PlotStyleTable::resolve_color(std::uint8_t aci) const {
    if (aci >= aci_entries.size())
        return std::nullopt;
    const auto &entry = aci_entries[aci];
    std::optional<std::array<float, 3>> color;
    if (entry.color)
        color = std::array<float, 3>{(*entry.color)[0] / 255.0f, (*entry.color)[1] / 255.0f,
                                     (*entry.color)[2] / 255.0f};
    if (entry.color_policy & 2) {
        auto rgb = color;
        if (!rgb) {
            if (aci == 7)
                rgb = std::array<float, 3>{0.0f, 0.0f, 0.0f};
            else if (auto resolved = Color::from_index(aci).rgb())
                rgb = std::array<float, 3>{(*resolved)[0] / 255.0f, (*resolved)[1] / 255.0f,
                                           (*resolved)[2] / 255.0f};
        }
        if (!rgb)
            return std::nullopt;
        const auto gray = 0.299f * (*rgb)[0] + 0.587f * (*rgb)[1] + 0.114f * (*rgb)[2];
        color = std::array<float, 3>{gray, gray, gray};
    }
    return color;
}

std::optional<float> PlotStyleTable::resolve_lineweight(std::uint8_t aci) const {
    if (aci >= aci_entries.size())
        return std::nullopt;
    const auto &entry = aci_entries[aci];
    if (entry.lineweight == 0 || entry.lineweight == 255)
        return std::nullopt;
    if (entry.lineweight >= lineweights.size())
        return std::nullopt;
    auto weight = lineweights[entry.lineweight];
    if (apply_factor)
        weight *= std::max(scale_factor, 0.0f);
    return weight;
}

float PlotStyleTable::resolve_screening(std::uint8_t aci) const {
    if (aci >= aci_entries.size())
        return 1.0f;
    return std::min<std::uint8_t>(aci_entries[aci].screening, 100) / 100.0f;
}

} // namespace libopencad
