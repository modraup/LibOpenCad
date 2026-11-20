// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/classes/arch.
#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <libopencad/dxf_raw_field.hpp>
#include <libopencad/dxf_version.hpp>
#include <libopencad/vector.hpp>

namespace libopencad {

enum class ArchKind {
    Wall,
    Door,
    Window,
    Opening,
    Stair,
    Railing,
    Slab,
    RoofSlab,
    Roof,
    Space,
    CurtainWall,
    CurtainWallUnit,
    WindowAssembly,
    ColumnGrid,
    CeilingGrid,
    Polygon,
    MassElement,
    MassGroup,
    MultiViewBlock,
    MaskBlock,
    Section,
    SectionLine,
    ElevationLine,
    Dimension,
    Schedule,
    LayoutCurve,
    LayoutGrid,
    Zone,
    Area,
    AreaGroup,
    Camera,
    Slice,
    EntityReference,
    Point,
    DisplayTheme,
    StructuralMember,
    Other
};

enum class ArchEditError { UnsupportedLayout, InvalidValue, Rejected };

struct StraightWallGeometry {
    Vector3 start{}, end{};
    double width = 0, height = 0;
};

struct OpeningSize {
    double width = 0, height = 0;
    std::int64_t width_bit = 0, height_bit = 0;
};

struct MemberSize {
    double length = 0, breadth = 0, depth = 0;
    std::int64_t length_bit = 0, breadth_bit = 0, depth_bit = 0;
};

enum class NumberRole { ProfileX, ProfileY, Marker, Width, Height, Length, Section, Other };

struct StoredNumber {
    std::int64_t bit = 0;
    double value = 0;
    NumberRole role = NumberRole::Other;
};

struct AecPlacement {
    std::string identifier;
    Vector3 origin{}, x_axis{1, 0, 0}, y_axis{0, 1, 0}, z_axis{0, 0, 1};
    bool rigid = false;
};

struct DecodedAec {
    std::string dxf_name, cpp_class_name;
    ArchKind kind = ArchKind::Other;
    bool is_entity = true;
    std::optional<std::int16_t> schema;
    std::vector<std::string> strings;
    std::optional<AecPlacement> placement;
    std::vector<std::vector<Vector3>> profiles;
    std::vector<std::uint64_t> handles;
};

struct ContinuousWall {
    std::vector<std::uint64_t> handles;
    std::vector<Vector3> points;
    double width = 0, height = 0;
};

struct ArchEntityData {
    std::string dxf_name, cpp_class_name;
    ArchKind kind = ArchKind::Other;
    std::vector<std::vector<Vector3>> profiles;
    std::vector<StoredNumber> fields;
    std::int16_t dwg_type_code = 0;
    std::int64_t dwg_handle_bits = 0;
    std::optional<DxfVersion> dwg_source_version;
    std::optional<std::string> raw_dwg_data;
    std::optional<std::vector<DxfRawField>> raw_dxf_codes;
    std::vector<std::uint8_t> graphic_data;
    std::uint64_t handle = 0, owner = 0, layer = 0, extension_dictionary = 0;
    std::vector<std::uint64_t> reactors;
    std::string layer_name = "0";
    bool entity = true;
    std::optional<StraightWallGeometry> straight_wall_geometry() const;
    DecodedAec decode() const;
    void decode_fields();
};

struct ArchObject {
    std::string dxf_name, cpp_class_name;
    ArchKind kind = ArchKind::Other;
    std::uint64_t handle = 0, owner = 0, extension_dictionary = 0;
    std::vector<std::uint64_t> reactors;
    std::optional<std::string> raw_dwg_data;
    std::int64_t raw_dwg_handle_bits = 0;
    std::optional<DxfVersion> raw_dwg_version;
    std::optional<std::vector<DxfRawField>> raw_dxf_codes;
    DecodedAec decode() const;
};

struct ArchitectureOmission {
    std::uint64_t handle = 0;
    std::string name, reason;
};

inline constexpr std::pair<const char *, const char *> ARCH_ENTITY_CLASSES[] = {
    {"AEC_WALL", "AecDbWall"},
    {"AEC_DOOR", "AecDbDoor"},
    {"AEC_WINDOW", "AecDbWindow"},
    {"AEC_OPENING", "AecDbOpening"},
    {"AEC_STAIR", "AecDbStair"},
    {"AEC_RAILING", "AecDbRailing"},
    {"AEC_SLAB", "AecDbSlab"},
    {"AEC_ROOFSLAB", "AecDbRoofSlab"},
    {"AEC_ROOF", "AecDbRoof"},
    {"AEC_SPACE", "AecDbSpace"},
    {"AEC_CURTAIN_WALL_LAYOUT", "AecDbCurtainWallLayout"},
    {"AEC_CURTAIN_WALL_UNIT", "AecDbCurtainWallUnit"},
    {"AEC_WINDOW_ASSEMBLY", "AecDbWindowAssembly"},
    {"AEC_COLUMN_GRID", "AecDbColumnGrid"},
    {"AEC_CEILING_GRID", "AecDbCeilingGrid"},
    {"AEC_POLYGON", "AecDbPolygon"},
    {"AEC_MASS_ELEM", "AecDbMassElem"},
    {"AEC_MASS_GROUP", "AecDbMassGroup"},
    {"AEC_MVBLOCK_REF", "AecDbMvBlockRef"},
    {"AEC_MASKBLOCK_REF", "AecDbMaskBlockRef"},
    {"AEC_2D_SECTION", "AecDb2dSection"},
    {"AEC_BDG_SECTIONLINE", "AecDbBdgSectionLine"},
    {"AEC_BDG_ELEVLINE", "AecDbBdgElevLine"},
    {"AEC_DIMENSION_GROUP", "AecDbDimensionGroup"},
    {"AEC_SCHEDULE_TABLE", "AecDbScheduleTable"},
    {"AEC_LAYOUT_CURVE", "AecDbLayoutCurve"},
    {"AEC_LAYOUT_GRID2D", "AecDbLayoutGrid2d"},
    {"AEC_LAYOUT_GRID3D", "AecDbLayoutGrid3d"},
    {"AEC_ZONE", "AecDbZone"},
    {"AEC_AREA", "AecDbArea"},
    {"AEC_AREAGROUP", "AecDbAreaGroup"},
    {"AEC_CAMERA", "AecDbCamera"},
    {"AEC_SLICE", "AecDbSlice"},
    {"AEC_ENT_REF", "AecDbEntRef"},
    {"AEC_POINT", "AecDbPoint"},
    {"AEC_DISPLAYTHEME", "AecDbDisplayTheme"},
    {"AECS_MEMBER", "AecsDbMember"},
};

bool is_architecture_name(std::string_view dxf_name);
bool is_civil_entity_name(std::string_view dxf_name);
std::optional<ArchKind> architecture_kind_of(std::string_view dxf_name);
std::optional<std::string> architecture_cpp_class_name(std::string_view dxf_name);
std::optional<std::vector<Vector3>> centered_wall_profile(Vector3 start, Vector3 end, double width);
std::optional<std::vector<Vector3>> rectangle_profile(Vector3 start, Vector3 end, double width);
std::optional<std::vector<Vector3>> continuous_wall_outline(const std::vector<Vector3> &points, double width);
std::optional<OpeningSize> door_size(const ArchEntityData &data);
std::optional<OpeningSize> window_size(const ArchEntityData &data);
std::optional<OpeningSize> opening_size(const ArchEntityData &data);
std::optional<MemberSize> member_size(const ArchEntityData &data);

class ArchEditException : public std::invalid_argument {
  public:
    explicit ArchEditException(ArchEditError error)
        : std::invalid_argument(error == ArchEditError::InvalidValue
                                    ? "invalid architecture value"
                                    : error == ArchEditError::Rejected
                                          ? "architecture edit was rejected"
                                          : "unsupported architecture layout"),
          error_(error) {}
    ArchEditError error() const { return error_; }

  private:
    ArchEditError error_;
};

} // namespace libopencad
