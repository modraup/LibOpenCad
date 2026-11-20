// SPDX-License-Identifier: MPL-2.0
// Native port of acadrust src/entities/acis/{types,parser,writer}.rs.
#pragma once
#include "vector.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace libopencad {
struct Transform;
}
namespace libopencad::acis {
struct SatVersion {
    std::uint32_t major = 7, minor = 0, patch = 0;
    std::uint32_t number() const;
    static SatVersion from_number(std::uint32_t value);
};
struct SatHeader {
    SatVersion version;
    std::size_t num_records = 0, num_bodies = 0;
    bool has_history = false;
    std::string product_id = "acadrust", product_version = "ACIS 7.0", date = "Thu Jan 01 00:00:00 2023";
    double spatial_resolution = 10, normal_tolerance = 1e-6;
    std::optional<double> resfit_tolerance = 1e-10;
};
struct SatPointer {
    std::int32_t value = -1;
    bool is_null() const { return value < 0; }
};
struct SatIdent {
    std::string value;
};
struct SatString {
    std::string value;
};
struct SatEnum {
    std::string value;
};
struct SatTerminator {};
// SAB payload is retained exactly, including its original numeric width and string prefix.
struct SabToken {
    std::uint8_t tag = 0;
    std::vector<std::uint8_t> data;
};
struct SatToken {
    using Value = std::variant<SatIdent, SatPointer, std::int64_t, double, SatString, Vector3, bool,
                               SatTerminator, SatEnum, SabToken>;
    Value value;
    SatToken() : value(SatIdent{}) {}
    template <class T> explicit SatToken(T v) : value(std::move(v)) {}
    std::optional<double> as_float() const;
    std::optional<std::int64_t> as_integer() const;
    std::optional<SatPointer> as_pointer() const;
    std::optional<std::string_view> as_ident() const;
    std::optional<std::string_view> as_string() const;
    std::optional<std::pair<std::array<double, 3>, std::size_t>> coordinates() const;
};
enum class Sense { Forward, Reversed };
enum class Sidedness { Single, Double };
struct SatRecord {
    std::int32_t index = 0;
    std::string entity_type;
    std::optional<std::string> sub_type;
    SatPointer attribute;
    std::int32_t subtype_id = -1;
    std::vector<SatToken> tokens;
    std::optional<std::string> raw_text;
    bool is_a(std::string_view type) const;
    std::vector<SatPointer> pointers() const;
    std::optional<double> token_float(std::size_t slot) const;
    std::optional<std::int64_t> token_integer(std::size_t slot) const;
    std::optional<SatPointer> token_pointer(std::size_t slot) const;
    std::optional<SatPointer> nth_pointer(std::size_t ordinal) const;
    std::optional<std::string_view> token_string(std::size_t slot) const;
    Sense token_sense(std::size_t slot) const;
};
struct SatPlacement {
    // SAT uses row vectors: world = scale * (point * matrix) + translation.
    std::array<std::array<double, 3>, 3> matrix{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    Vector3 translation;
    double scale = 1;
    Vector3 apply(Vector3 point) const;
};
struct SatValidationError {
    std::int32_t record_index, pointer_value;
    std::string context;
};
class SatParseError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
using KnotRuns = std::vector<std::pair<double, std::int32_t>>;
struct SplineNet {
    bool rational = false;
    std::int32_t degree_u = 1, degree_v = 1;
    bool u_closed = false, v_closed = false;
    KnotRuns u_knots, v_knots;
    std::vector<Vector3> control;
    std::vector<double> weights;
    double fit_tolerance = 0;
};
struct SatDocument {
    SatHeader header;
    std::vector<SatRecord> records;
    static SatDocument parse(std::string_view text);
    static SatDocument new_body();
    std::int32_t add_point(double x, double y, double z);
    std::int32_t add_plane_surface(Vector3 root, Vector3 normal, Vector3 u_direction);
    std::int32_t add_straight_curve(Vector3 root, Vector3 direction);
    std::int32_t add_cone_surface(Vector3 center, Vector3 axis, Vector3 major_axis, double ratio,
                                  double cos_half_angle, double sin_half_angle);
    std::int32_t add_sphere_surface(Vector3 center, double radius, Vector3 u_direction, Vector3 pole);
    std::int32_t add_torus_surface(Vector3 center, Vector3 normal, double major_radius, double minor_radius,
                                   Vector3 u_direction);
    std::int32_t add_ellipse_curve(Vector3 center, Vector3 normal, Vector3 major_axis, double ratio);
    std::int32_t add_spline_curve(bool rational, std::int32_t degree, bool closed, const KnotRuns &knots,
                                  const std::vector<Vector3> &control,
                                  const std::vector<double> &weights = {}, double fit_tolerance = 0);
    std::int32_t add_spline_surface(bool reverse_v, const SplineNet &net);
    std::int32_t add_offset_spline_surface(bool reverse_v, bool base_reverse_v, const SplineNet &base,
                                           double distance, const SplineNet &approximation);
    std::int32_t add_pcurve(bool rational, std::int32_t degree, bool closed, const KnotRuns &knots,
                            const std::vector<Vector2> &control, const std::vector<double> &weights,
                            double fit_tolerance, std::int32_t support_surface, Vector2 offsets);
    std::int32_t add_vertex(SatPointer edge, SatPointer point);
    std::int32_t add_edge(SatPointer start_vertex, double start_param, SatPointer end_vertex,
                          double end_param, SatPointer coedge, SatPointer curve, Sense sense);
    std::int32_t add_coedge(SatPointer next, SatPointer prev, SatPointer partner, SatPointer edge,
                            Sense sense, SatPointer owner_loop);
    std::int32_t add_loop(SatPointer next_loop, SatPointer first_coedge, SatPointer face);
    std::int32_t add_face(SatPointer next_face, SatPointer first_loop, SatPointer shell, SatPointer surface,
                          Sense sense, Sidedness sidedness);
    std::int32_t add_shell(SatPointer first_face, SatPointer lump);
    std::int32_t add_lump(SatPointer shell, SatPointer body);
    std::string to_sat_string() const;
    const SatRecord *record(std::int32_t index) const;
    SatRecord *record(std::int32_t index);
    const SatRecord *resolve(SatPointer pointer) const { return record(pointer.value); }
    std::int32_t add_record(SatRecord record);
    std::vector<const SatRecord *> records_of_type(std::string_view type) const;
    SatPlacement placement() const;
    void set_placement(const SatPlacement &placement);
    void apply_transform(const libopencad::Transform &transform);
    std::vector<SatValidationError> validate() const;
    void complete_brep_links();
    // Recover analytic and interpolated-curve legacy edge parameters atomically.
    void upgrade_to_v700();
    void strip_for_sab();
    std::optional<SatDocument> completed_for_restore() const;
    std::vector<SatDocument> split_bodies() const;
    // Encounter-order shared definitions; returned values do not borrow mutable records.
    std::optional<std::vector<SatToken>> subtype_tokens(std::size_t index) const;
};
// Value views use the same normalized v700 slots as Rust. They do not retain
// pointers into a document and remain valid if its record vector is edited.
struct SatBody {
    SatPointer next_body, lump, wire_body, transform;
    static std::optional<SatBody> from_record(const SatRecord &);
};
struct SatLump {
    SatPointer next_lump, shell, body;
    static std::optional<SatLump> from_record(const SatRecord &);
};
struct SatShell {
    SatPointer next_shell, subshell, face, wire, lump;
    static std::optional<SatShell> from_record(const SatRecord &);
};
struct SatFace {
    SatPointer attribute, next_face, first_loop, shell, subshell, surface;
    Sense sense;
    Sidedness sidedness;
    static std::optional<SatFace> from_record(const SatRecord &);
};
struct SatLoop {
    SatPointer next_loop, first_coedge, face;
    static std::optional<SatLoop> from_record(const SatRecord &);
};
struct SatCoedge {
    SatPointer next, prev, partner, edge;
    Sense sense;
    SatPointer owner_loop, pcurve;
    static std::optional<SatCoedge> from_record(const SatRecord &);
};
struct SatEdge {
    SatPointer start_vertex;
    double start_param;
    SatPointer end_vertex;
    double end_param;
    SatPointer coedge, curve;
    Sense sense;
    static std::optional<SatEdge> from_record(const SatRecord &);
};
struct SatVertex {
    SatPointer edge, point;
    static std::optional<SatVertex> from_record(const SatRecord &);
};
struct SatPoint {
    Vector3 position;
    static std::optional<SatPoint> from_record(const SatRecord &);
};
struct SatStraightCurve {
    Vector3 root_point, direction;
    static std::optional<SatStraightCurve> from_record(const SatRecord &);
};
struct SatEllipseCurve {
    Vector3 center, normal, major_axis;
    double ratio;
    static std::optional<SatEllipseCurve> from_record(const SatRecord &);
};
struct SatPlaneSurface {
    Vector3 root_point, normal, u_direction;
    Sense sense;
    static std::optional<SatPlaneSurface> from_record(const SatRecord &);
};
struct SatConeSurface {
    Vector3 center, axis, major_axis;
    double ratio, sin_half_angle, cos_half_angle, radius;
    Sense sense;
    static std::optional<SatConeSurface> from_record(const SatRecord &);
};
struct SatSphereSurface {
    Vector3 center;
    double radius;
    Vector3 u_direction, pole;
    Sense sense;
    static std::optional<SatSphereSurface> from_record(const SatRecord &);
};
struct SatTorusSurface {
    Vector3 center, normal;
    double major_radius, minor_radius;
    Vector3 u_direction;
    Sense sense;
    static std::optional<SatTorusSurface> from_record(const SatRecord &);
};
// Homogeneous controls: [x*w,y*w,z*w,w] or [u*w,v*w,w].
struct SatBSplineCurve {
    std::size_t degree = 0;
    std::vector<double> knots;
    std::vector<std::array<double, 4>> controls;
};
struct SatBSplinePCurve {
    std::size_t degree = 0;
    std::vector<double> knots;
    std::vector<std::array<double, 3>> controls;
};
struct SatBSplineSurface {
    bool rational = false;
    std::size_t degree_u = 0, degree_v = 0;
    std::optional<std::string> u_closure, v_closure, u_singularity, v_singularity;
    std::vector<double> u_knots, v_knots;
    std::size_t control_count_u = 0, control_count_v = 0;
    std::vector<std::array<double, 4>> control_points;
    std::optional<double> fit_tolerance;
};
struct SatCurveSupport {
    SatBSplineSurface surface;
    SatBSplinePCurve curve;
    double offset = 0;
};
class SatIntCurve {
    SatRecord record_;
    explicit SatIntCurve(const SatRecord &r) : record_(r) {}

  public:
    static std::optional<SatIntCurve> from_record(const SatRecord &);
    Sense sense() const;
    std::optional<SatBSplineCurve> bspline() const;
    std::optional<SatBSplineCurve> bspline_in(const SatDocument &) const;
    std::optional<SatCurveSupport> support_in(const SatDocument &) const;
    bool is_closed_in(const SatDocument &) const;
    std::vector<Vector3> sample(std::size_t segments) const;
    std::vector<Vector3> sample_range(double start, double end, std::size_t segments) const;
};
using SatBs3Curve = SatIntCurve;
class SatPCurve {
    SatRecord record_;
    explicit SatPCurve(const SatRecord &r) : record_(r) {}

  public:
    static std::optional<SatPCurve> from_record(const SatRecord &);
    Sense sense() const;
    std::optional<SatBSplinePCurve> bspline_in(const SatDocument &) const;
    std::vector<Vector2> sample(std::size_t segments) const;
    std::vector<Vector2> sample_in(const SatDocument &, std::size_t segments) const;
};
enum class SatSplineSurfaceKind { Spline, Mesh, Bs3 };
class SatSplineSurface {
    SatRecord record_;
    SatSplineSurfaceKind kind_;
    SatSplineSurface(const SatRecord &r, SatSplineSurfaceKind k) : record_(r), kind_(k) {}

  public:
    static std::optional<SatSplineSurface> from_record(const SatRecord &);
    SatSplineSurfaceKind kind() const { return kind_; }
    Sense sense() const;
    const SatRecord &record() const { return record_; }
    std::optional<std::vector<SatToken>> definition_tokens(const SatDocument &) const;
    std::optional<SatBSplineSurface> bspline(const SatDocument &) const;
    std::optional<std::pair<SatBSplineSurface, double>> offset_in(const SatDocument &) const;
};
using SatMeshSurface = SatSplineSurface;
using SatBs3Surface = SatSplineSurface;

SatDocument build_box(Vector3 center, double length, double width, double height);
SatDocument build_wedge(Vector3 origin, double length, double width, double height);
SatDocument build_pyramid(Vector3 center, double base_size, double height);
SatDocument build_cylinder(Vector3 center, double radius, double height);
SatDocument build_cone(Vector3 center, double radius, double height);
SatDocument build_sphere(Vector3 center, double radius);
SatDocument build_torus(Vector3 center, double major_radius, double minor_radius);
std::optional<SatDocument> build_planar_body(const std::vector<Vector3> &vertices,
                                             const std::vector<std::vector<std::size_t>> &faces);

struct SabError : std::runtime_error {
    std::size_t offset;
    SabError(std::string message, std::size_t at) : std::runtime_error(std::move(message)), offset(at) {}
};
struct SabReader {
    static std::pair<SatDocument, std::size_t> read_with_consumed(const std::vector<std::uint8_t> &data);
    static SatDocument read(const std::vector<std::uint8_t> &data) { return read_with_consumed(data).first; }
};
struct SabWriter {
    // Canonical ACIS/ASM header, typed tags and contextual spline numeric roles.
    static std::vector<std::uint8_t> write(const SatDocument &document);
};
std::string_view base_entity_type(std::string_view type);
} // namespace libopencad::acis
