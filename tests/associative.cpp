// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static std::uint64_t h(const std::string &s) { return s.empty() ? 0 : std::stoull(s, nullptr, 16); }
static std::string hex(std::uint64_t v) {
    std::ostringstream s;
    s << std::hex << std::uppercase << v;
    return s.str();
}
static std::string bytes(const DxfDocument &d, bool binary) {
    std::ostringstream s;
    d.write(s, binary);
    return s.str();
}
static DxfDocument read_bytes(const std::string &s) {
    std::istringstream i(s);
    return DxfDocument::read(i);
}
static std::vector<std::uint64_t> associative_handles(const DxfDocument &d) {
    std::vector<std::uint64_t> ids;
    auto collect = [&](const DxfRecord &object) {
        if (is_associative_object_name(object.type()))
            ids.push_back(h(object.handle()));
    };
    d.for_each<DxfSemanticObject>(collect);
    d.for_each<DxfRecord>(collect);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}
static std::string kind_name(AssociativeKind kind) {
    switch (kind) {
    case AssociativeKind::Unknown:
        return "Unknown";
    case AssociativeKind::Dependency:
        return "Dependency";
    case AssociativeKind::ValueDependency:
        return "ValueDependency";
    case AssociativeKind::GeomDependency:
        return "GeomDependency";
    case AssociativeKind::SurfaceActionBody:
        return "SurfaceActionBody";
    case AssociativeKind::Action:
        return "Action";
    case AssociativeKind::Network:
        return "Network";
    case AssociativeKind::AnnotationActionBody:
        return "AnnotationActionBody";
    case AssociativeKind::PersSubentManager:
        return "PersSubentManager";
    case AssociativeKind::EdgeActionParam:
        return "EdgeActionParam";
    case AssociativeKind::ConstraintGroup:
        return "ConstraintGroup";
    case AssociativeKind::Variable:
        return "Variable";
    case AssociativeKind::ActionParam:
        return "ActionParam";
    case AssociativeKind::CompoundActionParam:
        return "CompoundActionParam";
    case AssociativeKind::OsnapPointRefActionParam:
        return "OsnapPointRefActionParam";
    case AssociativeKind::PointRefActionParam:
        return "PointRefActionParam";
    case AssociativeKind::ObjectActionParam:
        return "ObjectActionParam";
    case AssociativeKind::PathActionParam:
        return "PathActionParam";
    case AssociativeKind::DimDependencyBody:
        return "DimDependencyBody";
    case AssociativeKind::FaceActionParam:
        return "FaceActionParam";
    case AssociativeKind::VertexActionParam:
        return "VertexActionParam";
    case AssociativeKind::AsmBodyActionParam:
        return "AsmBodyActionParam";
    case AssociativeKind::ArrayParameters:
        return "ArrayParameters";
    case AssociativeKind::ArrayActionBody:
        return "ArrayActionBody";
    case AssociativeKind::ArrayModifyActionBody:
        return "ArrayModifyActionBody";
    case AssociativeKind::DimensionAssociation:
        return "DimensionAssociation";
    case AssociativeKind::PersSubentManagerStatic:
        return "PersSubentManagerStatic";
    case AssociativeKind::ViewRepActionBody:
        return "ViewRepActionBody";
    case AssociativeKind::ViewObjectActionParam:
        return "ViewObjectActionParam";
    case AssociativeKind::ViewRepHatchManager:
        return "ViewRepHatchManager";
    case AssociativeKind::ViewRepHatchActionParam:
        return "ViewRepHatchActionParam";
    case AssociativeKind::ViewLabelActionParam:
        return "ViewLabelActionParam";
    case AssociativeKind::SmartCenterActionBody:
        return "SmartCenterActionBody";
    }
    return "Unknown";
}
static std::string trace(const DxfDocument &d) {
    std::ostringstream out;
    for (auto id : associative_handles(d)) {
        auto object = d.associative_object(id);
        check(object.has_value(), "Missing associative object");
        auto refs = object->referenced_handles();
        std::sort(refs.begin(), refs.end());
        refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
        out << "O\t" << hex(object->handle) << '\t' << object->dxf_name << '\t' << object->cpp_class_name
            << '\t' << hex(object->owner) << '\t' << kind_name(object->kind()) << '\t' << refs.size();
        for (auto ref : refs)
            out << '\t' << hex(ref);
        out << '\n';
        if (auto *network = std::get_if<AssocNetwork>(&object->data)) {
            auto owned = d.associative_owned_network(id);
            out << "N\t" << hex(id) << '\t' << network->owned_actions.size() << '\t' << owned.size();
            for (auto handle : owned)
                out << '\t' << hex(handle);
            out << '\n';
        }
        if (auto *group = std::get_if<Assoc2dConstraintGroup>(&object->data)) {
            out << "C\t" << hex(id) << '\t' << group->version << '\t' << group->nodes.size();
            for (const auto &node : group->nodes)
                out << '\t' << (node.class_name.empty() ? "-" : node.class_name) << ':' << node.node_id;
            out << '\n';
        }
        if (auto *variable = std::get_if<AssocVariable>(&object->data))
            out << "V\t" << hex(id) << '\t' << variable->name << '\t' << variable->expression << '\n';
    }
    return out.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto d = DxfDocument::read(in);
    auto original = bytes(d, d.is_binary());
    auto ids = associative_handles(d);
    check(ids.size() >= 12, "Fixture object count");
    check(!d.associative_object(0) && !d.associative_object(0xFFFFFFFFu), "Unknown handles");
    std::optional<std::uint64_t> network, other, action, variable, group, shared;
    int kinds = 0;
    for (auto id : ids) {
        auto object = d.associative_object(id);
        check(object.has_value(), "Parse associative object");
        check(object->kind() != AssociativeKind::Unknown, "Unknown family in fixture");
        ++kinds;
        if (object->kind() == AssociativeKind::Network) {
            if (!network)
                network = id;
            else
                other = id;
        } else if (object->kind() == AssociativeKind::Action)
            action = id;
        else if (object->kind() == AssociativeKind::Variable)
            variable = id;
        else if (object->kind() == AssociativeKind::ConstraintGroup)
            group = id;
    }
    check(network && other && action && variable && group, "Core graph objects");
    auto owned = d.associative_owned_network(*network);
    check(std::find(owned.begin(), owned.end(), *network) != owned.end(), "Owned walk includes start");
    check(std::find(owned.begin(), owned.end(), *other) != owned.end(), "Owned walk includes cycle peer");
    check(std::count(owned.begin(), owned.end(), *network) == 1, "Cycle visited once");
    auto refs = d.associative_objects_referencing(*action);
    check(refs.size() >= 2, "Shared action references");
    shared = *action;
    check(d.associative_references(*variable).size() >= 1, "Variable payload handles");
    auto t = trace(d);
    check(original == bytes(d, d.is_binary()), "Read-only API mutated document");
    for (auto id : ids) {
        auto object = *d.associative_object(id);
        d.update_associative_object(object);
    }
    check(trace(d) == t, "Identity rewrite changed graph");
    std::ofstream(std::string(argv[2]) + ".trace") << t;
    for (bool binary : {false, true}) {
        auto saved = bytes(d, binary);
        auto reopened = read_bytes(saved);
        check(trace(reopened) == t, "Native roundtrip graph");
        std::ofstream out(std::string(argv[2]) + (binary ? "-binary.dxf" : "-ascii.dxf"), std::ios::binary);
        out << saved;
    }
    auto edited = *d.associative_object(*variable);
    auto *value = std::get_if<AssocVariable>(&edited.data);
    check(value, "Variable payload");
    value->name = "宽度";
    d.update_associative_object(edited);
    auto again = d.associative_object(*variable);
    check(again && std::get<AssocVariable>(again->data).name == "宽度", "Variable name edit");
    std::unordered_map<std::uint64_t, std::uint64_t> remap;
    remap[*shared] = 0xABC;
    auto before = d.associative_objects_referencing(*shared);
    d.remap_associative_handles(remap);
    check(d.associative_objects_referencing(0xABC).size() == before.size(), "Handle remap");
    check(d.associative_objects_referencing(*shared).empty(), "Old handle still referenced");
    auto broken = bytes(read_bytes(original), false);
    auto marker = broken.find("AcConstrainedPoint");
    check(marker != std::string::npos, "Constraint class marker");
    broken.replace(marker, 18, "AcUnsupportedNode");
    auto damaged = read_bytes(broken);
    bool refused = false;
    try {
        for (auto id : associative_handles(damaged))
            damaged.associative_object(id);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "Unsupported constraint class accepted");
    AssociativeObject unknown;
    unknown.handle = *variable;
    unknown.dxf_name = "ACDBASSOCVARIABLE";
    bool unknown_refused = false;
    try {
        d.update_associative_object(unknown);
    } catch (const std::invalid_argument &) {
        unknown_refused = true;
    }
    check(unknown_refused, "Unknown rewrite accepted");
    std::cout << "Associative DXF families, graphs, remap, encodings and malformed refusal verified ("
              << kinds << " objects)\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
