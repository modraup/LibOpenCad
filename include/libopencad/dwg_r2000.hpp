// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <libopencad/arch.hpp>
#include <libopencad/associative.hpp>
#include <libopencad/class_object.hpp>
#include <libopencad/color.hpp>
#include <libopencad/dimension.hpp>
#include <array>
#include <libopencad/dynamic_block.hpp>
#include <libopencad/embedded_profile.hpp>
#include <libopencad/surface.hpp>
#include <libopencad/dwg_history.hpp>
#include <libopencad/vector.hpp>
#include <libopencad/lwvertex.hpp>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace libopencad {
struct DwgHandleReference {
    unsigned code;
    std::uint64_t value;
};
// The caller owns the storage backing the view.
class DwgBitReader {
  public:
    explicit DwgBitReader(std::string_view bytes);
    DwgBitReader(std::string_view bytes, std::size_t limit_bits);
    std::size_t position() const { return pos_; }
    std::size_t remaining() const { return limit_ - pos_; }
    void seek(std::size_t bit);
    std::uint64_t bits(unsigned width);
    std::uint64_t little_endian(unsigned bytes);
    std::int16_t bit_short();
    std::int32_t bit_long();
    std::int64_t bit_long_long();
    double bit_double();
    double raw_double();
    double default_double(double base);
    std::uint32_t modular_short();
    DwgHandleReference handle(std::uint64_t base = 0);

  private:
    std::string_view data_;
    std::size_t limit_;
    std::size_t pos_ = 0;
};
class DwgBitWriter {
  public:
    void bits(unsigned width, std::uint64_t value);
    void little_endian(unsigned bytes, std::uint64_t value);
    void bit_short(std::int16_t value);
    void bit_long(std::int32_t value);
    void bit_long_long(std::int64_t value);
    void bit_double(double value);
    void modular_short(std::uint32_t value);
    void absolute_handle(unsigned code, std::uint64_t handle);
    void relative_handle(std::uint64_t base, std::uint64_t handle);
    std::size_t position() const { return pos_; }
    const std::string &bytes() const { return data_; }

  private:
    void handle_payload(unsigned code, std::uint64_t value);
    std::string data_;
    std::size_t pos_ = 0;
};
struct DwgSectionLocation {
    std::uint32_t offset, size;
};
struct DwgR2000Index {
    std::map<unsigned, DwgSectionLocation> sections;
    std::map<std::uint64_t, std::size_t> object_offsets;
};
DwgR2000Index read_dwg_r2000_index(std::string_view file);
struct DwgR2000Record {
    std::size_t offset, body_offset, body_size;
    std::int16_t type;
    std::size_t type_end_bit, handle_end_bit, main_bits;
    std::size_t geometry_bit{}, split_field_bit{};
    std::size_t text_start{}, text_end{}, position_end{}, text_style_bit{};
    std::size_t ds_data_bit{};
    std::size_t layer_flags_bit{}, color_bit{}, color_handle_bit{}, visibility_bit{}, linetype_scale_bit{};
    std::size_t geometry_start{}, geometry_end{}, layer_bit{}, elevation_bit{}, thickness_bit{}, linetype_bit{}, linetype_flag_bit{};
};
struct DwgR2000EntityProperties {
    unsigned mode{}; // 0 = explicit owner, 1 = paper space, 2 = model space
    std::uint64_t owner{}, layer{}, extension_dictionary{};
    std::vector<std::uint64_t> reactors;
    std::uint64_t previous{}, next{}, linetype{}, plotstyle{};
    unsigned linetype_flags{}, plotstyle_flags{}, lineweight_index{};
    unsigned material_flags{}, shadow_flags{};
    std::uint64_t material{}, color_book{}, visual_styles[3]{};
    std::uint32_t transparency{};
    Color color;
    double linetype_scale{1.0};
    std::uint16_t legacy_layer_index{}, legacy_linetype_index{32767}; // AC1009 ByLayer when the optional slot is absent
    std::string legacy_block_name;
    bool invisible{}, has_ds_data{};
};
struct DwgR2000Line {
    DwgR2000EntityProperties properties;
    Point3 start, end;
    double thickness{};
    Vector3 normal;
};
struct DwgLwPolyline {
    DwgR2000EntityProperties properties;
    std::vector<LwVertex> vertices;
    Vector3 normal{0, 0, 1};
    double elevation{}, thickness{}, constant_width{};
    bool closed{}, plinegen{};
};
struct DwgCircle {
    DwgR2000EntityProperties properties;
    Point3 center;
    double radius{}, thickness{};
    Vector3 normal;
};
struct DwgArc : DwgCircle {
    double start_angle{}, end_angle{}; // radians
};
// Saved glyph layout of a native MTEXT. Background, columns and annotation
// contexts are retained in the source record for same-version writes.
struct DwgMText {
    DwgR2000EntityProperties properties;
    std::string value;
    Point3 insertion;
    Vector3 normal, x_direction;
    double height{}, width{}, extents_height{}, extents_width{}, line_spacing_factor{1};
    std::optional<double> rectangle_height;
    std::int16_t attachment{1}, drawing_direction{1}, line_spacing_style{1};
    std::uint64_t style{};
};
struct DwgDimension {
    DwgR2000EntityProperties properties;
    DimensionDefinition definition;
    std::uint64_t graphics_block{}, style{};
    Vector3 insertion_scale{1,1,1};
};
struct DwgFace {
    DwgR2000EntityProperties properties;
    std::array<Point3, 4> corners{};
    std::uint8_t invisible_edges = 0;
};
struct DwgSolid {
    DwgR2000EntityProperties properties;
    std::array<Point3,4> corners{};
    double thickness{};
    Vector3 normal{0,0,1};
};
struct DwgText {
    DwgR2000EntityProperties properties;
    std::string value;
    Point3 insertion{}, alignment{};
    Vector3 normal{0, 0, 1};
    double height{1}, thickness{}, rotation{}, oblique_angle{}, width_factor{1};
    std::int16_t generation{}, horizontal{}, vertical{};
    std::uint64_t style{}; // Style handle, or AC1009 table slot.
    unsigned flags{};
    bool has_alignment{};
};
// Stored surface construction inputs; edits do not regenerate the primary B-rep.
struct DwgSurface {
    std::string type;
    std::int16_t u_isolines{}, v_isolines{};
    SurfaceDefinition definition;
    SurfaceProfiles profiles;
    std::size_t parameters_begin_bit{}, parameters_end_bit{};
};
// COMMON_3DSOLID fields; stored reference point is distinct from derived geometry centre.
struct DwgModeler {
    DwgR2000EntityProperties properties;
    std::int16_t type{}, acis_version{}; // 37 REGION, 38 3DSOLID, 39 BODY; surfaces use a positional class code.
    bool acis_empty = true, acis_empty_bit = false;
    ProfileRegion data;
    std::optional<DwgSurface> surface;
    std::uint64_t history{};
    std::size_t materials_begin_bit{}, materials_end_bit{}, material_handles_begin{}, material_handles_end{};
    std::size_t payload_begin_bit{}, payload_end_bit{}; // Inline ACIS payload only.
};
// Fixed DWG table record types follow the on-disk type numbers. Block records also
// expose layout, BLOCK/ENDBLK and INSERT references. R13–R2000 store first/last links.
struct DwgSymbolRecord {
    std::int16_t type{};
    std::string name;
    std::uint64_t owner{}, begin{}, end{}, first{}, last{}, layout{}, extension_dictionary{};
    std::vector<std::uint64_t> members, inserts;
    std::uint64_t linetype{}; // LAYER record reference
    Point3 base_point{}; // BLOCK_HEADER insertion base in block coordinates.
    bool external_block{}; // Xref or overlay.
};
struct DwgEedItem {
    std::uint64_t application{};
    std::string payload;
};
struct DwgInsert {
    DwgR2000EntityProperties properties;
    Vector3 insertion{}, scale{1, 1, 1}, normal{0, 0, 1};
    double rotation{}, column_spacing{}, row_spacing{};
    std::uint64_t block{};
    std::int16_t columns = 1, rows = 1;
    bool minsert = false, has_attributes = false;
    std::string legacy_block_name; // AC1009 references a block table slot instead of a handle.
};
struct DwgDynamicRecord {
    std::uint64_t handle{}, owner{}, extension_dictionary{};
    std::vector<std::uint64_t> reactors;
    std::string dxf_name, cpp_name;
    DynamicBlockKind kind = DynamicBlockKind::Unknown;
    bool entity = false;
    DwgR2000EntityProperties properties;
    DynamicBlockPayload data;
};
struct DwgClassRecord {
    std::int16_t number{}, proxy_flags{}, item_class_id{};
    std::string application, cpp_name, dxf_name;
    bool zombie{};
    std::array<std::int32_t, 5> fields{}; // Instance count, DWG/maintenance versions and reserved fields.
};
struct DwgR2000Objects {
    std::uint64_t model_space{}, paper_space{};
    std::map<std::uint64_t, DwgSymbolRecord> symbols;
    std::map<std::string, Color> layer_colors; // Named CAD layers, including AC1009 table entries.
    std::map<std::string, std::vector<double>> simple_linetypes;
    std::map<std::string, std::uint16_t> legacy_layer_linetypes;
    // AC1009 tables have physical slots instead of stored object handles.
    std::map<std::int16_t, std::vector<std::string>> legacy_symbol_names;
    std::map<std::string, std::size_t> legacy_block_ends;
    std::map<std::string, DwgSymbolRecord> legacy_blocks; // Named AC1009 blocks, indexed by table name.
    std::map<std::uint64_t, DwgR2000Record> records;
    std::map<std::uint64_t, DwgR2000Line> lines;
    std::map<std::uint64_t, DwgLwPolyline> lwpolylines;
    std::map<std::uint64_t, std::string> lwpolyline_errors;
    std::map<std::uint64_t, DwgCircle> circles;
    std::map<std::uint64_t, DwgArc> arcs;
    std::map<std::uint64_t, DwgText> texts;
    std::map<std::uint64_t, DwgDimension> dimensions;
    std::map<std::uint64_t, DwgSolid> solids;
    std::map<std::uint64_t, DwgFace> faces;
    std::map<std::uint64_t, std::string> dimension_errors;
    std::map<std::uint64_t, DwgMText> mtexts;
    std::map<std::uint64_t, std::string> mtext_errors;
    // Positional class names, including duplicates.
    std::vector<std::string> class_names;
    std::vector<DwgClassRecord> classes;
    std::map<std::uint64_t,DwgHistoryRecord> history_records;
    std::map<std::uint64_t,DwgHistoryRecord> metadata_records;
    std::map<std::uint64_t,DwgDynamicRecord> dynamic_records;
    std::map<std::uint64_t,AssociativeObject> associative_records;
    std::map<std::uint64_t,ClassObject> class_objects;
    std::map<std::uint64_t,ArchEntityData> arch_entities;
    std::map<std::uint64_t,ArchObject> arch_objects;
    std::map<std::uint64_t,std::string> arch_errors;
    std::map<std::uint64_t,DwgInsert> inserts;
    std::map<std::uint64_t,std::vector<DwgEedItem>> eed;
    std::map<std::uint64_t,std::string> history_errors, metadata_errors, dynamic_errors, insert_errors,
        associative_errors, class_object_errors;
    std::string classes_error;
    std::map<std::uint64_t,DwgModeler> modelers;
    // Raw records survive unsupported/malformed modeler payloads. Typed access reports these errors.
    std::map<std::uint64_t,std::string> modeler_errors;
    // AC1009 compound records follow traversal order, including JUMP continuations.
    std::map<std::uint64_t, DwgR2000EntityProperties> legacy_entity_properties;
    std::map<std::uint64_t, std::vector<std::uint64_t>> legacy_sequences;
    // Each handle has a retained record with its raw type and source byte offset in records.
    std::vector<std::uint64_t> unknown_fixed_types;
};
// Validates every mapped record and decodes LINE geometry and handle references.
DwgR2000Objects read_dwg_r2000_objects(std::string_view file);
// Linear DWG container used by R13, R14 and R2000.
DwgR2000Index read_dwg_linear_index(std::string_view file);
DwgR2000Objects read_dwg_linear_objects(std::string_view file);
// AC1009 exposes stored entity handles and block names. For paged releases
// offsets refer to the decompressed AcDbObjects section.
DwgR2000Objects read_dwg_objects(std::string_view file);
// Change a circular radius in a supported DWG. Rebuild record and map checksums.
std::string set_dwg_radius(std::string_view file, std::uint64_t handle, double radius);
} // namespace libopencad
