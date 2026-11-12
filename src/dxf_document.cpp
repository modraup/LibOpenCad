// SPDX-License-Identifier: MPL-2.0
// Adapted from acadrust src/io/dxf/{reader,writer} text and section modules.
#include <libopencad/dxf_document.hpp>
#include "dxf_code_page.hpp"
#include "dxf_binary.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <istream>
#include <iterator>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <unordered_set>
#include <type_traits>

namespace libopencad {
namespace {
#include "dxf_defaults.inc"

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == s.npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

int integer(std::string_view s) {
    s = trim(s);
    if (s.empty()) throw std::runtime_error("Missing DXF integer");
    if (s.front() == '+') {
        s.remove_prefix(1);
        if (s.empty() || s.front() == '-') throw std::runtime_error("Invalid DXF integer");
    }
    int result{};
    auto parsed = std::from_chars(s.data(), s.data() + s.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size())
        throw std::runtime_error("Invalid DXF integer: " + std::string(s));
    return result;
}

std::optional<std::uint64_t> parse_handle(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    std::uint64_t value{};
    const auto end = s.data() + s.size();
    const auto parsed = std::from_chars(s.data(), end, value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return value;
}

std::uint64_t handle_value(std::string_view s) {
    return parse_handle(s).value_or(0);
}

std::string hex_handle(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << value;
    return out.str();
}

bool ascii_equal(std::string_view a, std::string_view b) {
    auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [&](unsigned char x, unsigned char y) { return lower(x) == lower(y); });
}

void finite(double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Non-finite DXF coordinate or size");
}

std::string decode_text(std::string_view s) {
    std::string result;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '^' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case ' ': result += '^'; ++i; continue;
                case 'J': result += '\n'; ++i; continue;
                case 'M': result += '\r'; ++i; continue;
                case 'I': result += '\t'; ++i; continue;
            }
        }
        result += s[i];
    }
    return result;
}

std::string encode_text(std::string_view s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '^': result += "^ "; break;
            case '\n': result += "^J"; break;
            case '\r': result += "^M"; break;
            case '\t': result += "^I"; break;
            default: result += c;
        }
    }
    return result;
}
} // namespace

// Use the first matching subclass (TEXT has two AcDbText markers).
// Pre-R13 records have no markers. Application groups and XDATA are opaque.
template<class Predicate>
DxfRecord::Position DxfRecord::locate_if(std::string_view subclass, Predicate match) const {
    const bool flat = std::none_of(pairs_.begin(), pairs_.end(),
        [](const Pair& p) { return p.code == 100; });
    bool active = flat || subclass.empty();
    unsigned depth = 0;
    for (std::size_t i = 1; i < pairs_.size(); ++i) {
        const auto& p = pairs_[i];
        if (p.code == 102) {
            if (!p.value.empty() && p.value.front() == '{') ++depth;
            else if (depth && p.value == "}") --depth;
            continue;
        }
        if (depth) continue;
        if (p.code == 1001 || p.code == 101)
            return {active ? i : std::string_view::npos, false};
        if (p.code == 100) {
            if (active) return {i, false};
            active = p.value == subclass;
        } else if (active && match(i, p.code)) {
            return {i, true};
        }
    }
    return {active ? pairs_.size() : std::string_view::npos, false};
}

DxfRecord::Position DxfRecord::locate(std::string_view subclass, int code) const {
    return locate_if(subclass, [code](std::size_t, int current) { return code == current; });
}

std::string DxfRecord::field(std::string_view subclass, int code, std::string fallback) const {
    const auto pos = locate(subclass, code);
    return pos.found ? pairs_[pos.index].value : fallback;
}

void DxfRecord::set_field(std::string_view subclass, int code, std::string value) {
    const auto pos = locate(subclass, code);
    if (pos.index == std::string_view::npos)
        throw std::runtime_error("Cannot edit missing DXF subclass " + std::string(subclass));
    if (pos.found) pairs_[pos.index].value = std::move(value);
    else pairs_.insert(pairs_.begin() + pos.index, {code, std::move(value)});
}

std::string DxfRecord::text_field(std::string_view subclass, int code, std::string fallback) const {
    return decode_string(field(subclass, code, std::move(fallback)));
}

std::string DxfRecord::decode_string(std::string_view bytes) const {
    auto text = detail::from_legacy(bytes, code_page_);
    return binary_ ? text : decode_text(text);
}

std::string DxfRecord::encode_string(std::string_view value) const {
    return detail::to_legacy(binary_ ? detail::binary_text(value) : encode_text(value), code_page_);
}

void DxfRecord::set_text_field(std::string_view subclass, int code, std::string value) {
    set_field(subclass, code, encode_string(value));
}

std::string DxfRecord::type() const {
    return pairs_.empty() ? std::string{} : std::string(trim(pairs_.front().value));
}

std::string DxfRecord::handle() const {
    const int code = !pairs_.empty() && trim(pairs_.front().value) == "DIMSTYLE" ? 105 : 5;
    return std::string(trim(field("", code)));
}

std::string DxfRecord::owner_handle() const { return std::string(trim(field("", 330))); }

int DxfRecord::short_field(std::string_view subclass, int code, int fallback) const {
    const auto pos = locate(subclass, code);
    if (!pos.found) return fallback;
    const auto& bytes = pairs_[pos.index].value;
    return binary_ ? detail::binary_short(bytes) : integer(bytes);
}

void DxfRecord::set_short_field(std::string_view subclass, int code, int value) {
    set_field(subclass, code, binary_ ? detail::binary_short(value) : std::to_string(value));
}

double DxfRecord::number(std::string_view subclass, int code, double fallback) const {
    const auto pos = locate(subclass, code);
    return pos.found ? number_at(pos.index) : fallback;
}

double DxfRecord::number_at(std::size_t index) const {
    const auto& pair = pairs_[index];
    if (binary_) return detail::binary_double(pair.value);
    std::istringstream in(pair.value);
    in.imbue(std::locale::classic());
    double value{};
    if (!(in >> value) || !(in >> std::ws).eof() || !std::isfinite(value))
        throw std::runtime_error("Invalid real in DXF group " + std::to_string(pair.code));
    return value;
}

void DxfRecord::set_number(std::string_view subclass, int code, double value) {
    set_field(subclass, code, encode_number(value));
}

std::string DxfRecord::encode_number(double value) const {
    finite(value);
    if (binary_) return detail::binary_double(value);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(std::numeric_limits<double>::max_digits10);
    out << value;
    return out.str();
}

Point3 DxfRecord::point(std::string_view sub, int code) const {
    return {number(sub, code, 0), number(sub, code + 10, 0), number(sub, code + 20, 0)};
}

// Match upstream PointReader: X and Y form a point; an omitted Z is zero.
Point3 DxfRecord::point_or(std::string_view sub, int code, Point3 fallback) const {
    if (!locate(sub, code).found || !locate(sub, code + 10).found) return fallback;
    return point(sub, code);
}

void DxfRecord::set_point(std::string_view sub, int code, Point3 p) {
    finite(p.x);
    finite(p.y);
    finite(p.z);
    set_number(sub, code, p.x);
    set_number(sub, code + 10, p.y);
    set_number(sub, code + 20, p.z);
}

constexpr auto layer_record = "AcDbLayerTableRecord";
std::string DxfLayer::name() const { return text_field(layer_record, 2); }
int DxfLayer::signed_color_index() const { return short_field(layer_record, 62, 7); }
void DxfLayer::set_signed_color_index(int index) {
    if (!index || index < -255 || index > 255) throw std::invalid_argument("Invalid layer ACI");
    set_short_field(layer_record, 62, index);
}
std::string DxfEntity::layer() const { return text_field("AcDbEntity", 8, "0"); }
void DxfEntity::set_layer(std::string value) { set_text_field("AcDbEntity", 8, std::move(value)); }

Color DxfEntity::color() const {
    Color result;
    // As in the Rust reader, apply valid fields in file order (normally 62 then 420).
    locate_if("AcDbEntity", [&](std::size_t i, int code) {
        if (code == 62) {
            if (auto aci = detail::as_i16(code, pairs_[i].value, binary_))
                result = Color::from_index(*aci);
        } else if (code == 420) {
            if (auto bits = detail::as_i32_bits(pairs_[i].value, binary_))
                result = Color::from_true_color_value(*bits);
        }
        return false;
    });
    return result;
}

void DxfEntity::set_color(Color value) {
    std::vector<Pair> replacement;
    if (value != Color::by_layer() && !(version_ == DxfVersion::AC1009 && value == Color::none())) {
        const int aci = value.approximate_index();
        replacement.push_back({62, binary_ ? detail::binary_short(aci) : std::to_string(aci)});
        if (auto rgb = value.to_true_color_value(); rgb && version_ >= DxfVersion::AC1018)
            replacement.push_back({420, binary_ ? detail::binary_int32_bits(*rgb) : std::to_string(*rgb)});
    }
    replace_common_fields({62, 420}, std::move(replacement));
}

std::string DxfEntity::linetype() const {
    std::string_view name;
    locate_if("AcDbEntity", [&](std::size_t i, int code) {
        if (code == 6) name = pairs_[i].value;
        return false;
    });
    return decode_string(name);
}

void DxfEntity::set_linetype(std::string_view name) {
    // EntityCommon::has_linetype compares this exact mixed-case spelling.
    if (name.empty() || name == "ByLayer") replace_common_fields({6}, {});
    else replace_common_fields({6}, {{6, encode_string(name)}});
}

std::int16_t DxfEntity::common_short(int code, std::int16_t fallback) const {
    auto result = fallback;
    locate_if("AcDbEntity", [&](std::size_t i, int current) {
        if (current == code) {
            if (auto value = detail::as_i16(code, pairs_[i].value, binary_)) result = *value;
        }
        return false;
    });
    return result;
}

LineWeight DxfEntity::line_weight() const { return LineWeight::from_value(common_short(370, -1)); }
void DxfEntity::set_line_weight(LineWeight weight) {
    if (weight == LineWeight::by_layer() || version_ == DxfVersion::AC1009) {
        replace_common_fields({370}, {});
    } else {
        const auto value = weight.value();
        replace_common_fields({370}, {{370, binary_ ? detail::binary_short(value) : std::to_string(value)}});
    }
}

bool DxfEntity::is_invisible() const { return common_short(60, 0) != 0; }
void DxfEntity::set_invisible(bool invisible) {
    if (invisible && version_ != DxfVersion::AC1009)
        replace_common_fields({60}, {{60, binary_ ? detail::binary_short(1) : "1"}});
    else replace_common_fields({60}, {});
}

// Replace every occurrence inside AcDbEntity, retaining the order of all other pairs.
void DxfEntity::replace_common_fields(std::initializer_list<int> codes, std::vector<Pair> replacement) {
    std::vector<std::size_t> old;
    const auto end = locate_if("AcDbEntity", [&](std::size_t i, int code) {
        if (std::find(codes.begin(), codes.end(), code) != codes.end()) old.push_back(i);
        return false;
    });
    if (end.index == std::string_view::npos)
        throw std::runtime_error("Cannot edit missing DXF subclass AcDbEntity");
    if (old.empty() && replacement.empty()) return;
    const auto at = old.empty() ? end.index : old.front();
    std::vector<Pair> updated;
    updated.reserve(pairs_.size() - old.size() + replacement.size());
    updated.insert(updated.end(), pairs_.begin(), pairs_.begin() + at);
    for (auto& pair : replacement) updated.push_back(std::move(pair));
    auto next = pairs_.begin() + at;
    for (auto i : old) {
        updated.insert(updated.end(), next, pairs_.begin() + i);
        next = pairs_.begin() + i + 1;
    }
    updated.insert(updated.end(), next, pairs_.end());
    pairs_.swap(updated);
}

Point3 DxfLine::start() const { return point("AcDbLine", 10); }
Point3 DxfLine::end() const { return point("AcDbLine", 11); }
void DxfLine::set_start(Point3 p) { set_point("AcDbLine", 10, p); }
void DxfLine::set_end(Point3 p) { set_point("AcDbLine", 11, p); }
Point3 DxfCircle::center() const { return point("AcDbCircle", 10); }
double DxfCircle::radius() const { return number("AcDbCircle", 40, 1); }
void DxfCircle::set_center(Point3 p) { set_point("AcDbCircle", 10, p); }
void DxfCircle::set_radius(double r) { set_number("AcDbCircle", 40, r); }
double DxfCircle::thickness() const { return number("AcDbCircle", 39, 0); }
void DxfCircle::set_thickness(double value) { set_number("AcDbCircle", 39, value); }
Point3 DxfCircle::normal() const { return point_or("AcDbCircle", 210, {0, 0, 1}); }
void DxfCircle::set_normal(Point3 value) { set_point("AcDbCircle", 210, value); }

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double radians_per_degree = pi / 180.;
constexpr double degrees_per_radian = 180. / pi;
constexpr auto ellipse_subclass = "AcDbEllipse";
}

double DxfArc::start_angle_radians() const { return number("AcDbArc", 50, 0) * radians_per_degree; }
double DxfArc::end_angle_radians() const { return number("AcDbArc", 51, 90) * radians_per_degree; }
void DxfArc::set_start_angle_radians(double angle) { set_number("AcDbArc", 50, angle * degrees_per_radian); }
void DxfArc::set_end_angle_radians(double angle) { set_number("AcDbArc", 51, angle * degrees_per_radian); }

Point3 DxfEllipse::center() const { return point(ellipse_subclass, 10); }
Point3 DxfEllipse::major_axis() const { return point_or(ellipse_subclass, 11, {1, 0, 0}); }
Point3 DxfEllipse::normal() const { return point_or(ellipse_subclass, 210, {0, 0, 1}); }
double DxfEllipse::minor_axis_ratio() const { return number(ellipse_subclass, 40, 0.5); }
double DxfEllipse::start_parameter() const { return number(ellipse_subclass, 41, 0); }
double DxfEllipse::end_parameter() const { return number(ellipse_subclass, 42, 2 * pi); }
void DxfEllipse::set_center(Point3 value) { set_point(ellipse_subclass, 10, value); }
void DxfEllipse::set_major_axis(Point3 value) { set_point(ellipse_subclass, 11, value); }
void DxfEllipse::set_normal(Point3 value) { set_point(ellipse_subclass, 210, value); }
void DxfEllipse::set_minor_axis_ratio(double value) { set_number(ellipse_subclass, 40, value); }
void DxfEllipse::set_start_parameter(double value) { set_number(ellipse_subclass, 41, value); }
void DxfEllipse::set_end_parameter(double value) { set_number(ellipse_subclass, 42, value); }

namespace {
constexpr auto heavy_polyline = "AcDb2dPolyline";
constexpr auto heavy_vertex = "AcDb2dVertex";
constexpr auto polyline3d = "AcDb3dPolyline";
constexpr auto vertex3d = "AcDb3dPolylineVertex";
constexpr auto polygon_mesh = "AcDbPolygonMesh";
constexpr auto mesh_vertex = "AcDbPolygonMeshVertex";
constexpr auto polyface_mesh = "AcDbPolyFaceMesh";
constexpr auto polyface_vertex = "AcDbPolyFaceMeshVertex";
constexpr auto face_record = "AcDbFaceRecord";

template<class Record>
auto sequence(Record& record) {
    using Sequence = std::conditional_t<std::is_const_v<Record>, const DxfVertexSequence, DxfVertexSequence>;
    return std::visit([](auto& item) -> Sequence* {
        if constexpr (std::is_base_of_v<DxfVertexSequence, std::decay_t<decltype(item)>>)
            return &item;
        return nullptr;
    }, record);
}
}

bool DxfPolyline3D::is_closed() const { return (short_field(polyline3d, 70, 0) & 1) != 0; }

PolylineVertex3D DxfVertexSequence::spatial_vertex(std::size_t index, std::string_view subclass) const {
    const auto& v = vertices_.at(index);
    return {v.point(subclass, 10), v.short_field(subclass, 70, 0),
            v.text_field("AcDbEntity", 8, "0"), v.handle(), v.owner_handle()};
}

void DxfVertexSequence::set_spatial_vertex(std::size_t index, std::string_view subclass, Point3 position) {
    vertices_.at(index).set_point(subclass, 10, position);
}

PolylineVertex3D DxfPolyline3D::vertex(std::size_t index) const {
    return spatial_vertex(index, vertex3d);
}

void DxfPolyline3D::set_vertex_position(std::size_t index, Point3 position) {
    set_spatial_vertex(index, vertex3d, position);
}

int DxfPolygonMesh::m_vertex_count() const { return short_field(polygon_mesh, 71, 0); }
int DxfPolygonMesh::n_vertex_count() const { return short_field(polygon_mesh, 72, 0); }
bool DxfPolygonMesh::is_closed_m() const { return (short_field(polygon_mesh, 70, 0) & 1) != 0; }
bool DxfPolygonMesh::is_closed_n() const { return (short_field(polygon_mesh, 70, 0) & 32) != 0; }

std::size_t DxfPolygonMesh::grid_index(std::size_t m, std::size_t n) const {
    const int rows = m_vertex_count(), cols = n_vertex_count();
    if (rows <= 0 || cols <= 0 || m >= static_cast<std::size_t>(rows) ||
        n >= static_cast<std::size_t>(cols))
        throw std::out_of_range("DXF polygon mesh grid coordinate outside declared dimensions");
    const auto stride = static_cast<std::size_t>(cols);
    if (n >= vertex_count() || m > (vertex_count() - 1 - n) / stride)
        throw std::out_of_range("DXF polygon mesh grid vertex missing");
    return m * stride + n;
}

PolylineVertex3D DxfPolygonMesh::vertex(std::size_t index) const {
    return spatial_vertex(index, mesh_vertex);
}

void DxfPolygonMesh::set_vertex_position(std::size_t index, Point3 position) {
    set_spatial_vertex(index, mesh_vertex, position);
}

bool PolyfaceFace::is_triangle() const noexcept {
    return indices[3] == 0 || indices[3] == indices[2] ||
           static_cast<long long>(indices[3]) == -static_cast<long long>(indices[2]);
}

void DxfPolyfaceMesh::index_children() {
    for (std::size_t i = 0; i < vertices_.size(); ++i) {
        const auto& child = vertices_[i];
        if (child.short_field(polyface_vertex, 70, 0) & 64)
            coordinate_records_.push_back(i);
        else if (child.short_field(face_record, 70, 0) & 128)
            face_records_.push_back(i);
    }
}

PolylineVertex3D DxfPolyfaceMesh::vertex(std::size_t index) const {
    return spatial_vertex(coordinate_records_.at(index), polyface_vertex);
}

PolyfaceFace DxfPolyfaceMesh::face(std::size_t index) const {
    const auto& child = vertices_[face_records_.at(index)];
    return {{child.short_field(face_record, 71, 0), child.short_field(face_record, 72, 0),
             child.short_field(face_record, 73, 0), child.short_field(face_record, 74, 0)},
            child.text_field("AcDbEntity", 8, "0"), child.handle(), child.owner_handle()};
}

void DxfPolyfaceMesh::set_face_indices(std::size_t index, std::array<int, 4> indices) {
    auto& record = vertices_[face_records_.at(index)];
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const int ref = indices[i];
        if ((ref == 0 && i < 3) || ref < -32767 || ref > 32767 ||
            static_cast<std::size_t>(std::abs(ref)) > vertex_count())
            throw std::invalid_argument("DXF polyface reference must identify an existing vertex");
    }
    DxfRecord edited = record;
    for (int i = 0; i < 4; ++i) {
        const int code = 71 + i;
        if (indices[i] == edited.short_field(face_record, code, 0)) continue;
        if (indices[i] == 0) {
            // Match the upstream writer: omit group 74 on a three-reference face.
            edited.pairs_.erase(edited.pairs_.begin() + edited.locate(face_record, code).index);
        } else {
            edited.set_short_field(face_record, code, indices[i]);
        }
    }
    record = std::move(edited);
}

bool DxfPolyline2D::is_closed() const { return (short_field(heavy_polyline, 70, 0) & 1) != 0; }
double DxfPolyline2D::default_start_width() const { return number(heavy_polyline, 40, 0); }
double DxfPolyline2D::default_end_width() const { return number(heavy_polyline, 41, 0); }

PolylineVertex2D DxfPolyline2D::vertex(std::size_t index) const {
    const auto& v = vertices_.at(index);
    return {v.point(heavy_vertex, 10), v.number(heavy_vertex, 40, default_start_width()),
            v.number(heavy_vertex, 41, default_end_width()), v.number(heavy_vertex, 42, 0),
            v.handle(), v.owner_handle()};
}

void DxfPolyline2D::set_vertex_position(std::size_t index, double x, double y) {
    auto& v = vertices_.at(index);
    finite(x);
    finite(y);
    v.set_number(heavy_vertex, 10, x);
    v.set_number(heavy_vertex, 20, y);
}

void DxfPolyline2D::set_vertex_widths(std::size_t index, double start, double end) {
    auto& v = vertices_.at(index);
    finite(start);
    finite(end);
    v.set_number(heavy_vertex, 40, start);
    v.set_number(heavy_vertex, 41, end);
}

void DxfPolyline2D::set_bulge(std::size_t index, double value) {
    vertices_.at(index).set_number(heavy_vertex, 42, value);
}

namespace {
constexpr auto polyline_subclass = "AcDbPolyline";
}

// Upstream completes a vertex on group 20. Optional fields belong to the
// most recently completed vertex; their absence never shifts the next bulge.
std::vector<DxfLwPolyline::VertexFields> DxfLwPolyline::vertex_fields() const {
    std::vector<VertexFields> fields;
    std::size_t x = missing;
    locate_if(polyline_subclass, [&](std::size_t index, int code) {
        if (code == 10) {
            x = index;
        } else if (code == 20 && x != missing) {
            fields.push_back({x, index});
            x = missing;
        } else if (!fields.empty()) {
            auto& vertex = fields.back();
            switch (code) {
                case 40: vertex.start_width = index; break;
                case 41: vertex.end_width = index; break;
                case 42: vertex.bulge = index; break;
                case 91: vertex.id = index; break;
            }
        }
        return false;
    });
    return fields;
}

std::vector<LwVertex> DxfLwPolyline::vertices() const {
    const auto fields = vertex_fields();
    std::vector<LwVertex> vertices;
    vertices.reserve(fields.size());
    auto optional_number = [&](std::size_t i) { return i == missing ? 0.0 : number_at(i); };
    for (const auto& v : fields) {
        int id = 0;
        if (v.id != missing) {
            const auto& bytes = pairs_[v.id].value;
            id = binary_ ? detail::binary_int32(bytes) : integer(bytes);
        }
        vertices.push_back({number_at(v.x), number_at(v.y), optional_number(v.bulge),
                            optional_number(v.start_width), optional_number(v.end_width), id});
    }
    return vertices;
}

void DxfLwPolyline::set_vertex_position(std::size_t index, double x, double y) {
    const auto v = vertex_fields().at(index);
    auto new_x = encode_number(x);
    auto new_y = encode_number(y);
    pairs_[v.x].value = std::move(new_x);
    pairs_[v.y].value = std::move(new_y);
}

void DxfLwPolyline::set_bulge(std::size_t index, double value) {
    const auto v = vertex_fields().at(index);
    if (v.bulge != missing) pairs_[v.bulge].value = encode_number(value);
    else if (value != 0) pairs_.insert(pairs_.begin() + v.y + 1, {42, encode_number(value)});
}

bool DxfLwPolyline::is_closed() const { return (short_field(polyline_subclass, 70, 0) & 1) != 0; }
void DxfLwPolyline::set_closed(bool closed) {
    const int flags = short_field(polyline_subclass, 70, 0);
    set_short_field(polyline_subclass, 70, closed ? flags | 1 : flags & ~1);
}
bool DxfLwPolyline::plinegen() const { return (short_field(polyline_subclass, 70, 0) & 128) != 0; }
double DxfLwPolyline::elevation() const { return number(polyline_subclass, 38, 0); }
double DxfLwPolyline::thickness() const { return number(polyline_subclass, 39, 0); }
double DxfLwPolyline::constant_width() const { return number(polyline_subclass, 43, 0); }
Point3 DxfLwPolyline::normal() const { return point_or(polyline_subclass, 210, {0,0,1}); }

Point3 DxfText::insertion_point() const { return point("AcDbText", 10); }
double DxfText::height() const { return number("AcDbText", 40, 1); }
std::string DxfText::text() const { return text_field("AcDbText", 1); }
void DxfText::set_insertion_point(Point3 p) { set_point("AcDbText", 10, p); }
void DxfText::set_height(double h) { set_number("AcDbText", 40, h); }
void DxfText::set_text(std::string s) { set_text_field("AcDbText", 1, std::move(s)); }

// acadrust tables/block_record.rs and document.rs::entities_in_block.
std::string DxfBlockRecord::name() const { return text_field("AcDbBlockTableRecord", 2); }
bool DxfBlockRecord::is_model_space() const { return ascii_equal(name(), "*Model_Space"); }
bool DxfBlockRecord::is_paper_space() const {
    constexpr std::string_view prefix = "*Paper_Space";
    return ascii_equal(name().substr(0, prefix.size()), prefix);
}

// Store record locations so copying a document never retains pointers into its source.
const DxfRecord& DxfDocument::record_at(const RecordLocation& location) const {
    const auto& record = records_[location.record];
    if (location.vertex) return sequence(record)->vertices_[*location.vertex];
    if (location.seqend) return *sequence(record)->seqend_;
    return std::visit([](const DxfRecord& item) -> const DxfRecord& { return item; }, record);
}

const DxfRecord* DxfDocument::get_record(std::string_view handle) const {
    const auto found = handle_index_.find(handle_value(handle));
    return found == handle_index_.end() ? nullptr : &record_at(found->second.back());
}

std::vector<const DxfRecord*> DxfDocument::records_with_handle(std::string_view handle) const {
    std::vector<const DxfRecord*> result;
    const auto found = handle_index_.find(handle_value(handle));
    if (found != handle_index_.end()) {
        for (const auto& location : found->second) result.push_back(&record_at(location));
    }
    return result;
}

std::vector<std::uint64_t> DxfDocument::duplicate_handles() const {
    std::vector<std::uint64_t> result;
    for (const auto& entry : handle_index_) {
        if (entry.second.size() > 1) result.push_back(entry.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

// acadrust document.rs::object_owner and audit.rs owner reference checks.
// The chain result also reports ambiguity and cycles across raw record types.
OwnershipChain DxfDocument::ownership_chain(std::string_view handle) const {
    OwnershipChain result{{}, OwnershipStatus::InvalidHandle, std::string(handle)};
    std::unordered_set<std::uint64_t> visited;
    while (true) {
        const auto value = parse_handle(result.reference);
        if (!value || (!*value && result.records.empty())) {
            result.status = OwnershipStatus::InvalidHandle;
            return result;
        }
        if (!*value) {
            result.status = OwnershipStatus::Root;
            return result;
        }
        const auto found = handle_index_.find(*value);
        if (found == handle_index_.end()) {
            result.status = OwnershipStatus::DanglingReference;
            return result;
        }
        if (found->second.size() > 1) {
            result.status = OwnershipStatus::AmbiguousHandle;
            return result;
        }
        if (!visited.insert(*value).second) {
            result.status = OwnershipStatus::Cycle;
            return result;
        }
        const auto& record = record_at(found->second.front());
        result.records.push_back(&record);
        const auto owner = record.locate("", 330);
        if (!owner.found) {
            result.status = OwnershipStatus::MissingOwner;
            result.reference.clear();
            return result;
        }
        result.reference = record.pairs_[owner.index].value;
    }
}

const DxfBlockRecord* DxfDocument::block_record(std::string_view owner_handle) const {
    const auto found = handle_index_.find(handle_value(owner_handle));
    if (found == handle_index_.end()) return nullptr;
    for (const auto& location : found->second) {
        if (location.vertex || location.seqend) continue;
        if (const auto* block = std::get_if<DxfBlockRecord>(&records_[location.record]))
            return block;
    }
    return nullptr;
}

std::vector<std::string> DxfDocument::entity_handles_owned_by(
        const std::vector<std::uint64_t>& owners) const {
    std::vector<std::string> result;
    for (auto index : entity_records_) {
        std::visit([&](const DxfRecord& entity) {
            const auto owner = handle_value(entity.owner_handle());
            if (owner && std::find(owners.begin(), owners.end(), owner) != owners.end() &&
                handle_value(entity.handle()))
                result.push_back(entity.handle());
        }, records_[index]);
    }
    return result;
}

std::vector<std::string> DxfDocument::entity_handles_in_block(std::string_view name) const {
    std::vector<std::uint64_t> owners;
    for_each<DxfBlockRecord>([&](const auto& block) {
        if (ascii_equal(block.name(), name)) owners.push_back(handle_value(block.handle()));
    });
    return entity_handles_owned_by(owners);
}

std::vector<std::string> DxfDocument::model_space_entity_handles() const {
    return entity_handles_in_block("*Model_Space");
}

std::vector<std::string> DxfDocument::paper_space_entity_handles() const {
    std::vector<std::uint64_t> owners;
    for_each<DxfBlockRecord>([&](const auto& block) {
        if (block.is_paper_space()) owners.push_back(handle_value(block.handle()));
    });
    return entity_handles_owned_by(owners);
}

std::string DxfLineType::name() const { return text_field("AcDbLinetypeTableRecord", 2, "Continuous"); }
std::string DxfLineType::description() const { return text_field("AcDbLinetypeTableRecord", 3); }
double DxfLineType::pattern_length() const { return number("AcDbLinetypeTableRecord", 40, 0); }
bool DxfLineType::xref_dependent() const { return (short_field("AcDbLinetypeTableRecord", 70, 0) & 16) != 0; }

std::vector<LineTypeElement> DxfLineType::elements() const {
    std::vector<LineTypeElement> result;
    locate_if("AcDbLinetypeTableRecord", [&](std::size_t index, int code) {
        const auto& value = pairs_[index].value;
        if (code == 49) {
            result.push_back({number_at(index), std::nullopt});
            return false;
        }
        if (result.empty()) return false;
        auto& complex = result.back().complex;
        if (code == 74) {
            const int flags = binary_ ? detail::binary_short(value) : integer(value);
            if (flags != 0) {
                if (!complex) complex.emplace();
                complex->source_flags = static_cast<std::uint8_t>(flags);
                complex->absolute_rotation = (flags & 1) != 0;
                complex->upright = (flags & 8) != 0;
                if (flags & 4) {
                    if (complex->content != LineTypeComplexData::Content::Shape) complex->shape_number = 0;
                    complex->content = LineTypeComplexData::Content::Shape;
                } else if (flags & 2) {
                    if (complex->content != LineTypeComplexData::Content::Text) complex->text.clear();
                    complex->content = LineTypeComplexData::Content::Text;
                }
            }
        }
        if (!complex) return false;
        switch (code) {
            case 9:
                complex->content = LineTypeComplexData::Content::Text;
                complex->text = decode_string(value);
                break;
            case 75:
                complex->content = LineTypeComplexData::Content::Shape;
                complex->shape_number = binary_ ? detail::binary_short(value) : integer(value);
                break;
            case 340: complex->style_handle = handle_value(value); break;
            case 44: complex->offset[0] = number_at(index); break;
            case 45: complex->offset[1] = number_at(index); break;
            case 46: complex->scale = number_at(index); break;
            case 50: complex->rotation = number_at(index) * (std::acos(-1.0) / 180.0); break;
            default: break;
        }
        return false;
    });
    return result;
}

const DxfLayer* DxfDocument::get_layer(std::string_view name) const {
    const auto key = detail::symbol_name_key(name);
    const DxfLayer* result = nullptr;
    for_each<DxfLayer>([&](const auto& layer) {
        if (detail::symbol_name_key(layer.name()) == key) result = &layer;
    });
    return result;
}

const DxfLineType* DxfDocument::get_linetype(std::string_view name) const {
    const auto key = detail::symbol_name_key(name);
    const DxfLineType* result = nullptr;
    for_each<DxfLineType>([&](const auto& linetype) {
        if (detail::symbol_name_key(linetype.name()) == key) result = &linetype;
    });
    return result;
}

DxfRecord DxfDocument::make_record(std::string type, std::string subclass) const {
    DxfRecord record;
    record.binary_ = binary_;
    if (!records_.empty())
        record.code_page_ = std::visit([](const DxfRecord& item) { return item.code_page_; }, records_.front());
    record.pairs_.push_back({0, std::move(type)});
    if (version_ != DxfVersion::AC1009 && !subclass.empty()) {
        record.pairs_.push_back({100, "AcDbSymbolTableRecord"});
        record.pairs_.push_back({100, std::move(subclass)});
    }
    return record;
}

void DxfDocument::rebuild_indexes() {
    handle_index_.clear();
    entity_records_.clear();
    header_index_.reset();
    std::string section;
    auto include = [&](const DxfRecord& item, RecordLocation location) {
        const auto handle = handle_value(item.handle());
        if (!handle) return;
        handle_index_[handle].push_back(location);
        const auto limit = std::numeric_limits<std::uint64_t>::max();
        next_handle_ = std::max(next_handle_, handle == limit ? limit : handle + 1);
    };
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto& item = record_at({i, std::nullopt, false});
        const auto type = item.type();
        if (type == "SECTION") {
            section = trim(item.field("", 2));
            if (section == "HEADER") header_index_ = i;
            continue;
        }
        if (type == "ENDSEC") section.clear();
        if (type == "ENDTAB") continue;
        if (section != "TABLES" && section != "BLOCKS" && section != "ENTITIES" && section != "OBJECTS")
            continue;
        include(item, {i, std::nullopt, false});
        if ((section == "BLOCKS" || section == "ENTITIES") &&
            type != "BLOCK" && type != "ENDBLK" && type != "VERTEX" && type != "SEQEND" && type != "ATTRIB")
            entity_records_.push_back(i);
        if (const auto* sequence_record = sequence(records_[i])) {
            for (std::size_t j = 0; j < sequence_record->vertices_.size(); ++j)
                include(sequence_record->vertices_[j], {i, j, false});
            if (sequence_record->seqend_) include(*sequence_record->seqend_, {i, std::nullopt, true});
        }
    }
}

// Called on a staged copy. Rebuild only after the entire structural edit succeeds.
std::uint64_t DxfDocument::insert_table_entry(std::string_view table_name, Record entry) {
    std::optional<std::size_t> table_start, table_end, tables_end;
    std::string section;
    bool matching = false;
    std::size_t count = 0;
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto& record = record_at({i, std::nullopt, false});
        const auto type = record.type();
        if (type == "SECTION") section = trim(record.field("", 2));
        if (section != "TABLES") continue;
        if (type == "ENDSEC") { tables_end = i; section.clear(); }
        if (type == "TABLE") {
            matching = trim(record.field("", 2)) == table_name;
            if (matching) {
                if (table_start) throw std::runtime_error("Multiple DXF tables with the same name");
                table_start = i;
            }
        } else if (matching && type == "ENDTAB") {
            table_end = i;
            matching = false;
        } else if (matching && type == table_name) ++count;
    }
    if (count >= 32767) throw std::overflow_error("DXF table count exceeds group 70 range");
    DxfRecord header = table_start ? record_at({*table_start, std::nullopt, false}) : make_record("TABLE");
    auto owner = handle_value(header.handle());
    if (owner && records_with_handle(header.handle()).size() != 1)
        throw std::runtime_error("Ambiguous DXF table handle");
    if (!owner) owner = allocate_handle();
    header.set_field("", 5, hex_handle(owner));
    if (!table_start) {
        header.set_field("", 2, std::string(table_name));
        if (version_ != DxfVersion::AC1009) {
            header.set_field("", 330, "0");
            header.pairs_.push_back({100, "AcDbSymbolTable"});
        }
    }
    header.set_short_field("AcDbSymbolTable", 70, static_cast<int>(count + 1));
    if (!table_start && table_name == "DIMSTYLE" && version_ != DxfVersion::AC1009) {
        header.pairs_.push_back({100, "AcDbDimStyleTable"});
        header.set_short_field("AcDbDimStyleTable", 71, 1);
    }
    const auto handle = allocate_handle();
    std::visit([&](DxfRecord& record) {
        record.set_field("", record.type() == "DIMSTYLE" ? 105 : 5, hex_handle(handle));
        if (version_ != DxfVersion::AC1009) record.set_field("", 330, hex_handle(owner));
    }, entry);
    if (table_start) {
        if (!table_end) throw std::runtime_error("Missing DXF table terminator");
        records_[*table_start] = std::move(header);
        records_.insert(records_.begin() + *table_end, std::move(entry));
    } else {
        std::vector<Record> added;
        std::size_t position = tables_end.value_or(records_.size());
        if (!tables_end) {
            // Put TABLES before drawing content, following HEADER/CLASSES when present.
            for (std::size_t i = 0; i < records_.size(); ++i) {
                const auto& record = record_at({i, std::nullopt, false});
                const auto name = record.field("", 2);
                if (record.type() == "EOF" || (record.type() == "SECTION" &&
                    name != "HEADER" && name != "CLASSES")) { position = i; break; }
            }
            auto start = make_record("SECTION");
            start.pairs_.push_back({2, "TABLES"});
            added.emplace_back(std::move(start));
        }
        added.emplace_back(std::move(header));
        added.push_back(std::move(entry));
        added.emplace_back(make_record("ENDTAB"));
        if (!tables_end) added.emplace_back(make_record("ENDSEC"));
        records_.insert(records_.begin() + position,
            std::make_move_iterator(added.begin()), std::make_move_iterator(added.end()));
    }
    rebuild_indexes();
    return handle;
}

std::uint64_t DxfDocument::add_layer(std::string name, int color, std::string linetype) {
    if (get_layer(name)) throw std::invalid_argument("Layer name already exists");
    DxfDocument staged = *this;
    if (!staged.get_linetype(linetype)) {
        if (!ascii_equal(linetype, "Continuous"))
            throw std::invalid_argument("Layer linetype does not exist");
        staged.add_linetype({"Continuous", "Solid line", {}, 0, false});
    }
    DxfLayer layer;
    static_cast<DxfRecord&>(layer) = staged.make_record("LAYER", layer_record);
    layer.set_text_field(layer_record, 2, std::move(name));
    layer.set_short_field(layer_record, 70, 0);
    layer.set_signed_color_index(color);
    layer.set_text_field(layer_record, 6, std::move(linetype));
    if (version_ != DxfVersion::AC1009) {
        layer.set_short_field(layer_record, 370, -3);
        layer.pairs_.push_back({290, binary_ ? std::string(1, char(1)) : "1"});
        if (const auto* zero = get_layer("0")) {
            const auto plotstyle = zero->field(layer_record, 390);
            if (!plotstyle.empty()) layer.set_field(layer_record, 390, plotstyle);
        }
    }
    const auto handle = staged.insert_table_entry("LAYER", std::move(layer));
    *this = std::move(staged);
    return handle;
}

std::uint64_t DxfDocument::add_linetype(const LineTypeDefinition& definition) {
    if (get_linetype(definition.name)) throw std::invalid_argument("Linetype name already exists");
    if (definition.elements.size() > 32767) throw std::overflow_error("Too many linetype elements");
    DxfLineType linetype;
    static_cast<DxfRecord&>(linetype) = make_record("LTYPE", "AcDbLinetypeTableRecord");
    const auto sub = "AcDbLinetypeTableRecord";
    linetype.set_text_field(sub, 2, definition.name);
    linetype.set_short_field(sub, 70, definition.xref_dependent ? 16 : 0);
    linetype.set_text_field(sub, 3, definition.description);
    linetype.set_short_field(sub, 72, 65);
    linetype.set_short_field(sub, 73, static_cast<int>(definition.elements.size()));
    linetype.set_number(sub, 40, definition.pattern_length);
    auto number = [&](int code, double value) { finite(value); linetype.pairs_.push_back({code, linetype.encode_number(value)}); };
    auto short_value = [&](int code, int value) {
        if (value < -32768 || value > 32767) throw std::invalid_argument("Invalid linetype short value");
        linetype.pairs_.push_back({code, binary_ ? detail::binary_short(value) : std::to_string(value)});
    };
    for (const auto& element : definition.elements) {
        number(49, element.length);
        if (!element.complex) { short_value(74, 0); continue; }
        const auto& c = *element.complex;
        int flags = c.source_flags & ~15;
        if (c.absolute_rotation) flags |= 1;
        if (c.upright && version_ >= DxfVersion::AC1027) flags |= 8;
        const bool text = c.content == LineTypeComplexData::Content::Text;
        flags |= text ? 2 : 4;
        short_value(74, flags);
        short_value(75, text ? 0 : c.shape_number);
        auto style = c.style_handle;
        if (!style) {
            // Same fallback as the Rust writer: Standard, then the first valid style.
            for (const auto& record : records_) {
                const auto* raw = std::get_if<DxfRecord>(&record);
                if (!raw || raw->type() != "STYLE" || !handle_value(raw->handle())) continue;
                if (!style) style = handle_value(raw->handle());
                if (detail::symbol_name_key(raw->text_field("AcDbTextStyleTableRecord", 2)) ==
                    detail::symbol_name_key("Standard")) { style = handle_value(raw->handle()); break; }
            }
        }
        linetype.pairs_.push_back({340, hex_handle(style)});
        number(46, c.scale);
        number(50, c.rotation * (180.0 / std::acos(-1.0)));
        number(44, c.offset[0]);
        number(45, c.offset[1]);
        if (text && !c.text.empty()) linetype.pairs_.push_back({9, linetype.encode_string(c.text)});
    }
    DxfDocument staged = *this;
    const auto handle = staged.insert_table_entry("LTYPE", std::move(linetype));
    *this = std::move(staged);
    return handle;
}

DxfEntity DxfDocument::make_entity(std::string type, std::string subclass,
                                  const EntityOptions& options) const {
    const auto* layer = get_layer(options.layer);
    if (!layer) throw std::invalid_argument("Entity layer does not exist");
    const auto* linetype = options.linetype.empty() ? nullptr : get_linetype(options.linetype);
    if (!options.linetype.empty() && !linetype)
        throw std::invalid_argument("Entity linetype does not exist");
    DxfEntity entity;
    static_cast<DxfRecord&>(entity) = make_record(std::move(type));
    entity.version_ = version_;
    if (version_ != DxfVersion::AC1009) {
        entity.pairs_.push_back({100, "AcDbEntity"});
        entity.pairs_.push_back({100, std::move(subclass)});
    }
    entity.set_layer(layer->name());
    entity.set_linetype(linetype ? linetype->name() : "");
    entity.set_color(options.color);
    entity.set_line_weight(options.line_weight);
    entity.set_invisible(options.invisible);
    return entity;
}

std::uint64_t DxfDocument::insert_entity(Record entry, EntitySpace space) {
    if (space != EntitySpace::Model && space != EntitySpace::Paper)
        throw std::invalid_argument("Invalid entity space");
    const auto name = space == EntitySpace::Model ? "*Model_Space" : "*Paper_Space";
    const DxfBlockRecord* owner = nullptr;
    for_each<DxfBlockRecord>([&](const auto& block) {
        if (!ascii_equal(block.name(), name)) return;
        if (owner) throw std::invalid_argument("Ambiguous space block name");
        owner = &block;
    });
    if (!owner || !handle_value(owner->handle()) || records_with_handle(owner->handle()).size() != 1)
        throw std::invalid_argument("Missing or ambiguous space block handle");
    const auto owner_handle = owner->handle();

    std::optional<std::size_t> end;
    bool inside = false, seen = false;
    std::size_t insertion = records_.size();
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto& record = record_at({i, std::nullopt, false});
        if (record.type() == "SECTION") {
            const std::string section(trim(record.field("", 2)));
            if (inside) throw std::invalid_argument("Unterminated ENTITIES section");
            if (section == "ENTITIES") {
                if (seen) throw std::invalid_argument("Multiple ENTITIES sections");
                inside = seen = true;
            }
            if (section == "OBJECTS") insertion = std::min(insertion, i);
        } else if (record.type() == "ENDSEC" && inside) {
            end = i;
            inside = false;
        } else if (record.type() == "EOF") insertion = std::min(insertion, i);
    }
    if (inside) throw std::invalid_argument("Unterminated ENTITIES section");

    // Commit together: allocation, ownership, insertion and index rebuilding.
    DxfDocument staged = *this;
    const auto handle = staged.allocate_handle();
    std::visit([&](DxfRecord& record) {
        record.set_field("", 5, hex_handle(handle));
        record.set_field("", 330, owner_handle);
        if (space == EntitySpace::Paper) record.set_short_field("AcDbEntity", 67, 1);
    }, entry);
    if (auto* compound = sequence(entry)) {
        auto assign = [&](DxfRecord& child) {
            // Existing copy paths remap populated identities separately.
            if (!child.handle().empty()) return;
            child.set_field("", 5, hex_handle(staged.allocate_handle()));
            child.set_field("", 330, hex_handle(handle));
            if (space == EntitySpace::Paper) child.set_short_field("AcDbEntity", 67, 1);
        };
        for (auto& child : compound->vertices_) assign(child);
        if (compound->seqend_) assign(*compound->seqend_);
    }
    if (end) {
        staged.records_.insert(staged.records_.begin() + *end, std::move(entry));
    } else {
        auto section = staged.make_record("SECTION");
        section.set_field("", 2, "ENTITIES");
        std::vector<Record> added;
        added.emplace_back(std::move(section));
        added.emplace_back(std::move(entry));
        added.emplace_back(staged.make_record("ENDSEC"));
        staged.records_.insert(staged.records_.begin() + insertion,
            std::make_move_iterator(added.begin()), std::make_move_iterator(added.end()));
    }
    staged.rebuild_indexes();
    *this = std::move(staged);
    return handle;
}

std::uint64_t DxfDocument::add_line(Point3 start, Point3 end, const EntityOptions& options) {
    DxfLine line;
    static_cast<DxfEntity&>(line) = make_entity("LINE", "AcDbLine", options);
    line.set_start(start);
    line.set_end(end);
    return insert_entity(std::move(line), options.space);
}

std::uint64_t DxfDocument::add_circle(Point3 center, double radius, const EntityOptions& options) {
    DxfCircle circle;
    static_cast<DxfEntity&>(circle) = make_entity("CIRCLE", "AcDbCircle", options);
    circle.set_center(center);
    circle.set_radius(radius);
    return insert_entity(std::move(circle), options.space);
}

// Ported from entities/{arc,ellipse,lwpolyline,polyline2d,polyline3d,polygon_mesh,polyface_mesh}
// and io/dxf/writer/section_writer.rs. Traditional sequences retain their original DXF type.
std::uint64_t DxfDocument::add_arc(Point3 center, double radius, double start, double end,
                                  const EntityOptions& options) {
    DxfArc arc;
    static_cast<DxfEntity&>(arc) = make_entity("ARC", "AcDbCircle", options);
    arc.set_center(center);
    arc.set_radius(radius);
    if (version_ != DxfVersion::AC1009) arc.pairs_.push_back({100, "AcDbArc"});
    arc.set_start_angle_radians(start);
    arc.set_end_angle_radians(end);
    return insert_entity(std::move(arc), options.space);
}

std::uint64_t DxfDocument::add_ellipse(Point3 center, Point3 major, double ratio,
                                      double start, double end, Point3 normal,
                                      const EntityOptions& options) {
    if (version_ == DxfVersion::AC1009) throw std::invalid_argument("ELLIPSE requires R13 or later");
    DxfEllipse ellipse;
    static_cast<DxfEntity&>(ellipse) = make_entity("ELLIPSE", "AcDbEllipse", options);
    ellipse.set_center(center);
    ellipse.set_major_axis(major);
    ellipse.set_minor_axis_ratio(ratio);
    ellipse.set_start_parameter(start);
    ellipse.set_end_parameter(end);
    ellipse.set_normal(normal);
    return insert_entity(std::move(ellipse), options.space);
}

std::uint64_t DxfDocument::add_lwpolyline(const std::vector<LwVertex>& vertices, bool closed,
                                         const EntityOptions& options) {
    if (version_ == DxfVersion::AC1009 || version_ == DxfVersion::AC1012)
        throw std::invalid_argument("LWPOLYLINE requires R14 or later");
    if (vertices.size() > INT32_MAX) throw std::overflow_error("Too many lightweight vertices");
    DxfLwPolyline polyline;
    static_cast<DxfEntity&>(polyline) = make_entity("LWPOLYLINE", "AcDbPolyline", options);
    polyline.set_field("AcDbPolyline", 90, binary_ ? detail::binary_int32_bits(vertices.size()) : std::to_string(vertices.size()));
    polyline.set_closed(closed);
    for (const auto& v : vertices) {
        for (const auto& pair : std::initializer_list<std::pair<int, double>>{
             {10,v.x}, {20,v.y}, {40,v.start_width}, {41,v.end_width}, {42,v.bulge}})
            polyline.pairs_.push_back({pair.first, polyline.encode_number(pair.second)});
        polyline.pairs_.push_back({91, binary_ ? detail::binary_int32_bits(v.vertex_id) : std::to_string(v.vertex_id)});
    }
    return insert_entity(std::move(polyline), options.space);
}

void DxfDocument::initialize_sequence(DxfVertexSequence& entity, std::string_view subclass,
    int flags, const std::vector<Point3>& vertices, std::string_view vertex_subclass,
    int vertex_flags, const EntityOptions& options) const {
    static_cast<DxfEntity&>(entity) = make_entity("POLYLINE", std::string(subclass), options);
    entity.set_short_field(subclass, 66, 1);
    entity.set_point(subclass, 10, {0,0,0});
    entity.set_short_field(subclass, 70, flags);
    for (const auto& p : vertices) {
        DxfRecord child = make_entity("VERTEX", "AcDbVertex", options);
        if (version_ != DxfVersion::AC1009) child.pairs_.push_back({100, std::string(vertex_subclass)});
        child.set_point(vertex_subclass, 10, p);
        child.set_short_field(vertex_subclass, 70, vertex_flags);
        entity.vertices_.push_back(std::move(child));
    }
    auto end = make_record("SEQEND");
    if (version_ != DxfVersion::AC1009) end.pairs_.push_back({100, "AcDbEntity"});
    end.set_text_field("AcDbEntity", 8, entity.layer());
    entity.seqend_ = std::move(end);
}

std::uint64_t DxfDocument::add_polyline2d(const std::vector<LwVertex>& vertices, bool closed,
                                         double elevation, const EntityOptions& options) {
    std::vector<Point3> points;
    for (const auto& v : vertices) points.push_back({v.x,v.y,0});
    DxfPolyline2D polyline;
    initialize_sequence(polyline, heavy_polyline, closed ? 1 : 0, points, heavy_vertex, 0, options);
    polyline.set_number(heavy_polyline, 30, elevation);
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        polyline.set_vertex_widths(i, vertices[i].start_width, vertices[i].end_width);
        polyline.set_bulge(i, vertices[i].bulge);
    }
    return insert_entity(std::move(polyline), options.space);
}

std::uint64_t DxfDocument::add_polyline3d(const std::vector<Point3>& vertices, bool closed,
                                         const EntityOptions& options) {
    DxfPolyline3D polyline;
    initialize_sequence(polyline, polyline3d, 8 | (closed ? 1 : 0), vertices, vertex3d, 32, options);
    return insert_entity(std::move(polyline), options.space);
}

std::uint64_t DxfDocument::add_polygon_mesh(int m, int n, const std::vector<Point3>& vertices,
    bool closed_m, bool closed_n, const EntityOptions& options) {
    if (m < 0 || n < 0 || ((m == 0) != (n == 0)) || m > 32767 || n > 32767 ||
        std::size_t(m) * std::size_t(n) != vertices.size())
        throw std::invalid_argument("Polygon mesh dimensions must match its vertex count");
    DxfPolygonMesh mesh;
    initialize_sequence(mesh, polygon_mesh, 16 | (closed_m ? 1 : 0) | (closed_n ? 32 : 0),
                        vertices, mesh_vertex, 64, options);
    mesh.set_short_field(polygon_mesh, 71, m);
    mesh.set_short_field(polygon_mesh, 72, n);
    return insert_entity(std::move(mesh), options.space);
}

std::uint64_t DxfDocument::add_polyface_mesh(const std::vector<Point3>& vertices,
    const std::vector<std::array<int,4>>& faces, const EntityOptions& options) {
    if (vertices.size() > 32767 || faces.size() > 32767)
        throw std::invalid_argument("Polyface counts exceed DXF int16 range");
    DxfPolyfaceMesh mesh;
    initialize_sequence(mesh, "AcDbPolyFaceMesh", 64, vertices, polyface_vertex, 192, options);
    mesh.set_short_field("AcDbPolyFaceMesh", 71, vertices.size());
    mesh.set_short_field("AcDbPolyFaceMesh", 72, faces.size());
    for (std::size_t i = 0; i < faces.size(); ++i) {
        auto face = make_entity("VERTEX", std::string(face_record), options);
        face.set_point(face_record, 10, {0,0,0});
        face.set_short_field(face_record, 70, 128);
        mesh.vertices_.push_back(std::move(face));
    }
    mesh.index_children();
    for (std::size_t i = 0; i < faces.size(); ++i) mesh.set_face_indices(i, faces[i]);
    return insert_entity(std::move(mesh), options.space);
}

std::uint64_t DxfDocument::add_text(std::string value, Point3 insertion, double height,
                                  const EntityOptions& options) {
    // Match Text::new/with_value/with_height and the two AcDbText scopes in write_text.
    const DxfRecord* style = nullptr;
    for (const auto& record : records_) {
        const auto* raw = std::get_if<DxfRecord>(&record);
        if (raw && raw->type() == "STYLE" &&
            detail::symbol_name_key(raw->text_field("AcDbTextStyleTableRecord", 2)) ==
            detail::symbol_name_key("Standard")) style = raw;
    }
    if (!style) throw std::invalid_argument("Standard text style does not exist");
    DxfText text;
    static_cast<DxfEntity&>(text) = make_entity("TEXT", "AcDbText", options);
    text.set_insertion_point(insertion);
    text.set_height(height);
    text.set_text(std::move(value));
    text.set_text_field("AcDbText", 7, style->text_field("AcDbTextStyleTableRecord", 2));
    text.set_short_field("AcDbText", 72, 0);
    if (version_ != DxfVersion::AC1009) text.pairs_.push_back({100, "AcDbText"});
    text.pairs_.push_back({73, binary_ ? detail::binary_short(0) : "0"});
    return insert_entity(std::move(text), options.space);
}

std::uint64_t DxfDocument::copy_entity(std::uint64_t handle, EntitySpace space) {
    return copy_entity(hex_handle(handle), space);
}

std::uint64_t DxfDocument::copy_entity(std::string_view handle, EntitySpace space) {
    const auto identity = handle_value(handle);
    const auto found = handle_index_.find(identity);
    if (!identity || found == handle_index_.end() || found->second.size() != 1)
        throw std::invalid_argument("Copy requires a unique existing entity handle");
    const auto location = found->second.front();
    const auto& source = record_at(location);
    const auto type = source.type();
    const auto* path2d = std::get_if<DxfPolyline2D>(&records_[location.record]);
    const auto* path3d = std::get_if<DxfPolyline3D>(&records_[location.record]);
    const auto* mesh = std::get_if<DxfPolygonMesh>(&records_[location.record]);
    const auto* polyface = std::get_if<DxfPolyfaceMesh>(&records_[location.record]);
    const DxfVertexSequence* path = path2d;
    if (path3d) path = path3d;
    if (mesh) path = mesh;
    if (polyface) path = polyface;
    if (location.vertex || location.seqend ||
        std::find(entity_records_.begin(), entity_records_.end(), location.record) == entity_records_.end() ||
        (type != "LINE" && type != "CIRCLE" && type != "ARC" && type != "ELLIPSE" && type != "TEXT" && type != "LWPOLYLINE" && !path))
        throw std::invalid_argument("Copy supports basic entities and parsed traditional POLYLINE sequences only");
    std::vector<const DxfRecord*> roots{&source};
    if (path) {
        if (!path->seqend_) throw std::invalid_argument("Cannot copy an unterminated POLYLINE sequence");
        auto include = [&](const DxfRecord& child) {
            const auto owner = child.owner_handle();
            if (!owner.empty()) {
                const auto h = parse_handle(owner);
                if (!h || (*h && *h != identity))
                    throw std::invalid_argument("Copied POLYLINE child has inconsistent owner");
            }
            roots.push_back(&child);
        };
        for (const auto& child : path->vertices_) include(child);
        include(*path->seqend_);
    }
    const auto entity_records = roots.size();
    const auto tree = extension_tree(roots);
    std::unordered_set<const DxfRecord*> members(tree.begin(), tree.end());
    std::unordered_set<std::uint64_t> identities;
    for (const auto* member : tree) {
        const auto h = handle_value(member->handle());
        const auto indexed = handle_index_.find(h);
        if (!h || !identities.insert(h).second || indexed == handle_index_.end() || indexed->second.size() != 1)
            throw std::invalid_argument("Copied record requires a unique nonzero identity");
        unsigned depth = 0, handles = 0, owners = 0, payloads = 0;
        bool common = true;
        for (const auto& p : member->pairs_) {
            if (p.code == 102) {
                if (!p.value.empty() && p.value.front() == '{') ++depth;
                else if (depth && p.value == "}") --depth;
                continue;
            }
            if (depth) continue;
            if (p.code == 1001 || p.code == 101) break;
            if (p.code == 100) { common = false; if (p.value == "AcDbXrecord") ++payloads; }
            if (common && p.code == 5) ++handles;
            if (common && p.code == 330) ++owners;
        }
        if (handles != 1 || owners > 1 || (member->type() == "XRECORD" && payloads != 1))
            throw std::invalid_argument("Ambiguous copied identity, owner or XRECORD payload");
    }
    // Do not silently omit an owned object or duplicate a tree shared by another owner.
    // Ordinary incoming pointers are allowed: they keep referring to the original.
    auto check_external = [&](const DxfRecord& record) {
        if (members.count(&record)) return;
        if (identities.count(handle_value(record.owner_handle())))
            throw std::invalid_argument("Unlisted object owned by the copied tree");
        for (const auto& p : record.pairs_)
            if (p.code >= 350 && p.code <= 369 && identities.count(handle_value(p.value)))
                throw std::invalid_argument("Copied tree has external ownership references");
    };
    for (const auto& record : records_) {
        std::visit(check_external, record);
        if (const auto* children = sequence(record)) {
            for (const auto& child : children->vertices_) check_external(child);
            if (children->seqend_) check_external(*children->seqend_);
        }
    }
    Record entry;
    auto initialize = [&](auto& entity) {
        static_cast<DxfRecord&>(entity) = source;
        entity.version_ = version_;
        entity.set_short_field("AcDbEntity", 67, space == EntitySpace::Paper ? 1 : 0);
    };
    if (path2d) { DxfPolyline2D entity = *path2d; initialize(entity); entry = std::move(entity); }
    else if (path3d) { DxfPolyline3D entity = *path3d; initialize(entity); entry = std::move(entity); }
    else if (mesh) { DxfPolygonMesh entity = *mesh; initialize(entity); entry = std::move(entity); }
    else if (polyface) { DxfPolyfaceMesh entity = *polyface; initialize(entity); entry = std::move(entity); }
    else if (type == "LINE") { DxfLine entity; initialize(entity); entry = std::move(entity); }
    else if (type == "CIRCLE") { DxfCircle entity; initialize(entity); entry = std::move(entity); }
    else if (type == "ARC") { DxfArc entity; initialize(entity); entry = std::move(entity); }
    else if (type == "ELLIPSE") { DxfEllipse entity; initialize(entity); entry = std::move(entity); }
    else if (type == "TEXT") { DxfText entity; initialize(entity); entry = std::move(entity); }
    else { DxfLwPolyline entity; initialize(entity); entry = std::move(entity); }

    DxfDocument staged = *this;
    const auto fresh = staged.insert_entity(std::move(entry), space);
    // Like Rust document/block_graph::import_block_graph, allocate the entire map first.
    std::unordered_map<std::uint64_t, std::uint64_t> remap{{identity, fresh}};
    for (std::size_t i = 1; i < tree.size(); ++i)
        remap.emplace(handle_value(tree[i]->handle()), staged.allocate_handle());
    auto rewrite = [&](DxfRecord& record) {
        auto change = [&](DxfRecord::Pair& p, bool editable) {
            const auto it = remap.find(handle_value(p.value));
            if (it == remap.end()) return;
            if (!editable) throw std::invalid_argument("Internal reference in opaque application data");
            p.value = hex_handle(it->second);
        };
        auto& pairs = record.pairs_;
        bool payload = false, xdata = false, opaque = false;
        unsigned depth = 0;
        for (std::size_t i = 1; i < pairs.size(); ++i) {
            auto& p = pairs[i];
            if (!depth && !xdata && !opaque && p.code == 100 &&
                p.value == "AcDbXrecord" && record.type() == "XRECORD") payload = true;
            if (payload) {
                // Upstream XRecordValueType treats 5/105/320-329 as strings,
                // and 330-369, 390-399, 480/481/1005 as references.
                if ((p.code >= 330 && p.code <= 369) || (p.code >= 390 && p.code <= 399) ||
                    p.code == 480 || p.code == 481 || p.code == 1005) change(p, true);
                continue;
            }
            if (!depth && p.code == 1001) xdata = true;
            if (!depth && p.code == 101) opaque = true;
            if (!depth && !opaque && !xdata && p.code == 102 &&
                (p.value == "{ACAD_REACTORS" || p.value == "{ACAD_XDICTIONARY")) {
                const bool dictionary = p.value == "{ACAD_XDICTIONARY";
                auto end = i + 1;
                while (end < pairs.size() && pairs[end].code == (dictionary ? 360 : 330)) ++end;
                if (end == pairs.size() || pairs[end].code != 102 || pairs[end].value != "}" ||
                    (dictionary && end != i + 2))
                    throw std::invalid_argument("Malformed common reference group");
                for (auto j = i + 1; j < end; ++j) change(pairs[j], true);
                i = end;
                continue;
            }
            if (p.code == 102) {
                if (!p.value.empty() && p.value.front() == '{') ++depth;
                else if (depth && p.value == "}") --depth;
                continue;
            }
            const bool reference = (p.code >= 320 && p.code <= 369) ||
                (p.code >= 390 && p.code <= 399) || p.code == 480 || p.code == 481 || p.code == 1005;
            if (reference) change(p, !depth && !opaque && (!xdata || p.code == 1005));
        }
    };
    const auto copied_index = staged.handle_index_.at(fresh).front().record;
    auto& copied = std::visit([](auto& r) -> DxfRecord& { return r; }, staged.records_[copied_index]);
    rewrite(copied);
    // Parent ownership is a space BLOCK_RECORD; child ownership is the new parent.
    // Space flags and existing layout-name hints still describe that same space.
    const auto target_owner = copied.owner_handle();
    auto set_space = [&](DxfRecord& record) {
        record.set_short_field("AcDbEntity", 67, space == EntitySpace::Paper ? 1 : 0);
        if (record.locate("AcDbEntity", 410).found) {
            std::optional<std::string> layout_name;
            for (const auto& item : records_) {
                const auto& layout = std::visit([](const auto& r) -> const DxfRecord& { return r; }, item);
                if (layout.type() != "LAYOUT" ||
                    handle_value(layout.field("AcDbLayout", 330)) != handle_value(target_owner)) continue;
                if (layout_name) throw std::invalid_argument("Ambiguous target layout");
                layout_name = layout.field("AcDbLayout", 1);
            }
            if (!layout_name || layout_name->empty()) throw std::invalid_argument("Missing target layout name");
            record.set_field("AcDbEntity", 410, *layout_name);
        }
    };
    set_space(copied);
    if (auto* children = sequence(staged.records_[copied_index])) {
        auto remap_child = [&](DxfRecord& child) {
            const auto old = handle_value(child.handle());
            child.set_field("", 5, hex_handle(remap.at(old)));
            rewrite(child);
            child.set_field("", 330, hex_handle(fresh));
            set_space(child);
        };
        for (auto& child : children->vertices_) remap_child(child);
        remap_child(*children->seqend_);
    }
    if (tree.size() > entity_records) {
        std::optional<std::size_t> objects_end;
        bool inside = false, seen = false;
        for (std::size_t i = 0; i < staged.records_.size(); ++i) {
            const auto& r = staged.record_at({i, std::nullopt, false});
            if (r.type() == "SECTION" && trim(r.field("", 2)) == "OBJECTS") {
                if (seen) throw std::invalid_argument("Multiple OBJECTS sections");
                inside = seen = true;
            } else if (r.type() == "ENDSEC" && inside) { objects_end = i; inside = false; }
        }
        if (!objects_end || inside) throw std::invalid_argument("Missing complete OBJECTS section");
        std::vector<Record> objects;
        for (std::size_t i = entity_records; i < tree.size(); ++i) {
            DxfRecord object = *tree[i];
            object.set_field("", 5, hex_handle(remap.at(handle_value(object.handle()))));
            rewrite(object);
            objects.emplace_back(std::move(object));
        }
        staged.records_.insert(staged.records_.begin() + *objects_end,
            std::make_move_iterator(objects.begin()), std::make_move_iterator(objects.end()));
    }
    staged.rebuild_indexes();
    *this = std::move(staged);
    return fresh;
}

std::vector<const DxfRecord*> DxfDocument::extension_tree(std::vector<const DxfRecord*> group) const {
    // Port the owner-chain cascade in Rust classes/arch/host::objects_owned_by,
    // verifying dictionary links and explicit OBJECTS ownership chains. Raw owned
    // objects are retained in the traversal; conflicting ownership is rejected.
    std::unordered_set<std::size_t> removed_records;
    std::unordered_set<std::uint64_t> doomed;
    for (const auto* root : group) {
        const auto h = handle_value(root->handle());
        if (h) doomed.insert(h);
    }
    std::unordered_set<std::size_t> object_records;
    std::string section;
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto& record = std::visit([](const auto& r) -> const DxfRecord& { return r; }, records_[i]);
        if (record.type() == "SECTION") section = trim(record.field("", 2));
        else if (record.type() == "ENDSEC") section.clear();
        else if (section == "OBJECTS") object_records.insert(i);
    }
    auto include_object = [&](std::string_view text, const DxfRecord& owner, bool dictionary) {
        const auto h = parse_handle(text);
        if (!h || !*h) throw std::invalid_argument("Invalid extension dictionary object handle");
        const auto indexed = handle_index_.find(*h);
        if (indexed == handle_index_.end() || indexed->second.size() != 1)
            throw std::invalid_argument("Missing or ambiguous extension dictionary object");
        const auto at = indexed->second.front();
        const auto& child = record_at(at);
        if (at.vertex || at.seqend || !object_records.count(at.record) ||
            (dictionary && child.type() != "DICTIONARY"))
            throw std::invalid_argument("Unsupported extension dictionary object type");
        // Count common owner fields, excluding application groups and object payload.
        unsigned depth = 0, owners = 0;
        for (std::size_t i = 1; i < child.pairs_.size(); ++i) {
            const auto& p = child.pairs_[i];
            if (p.code == 102) {
                if (!p.value.empty() && p.value.front() == '{') ++depth;
                else if (depth && p.value == "}") --depth;
                continue;
            }
            if (depth) continue;
            if (p.code == 100 || p.code == 1001 || p.code == 101) break;
            if (p.code == 330) {
                ++owners;
                if (!handle_value(owner.handle()) || handle_value(p.value) != handle_value(owner.handle()))
                    throw std::invalid_argument("Extension dictionary object has inconsistent owner");
            }
        }
        if (owners != 1 || !doomed.insert(*h).second || !removed_records.insert(at.record).second)
            throw std::invalid_argument("Ambiguous or cyclic extension dictionary ownership");
        group.push_back(&child);
    };
    // The work list grows as dictionaries and object extension dictionaries are found.
    // Iteration avoids recursion for deeply nested files.
    for (std::size_t member_index = 0; member_index < group.size(); ++member_index) {
        const auto& member = *group[member_index];
        const auto& pairs = member.pairs_;
        unsigned depth = 0;
        bool extension_seen = false;
        for (std::size_t i = 1; i < pairs.size(); ++i) {
            const auto& p = pairs[i];
            if (!depth && (p.code == 100 || p.code == 1001 || p.code == 101)) break;
            if (p.code != 102) continue;
            if (!depth && p.value == "{ACAD_XDICTIONARY") {
                if (extension_seen || i + 2 >= pairs.size() || pairs[i+1].code != 360 ||
                    pairs[i+2].code != 102 || pairs[i+2].value != "}")
                    throw std::invalid_argument("Malformed extension dictionary group");
                extension_seen = true;
                include_object(pairs[i+1].value, member, true);
                i += 2;
                continue;
            }
            if (!p.value.empty() && p.value.front() == '{') ++depth;
            else if (depth && p.value == "}") --depth;
        }
        auto include_owned = [&] {
            const auto owner = handle_value(member.handle());
            if (!owner) return;
            for (std::size_t index = 0; index < records_.size(); ++index) {
                if (!object_records.count(index)) continue;
                const auto& candidate = record_at({index, std::nullopt, false});
                if (handle_value(candidate.owner_handle()) != owner || doomed.count(handle_value(candidate.handle()))) continue;
                include_object(candidate.handle(), member, false);
            }
        };
        if (member.type() != "DICTIONARY") { include_owned(); continue; }
        bool active = false, subclass_seen = false;
        std::unordered_set<std::string> names;
        depth = 0;
        for (std::size_t i = 1; i < pairs.size(); ++i) {
            const auto& p = pairs[i];
            if (p.code == 102) {
                if (!p.value.empty() && p.value.front() == '{') ++depth;
                else if (depth && p.value == "}") --depth;
                continue;
            }
            if (depth) continue;
            if (p.code == 1001 || p.code == 101) break;
            if (p.code == 100) {
                active = p.value == "AcDbDictionary";
                if (active && subclass_seen) throw std::invalid_argument("Duplicate dictionary subclass");
                subclass_seen = subclass_seen || active;
                continue;
            }
            if (!active) continue;
            if (p.code == 3) {
                if (p.value.empty() || !names.insert(p.value).second || i + 1 >= pairs.size() ||
                    (pairs[i+1].code != 350 && pairs[i+1].code != 360))
                    throw std::invalid_argument("Malformed dictionary entry");
                include_object(pairs[++i].value, member, false);
            } else if (p.code == 350 || p.code == 360) {
                throw std::invalid_argument("Dictionary entry has no name");
            }
        }
        if (!subclass_seen) throw std::invalid_argument("Missing dictionary subclass");
        include_owned();
    }
    for (const auto* member : group) {
        for (const auto& pair : member->pairs_) {
            const auto value = handle_value(pair.value);
            if (pair.code >= 350 && pair.code <= 369 && value && !doomed.count(value))
                throw std::invalid_argument("Unsupported owned object outside the extension dictionary tree");
        }
    }

    return group;
}

bool DxfDocument::remove_entity(std::uint64_t handle) {
    return remove_entity(hex_handle(handle));
}

bool DxfDocument::remove_entity(std::string_view handle) {
    const auto identity = handle_value(handle);
    const auto found = handle_index_.find(identity);
    if (!identity || found == handle_index_.end()) return false;
    if (found->second.size() != 1)
        throw std::invalid_argument("Cannot remove an ambiguous DXF identity");
    const auto location = found->second.front();
    const auto& target = record_at(location);
    const auto type = target.type();
    const bool polyline = std::holds_alternative<DxfPolyline2D>(records_[location.record]) ||
                          std::holds_alternative<DxfPolyline3D>(records_[location.record]) ||
                          std::holds_alternative<DxfPolygonMesh>(records_[location.record]) ||
                          std::holds_alternative<DxfPolyfaceMesh>(records_[location.record]);
    if (location.vertex || location.seqend ||
        std::find(entity_records_.begin(), entity_records_.end(), location.record) == entity_records_.end() ||
        (type != "LINE" && type != "CIRCLE" && type != "ARC" && type != "ELLIPSE" && type != "TEXT" && type != "LWPOLYLINE" && !polyline))
        throw std::invalid_argument("Removal supports LINE, CIRCLE, ARC, ELLIPSE, TEXT, LWPOLYLINE and parsed POLYLINE sequences only");

    // Rust stores ordered vertices inside the parent entity. This representation also
    // retains SEQEND and indexes every child identity, so erase the whole owned sequence.
    // Polyface face records and unclassified VERTEX records also live in vertices_.
    // Use that raw sequence, not the filtered coordinate/face views, to avoid orphaning children.
    std::unordered_set<std::uint64_t> doomed{identity};
    std::vector<const DxfRecord*> group{&target};
    if (polyline) {
        const auto* children = sequence(records_[location.record]);
        auto include_child = [&](const DxfRecord& child) {
            const auto owner_text = child.owner_handle();
            if (!owner_text.empty()) {
                const auto owner = parse_handle(owner_text);
                if (!owner || (*owner && *owner != identity))
                    throw std::invalid_argument("Polyline child has an inconsistent owner");
            }
            const auto text = child.handle();
            if (!text.empty()) {
                const auto handle = parse_handle(text);
                if (!handle) throw std::invalid_argument("Invalid polyline child identity");
                if (*handle) {
                    const auto indexed = handle_index_.find(*handle);
                    if (!doomed.insert(*handle).second || indexed == handle_index_.end() ||
                        indexed->second.size() != 1 || indexed->second.front().record != location.record)
                        throw std::invalid_argument("Ambiguous polyline child identity");
                }
            }
            group.push_back(&child);
        };
        for (const auto& vertex : children->vertices_) include_child(vertex);
        if (children->seqend_) include_child(*children->seqend_);
    }

    group = extension_tree(std::move(group));
    std::unordered_set<std::size_t> removed_records{location.record};
    for (const auto* member : group) {
        const auto h = handle_value(member->handle());
        if (h) {
            doomed.insert(h);
            removed_records.insert(handle_index_.at(h).front().record);
        }
    }

    // Rust removes from the entity index and filters block membership through it.
    // This record model derives membership from explicit owners; rebuild both indexes.
    DxfDocument staged = *this;
    auto clean_reactors = [&](DxfRecord& record) {
        auto& pairs = record.pairs_;
        std::vector<bool> erase(pairs.size(), false);
        bool changed = false, opaque = false;
        unsigned depth = 0;
        for (std::size_t i = 1; i < pairs.size(); ++i) {
            const auto& pair = pairs[i];
            if (!depth && (pair.code == 1001 || pair.code == 101 ||
                (record.type() == "XRECORD" && pair.code == 100 && pair.value == "AcDbXrecord"))) opaque = true;
            if (pair.code != 102) continue;
            if (!opaque && !depth && pair.value == "{ACAD_REACTORS") {
                auto end = i + 1;
                while (end < pairs.size() && pairs[end].code == 330) ++end;
                // Only a complete top-level group containing handle entries is editable.
                // Nested, unterminated or opaque application data retains its references.
                if (end < pairs.size() && pairs[end].code == 102 && pairs[end].value == "}") {
                    std::size_t removed = 0;
                    for (auto j = i + 1; j < end; ++j) {
                        if (doomed.count(handle_value(pairs[j].value))) {
                            erase[j] = true; ++removed;
                        }
                    }
                    if (removed) {
                        changed = true;
                        if (removed == end - i - 1) erase[i] = erase[end] = true;
                    }
                    i = end;
                    continue;
                }
            }
            if (!pair.value.empty() && pair.value.front() == '{') ++depth;
            else if (depth && pair.value == "}") --depth;
        }
        // Port the Rust writer's reactor filtering for the identities removed by this
        // operation only. Unrelated stale/zero references must remain byte-for-byte intact.
        for (const auto& [i, editable] : reference_positions(record)) {
            if (!erase[i] && doomed.count(handle_value(pairs[i].value)))
                throw std::invalid_argument("Cannot remove an entity or child with a non-reactor reference");
        }
        if (changed) {
            std::vector<DxfRecord::Pair> kept;
            kept.reserve(pairs.size());
            for (std::size_t i = 0; i < pairs.size(); ++i)
                if (!erase[i]) kept.push_back(std::move(pairs[i]));
            pairs = std::move(kept);
        }
    };
    for (std::size_t i = 0; i < staged.records_.size(); ++i) {
        if (removed_records.count(i)) continue;
        std::visit(clean_reactors, staged.records_[i]);
        if (auto* children = sequence(staged.records_[i])) {
            for (auto& vertex : children->vertices_) clean_reactors(vertex);
            if (children->seqend_) clean_reactors(*children->seqend_);
        }
    }
    // Compact in file order without repeated vector shifts for large object trees.
    std::vector<Record> retained;
    retained.reserve(staged.records_.size() - removed_records.size());
    for (std::size_t i = 0; i < staged.records_.size(); ++i)
        if (!removed_records.count(i)) retained.push_back(std::move(staged.records_[i]));
    staged.records_ = std::move(retained);
    // Persist the reconciled allocator even if the source HANDSEED was missing/stale.
    staged.handle_reserved_ = true;
    staged.rebuild_indexes();
    *this = std::move(staged);
    return true;
}

DxfDocument DxfDocument::create(bool binary, std::int16_t insertion_units) {
    // Reuse the record reader to establish subclasses, scopes and all indexes.
    // Defaults are compiled into this translation unit; no external file is read.
    std::ostringstream source;
    source.imbue(std::locale::classic());
    std::string_view variable;
    for (const auto& pair : default_pairs) {
        source << pair.code << '\n';
        if (variable == "$INSUNITS" && pair.code == 70) source << insertion_units;
        else source << pair.value;
        source << '\n';
        variable = pair.code == 9 ? pair.value : "";
    }
    std::istringstream input(source.str());
    auto doc = read(input);
    if (binary) {
        for (auto& record : doc.records_) {
            std::visit([](DxfRecord& item) {
                for (auto& pair : item.pairs_)
                    pair.value = detail::binary_value_from_text(pair.code, pair.value);
                item.binary_ = true;
            }, record);
        }
        doc.binary_ = true;
    }
    return doc;
}

DxfDocument DxfDocument::read(std::istream& input) {
    DxfDocument doc;
    std::string section, table;
    const char* page = nullptr;
    bool eof = false;
    std::vector<DxfRecord::Pair> pairs;
    auto finish = [&] {
        if (pairs.empty()) return;
        const auto& first = pairs.front();
        std::string_view type = first.code == 0 ? trim(first.value) : std::string_view{};
        // A heavy sequence owns consecutive VERTEX records and its SEQEND.
        // A new top-level record ends the sequence even if SEQEND was omitted.
        if ((section == "ENTITIES" || section == "BLOCKS") && !doc.records_.empty() && (type == "VERTEX" || type == "SEQEND")) {
            auto* parent = sequence(doc.records_.back());
            if (parent && !parent->seqend_) {
                DxfRecord& child = type == "VERTEX" ? parent->vertices_.emplace_back()
                                                    : parent->seqend_.emplace();
                child.binary_ = doc.binary_;
                child.pairs_.swap(pairs);
                return;
            }
        }
        Record record;
        if (type == "SECTION" || (type == "TABLE" && section == "TABLES")) {
            auto name = std::find_if(pairs.begin() + 1, pairs.end(),
                                     [](const auto& p) { return p.code == 2; });
            if (name == pairs.end()) throw std::runtime_error("Missing DXF section/table name");
            auto& scope = type == "SECTION" ? section : table;
            if (!scope.empty()) throw std::runtime_error("Nested DXF section/table");
            scope = trim(name->value);
            if (type == "SECTION" && scope == "HEADER") {
                doc.header_index_ = doc.records_.size();
                std::string_view version, declaration;
                for (auto it = name + 1; it != pairs.end(); ++it) {
                    if (it->code != 9 || it + 1 == pairs.end()) continue;
                    const auto& value = *(it + 1);
                    if (it->value == "$ACADVER") {
                        doc.version_ = dxf_version_from_string(value.value);
                        if (value.code == 1) version = trim(value.value);
                    } else if (it->value == "$HANDSEED" && value.code == 5) {
                        doc.next_handle_ = std::max(doc.next_handle_, handle_value(value.value));
                    } else if (it->value == "$INSUNITS") {
                        if (auto units = detail::as_i16(value.code, value.value, doc.binary_))
                            doc.insertion_units_ = *units;
                    } else if (it->value == "$DWGCODEPAGE" && value.code == 3) {
                        declaration = trim(value.value);
                    }
                }
                page = detail::legacy_page(version, declaration);
            }
        } else if (type == "ENDTAB") {
            if (table.empty()) throw std::runtime_error("Unmatched ENDTAB");
            table.clear();
        } else if (type == "ENDSEC") {
            if (section.empty() || !table.empty()) throw std::runtime_error("Unmatched ENDSEC");
            section.clear();
        } else if (type == "EOF") {
            if (!section.empty()) throw std::runtime_error("Unclosed DXF section");
            eof = true;
        } else if (section == "TABLES" && table == "BLOCK_RECORD" && type == "BLOCK_RECORD") {
            record.emplace<DxfBlockRecord>();
        } else if (section == "TABLES" && table == "LTYPE" && type == "LTYPE") {
            record.emplace<DxfLineType>();
        } else if (section == "TABLES" && table == "LAYER" && type == "LAYER") {
            record.emplace<DxfLayer>();
        } else if (section == "ENTITIES" || (section == "BLOCKS" && type == "POLYLINE")) {
            if (type == "LINE") record.emplace<DxfLine>();
            else if (type == "CIRCLE") record.emplace<DxfCircle>();
            else if (type == "ARC") record.emplace<DxfArc>();
            else if (type == "ELLIPSE") record.emplace<DxfEllipse>();
            else if (type == "POLYLINE") {
                DxfRecord header;
                header.binary_ = doc.binary_;
                header.pairs_.swap(pairs);
                // On flat records, polyface (64) precedes polygon mesh (16), then 3D (8).
                const int mesh_flags = header.short_field(polygon_mesh, 70, 0);
                const int flags3d = header.short_field(polyline3d, 70, 0);
                if (header.short_field(polyface_mesh, 70, 0) & 64) {
                    record.emplace<DxfPolyfaceMesh>();
                } else if ((mesh_flags & (16 | 64)) == 16) {
                    record.emplace<DxfPolygonMesh>();
                } else if ((flags3d & (8 | 16 | 64)) == 8) {
                    record.emplace<DxfPolyline3D>();
                } else if (header.locate(heavy_polyline, 70).index != std::string_view::npos &&
                           (header.short_field(heavy_polyline, 70, 0) & (8 | 16 | 64)) == 0) {
                    record.emplace<DxfPolyline2D>();
                }
                header.pairs_.swap(pairs);
            }
            else if (type == "LWPOLYLINE") record.emplace<DxfLwPolyline>();
            else if (type == "TEXT") record.emplace<DxfText>();
        }
        std::visit([&](DxfRecord& item) {
            item.pairs_.swap(pairs);
        }, record);
        doc.records_.push_back(std::move(record));
    };

    doc.binary_ = input.peek() == 'A';
    if (doc.binary_) {
        std::string signature(detail::binary_signature.size(), '\0');
        if (!input.read(signature.data(), signature.size()) || signature != detail::binary_signature)
            throw std::runtime_error("Invalid binary DXF signature");
    }

    std::string code_line, value;
    bool first_pair = true;
    while (input.peek() != std::char_traits<char>::eof()) {
        int code;
        if (doc.binary_) {
            const int first = input.get();
            if (first_pair) {
                const int next = input.peek();
                doc.compact_codes_ = first == 0 && next >= 0x20 && next < 0x7f;
                first_pair = false;
            }
            code = detail::read_binary_code(input, first, doc.compact_codes_);
            value = detail::read_binary_value(input, code);
        } else {
            if (!std::getline(input, code_line)) throw std::runtime_error("Missing DXF code");
            code = integer(code_line);
            if (!std::getline(input, value)) throw std::runtime_error("Missing DXF group value");
            if (!value.empty() && value.back() == '\r') value.pop_back();
        }
        if (code == 0) finish();
        if (eof) throw std::runtime_error("Records after DXF EOF");
        pairs.push_back({code, std::move(value)});
    }
    if (input.bad() || !input.eof()) throw std::runtime_error("DXF input failed");
    finish();
    if (!eof) throw std::runtime_error("Missing DXF EOF");
    for (auto& record : doc.records_) {
        std::visit([&](auto& item) {
            item.code_page_ = page;
            item.binary_ = doc.binary_;
            if constexpr (std::is_base_of_v<DxfEntity, std::decay_t<decltype(item)>>)
                item.version_ = doc.version_;
        }, record);
        if (auto* path = sequence(record)) {
            for (auto& vertex : path->vertices_) vertex.code_page_ = page;
            if (path->seqend_) path->seqend_->code_page_ = page;
        }
        if (auto* mesh = std::get_if<DxfPolyfaceMesh>(&record)) mesh->index_children();
    }
    doc.rebuild_indexes();
    return doc;
}

std::uint64_t DxfDocument::allocate_handle() {
    if (next_handle_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("DXF handle space exhausted");
    handle_reserved_ = true;
    return next_handle_++;
}

#include "dxf_symbols.inc"
#include "dxf_references.inc"

void DxfDocument::write(std::ostream& out, bool binary) const {
    if (binary == binary_) { write(out); return; }
    DxfDocument converted = *this;
    auto convert = [&](DxfRecord& record) {
        for (auto& pair : record.pairs_) {
            if (detail::is_string_value(pair.code)) {
                // Transport escaping changes; legacy code-page bytes stay in the same code page.
                auto value = record.decode_string(pair.value);
                if (value.find('\0') != std::string::npos)
                    throw std::invalid_argument("DXF string contains a null byte");
                pair.value = detail::to_legacy(binary ? value : encode_text(value), record.code_page_);
            } else {
                pair.value = binary ? detail::binary_value_from_text(pair.code, pair.value)
                                    : detail::text_value_from_binary(pair.code, pair.value);
            }
        }
        record.binary_ = binary;
    };
    for (auto& record : converted.records_) {
        std::visit(convert, record);
        if (auto* compound = sequence(record)) {
            for (auto& vertex : compound->vertices_) convert(vertex);
            if (compound->seqend_) convert(*compound->seqend_);
        }
    }
    converted.binary_ = binary;
    converted.compact_codes_ = binary && version_ == DxfVersion::AC1009;
    converted.write(out);
}

void DxfDocument::write(std::ostream& out) const {
    if (binary_) out.write(detail::binary_signature.data(), detail::binary_signature.size());
    auto write_record = [&](const DxfRecord& item) {
        for (const auto& p : item.pairs_) {
            if (binary_) {
                detail::write_binary_pair(out, compact_codes_, p.code, p.value);
                continue;
            }
            const auto code = std::to_string(p.code);
            if (code.size() < 3) out.write("   ", 3 - code.size());
            out << code << "\r\n" << p.value << "\r\n";
        }
    };
    // Overlay the updated seed only when a caller reserved a handle. Keep
    // untouched headers verbatim, including stale seeds in existing drawings.
    auto write_header = [&](DxfRecord header) {
        std::ostringstream hex;
        hex.imbue(std::locale::classic());
        hex << std::uppercase << std::hex << next_handle_;
        const auto seed = hex.str();
        bool found = false;
        for (auto it = header.pairs_.begin(); it != header.pairs_.end(); ++it) {
            if (it->code != 9 || it->value != "$HANDSEED") continue;
            auto value = it + 1;
            if (value == header.pairs_.end() || value->code != 5)
                value = header.pairs_.insert(value, {5, seed});
            else
                value->value = seed;
            it = value;
            found = true;
        }
        if (!found) {
            header.pairs_.push_back({9, "$HANDSEED"});
            header.pairs_.push_back({5, seed});
        }
        write_record(header);
    };
    if (handle_reserved_ && !header_index_) {
        DxfRecord header, end;
        header.pairs_ = {{0, "SECTION"}, {2, "HEADER"}};
        end.pairs_ = {{0, "ENDSEC"}};
        write_header(std::move(header));
        write_record(end);
    }
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto& record = records_[i];
        if (handle_reserved_ && header_index_ == i) {
            write_header(std::get<DxfRecord>(record));
            continue;
        }
        std::visit(write_record, record);
        if (const auto* path = sequence(record)) {
            for (const auto& vertex : path->vertices_) write_record(vertex);
            if (path->seqend_) write_record(*path->seqend_);
        }
    }
    if (!out) throw std::runtime_error("DXF output failed");
}
} // namespace libopencad
