// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/classes/arch name helpers, profiles and DXF omit/proxy policy.
#include "arch_internal.hpp"
#include "dxf_binary.hpp"
#include <libopencad/arch.hpp>
#include <libopencad/dxf_document.hpp>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace libopencad {
namespace {
std::string ascii_upper(std::string_view value) {
    std::string out(value);
    for (char &c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trimmed(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos)
        return {};
    return std::string(value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1));
}
bool finite_point(Vector3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
constexpr ArchKind ENTITY_KINDS[] = {
    ArchKind::Wall,           ArchKind::Door,         ArchKind::Window,        ArchKind::Opening,
    ArchKind::Stair,          ArchKind::Railing,      ArchKind::Slab,          ArchKind::RoofSlab,
    ArchKind::Roof,           ArchKind::Space,        ArchKind::CurtainWall,   ArchKind::CurtainWallUnit,
    ArchKind::WindowAssembly, ArchKind::ColumnGrid,   ArchKind::CeilingGrid,   ArchKind::Polygon,
    ArchKind::MassElement,    ArchKind::MassGroup,    ArchKind::MultiViewBlock, ArchKind::MaskBlock,
    ArchKind::Section,        ArchKind::SectionLine,  ArchKind::ElevationLine, ArchKind::Dimension,
    ArchKind::Schedule,       ArchKind::LayoutCurve,  ArchKind::LayoutGrid,    ArchKind::LayoutGrid,
    ArchKind::Zone,           ArchKind::Area,         ArchKind::AreaGroup,     ArchKind::Camera,
    ArchKind::Slice,          ArchKind::EntityReference, ArchKind::Point,      ArchKind::DisplayTheme,
    ArchKind::StructuralMember};
std::uint64_t handle_of(const DxfRecord &record) {
    auto text = trimmed(record.handle());
    if (text.empty())
        return 0;
    return std::stoull(text, nullptr, 16);
}
std::vector<std::uint8_t> hex_bytes(std::string_view text) {
    std::string hex;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c)))
            hex.push_back(c);
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned value = 0;
        auto parsed = std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + i + 2)
            break;
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}
std::string hex_text(const std::vector<std::uint8_t> &bytes) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (auto b : bytes) {
        text.push_back(digits[b >> 4]);
        text.push_back(digits[b & 15]);
    }
    return text;
}
} // namespace

bool is_architecture_name(std::string_view dxf_name) {
    auto n = ascii_upper(trimmed(dxf_name));
    return n.rfind("AEC", 0) == 0 || n.rfind("AAEC", 0) == 0 || n.rfind("ABST_", 0) == 0;
}
bool is_civil_entity_name(std::string_view dxf_name) {
    auto n = ascii_upper(trimmed(dxf_name));
    return n == "AECC_COGO_POINT" || n == "AECC_ALIGNMENT" || n == "AECC_TIN_SURFACE" || n == "AECC_PROFILE";
}
std::optional<ArchKind> architecture_kind_of(std::string_view dxf_name) {
    auto n = ascii_upper(trimmed(dxf_name));
    for (std::size_t i = 0; i < std::size(ARCH_ENTITY_CLASSES); ++i)
        if (n == ARCH_ENTITY_CLASSES[i].first)
            return ENTITY_KINDS[i];
    return std::nullopt;
}
std::optional<std::string> architecture_cpp_class_name(std::string_view dxf_name) {
    auto n = ascii_upper(trimmed(dxf_name));
    for (auto &entry : ARCH_ENTITY_CLASSES)
        if (n == entry.first)
            return std::string(entry.second);
    return std::nullopt;
}
std::optional<std::vector<Vector3>> centered_wall_profile(Vector3 start, Vector3 end, double width) {
    auto edge = end - start;
    auto length = edge.length();
    if (!finite_point(start) || !finite_point(end) || !std::isfinite(width) || width <= 0 ||
        !std::isfinite(length) || length <= 0 || std::abs(start.z - end.z) > 1e-12)
        return std::nullopt;
    auto normal = Vector3{-edge.y / length, edge.x / length, 0} * (width * 0.5);
    return std::vector<Vector3>{start - normal, end - normal, end + normal, start + normal, start - normal};
}
std::optional<std::vector<Vector3>> rectangle_profile(Vector3 start, Vector3 end, double width) {
    auto edge = end - start;
    auto length = edge.length();
    if (!finite_point(start) || !finite_point(end) || !std::isfinite(width) || width <= 0 ||
        !std::isfinite(length) || length <= 0 || std::abs(start.z - end.z) > 1e-12)
        return std::nullopt;
    auto normal = Vector3{-edge.y / length, edge.x / length, 0} * width;
    return std::vector<Vector3>{start, end, end + normal, start + normal, start};
}
namespace {
bool arch_positive(double value) { return std::isfinite(value) && value > 0; }
std::optional<Vector3> outline_unit(Vector3 from, Vector3 to) {
    auto edge = to - from;
    auto length = edge.length();
    if (!arch_positive(length))
        return std::nullopt;
    return edge * (1.0 / length);
}
Vector3 outline_left(Vector3 direction) { return {-direction.y, direction.x, 0}; }
std::optional<Vector3> outline_intersect(Vector3 origin, Vector3 through, Vector3 other, Vector3 other_through) {
    auto first = through - origin;
    auto second = other_through - other;
    auto cross = first.x * second.y - first.y * second.x;
    if (std::abs(cross) < 1e-9)
        return std::nullopt;
    auto delta = other - origin;
    auto parameter = (delta.x * second.y - delta.y * second.x) / cross;
    return origin + first * parameter;
}
std::optional<std::pair<Vector3, Vector3>> outline_offset(const std::vector<Vector3> &points, std::size_t index,
                                                          double half) {
    if (index == 0) {
        auto direction = outline_unit(points[0], points[1]);
        if (!direction)
            return std::nullopt;
        auto normal = outline_left(*direction) * half;
        return std::pair<Vector3, Vector3>{points[0] + normal, points[0] - normal};
    }
    if (index + 1 == points.size()) {
        auto direction = outline_unit(points[index - 1], points[index]);
        if (!direction)
            return std::nullopt;
        auto normal = outline_left(*direction) * half;
        return std::pair<Vector3, Vector3>{points[index] + normal, points[index] - normal};
    }
    auto incoming = outline_unit(points[index - 1], points[index]);
    auto outgoing = outline_unit(points[index], points[index + 1]);
    if (!incoming || !outgoing)
        return std::nullopt;
    auto point = points[index];
    auto left_in = outline_left(*incoming) * half;
    auto left_out = outline_left(*outgoing) * half;
    auto left = outline_intersect(point + left_in, point + left_in + *incoming, point + left_out,
                                  point + left_out + *outgoing)
                    .value_or(point + left_in);
    auto right = outline_intersect(point - left_in, point - left_in + *incoming, point - left_out,
                                   point - left_out + *outgoing)
                     .value_or(point - left_in);
    return std::pair<Vector3, Vector3>{left, right};
}
} // namespace
std::optional<std::vector<Vector3>> continuous_wall_outline(const std::vector<Vector3> &points, double width) {
    if (points.size() < 2 || !arch_positive(width))
        return std::nullopt;
    for (auto point : points)
        if (!finite_point(point) || std::abs(point.z) > 1e-12)
            return std::nullopt;
    auto half = width * 0.5;
    std::vector<Vector3> left, right;
    left.reserve(points.size());
    right.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        auto offset = outline_offset(points, i, half);
        if (!offset)
            return std::nullopt;
        left.push_back(offset->first);
        right.push_back(offset->second);
    }
    std::vector<Vector3> outline = std::move(left);
    outline.insert(outline.end(), right.rbegin(), right.rend());
    outline.push_back(outline.front());
    for (auto point : outline)
        if (!finite_point(point))
            return std::nullopt;
    return outline;
}

bool DxfDocument::architecture_has_class_body(const DxfRecord &record) const {
    bool after_entity = false, after_object = false;
    for (const auto &pair : record.pairs_) {
        if (pair.code == 100) {
            if (pair.value == "AcDbEntity") {
                after_entity = true;
                continue;
            }
            if (after_entity || after_object)
                return true;
            after_object = true;
            continue;
        }
        if (!after_entity && !after_object)
            continue;
        if (after_entity) {
            switch (pair.code) {
            case 6:
            case 8:
            case 48:
            case 60:
            case 62:
            case 67:
            case 92:
            case 160:
            case 310:
            case 370:
            case 420:
            case 430:
            case 440:
            case 390:
            case 347:
            case 348:
            case 349:
            case 284:
                continue;
            default:
                return true;
            }
        }
        return true;
    }
    return false;
}
bool DxfDocument::architecture_has_graphics(const DxfRecord &record) const {
    for (const auto &pair : record.pairs_)
        if (pair.code == 310 && !pair.value.empty())
            return true;
    return false;
}
std::optional<DxfRecord> DxfDocument::architecture_written_record(const DxfRecord &record,
                                                                  std::string_view section) const {
    if (!is_architecture_name(record.type()))
        return record;
    if (architecture_has_class_body(record))
        return record;
    const bool entity = section == "ENTITIES" || section == "BLOCKS";
    if (!entity)
        return std::nullopt;
    if (!architecture_has_graphics(record))
        return std::nullopt;
    DxfRecord out = record;
    std::vector<std::uint8_t> graphics;
    std::vector<DxfRecord::Pair> kept;
    kept.reserve(out.pairs_.size());
    bool after_entity = false;
    for (const auto &pair : out.pairs_) {
        if (pair.code == 0)
            kept.push_back({0, "ACAD_PROXY_ENTITY"});
        else if (pair.code == 100 && pair.value == "AcDbEntity") {
            after_entity = true;
            kept.push_back(pair);
        } else if (after_entity && (pair.code == 92 || pair.code == 160 || pair.code == 310)) {
            if (pair.code == 310) {
                auto text = binary_ ? detail::text_value_from_binary(310, pair.value) : pair.value;
                auto bytes = hex_bytes(text);
                graphics.insert(graphics.end(), bytes.begin(), bytes.end());
            }
        } else
            kept.push_back(pair);
    }
    auto encode = [&](int code, const std::string &value) {
        if (binary_ && !detail::is_string_value(code))
            return detail::binary_value_from_text(code, value);
        if (binary_ && code == 310)
            return detail::binary_value_from_text(310, value);
        return value;
    };
    std::int32_t class_id = 499;
    for (const auto &item : records_) {
        const auto &cls = std::visit([](const DxfRecord &r) -> const DxfRecord & { return r; }, item);
        if (cls.type() == "CLASS" && ascii_upper(trimmed(cls.field("", 1))) == ascii_upper(trimmed(record.type()))) {
            (void)class_id;
            break;
        }
    }
    kept.push_back({100, "AcDbProxyEntity"});
    kept.push_back({90, encode(90, "498")});
    kept.push_back({91, encode(91, std::to_string(class_id))});
    if (version_ >= DxfVersion::AC1032) {
        kept.push_back({71, encode(71, "0")});
        kept.push_back({97, encode(97, "0")});
    } else
        kept.push_back({95, encode(95, "0")});
    kept.push_back({70, encode(70, "0")});
    const int size_code = version_ >= DxfVersion::AC1024 ? 160 : 92;
    kept.push_back({size_code, encode(size_code, std::to_string(graphics.size()))});
    for (std::size_t at = 0; at < graphics.size(); at += 127) {
        std::vector<std::uint8_t> chunk(graphics.begin() + static_cast<std::ptrdiff_t>(at),
                                        graphics.begin() + static_cast<std::ptrdiff_t>(
                                            std::min(at + 127, graphics.size())));
        kept.push_back({310, encode(310, hex_text(chunk))});
    }
    const int payload_code = version_ >= DxfVersion::AC1024 ? 161 : 93;
    kept.push_back({payload_code, encode(payload_code, "0")});
    kept.push_back({94, encode(94, "0")});
    out.pairs_ = std::move(kept);
    return out;
}

std::vector<ArchitectureOmission> DxfDocument::omitted_architecture_records() const {
    if (version_ == DxfVersion::AC1009)
        return {};
    std::vector<ArchitectureOmission> omitted;
    std::string section;
    for (const auto &item : records_) {
        const auto &record = std::visit([](const DxfRecord &r) -> const DxfRecord & { return r; }, item);
        if (record.type() == "SECTION")
            section = record.field("", 2);
        else if (record.type() == "ENDSEC")
            section.clear();
        if (!is_architecture_name(record.type()))
            continue;
        if (architecture_written_record(record, section))
            continue;
        omitted.push_back({handle_of(record), record.type(),
                           section == "OBJECTS" || section == "CLASSES"
                               ? "no DXF body"
                               : "no DXF body and no proxy graphics"});
    }
    return omitted;
}
DxfRecord DxfDocument::architecture_omit_adjust(const DxfRecord &record) const {
    auto omitted = omitted_architecture_records();
    if (omitted.empty())
        return record;
    std::unordered_set<std::uint64_t> drop;
    drop.reserve(omitted.size());
    for (const auto &item : omitted)
        drop.insert(item.handle);
    DxfRecord out = record;
    auto parse_text = [](std::string_view value) {
        auto text = trimmed(value);
        if (text.empty())
            return static_cast<std::uint64_t>(0);
        try {
            return std::stoull(text, nullptr, 16);
        } catch (const std::exception &) {
            return static_cast<std::uint64_t>(0);
        }
    };
    if (out.type() == "DICTIONARY" || out.type() == "ACDBDICTIONARYWDFLT") {
        std::vector<DxfRecord::Pair> kept;
        kept.reserve(out.pairs_.size());
        for (std::size_t i = 0; i < out.pairs_.size(); ++i) {
            if (out.pairs_[i].code == 3 && i + 1 < out.pairs_.size()) {
                auto code = out.pairs_[i + 1].code;
                if ((code == 350 || code == 360) && drop.count(parse_text(out.pairs_[i + 1].value))) {
                    ++i;
                    continue;
                }
            }
            kept.push_back(out.pairs_[i]);
        }
        out.pairs_ = std::move(kept);
    }
    for (auto &pair : out.pairs_) {
        if (pair.code != 1005)
            continue;
        if (drop.count(parse_text(pair.value)))
            pair.value = "0";
    }
    return out;
}
} // namespace libopencad
