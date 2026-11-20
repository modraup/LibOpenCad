// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_document.hpp>
#include <algorithm>
#include <fstream>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static std::string hex(std::uint64_t v) {
    std::ostringstream s;
    s << std::hex << std::uppercase << v;
    return s.str();
}
static std::string bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static DwgDocument read_bytes(const std::string &s) {
    std::istringstream i(s);
    return DwgDocument::read(i);
}
static void save(const DwgDocument &d, const std::string &p) {
    std::ofstream o(p, std::ios::binary);
    d.write(o);
    check(bool(o), "Cannot write DWG");
}
static std::string kind_name(AssociativeKind kind) {
    switch (kind) {
    case AssociativeKind::Network:
        return "Network";
    case AssociativeKind::Action:
        return "Action";
    case AssociativeKind::Variable:
        return "Variable";
    case AssociativeKind::ConstraintGroup:
        return "ConstraintGroup";
    default:
        return "Other";
    }
}
static std::string trace(const DwgDocument &d) {
    std::ostringstream out;
    std::vector<std::uint64_t> ids;
    for (auto &[h, object] : d.objects().associative_records)
        ids.push_back(h);
    std::sort(ids.begin(), ids.end());
    for (auto id : ids) {
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
    auto d = DwgDocument::read(in);
    if (!d.objects().associative_errors.empty()) {
        std::ostringstream o;
        o << "Associative decode errors";
        for (auto &[h, e] : d.objects().associative_errors)
            o << " [" << h << "] " << e;
        throw std::runtime_error(o.str());
    }
    check(d.objects().associative_records.size() >= 12, "Fixture object count");
    check(!d.associative_object(0) && !d.associative_object(0xFFFFFFFFu), "Unknown handles");
    std::optional<std::uint64_t> network, other, action, variable, group;
    int kinds = 0;
    for (auto &[id, object] : d.objects().associative_records) {
        auto parsed = d.associative_object(id);
        check(parsed.has_value(), "Parse associative object");
        check(parsed->kind() != AssociativeKind::Unknown, "Unknown family in fixture");
        ++kinds;
        if (parsed->kind() == AssociativeKind::Network) {
            if (!network)
                network = id;
            else
                other = id;
        } else if (parsed->kind() == AssociativeKind::Action)
            action = id;
        else if (parsed->kind() == AssociativeKind::Variable)
            variable = id;
        else if (parsed->kind() == AssociativeKind::ConstraintGroup)
            group = id;
    }
    check(network && other && action && variable && group, "Core graph objects");
    auto owned = d.associative_owned_network(*network);
    check(std::find(owned.begin(), owned.end(), *network) != owned.end(), "Owned walk includes start");
    check(std::find(owned.begin(), owned.end(), *other) != owned.end(), "Owned walk includes cycle peer");
    check(std::count(owned.begin(), owned.end(), *network) == 1, "Cycle visited once");
    auto refs = d.associative_objects_referencing(*action);
    check(refs.size() >= 2, "Shared action references");
    check(d.associative_references(*variable).size() >= 1, "Variable payload handles");
    auto snapshot = bytes(d);
    auto t = trace(d);
    check(snapshot == bytes(d), "Read-only API mutated document");
    std::vector<AssociativeObject> objects;
    for (auto &[id, object] : d.objects().associative_records)
        objects.push_back(object);
    for (auto &object : objects)
        d.update_associative_object(object);
    check(trace(d) == t, "Identity rewrite changed graph");
    save(d, std::string(argv[2]) + ".identity.dwg");
    auto edited = *d.associative_object(*variable);
    auto *value = std::get_if<AssocVariable>(&edited.data);
    check(value, "Variable payload");
    value->name = "宽度";
    d.update_associative_object(edited);
    auto again = d.associative_object(*variable);
    check(again && std::get<AssocVariable>(again->data).name == "宽度", "Variable name edit");
    save(d, std::string(argv[2]) + ".edited.dwg");
    std::unordered_map<std::uint64_t, std::uint64_t> remap;
    remap[*action] = 0xABC;
    auto before = d.associative_objects_referencing(*action);
    d.remap_associative_handles(remap);
    check(d.associative_objects_referencing(0xABC).size() == before.size(), "Handle remap");
    check(d.associative_objects_referencing(*action).empty(), "Old handle still referenced");
    auto damaged_object = *d.associative_object(*group);
    auto *group_data = std::get_if<Assoc2dConstraintGroup>(&damaged_object.data);
    check(group_data && !group_data->nodes.empty(), "Constraint group payload");
    bool renamed_node = false;
    for (auto &node : group_data->nodes)
        if (!node.class_name.empty()) {
            node.class_name = "AcUnsupportedNode";
            renamed_node = true;
            break;
        }
    check(renamed_node, "Constraint class marker");
    auto before_damage = bytes(d);
    d.update_associative_object(damaged_object);
    auto damaged = read_bytes(bytes(d));
    bool refused = false;
    try {
        if (!damaged.objects().associative_errors.empty())
            throw std::invalid_argument(damaged.objects().associative_errors.begin()->second);
        damaged.associative_object(*group);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "Unsupported constraint class accepted");
    d = read_bytes(before_damage);
    AssociativeObject unknown;
    unknown.handle = *variable;
    unknown.dxf_name = d.associative_object(*variable)->dxf_name;
    bool unknown_refused = false;
    auto before_unknown = bytes(d);
    try {
        d.update_associative_object(unknown);
    } catch (const std::invalid_argument &) {
        unknown_refused = true;
    }
    check(unknown_refused, "Unknown rewrite accepted");
    check(bytes(d) == before_unknown, "Rejected rewrite is atomic");
    std::cout << "Associative DWG families, graphs, remap and malformed refusal verified (" << kinds
              << " objects)\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
