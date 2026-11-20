// SPDX-License-Identifier: MPL-2.0
// Port of acadrust objects/associative.rs and io/dxf/{reader,writer} associative modules.
#include <libopencad/dxf_document.hpp>
#include <libopencad/dxf_raw_field.hpp>
#include "dxf_binary.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace libopencad {
namespace {
std::string hex(std::uint64_t value) {
    std::ostringstream s;
    s << std::hex << std::uppercase << value;
    return s.str();
}
std::string trimmed(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos)
        return {};
    return std::string(value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1));
}
std::string ascii_upper(std::string_view value) {
    std::string out(value);
    for (char &c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::uint64_t handle_of(std::string_view input) {
    auto value = trimmed(input);
    if (value.empty())
        return 0;
    std::uint64_t result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        return 0;
    return result;
}
std::int32_t integer_of(std::string_view input) {
    auto value = trimmed(input);
    if (value.empty())
        return 0;
    std::int64_t result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        return 0;
    if (result < std::numeric_limits<std::int32_t>::min() ||
        result > std::numeric_limits<std::int32_t>::max())
        return 0;
    return static_cast<std::int32_t>(result);
}
std::int64_t integer64_of(std::string_view input) {
    auto value = trimmed(input);
    if (value.empty())
        return 0;
    std::int64_t result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        return 0;
    return result;
}
double real_of(std::string_view input) {
    auto value = trimmed(input);
    if (value.empty())
        return 0;
    std::istringstream in{std::string(value)};
    in.imbue(std::locale::classic());
    double result = 0;
    if (!(in >> result) || !(in >> std::ws).eof() || !std::isfinite(result))
        return 0;
    return result;
}
constexpr int kMaxCount = 100000;
int capped(std::int32_t count) {
    if (count < 0)
        return 0;
    return count > kMaxCount ? kMaxCount : count;
}
} // namespace

std::string associative_canonical_name(std::string_view name) {
    auto canonical = ascii_upper(name);
    if (canonical.rfind("ACDBASSOC", 0) == 0)
        canonical = "ASSOC" + canonical.substr(9);
    if (canonical == "ASSOCALIGNEDIMACTIONBODY")
        return "ASSOCALIGNEDDIMACTIONBODY";
    if (canonical == "ACDBPERSSUBENTMANAGER")
        return "PERSUBENTMGR";
    if (canonical == "ACDBDIMASSOC")
        return "DIMASSOC";
    return canonical;
}
bool is_associative_object_name(std::string_view name) {
    static const std::unordered_set<std::string> names = {
        "ASSOCDEPENDENCY",
        "ASSOCPLANESURFACEACTIONBODY",
        "ASSOCEXTENDSURFACEACTIONBODY",
        "ASSOCEXTRUDEDSURFACEACTIONBODY",
        "ASSOCLOFTEDSURFACEACTIONBODY",
        "ASSOCNETWORKSURFACEACTIONBODY",
        "ASSOCOFFSETSURFACEACTIONBODY",
        "ASSOCREVOLVEDSURFACEACTIONBODY",
        "ASSOCTRIMSURFACEACTIONBODY",
        "ASSOCBLENDSURFACEACTIONBODY",
        "ASSOCPATCHSURFACEACTIONBODY",
        "ASSOCFILLETSURFACEACTIONBODY",
        "ASSOCACTION",
        "ASSOCVALUEDEPENDENCY",
        "ASSOCGEOMDEPENDENCY",
        "ASSOCNETWORK",
        "ASSOCSWEPTSURFACEACTIONBODY",
        "ASSOCEDGECHAMFERACTIONBODY",
        "ASSOCEDGEFILLETACTIONBODY",
        "ASSOCRESTOREENTITYSTATEACTIONBODY",
        "ASSOCMLEADERACTIONBODY",
        "ASSOCALIGNEDDIMACTIONBODY",
        "ASSOC3POINTANGULARDIMACTIONBODY",
        "ASSOCORDINATEDIMACTIONBODY",
        "ASSOCROTATEDDIMACTIONBODY",
        "ASSOCPERSSUBENTMANAGER",
        "ASSOCEDGEACTIONPARAM",
        "ASSOC2DCONSTRAINTGROUP",
        "ASSOCVARIABLE",
        "ASSOCACTIONPARAM",
        "ASSOCCOMPOUNDACTIONPARAM",
        "ASSOCOSNAPPOINTREFACTIONPARAM",
        "ASSOCPOINTREFACTIONPARAM",
        "ASSOCOBJECTACTIONPARAM",
        "ASSOCPATHACTIONPARAM",
        "ASSOCDIMDEPENDENCYBODY",
        "ASSOCFACEACTIONPARAM",
        "ASSOCVERTEXACTIONPARAM",
        "ASSOCASMBODYACTIONPARAM",
        "ASSOCARRAYMODIFYPARAMETERS",
        "ASSOCARRAYPATHPARAMETERS",
        "ASSOCARRAYPOLARPARAMETERS",
        "ASSOCARRAYRECTANGULARPARAMETERS",
        "ASSOCARRAYACTIONBODY",
        "ASSOCARRAYMODIFYACTIONBODY",
        "DIMASSOC",
        "PERSUBENTMGR",
        "ASSOCVIEWREPACTIONBODY",
        "ASSOCVIEWBORDERACTIONPARAM",
        "ASSOCVIEWREPHATCHMANAGER",
        "ASSOCVIEWREPACTIONPARAM",
        "ASSOCVIEWREPHATCHACTIONPARAM",
        "ASSOCVIEWSYMBOLACTIONPARAM",
        "ASSOCVIEWSTYLEACTIONPARAM",
        "ASSOCVIEWLABELACTIONPARAM",
        "ACDBCENTERMARKACTIONBODY",
        "ACDBCENTERLINEACTIONBODY",
    };
    return names.count(associative_canonical_name(name)) != 0;
}
std::optional<std::string> associative_cpp_class_name(std::string_view name) {
    auto canonical = associative_canonical_name(name);
    static const std::map<std::string, const char *> names = {
        {"ASSOCDEPENDENCY", "AcDbAssocDependency"},
        {"ASSOCPLANESURFACEACTIONBODY", "AcDbAssocPlaneSurfaceActionBody"},
        {"ASSOCEXTENDSURFACEACTIONBODY", "AcDbAssocExtendSurfaceActionBody"},
        {"ASSOCEXTRUDEDSURFACEACTIONBODY", "AcDbAssocExtrudedSurfaceActionBody"},
        {"ASSOCLOFTEDSURFACEACTIONBODY", "AcDbAssocLoftedSurfaceActionBody"},
        {"ASSOCNETWORKSURFACEACTIONBODY", "AcDbAssocNetworkSurfaceActionBody"},
        {"ASSOCOFFSETSURFACEACTIONBODY", "AcDbAssocOffsetSurfaceActionBody"},
        {"ASSOCREVOLVEDSURFACEACTIONBODY", "AcDbAssocRevolvedSurfaceActionBody"},
        {"ASSOCTRIMSURFACEACTIONBODY", "AcDbAssocTrimSurfaceActionBody"},
        {"ASSOCBLENDSURFACEACTIONBODY", "AcDbAssocBlendSurfaceActionBody"},
        {"ASSOCPATCHSURFACEACTIONBODY", "AcDbAssocPatchSurfaceActionBody"},
        {"ASSOCFILLETSURFACEACTIONBODY", "AcDbAssocFilletSurfaceActionBody"},
        {"ASSOCACTION", "AcDbAssocAction"},
        {"ASSOCVALUEDEPENDENCY", "AcDbAssocValueDependency"},
        {"ASSOCGEOMDEPENDENCY", "AcDbAssocGeomDependency"},
        {"ASSOCNETWORK", "AcDbAssocNetwork"},
        {"ASSOCSWEPTSURFACEACTIONBODY", "AcDbAssocSweptSurfaceActionBody"},
        {"ASSOCEDGECHAMFERACTIONBODY", "AcDbAssocEdgeChamferActionBody"},
        {"ASSOCEDGEFILLETACTIONBODY", "AcDbAssocEdgeFilletActionBody"},
        {"ASSOCRESTOREENTITYSTATEACTIONBODY", "AcDbAssocRestoreEntityStateActionBody"},
        {"ASSOCMLEADERACTIONBODY", "AcDbAssocMLeaderActionBody"},
        {"ASSOCALIGNEDDIMACTIONBODY", "AcDbAssocAlignedDimActionBody"},
        {"ASSOC3POINTANGULARDIMACTIONBODY", "AcDbAssoc3PointAngularDimActionBody"},
        {"ASSOCORDINATEDIMACTIONBODY", "AcDbAssocOrdinateDimActionBody"},
        {"ASSOCROTATEDDIMACTIONBODY", "AcDbAssocRotatedDimActionBody"},
        {"ASSOCPERSSUBENTMANAGER", "AcDbAssocPersSubentManager"},
        {"ASSOCEDGEACTIONPARAM", "AcDbAssocEdgeActionParam"},
        {"ASSOC2DCONSTRAINTGROUP", "AcDbAssoc2dConstraintGroup"},
        {"ASSOCVARIABLE", "AcDbAssocVariable"},
        {"ASSOCACTIONPARAM", "AcDbAssocActionParam"},
        {"ASSOCCOMPOUNDACTIONPARAM", "AcDbAssocCompoundActionParam"},
        {"ASSOCOSNAPPOINTREFACTIONPARAM", "AcDbAssocOsnapPointRefActionParam"},
        {"ASSOCPOINTREFACTIONPARAM", "AcDbAssocPointRefActionParam"},
        {"ASSOCOBJECTACTIONPARAM", "AcDbAssocObjectActionParam"},
        {"ASSOCPATHACTIONPARAM", "AcDbAssocPathActionParam"},
        {"ASSOCDIMDEPENDENCYBODY", "AcDbAssocDimDependencyBody"},
        {"ASSOCFACEACTIONPARAM", "AcDbAssocFaceActionParam"},
        {"ASSOCVERTEXACTIONPARAM", "AcDbAssocVertexActionParam"},
        {"ASSOCASMBODYACTIONPARAM", "AcDbAssocAsmbodyActionParam"},
        {"ASSOCARRAYMODIFYPARAMETERS", "AcDbAssocArrayModifyParameters"},
        {"ASSOCARRAYPATHPARAMETERS", "AcDbAssocArrayPathParameters"},
        {"ASSOCARRAYPOLARPARAMETERS", "AcDbAssocArrayPolarParameters"},
        {"ASSOCARRAYRECTANGULARPARAMETERS", "AcDbAssocArrayRectangularParameters"},
        {"ASSOCARRAYACTIONBODY", "AcDbAssocArrayActionBody"},
        {"ASSOCARRAYMODIFYACTIONBODY", "AcDbAssocArrayModifyActionBody"},
        {"DIMASSOC", "AcDbDimAssoc"},
        {"PERSUBENTMGR", "AcDbPersSubentManager"},
        {"ASSOCVIEWREPACTIONBODY", "AcDbAssocViewRepActionBody"},
        {"ASSOCVIEWBORDERACTIONPARAM", "AcDbAssocViewBorderActionParam"},
        {"ASSOCVIEWREPHATCHMANAGER", "AcDbAssocViewRepHatchManager"},
        {"ASSOCVIEWREPACTIONPARAM", "AcDbAssocViewRepActionParam"},
        {"ASSOCVIEWREPHATCHACTIONPARAM", "AcDbAssocViewRepHatchActionParam"},
        {"ASSOCVIEWSYMBOLACTIONPARAM", "AcDbAssocViewSymbolActionParam"},
        {"ASSOCVIEWSTYLEACTIONPARAM", "AcDbAssocViewStyleActionParam"},
        {"ASSOCVIEWLABELACTIONPARAM", "AcDbAssocViewLabelActionParam"},
        {"ACDBCENTERMARKACTIONBODY", "AcDbCenterMarkActionBody"},
        {"ACDBCENTERLINEACTIONBODY", "AcDbCenterLineActionBody"},
    };
    auto found = names.find(canonical);
    if (found == names.end())
        return std::nullopt;
    return found->second;
}
std::int32_t Assoc2dConstraintGroup::effective_next_node_id() const {
    auto next = next_node_id;
    for (const auto &node : nodes) {
        if (node.node_id < std::numeric_limits<std::int32_t>::max())
            next = std::max(next, node.node_id + 1);
    }
    return next;
}

namespace {
void visit_eval(AssocEvalVariant &value, const std::function<void(std::uint64_t &)> &visit) {
    if (auto *handle = std::get_if<std::uint64_t>(&value.value))
        visit(*handle);
}
void visit_value_param(AssocValueParam &value, const std::function<void(std::uint64_t &)> &visit) {
    for (auto &variable : value.variables) {
        visit_eval(variable.value, visit);
        visit(variable.handle);
    }
    visit(value.controlled_object_dependency);
}
void visit_dependency(AssocDependency &value, const std::function<void(std::uint64_t &)> &visit) {
    visit(value.dependent_on);
    visit(value.read_dependency);
    visit(value.node);
    visit(value.dependency_body);
}
void visit_action(AssocAction &value, const std::function<void(std::uint64_t &)> &visit) {
    visit(value.owning_network);
    visit(value.action_body);
    for (auto &dependency : value.dependencies)
        visit(dependency.dependency);
    for (auto &handle : value.owned_parameters)
        visit(handle);
    for (auto &parameter : value.values)
        visit_value_param(parameter, visit);
}
void visit_parameter_body(AssocParamBasedActionBody &value,
                          const std::function<void(std::uint64_t &)> &visit) {
    for (auto &handle : value.dependencies)
        visit(handle);
    for (auto &parameter : value.values)
        visit_value_param(parameter, visit);
    visit(value.dependency);
}
void visit_single(AssocSingleDependencyActionParam &value,
                  const std::function<void(std::uint64_t &)> &visit) {
    visit(value.dependency);
}
void visit_compound(AssocCompoundActionParam &value, const std::function<void(std::uint64_t &)> &visit) {
    for (auto &handle : value.parameters)
        visit(handle);
    if (value.child_parameter) {
        visit(value.child_parameter->parameter);
        visit(value.child_parameter->secondary_parameter);
        visit(value.child_parameter->tertiary_parameter);
    }
}
void visit_array_items(std::vector<AssocArrayItem> &items,
                       const std::function<void(std::uint64_t &)> &visit) {
    for (auto &item : items) {
        if (item.first_handle)
            visit(*item.first_handle);
        if (item.second_handle)
            visit(*item.second_handle);
    }
}
void visit_data(AssociativeData &data, const std::function<void(std::uint64_t &)> &visit) {
    std::visit(
        [&](auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, AssocDependency>)
                visit_dependency(value, visit);
            else if constexpr (std::is_same_v<T, AssocValueDependency>) {
                visit_dependency(value.dependency, visit);
                visit_eval(value.value, visit);
            } else if constexpr (std::is_same_v<T, AssocGeomDependency>)
                visit_dependency(value.dependency, visit);
            else if constexpr (std::is_same_v<T, AssocSurfaceActionBody>) {
                visit_parameter_body(value.parameter_body, visit);
                visit(value.surface_body.dependency);
            } else if constexpr (std::is_same_v<T, AssocAction>)
                visit_action(value, visit);
            else if constexpr (std::is_same_v<T, AssocNetwork>) {
                visit_action(value.action, visit);
                for (auto &dependency : value.actions)
                    visit(dependency.dependency);
                for (auto &handle : value.owned_actions)
                    visit(handle);
            } else if constexpr (std::is_same_v<T, AssocAnnotationActionBody>) {
                visit_parameter_body(value.annotation.parameter_body, visit);
                visit(value.annotation.dependency);
                visit(value.entity);
                for (auto &dependency : value.actions)
                    visit(dependency.dependency);
                visit(value.read_node);
                visit(value.dimension_node);
                visit(value.dependency);
            } else if constexpr (std::is_same_v<T, AssocEdgeActionParam>) {
                visit_single(value.single_dependency, visit);
                visit(value.parameter);
            } else if constexpr (std::is_same_v<T, Assoc2dConstraintGroup>) {
                visit_action(value.action, visit);
                visit(value.dependency);
                for (auto &handle : value.actions)
                    visit(handle);
                for (auto &node : value.nodes)
                    std::visit(
                        [&](auto &item) {
                            using N = std::decay_t<decltype(item)>;
                            if constexpr (std::is_same_v<N, AssocConstraintAngle> ||
                                          std::is_same_v<N, AssocConstraintDistance> ||
                                          std::is_same_v<N, AssocConstraintRadiusDiameter>) {
                                visit(item.value_dependency);
                                visit(item.dimension_dependency);
                            } else if constexpr (std::is_same_v<N, AssocConstraintImplicitPoint> ||
                                                 std::is_same_v<N, AssocConstraintPoint> ||
                                                 std::is_same_v<N, AssocConstraintLine> ||
                                                 std::is_same_v<N, AssocConstraintBoundedLine> ||
                                                 std::is_same_v<N, AssocConstraintCircle> ||
                                                 std::is_same_v<N, AssocConstraintArc> ||
                                                 std::is_same_v<N, AssocConstraintEllipse> ||
                                                 std::is_same_v<N, AssocConstraintBoundedEllipse> ||
                                                 std::is_same_v<N, AssocConstraintRigidSet> ||
                                                 std::is_same_v<N, AssocConstraintSpline>)
                                visit(item.geometry_dependency);
                        },
                        node.data);
            } else if constexpr (std::is_same_v<T, AssocVariable>) {
                visit_action(value.action, visit);
                visit_eval(value.value, visit);
                for (auto &item : value.dependencies)
                    visit(item.dependency);
            } else if constexpr (std::is_same_v<T, AssocCompoundActionParam>)
                visit_compound(value, visit);
            else if constexpr (std::is_same_v<T, AssocPointRefActionParam>)
                visit_compound(value.compound, visit);
            else if constexpr (std::is_same_v<T, AssocOsnapPointRefActionParam>)
                visit_compound(value.compound, visit);
            else if constexpr (std::is_same_v<T, AssocPathActionParam>)
                visit_compound(value.compound, visit);
            else if constexpr (std::is_same_v<T, AssocSingleDependencyActionParam>)
                visit_single(value, visit);
            else if constexpr (std::is_same_v<T, AssocFaceActionParam> ||
                               std::is_same_v<T, AssocVertexActionParam>)
                visit_single(value.single_dependency, visit);
            else if constexpr (std::is_same_v<T, AssocAsmBodyActionParam>) {
                visit_single(value.single_dependency, visit);
                visit(value.history);
            } else if constexpr (std::is_same_v<T, AssocArrayParameters>)
                visit_array_items(value.items, visit);
            else if constexpr (std::is_same_v<T, AssocArrayActionBody>) {
                visit_parameter_body(value.parameter_body, visit);
                visit_array_items(value.items, visit);
            } else if constexpr (std::is_same_v<T, AssocArrayModifyActionBody>) {
                visit_parameter_body(value.body.parameter_body, visit);
                visit_array_items(value.body.items, visit);
            } else if constexpr (std::is_same_v<T, AssocDimensionAssociation>) {
                visit(value.dimension);
                for (auto &slot : value.references)
                    for (auto &reference : slot) {
                        for (auto &handle : reference.xrefs)
                            visit(handle);
                        for (auto &handle : reference.intersection_objects)
                            visit(handle);
                    }
            } else if constexpr (std::is_same_v<T, AssocViewRepActionBody>)
                visit(value.view_rep);
            else if constexpr (std::is_same_v<T, AssocViewObjectActionParam> ||
                               std::is_same_v<T, AssocViewRepHatchActionParam> ||
                               std::is_same_v<T, AssocViewLabelActionParam>)
                visit_single(value.single_dependency, visit);
            else if constexpr (std::is_same_v<T, AssocViewRepHatchManager>) {
                visit_compound(value.compound, visit);
                for (auto &item : value.items)
                    visit(item.parameter);
            } else if constexpr (std::is_same_v<T, AssocSmartCenterActionBody>)
                visit_parameter_body(value.parameter_body, visit);
        },
        data);
}
} // namespace

void AssociativeObject::visit_handles(const std::function<void(std::uint64_t &)> &visit) {
    visit(owner);
    for (auto &reactor : reactors)
        visit(reactor);
    if (xdictionary_handle)
        visit(*xdictionary_handle);
    visit_data(data, visit);
}
void AssociativeObject::visit_handles(const std::function<void(std::uint64_t)> &visit) const {
    auto copy = *this;
    copy.visit_handles([&](std::uint64_t &handle) { visit(handle); });
}
bool AssociativeObject::references_handle(std::uint64_t target) const {
    bool found = false;
    visit_handles([&](std::uint64_t handle) {
        if (handle == target)
            found = true;
    });
    return found;
}
std::vector<std::uint64_t> AssociativeObject::referenced_handles() const {
    std::vector<std::uint64_t> handles;
    visit_handles([&](std::uint64_t handle) {
        if (handle)
            handles.push_back(handle);
    });
    return handles;
}
void AssociativeObject::remap_handles(const std::unordered_map<std::uint64_t, std::uint64_t> &map) {
    visit_handles([&](std::uint64_t &handle) {
        auto found = map.find(handle);
        if (found != map.end())
            handle = found->second;
    });
}

namespace {
#include "associative_dxf.inc"
} // namespace

class AssociativeReader {
  public:
    static AssociativeObject parse(const DxfDocument &doc, const DxfRecord &record) {
        AssocDxfRecord collected;
        std::string section;
        std::string group;
        bool owner_seen = false;
        for (const auto &pair : record.pairs_) {
            auto value = detail::is_string_value(pair.code)
                             ? record.decode_string(pair.value)
                             : (record.binary_ ? detail::text_value_from_binary(pair.code, pair.value)
                                               : pair.value);
            if (pair.code == 0)
                continue;
            if (pair.code == 5) {
                collected.handle = handle_of(value);
                continue;
            }
            if (pair.code == 102) {
                group = value;
                if (group == "}")
                    group.clear();
                continue;
            }
            if (pair.code == 330 && group == "{ACAD_REACTORS") {
                collected.reactors.push_back(handle_of(value));
                continue;
            }
            if (pair.code == 360 && group == "{ACAD_XDICTIONARY") {
                collected.xdictionary_handle = handle_of(value);
                continue;
            }
            if (pair.code == 330 && !owner_seen && group.empty() && section.empty()) {
                collected.owner = handle_of(value);
                owner_seen = true;
                continue;
            }
            if (pair.code == 100) {
                section = value;
                continue;
            }
            collected.sections[section].push_back({pair.code, std::move(value)});
        }
        if (!collected.handle)
            collected.handle = handle_of(record.handle());
        if (!collected.owner)
            collected.owner = handle_of(record.owner_handle());
        return parse_associative_object(collected, record.type(), doc.version());
    }
    static void encode(DxfDocument &doc, DxfRecord &record, const AssociativeObject &object) {
        auto fields = encode_associative_fields(object, doc.version());
        std::vector<DxfRecord::Pair> pairs;
        pairs.reserve(fields.size());
        for (const auto &field : fields) {
            auto binary = detail::binary_value_from_text(field.code, field.value);
            pairs.push_back({field.code, detail::is_string_value(field.code)
                                             ? record.encode_string(field.value)
                                             : (record.binary_ ? std::move(binary) : field.value)});
        }
        record.pairs_ = std::move(pairs);
    }
    static DxfRecord &mutable_record(DxfDocument &doc, std::uint64_t id) {
        auto found = doc.handle_index_.find(id);
        if (found == doc.handle_index_.end() || found->second.size() != 1 || found->second[0].vertex ||
            found->second[0].seqend)
            throw std::invalid_argument("Associative edit requires a unique object");
        return std::visit([](DxfRecord &record) -> DxfRecord & { return record; },
                          doc.records_[found->second[0].record]);
    }
    static std::vector<const DxfRecord *> associative_records(const DxfDocument &doc) {
        std::vector<const DxfRecord *> records;
        for (std::size_t i = 0; i < doc.records_.size(); ++i) {
            const auto &record = doc.record_at({i, std::nullopt, false});
            if (is_associative_object_name(record.type()))
                records.push_back(&record);
        }
        return records;
    }
};

std::optional<AssociativeObject> DxfDocument::associative_object(std::uint64_t handle) const {
    if (!handle)
        return std::nullopt;
    auto record = get_record(hex(handle));
    if (!record || !is_associative_object_name(record->type()))
        return std::nullopt;
    auto object = AssociativeReader::parse(*this, *record);
    object.source_version = version_;
    return object;
}
void DxfDocument::update_associative_object(const AssociativeObject &object) {
    if (std::holds_alternative<std::monostate>(object.data))
        throw std::invalid_argument("Cannot rewrite unknown associative object");
    if (!object.handle || object.dxf_name.empty())
        throw std::invalid_argument("Associative object requires an identity");
    auto copy = *this;
    auto &record = AssociativeReader::mutable_record(copy, object.handle);
    if (!is_associative_object_name(record.type()))
        throw std::invalid_argument("Record is not an associative object");
    if (record.type() != object.dxf_name)
        throw std::invalid_argument("Associative object name mismatch");
    if (handle_of(record.handle()) != object.handle)
        throw std::invalid_argument("Associative object handle mismatch");
    AssociativeReader::encode(copy, record, object);
    auto cpp = associative_cpp_class_name(object.dxf_name).value_or(object.cpp_class_name);
    bool registered = false;
    for (const auto &entry : copy.classes())
        if (entry.name == object.dxf_name) {
            if (entry.is_entity)
                throw std::invalid_argument("Conflicting associative CLASS");
            registered = true;
            break;
        }
    if (!registered) {
        ClassDefinition definition;
        definition.name = object.dxf_name;
        definition.cpp_name = cpp;
        copy.set_class(definition);
    }
    *this = std::move(copy);
}
std::vector<std::uint64_t> DxfDocument::associative_references(std::uint64_t handle) const {
    auto object = associative_object(handle);
    if (!object)
        return {};
    return object->referenced_handles();
}
std::vector<std::uint64_t> DxfDocument::associative_objects_referencing(std::uint64_t target) const {
    std::vector<std::uint64_t> handles;
    for (auto *record : AssociativeReader::associative_records(*this)) {
        auto object = AssociativeReader::parse(*this, *record);
        if (object.references_handle(target))
            handles.push_back(object.handle);
    }
    std::sort(handles.begin(), handles.end());
    handles.erase(std::unique(handles.begin(), handles.end()), handles.end());
    return handles;
}
void DxfDocument::remap_associative_handles(const std::unordered_map<std::uint64_t, std::uint64_t> &map) {
    auto copy = *this;
    std::vector<AssociativeObject> objects;
    for (auto *record : AssociativeReader::associative_records(copy)) {
        auto object = AssociativeReader::parse(copy, *record);
        if (std::holds_alternative<std::monostate>(object.data))
            continue;
        object.remap_handles(map);
        objects.push_back(std::move(object));
    }
    for (const auto &object : objects)
        AssociativeReader::encode(copy, AssociativeReader::mutable_record(copy, object.handle), object);
    *this = std::move(copy);
}
std::vector<std::uint64_t> DxfDocument::associative_owned_network(std::uint64_t network) const {
    std::vector<std::uint64_t> owned;
    std::unordered_set<std::uint64_t> seen;
    std::vector<std::uint64_t> stack{network};
    while (!stack.empty()) {
        auto handle = stack.back();
        stack.pop_back();
        if (!handle || !seen.insert(handle).second)
            continue;
        owned.push_back(handle);
        auto object = associative_object(handle);
        if (!object)
            continue;
        if (auto *value = std::get_if<AssocNetwork>(&object->data))
            for (auto action : value->owned_actions)
                stack.push_back(action);
    }
    return owned;
}
} // namespace libopencad
