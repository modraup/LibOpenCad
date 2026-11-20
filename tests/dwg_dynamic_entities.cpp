// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
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
static std::string bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static DwgDocument parse(const std::string &s) {
    std::istringstream i(s);
    return DwgDocument::read(i);
}
int main(int argc, char **argv) try {
    if (argc != 2)
        return 2;
    const std::string prefix = argv[1];
    std::istringstream in(create_dwg(DxfVersion::AC1032, {}));
    auto d = DwgDocument::read(in);
    DwgDynamicRecord empty;
    empty.dxf_name = "LINEARPARAMETERENTITY";
    empty.cpp_name = "AcDbBlockLinearParameterEntity";
    empty.kind = DynamicBlockKind::EmptyEntity;
    empty.entity = true;
    empty.data = std::monostate{};
    auto linear = d.insert_dynamic_record(empty);
    check(d.objects().dynamic_records.count(linear), "Empty entity inserted");
    check(d.dynamic_record(linear).kind == DynamicBlockKind::EmptyEntity, "Empty entity kind");
    auto identity = d.dynamic_record(linear);
    d.update_dynamic_record(identity);
    check(d.dynamic_record(linear).kind == DynamicBlockKind::EmptyEntity, "Empty entity identity");
    DwgDynamicRecord angular;
    angular.dxf_name = "BLOCKANGULARCONSTRAINTPARAMETERENTITY";
    angular.cpp_name = "AcDbBlockAngularConstraintParameterEntity";
    angular.kind = DynamicBlockKind::AngularConstraintParameterEntity;
    angular.entity = true;
    BlockAngularConstraintParameterEntity payload;
    payload.expression_name = "NativeAngle";
    payload.angle = 0.75;
    payload.center_point = {4, 5, 0};
    payload.label_point = {6, 7, 0};
    angular.data = payload;
    auto angle = d.insert_dynamic_record(angular);
    auto *value = std::get_if<BlockAngularConstraintParameterEntity>(&d.dynamic_record(angle).data);
    check(value && value->expression_name == "NativeAngle" && std::abs(value->angle - 0.75) < 1e-12,
          "Angular DWG payload");
    auto edited = d.dynamic_record(angle);
    std::get<BlockAngularConstraintParameterEntity>(edited.data).expression_name = "EditedAngle";
    d.update_dynamic_record(edited);
    check(std::get<BlockAngularConstraintParameterEntity>(d.dynamic_record(angle).data).expression_name ==
              "EditedAngle",
          "Angular DWG rewrite");
    bool refused = false;
    try {
        DwgDynamicRecord unknown;
        unknown.handle = linear;
        unknown.dxf_name = "LINEARPARAMETERENTITY";
        unknown.kind = DynamicBlockKind::Unknown;
        unknown.entity = true;
        d.update_dynamic_record(unknown);
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "Unknown DWG entity rewrite accepted");
    for (int mutation = 0; mutation < 4; ++mutation) {
        auto bad = empty;
        if (mutation == 0)
            bad.kind = DynamicBlockKind::LinearParameter;
        if (mutation == 1)
            bad.data = BlockLinearParameter{};
        if (mutation == 2)
            bad.dxf_name = "BLOCKLINEARPARAMETER";
        if (mutation == 3) {
            bad.dxf_name = "BLOCKLINEARPARAMETER";
            bad.kind = DynamicBlockKind::LinearParameter;
            bad.data = BlockLinearParameter{};
        }
        auto before = bytes(d);
        refused = false;
        try {
            d.insert_dynamic_record(bad);
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused && bytes(d) == before, "Invalid dynamic insertion must reject atomically");
    }
    auto saved = bytes(d);
    auto reopened = parse(saved);
    check(reopened.dynamic_record(linear).kind == DynamicBlockKind::EmptyEntity, "Empty DWG roundtrip");
    check(std::get<BlockAngularConstraintParameterEntity>(reopened.dynamic_record(angle).data).expression_name ==
              "EditedAngle",
          "Angular DWG roundtrip");
    std::ofstream(prefix + ".dwg", std::ios::binary) << saved;
    std::cout << "Dynamic DWG entity rewrite and allocation verified\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
