// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/sheet_set.rs (DST cipher, XML database, AcSheetSetData).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace libopencad {

inline constexpr const char *kClsidDatabase = "g2162C6B6-0CE4-40E8-912B-46F59DFDF826";
inline constexpr const char *kClsidSheetSet = "gB20534F2-0978-418C-8D14-2E6928A077ED";
inline constexpr const char *kClsidSubset = "g076D548F-B0F5-4FE1-B35D-7F7B73B8D322";
inline constexpr const char *kClsidSheet = "g16A07941-BC15-4D48-A880-9D5A211D5065";
inline constexpr const char *kClsidCalloutBlocks = "g203EAB46-483B-4E6B-A10B-15E9A4B210FF";
inline constexpr const char *kClsidCustomPropertyBag = "g4D103908-8C86-4D95-BBF4-68B9A7B00731";
inline constexpr const char *kClsidCustomPropertyValue = "g8D22A2A4-1777-4D78-84CC-69EF741FE954";
inline constexpr const char *kClsidProjectPointLocations = "gE40EA246-BAB4-4907-81A5-511EA30C16FD";
inline constexpr const char *kClsidPublishOptions = "gF57F96E7-0F16-4DC9-8F09-52F7BB389AB6";
inline constexpr const char *kClsidResources = "g3F0FAF10-09DE-4EBA-AED1-C4E4D6FECF5D";
inline constexpr const char *kClsidSheetSelSets = "g444780B8-6527-43A8-8DC4-FAB41B7E48BB";
inline constexpr const char *kClsidViewCategories = "g021730DF-5BEA-48E9-BC7A-35087A674FD0";
inline constexpr const char *kClsidLayoutReference = "g94910E94-4FCA-427C-B6ED-2EC9E1C900C7";
inline constexpr const char *kClsidSheetViews = "gF40F931B-64BC-4B90-9FC8-A11A77D6815B";
inline constexpr const char *kClsidFileReference = "g6BF87AE7-1BEC-4BDB-98BB-5B91F7772793";
inline constexpr const char *kClsidSimpleFileReference = "gD15A03C2-C39B-428A-9BBA-C031347C496F";
inline constexpr const char *kClsidSheetView = "gB6E09611-4659-4F0D-981D-D62B11FD8426";
inline constexpr const char *kClsidViewReference = "g9BEA33B1-05AD-419F-B680-BC7FF6A4F41D";
inline constexpr const char *kClsidViewCategory = "g4AEA81ED-C24F-477B-A534-EA69220A276A";
inline constexpr const char *kClsidCalloutBlockReferences = "g67C52FE4-0A6B-4C82-A4CC-5E68537747B0";
inline constexpr const char *kClsidObjectReference = "g00DEB7FB-A073-4ECD-BCE0-121B45C6864D";
inline constexpr const char *kClsidBlockRecordReference = "g11782523-474B-4C83-9646-57C052847FBB";

inline constexpr std::int32_t kCustomSheetSetProp = 1;
inline constexpr std::int32_t kCustomSheetProp = 2;
inline constexpr const char *kSheetSetData = "AcSheetSetData";

enum class ComponentKind { SheetSet, Subset, Sheet };

struct SheetSetElement {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;
    std::vector<SheetSetElement> children;

    static SheetSetElement make(std::string name);
    std::optional<std::string_view> attr(std::string_view key) const;
    std::string_view id() const;
    std::optional<std::string_view> propname() const;
    const SheetSetElement *named(std::string_view name) const;
    SheetSetElement *named(std::string_view name);
    std::optional<std::string_view> prop(std::string_view name) const;
    void set_prop(std::string_view name, std::string_view value);
    void set_prop_vt(std::string_view name, std::int32_t vt, std::string_view value);
    void put_named(SheetSetElement child);
    void remove_named(std::string_view name);
    const SheetSetElement *find(std::string_view id) const;
    SheetSetElement *find(std::string_view id);
    const SheetSetElement *parent_of(std::string_view id) const;
};

struct LayoutReference {
    std::string file_name;
    std::string name;
    std::string handle;
};

struct SheetSetData {
    std::string layout_handle;
    std::string layout_name;
    std::string sheet_dwg_name;
    std::string sheet_set_file_name;
    std::int32_t sheet_set_version = 0;
    std::int32_t update_count = 0;
    std::string update_time;
};

struct SheetSetDatabase {
    SheetSetElement root;
    std::optional<std::string> path;

    static SheetSetDatabase make(std::string_view name, std::string_view description);
    static SheetSetDatabase from_xml(std::string_view text);
    static SheetSetDatabase from_bytes(const std::uint8_t *data, std::size_t size);
    static SheetSetDatabase from_bytes(const std::vector<std::uint8_t> &data) {
        return from_bytes(data.data(), data.size());
    }
    static SheetSetDatabase read(const std::string &path);

    std::string to_xml() const;
    std::vector<std::uint8_t> to_bytes() const;
    void write(const std::string &path);

    std::int32_t file_revision() const;
    const SheetSetElement &sheet_set() const;
    SheetSetElement &sheet_set();
    std::string_view name() const;
    const SheetSetElement *find(std::string_view id) const;
    SheetSetElement *find(std::string_view id);
    const SheetSetElement *parent_of(std::string_view id) const;

    std::optional<std::string> add_subset(std::string_view parent, std::string_view name,
                                          std::string_view description);
    std::optional<std::string> add_sheet(std::string_view parent, std::string_view number,
                                         std::string_view title, std::string_view description);
    bool remove(std::string_view id);
    bool set_file_reference(std::string_view component, std::string_view propname, std::string_view file);
    std::optional<std::string> file_reference(std::string_view component, std::string_view propname) const;
    bool set_layout_reference(std::string_view component, std::string_view propname,
                              const LayoutReference &reference);
    std::optional<LayoutReference> layout_reference(std::string_view component,
                                                    std::string_view propname) const;
    std::string resolve_file(const SheetSetElement &reference) const;
    std::vector<const SheetSetElement *> sheets() const;
    const SheetSetElement *sheet_for(std::string_view drawing,
                                     std::optional<std::string_view> layout = {}) const;
    std::optional<std::string> sheet_value(const SheetSetElement &sheet, std::string_view component,
                                           std::string_view property) const;
    std::optional<std::string> set_value(std::string_view property) const;
    std::optional<std::string> navigation_value(std::string_view set_id,
                                                std::optional<std::string_view> component,
                                                std::string_view property) const;
    std::vector<const SheetSetElement *> view_categories() const;
    std::vector<const SheetSetElement *> callout_blocks() const;
    std::optional<std::string> add_callout_block(std::string_view file, std::string_view name,
                                                 std::string_view handle);
    std::vector<std::string> category_blocks(std::string_view id) const;
    std::optional<std::string> set_view_category(std::optional<std::string_view> id, std::string_view name,
                                                 const std::vector<std::string> &blocks);
    std::vector<const SheetSetElement *> sheet_views(const SheetSetElement &sheet) const;
    static std::optional<std::string_view> view_category_of(const SheetSetElement &view);
    std::optional<std::string> add_sheet_view(std::string_view sheet, std::optional<std::string_view> category,
                                              const LayoutReference &view, std::string_view title);
    std::optional<std::string> add_resource(std::string_view folder);
};

std::vector<std::uint8_t> sheet_set_encode(const std::uint8_t *plain, std::size_t size);
std::vector<std::uint8_t> sheet_set_decode(const std::uint8_t *data, std::size_t size);
SheetSetElement parse_sheet_set_xml(std::string_view text);
std::string write_sheet_set_xml(const SheetSetElement &root);
std::string sheet_set_new_id();
SheetSetElement sheet_set_prop(std::string_view name, std::int32_t vt, std::string_view value);
SheetSetElement sheet_set_object(std::string_view tag, std::string_view clsid,
                                 std::optional<std::string_view> propname);
std::optional<ComponentKind> component_kind_of(const SheetSetElement &el);
std::string number_and_title(const SheetSetElement &sheet);
std::vector<std::tuple<std::string, std::string, std::int32_t>> custom_properties(const SheetSetElement &el);
void set_custom_property(SheetSetElement &el, std::string_view name, std::string_view value, std::int32_t flags);
void remove_custom_property(SheetSetElement &el, std::string_view name);
std::optional<std::string> relative_sheet_path(std::string_view folder, std::string_view file);
std::string native_sheet_path(std::string_view path);
std::string sheet_path_key(std::string_view path);
std::string placeholder_type_name(std::string_view component, std::string_view property);
std::optional<std::tuple<std::string, std::string, std::string>> parse_sheet_field_code(std::string_view code);
std::optional<std::tuple<std::string, std::string, std::optional<std::string>>>
parse_sheet_navigation(std::string_view component);
std::vector<std::pair<std::string, std::string>> field_child_values(std::string_view code);

} // namespace libopencad
