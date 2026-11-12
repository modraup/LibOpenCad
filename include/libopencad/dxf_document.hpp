// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "color.hpp"
#include "dxf_version.hpp"
#include "line_weight.hpp"
#include "vector.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <unordered_map>
#include <vector>

namespace libopencad {

class DxfRecord {
public:
    std::string type() const;
    // Record identity (group 5, or 105 for DIMSTYLE).
    std::string handle() const;
    std::string owner_handle() const;
protected:
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
    struct Pair { int code; std::string value; };
    struct Position { std::size_t index; bool found; };
    template<class Predicate> Position locate_if(std::string_view subclass, Predicate match) const;
    double number_at(std::size_t index) const;
    std::string decode_string(std::string_view bytes) const;
    std::string encode_string(std::string_view value) const;
    std::string encode_number(double value) const;
    Position locate(std::string_view subclass, int code) const;
    bool binary_ = false;
    const char* code_page_ = nullptr; // Static codec name, raw pairs retain source bytes.
    std::vector<Pair> pairs_;
    friend class DxfDocument;
    friend class DxfEntity;
    friend class DxfLineType;
    friend class DxfLwPolyline;
    friend class DxfPolyline2D;
    friend class DxfVertexSequence;
    friend class DxfPolyfaceMesh;
};

class DxfBlockRecord : public DxfRecord {
public:
    std::string name() const;
    bool is_model_space() const;
    bool is_paper_space() const;
};

class DxfLayer : public DxfRecord {
public:
    std::string name() const;
    int signed_color_index() const; // Negative means the layer is switched off.
    void set_signed_color_index(int index);
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

class DxfEntity : public DxfRecord {
public:
    std::string layer() const;
    void set_layer(std::string name);
    Color color() const;
    void set_color(Color value);
    // An absent group 6 is returned as an empty name (ByLayer).
    std::string linetype() const;
    void set_linetype(std::string_view name);
    LineWeight line_weight() const;
    void set_line_weight(LineWeight value);
    bool is_invisible() const;
    void set_invisible(bool invisible);
private:
    std::int16_t common_short(int code, std::int16_t fallback) const;
    void replace_common_fields(std::initializer_list<int> codes, std::vector<Pair> replacement);
    DxfVersion version_ = DxfVersion::AC1032;
    friend class DxfDocument;
};

class DxfLine : public DxfEntity {
public:
    Point3 start() const;
    Point3 end() const;
    void set_start(Point3 value);
    void set_end(Point3 value);
};

class DxfCircle : public DxfEntity {
public:
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
    double start_angle_radians() const;
    double end_angle_radians() const;
    void set_start_angle_radians(double value);
    void set_end_angle_radians(double value);
};

class DxfEllipse : public DxfEntity {
public:
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
};

struct PolylineVertex3D {
    Point3 position; // XYZ in world coordinates.
    int flags;
    std::string layer, handle, owner_handle;
};

// Traditional POLYLINE owns its VERTEX records and terminator, in file order.
class DxfVertexSequence : public DxfEntity {
public:
    const DxfRecord* seqend() const noexcept { return seqend_ ? &*seqend_ : nullptr; }
protected:
    PolylineVertex3D spatial_vertex(std::size_t index, std::string_view subclass) const;
    void set_spatial_vertex(std::size_t index, std::string_view subclass, Point3 position);
    std::vector<DxfRecord> vertices_;
private:
    std::optional<DxfRecord> seqend_;
    friend class DxfDocument;
};

class DxfPolylineSequence : public DxfVertexSequence {
public:
    std::size_t vertex_count() const noexcept { return vertices_.size(); }
};

class DxfPolyline2D : public DxfPolylineSequence {
public:
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
    bool is_closed() const;
    PolylineVertex3D vertex(std::size_t index) const;
    void set_vertex_position(std::size_t index, Point3 position);
};

class DxfPolygonMesh : public DxfPolylineSequence {
public:
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
    // The fourth slot can also repeat the third vertex on a triangle.
    bool is_triangle() const noexcept;
};

class DxfPolyfaceMesh : public DxfVertexSequence {
public:
    std::size_t vertex_count() const noexcept { return coordinate_records_.size(); }
    std::size_t face_count() const noexcept { return face_records_.size(); }
    PolylineVertex3D vertex(std::size_t index) const;
    PolyfaceFace face(std::size_t index) const;
    void set_face_indices(std::size_t index, std::array<int, 4> indices);
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
    std::vector<LwVertex> vertices() const;
    void set_vertex_position(std::size_t index, double x, double y);
    void set_bulge(std::size_t index, double value);
    bool is_closed() const;
    void set_closed(bool closed);
    bool plinegen() const;
    double elevation() const;
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

class DxfText : public DxfEntity {
public:
    Point3 insertion_point() const;
    double height() const;
    std::string text() const;
    void set_insertion_point(Point3 value);
    void set_height(double value);
    void set_text(std::string value);
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

struct EntityOptions {
    EntitySpace space = EntitySpace::Model;
    std::string layer = "0";
    std::string linetype; // Empty means ByLayer.
    Color color;
    LineWeight line_weight;
    bool invisible = false;
};

// Common symbol-table operations. Property values use UTF-8 strings or canonical
// ASCII numbers/hex handles, independent of the document's transport encoding.
enum class SymbolTable { Layer, LineType, TextStyle, DimStyle, AppId, Ucs, View, VPort, BlockRecord };
struct SymbolProperty { int code; std::string value; };
struct SymbolEntry {
    std::string name, handle, owner_handle;
    std::vector<SymbolProperty> properties;
};

enum class ReferenceStatus { Invalid, Dangling, Ambiguous, OwnerCycle, MissingOwner, InvalidOwner };
struct ReferenceIssue {
    std::string source_handle;
    int code;
    std::string target;
    ReferenceStatus status;
    std::optional<SymbolTable> table; // Present for a name-based symbol reference.
};
struct HandleReplacement { std::string from, to; };

class DxfDocument {
public:
    // AC1032 defaults matching acadrust CadDocument::new, with selectable DXF encoding.
    static DxfDocument create(bool binary = false, std::int16_t insertion_units = 0);
    static DxfDocument read(std::istream& input);
    // Detects the input encoding and preserves it on write.
    bool is_binary() const noexcept { return binary_; }
    DxfVersion version() const noexcept { return version_; }
    // Raw $INSUNITS code, matching acadrust HeaderVariables::insertion_units.
    std::int16_t insertion_units() const noexcept { return insertion_units_; }
    // First candidate after reconciling $HANDSEED with stored record identities.
    std::uint64_t next_handle() const noexcept { return next_handle_; }
    // Reserve one handle and advance $HANDSEED. Throws on 64-bit exhaustion.
    std::uint64_t allocate_handle();
    void write(std::ostream& output) const;
    // Select transport encoding without changing the source document or DXF version/code page.
    void write(std::ostream& output, bool binary) const;
    // Rust symbol-table Unicode name matching; last matching table entry wins.
    // Queries use UTF-8. Unknown names return nullptr.
    const DxfLayer* get_layer(std::string_view name) const;
    const DxfLineType* get_linetype(std::string_view name) const;
    // Success invalidates existing record pointers/references. Failure leaves the document intact.
    std::uint64_t add_layer(std::string name, int signed_color_index = 7,
                            std::string linetype = "Continuous");
    std::uint64_t add_linetype(const LineTypeDefinition& definition);
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
    // Scan all stored records, including compound children, raw entities and OBJECTS.
    std::vector<ReferenceIssue> reference_issues() const;
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
    // Existing layers/linetypes and an unambiguous space BLOCK_RECORD are required.
    // Return the allocated identity. Same invalidation/rollback contract as add_layer.
    std::uint64_t add_line(Point3 start, Point3 end, const EntityOptions& options = {});
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
    // Deep-copy LINE/CIRCLE/ARC/ELLIPSE/TEXT/LWPOLYLINE and verified extension dictionaries and owned OBJECTS subgraphs in this document.
    // Also copies parsed ENTITIES/BLOCKS 2D/3D POLYLINE, Polygon Mesh and Polyface Mesh,
    // including all coordinate/face/unclassified VERTEX records and SEQEND.
    // A complete sequence and unique nonzero child handles are required.
    // Remaps internal references; external references and source records remain unchanged.
    // Defaults to model space. Returns a fresh entity handle; rejects invalid/unknown sources,
    // unsupported ownership/opaque internal references and ambiguous spaces without partial edits.
    // Success invalidates existing record pointers/references.
    std::uint64_t copy_entity(std::string_view handle, EntitySpace space = EntitySpace::Model);
    std::uint64_t copy_entity(std::uint64_t handle, EntitySpace space = EntitySpace::Model);
    // Remove an independent top-level LINE/CIRCLE/ARC/ELLIPSE/TEXT/LWPOLYLINE in ENTITIES or BLOCKS.
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
    // Resolve an explicit owner. Null, invalid or unknown handles return nullptr.
    const DxfBlockRecord* block_record(std::string_view owner_handle) const;
    // Top-level identities in file order, including preserved unknown entities.
    // Requires explicit ownership by a BLOCK_RECORD. Child records are excluded.
    std::vector<std::string> entity_handles_in_block(std::string_view name) const;
    std::vector<std::string> model_space_entity_handles() const;
    std::vector<std::string> paper_space_entity_handles() const;

    // Visits typed records in file order, including traditional sequences in BLOCKS.
    // Other BLOCKS entities remain raw; get_record and copy/remove still support them.
    template<class T, class Fn> void for_each(Fn&& fn) {
        for (auto& record : records_) visit_record<T>(record, fn);
    }
    template<class T, class Fn> void for_each(Fn&& fn) const {
        for (const auto& record : records_) visit_record<T>(record, fn);
    }
private:
    template<class T, class Variant, class Fn>
    static void visit_record(Variant& record, Fn& fn) {
        if constexpr (std::is_same_v<T, DxfEntity>) {
            using Entity = std::conditional_t<std::is_const_v<Variant>, const DxfEntity, DxfEntity>;
            std::visit([&](auto& item) {
                if constexpr (std::is_base_of_v<DxfEntity, std::decay_t<decltype(item)>>)
                    fn(static_cast<Entity&>(item));
            }, record);
        } else if (auto* item = std::get_if<T>(&record)) {
            fn(*item);
        }
    }
    bool binary_ = false;
    bool compact_codes_ = false;
    DxfVersion version_ = DxfVersion::AC1032;
    std::int16_t insertion_units_ = 0;
    std::uint64_t next_handle_ = 0x10;
    bool handle_reserved_ = false;
    std::optional<std::size_t> header_index_;
    using Record = std::variant<DxfRecord, DxfBlockRecord, DxfLayer, DxfLineType, DxfLine, DxfCircle, DxfArc, DxfEllipse, DxfPolyline2D, DxfPolyline3D, DxfPolygonMesh, DxfPolyfaceMesh, DxfLwPolyline, DxfText>;
    DxfRecord make_record(std::string type, std::string subclass = {}) const;
    DxfEntity make_entity(std::string type, std::string subclass, const EntityOptions& options) const;
    void initialize_sequence(DxfVertexSequence& entity, std::string_view subclass, int flags,
                             const std::vector<Point3>& vertices, std::string_view vertex_subclass,
                             int vertex_flags, const EntityOptions& options) const;
    std::uint64_t insert_entity(Record entry, EntitySpace space);
    std::uint64_t insert_table_entry(std::string_view table, Record entry);
    std::vector<std::size_t> symbol_locations(SymbolTable table, std::string_view name) const;
    std::vector<std::size_t> symbol_references(const DxfRecord& record, SymbolTable table) const;
    void apply_symbol_properties(DxfRecord& record, SymbolTable table,
                                 const std::vector<SymbolProperty>& properties) const;
    std::vector<std::pair<std::size_t, bool>> reference_positions(const DxfRecord& record) const;
    std::uint64_t insert_object(DxfRecord record);
    std::uint64_t add_dictionary_object(std::string_view parent, std::string name, DxfRecord object);
    void rebuild_indexes();
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
};

} // namespace libopencad
