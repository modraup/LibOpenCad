// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <optional>
#include <tuple>
#include <vector>
#include <libopencad/acis.hpp>
#include <libopencad/arch.hpp>
#include <libopencad/dwg_history.hpp>
#include <libopencad/civil.hpp>
#include <libopencad/object_data.hpp>
#include <libopencad/transparency.hpp>
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_r2000.hpp>
namespace libopencad::detail {
std::string register_dwg_surface(std::string_view,const DwgR2000Objects&,std::string_view);
std::string register_dwg_classes(std::string_view, const DwgR2000Objects &,
                                 const std::vector<std::tuple<std::string, std::string, std::int16_t>> &);
std::string create_dwg_history(std::string_view, const DwgR2000Objects &, std::uint64_t,
                               SolidHistoryOperation, const SurfaceProfiles &);
std::string append_dwg_history(std::string_view, const DwgR2000Objects &, std::uint64_t,
                               SolidHistoryOperation, const SurfaceProfiles &);
std::string ensure_dwg_metadata(std::string_view, const DwgR2000Objects &, std::uint64_t, std::string_view,
                                const std::string &);
std::string add_dwg_modeler(std::string_view,const DwgR2000Objects&,unsigned,const DwgSurface*,std::uint64_t,std::uint64_t&);
std::string copy_dwg_modeler(std::string_view,const DwgR2000Objects&,std::uint64_t,std::uint64_t,std::map<std::uint64_t,std::uint64_t>&);
std::string remove_dwg_modeler(std::string_view,const DwgR2000Objects&,std::uint64_t);

std::string edit_dwg_history_parameters(std::string_view,const DwgR2000Objects&,std::uint64_t,const DwgHistoryRecord&);
std::string edit_dwg_surface(std::string_view, const DwgR2000Objects&, std::uint64_t, const DwgSurface&);
std::string clear_dwg_modeler(std::string_view, const DwgR2000Objects&, std::uint64_t);
std::string edit_dwg_modeler(std::string_view, const DwgR2000Objects&, std::uint64_t, const acis::SatDocument&);
std::string format_dwg_text(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                            const DwgText &value);
std::string edit_dwg_text(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                          std::string_view value, Point3 insertion);
std::string add_dwg_layer(std::string_view file, const DwgR2000Objects &objects, std::string_view name,
                          int color, std::uint64_t &id, unsigned lw_index = 31);
std::string add_dwg_linetype(std::string_view file, const DwgR2000Objects &objects, std::string_view name,
                             const std::vector<double> &segments, std::string_view description, std::uint64_t &id);
std::optional<std::string> set_dwg_layer_linetype(std::string_view file, const DwgR2000Objects &objects,
                                                  std::string_view layer, std::string_view linetype);
std::string set_dwg_entity_display(std::string_view file, const DwgR2000Objects &objects,
                                   std::uint64_t entity, std::optional<Transparency> transparency,
                                   std::optional<double> scale, std::optional<bool> invisible,
                                   std::optional<std::uint8_t> weight);
std::string set_dwg_entity_color(std::string_view file, const DwgR2000Objects &objects,
                                 std::uint64_t entity, Color color);
std::string set_dwg_entity_layer(std::string_view file, const DwgR2000Objects &objects,
                                  std::uint64_t entity, std::string_view name);
std::string set_dwg_entity_linetype(std::string_view file, const DwgR2000Objects &objects,
                                    std::uint64_t entity, std::string_view name);
std::string move_dwg_entity(std::string_view file, const DwgR2000Objects &objects, std::uint64_t entity,
                            std::uint64_t block);
std::string move_legacy_entity(std::string_view file, const DwgR2000Objects &objects, std::uint64_t entity,
                               std::string_view name);
std::string edit_dwg_primitive(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                               const DwgPrimitive &value, std::uint64_t layer);
std::string edit_dwg_dynamic_record(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                                    const DwgDynamicRecord &record);
std::string add_dwg_dynamic_record(std::string_view file, const DwgR2000Objects &objects, DwgDynamicRecord record,
                                   std::uint64_t block_or_layout, std::uint64_t &id);
std::string edit_dwg_associative_records(std::string_view file, const DwgR2000Objects &objects,
                                         const std::vector<AssociativeObject> &records);
std::string edit_dwg_class_objects(std::string_view file, const DwgR2000Objects &objects,
                                   const std::vector<ClassObject> &records);
std::string add_dwg_class_object(std::string_view file, const DwgR2000Objects &objects, ClassObject record,
                                 std::uint64_t &id);
std::string add_dwg_object_data_table(std::string_view file, const DwgR2000Objects &objects,
                                      const ObjectDataTable &table, std::uint64_t &id);
std::string attach_dwg_object_data(std::string_view file, const DwgR2000Objects &objects, std::uint64_t entity,
                                   std::uint64_t table, const std::vector<ObjectDataValue> &values,
                                   std::uint64_t &id);
std::string create_dwg_architecture(ArchKind kind, Vector3 location, Vector3 direction, double width,
                                    double height, double length, const std::vector<Vector3> &profile,
                                    const std::optional<std::string> &layer);
std::string set_dwg_wall(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                         Vector3 start, Vector3 end, double width, double height);
std::string set_dwg_door(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                         double width, double height);
std::string set_dwg_window(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                           double width, double height);
std::string set_dwg_opening(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                            double width, double height);
std::string set_dwg_stair(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle, double run,
                          double width, double height);
std::string set_dwg_member(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle, double length,
                           double breadth, double depth);
std::string set_dwg_arch_bounds(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                                ArchKind kind, Vector3 min, Vector3 max);
std::string add_dwg_wall(std::string_view file, const DwgR2000Objects &objects, Vector3 start, Vector3 end,
                         double width, double height, const std::optional<std::string> &layer, std::uint64_t &id);
std::string translate_dwg_architecture(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                                       Vector3 delta);
std::string set_dwg_profile_bounds(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                                   Vector3 min, Vector3 max);
std::string reshape_dwg_rectangle(std::string_view file, const DwgR2000Objects &objects, std::uint64_t handle,
                                  Vector3 start, Vector3 end, double width);
std::string set_dwg_continuous_wall(std::string_view file, const DwgR2000Objects &objects, const ContinuousWall &chain,
                                    const std::vector<Vector3> &points, double width, double height);
std::string ensure_dwg_civil(std::string_view file, const DwgR2000Objects &objects);
std::string seed_dwg_civil_point_styles(std::string_view file, const DwgR2000Objects &objects);
std::string add_dwg_cogo_point(std::string_view file, const DwgR2000Objects &objects,
                              std::optional<std::uint32_t> number, Vector3 position,
                              const std::optional<std::string> &description, std::optional<std::uint64_t> style,
                              const std::optional<std::string> &layer, std::uint64_t &id);
std::string set_dwg_cogo_point(std::string_view file, const DwgR2000Objects &objects, std::uint64_t point,
                              std::uint32_t number, Vector3 position, const std::optional<std::string> &description);
std::string remove_dwg_cogo_point(std::string_view file, const DwgR2000Objects &objects, std::uint64_t point);
std::string add_dwg_point_group(std::string_view file, const DwgR2000Objects &objects, std::string_view name,
                               const std::optional<std::string> &description, const std::vector<std::uint32_t> &numbers,
                               std::uint64_t &id);
std::string set_dwg_point_group(std::string_view file, const DwgR2000Objects &objects, std::uint64_t group,
                               std::string_view name, const std::optional<std::string> &description,
                               const std::vector<std::uint32_t> &numbers);
std::string add_dwg_point_group_query(std::string_view file, const DwgR2000Objects &objects, std::string_view name,
                                     const std::optional<std::string> &description, const PointQuery &query,
                                     std::uint64_t &id);
std::string set_dwg_point_group_query(std::string_view file, const DwgR2000Objects &objects, std::uint64_t group,
                                     std::string_view name, const std::optional<std::string> &description,
                                     const PointQuery &query);
std::string add_dwg_alignment(std::string_view file, const DwgR2000Objects &objects, std::string_view name,
                             const std::vector<Vector2> &vertices, const std::optional<std::string> &layer,
                             std::uint64_t &id);
std::string add_dwg_alignment_with_curves(std::string_view file, const DwgR2000Objects &objects, std::string_view name,
                                         const std::vector<Vector2> &vertices, const std::vector<VertexCurve> &curves,
                                         const std::optional<std::string> &layer, std::uint64_t &id);
std::string add_dwg_profile(std::string_view file, const DwgR2000Objects &objects, std::uint64_t alignment,
                           std::string_view name, const std::vector<Vector2> &pvis, const std::vector<double> &curves,
                           const std::optional<std::string> &layer, std::uint64_t &id);
std::string add_dwg_profile_with_curves(std::string_view file, const DwgR2000Objects &objects, std::uint64_t alignment,
                                       std::string_view name, const std::vector<Vector2> &pvis,
                                       const std::vector<VerticalCurve> &curves, const std::optional<std::string> &layer,
                                       std::uint64_t &id);
std::string add_dwg_tin_surface(std::string_view file, const DwgR2000Objects &objects, std::string_view name,
                               const std::vector<Vector3> &points, const std::optional<std::string> &layer,
                               std::uint64_t &id);
} // namespace libopencad::detail
