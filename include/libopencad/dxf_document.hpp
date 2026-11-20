// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "acis.hpp"
#include "surface.hpp"
#include "solid_history.hpp"
#include "audit.hpp"
#include "arch.hpp"
#include "dynamic_block.hpp"
#include "associative.hpp"
#include <libopencad/bounds.hpp>
#include <libopencad/transform.hpp>
#include <libopencad/mesh.hpp>
#include "dxf_read.hpp"
#include "dxf_r12.hpp"
#include "dxf_convert.hpp"

#include "color.hpp"
#include "hatch.hpp"
#include "dimension.hpp"
#include "mline.hpp"
#include "multileader.hpp"
#include "table.hpp"
#include "table_content.hpp"
#include "cell_style_map.hpp"
#include "external_reference.hpp"
#include "transparency.hpp"
#include "material.hpp"
#include "field.hpp"
#include "proxy.hpp"
#include "auxiliary_object.hpp"
#include "extended_entity.hpp"
#include "drawing_object.hpp"
#include "class_object.hpp"
#include "object_context.hpp"
#include "data_object.hpp"
#include "semantic_property.hpp"
#include "dxf_version.hpp"
#include "line_weight.hpp"
#include "layer_state.hpp"
#include "sheet_set.hpp"
#include "nested_copy.hpp"
#include "vector.hpp"
#include "handle_reservation.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace libopencad {

// The code distinguishes values sharing a C++ type (e.g. point vs displacement).
struct XDataValue {
    int code;
    std::variant<std::string, std::vector<std::uint8_t>, std::uint64_t,
                 Point3, double, std::int16_t, std::int32_t> value;
};
struct ExtendedDataRecord {
    std::string application_name;
    std::vector<XDataValue> values;
};

#define LIBOPENCAD_IN_DXF_DOCUMENT 1
#include "semantic_inventory.hpp"
#undef LIBOPENCAD_IN_DXF_DOCUMENT

class DxfRecord {
public:
    std::string type() const;
    // Record identity (group 5, or 105 for DIMSTYLE).
    std::string handle() const;
    std::string owner_handle() const;
    std::vector<ExtendedDataRecord> extended_data() const;
protected:
    std::vector<DxfRawField> subclass_fields(std::string_view subclass) const;
    void set_subclass_fields(std::string_view subclass, const std::vector<DxfRawField>& fields,
                             bool literal_binary_strings = false, bool allow_extended_codes = false);
    std::string field(std::string_view subclass, int code, std::string fallback = {}) const;
    void set_field(std::string_view subclass, int code, std::string value);
    std::string text_field(std::string_view subclass, int code, std::string fallback = {}) const;
    void set_text_field(std::string_view subclass, int code, std::string value);
    int short_field(std::string_view subclass, int code, int fallback) const;
    void set_short_field(std::string_view subclass, int code, int value);
    double number(std::string_view subclass, int code, double fallback) const;
    void set_number(std::string_view subclass, int code, double value);
    Point3 point(std::string_view subclass, int code) const;
    Point3 point_or(std::string_view subclass, int code, Point3 fallback) const;
    void set_point(std::string_view subclass, int code, Point3 value);
private:
    friend class DynamicBlockReader;
    friend class DynamicBlockObjectReader;
    friend class AssociativeReader;
    friend class SolidHistoryReader;
    friend class DxfModelerEntity;
    friend class R12Exporter;
    friend class DxfVersionConverter;
    struct Pair { int code; std::string value; };
    struct Position { std::size_t index; bool found; };
    template<class Predicate> Position locate_if(std::string_view subclass, Predicate match) const;
    double number_at(std::size_t index) const;
    std::string decode_string(std::string_view bytes) const;
    std::string encode_string(std::string_view value) const;
    std::string encode_number(double value) const;
    Position locate(std::string_view subclass, int code) const;
    std::vector<std::pair<std::size_t, std::size_t>> xdata_ranges() const;
    void upsert_extended_data(const ExtendedDataRecord& data);
    std::vector<SemanticProperty> semantic_properties() const;
    void set_semantic_properties(const std::vector<SemanticProperty>& value);
    bool binary_ = false;
    const char* code_page_ = nullptr; // Static codec name, raw pairs retain source bytes.
    std::vector<Pair> pairs_;
    friend class DwgDocument;
    friend class DxfDocument;
    friend class SemanticInventoryV1;
    friend class DxfEntity;
    friend class DxfUnderlayDefinition;
    friend class DxfProxyObject;
    friend class DxfDataObject;
    friend class DxfObjectContext;
    friend class DxfSemanticEntity;
    friend class DxfSemanticObject;
    friend class DxfLineType;
    friend class DxfLwPolyline;
    friend class DxfPolyline2D;
    friend class DxfPolyline3D;
    friend class DxfVertexSequence;
    friend class DxfPolyfaceMesh;
    friend class DxfSpline;
    friend class DxfHelix;
    friend class DxfMText;
    friend class DxfProxyEntity;
    friend class DxfModelerEntity;
    friend class DxfExtendedEntity;
    friend class DxfInsert;
    friend class DxfText;
    friend class DxfAttribute;
    friend class DxfViewport;
    friend class DxfLeader;
    friend class DxfHatch;
    friend class DxfDimension;
    friend class DxfMLine;
    friend class DxfMultiLeader;
    friend class DxfTable;
    friend class DxfMultiLeaderStyle;
    friend class DxfMLineStyle;
};

struct PlotSettingsDefinition {
    std::string page_name, printer_name, paper_size, view_name, style_sheet;
    double margin_left=0, margin_bottom=0, margin_right=0, margin_top=0;
    double paper_width=0, paper_height=0, origin_x=0, origin_y=0;
    double window_min_x=0, window_min_y=0, window_max_x=0, window_max_y=0;
    double scale_numerator=1, scale_denominator=1, scale_factor=1, image_origin_x=0, image_origin_y=0;
    std::int16_t flags=0, paper_units=0, rotation=0, plot_type=5, scale_type=0;
    std::int16_t shade_mode=0, shade_resolution=0, shade_dpi=300;
    std::uint64_t visual_style=0;
};
struct LayoutDefinition {
    std::int16_t flags=0, tab_order=0, ucs_ortho_type=0;
    Vector2 min_limits{}, max_limits{};
    Point3 insertion_base{}, min_extents{}, max_extents{12,9,0};
    Point3 ucs_origin{}, ucs_x_axis{1,0,0}, ucs_y_axis{0,1,0};
    double elevation=0;
    std::uint64_t named_ucs=0, base_ucs=0;
};
class DxfLayout : public DxfRecord {
public:
    std::string name() const;
    std::string block_record_handle() const;
    std::string viewport_handle() const;
    LayoutDefinition definition() const;
    void set_definition(const LayoutDefinition& value);
    PlotSettingsDefinition plot_settings() const;
    void set_plot_settings(const PlotSettingsDefinition& value);
};

class DxfBlockRecord : public DxfRecord {
public:
    std::string name() const;
    bool is_model_space() const;
    bool is_paper_space() const;
    // AcDbBlockTableRecord group 70 is insertion units (rust BlockRecord::units).
    // Pre-R2007 files may store the same value as ACAD DesignCenter XDATA.
    std::int16_t units() const;
    // Same DXF group 70 as units(); BLOCK type flags live on DxfBlock.
    std::int16_t flags() const;
};

class DxfLayer : public DxfRecord {
public:
    std::string name() const;
    int signed_color_index() const; // Negative means the layer is switched off.
    void set_signed_color_index(int index);
    std::string linetype() const;
};

struct LineTypeComplexData {
    enum class Content { Shape, Text };
    Content content = Content::Shape;
    int shape_number = 0;
    std::string text;
    std::uint64_t style_handle = 0;
    double scale = 1, rotation = 0; // Radians.
    std::array<double, 2> offset{0, 0};
    bool absolute_rotation = false, upright = false;
    std::uint8_t source_flags = 0;
};

struct LineTypeElement {
    double length = 0; // Positive dash, negative gap, zero dot.
    std::optional<LineTypeComplexData> complex;
};

struct LineTypeDefinition {
    std::string name, description;
    std::vector<LineTypeElement> elements;
    double pattern_length = 0;
    bool xref_dependent = false;
};

class DxfLineType : public DxfRecord {
public:
    std::string name() const;
    std::string description() const;
    double pattern_length() const;
    bool xref_dependent() const;
    std::vector<LineTypeElement> elements() const;
};

struct EntityDisplayProperties {
    double linetype_scale=1;
    Transparency transparency;
    std::string color_name;
    // DXF group 347/284 are retained even though the pinned Rust DXF reader omits them.
    std::uint64_t material=0,plot_style=0;
    // 0 ByLayer, 1 ByBlock, 3 by handle. rust DXF omits material_flags (DWG/in-memory).
    std::uint8_t material_flags=0;
    // DXF 380: 0 ByLayer, 1 ByBlock, 2 named/dictionary without a 390 handle, 3 by handle.
    std::uint8_t shadow_flags=0,plot_style_mode=0;
    std::vector<std::uint64_t> visual_styles; // Full, face, edge, in wire order.
};
class DxfEntity : public DxfRecord {
public:
    std::string layer() const;
    void set_layer(std::string name);
    Color color() const;
    void set_color(Color value);
    // An absent group 6 is returned as an empty name (ByLayer).
    Transparency transparency() const;
    void set_transparency(Transparency value);
    double linetype_scale() const;
    void set_linetype_scale(double value);
    std::string linetype() const;
    void set_linetype(std::string_view name);
    LineWeight line_weight() const;
    void set_line_weight(LineWeight value);
    bool is_invisible() const;
    void set_invisible(bool invisible);
    std::optional<std::vector<std::uint8_t>> graphic_data() const;
    void set_graphic_data(const std::optional<std::vector<std::uint8_t>>& value);
    EntityDisplayProperties display_properties() const;
    void set_display_properties(const EntityDisplayProperties& value);
protected:
    void transform_extended_data(const Transform& transform);
private:
    std::int16_t common_short(int code, std::int16_t fallback) const;
    void replace_common_fields(std::initializer_list<int> codes, std::vector<Pair> replacement);
    std::uint8_t material_flags_ = 0;
    friend class DxfMText;
    friend class DxfInsert;
    friend class DxfText;
    friend class DxfAttribute;
    friend class DxfViewport;
    friend class DxfLeader;
    friend class DxfHatch;
    friend class DxfDimension;
    friend class DxfMLine;
    friend class DxfMultiLeader;
    friend class DxfTable;
    friend class DxfMultiLeaderStyle;
    friend class DxfMLineStyle;
    friend class DxfProxyEntity;
    friend class DxfModelerEntity;
    friend class DxfVersionConverter;
    DxfVersion version_ = DxfVersion::AC1032;
    // Explicit set_color also replaces a DWG book reference with its fallback ACI.
    bool uses_dwg_color_book_ = false;
    friend class DwgDocument;
    friend class DxfDocument;
};

struct ViewportDefinition {
    Point3 center{}, view_center{}, snap_base{}, snap_spacing{10,10,0}, grid_spacing{10,10,0};
    Point3 view_direction{0,0,1}, view_target{}, ucs_origin{}, ucs_x_axis{1,0,0}, ucs_y_axis{0,1,0};
    double width=297, height=210, lens_length=50, front_clip_z=0, back_clip_z=0, view_height=210;
    double snap_angle=0, twist_angle=0, elevation=0, brightness=0, contrast=0; // Angles in radians.
    std::int16_t id=0, circle_sides=1000, render_mode=0, ucs_ortho_type=0, shade_plot_mode=0, grid_major=5, default_lighting_type=1;
    std::optional<std::int16_t> dxf_status;
    std::int32_t status_flags=0x8000; // Includes grid behavior in bits 18..21.
    bool off_screen=false, ucs_at_origin=false, ucs_per_viewport=false, default_lighting=true;
    std::string style_sheet;
    std::vector<std::uint64_t> frozen_layers;
    std::uint64_t clip_boundary=0, ucs=0, base_ucs=0, background=0, shade_plot=0, visual_style=0, sun=0;
    Color ambient_color=Color::by_block();
    bool is_on() const { return !(status_flags&0x20000)&&(status_flags&0x8000); }
};
class DxfViewport : public DxfEntity {
public:
    void translate(Vector3 offset); // Moves center only, matching the upstream direct path.
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    ViewportDefinition definition() const;
    void set_definition(const ViewportDefinition& value);
};

enum class ModelerKind { Solid3D, Region, Body, Surface, PlaneSurface, ExtrudedSurface, LoftedSurface, RevolvedSurface, SweptSurface, NurbSurface };
class DxfModelerEntity : public DxfEntity {
public:
    ModelerKind kind() const;
    std::string uid() const;
    std::string history_handle() const;
    std::pair<std::int16_t,std::int16_t> isolines() const;
    void set_isolines(std::int16_t u, std::int16_t v);
    SurfaceDefinition surface_definition() const;
    void set_surface_definition(const SurfaceDefinition& definition);
    // Native subclass inputs only; document query also resolves the loft fallback.
    SurfaceProfiles surface_profiles() const;
private:
    void set_surface_profiles(const SurfaceProfiles& profiles);
    friend class DxfDocument;
};

class DxfLine : public DxfEntity {
public:
    // Atomic geometry and world-XDATA edits. Offsets and mirror planes use WCS.
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    void mirror_x() { apply_mirror(Transform::from_mirror_x()); }
    void mirror_y() { apply_mirror(Transform::from_mirror_y()); }
    void mirror_z() { apply_mirror(Transform::from_mirror_z()); }
    void mirror_about_line(Vector3 first, Vector3 second) {
        apply_mirror(Transform::from_mirror_line(first, second));
    }
    void mirror_about_plane(Vector3 point, Vector3 normal) {
        apply_mirror(Transform::from_mirror_plane(point, normal));
    }

    double length() const;
    Vector3 direction() const;
    Vector3 midpoint() const;
    BoundingBox3D bounding_box() const; // Endpoint bounds, as in Rust; excludes thickness.

    Point3 start() const;
    Point3 end() const;
    void set_start(Point3 value);
    void set_end(Point3 value);
    double thickness() const;
    Point3 normal() const;
    void set_thickness(double value);
    void set_normal(Point3 value);
};

// acadrust entities/{point,ray,xline}.rs and DXF section_reader/section_writer.
class DxfPoint : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    Point3 location() const;
    double thickness() const;
    Point3 normal() const;
    // Pinned upstream reads/writes group 50 directly, without degree conversion.
    double x_axis_angle() const;
    void set_location(Point3 value);
    void set_thickness(double value);
    void set_normal(Point3 value);
    void set_x_axis_angle(double value);
};

class DxfRay : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    Point3 base_point() const;
    // Normalized like Rust's reader/constructor; zero stays zero.
    Vector3 direction() const;
    void set_base_point(Point3 value);
    void set_direction(Vector3 value);
};

class DxfXLine : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    Point3 base_point() const;
    Vector3 direction() const;
    void set_base_point(Point3 value);
    void set_direction(Vector3 value);
};

// SOLID and TRACE share AcDbTrace storage; quadrilateral boundary order is 0,1,3,2.
class DxfSolid : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    Point3 corner(std::size_t index) const;
    void set_corner(std::size_t index, Point3 value);
    bool is_trace() const { return type() == "TRACE"; }
    bool is_triangle() const;
    std::vector<Point3> corners() const;
    std::vector<Point3> boundary_corners() const;
    Point3 normal() const;
    void set_normal(Point3 value);
    double thickness() const;
    void set_thickness(double value);
};

class DxfMesh : public DxfEntity {
public:
    MeshDefinition definition() const;
    void set_definition(const MeshDefinition& value);
    BoundingBox3D bounding_box() const;
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
};

class DxfFace3D : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    Point3 corner(std::size_t index) const;
    void set_corner(std::size_t index, Point3 value);
    bool is_triangle() const;
    std::vector<Point3> corners() const;
    double area() const;
    // Edge indices 0..3 follow stored vertex order; edits preserve unknown flag bits.
    std::uint8_t invisible_edges() const;
    void set_invisible_edges(std::uint8_t flags);
    bool is_edge_invisible(std::size_t index) const;
    void set_edge_invisible(std::size_t index, bool invisible);
};

struct SplineDefinition {
    std::int16_t degree = 3, flags = 0; // Raw DXF flags, including parameterization/frame bits.
    std::vector<double> knots, weights;
    std::vector<Point3> control_points, fit_points;
    Point3 normal{0,0,1}, begin_tangent{}, end_tangent{};
    double knot_tolerance = 0, control_tolerance = 0, fit_tolerance = 0;
};
class DxfSpline : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    SplineDefinition definition() const;
    // Replaces the spline subclass atomically, retaining unknown/private fields.
    void set_definition(const SplineDefinition& value);
};
enum class HelixConstraint : std::uint8_t { TurnHeight, Turns, Height };
struct HelixParameters {
    std::int32_t major_version = 29, maintenance_version = 0;
    Point3 axis_base_point{}, start_point{}, axis_vector{0,0,1};
    double radius = 1, turns = 3, turn_height = 1;
    bool handedness = true;
    HelixConstraint constraint = HelixConstraint::TurnHeight;
};
class DxfHelix : public DxfSpline {
public:
    HelixParameters parameters() const;
    // Parameters and the evaluated spline remain independently editable, as upstream.
    void set_parameters(const HelixParameters& value);
};

// acadrust entities/table.rs: legacy ACAD_TABLE layout and cell values.
class DxfTable : public DxfEntity {
public:
    // Updates the entity only. After edits, use the document's
    // synchronize_table_dimensions to update existing TABLECONTENT dimensions.
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    TableDefinition definition() const;
    void set_definition(const TableDefinition& definition);
    // Replace all merges, clearing previous per-cell merge flags and spans.
    void set_merged_ranges(const std::vector<TableRange>& ranges);
private:
    void transform_table_fields(Point3 direction, double scale);
};
// Stored table-cell geometry; this API does not calculate layout.
struct ShapeDefinition {
    std::string name;
    Point3 insertion{},normal{0,0,1};
    double size=1,rotation=0,relative_x_scale=1,oblique_angle=0,thickness=0;
    std::vector<DxfRawField> extra_fields;
};
class DxfShape : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    ShapeDefinition definition() const;
    void set_definition(const ShapeDefinition& value);
};

class DxfRasterImage : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    std::array<Point3, 4> corners() const;
    BoundingBox3D bounding_box() const; // Full image rectangle; excludes clipping.
    std::vector<Point3> wipeout_world_boundary_vertices() const; // Throws for IMAGE.
    RasterImageDefinition definition() const;
    void set_definition(const RasterImageDefinition& value);
    bool is_wipeout() const { return type()=="WIPEOUT"; }
};
class DxfUnderlay : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    std::vector<Point3> world_clip_boundary() const;
    UnderlayType underlay_type() const;
    UnderlayDefinition definition() const;
    void set_definition(const UnderlayDefinition& value);
};
class DxfImageDefinition : public DxfRecord {
public:
    ImageDefinition definition() const;
    void set_definition(const ImageDefinition& value);
};
class DxfUnderlayDefinition : public DxfRecord {
public:
    UnderlayType underlay_type() const;
    UnderlayFileDefinition definition() const;
    void set_definition(const UnderlayFileDefinition& value);
};
class DxfRasterVariables : public DxfRecord {
public:
    RasterVariablesDefinition definition() const;
    void set_definition(const RasterVariablesDefinition& value);
};
class DxfImageReactor : public DxfRecord {
public:
    std::uint64_t image_handle() const;
    std::int32_t class_version() const;
    void set_class_version(std::int32_t value);
};

class DxfAuxiliaryObject : public DxfRecord {
public:
    AuxiliaryDefinition definition() const;
    void set_definition(const AuxiliaryDefinition& value);
};
class DxfDataObject : public DxfRecord {
public:
    DataObjectDefinition definition() const;
    void set_definition(const DataObjectDefinition& value);
};
class DxfObjectContext : public DxfRecord {
public:
    ObjectContextDefinition definition() const;
    void set_definition(const ObjectContextDefinition& value);
};
class DxfSemanticEntity : public DxfEntity {
public:
    std::vector<SemanticProperty> properties() const {return semantic_properties();}
    void set_properties(const std::vector<SemanticProperty>& value) {set_semantic_properties(value);}
};
class DxfSemanticObject : public DxfRecord {
public:
    std::vector<SemanticProperty> properties() const {return semantic_properties();}
    void set_properties(const std::vector<SemanticProperty>& value) {set_semantic_properties(value);}
};
class DxfDrawingObject : public DxfRecord {
public:
    DrawingObjectDefinition definition() const;
    void set_definition(const DrawingObjectDefinition& value);
};
class DxfClassObject : public DxfRecord {
    friend class DxfVersionConverter;
public:
    ClassObjectData definition() const;
    void set_definition(const ClassObjectData& value);
private:
    DxfVersion version_ = DxfVersion::AC1032;
    friend class DxfDocument;
};
class DxfExtendedEntity : public DxfEntity {
public:
    BoundingBox3D bounding_box() const;
    // Geometry follows the typed extended definition; metadata-only kinds
    // transform geometric XDATA only. OLE payload bytes stay verbatim.
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    ExtendedEntityDefinition definition() const;
    void set_definition(const ExtendedEntityDefinition& value);
};
class DxfProxyEntity : public DxfEntity {
public:
    ProxyDefinition definition() const;
    void set_definition(const ProxyDefinition& value);
};
class DxfProxyObject : public DxfRecord {
    friend class DxfVersionConverter;
public:
    ProxyDefinition definition() const;
    void set_definition(const ProxyDefinition& value);
private:
    DxfVersion version_=DxfVersion::AC1032;
    friend class DxfDocument;
};
class DxfField : public DxfRecord {
public:
    FieldDefinition definition() const;
    void set_definition(const FieldDefinition& value);
};
class DxfFieldList : public DxfRecord {
public:
    FieldListDefinition definition() const;
    void set_definition(const FieldListDefinition& value);
};
class DxfMaterial : public DxfRecord {
public:
    MaterialDefinition definition() const;
    void set_definition(const MaterialDefinition& value);
};
class DxfCellStyleMap : public DxfRecord {
public:
    CellStyleMapDefinition definition() const;
    void set_definition(const CellStyleMapDefinition& definition);
};
class DxfTableGeometry : public DxfRecord {
public:
    TableGeometryDefinition definition() const;
    void set_definition(const TableGeometryDefinition& definition);
};
// TABLECONTENT and TABLETEMPLATE share the same DXF payload.
class DxfTableContent : public DxfRecord {
public:
    bool is_template() const { return type()=="TABLETEMPLATE"; }
    TableContentDefinition definition() const;
    void set_definition(const TableContentDefinition& definition);
};
class DxfTableStyle : public DxfRecord {
public:
    TableStyleDefinition definition() const;
    void set_definition(const TableStyleDefinition& definition);
};
class DxfMultiLeaderStyle : public DxfRecord {
public:
    MultiLeaderStyleDefinition definition() const;
    void set_definition(const MultiLeaderStyleDefinition& definition);
};
class DxfMultiLeader : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    BoundingBox3D bounding_box() const;
    MultiLeaderDefinition definition() const;
    void set_definition(const MultiLeaderDefinition& definition);
private:
    std::optional<double> text_height_; // Upstream in-memory field; not serialized by DXF.
    struct InactiveGeometry {
        Point3 text_normal, text_location, text_direction, block_location, block_scale;
        double text_rotation, text_width, text_boundary_height, block_rotation;
    };
    std::optional<InactiveGeometry> inactive_geometry_;
    std::pair<std::size_t,std::size_t> scope_range() const;
};
class DxfMLineStyle : public DxfRecord {
public:
    std::string name() const;
    MLineStyleDefinition definition() const;
    void set_definition(const MLineStyleDefinition& definition);
};
class DxfMLine : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    std::optional<BoundingBox3D> bounding_box() const;
    double length() const;
    MLineDefinition definition() const;
    void set_definition(const MLineDefinition& definition);
};
class DxfDimension : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    double measurement() const; // Angular dimensions in degrees; others in drawing units.
    double stored_measurement() const; // Angular dimensions in radians (DXF group 42).
    void refresh_measurement();
    BoundingBox3D bounding_box() const; // Definition geometry, excluding rendered text/arrows.
    DimensionDefinition definition() const;
    void set_definition(const DimensionDefinition& value);
private:
    double insertion_rotation_ = 0;
};

class DxfHatch : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform, const MirrorOptions& options = {});
    std::optional<Vector2> stored_pattern_origin() const;
    Vector2 pattern_origin() const;
    bool record_pattern_origin(Vector2 origin); // ACAD top-level 1010, without moving pattern lines.
    bool set_pattern_origin(Vector2 origin); // Move pattern lines by the change in origin.
    HatchDefinition definition() const;
    void set_definition(const HatchDefinition& value);
private:
    std::optional<Vector2> solid_pattern_parameters_; // Angle/scale omitted by solid DXF output.
};

struct ToleranceDefinition {
    std::string text, dimension_style = "Standard";
    Point3 insertion{}, direction{1,0,0}, normal{0,0,1};
    // In-memory/DWG fields; DXF reload restores the upstream defaults.
    double text_height = .18, dimension_gap = .09;
};
class DxfTolerance : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    BoundingBox3D bounding_box() const;
    std::size_t line_count() const;
    ToleranceDefinition definition() const;
    void set_definition(const ToleranceDefinition& value);
private:
    double text_height_ = .18, dimension_gap_ = .09;
};
struct LeaderDefinition {
    std::string dimension_style = "Standard";
    bool arrow_enabled = true, hookline_enabled = false;
    std::int16_t path_type = 0, creation_type = 0, hookline_direction = 0;
    double text_height = 2.5, text_width = 0;
    std::vector<Point3> vertices;
    Color override_color;
    std::uint64_t annotation_handle = 0;
    Point3 normal{0,0,1}, horizontal_direction{1,0,0}, block_offset{}, annotation_offset{};
    Point3 origin{}; // In-memory/DWG origin, omitted by the upstream DXF writer.
};
class DxfLeader : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    BoundingBox3D bounding_box() const;
    double length() const;
    LeaderDefinition definition() const;
    void set_definition(const LeaderDefinition& value);
private:
    std::optional<Point3> origin_;
};

struct MTextColumns {
    std::int16_t type = 0; // 0 none, 1 static, 2 dynamic.
    std::int32_t count = 0;
    bool flow_reversed = false, auto_height = false;
    double width = 0, gutter = 0;
    std::vector<double> heights;
};
struct MTextDefinition {
    std::string text, style = "Standard";
    Point3 insertion{}, normal{0,0,1};
    double height = 1, rectangle_width = 10, rotation = 0;
    std::optional<double> rectangle_height;
    // An explicit direction takes precedence over rotation, as in Rust.
    std::optional<Vector3> x_direction;
    std::int16_t attachment = 1, drawing_direction = 1, line_spacing_style = 1;
    double line_spacing_factor = 1, extents_width = 0, extents_height = 0;
    std::int32_t background_flags = 0, background_transparency = 0;
    double background_scale = 1.5;
    Color background_color;
    MTextColumns columns;
};
class DxfMText : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform, const MirrorOptions& options = {});

    std::string text() const;
    void set_text(std::string_view value);
    MTextDefinition definition() const;
    // Atomic edit; column representation follows the document's DXF version.
    void set_definition(const MTextDefinition& value);
private:
    std::vector<Pair> text_pairs(std::string_view value) const;
    void replace_fields(const std::vector<int>& codes, const std::vector<Pair>& values);
    std::pair<std::size_t,std::size_t> embedded_range() const;
};

class DxfCircle : public DxfEntity {
public:
    // Atomic geometry and world-XDATA edits. Offsets and mirror planes use WCS.
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    void mirror_x() { apply_mirror(Transform::from_mirror_x()); }
    void mirror_y() { apply_mirror(Transform::from_mirror_y()); }
    void mirror_z() { apply_mirror(Transform::from_mirror_z()); }
    void mirror_about_line(Vector3 first, Vector3 second) {
        apply_mirror(Transform::from_mirror_line(first, second));
    }
    void mirror_about_plane(Vector3 point, Vector3 normal) {
        apply_mirror(Transform::from_mirror_plane(point, normal));
    }

    double diameter() const;
    double circumference() const;
    double area() const;
    Vector3 center_wcs() const;
    std::pair<Vector3, Vector3> axes_wcs() const;
    Vector3 point_at_angle_wcs(double angle) const; // Radians.
    BoundingBox3D bounding_box() const;

    Point3 center() const;
    double radius() const;
    double thickness() const;
    Point3 normal() const;
    void set_center(Point3 value);
    void set_radius(double value);
    void set_thickness(double value);
    void set_normal(Point3 value);
};

// ARC stores its center, radius, thickness and normal in AcDbCircle.
class DxfArc : public DxfCircle {
public:
    // Atomic geometry and world-XDATA edits. Offsets and mirror planes use WCS.
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    void mirror_x() { apply_mirror(Transform::from_mirror_x()); }
    void mirror_y() { apply_mirror(Transform::from_mirror_y()); }
    void mirror_z() { apply_mirror(Transform::from_mirror_z()); }
    void mirror_about_line(Vector3 first, Vector3 second) {
        apply_mirror(Transform::from_mirror_line(first, second));
    }
    void mirror_about_plane(Vector3 point, Vector3 normal) {
        apply_mirror(Transform::from_mirror_plane(point, normal));
    }

    double sweep_angle() const; // Rust adds one turn to a negative difference.
    double arc_length() const;
    Vector3 start_point() const; // OCS; use the _wcs variants for world coordinates.
    Vector3 end_point() const;
    Vector3 midpoint() const;
    Vector3 point_at_angle_wcs(double angle) const;
    Vector3 start_point_wcs() const;
    Vector3 end_point_wcs() const;
    Vector3 midpoint_wcs() const;
    BoundingBox3D bounding_box() const;

    double start_angle_radians() const;
    double end_angle_radians() const;
    void set_start_angle_radians(double value);
    void set_end_angle_radians(double value);
};

class DxfEllipse : public DxfEntity {
public:
    // Atomic geometry and world-XDATA edits. Offsets and mirror planes use WCS.
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    void mirror_x() { apply_mirror(Transform::from_mirror_x()); }
    void mirror_y() { apply_mirror(Transform::from_mirror_y()); }
    void mirror_z() { apply_mirror(Transform::from_mirror_z()); }
    void mirror_about_line(Vector3 first, Vector3 second) {
        apply_mirror(Transform::from_mirror_line(first, second));
    }
    void mirror_about_plane(Vector3 point, Vector3 normal) {
        apply_mirror(Transform::from_mirror_plane(point, normal));
    }

    double major_axis_length() const;
    double minor_axis_length() const;
    bool is_full() const;
    // Matches Rust's simplified XY radius box, not exact tilted/trimmed ellipse bounds.
    BoundingBox3D bounding_box() const;

    Point3 center() const;
    Point3 major_axis() const; // Vector relative to the center.
    double minor_axis_ratio() const;
    Point3 normal() const;
    double start_parameter() const; // Radians.
    double end_parameter() const;
    void set_center(Point3 value);
    void set_major_axis(Point3 value);
    void set_minor_axis_ratio(double value);
    void set_normal(Point3 value);
    void set_start_parameter(double value);
    void set_end_parameter(double value);
};

struct PolylineVertex2D {
    Point3 location; // Coordinates in the object's coordinate system.
    double start_width, end_width, bulge;
    std::string handle, owner_handle;
    std::int16_t flags=0;
    double curve_tangent=0;
    std::int32_t id=0;
};

struct PolylineVertex3D {
    Point3 position; // XYZ in world coordinates.
    int flags;
    std::string layer, handle, owner_handle;
    double bulge=0,start_width=0,end_width=0,curve_tangent=0;
    std::int32_t id=0;
};

struct PolylineHeader {
    std::int16_t flags=0,smooth_type=0,m_count=0,n_count=0,m_density=0,n_density=0;
    double elevation=0,thickness=0,start_width=0,end_width=0;
    Point3 normal{0,0,1};
};
// Ordered children and persistent SEQEND identities shared by POLYLINE and INSERT.
class DxfCompoundEntity : public DxfEntity {
public:
    const DxfRecord* seqend() const noexcept { return seqend_ ? &*seqend_ : nullptr; }
protected:
    friend class R12Exporter;
    friend class DxfVersionConverter;
    std::vector<DxfRecord> children_;
    std::optional<DxfRecord> seqend_;
    friend class DxfDocument;
};
class DxfVertexSequence : public DxfCompoundEntity {
public:
    // POLYLINE only; INSERT sequences reject these geometry-header operations.
    PolylineHeader polyline_header() const;
    void set_polyline_header(const PolylineHeader& value);
protected:
    std::string_view polyline_scope() const;
    PolylineVertex3D spatial_vertex(std::size_t index, std::string_view subclass) const;
    void set_spatial_vertex(std::size_t index, std::string_view subclass, Point3 position);
};

class DxfAttribute;
struct AttributeDefinition;
struct InsertDefinition {
    std::string block_name;
    Point3 insertion{}, scale{1,1,1}, normal{0,0,1};
    double rotation = 0, column_spacing = 0, row_spacing = 0; // Radians.
    std::uint16_t columns = 1, rows = 1;
};
class DxfInsert : public DxfCompoundEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform, const MirrorOptions& options = {});

    Transform get_transform() const;
    // Row-major OCS insertion points; throws above 1,000,000 cells.
    std::vector<Point3> array_points() const;
    InsertDefinition definition() const;
    void set_definition(const InsertDefinition& value);
    std::size_t attribute_count() const noexcept { return children_.size(); }
    DxfAttribute attribute(std::size_t index) const;
    void set_attribute(std::size_t index, const DxfAttribute& value);
private:
    std::string_view subclass() const;
    friend class DxfDocument;
};
class DxfBlock : public DxfEntity {
public:
    std::string name() const;
    Point3 base_point() const;
    void set_base_point(Point3 value);
    std::string description() const;
    void set_description(std::string_view value);
    std::string xref_path() const;
    void set_xref_path(std::string_view value);
    std::int16_t flags() const;
    void set_flags(std::int16_t value);
};

class DxfPolylineSequence : public DxfVertexSequence {
public:
    std::size_t vertex_count() const noexcept { return children_.size(); }
};

class DxfPolyline2D : public DxfPolylineSequence {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    std::int16_t flags() const;
    void set_flags(std::int16_t value);
    void set_closed(bool closed);
    double elevation() const;
    double thickness() const;
    Point3 normal() const;
    std::int16_t smooth_type() const;
    void set_elevation(double value);
    void set_thickness(double value);
    void set_normal(Point3 value);
    void set_smooth_type(std::int16_t value);
    void set_default_widths(double start, double end);
    void set_vertex_metadata(std::size_t index, std::int16_t flags, double tangent, std::int32_t id);
    bool is_closed() const;
    double default_start_width() const;
    double default_end_width() const;
    PolylineVertex2D vertex(std::size_t index) const;
    void set_vertex_position(std::size_t index, double x, double y);
    void set_vertex_widths(std::size_t index, double start, double end);
    void set_bulge(std::size_t index, double value);
};

class DxfPolyline3D : public DxfPolylineSequence {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    void set_closed(bool value);
    void set_vertex_flags(std::size_t index, std::int16_t value);
    bool is_closed() const;
    PolylineVertex3D vertex(std::size_t index) const;
    void set_vertex_position(std::size_t index, Point3 position);
};

class DxfPolygonMesh : public DxfPolylineSequence {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    int m_vertex_count() const;
    int n_vertex_count() const;
    bool is_closed_m() const;
    bool is_closed_n() const;
    // Maps a zero-based grid coordinate to an existing VERTEX in file order.
    std::size_t grid_index(std::size_t m, std::size_t n) const;
    PolylineVertex3D vertex(std::size_t index) const;
    void set_vertex_position(std::size_t index, Point3 position);
};

struct PolyfaceFace {
    // Signed one-based DXF references. A negative value hides the outgoing edge.
    std::array<int, 4> indices;
    std::string layer, handle, owner_handle;
    std::optional<Color> color;
    std::int16_t flags=128;
    // The fourth slot can also repeat the third vertex on a triangle.
    bool is_triangle() const noexcept;
};

class DxfPolyfaceMesh : public DxfVertexSequence {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);

    std::size_t vertex_count() const noexcept { return coordinate_records_.size(); }
    std::size_t face_count() const noexcept { return face_records_.size(); }
    PolylineVertex3D vertex(std::size_t index) const;
    PolyfaceFace face(std::size_t index) const;
    void set_face_indices(std::size_t index, std::array<int, 4> indices);
    void set_vertex_position(std::size_t index, Point3 value);
    void set_vertex_metadata(std::size_t index, const PolylineVertex3D& value);
    void set_face_color(std::size_t index, Color value);
private:
    void index_children();
    std::vector<std::size_t> coordinate_records_, face_records_;
    friend class DxfDocument;
};

struct LwVertex {
    double x, y;
    double bulge; // Outgoing segment, including last-to-first when closed.
    double start_width, end_width;
    int vertex_id;
};

class DxfLwPolyline : public DxfEntity {
public:
    // Atomic geometry and world-XDATA edits. Offsets and mirror planes use WCS.
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform);
    void mirror_x() { apply_mirror(Transform::from_mirror_x()); }
    void mirror_y() { apply_mirror(Transform::from_mirror_y()); }
    void mirror_z() { apply_mirror(Transform::from_mirror_z()); }
    void mirror_about_line(Vector3 first, Vector3 second) {
        apply_mirror(Transform::from_mirror_line(first, second));
    }
    void mirror_about_plane(Vector3 point, Vector3 normal) {
        apply_mirror(Transform::from_mirror_plane(point, normal));
    }

    std::size_t vertex_count() const;
    // Rust vertex-only OCS box mapped to WCS; excludes bulge, widths and thickness.
    BoundingBox3D bounding_box() const;

    std::vector<LwVertex> vertices() const;
    void set_vertex_position(std::size_t index, double x, double y);
    void set_bulge(std::size_t index, double value);
    bool is_closed() const;
    void set_closed(bool closed);
    bool plinegen() const;
    void set_plinegen(bool value);
    void set_vertex_widths(std::size_t index, double start, double end);
    void set_vertex_id(std::size_t index, std::int32_t value);
    void set_vertices(const std::vector<LwVertex>& value);
    void set_thickness(double value);
    void set_constant_width(double value);
    void set_normal(Point3 value);
    double elevation() const;
    void set_elevation(double value);
    double thickness() const;
    double constant_width() const;
    Point3 normal() const;
private:
    static constexpr std::size_t missing = std::string_view::npos;
    struct VertexFields {
        std::size_t x, y;
        std::size_t bulge = missing, start_width = missing, end_width = missing, id = missing;
    };
    std::vector<VertexFields> vertex_fields() const;
};

struct TextDefinition {
    std::string text, style="Standard";
    Point3 insertion{}, normal{0,0,1};
    std::optional<Point3> alignment_point;
    double height=1, thickness=0, rotation=0, width_factor=1, oblique_angle=0; // Angles in radians.
    std::int16_t generation_flags=0, horizontal_alignment=0, vertical_alignment=0;
};
class DxfText : public DxfEntity {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform, const MirrorOptions& options = {});

    TextDefinition definition() const;
    void set_definition(const TextDefinition& value);
    Point3 insertion_point() const;
    double height() const;
    std::string text() const;
    void set_insertion_point(Point3 value);
    void set_height(double value);
    void set_text(std::string value);
private:
    Position vertical_position() const;
};
struct AttributeDefinition {
    TextDefinition text;
    std::string tag, prompt; // Prompt applies to ATTDEF only.
    std::int16_t flags=0, field_length=0;
    bool lock_position=false, multiline=false;
    std::optional<MTextDefinition> embedded_mtext;
};
// Shared ATTRIB/ATTDEF representation; ATTRIB children are accessed through INSERT.
class DxfAttribute : public DxfText {
public:
    void translate(Vector3 offset);
    void apply_transform(const Transform& transform);
    void apply_mirror(const Transform& transform, const MirrorOptions& options = {});

    AttributeDefinition attribute_definition() const;
    void set_attribute_definition(const AttributeDefinition& value);
private:
    std::string_view attribute_subclass() const;
};


enum class OwnershipStatus {
    Root, MissingOwner, InvalidHandle, DanglingReference, AmbiguousHandle, Cycle
};

struct OwnershipChain {
    // Source followed by each uniquely resolved ancestor, excluding a repeated node.
    std::vector<const DxfRecord*> records;
    OwnershipStatus status;
    // Raw terminal owner reference, or the input handle when the source fails.
    std::string reference;
};

enum class EntitySpace { Model, Paper }; // *Model_Space or the default *Paper_Space.

// Detached output of polyline and mesh decomposition. Identities are unset;
// visual properties and source ownership are inherited, graph/XDATA payloads are not.
using ExplodedPrimitive = std::variant<DxfLine, DxfArc, DxfPoint, DxfFace3D>;
// Detached wire-only faces follow the Rust modeler explode contract; they do
// not contain a new ACIS body and cannot be saved as a modeler payload.
struct ModelerWireFace { DxfEntity common; bool planar=true; std::vector<Point3> points; };
using ExplodedModelerPart=std::variant<DxfLine,DxfCircle,DxfEllipse,DxfSpline,ModelerWireFace>;

struct EntityOptions {
    EntitySpace space = EntitySpace::Model;
    std::string layer = "0";
    std::string linetype; // Empty means ByLayer.
    Color color;
    LineWeight line_weight;
    bool invisible = false;
    // Nonempty selects a named block instead of space. The block must already exist.
    std::string block_name;
    std::string layout_name; // Mutually exclusive with block_name.
};

// Common symbol-table operations. Property values use UTF-8 strings or canonical
// ASCII numbers/hex handles, independent of the document's transport encoding.
enum class SymbolTable { Layer, LineType, TextStyle, DimStyle, AppId, Ucs, View, VPort, BlockRecord };
struct SymbolProperty { int code; std::string value; };
// DXF table definitions. Angles are radians; bit fields retain unmodeled bits.
// DXF fields of DimStyle; construction defaults match Rust DimStyle::new.
struct DimStyleDefinition {
    std::int16_t flags = 0;
    bool annotative = false;
    std::int16_t dimclrd = 0;
    double dimdle = 0.0;
    double dimdli = 3.75;
    double dimgap = 0.625;
    std::int16_t dimlwd = -2;
    bool dimsd1 = false;
    bool dimsd2 = false;
    std::int16_t dimclre = 0;
    double dimexe = 1.25;
    double dimexo = 0.625;
    std::int16_t dimlwe = -2;
    bool dimse1 = false;
    bool dimse2 = false;
    double dimfxl = 1.0;
    bool dimfxlon = false;
    double dimasz = 0.18;
    std::uint64_t dimblk = 0;
    std::uint64_t dimblk1 = 0;
    std::uint64_t dimblk2 = 0;
    std::uint64_t dimldrblk = 0;
    bool dimsah = true;
    double dimcen = 0.09;
    double dimtsz = 0.0;
    std::int16_t dimclrt = 0;
    double dimtxt = 0.18;
    std::int16_t dimjust = 0;
    std::int16_t dimtad = 1;
    double dimtvp = 0.0;
    bool dimtih = false;
    bool dimtoh = false;
    bool dimtix = false;
    bool dimsoxd = false;
    std::int16_t dimtfill = 0;
    std::int16_t dimtfillclr = 0;
    std::int16_t dimtmove = 0;
    bool dimtxtdirection = false;
    std::uint64_t dimtxsty_handle = 0;
    double dimscale = 1.0;
    double dimlfac = 1.0;
    std::int16_t dimlunit = 2;
    std::int16_t dimdec = 2;
    double dimrnd = 0.0;
    std::int16_t dimdsep = 46;
    std::int16_t dimzin = 8;
    std::int16_t dimaunit = 0;
    std::int16_t dimadec = 0;
    std::int16_t dimazin = 0;
    bool dimalt = false;
    double dimaltf = 25.4;
    std::int16_t dimaltd = 3;
    std::int16_t dimaltu = 2;
    std::int16_t dimalttd = 3;
    double dimaltrnd = 0.0;
    std::string dimapost = {};
    std::int16_t dimaltz = 0;
    std::int16_t dimalttz = 0;
    bool dimtol = false;
    bool dimlim = false;
    double dimtp = 0.0;
    double dimtm = 0.0;
    std::int16_t dimtdec = 2;
    double dimtfac = 1.0;
    std::int16_t dimtolj = 0;
    std::int16_t dimtzin = 8;
    std::int16_t dimatfit = 3;
    bool dimtofl = false;
    bool dimupt = false;
    std::int16_t dimfit = 0;
    std::string dimpost = "<>";
    std::int16_t dimfrac = 0;
    std::int16_t dimarcsym = 0;
    double dimjogang = 0.78539816339744830962;
    std::uint64_t dimltex_handle = 0;
    std::uint64_t dimltex1_handle = 0;
    std::uint64_t dimltex2_handle = 0;
    std::int16_t dimunit = 2;
};
struct TextStyleDefinition {
    std::int16_t flags = 0, generation_flags = 0;
    double height = 0, width_factor = 1, oblique_angle = 0, last_height = 2.5;
    std::string font_file = "txt", big_font_file, true_type_font;
    std::int32_t true_type_font_flags = 34;
    bool annotative = false;
};
struct UcsDefinition {
    std::int16_t flags = 0;
    Point3 origin = {};
    Point3 x_axis = {1,0,0};
    Point3 y_axis = {0,1,0};
    double elevation = 0;
    std::int16_t ortho_type = 0;
    std::int16_t ortho_view_type = 0;
    std::uint64_t named_ucs_handle = 0;
    std::uint64_t base_ucs_handle = 0;
};
struct ViewDefinition {
    std::int16_t flags = 0;
    std::int16_t view_mode = 0;
    Point3 center = {};
    Point3 direction = {0,0,1};
    Point3 target = {};
    double height = 1;
    double width = 1;
    double lens_length = 50;
    double front_clip = 0;
    double back_clip = 0;
    double twist_angle = 0;
    bool ucs_associated = false;
    bool camera_plottable = false;
    std::int16_t render_mode = 0;
    Point3 ucs_origin = {};
    Point3 ucs_x_axis = {1,0,0};
    Point3 ucs_y_axis = {0,1,0};
    double ucs_elevation = 0;
    std::int16_t ucs_ortho_type = 0;
    std::uint64_t named_ucs_handle = 0;
    std::uint64_t base_ucs_handle = 0;
    std::uint64_t background_handle = 0;
    std::uint64_t visual_style_handle = 0;
    std::uint64_t sun_handle = 0;
    bool use_default_lights = true;
    std::int16_t default_lighting_type = 1;
    double brightness = 0;
    double contrast = 0;
    Color ambient_color = Color::from_index(250);
    std::uint64_t live_section_handle = 0;
};
struct VPortDefinition {
    std::int16_t flags = 0;
    std::int16_t view_mode = 0;
    Vector2 lower_left = {};
    Vector2 upper_right = {1,1};
    Vector2 view_center = {};
    Vector2 snap_base = {};
    Vector2 snap_spacing = {.5,.5};
    Vector2 grid_spacing = {10,10};
    Point3 view_direction = {0,0,1};
    Point3 view_target = {};
    double view_height = 10;
    double aspect_ratio = 1;
    double lens_length = 50;
    double front_clip = 0;
    double back_clip = 0;
    double snap_rotation = 0;
    double view_twist = 0;
    std::int16_t circle_zoom = 100;
    bool fast_zoom = true;
    std::int16_t ucs_icon_flags = 3;
    bool snap_on = false;
    bool grid_on = false;
    bool snap_style = false;
    std::int16_t snap_isopair = 0;
    std::int16_t grid_flags = 2;
    std::int16_t grid_major = 5;
    bool ucs_per_viewport = true;
    std::int16_t render_mode = 0;
    Point3 ucs_origin = {};
    Point3 ucs_x_axis = {1,0,0};
    Point3 ucs_y_axis = {0,1,0};
    double ucs_elevation = 0;
    std::int16_t ucs_ortho_type = 0;
    std::uint64_t named_ucs_handle = 0;
    std::uint64_t base_ucs_handle = 0;
    std::uint64_t background_handle = 0;
    std::uint64_t visual_style_handle = 0;
    std::uint64_t sun_handle = 0;
    bool use_default_lights = true;
    std::int16_t default_lighting_type = 1;
    double brightness = 0;
    double contrast = 0;
    Color ambient_color = Color::from_index(250);
};

struct SymbolEntry {
    std::string name, handle, owner_handle;
    std::vector<SymbolProperty> properties;
};

enum class ReferenceStatus { Invalid, Dangling, Ambiguous, OwnerCycle, MissingOwner, InvalidOwner,
                             UnlistedChild, StaleAttributeFlag };
struct ReferenceIssue {
    std::string source_handle;
    int code;
    std::string target;
    ReferenceStatus status;
    std::optional<SymbolTable> table; // Present for a name-based symbol reference.
};
struct HandleReplacement { std::string from, to; };

class FieldContext;
struct RecoverOutcome;

enum class LayerViewportOverrideKind { Alpha, Color, Linetype, Lineweight };

struct XRecordOverrideValue {
    enum class Kind { Int32, Handle };
    Kind kind = Kind::Int32;
    std::int32_t i32 = 0;
    std::uint64_t handle = 0;
    static XRecordOverrideValue from_int32(std::int32_t value) {
        XRecordOverrideValue out;
        out.kind = Kind::Int32;
        out.i32 = value;
        return out;
    }
    static XRecordOverrideValue from_handle(std::uint64_t value) {
        XRecordOverrideValue out;
        out.kind = Kind::Handle;
        out.handle = value;
        return out;
    }
};

struct LayerViewportOverride {
    std::uint64_t viewport = 0;
    XRecordOverrideValue value;
};

// rust EntityChangeRecorder: first-touch snapshots. Native stores the DXF type
// name rather than an Arc entity graph; absent type means the entity was added.
class EntityChangeRecorder {
  public:
    std::vector<std::pair<std::uint64_t, std::optional<std::string>>> take_before_images() {
        std::vector<std::pair<std::uint64_t, std::optional<std::string>>> out;
        out.reserve(order_.size());
        for (auto handle : order_) {
            auto found = before_.find(handle);
            std::optional<std::string> type;
            if (found != before_.end()) {
                type = std::move(found->second);
                before_.erase(found);
            }
            out.emplace_back(handle, std::move(type));
        }
        order_.clear();
        before_.clear();
        return out;
    }

  private:
    friend class DxfDocument;
    void record(std::uint64_t handle, std::optional<std::string> before) {
        if (before_.count(handle))
            return;
        order_.push_back(handle);
        before_.emplace(handle, std::move(before));
    }
    std::vector<std::uint64_t> order_;
    std::unordered_map<std::uint64_t, std::optional<std::string>> before_;
};

// rust document/block_graph::BlockGraph: a block record's extension-dictionary
// object graph (Dictionary/XRecord/dynamic-block objects) plus XDATA.
struct BlockGraph {
    std::uint64_t record = 0;
    std::optional<std::uint64_t> root;
    std::vector<DxfRecord> objects;
    std::vector<BlockVisibilityParameter> visibility;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> representations;
    std::vector<std::pair<std::uint64_t, std::vector<ExtendedDataRecord>>> xdata;
    std::vector<ClassDefinition> classes;
};

class DxfDocument {
public:
    // AC1032 defaults matching acadrust CadDocument::new, with selectable DXF encoding.
    static DxfDocument create(bool binary = false, std::int16_t insertion_units = 0);
    static DxfDocument read(std::istream& input);
    // Validates group values and framing, with source-position diagnostics.
    // Failsafe skips malformed records at known boundaries; binary framing errors
    // stop recovery rather than guessing a resynchronization point.
    static DxfDocument read(std::istream& input, const DxfReadOptions& options,
                            DxfReadStatistics& statistics);
    // rust DxfReader::recover: failsafe read, usability gate, then repair().
    static RecoverOutcome recover(std::istream& input);
    static RecoverOutcome recover(std::istream& input, DxfReadOptions options);
    // Detects the input encoding and preserves it on write.
    bool is_binary() const noexcept { return binary_; }
    DxfVersion version() const noexcept { return version_; }
    // Raw $INSUNITS code, matching acadrust HeaderVariables::insertion_units.
    std::int16_t insertion_units() const noexcept { return insertion_units_; }
    // Header unit / plot-style fields used by field pictures. Defaults match
    // acadrust HeaderVariables: LUNITS=2, LUPREC=4, AUNITS=0, AUPREC=0, PSTYLEMODE=true.
    std::int16_t linear_unit_format() const;
    std::int16_t linear_unit_precision() const;
    std::int16_t angular_unit_format() const;
    std::int16_t angular_unit_precision() const;
    bool plot_style_mode() const;
    // Header UCS used by versioned AcObjProp.* (rust HeaderVariables $UCS* / $PUCS*).
    Point3 model_space_ucs_origin() const;
    Point3 model_space_ucs_x_axis() const;
    Point3 model_space_ucs_y_axis() const;
    Point3 paper_space_ucs_origin() const;
    Point3 paper_space_ucs_x_axis() const;
    Point3 paper_space_ucs_y_axis() const;
    void set_model_space_ucs(Point3 origin, Point3 x_axis, Point3 y_axis);
    void set_paper_space_ucs(Point3 origin, Point3 x_axis, Point3 y_axis);
    // First candidate after reconciling $HANDSEED with stored record identities.
    // Sorted, distinct $DIM names actually carried by the DXF HEADER.
    std::vector<std::string> dimension_header_variables() const;
    std::uint64_t next_handle() const noexcept { return next_handle_; }
    // Reserve one handle and advance $HANDSEED. Throws on 64-bit exhaustion.
    std::uint64_t allocate_handle();
    void write(std::ostream& output) const;
    // Controlled R13-R2018 output. Unsupported schema changes refuse even
    // with allow_loss; use export_r12 for R12 targets.
    DxfConversionResult export_version(DxfVersion target, bool binary,
                                       const DxfConversionOptions& options = {}) const;
    DxfConversionReport write_version(std::ostream& output, DxfVersion target, bool binary,
                                      const DxfConversionOptions& options = {}) const;
    // Select transport encoding without changing the source document or DXF version/code page.
    void write(std::ostream& output, bool binary) const;
    // Dedicated R12 ASCII conversion. Preview includes every known loss/refusal.
    R12ExportResult export_r12(const R12ExportOptions& options = {}) const;
    // Refusal writes no bytes. Stream I/O failure cannot roll back an external sink.
    R12ExportReport write_r12(std::ostream& output, const R12ExportOptions& options = {}) const;
    // Rust symbol-table Unicode name matching; last matching table entry wins.
    // Queries use UTF-8. Unknown names return nullptr.
    const DxfLayer* get_layer(std::string_view name) const;
    const DxfLineType* get_linetype(std::string_view name) const;
    // Success invalidates existing record pointers/references. Failure leaves the document intact.
    std::uint64_t add_layer(std::string name, int signed_color_index = 7,
                            std::string linetype = "Continuous");
    std::uint64_t add_linetype(const LineTypeDefinition& definition);
    // Full setters require R2000+ for UCS and R2007+ for VIEW/VPORT.
    std::optional<DimStyleDefinition> get_dim_style(std::string_view name) const;
    // Full definition setters require R2010+; legacy defaults are recognized when reading.
    void set_dim_style(std::string_view name, const DimStyleDefinition& value);
    std::uint64_t add_dim_style(std::string name, const DimStyleDefinition& value = {});
    std::optional<TextStyleDefinition> get_text_style(std::string_view name) const;
    void set_text_style(std::string_view name, const TextStyleDefinition& value);
    // Named text styles. Shape-file STYLE records remain available through raw symbol properties.
    std::uint64_t add_text_style(std::string name, const TextStyleDefinition& value = {});
    std::optional<UcsDefinition> get_ucs(std::string_view name) const;
    void set_ucs(std::string_view name, const UcsDefinition& value);
    std::uint64_t add_ucs(std::string name, const UcsDefinition& value = {});
    std::optional<ViewDefinition> get_view(std::string_view name) const;
    void set_view(std::string_view name, const ViewDefinition& value);
    std::uint64_t add_view(std::string name, const ViewDefinition& value = {});
    std::optional<VPortDefinition> get_vport(std::string_view name) const;
    void set_vport(std::string_view name, const VPortDefinition& value);
    std::uint64_t add_vport(std::string name, const VPortDefinition& value = {});
    std::optional<SymbolEntry> get_symbol(SymbolTable table, std::string_view name) const;
    std::vector<SymbolEntry> symbols(SymbolTable table) const;
    // Initialize upstream defaults, then apply optional DXF property overrides.
    std::uint64_t add_symbol(SymbolTable table, std::string name,
                             const std::vector<SymbolProperty>& properties = {});
    std::uint64_t rename_symbol(SymbolTable table, std::string_view old_name, std::string new_name);
    // Refuse referenced/reserved entries. Empty custom blocks include BLOCK/ENDBLK cleanup.
    bool remove_symbol(SymbolTable table, std::string_view name);
    void set_symbol_properties(SymbolTable table, std::string_view name,
                               const std::vector<SymbolProperty>& properties);
    // Read-only audit includes compound identities and raw references. Native storage
    // has no independent object-map/table keys; those Rust issue codes are reserved.
    AuditReport audit() const;
    // Atomic deterministic repair. APPID collisions receive new handles (name-based references).
    // Other duplicate/invalid identities and uncertain references
    // are retained in unresolved; no geometry is guessed or discarded.
    RepairReport repair();
    // Scan all stored records, including compound children, raw entities and OBJECTS.
    std::vector<ReferenceIssue> reference_issues() const;
    // Clear only BLOCK attribute-definition flags whose block has no ATTDEF.
    std::size_t repair_attribute_definition_flags();
    // Explicitly replace references; identities are never changed. Targets must resolve uniquely.
    // Unknown application groups are protected; any unsafe replacement fails atomically.
    std::size_t repair_references(const std::vector<HandleReplacement>& replacements);
    // Fill absent/null entity owners from group 67 or the enclosing named BLOCK.
    // Existing uniquely named space/block records are required; nonzero owners are preserved.
    std::size_t repair_entity_owners();
    std::size_t repair_symbol_references(SymbolTable table, std::string_view old_name, std::string_view target_name);
    std::uint64_t add_extension_dictionary(std::string_view owner_handle);
    std::uint64_t add_dictionary(std::string_view parent_dictionary, std::string name);
    std::uint64_t add_xrecord(std::string_view parent_dictionary, std::string name,
                             const std::vector<SymbolProperty>& values);
    // Native ACAD_LAYERSTATES XRecords on the LAYER table extension dictionary.
    // Capture/store update an existing case-insensitive name in place and keep its handle.
    std::vector<LayerState> layer_states() const;
    std::optional<LayerState> layer_state(std::string_view name) const;
    std::uint64_t capture_layer_state(std::string name, std::string description = {});
    std::uint64_t store_layer_state(LayerState state);
    std::optional<std::size_t> restore_layer_state(std::string_view name);
    bool rename_layer_state(std::string_view old_name, std::string_view new_name);
    bool delete_layer_state(std::string_view name);
    std::optional<SheetSetData> sheet_set_data() const;
    void set_sheet_set_data(const SheetSetData& data);
    NestedCopySymbolNames nested_copy_symbol_names(NestedCopyMode mode) const;
    std::size_t localize_nested_copy_symbols(const std::vector<std::uint64_t>& entities,
                                             const NestedCopySymbolNames& names);
    // Exact application-name matching, as in Rust ExtendedData. Registers APPID if absent.
    // Values are typed, finite, UTF-8; binary values are individual chunks of at most 255 bytes.
    // XRECORD payloads are edited through the XRECORD API, not interpreted as XDATA.
    void set_extended_data(std::string_view handle, const ExtendedDataRecord& data);
    bool remove_extended_data(std::string_view handle, std::string_view application_name);
    // APPID records that no extended data names: entity XDATA (INSERT attributes
    // and mesh vertices included), object XDATA, raw 1001 groups, and an APPID
    // that owns objects (its extension dictionary). ACAD, AcadAnnotative,
    // AcCmTransparency and AcAecLayerStandard stay; writers derive those from
    // modelled fields. Matches acadrust `CadDocument::unused_app_ids`. DXF
    // names applications by 1001 string; DWG EED handle maps are not stored
    // on `DxfDocument`.
    std::vector<std::uint64_t> unused_app_ids() const;
    // Existing layers/linetypes and an unambiguous space BLOCK_RECORD are required.
    // Return the allocated identity. Same invalidation/rollback contract as add_layer.
    const DxfLayout* get_layout(std::string_view name) const;
    std::vector<std::string> entity_handles_in_layout(std::string_view name) const;
    std::uint64_t add_layout(std::string name);
    std::uint64_t add_viewport(const ViewportDefinition& definition, const EntityOptions& options);
    std::uint64_t add_insert(const InsertDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_insert(const InsertDefinition& definition, const std::vector<AttributeDefinition>& attributes,
                             const EntityOptions& options = {});
    std::uint64_t add_attribute_definition(const AttributeDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_line(Point3 start, Point3 end, const EntityOptions& options = {});
    std::uint64_t add_point(Point3 location, const EntityOptions& options = {});
    // R13+ creation. Direction is normalized; finite zero vectors are preserved.
    std::uint64_t add_ray(Point3 base_point, Vector3 direction, const EntityOptions& options = {});
    std::uint64_t add_xline(Point3 base_point, Vector3 direction, const EntityOptions& options = {});
    // Three or four stored DXF corners; triangles repeat their third corner.
    std::uint64_t add_solid(const std::vector<Point3>& corners, const EntityOptions& options = {});
    std::uint64_t add_trace(const std::vector<Point3>& corners, const EntityOptions& options = {});
    std::uint64_t add_face3d(const std::vector<Point3>& corners, std::uint8_t invisible_edges = 0,
                              const EntityOptions& options = {});
    std::uint64_t add_mesh(const MeshDefinition& value, const EntityOptions& options = {});
    std::uint64_t add_spline(const SplineDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_helix(const SplineDefinition& definition, const HelixParameters& parameters = {},
                             const EntityOptions& options = {});
    // Full HATCH construction (including spline fit data) requires R2010+.
    const DxfMLineStyle* get_mline_style(std::string_view name) const;
    std::uint64_t add_mline_style(std::string name, const MLineStyleDefinition& definition);
    const DxfTableStyle* get_table_style(std::string_view name) const;
    std::uint64_t add_table_style(std::string name, const TableStyleDefinition& definition);
    std::uint64_t add_table(const TableDefinition& definition, const EntityOptions& options = {});
    // rust CadDocument table cell fields: FIELD objects owned by the table (DXF 344).
    std::optional<std::uint64_t> set_table_cell_field(std::uint64_t table, std::size_t row, std::size_t column,
                                                      std::string_view template_code,
                                                      const std::vector<FieldDefinition>& children);
    bool remove_table_cell_field(std::uint64_t table, std::size_t row, std::size_t column);
    std::pair<bool, std::size_t> update_table_cell_fields(const FieldContext& ctx, std::uint64_t table);
    // Regenerates the anonymous *T block (SOLID/MTEXT/LINE). False if missing or already matching.
    bool refresh_table_block(std::uint64_t table);
    std::optional<std::vector<std::optional<std::uint64_t>>>
    build_table_block(std::uint64_t table, const std::vector<TableCellField>& fields);
    // rust writer-internal prepare_table_recompose_data. Explicit; write() does not call this.
    void prepare_table_recompose_data();
    // rust CadDocument text-host fields (MTEXT/TEXT/ATTDEF/ATTRIB xdict ACAD_FIELD/TEXT).
    std::optional<std::uint64_t> set_text_field(std::uint64_t host, std::string_view template_code,
                                                const std::vector<FieldDefinition>& children);
    bool remove_text_field(std::uint64_t host);
    std::pair<std::vector<std::uint64_t>, std::size_t> update_fields(const FieldContext& ctx, std::int32_t event,
                                                                    const std::vector<std::uint64_t>* hosts = nullptr);
    std::vector<std::uint64_t> stamp_plot_fields(const FieldContext& ctx);
    std::vector<std::uint64_t> refresh_sheet_set_fields(const FieldContext& ctx);
    std::vector<std::uint64_t> refresh_table_formulas(const FieldContext& ctx, std::uint64_t table);
    std::string format_with_picture(double value, std::string_view picture) const;
    std::string format_point_with_picture(Point3 point, std::string_view picture) const;
    // rust CadDocument table field_handles leftovers: FIELD objects owned by the
    // table (DXF 330) that no cell DXF 344 references, plus their descendants.
    std::vector<std::uint64_t> orphan_table_fields(std::uint64_t table) const;
    std::vector<std::uint64_t> prune_table_fields(std::uint64_t table);
    // rust CadDocument::remove_fields: drop FIELD objects and FIELDLIST entries.
    void remove_fields(const std::vector<std::uint64_t>& doomed);
    // rust CadDocument::attach_attribute_fields: copy ATTDEF hosted fields onto
    // INSERT ATTRIB (tag case-insensitive). rust register_fields stays internal.
    std::vector<std::uint64_t> attach_attribute_fields(std::uint64_t insert, const FieldContext& ctx);
    std::vector<std::uint64_t> attach_attribute_fields_mapped(
        std::uint64_t insert, const FieldContext& ctx,
        const std::function<std::string(std::string_view)>& map);
    // R2007+ modern objects; dictionary insertion and synchronization are atomic.
    std::uint64_t add_table_content(std::string_view dictionary, std::string name,
        const TableContentDefinition& definition, bool as_template=false);
    std::string table_content_handle(std::string_view entity) const;
    // Combines current legacy values with retained modern formats and additional contents.
    TableContentDefinition table_content_definition(std::string_view entity) const;
    // Modern definition is authoritative; projects its first content into each legacy cell.
    void set_table_content(std::string_view entity, const TableContentDefinition& definition);
    // Refresh the linked object after low-level DxfTable edits, retaining modern-only fields.
    void synchronize_table_content(std::string_view entity);
    // Copy only authoritative row/column sizes to an existing TABLECONTENT adjunct.
    // Call outside for_each: successful synchronization invalidates record references.
    // Missing adjuncts are a no-op; mismatched dimensions reject atomically.
    void synchronize_table_dimensions(std::string_view entity);
    std::uint64_t add_table_geometry(std::string_view dictionary, std::string name,
        const TableGeometryDefinition& definition);
    std::string table_geometry_handle(std::string_view entity) const;
    // Returns the stored object, including its declared dimensions and partial caches.
    TableGeometryDefinition table_geometry_definition(std::string_view entity) const;
    // Linked writes require exactly one cache cell per entity cell. No geometry is calculated.
    void set_table_geometry(std::string_view entity, const TableGeometryDefinition& definition);
    void synchronize_table_geometry(std::string_view entity);
    std::string extension_dictionary_handle(std::string_view owner) const;
    std::int16_t wipeout_frame() const;
    // Creating the variables object follows the R2007+ auxiliary-object boundary.
    void set_wipeout_frame(std::int16_t mode);
    std::string root_dictionary_handle() const;
    std::string dictionary_entry(std::string_view dictionary, std::string_view name) const;
    std::uint64_t add_auxiliary_object(std::string_view dictionary, std::string name, const AuxiliaryDefinition& definition);
    void set_auxiliary_definition(std::string_view handle, const AuxiliaryDefinition& definition);
    // rust CadDocument named groups: ACAD_GROUP dictionary, GROUP objects, entity reactors.
    std::optional<GroupObject> get_group(std::string_view name) const;
    std::optional<GroupObject> get_group_by_handle(std::uint64_t handle) const;
    std::vector<GroupObject> groups() const;
    std::uint64_t create_group(std::string name, const std::vector<std::uint64_t>& entities);
    std::uint64_t create_unnamed_group(const std::vector<std::uint64_t>& entities);
    void add_entity_to_group(std::string_view name, std::uint64_t entity);
    void remove_entity_from_group(std::string_view name, std::uint64_t entity);
    std::optional<GroupObject> remove_group(std::string_view name);
    // rust CadDocument mesh UVW XRecord ADSK_XREC_SUBDVERTEXTEXCOORDS (43/44/45).
    std::vector<Vector3> mesh_texture_coordinates(std::uint64_t mesh) const;
    void set_mesh_texture_coordinates(std::uint64_t mesh, const std::vector<Vector3>& coordinates);
    // rust CadDocument viewport XRecord ASDK_XREC_ANNOTATION_SCALE_INFO (340).
    std::optional<std::uint64_t> viewport_annotation_scale(std::uint64_t viewport) const;
    void set_viewport_annotation_scale(std::uint64_t viewport, std::uint64_t scale);
    // rust CadDocument layer viewport overrides (ADSK_XREC_LAYER_*_OVR). False on kind/value mismatch.
    bool set_layer_viewport_override(std::uint64_t layer, LayerViewportOverrideKind kind,
                                     std::uint64_t viewport, XRecordOverrideValue value);
    std::vector<LayerViewportOverride> layer_viewport_overrides(std::uint64_t layer,
                                                                LayerViewportOverrideKind kind) const;
    // rust CadDocument::begin_entity_change_recording / take_before_images.
    std::shared_ptr<EntityChangeRecorder> begin_entity_change_recording();
    void end_entity_change_recording();
    void record_all_entities_for_transaction() const;
    // rust CadDocument drawing variables (NOD AcDbVariableDictionary / DICTIONARYVAR).
    std::optional<std::string> drawing_variable(std::string_view name) const;
    bool set_drawing_variable(std::string_view name, std::string_view value);
    Vector2 hatch_origin() const;
    bool set_hatch_origin(Vector2 origin);
    Transparency current_entity_transparency() const;
    bool set_current_entity_transparency(Transparency value);
    // rust CadDocument::rename_layer: named wrapper over rename_symbol(Layer).
    std::uint64_t rename_layer(std::string_view old_name, std::string new_name);
    // rust CadDocument document-level MIRROR / MIRROR3D. False for a missing
    // handle, unsupported geometry or invalid line/plane; membership persists.
    bool mirror_entity(std::uint64_t handle, Vector3 first, Vector3 second);
    bool mirror_entity_with_options(std::uint64_t handle, Vector3 first, Vector3 second,
                                    MirrorOptions options);
    bool mirror3d_entity(std::uint64_t handle, MirrorPlane plane);
    bool mirror3d_entity_with_options(std::uint64_t handle, MirrorPlane plane, MirrorOptions options);
    // rust CadDocument::try_reserve_handles. Zero is a no-op. Errors leave state unchanged.
    HandleReservation try_reserve_handles(std::size_t count);
    // rust CadDocument::snapshot_structure / swap_structure.
    DxfDocument snapshot_structure();
    DxfDocument swap_structure(DxfDocument snapshot);
    // rust CadDocument::export_block_graph / import_block_graph.
    std::optional<BlockGraph> export_block_graph(std::uint64_t record) const;
    void import_block_graph(const BlockGraph &graph, std::uint64_t record,
                            const std::unordered_map<std::uint64_t, std::uint64_t> &map);
    std::vector<ClassDefinition> classes() const;
    void set_class(const ClassDefinition& definition);
    std::uint64_t add_data_object(std::string_view dictionary, std::string name, const DataObjectDefinition& value);
    std::uint64_t add_object_context(std::string_view dictionary, std::string name, const ObjectContextDefinition& value);
    std::uint64_t add_annotation_context(std::string_view entity, std::string name, const ObjectContextDefinition& value);
    std::uint64_t add_registered_entity(std::string_view class_name, const std::vector<SemanticProperty>& properties, const EntityOptions& options = {});
    std::uint64_t add_registered_object(std::string_view dictionary, std::string name, std::string_view class_name, const std::vector<SemanticProperty>& properties);
    std::uint64_t add_drawing_object(std::string_view dictionary, std::string name, const DrawingObjectDefinition& value);
    std::uint64_t add_class_object(std::string_view dictionary, std::string name, const ClassObjectData& value);
    // Map 3D Object Data tables under NOD `IrdDscDictionary`, matching acadrust
    // CadDocument::{object_data_tables,add_object_data_table,attach_object_data,object_data}.
    // Construction requires R2004+. Duplicate table names and value/type mismatches refuse.
    std::vector<std::pair<std::uint64_t, ObjectDataTable>> object_data_tables() const;
    std::uint64_t add_object_data_table(const ObjectDataTable& table);
    std::uint64_t attach_object_data(std::uint64_t entity, std::uint64_t table,
                                     const std::vector<ObjectDataValue>& values);
    std::vector<ObjectDataAttachment> object_data(std::uint64_t entity) const;
    std::uint64_t add_extended_entity(const ExtendedEntityDefinition& value, const EntityOptions& options = {});
    std::uint64_t add_proxy_entity(const ProxyDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_proxy_object(std::string_view dictionary, std::string name, const ProxyDefinition& definition);
    std::uint64_t add_field(std::string_view dictionary, std::string name, const FieldDefinition& definition);
    std::uint64_t add_field_child(std::string_view parent, const FieldDefinition& definition);
    void set_field_definition(std::string_view handle, const FieldDefinition& definition);
    std::uint64_t add_field_list(std::string_view dictionary, std::string name, const FieldListDefinition& definition);
    std::uint64_t add_material(const MaterialDefinition& definition);
    MaterialDefinition material_definition(std::string_view handle) const;
    void set_material_definition(std::string_view handle, const MaterialDefinition& definition);
    std::uint64_t add_shape(const ShapeDefinition& definition, const EntityOptions& options = {});
    // Anonymous STYLE entries are addressed by handle; more than one may have an empty name.
    std::uint64_t add_shape_file_style(const TextStyleDefinition& definition);
    std::optional<TextStyleDefinition> get_text_style_by_handle(std::string_view handle) const;
    void set_text_style_by_handle(std::string_view handle, const TextStyleDefinition& definition);
    void set_entity_display_properties(std::string_view handle, const EntityDisplayProperties& definition);
    std::uint64_t add_image_definition(std::string name, const ImageDefinition& definition);
    std::uint64_t add_raster_image(const RasterImageDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_wipeout(const RasterImageDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_underlay_definition(UnderlayType type, std::string name, const UnderlayFileDefinition& definition);
    std::uint64_t add_underlay(UnderlayType type, const UnderlayDefinition& definition, const EntityOptions& options = {});
    std::uint64_t set_raster_variables(const RasterVariablesDefinition& definition);
    std::uint64_t add_cell_style_map(std::string_view dictionary, std::string name, const CellStyleMapDefinition& definition);
    CellStyleMapDefinition table_cell_styles(std::string_view style) const;
    void set_table_cell_styles(std::string_view style, const CellStyleMapDefinition& definition);
    std::string table_style_template_handle(std::string_view style) const;
    void set_table_style_template(std::string_view style, const TableContentDefinition& definition);
    const DxfMultiLeaderStyle* get_multileader_style(std::string_view name) const;
    std::uint64_t add_multileader_style(std::string name, const MultiLeaderStyleDefinition& definition);
    std::uint64_t add_multileader(const MultiLeaderDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_mline(const MLineDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_dimension(const DimensionDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_hatch(const HatchDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_tolerance(const ToleranceDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_leader(const LeaderDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_mtext(const MTextDefinition& definition, const EntityOptions& options = {});
    std::uint64_t add_circle(Point3 center, double radius, const EntityOptions& options = {});
    // Default Standard style, left/baseline alignment and +Z normal.
    std::uint64_t add_text(std::string text, Point3 insertion, double height,
                           const EntityOptions& options = {});
    std::uint64_t add_arc(Point3 center, double radius, double start_angle, double end_angle,
                          const EntityOptions& options = {});
    std::uint64_t add_ellipse(Point3 center, Point3 major_axis, double ratio,
                              double start_parameter = 0, double end_parameter = 6.283185307179586,
                              Point3 normal = {0, 0, 1}, const EntityOptions& options = {});
    std::uint64_t add_lwpolyline(const std::vector<LwVertex>& vertices, bool closed = false,
                                 const EntityOptions& options = {});
    std::uint64_t add_polyline2d(const std::vector<LwVertex>& vertices, bool closed = false,
                                 double elevation = 0, const EntityOptions& options = {});
    std::uint64_t add_polyline3d(const std::vector<Point3>& vertices, bool closed = false,
                                 const EntityOptions& options = {});
    std::uint64_t add_polygon_mesh(int m, int n, const std::vector<Point3>& vertices,
                                   bool closed_m = false, bool closed_n = false,
                                   const EntityOptions& options = {});
    std::uint64_t add_polyface_mesh(const std::vector<Point3>& vertices,
                                    const std::vector<std::array<int, 4>>& faces,
                                    const EntityOptions& options = {});
    // Deep-copy LINE/POINT/RAY/XLINE/CIRCLE/ARC/ELLIPSE/TEXT/LWPOLYLINE and verified extension dictionaries and owned OBJECTS subgraphs in this document.
    // Also copies parsed ENTITIES/BLOCKS 2D/3D POLYLINE, Polygon Mesh and Polyface Mesh,
    // including all coordinate/face/unclassified VERTEX records and SEQEND.
    // A complete sequence and unique nonzero child handles are required.
    // Remaps internal references; external references and source records remain unchanged.
    // Defaults to model space. Returns a fresh entity handle; rejects invalid/unknown sources,
    // unsupported ownership/opaque internal references and ambiguous spaces without partial edits.
    // Success invalidates existing record pointers/references.
    std::uint64_t copy_entity(std::string_view handle, EntitySpace space = EntitySpace::Model);
    std::uint64_t copy_entity(std::uint64_t handle, EntitySpace space = EntitySpace::Model);
    // Target selection only; source display properties are retained.
    std::uint64_t copy_entity(std::string_view handle, const EntityOptions& target);
    std::uint64_t copy_entity(std::uint64_t handle, const EntityOptions& target);
    // Rust standalone EXPLODE semantics for lightweight/traditional polylines and meshes.
    // LINE/ARC/CIRCLE/ELLIPSE/POINT/RAY/XLINE/SOLID/3DFACE/SPLINE return no parts.
    // Other types and ambiguous/nonentity handles are rejected explicitly.
    std::vector<ExplodedPrimitive> explode_entity(std::string_view handle) const;
    std::vector<ExplodedModelerPart> explode_modeler(std::uint64_t handle) const;
    std::optional<BlockEvaluationGraph> evaluation_graph(std::uint64_t object) const;
    std::optional<SolidHistoryOperation> solid_history_definition(std::uint64_t node) const;
    SolidHistoryGraph create_solid_history(std::uint64_t entity, SolidHistoryOperation operation);
    SolidHistoryGraph append_solid_history(std::uint64_t entity, SolidHistoryOperation operation);
    // Moves tool history into target; existing node handles survive. Codes: 0 union, 1 intersect, 2 subtract.
    // Edits stored history only; neither entity's ACIS geometry is regenerated.
    SolidHistoryGraph merge_solid_history_boolean(std::uint64_t target, std::uint64_t tool,
                                                  std::uint8_t operation);
    SolidHistoryOperation update_solid_history(std::uint64_t entity, SolidHistoryOperation operation);
    SolidHistoryOperation update_solid_history_step(std::uint64_t entity, SolidHistoryOperation operation);
    std::optional<SolidHistoryRoot> solid_history_root(std::uint64_t object) const;
    std::optional<SolidHistoryNode> solid_history_node(std::uint64_t object) const;
    std::optional<SolidHistoryGraph> solid_history_graph(std::uint64_t entity) const;
    std::optional<SolidHistoryNode> solid_history_operation(std::uint64_t entity) const;
    std::optional<std::vector<SolidHistoryNode>> solid_history_operations(std::uint64_t entity) const;
    std::optional<SolidHistoryTree> solid_history_tree(std::uint64_t entity) const;
    std::vector<HistoryReference> solid_history_references(std::uint64_t entity) const;
    // Stored graph edits only; no B-rep regeneration. All failures are atomic.
    void set_solid_history_active_node(std::uint64_t entity, std::int32_t node_id);
    void set_solid_history_node_material(std::uint64_t node, std::uint64_t material);
    void set_solid_history_evaluation_graph(std::uint64_t entity, const BlockEvaluationGraph& graph);
    // Target selects ownership only; source visual properties are retained.
    // Allocate and insert all parts atomically in the requested target; optionally remove
    // the original group using the existing dictionary/reference deletion rules.
    std::vector<std::uint64_t> explode_entity_into(std::string_view handle,
        const EntityOptions& target = {}, bool remove_source = false);
    // Recursively bake supported INSERT members into the selected target space.
    // Preserves exact affine nesting; nonuniform circles/arcs become ellipses.
    // Arrays require explicit opt-in; source INSERT and definitions remain by default.
    // Also supports typed annotation, raster, table and extended-entity transforms.
    // Specialized shear leaves with owned objects refuse until detached graphs are supported.
    // Cycles, limits, ineligible nested blocks and unsupported members refuse atomically.
    std::vector<std::uint64_t> expand_insert_into(std::string_view handle,
        const EntityOptions& target = {}, bool remove_source = false,
        bool expand_arrays = false, std::size_t max_entities = 1000000);
    // Remove an independent top-level LINE/POINT/RAY/XLINE/CIRCLE/ARC/ELLIPSE/TEXT/LWPOLYLINE in ENTITIES or BLOCKS.
    // Also removes parsed ENTITIES/BLOCKS 2D/3D POLYLINE, Polygon Mesh and Polyface Mesh
    // with all their coordinate, face and other VERTEX records and SEQEND.
    // Invalid/null/unknown handles return false.
    // Removes references to deleted members from well-formed top-level ACAD_REACTORS groups.
    // Cascades through verified extension dictionaries and the complete OBJECTS owner graph.
    // Ambiguous identities, foreign ownership pointers, other external references or inconsistent ownership
    // throw; no partial deletion occurs. Children cannot be removed individually.
    // Success invalidates record pointers, preserves survivor order and never reuses the handle.
    bool remove_entity(std::string_view handle);
    bool remove_entity(std::uint64_t handle);
    // Last record with this identity, matching Rust's entity index insertion.
    // Null, invalid and unknown handles return nullptr.
    const DxfRecord* get_record(std::string_view handle) const;
    std::vector<const DxfRecord*> records_with_handle(std::string_view handle) const;
    // Numeric handles occurring more than once, sorted ascending.
    std::vector<std::uint64_t> duplicate_handles() const;
    // Follows explicit owner links without changing the drawing. O(chain length).
    // Returned record pointers belong to this document.
    OwnershipChain ownership_chain(std::string_view handle) const;
    // R2007+ specialized surface inputs, including the stored loft metadata fallback.
    // Edits are atomic, synchronize existing fallback data, and preserve the ACIS body.
    // Missing/ambiguous handles, unsupported kinds and unsafe metadata edits throw.
    SurfaceProfiles surface_profiles(std::uint64_t handle) const;
    void set_surface_profiles(std::uint64_t handle, const SurfaceProfiles& profiles);
    // Native modeler geometry: inline SAT before R2013; ACDSDATA SAB from R2013.
    // Unknown handles return empty; ambiguous associations and malformed payloads throw.
    std::optional<acis::SatDocument> modeler_geometry(std::uint64_t handle) const;
    // Atomic same-version update; existing SAB records/tags are retained.
    void set_modeler_geometry(std::uint64_t handle, const acis::SatDocument& geometry);
    void transform_modeler(std::uint64_t handle, const Transform& transform);
    // New SAB payloads use upstream strip_for_sab; edits do not strip existing payloads.
    std::uint64_t add_modeler_entity(ModelerKind kind, const acis::SatDocument& geometry,
                                    const EntityOptions& options = {});
    // Read-only DXF dynamic definition and saved INSERT state. No action evaluation.
    // Missing definitions return empty; malformed/unsupported property graphs throw.
    bool is_dynamic_block(std::uint64_t definition) const;
    std::optional<std::uint64_t> dynamic_block_definition(std::uint64_t block) const;
    std::optional<std::uint64_t> dynamic_definition_for_insert(std::uint64_t insert) const;
    std::optional<BlockEvaluationGraph> dynamic_block_graph(std::uint64_t definition) const;
    std::optional<BlockVisibilityParameter> block_visibility_parameter(std::uint64_t definition) const;
    std::optional<std::pair<std::uint64_t, BlockVisibilityParameter>>
        dynamic_visibility_for_insert(std::uint64_t insert) const;
    std::vector<DynamicBlockProperty> dynamic_block_properties(std::uint64_t definition,
        std::optional<std::uint64_t> insert = std::nullopt) const;
    // Typed DXF dynamic-block objects. Missing/non-dynamic handles return empty.
    // Unknown families refuse rewrite. Entity records use the entity APIs below.
    std::optional<DynamicBlockObject> dynamic_object(std::uint64_t handle) const;
    void update_dynamic_object(const DynamicBlockObject& object);
    std::vector<std::uint64_t> dynamic_object_references(std::uint64_t handle) const;
    std::vector<std::uint64_t> dynamic_objects_referencing(std::uint64_t target) const;
    void remap_dynamic_handles(const std::unordered_map<std::uint64_t, std::uint64_t>& map);
    std::optional<DynamicBlockObject> dynamic_entity(std::uint64_t handle) const;
    void update_dynamic_entity(const DynamicBlockObject& entity);
    std::uint64_t insert_dynamic_object(DynamicBlockObject object);
    std::uint64_t insert_dynamic_entity(DynamicBlockObject entity, const EntityOptions& options = {});
    // Arch DXF records without a class body and without proxy graphics are omitted.
    // Graphics-only Arch entities are written as ACAD_PROXY_ENTITY. R12 refuses Arch.
    std::vector<ArchitectureOmission> omitted_architecture_records() const;
    // Typed DXF associative objects. Missing/non-associative handles return empty.
    // Unknown families refuse rewrite. Graph walks detect cyclic owned networks.
    std::optional<AssociativeObject> associative_object(std::uint64_t handle) const;
    void update_associative_object(const AssociativeObject& object);
    std::vector<std::uint64_t> associative_references(std::uint64_t handle) const;
    std::vector<std::uint64_t> associative_objects_referencing(std::uint64_t target) const;
    void remap_associative_handles(const std::unordered_map<std::uint64_t, std::uint64_t>& map);
    std::vector<std::uint64_t> associative_owned_network(std::uint64_t network) const;
    // Resolve an explicit owner. Null, invalid or unknown handles return nullptr.
    const DxfBlockRecord* block_record(std::string_view owner_handle) const;
    // Top-level identities in file order, including preserved unknown entities.
    // Requires explicit ownership by a BLOCK_RECORD. Child records are excluded.
    std::vector<std::string> entity_handles_in_block(std::string_view name) const;
    std::vector<std::string> model_space_entity_handles() const;
    std::vector<std::string> paper_space_entity_handles() const;

    // rust CadDocument::summary_info. Values come from HEADER $TITLE / $AUTHOR / …
    // when present; otherwise empty defaults.
    SummaryInfo summary_info() const;
    // rust CadDocument::semantic_inventory_v1. Visits header, table records,
    // classes, entities, objects, summary, relationships and object XDATA.
    // Omits caches, source path, handle allocation and raw file bytes.
    SemanticInventoryV1 semantic_inventory_v1() const;

    // Visits typed records in file order, including entities inside named BLOCKS.
    // DxfEntity excludes structural BLOCK markers; query DxfBlock explicitly.
    template<class T, class Fn> void for_each(Fn&& fn) {
        for (auto& record : records_) visit_record<T>(record, fn);
    }
    template<class T, class Fn> void for_each(Fn&& fn) const {
        for (const auto& record : records_) visit_record<T>(record, fn);
    }
private:
    friend class DynamicBlockReader;
    friend class DynamicBlockObjectReader;
    friend class AssociativeReader;
    friend class SolidHistoryReader;
    void validate_history_mutation(std::uint64_t entity) const;
    SolidHistoryGraph ensure_history_evaluation_graph(std::uint64_t entity);
    void write_history_operation(std::uint64_t node, const SolidHistoryOperation& operation);
    void write_history_metadata(std::uint64_t owner, std::string_view key, const std::vector<DxfRawField>* fields);
    void erase_history_records(const std::unordered_set<std::uint64_t>& handles);
    void ensure_history_class(std::string_view name, std::string_view cpp);
    void erase_dictionary_entry(DxfRecord& dictionary, std::string_view key);
    void erase_xdictionary_group(DxfRecord& host);
    bool dictionary_has_entries(const DxfRecord& dictionary) const;
    void erase_object_handles(const std::unordered_set<std::uint64_t>& handles);
    std::string ensure_group_dictionary();
    void attach_group_reactor(std::uint64_t entity, std::uint64_t group);
    void detach_group_reactor(std::uint64_t entity, std::uint64_t group);
    std::vector<std::pair<std::string, std::uint64_t>> group_dictionary_entries() const;
    std::optional<GroupObject> group_object_at(std::uint64_t handle, std::string name) const;
    std::uint64_t ensure_named_xrecord(std::uint64_t owner, std::string_view key);
    const DxfRecord* named_xrecord(std::uint64_t owner, std::string_view key) const;
    void record_entity_before(std::uint64_t handle, std::optional<std::string> before) const;
    std::optional<std::string> variable_dictionary_handle() const;
    std::optional<std::string> ensure_variable_dictionary();

    template<class T, class Variant, class Fn>
    static void visit_record(Variant& record, Fn& fn) {
        if constexpr (std::is_same_v<T, DxfEntity>) {
            using Entity = std::conditional_t<std::is_const_v<Variant>, const DxfEntity, DxfEntity>;
            std::visit([&](auto& item) {
                if constexpr (std::is_base_of_v<DxfEntity, std::decay_t<decltype(item)>> &&
                              !std::is_same_v<DxfBlock, std::decay_t<decltype(item)>>)
                    fn(static_cast<Entity&>(item));
            }, record);
        } else if (auto* item = std::get_if<T>(&record)) {
            fn(*item);
        }
    }
    bool binary_ = false;
    friend class R12Exporter;
    friend class DxfVersionConverter;
    bool compact_codes_ = false;
    friend class DxfMText;
    friend class DxfInsert;
    friend class DxfText;
    friend class DxfAttribute;
    friend class DxfViewport;
    friend class DxfLeader;
    friend class DxfHatch;
    friend class DxfDimension;
    friend class DxfMLine;
    friend class DxfMultiLeader;
    friend class DxfTable;
    friend class DxfMultiLeaderStyle;
    friend class DxfMLineStyle;
    friend class DxfVersionConverter;
    DxfVersion version_ = DxfVersion::AC1032;
    std::int16_t insertion_units_ = 0;
    std::uint64_t next_handle_ = 0x10;
    bool handle_reserved_ = false;
    std::unordered_set<std::string> retired_registered_classes_;
    std::optional<std::size_t> header_index_;
    std::int16_t header_int16(std::string_view name, std::int16_t fallback) const;
    bool header_bool290(std::string_view name, bool fallback) const;
    Point3 header_point(std::string_view name, Point3 fallback) const;
    void set_header_point(std::string_view name, Point3 value);
    using Record = std::variant<DxfRecord, DxfModelerEntity, DxfDataObject, DxfObjectContext, DxfSemanticEntity, DxfSemanticObject, DxfDrawingObject, DxfClassObject, DxfExtendedEntity, DxfAuxiliaryObject, DxfProxyEntity, DxfProxyObject, DxfField, DxfFieldList, DxfMaterial, DxfShape, DxfRasterImage, DxfUnderlay, DxfImageDefinition, DxfUnderlayDefinition, DxfRasterVariables, DxfImageReactor, DxfCellStyleMap, DxfTableGeometry, DxfTableContent, DxfTable, DxfTableStyle, DxfMultiLeaderStyle, DxfMultiLeader, DxfMLineStyle, DxfMLine, DxfDimension, DxfHatch, DxfTolerance, DxfLeader, DxfLayout, DxfViewport, DxfBlockRecord, DxfLayer, DxfLineType, DxfLine, DxfPoint, DxfRay, DxfXLine, DxfSolid, DxfFace3D, DxfMesh, DxfSpline, DxfHelix, DxfMText, DxfBlock, DxfInsert, DxfCircle, DxfArc, DxfEllipse, DxfPolyline2D, DxfPolyline3D, DxfPolygonMesh, DxfPolyfaceMesh, DxfLwPolyline, DxfText, DxfAttribute>;
    bool mirror_entity_with_transform(std::uint64_t handle, const Transform& transform, MirrorOptions options);
    std::vector<Record> extract_graphical_entities();
    void insert_graphical_entities(std::vector<Record> entities);
    bool owner_chain_reaches(std::uint64_t handle, std::uint64_t target) const;
    void set_xdictionary_handle(DxfRecord& host, std::uint64_t dictionary);
    std::uint64_t insert_solid(const std::vector<Point3>& corners, bool trace, const EntityOptions& options);
    DxfRecord make_record(std::string type, std::string subclass = {}) const;
    DxfEntity make_entity(std::string type, std::string subclass, const EntityOptions& options) const;
    void initialize_sequence(DxfVertexSequence& entity, std::string_view subclass, int flags,
                             const std::vector<Point3>& vertices, std::string_view vertex_subclass,
                             int vertex_flags, const EntityOptions& options) const;
    std::uint64_t insert_entity(Record entry, EntitySpace space);
    std::uint64_t insert_entity(Record entry, const EntityOptions& options);
    std::optional<std::size_t> modeler_payload_record(std::uint64_t handle) const;
    std::uint64_t add_symbol_impl(SymbolTable table, std::string name, const std::vector<SymbolProperty>& properties, bool anonymous_table_block);
    std::uint64_t insert_table_entry(std::string_view table, Record entry);
    std::vector<std::size_t> symbol_locations(SymbolTable table, std::string_view name) const;
    std::vector<std::size_t> symbol_references(const DxfRecord& record, SymbolTable table) const;
    void apply_symbol_properties(DxfRecord& record, SymbolTable table,
                                 const std::vector<SymbolProperty>& properties) const;
    std::vector<std::pair<std::size_t, bool>> reference_positions(const DxfRecord& record) const;
    std::pair<std::string,std::string> table_roundtrip_links(std::string_view entity) const;
    void update_table_geometry(std::string_view entity, const TableContentDefinition& definition);
    void project_legacy_table_cells(TableDefinition& table, const TableContentDefinition& content) const;
    TableContentDefinition resolved_table_content(std::string_view entity, bool synchronize) const;
    void validate_table_content(TableContentDefinition& definition) const;
    void link_image_reactors(std::uint64_t definition);
    std::uint64_t insert_object(DxfRecord record);
    std::string named_object_dictionary(std::string_view name) const;
    std::uint64_t add_dictionary_object(std::string_view parent, std::string name, DxfRecord object);
    std::uint64_t add_dictionary_object_impl(std::string_view parent, std::string name, DxfRecord object);
    std::uint64_t insert_owned_class_object(std::uint64_t owner, const ClassObjectData& value);
    std::uint64_t ensure_object_data_dictionary();
    std::uint64_t add_object_data_descriptor(std::uint64_t dictionary, const ObjectDataDescriptor& descriptor);
    std::optional<ObjectDataTable> object_data_table(std::uint64_t descriptor) const;
    const DxfClassObject* class_object_record(std::uint64_t handle) const;
    DxfClassObject* class_object_record(std::uint64_t handle);
    std::vector<std::pair<std::string, std::uint64_t>> named_dictionary_entries(std::string_view dictionary) const;
    std::string layer_table_handle() const;
    std::string layer_states_dictionary_handle() const;
    std::uint64_t ensure_layer_states_dictionary();
    LayerState decode_layer_state(std::string_view name, const DxfRecord& xrecord) const;
    std::optional<LayerStateLayer> decode_layer_state_layer(const DxfRecord& xrecord,
        const std::vector<std::size_t>& payload, std::size_t begin, std::size_t end) const;
    std::vector<SymbolProperty> encode_layer_state(const LayerState& state) const;
    void set_xrecord_layer_state(DxfRecord& record, const LayerState& state);
    std::string header_current_layer() const;
    void set_header_current_layer(std::string name);
    DxfRecord& mutable_object(std::uint64_t handle);
    const DxfRecord* object_record(std::uint64_t handle) const;
    void rebuild_indexes();
    bool architecture_has_class_body(const DxfRecord &record) const;
    bool architecture_has_graphics(const DxfRecord &record) const;
    std::optional<DxfRecord> architecture_written_record(const DxfRecord &record,
                                                         std::string_view section) const;
    DxfRecord architecture_omit_adjust(const DxfRecord &record) const;
    std::vector<const DxfRecord*> extension_tree(std::vector<const DxfRecord*> roots) const;
    struct RecordLocation {
        std::size_t record;
        std::optional<std::size_t> vertex;
        bool seqend = false;
    };
    const DxfRecord& record_at(const RecordLocation& location) const;
    std::unordered_map<std::uint64_t, std::vector<RecordLocation>> handle_index_;
    std::vector<Record> records_;
    std::vector<std::size_t> entity_records_;
    std::vector<std::string> entity_handles_owned_by(const std::vector<std::uint64_t>& owners) const;
    mutable std::shared_ptr<EntityChangeRecorder> entity_change_recorder_;
};

struct RecoverOutcome {
    DxfDocument document;
    DxfReadStatistics stats;
    RepairReport repair;
};

} // namespace libopencad
