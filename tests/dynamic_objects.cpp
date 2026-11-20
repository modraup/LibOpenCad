// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
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
static std::vector<std::uint64_t> dynamic_handles(const DxfDocument &d) {
    std::vector<std::uint64_t> ids;
    auto collect = [&](const DxfRecord &object) {
        if (is_dynamic_block_object_name(object.type()))
            ids.push_back(h(object.handle()));
    };
    d.for_each<DxfSemanticObject>(collect);
    d.for_each<DxfRecord>(collect);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}
static std::string trace(const DxfDocument &d) {
    std::ostringstream out;
    for (auto id : dynamic_handles(d)) {
        auto object = d.dynamic_object(id);
        check(object.has_value(), "Parse dynamic object");
        auto refs = object->referenced_handles();
        std::sort(refs.begin(), refs.end());
        refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
        out << "O\t" << hex(object->handle) << '\t' << object->dxf_name << '\t' << object->cpp_class_name
            << '\t' << hex(object->owner) << '\t' << static_cast<int>(object->kind) << '\t' << refs.size();
        for (auto ref : refs)
            out << '\t' << hex(ref);
        out << '\n';
        if (auto *linear = std::get_if<BlockLinearParameter>(&object->data))
            out << "L\t" << hex(id) << '\t' << linear->distance_name << '\n';
        if (auto *visibility = std::get_if<BlockVisibilityParameter>(&object->data))
            out << "V\t" << hex(id) << '\t' << visibility->name << '\t' << visibility->states.size() << '\n';
        if (auto *action = std::get_if<BlockFlipAction>(&object->data)) {
            out << "F\t" << hex(id) << '\t' << action->action.dependencies.size();
            for (auto dep : action->action.dependencies)
                out << '\t' << hex(dep);
            out << '\n';
        }
    }
    return out.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    auto d = DxfDocument::read(in);
    auto original = bytes(d, d.is_binary());
    auto ids = dynamic_handles(d);
    check(ids.size() >= 20, "Fixture object count");
    check(!d.dynamic_object(0) && !d.dynamic_object(0xFFFFFFFFu), "Unknown handles");
    std::optional<std::uint64_t> linear, visibility, action;
    int kinds = 0;
    for (auto id : ids) {
        auto object = d.dynamic_object(id);
        check(object.has_value(), "Parse dynamic object");
        check(object->kind != DynamicBlockKind::Unknown, "Unknown family in fixture");
        ++kinds;
        if (object->kind == DynamicBlockKind::LinearParameter && !linear)
            linear = id;
        else if (object->kind == DynamicBlockKind::VisibilityParameter)
            visibility = id;
        else if (object->kind == DynamicBlockKind::FlipAction)
            action = id;
    }
    check(linear && visibility && action, "Core dynamic objects");
    check(d.dynamic_object_references(*visibility).size() >= 1, "Visibility payload handles");
    auto t = trace(d);
    check(original == bytes(d, d.is_binary()), "Read-only API mutated document");
    for (auto id : ids) {
        auto object = *d.dynamic_object(id);
        d.update_dynamic_object(object);
    }
    check(trace(d) == t, "Identity rewrite changed graph");
    auto definition = d.get_symbol(SymbolTable::BlockRecord, "Dyn主块");
    check(definition.has_value(), "Missing fixture definition");
    check(d.dynamic_block_properties(h(definition->handle)).size() == 20, "Identity rewrite properties");
    std::ofstream(std::string(argv[2]) + ".trace") << t;
    for (bool binary : {false, true}) {
        auto saved = bytes(d, binary);
        auto reopened = read_bytes(saved);
        check(trace(reopened) == t, "Native roundtrip graph");
        std::ofstream out(std::string(argv[2]) + (binary ? "-binary.dxf" : "-ascii.dxf"), std::ios::binary);
        out << saved;
    }
    auto edited = *d.dynamic_object(*linear);
    auto *value = std::get_if<BlockLinearParameter>(&edited.data);
    check(value, "Linear payload");
    value->distance_name = "NativeLinear";
    d.update_dynamic_object(edited);
    auto again = d.dynamic_object(*linear);
    check(again && std::get<BlockLinearParameter>(again->data).distance_name == "NativeLinear",
          "Linear name edit");
    auto flip = *d.dynamic_object(*action);
    auto *payload = std::get_if<BlockFlipAction>(&flip.data);
    check(payload && !payload->action.dependencies.empty(), "Flip dependencies");
    auto shared = payload->action.dependencies.front();
    check(shared != 0, "Flip dependency handle");
    std::unordered_map<std::uint64_t, std::uint64_t> remap;
    remap[shared] = 0xABC;
    auto before = d.dynamic_objects_referencing(shared);
    check(!before.empty(), "Flip referenced");
    d.remap_dynamic_handles(remap);
    check(d.dynamic_objects_referencing(0xABC).size() == before.size(), "Handle remap");
    check(d.dynamic_objects_referencing(shared).empty(), "Old handle still referenced");
    auto remapped = d.dynamic_object(*action);
    check(remapped && std::get<BlockFlipAction>(remapped->data).action.dependencies.front() == 0xABC,
          "Flip dependency remapped");
    DynamicBlockObject unknown;
    unknown.handle = *linear;
    unknown.dxf_name = "BLOCKLINEARPARAMETER";
    unknown.kind = DynamicBlockKind::Unknown;
    bool unknown_refused = false;
    try {
        d.update_dynamic_object(unknown);
    } catch (const std::invalid_argument &) {
        unknown_refused = true;
    }
    check(unknown_refused, "Unknown rewrite accepted");
    auto bad = *d.dynamic_object(*linear);
    bad.kind = DynamicBlockKind::EmptyEntity;
    for (bool insert : {false, true}) {
        auto before_bad = bytes(d, d.is_binary());
        bool refused = false;
        try {
            if (insert)
                d.insert_dynamic_object(bad);
            else
                d.update_dynamic_object(bad);
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused && bytes(d, d.is_binary()) == before_bad, "Invalid DXF object changed document");
    }
    check(d.dynamic_object(*linear) &&
              std::get<BlockLinearParameter>(d.dynamic_object(*linear)->data).distance_name == "NativeLinear",
          "Unknown rewrite mutated document");
    std::cout << "Dynamic DXF families, rewrite, remap and malformed refusal verified (" << kinds
              << " objects)\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
