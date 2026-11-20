// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
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
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    const bool binary = std::string(argv[1]) == "binary";
    const std::string prefix = argv[2];
    auto d = DxfDocument::create(binary);
    DynamicBlockObject empty;
    empty.dxf_name = "LINEARPARAMETERENTITY";
    empty.kind = DynamicBlockKind::EmptyEntity;
    empty.is_entity = true;
    empty.data = std::monostate{};
    auto linear = d.insert_dynamic_entity(empty);
    auto parsed = d.dynamic_entity(linear);
    check(parsed && parsed->kind == DynamicBlockKind::EmptyEntity, "Empty entity parse");
    check(parsed->dxf_name == "LINEARPARAMETERENTITY", "Empty entity name");
    d.update_dynamic_entity(*parsed);
    check(d.dynamic_entity(linear)->kind == DynamicBlockKind::EmptyEntity, "Empty entity identity");
    DynamicBlockObject angular;
    angular.dxf_name = "BLOCKANGULARCONSTRAINTPARAMETERENTITY";
    angular.kind = DynamicBlockKind::AngularConstraintParameterEntity;
    angular.is_entity = true;
    BlockAngularConstraintParameterEntity payload;
    payload.expression_name = "NativeAngle";
    payload.angle = 1.25;
    payload.center_point = {1, 2, 0};
    payload.label_point = {3, 4, 0};
    angular.data = payload;
    auto angle = d.insert_dynamic_entity(angular);
    auto again = d.dynamic_entity(angle);
    check(again && again->kind == DynamicBlockKind::AngularConstraintParameterEntity, "Angular entity parse");
    auto *value = std::get_if<BlockAngularConstraintParameterEntity>(&again->data);
    check(value && value->expression_name == "NativeAngle" && std::abs(value->angle - 1.25) < 1e-12,
          "Angular payload");
    value->expression_name = "EditedAngle";
    d.update_dynamic_entity(*again);
    check(std::get<BlockAngularConstraintParameterEntity>(d.dynamic_entity(angle)->data).expression_name ==
              "EditedAngle",
          "Angular rewrite");
    bool refused = false;
    try {
        DynamicBlockObject bad;
        bad.dxf_name = "BLOCKLINEARPARAMETER";
        bad.kind = DynamicBlockKind::LinearParameter;
        bad.is_entity = true;
        d.insert_dynamic_entity(bad);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "Object family accepted as entity");
    refused = false;
    try {
        DynamicBlockObject unknown;
        unknown.handle = linear;
        unknown.dxf_name = "LINEARPARAMETERENTITY";
        unknown.kind = DynamicBlockKind::Unknown;
        unknown.is_entity = true;
        d.update_dynamic_entity(unknown);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "Unknown entity rewrite accepted");
    for (bool mismatch_kind : {false, true}) {
        auto bad = *d.dynamic_entity(linear);
        if (mismatch_kind)
            bad.kind = DynamicBlockKind::LinearParameter;
        else
            bad.data = BlockLinearParameter{};
        for (bool insert : {false, true}) {
            auto before = bytes(d, binary);
            refused = false;
            try {
                if (insert)
                    d.insert_dynamic_entity(bad);
                else
                    d.update_dynamic_entity(bad);
            } catch (const std::invalid_argument &) {
                refused = true;
            }
            check(refused && bytes(d, binary) == before, "Invalid DXF entity changed document");
        }
    }
    for (bool out_binary : {false, true}) {
        auto saved = bytes(d, out_binary);
        auto reopened = read_bytes(saved);
        check(reopened.dynamic_entity(linear) &&
                  reopened.dynamic_entity(linear)->kind == DynamicBlockKind::EmptyEntity,
              "Empty entity roundtrip");
        auto restored = reopened.dynamic_entity(angle);
        check(restored && std::get<BlockAngularConstraintParameterEntity>(restored->data).expression_name ==
                              "EditedAngle",
              "Angular roundtrip");
        std::ofstream out(prefix + (out_binary ? "-binary.dxf" : "-ascii.dxf"), std::ios::binary);
        out << saved;
    }
    std::cout << "Dynamic DXF entity rewrite and allocation verified\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
