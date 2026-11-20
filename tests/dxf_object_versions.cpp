// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <libopencad/dxf_convert.hpp>
int main(int argc, char **argv) try {
    check(argc == 4, "format fixture-dir prefix");
    const bool binary = std::string(argv[1]) == "binary";
    const std::string dir = argv[2], prefix = argv[3], format = argv[1];
    DxfConversionOptions allow;
    allow.allow_loss = true;
    for (auto name : {"proxies", "fields", "table-content", "tables", "table-geometry"}) {
        std::ifstream in(dir + "/" + name + "-" + format + ".dxf", std::ios::binary);
        auto d = DxfDocument::read(in);
        const auto before = save(d);
        write(d, prefix + "-" + name + "-source.dxf");
        for (auto v : {DxfVersion::AC1021, DxfVersion::AC1024, DxfVersion::AC1027, DxfVersion::AC1032}) {
            auto result = d.export_version(v, binary, allow);
            if (!result.report.can_write)
                for (auto &i : result.report.issues)
                    std::cerr << name << " " << i.record_type << " " << i.code << " " << i.message << '\n';
            check(result.report.can_write, "modern object conversion");
            auto out = read(result.bytes);
            check(out.duplicate_handles().empty(), "object identities unique");
            std::ofstream file(prefix + "-" + name + "-" + std::string(dxf_version_string(v)) + ".dxf",
                               std::ios::binary);
            file << result.bytes;
        }
        check(before == save(d), "source unchanged");
        if (std::string(name) == "proxies")
            check(!d.export_version(DxfVersion::AC1018, binary, allow).report.can_write,
                  "unsafe proxy rejected");
        else
            for (auto v : {DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015, DxfVersion::AC1018}) {
                auto result = d.export_version(v, binary, allow);
                if (!result.report.can_write)
                    for (auto &i : result.report.issues)
                        if (i.kind == DxfConversionIssueKind::Error)
                            std::cerr << name << " " << i.record_type << " " << i.message << '\n';
                check(result.report.can_write, "legacy table/FIELD conversion");
                auto saved = read(result.bytes);
                check(saved.version() == v && saved.is_binary() == binary,
                      "exact legacy version and transport");
                saved.for_each<DxfField>([](const auto &field) {
                    auto f = field.definition();
                    check(f.child_values.size() == 3 && f.child_values[1].value.point.z == 3 &&
                              f.value.text == "字段值" && f.value_string == "显示文字",
                          "C++ reads unframed FIELD values");
                });
                if (std::string(name) != "table-geometry")
                    check(std::any_of(result.report.issues.begin(), result.report.issues.end(),
                                      [](const auto &i) {
                                          return i.code == "legacy-table-field-loss" &&
                                                 !i.source_handle.empty();
                                      }),
                          "identified loss");
                check(d.export_version(v, binary).bytes.empty(), "strict refusal");
                auto upgraded = saved.export_version(DxfVersion::AC1032, binary, allow);
                if (!upgraded.report.can_write)
                    for (auto &issue : upgraded.report.issues)
                        if (issue.kind == DxfConversionIssueKind::Error)
                            std::cerr << name << " " << issue.message << '\n';
                check(upgraded.report.can_write, "promote legacy tables");
                std::ofstream(prefix + "-" + name + "-" + std::string(dxf_version_string(v)) + "-up.dxf",
                              std::ios::binary)
                    << upgraded.bytes;
                std::ofstream out(prefix + "-" + name + "-" + std::string(dxf_version_string(v)) + ".dxf",
                                  std::ios::binary);
                out << result.bytes;
            }
        check(before == save(d), "legacy export leaves source unchanged");
    }
    for (auto name : {"class-versions", "drawing-objects", "registered-classes", "data-objects",
                      "object-contexts", "extended-entities", "more-extended", "multileaders"}) {
        std::ifstream input(dir + "/" + name + "-" + format + ".dxf", std::ios::binary);
        auto doc = DxfDocument::read(input);
        write(doc, prefix + "-" + name + "-source.dxf");
        for (auto v : {DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015, DxfVersion::AC1018,
                       DxfVersion::AC1021, DxfVersion::AC1024, DxfVersion::AC1027, DxfVersion::AC1032}) {
            auto result = doc.export_version(v, binary, allow);
            if (!result.report.can_write)
                for (auto &issue : result.report.issues)
                    if (issue.kind == DxfConversionIssueKind::Error)
                        std::cerr << name << " " << issue.record_type << " " << issue.message << '\n';
            check(result.report.can_write, "registered and drawing object conversion");
            auto path = prefix + "-" + name + "-" + std::string(dxf_version_string(v));
            std::ofstream(path + ".dxf", std::ios::binary) << result.bytes;
            if (v == DxfVersion::AC1018 || v == DxfVersion::AC1024) {
                auto upgraded = read(result.bytes).export_version(DxfVersion::AC1032, binary, allow);
                check(upgraded.report.can_write, "promote legacy registered objects");
                std::ofstream(path + "-up.dxf", std::ios::binary) << upgraded.bytes;
            }
        }
    }
    auto opaque = DxfDocument::create(binary);
    FieldDefinition private_value;
    private_value.value.type = 4;
    private_value.value.text = "retained";
    private_value.value.extra_fields = {{102, "{PRIVATE"}, {90, "222"}, {102, "}"}};
    auto private_handle = hex(opaque.add_field(opaque.root_dictionary_handle(), "Private", private_value));
    auto refusal = opaque.export_version(DxfVersion::AC1018, binary, allow);
    check(refusal.bytes.empty() && !refusal.report.can_write &&
              std::any_of(refusal.report.issues.begin(), refusal.report.issues.end(),
                          [&](const auto &i) {
                              return i.kind == DxfConversionIssueKind::Error &&
                                     i.source_handle == private_handle &&
                                     i.code == "legacy-table-field-refused";
                          }),
          "private ACVALUE refuses even with loss permission");
    auto simple = DxfDocument::create(binary);
    ProxyDefinition p;
    p.class_id = 500;
    p.dwg_version = 32;
    p.maintenance_version = 106;
    p.payload = {10, {0xab, 0xc0}};
    auto id = hex(simple.add_proxy_entity(p));
    p.proxy_id = 499;
    simple.add_proxy_object(simple.root_dictionary_handle(), "proxy", p);
    write(simple, prefix + "-proxy-old-source.dxf");
    for (auto v : {DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015, DxfVersion::AC1018}) {
        auto result = simple.export_version(v, binary, allow);
        check(result.report.can_write, "old proxy envelopes");
        auto out = read(result.bytes);
        out.for_each<DxfProxyEntity>([&](const auto &e) {
            auto value = e.definition();
            check(e.handle() == id && value.payload.data == p.payload.data && value.payload.bit_count == 10 &&
                      value.dwg_version == 32 && value.maintenance_version == 106,
                  "proxy payload preserved");
        });
        std::ofstream file(prefix + "-proxy-old-" + std::string(dxf_version_string(v)) + ".dxf",
                           std::ios::binary);
        file << result.bytes;
    }
    simple.for_each<DxfProxyEntity>([](auto &e) {
        auto p = e.definition();
        p.maintenance_version = 70000;
        e.set_definition(p);
    });
    check(!simple.export_version(DxfVersion::AC1027, binary, allow).report.can_write,
          "packed proxy version overflow");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
