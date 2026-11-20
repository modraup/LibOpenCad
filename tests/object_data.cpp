// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dwg_create.hpp>
#include <libopencad/dwg_document.hpp>
#include <libopencad/dxf_document.hpp>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
using namespace libopencad;
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static std::string dxf_bytes(const DxfDocument &d, bool binary) {
    std::ostringstream o;
    d.write(o, binary);
    return o.str();
}
static std::string dwg_bytes(const DwgDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
static DxfDocument parse_dxf(const std::string &s) {
    std::istringstream i(s);
    return DxfDocument::read(i);
}
static DwgDocument parse_dwg(const std::string &s) {
    std::istringstream i(s);
    return DwgDocument::read(i);
}
static void write_file(const std::string &path, const std::string &bytes) {
    std::ofstream(path, std::ios::binary) << bytes;
}
static std::string hex(std::uint64_t v) {
    std::ostringstream o;
    o << std::hex << std::uppercase << v;
    return o.str();
}
static std::vector<std::uint8_t> hex_bytes(std::string_view text) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        auto n = std::stoul(std::string(text.substr(i, 2)), nullptr, 16);
        out.push_back(static_cast<std::uint8_t>(n));
    }
    return out;
}
static ObjectDataTable test_table() {
    return {"TEST",
            "",
            {{"VOLTAGE", ObjectDataType::real(), ""},
             {"CIRCUITS", ObjectDataType::integer(), ""},
             {"OWNER", ObjectDataType::character(), ""}}};
}
static std::vector<ObjectDataValue> test_values(std::string owner) {
    return {ObjectDataValue::from_real(230), ObjectDataValue::from_integer(2),
            ObjectDataValue::from_character(std::move(owner))};
}
static void check_tables(const std::vector<std::pair<std::uint64_t, ObjectDataTable>> &tables) {
    check(tables.size() == 1, "Object Data table count");
    check(tables[0].second == test_table(), "Object Data table fields");
}
static void check_records(const std::vector<ObjectDataAttachment> &records, std::uint64_t table,
                          const std::string &owner) {
    check(records.size() == 1, "Object Data record count");
    check(records[0].table == table, "Object Data record table");
    check(records[0].values == test_values(owner), "Object Data record values");
}
static std::string trim_line(std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    std::size_t i = 0;
    while (i < line.size() && line[i] == ' ')
        ++i;
    return line.substr(i);
}
static bool has_table(const std::vector<std::pair<std::uint64_t, ObjectDataTable>> &tables,
                      const ObjectDataTable &want) {
    for (const auto &item : tables)
        if (item.second == want)
            return true;
    return false;
}
static bool same_handle(std::string_view a, std::string_view b) {
    return std::stoull(std::string(a), nullptr, 16) == std::stoull(std::string(b), nullptr, 16);
}
static std::string drop_named_entry(const std::string &ascii, std::string_view key, std::string_view handle) {
    std::istringstream in(ascii);
    std::ostringstream out;
    std::string code, value;
    bool dropped = false;
    while (std::getline(in, code) && std::getline(in, value)) {
        if (!dropped && trim_line(code) == "3" && trim_line(value) == key) {
            std::string next_code, next_value;
            check(static_cast<bool>(std::getline(in, next_code) && std::getline(in, next_value)),
                  "truncated dictionary entry");
            auto group = trim_line(next_code);
            check(group == "350" || group == "360", "dictionary handle group");
            if (same_handle(trim_line(next_value), handle)) {
                dropped = true;
                continue;
            }
            out << code << '\n' << value << '\n' << next_code << '\n' << next_value << '\n';
            continue;
        }
        out << code << '\n' << value << '\n';
    }
    check(dropped, "missing dictionary key");
    return out.str();
}
static void stamp_unread_payload(DxfDocument &dxf, const std::vector<std::uint8_t> &payload) {
    dxf.for_each<DxfClassObject>([&](DxfClassObject &object) {
        auto data = object.definition();
        if (auto *descriptor = std::get_if<ObjectDataDescriptor>(&data)) {
            if (descriptor->as_table()) {
                descriptor->payload = payload;
                object.set_definition(*descriptor);
            }
        }
    });
}
static bool kept_unread_payload(const DxfDocument &dxf, const std::vector<std::uint8_t> &payload) {
    bool kept = false;
    dxf.for_each<DxfClassObject>([&](const DxfClassObject &object) {
        auto data = object.definition();
        if (auto *descriptor = std::get_if<ObjectDataDescriptor>(&data))
            kept = kept || descriptor->payload == payload;
    });
    return kept;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    std::string prefix = argv[1];
    check(object_data_next_key({}) == "*A1", "empty dictionary key");
    check(object_data_next_key({{"*A2", 1}, {"*A3", 2}}) == "*A4", "skipped dictionary key");
    const char *payloads[] = {
        "0115000043006C0061007300730000004900720064000000000049007200640000005400680065002000620061007300650020007300750070006500720063006C006100730073000000490072006400200043006C0061007300730020005600200031002E0030003200000000000000",
        "0117000043006C006100730073004E0075006D0065007200690063000000490072006400000043006C006100730073000000490072006400000054006800650020006200610073006500200063006C00610073007300200066006F007200200074006800650020006E0075006D00650072006900630073000000490072006400200043006C0061007300730020005600200031002E0030003200000000000000",
        "0116000043006C006100730073005200650061006C000000490072006400000043006C006100730073004E0075006D0065007200690063000000490072006400000054006800650020007200650061006C002000760061006C00750065000000490072006400200043006C0061007300730020005600200031002E0030003200000000000000",
        "0116000043006C006100730073004C006F006E0067000000490072006400000043006C006100730073004E0075006D0065007200690063000000490072006400000054006800650020006C006F006E0067002000760061006C00750065000000490072006400200043006C0061007300730020005600200031002E0030003200000000000000",
        "0116000043006C0061007300730043006800610072000000490072006400000043006C006100730073000000490072006400000054006800650020006300680061007200610063007400650072002000760061006C00750065000000490072006400200043006C0061007300730020005600200031002E0030003200000000000000",
    };
    auto built = object_data_built_in_descriptors();
    check(built.size() == 5, "built-in Object Data types");
    for (std::size_t i = 0; i < built.size(); ++i)
        check(built[i].payload == hex_bytes(payloads[i]), "built-in Object Data payload");
    ObjectDataDescriptor truncated;
    truncated.kind = ObjectDataDescriptor::kind_table;
    truncated.payload = {1, 2, 3};
    check(!truncated.as_table(), "truncated table payload");
    check(DxfDocument::create().object_data_tables().empty(), "empty Object Data tables");
    auto dxf = DxfDocument::create();
    auto line = dxf.add_line({0, 0, 0}, {10, 0, 0});
    auto table = dxf.add_object_data_table(test_table());
    auto record = dxf.attach_object_data(line, table, test_values("hello"));
    check_tables(dxf.object_data_tables());
    check(dxf.object_data_tables()[0].first == table, "table handle");
    check_records(dxf.object_data(line), table, "hello");
    check(dxf.object_data(line)[0].record == record, "record handle");
    check(!dxf.dictionary_entry(dxf.root_dictionary_handle(), object_data_dictionary_key).empty(),
          "NOD Object Data dictionary");
    auto xdic = dxf.extension_dictionary_handle(hex(line));
    check(dxf.dictionary_entry(xdic, "*A1") == hex(record), "first extension key");
    std::size_t records = 0;
    dxf.for_each<DxfClassObject>([&](const DxfClassObject &object) {
        if (object.type() == "IRD_DSC_RECORD")
            ++records;
    });
    check(records == 6, "built-in plus table descriptors");
    bool refused = false;
    try {
        dxf.add_object_data_table(test_table());
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "duplicate table accepted");
    refused = false;
    try {
        dxf.add_object_data_table({"test", "", test_table().fields});
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "case-insensitive duplicate table accepted");
    refused = false;
    try {
        dxf.attach_object_data(line, table, {ObjectDataValue::from_integer(1)});
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "mismatched values accepted");
    check(dxf.object_data(line).size() == 1, "failed attach mutated drawing");
    refused = false;
    try {
        dxf.attach_object_data(table, table, test_values("hello"));
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "object handle treated as entity");
    auto other = ObjectDataTable{"OTHER", "", {{"NAME", ObjectDataType::character(), ""}}};
    auto other_handle = dxf.add_object_data_table(other);
    dxf.attach_object_data(line, other_handle, {ObjectDataValue::from_character("x")});
    check(dxf.object_data_tables().size() == 2, "second table");
    check(dxf.object_data(line).size() == 2, "second record");
    check(dxf.dictionary_entry(xdic, "*A2").size(), "second extension key");
    auto ascii = dxf_bytes(dxf, false);
    auto binary = dxf_bytes(dxf, true);
    write_file(prefix + "-ascii.dxf", ascii);
    write_file(prefix + "-binary.dxf", binary);
    auto loaded = parse_dxf(ascii);
    check(loaded.object_data_tables().size() == 2, "round-trip table count");
    check(has_table(loaded.object_data_tables(), test_table()), "round-trip TEST table");
    auto loaded_line = loaded.model_space_entity_handles();
    check(!loaded_line.empty(), "round-trip line");
    auto loaded_handle = std::stoull(loaded_line.front(), nullptr, 16);
    check(loaded.object_data(loaded_handle).size() == 2, "round-trip records");
    check(parse_dxf(binary).object_data_tables().size() == 2, "binary tables");
    auto reused = DxfDocument::create();
    auto reused_line = reused.add_line({0, 0, 0}, {10, 0, 0});
    auto reused_table = reused.add_object_data_table(test_table());
    reused.attach_object_data(reused_line, reused_table, test_values("hello"));
    reused.attach_object_data(reused_line, reused_table, test_values("second"));
    auto reused_xdic = reused.extension_dictionary_handle(hex(reused_line));
    auto first_record = reused.dictionary_entry(reused_xdic, "*A1");
    check(first_record.size() && reused.dictionary_entry(reused_xdic, "*A2").size(), "two extension keys");
    auto dropped = parse_dxf(drop_named_entry(dxf_bytes(reused, false), "*A1", first_record));
    auto dropped_line = std::stoull(dropped.model_space_entity_handles().front(), nullptr, 16);
    auto dropped_table = dropped.object_data_tables()[0].first;
    auto dropped_xdic = dropped.extension_dictionary_handle(hex(dropped_line));
    dropped.attach_object_data(dropped_line, dropped_table, test_values("third"));
    check(dropped.dictionary_entry(dropped_xdic, "*A1").empty(), "deleted key reused");
    check(dropped.dictionary_entry(dropped_xdic, "*A2").size() && dropped.dictionary_entry(dropped_xdic, "*A3").size(),
          "next key after deleted entry");
    std::vector<std::uint8_t> unread_payload{0x00, 0x30, 0x00, 0x00, 0xff, 0xff};
    auto unread = DxfDocument::create();
    auto unread_line = unread.add_line({0, 0, 0}, {10, 0, 0});
    unread.add_object_data_table(test_table());
    unread.attach_object_data(unread_line, unread.object_data_tables()[0].first, test_values("hello"));
    stamp_unread_payload(unread, unread_payload);
    check(kept_unread_payload(parse_dxf(dxf_bytes(unread, false)), unread_payload), "unread DXF payload");
    check(kept_unread_payload(parse_dxf(dxf_bytes(unread, true)), unread_payload), "unread binary payload");
    auto long_owner = std::string("Hé,") + std::string(351, 'l') + "o\"'";
    auto long_dxf = DxfDocument::create();
    auto long_line = long_dxf.add_line({0, 0, 0}, {10, 0, 0});
    auto long_table = long_dxf.add_object_data_table(test_table());
    long_dxf.attach_object_data(long_line, long_table, test_values(long_owner));
    auto long_ascii = dxf_bytes(long_dxf, false);
    write_file(prefix + "-long-ascii.dxf", long_ascii);
    std::vector<std::string> chunks;
    {
        std::istringstream in(long_ascii);
        std::string line_text, value;
        while (std::getline(in, line_text) && std::getline(in, value)) {
            if (trim_line(line_text) == "1004") {
                auto chunk = trim_line(value);
                check(chunk.size() <= 254, "Object Data DXF chunk");
                chunks.push_back(std::move(chunk));
            }
        }
    }
    check(chunks.size() > 2, "long Object Data spans DXF chunks");
    auto long_loaded = parse_dxf(long_ascii);
    auto long_entities = long_loaded.model_space_entity_handles();
    check(long_loaded.object_data(std::stoull(long_entities.front(), nullptr, 16))[0].values ==
              test_values(long_owner),
          "long Object Data values");
    DxfConversionOptions allow;
    allow.allow_loss = true;
    for (auto version : {DxfVersion::AC1018, DxfVersion::AC1024, DxfVersion::AC1027, DxfVersion::AC1032}) {
        for (bool bin : {false, true}) {
            auto exported = dxf.export_version(version, bin, allow);
            check(exported.report.can_write, "Object Data conversion");
            auto converted = parse_dxf(exported.bytes);
            check(converted.object_data_tables().size() == 2, "converted tables");
            auto tag = std::string("-") + std::string(dxf_version_string(version)) +
                       (bin ? "-binary.dxf" : "-ascii.dxf");
            write_file(prefix + tag, exported.bytes);
        }
    }
    std::string source = __FILE__;
    auto slash = source.find_last_of('/');
    std::ifstream legacy_in(source.substr(0, slash) + "/data/modeler/AC1015-ascii.dxf", std::ios::binary);
    auto old = DxfDocument::read(legacy_in);
    refused = false;
    try {
        old.add_object_data_table(test_table());
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "AC1015 Object Data construction accepted");
    R12ExportOptions r12;
    r12.allow_loss = true;
    auto r12_dropped = dxf.export_r12(r12);
    check(r12_dropped.report.can_write && r12_dropped.report.removed_objects > 0, "R12 drops Object Data");
    std::istringstream dwg_in(create_dwg(DxfVersion::AC1032, {{DwgLineGeometry{{0, 0, 0}, {10, 0, 0}}}}));
    auto dwg = DwgDocument::read(dwg_in);
    auto dwg_line = dwg.objects().lines.begin()->first;
    auto dwg_table = dwg.add_object_data_table(test_table());
    dwg.attach_object_data(dwg_line, dwg_table, test_values("hello"));
    check_tables(dwg.object_data_tables());
    check_records(dwg.object_data(dwg_line), dwg_table, "hello");
    auto dwg_other = dwg.add_object_data_table(other);
    dwg.attach_object_data(dwg_line, dwg_other, {ObjectDataValue::from_character("x")});
    check(dwg.object_data(dwg_line).size() == 2, "DWG second record");
    refused = false;
    try {
        dwg.add_object_data_table(test_table());
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "DWG duplicate table accepted");
    auto saved = dwg_bytes(dwg);
    write_file(prefix + ".dwg", saved);
    auto restored = parse_dwg(saved);
    check(restored.object_data_tables().size() == 2, "DWG round-trip tables");
    auto restored_line = restored.objects().lines.begin()->first;
    check(restored.object_data(restored_line).size() == 2, "DWG round-trip records");
    std::istringstream ac1015_in(create_dwg(DxfVersion::AC1015, {{DwgLineGeometry{{0, 0, 0}, {10, 0, 0}}}}));
    auto ac1015 = DwgDocument::read(ac1015_in);
    refused = false;
    try {
        ac1015.add_object_data_table(test_table());
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "AC1015 DWG Object Data construction accepted");
    std::istringstream ac1018_in(create_dwg(DxfVersion::AC1018, {{DwgLineGeometry{{0, 0, 0}, {10, 0, 0}}}}));
    auto ac1018 = DwgDocument::read(ac1018_in);
    auto ac1018_table = ac1018.add_object_data_table(test_table());
    ac1018.attach_object_data(ac1018.objects().lines.begin()->first, ac1018_table, test_values("hello"));
    check_tables(ac1018.object_data_tables());
    auto long_dwg_in = create_dwg(DxfVersion::AC1032, {{DwgLineGeometry{{0, 0, 0}, {10, 0, 0}}}});
    auto long_dwg = parse_dwg(long_dwg_in);
    auto long_dwg_table = long_dwg.add_object_data_table(test_table());
    long_dwg.attach_object_data(long_dwg.objects().lines.begin()->first, long_dwg_table, test_values(long_owner));
    auto long_restored = parse_dwg(dwg_bytes(long_dwg));
    check(long_restored.object_data(long_restored.objects().lines.begin()->first)[0].values ==
              test_values(long_owner),
          "long DWG Object Data values");
    auto unread_dwg = parse_dwg(create_dwg(DxfVersion::AC1032, {{DwgLineGeometry{{0, 0, 0}, {10, 0, 0}}}}));
    auto unread_dwg_table = unread_dwg.add_object_data_table(test_table());
    unread_dwg.attach_object_data(unread_dwg.objects().lines.begin()->first, unread_dwg_table, test_values("hello"));
    std::optional<ClassObject> unread_desc;
    for (const auto &[handle, object] : unread_dwg.objects().class_objects) {
        (void)handle;
        if (auto *descriptor = std::get_if<ObjectDataDescriptor>(&object.data))
            if (descriptor->as_table()) {
                unread_desc = object;
                break;
            }
    }
    check(unread_desc.has_value(), "DWG table descriptor");
    std::get<ObjectDataDescriptor>(unread_desc->data).payload = unread_payload;
    unread_dwg.update_class_object(*unread_desc);
    auto unread_dwg_restored = parse_dwg(dwg_bytes(unread_dwg));
    bool unread_dwg_kept = false;
    for (const auto &[handle, object] : unread_dwg_restored.objects().class_objects) {
        (void)handle;
        if (auto *descriptor = std::get_if<ObjectDataDescriptor>(&object.data))
            unread_dwg_kept = unread_dwg_kept || descriptor->payload == unread_payload;
    }
    check(unread_dwg_kept, "unread DWG payload");
    std::cout << "Object Data graph APIs verified\n";
    return 0;
}