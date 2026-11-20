// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <optional>
#include <unordered_map>
#include <libopencad/arch.hpp>
#include <libopencad/civil.hpp>
#include <libopencad/dwg_r2000.hpp>
#include <libopencad/dxf_document.hpp>
#include <libopencad/preview.hpp>
namespace libopencad {
// Same-version edits to an existing drawing. Geometry uses the same entity
// types and units as DxfDocument. Keep original bytes for opaque objects.
class DwgDocument {
  public:
    static DwgDocument read(std::istream &input);
    void write(std::ostream &output) const;
    const DwgR2000Objects &objects() const { return objects_; }
    DxfLine line(std::uint64_t handle) const;
    DxfCircle circle(std::uint64_t handle) const;
    DxfArc arc(std::uint64_t handle) const;
    DxfText text(std::uint64_t handle) const;
    // R13+ REGION/BODY/3DSOLID and R2004+ surfaces. Typed errors are isolated by handle.
    // R2007+ history and owned versioned metadata. Shared dictionaries reject edits.
    // Replaces operation inputs, preserving common fields and the entity B-rep.
    HistoryLoftSettings history_loft_settings(std::uint64_t handle) const;
    void set_history_loft_settings(std::uint64_t handle, const HistoryLoftSettings&);
    // Allocates a root, evaluation graph and first operation. Surfaces attach through
    // CADCODEC_SURFACE_HISTORY_V1; 3DSOLID (and R2007–R2010 REGION/BODY) use the stored handle.
    SolidHistoryGraph create_solid_history(std::uint64_t entity, SolidHistoryOperation operation,
                                           SurfaceProfiles profiles = {});
    SolidHistoryGraph append_solid_history(std::uint64_t entity, SolidHistoryOperation operation,
                                           SurfaceProfiles profiles = {});
    // Uses the operation evaluation ID (or step-ID fallback), not the graph node ID.
    void set_history_active_node(std::uint64_t root, std::int32_t node);
    void set_history_evaluation_graph(std::uint64_t root, std::uint64_t graph);
    void set_history_node_expression(std::uint64_t graph, std::int32_t node, std::uint64_t expression);
    void set_history_material(std::uint64_t operation, std::uint64_t material);
    void set_surface_history(std::uint64_t surface, std::uint64_t root);
    void update_history_inputs(std::uint64_t handle, const SolidHistoryParameters&, const SurfaceProfiles&);
    const DwgHistoryRecord &history_record(std::uint64_t handle) const;
    std::optional<SolidHistoryGraph> modeler_history_graph(std::uint64_t entity_handle) const;
    // Typed dynamic-block objects, INSERT/MINSERT and saved-state queries. No action evaluation.
    const DwgDynamicRecord &dynamic_record(std::uint64_t handle) const;
    const DwgInsert &insert(std::uint64_t handle) const;
    void update_dynamic_record(const DwgDynamicRecord &record);
    std::uint64_t insert_dynamic_record(DwgDynamicRecord record, std::uint64_t block_or_layout = 0);
    // AC1032 Arch constructors. Same-version DWG I/O preserves opaque bodies.
    static DwgDocument create_wall(Vector3 start, Vector3 end, double width, double height,
                                   std::optional<std::string> layer = std::nullopt);
    static DwgDocument create_door(double width, double height,
                                   std::optional<std::string> layer = std::nullopt);
    static DwgDocument create_window(double width, double height,
                                     std::optional<std::string> layer = std::nullopt);
    std::optional<ArchEntityData> architecture_entity(std::uint64_t handle) const;
    std::optional<ArchObject> architecture_object(std::uint64_t handle) const;
    void set_wall(std::uint64_t handle, Vector3 start, Vector3 end, double width, double height);
    void set_door(std::uint64_t handle, double width, double height);
    void set_window(std::uint64_t handle, double width, double height);
    void set_opening(std::uint64_t handle, double width, double height);
    void set_stair(std::uint64_t handle, double run, double width, double height);
    void set_member(std::uint64_t handle, double length, double breadth, double depth);
    void set_window_assembly(std::uint64_t handle, Vector3 min, Vector3 max);
    void set_railing(std::uint64_t handle, Vector3 min, Vector3 max);
    void set_slab(std::uint64_t handle, Vector3 min, Vector3 max);
    void set_roof(std::uint64_t handle, Vector3 min, Vector3 max);
    void set_roof_slab(std::uint64_t handle, Vector3 min, Vector3 max);
    void set_space(std::uint64_t handle, Vector3 min, Vector3 max);
    void set_curtain_wall(std::uint64_t handle, Vector3 min, Vector3 max);
    void set_column_grid(std::uint64_t handle, Vector3 min, Vector3 max);
    void set_mass_element(std::uint64_t handle, Vector3 min, Vector3 max);
    void set_display_theme(std::uint64_t handle, Vector3 min, Vector3 max);
    void set_ceiling_grid(std::uint64_t handle, Vector3 min, Vector3 max);
    std::uint64_t add_wall(Vector3 start, Vector3 end, double width, double height,
                           std::optional<std::string> layer = std::nullopt);
    void translate_architecture(std::uint64_t handle, Vector3 delta);
    void set_profile_bounds(std::uint64_t handle, Vector3 min, Vector3 max);
    void reshape_rectangle(std::uint64_t handle, Vector3 start, Vector3 end, double width);
    std::vector<ContinuousWall> continuous_walls() const;
    void set_continuous_wall(const ContinuousWall &chain, const std::vector<Vector3> &points, double width,
                             double height);
    std::vector<std::uint64_t> entities_anchored_to(std::uint64_t host) const;
    // Civil 3D COGO points, number-list and elevation/description point groups,
    // siteless alignments of fixed lines and TIN surfaces from points.
    // Construction is AC1032 DWG only.
    // COGO construction requires the drawing's Civil settings/style tree;
    // alignments and TIN surfaces write that tree, the siteless node and the
    // surface style collection when missing. Stand-in style records for tests
    // are not a public constructor.
    std::uint64_t ensure_civil();
    std::vector<CogoPoint> cogo_points() const;
    std::uint64_t add_cogo_point(std::optional<std::uint32_t> number, Vector3 position,
                                std::optional<std::string> description = std::nullopt,
                                std::optional<std::uint64_t> style = std::nullopt,
                                std::optional<std::string> layer = std::nullopt);
    void set_cogo_point(std::uint64_t point, std::uint32_t number, Vector3 position,
                       std::optional<std::string> description = std::nullopt);
    void remove_cogo_point(std::uint64_t point);
    std::uint64_t add_point_group(std::string_view name, std::optional<std::string> description,
                                 const std::vector<std::uint32_t> &numbers);
    void set_point_group(std::uint64_t group, std::string_view name, std::optional<std::string> description,
                        const std::vector<std::uint32_t> &numbers);
    std::uint64_t add_point_group_query(std::string_view name, std::optional<std::string> description,
                                       const PointQuery &query);
    void set_point_group_query(std::uint64_t group, std::string_view name, std::optional<std::string> description,
                              const PointQuery &query);
    std::vector<std::uint32_t> point_group_numbers(std::uint64_t group) const;
    std::optional<std::uint32_t> cogo_point_number(std::uint64_t point) const;
    std::uint64_t add_alignment(std::string_view name, const std::vector<Vector2> &vertices,
                               std::optional<std::string> layer = std::nullopt);
    std::uint64_t add_alignment_with_curves(std::string_view name, const std::vector<Vector2> &vertices,
                                           const std::vector<VertexCurve> &curves,
                                           std::optional<std::string> layer = std::nullopt);
    std::optional<std::string> alignment_name(std::uint64_t handle) const;
    std::optional<double> alignment_start_station(std::uint64_t handle) const;
    std::vector<std::vector<Vector2>> alignment_polylines(std::uint64_t handle) const;
    std::optional<std::vector<AlignmentElement>> alignment_elements(std::uint64_t handle) const;
    std::vector<std::pair<AlignmentPiece, std::vector<Vector2>>> alignment_pieces(std::uint64_t handle) const;
    std::uint64_t add_profile(std::uint64_t alignment, std::string_view name, const std::vector<Vector2> &pvis,
                             const std::vector<double> &curves, std::optional<std::string> layer = std::nullopt);
    std::uint64_t add_profile_with_curves(std::uint64_t alignment, std::string_view name, const std::vector<Vector2> &pvis,
                                         const std::vector<VerticalCurve> &curves,
                                         std::optional<std::string> layer = std::nullopt);
    std::optional<std::string> profile_name(std::uint64_t handle) const;
    std::vector<std::pair<ProfilePiece, std::vector<Vector2>>> profile_pieces(std::uint64_t handle) const;
    std::uint64_t add_tin_surface(std::string_view name, const std::vector<Vector3> &points,
                                 std::optional<std::string> layer = std::nullopt);
    std::optional<TinSurface> tin_surface(std::uint64_t handle) const;
    bool is_dynamic_block(std::uint64_t definition) const;
    std::optional<std::uint64_t> dynamic_block_definition(std::uint64_t block) const;
    std::optional<std::uint64_t> dynamic_definition_for_insert(std::uint64_t insert) const;
    std::optional<BlockEvaluationGraph> dynamic_block_graph(std::uint64_t definition) const;
    std::optional<BlockVisibilityParameter> block_visibility_parameter(std::uint64_t definition) const;
    std::optional<std::pair<std::uint64_t, BlockVisibilityParameter>>
        dynamic_visibility_for_insert(std::uint64_t insert) const;
    std::vector<DynamicBlockProperty> dynamic_block_properties(std::uint64_t definition,
        std::optional<std::uint64_t> insert = std::nullopt) const;
    // Typed DWG associative objects. Missing/non-associative handles return empty.
    // Decode errors and unknown families refuse rewrite. Graph walks detect cycles.
    std::optional<AssociativeObject> associative_object(std::uint64_t handle) const;
    void update_associative_object(const AssociativeObject& object);
    std::vector<std::uint64_t> associative_references(std::uint64_t handle) const;
    std::vector<std::uint64_t> associative_objects_referencing(std::uint64_t target) const;
    void remap_associative_handles(const std::unordered_map<std::uint64_t, std::uint64_t>& map);
    std::vector<std::uint64_t> associative_owned_network(std::uint64_t network) const;
    // Typed DWG ClassObject slice: RENDERSETTINGS, MENTALRAYRENDERSETTINGS,
    // RAPIDRTRENDERSETTINGS and SECTION_MANAGER. Decode errors refuse rewrite.
    std::optional<ClassObject> class_object(std::uint64_t handle) const;
    void update_class_object(const ClassObject& object);
    std::uint64_t insert_class_object(ClassObject object);
    void remap_class_object_handles(const std::unordered_map<std::uint64_t, std::uint64_t>& map);
    // Map 3D Object Data graph APIs matching acadrust CadDocument. Construction requires R2004+.
    std::vector<std::pair<std::uint64_t, ObjectDataTable>> object_data_tables() const;
    std::uint64_t add_object_data_table(const ObjectDataTable& table);
    std::uint64_t attach_object_data(std::uint64_t entity, std::uint64_t table,
                                     const std::vector<ObjectDataValue>& values);
    std::vector<ObjectDataAttachment> object_data(std::uint64_t entity) const;
    const DwgSurface &surface(std::uint64_t handle) const;
    void update_surface(std::uint64_t handle, const DwgSurface &surface);
    const DwgModeler &modeler(std::uint64_t handle) const;
    // Native lifecycle; target 0 selects model space. Copies include owned history/dictionary graphs.
    std::uint64_t add_modeler(ModelerKind kind, const acis::SatDocument&, std::uint64_t block_or_layout=0);
    std::uint64_t add_surface(const DwgSurface&, const acis::SatDocument&, std::uint64_t block_or_layout=0);
    std::uint64_t copy_modeler(std::uint64_t handle, std::uint64_t block_or_layout=0,
                               std::map<std::uint64_t,std::uint64_t>* remapping=nullptr);
    void remove_modeler(std::uint64_t handle);
    // Replace or fill a body, creating the R2013+ datastore when necessary.
    // Existing AcDs schema and indices must match the supported native layout.
    // Preserves display caches/history; does not regenerate them. Failure is atomic.
    void set_modeler_geometry(std::uint64_t handle, const acis::SatDocument &geometry);
    // Clear primary ACIS data and its material bindings; retain entity, caches and history.
    void clear_modeler_geometry(std::uint64_t handle);
    Vector3 modeler_reference_point(std::uint64_t handle) const;
    std::optional<acis::SatDocument> modeler_geometry(std::uint64_t handle) const;
    // TEXT supports value, insertion, format, oblique angle, generation flags, thickness,
    // normal, existing style assignment and common display properties.
    // AC1009 stores the low eight generation bits. Unsupported display fields are omitted.
    // TextDefinition angles are radians; alignment Z shares the insertion elevation.
    void update(const DxfText &text);
    void update(const DxfLine &line);
    void update(const DxfCircle &circle);
    void update(const DxfArc &arc);
    // Return the new handle (R13+) or zero-based layer slot (R11/R12).
    std::uint64_t add_layer(std::string_view name, int color_index = 7);
    // Positive lengths are dashes, negative lengths are gaps, and zero is a dot.
    // Returns a handle (R13+) or the physical LTYPE table slot (R11/R12).
    std::uint64_t add_linetype(std::string_view name, const std::vector<double> &segments,
                               std::string_view description = {});
    void set_layer_linetype(std::string_view layer, std::string_view linetype);
    // Move primitives, modelers/surfaces, INSERT or traditional POLYLINE groups to a block or layout.
    void move_entity(std::uint64_t entity, std::uint64_t block_or_layout);
    void move_entity(std::uint64_t entity, std::string_view block_name);
    // rust CadDocument::preview: DWG thumbnail at the header preview seeker.
    std::optional<Preview> preview() const;
    void set_preview(std::optional<Preview> value);

  private:
    explicit DwgDocument(std::string bytes);
    void initialize(DxfEntity &entity, const char *type, std::uint64_t handle,
                    const DwgR2000EntityProperties &props) const;
    template <class Entity>
    std::optional<std::string> edit_geometry_layer(const Entity &entity, const Entity &before) const;
    template <class Entity> void apply(const Entity &entity, const Entity &before);
    void apply_common(const DxfEntity &entity, const DxfEntity &before,
                      std::optional<DwgDocument> &staged) const;
    std::string bytes_;
    DwgR2000Objects objects_;
};
} // namespace libopencad
