// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "geometry_operations.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace libopencad;
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
static std::string bytes(const DxfDocument &doc) {
    std::ostringstream out;
    doc.write(out);
    return out.str();
}
struct InspectableOle : DxfExtendedEntity {
    explicit InspectableOle(const DxfExtendedEntity &e) : DxfExtendedEntity(e) {}
    std::vector<DxfRawField> payload() const {
        auto fields = subclass_fields("AcDbOle2Frame");
        unsigned depth = 0;
        for (auto &f : fields) {
            if (f.code == 102) {
                if (!f.value.empty() && f.value.front() == '{')
                    ++depth;
                else if (depth && f.value == "}")
                    --depth;
            }
            if (!depth && (f.code == 10 || f.code == 20 || f.code == 30 || f.code == 11 || f.code == 21 ||
                           f.code == 31))
                f.value.clear();
        }
        return fields;
    }
    void omit_coordinates(bool partial) {
        auto fields = subclass_fields("AcDbOle2Frame");
        std::vector<DxfRawField> kept;
        for (auto &f : fields) {
            bool coordinate =
                f.code == 10 || f.code == 20 || f.code == 30 || f.code == 11 || f.code == 21 || f.code == 31;
            if (!coordinate || (partial && f.code == 20))
                kept.push_back(f);
        }
        set_subclass_fields("AcDbOle2Frame", kept);
    }
};
static bool same_payload(const DxfExtendedEntity &a, const DxfExtendedEntity &b) {
    auto x = InspectableOle(a).payload(), y = InspectableOle(b).payload();
    if (x.size() != y.size())
        return false;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (x[i].code != y[i].code || x[i].value != y[i].value)
            return false;
    return true;
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    std::ifstream input(argv[1], std::ios::binary);
    auto source = DxfDocument::read(input);
    auto original = bytes(source);
    std::size_t targets = 0, controls = 0;
    source.for_each<DxfExtendedEntity>([&](const auto &e) {
        if (e.type() == "OLE2FRAME") {
            if (e.layer() == "Control")
                ++controls;
            else
                ++targets;
        }
    });
    check(targets == 20 && controls == 2, "OLE fixture coverage");
    for (int step = 0; step < 16; ++step) {
        auto doc = source;
        auto t = operation(step == 14 ? 4 : step == 15 ? 6 : step);
        doc.for_each<DxfExtendedEntity>([&](auto &e) {
            if (e.type() != "OLE2FRAME" || e.layer() == "Control")
                return;
            auto before = e;
            if (step == 0)
                e.translate({3, -4, 5});
            else if (step == 5 || step == 6 || step == 7 || step == 11 || step >= 14) {
                e.apply_mirror(t);
                if (step == 15)
                    e.apply_mirror(t);
            } else
                e.apply_transform(t);
            check(same_payload(before, e), "OLE payload, flags or private data changed");
            check(std::get<OleFrameDefinition>(before.definition()).payload ==
                      std::get<OleFrameDefinition>(e.definition()).payload,
                  "Embedded bytes changed");
        });
        if (step == 7 || step == 11)
            check(bytes(doc) == original, "No-op mirror changed drawing");
        auto path = std::string(argv[2]) + "." + std::to_string(step);
        std::ofstream out(path + ".dxf", std::ios::binary);
        doc.write(out);
        out.close();
        check(bool(out), "Save failed");
        std::ofstream trace(path + ".trace");
        trace << std::setprecision(17);
        doc.for_each<DxfExtendedEntity>([&](const auto &e) {
            if (e.type() != "OLE2FRAME")
                return;
            auto v = std::get<OleFrameDefinition>(e.definition());
            trace << e.handle();
            for (auto p : {v.upper_left, v.lower_right})
                trace << ' ' << p.x << ' ' << p.y << ' ' << p.z;
            trace << '\n';
        });
        trace.close();
        check(bool(trace), "Trace failed");
    }
    source.for_each<DxfExtendedEntity>([&](auto &e) {
        if (e.type() != "OLE2FRAME")
            return;
        for (int action = 0; action < 3; ++action) {
            bool refused = false;
            try {
                if (action == 0)
                    e.translate({std::numeric_limits<double>::quiet_NaN(), 0, 0});
                else if (action == 1)
                    e.apply_transform(operation(11));
                else
                    e.apply_transform(Transform::from_scale(std::numeric_limits<double>::max()));
            } catch (const std::exception &) {
                refused = true;
            }
            check(refused, "Invalid/overflowing edit accepted");
        }
        auto before = std::get<OleFrameDefinition>(e.definition());
        for (bool partial : {false, true}) {
            InspectableOle probe(e);
            probe.omit_coordinates(partial);
            auto v = std::get<OleFrameDefinition>(probe.definition());
            auto expected = Point3{1, partial ? before.upper_left.y : 1, 0};
            check(v.upper_left == expected && v.lower_right == Point3{}, "Missing coordinate defaults");
            probe.translate({3, -4, 5});
            v = std::get<OleFrameDefinition>(probe.definition());
            check(v.upper_left == expected + Point3{3, -4, 5} && v.lower_right == Point3{3, -4, 5},
                  "Partial coordinate translation");
            check(v.payload == before.payload, "Missing-coordinate edit changed payload");
        }
        auto probe = e;
        auto v = before;
        v.extra_fields = {{102, "{PRIVATE"}, {10, "123"}, {20, "456"},   {30, "789"}, {11, "321"},
                          {21, "654"},       {31, "987"}, {310, "00FF"}, {102, "}"}};
        probe.set_definition(v);
        auto authored = probe;
        probe.apply_transform(operation(8));
        check(same_payload(probe, authored), "Private same-code geometry or bytes changed");
        auto a = e, b = e;
        a.translate({3, -4, 5});
        b.apply_transform(operation(1));
        auto av = std::get<OleFrameDefinition>(a.definition()),
             bv = std::get<OleFrameDefinition>(b.definition());
        check(av.upper_left == bv.upper_left && av.lower_right == bv.lower_right,
              "Direct/matrix translation");
        b = e;
        b.apply_mirror(operation(6));
        b.apply_mirror(operation(6));
        bv = std::get<OleFrameDefinition>(b.definition());
        check((bv.upper_left - before.upper_left).length() < 1e-10 &&
                  (bv.lower_right - before.lower_right).length() < 1e-10,
              "Double reflection changed corners");
    });
    check(bytes(source) == original, "Rejected edit partially committed");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
