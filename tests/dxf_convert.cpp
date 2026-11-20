// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
namespace {
const std::vector<DxfVersion> versions{DxfVersion::AC1012, DxfVersion::AC1014, DxfVersion::AC1015,
                                       DxfVersion::AC1018, DxfVersion::AC1021, DxfVersion::AC1024,
                                       DxfVersion::AC1027, DxfVersion::AC1032};
void bytes(const std::string &path, const std::string &value) {
    std::ofstream out(path, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << value;
}
bool issue(const DxfConversionReport &report, const std::string &code) {
    return std::any_of(report.issues.begin(), report.issues.end(),
                       [&](const auto &i) { return i.code == code; });
}
DxfDocument fixture(bool binary) {
    auto d = DxfDocument::create(binary, 4);
    d.add_layer("中文图层", 2);
    LineTypeDefinition lt;
    lt.name = "中文线型";
    lt.pattern_length = 3;
    LineTypeComplexData complex;
    complex.content = LineTypeComplexData::Content::Text;
    complex.text = "文字";
    complex.style_handle = std::stoull(d.get_symbol(SymbolTable::TextStyle, "Standard")->handle, nullptr, 16);
    complex.upright = true;
    lt.elements = {{2, complex}, {-1, std::nullopt}};
    d.add_linetype(lt);
    EntityOptions options;
    options.layer = "中文图层";
    options.color = Color::from_rgb(19, 145, 211);
    const auto line = hex(d.add_line({1, 2, 3}, {4, 5, 6}, options));
    d.add_circle({7, 8, 9}, 2.5);
    options.space = EntitySpace::Paper;
    options.color = Color::by_block();
    d.add_text("中文版本转换", {2, 3, 4}, 2.5, options);
    MTextDefinition m;
    m.text = "中文\\P第二行";
    m.height = 2;
    m.rectangle_width = 25;
    d.add_mtext(m);
    d.add_symbol(SymbolTable::BlockRecord, "UnitBlock");
    options = {};
    options.block_name = "UnitBlock";
    AttributeDefinition a;
    a.tag = "TAG";
    a.text.text = "属性";
    a.text.height = 1;
    a.lock_position = true;
    d.add_attribute_definition(a, options);
    InsertDefinition insert;
    insert.block_name = "UnitBlock";
    d.add_insert(insert, {a});
    d.for_each<DxfLine>([&](auto &e) {
        EntityDisplayProperties p;
        p.transparency = Transparency::explicit_alpha(127);
        p.color_name = "Book$Shade";
        p.plot_style = 0x30;
        p.plot_style_mode = 3;
        e.set_display_properties(p);
        e.set_graphic_data(std::vector<std::uint8_t>(270, 0x5a));
    });
    const auto dict = hex(d.add_extension_dictionary(line));
    d.add_xrecord(dict, "Literal", {{90, "123"}, {440, "33554560"}, {1, "中文数据"}});
    auto data = records(save(d), binary);
    const auto arrow = d.get_symbol(SymbolTable::BlockRecord, "UnitBlock")->handle;
    for (auto &r : data) {
        if (r.front().second == "DIMSTYLE")
            r.push_back({342, arrow});
        if (r.front().second == "SECTION" && r.size() > 1 && r[1].second == "HEADER") {
            for (auto &p : r)
                if (p.first == 3 && p.second == "ANSI_1252")
                    p.second = "ANSI_936";
            Record info{{9, "$TITLE"}, {1, "中文标题"}};
            r.insert(r.end(), info.begin(), info.end());
        }
        if (r.front().second == "BLOCK_RECORD" &&
            std::find(r.begin(), r.end(), Pair{2, "UnitBlock"}) != r.end()) {
            bool found = false;
            for (auto &p : r)
                if (p.first == 70) {
                    p.second = binary ? detail::binary_short(4) : "4";
                    found = true;
                }
            if (!found)
                r.push_back({70, binary ? detail::binary_short(4) : "4"});
        }
    }
    return read(encode(data, binary));
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 3, "format prefix");
        const bool binary = std::string(argv[1]) == "binary";
        const std::string prefix = argv[2];
        auto source = fixture(binary);
        const auto original = save(source);
        write(source, prefix + "-source.dxf");
        DxfConversionOptions allow;
        allow.allow_loss = true;
        for (auto target : versions) {
            const auto name = std::string(dxf_version_string(target));
            const auto strict = source.export_version(target, binary);
            const bool loses =
                std::any_of(strict.report.issues.begin(), strict.report.issues.end(),
                            [](const auto &i) { return i.kind == DxfConversionIssueKind::Loss; });
            if (loses)
                check(!strict.report.can_write && strict.bytes.empty(), "strict downgrade loss refusal");
            if (target < DxfVersion::AC1018)
                check(loses, "RGB downgrade reports loss");
            auto result = source.export_version(target, binary, allow);
            if (!result.report.can_write)
                for (const auto &i : result.report.issues)
                    std::cerr << i.code << ": " << i.message << '\n';
            check(result.report.can_write && result.report.effective == target, "explicit exact target");
            auto got = read(result.bytes);
            check(got.version() == target && got.is_binary() == binary, "target header and transport");
            got.for_each<DxfLine>([&](const auto &e) {
                check(e.start() == Point3{1, 2, 3} && e.end() == Point3{4, 5, 6}, "geometry retained");
                check(e.color() == (target < DxfVersion::AC1018
                                        ? Color::from_index(Color::from_rgb(19, 145, 211).approximate_index())
                                        : Color::from_rgb(19, 145, 211)),
                      "target color");
                check(e.graphic_data() ==
                          std::optional<std::vector<std::uint8_t>>(std::vector<std::uint8_t>(270, 0x5a)),
                      "graphic bytes retained");
                check(e.display_properties().transparency == (target < DxfVersion::AC1018
                                                                  ? Transparency::by_layer()
                                                                  : Transparency::explicit_alpha(127)),
                      "target transparency");
            });
            got.for_each<DxfText>(
                [](const auto &e) { check(e.text() == "中文版本转换", "text transcoding"); });
            check(got.model_space_entity_handles() == source.model_space_entity_handles() &&
                      got.paper_space_entity_handles() == source.paper_space_entity_handles(),
                  "ownership and order");
            check(got.reference_issues().empty(), "converted graph integrity");
            bytes(prefix + "-" + name + ".dxf", result.bytes);
            // Every supported source generation upgrades back to R2018, without
            // inventing the color/transparency/lock data discarded on downgrade.
            auto back = got.export_version(DxfVersion::AC1032, binary, allow);
            check(back.report.can_write && read(back.bytes).version() == DxfVersion::AC1032,
                  "legacy to modern conversion");
            bytes(prefix + "-" + name + "-up.dxf", back.bytes);
            check(save(source) == original, "source unchanged");
            if (target < DxfVersion::AC1021)
                check(result.bytes.find(std::string("\xD6\xD0\xCE\xC4")) != std::string::npos,
                      "GBK output bytes");
        }
        auto policy = allow;
        policy.policy = DxfOutputPolicy::UpstreamCompatible;
        const auto promoted = source.export_version(DxfVersion::AC1012, true, policy);
        check(promoted.report.can_write && promoted.report.effective == DxfVersion::AC1024 &&
                  issue(promoted.report, "target-promoted"),
              "explicitly reported Rust binary promotion");
        bytes(prefix + "-promoted.dxf", promoted.bytes);
        std::ostringstream output;
        output << "untouched";
        bool refused = false;
        try {
            source.write_version(output, DxfVersion::AC1012, binary);
        } catch (const DxfConversionError &e) {
            refused = !e.report().can_write;
        }
        check(refused && output.str() == "untouched", "refusal is atomic");
        auto small = allow;
        small.max_output_bytes = 32;
        check(!source.export_version(DxfVersion::AC1032, binary, small).report.can_write, "bounded output");
        check(!source.export_version(DxfVersion::AC1012, binary, small).report.can_write,
              "bounded converted output");
        // Malformed and unregistered units packets must refuse instead of indexing
        // beyond their bounds or producing dangling APPID references.
        const auto unit = source.get_symbol(SymbolTable::BlockRecord, "UnitBlock")->handle;
        auto malformed = records(original, binary);
        at(malformed, unit).insert(at(malformed, unit).end(), {{1001, "ACAD"}, {1000, "DesignCenter Data"}});
        check(issue(read(encode(malformed, binary)).export_version(DxfVersion::AC1018, binary, allow).report,
                    "block-units-xdata-invalid"),
              "short DesignCenter packet refused");
        auto missing = records(original, binary);
        drop(missing, {source.get_symbol(SymbolTable::AppId, "ACAD")->handle});
        check(issue(read(encode(missing, binary)).export_version(DxfVersion::AC1018, binary, allow).report,
                    "block-units-appid-missing"),
              "unregistered units packet refused");
        auto stale = records(original, binary);
        auto &br = at(stale, unit);
        for (auto &p : br)
            if (p.first == 70)
                p.second = binary ? detail::binary_short(0) : "0";
        Record packet{{1001, "ACAD"}, {1000, "DesignCenter Data"}, {1002, "{"}, {1070, "1"}, {1070, "4"},
                      {1002, "}"}};
        if (binary)
            for (auto &p : packet)
                p.second = detail::binary_value_from_text(p.first, p.second);
        br.insert(br.end(), packet.begin(), packet.end());
        auto zero = read(encode(stale, binary)).export_version(DxfVersion::AC1018, binary, allow);
        check(zero.report.can_write, "zero units overwrite stale XDATA");
        for (const auto &app : read(zero.bytes).block_record(unit)->extended_data())
            if (app.application_name == "ACAD")
                check(std::get<std::int16_t>(app.values[3].value) == 0, "stale units cleared");
        // Private group numbers are not common entity properties.
        auto private_data = records(original, binary);
        const auto line_handle = source.model_space_entity_handles().front();
        auto &record = at(private_data, line_handle);
        Record private_group{{102, "{PRIVATE"}, {420, "123456"}, {440, "33554560"}, {102, "}"}};
        if (binary)
            for (auto &p : private_group)
                p.second = detail::binary_value_from_text(p.first, p.second);
        record.insert(record.end(), private_group.begin(), private_group.end());
        auto private_result =
            read(encode(private_data, binary)).export_version(DxfVersion::AC1012, binary, allow);
        check(private_result.report.can_write, "private entity group retained");
        // Inspect the ASCII form even when testing binary input/output.
        std::ostringstream private_ascii;
        read(private_result.bytes).write(private_ascii, false);
        const auto private_records = records(private_ascii.str(), false);
        auto private_line = std::find_if(private_records.begin(), private_records.end(),
                                         [&](const auto &r) { return identity(r) == line_handle; });
        check(private_line != private_records.end() && std::find(private_line->begin(), private_line->end(),
                                                                 Pair{420, "123456"}) != private_line->end(),
              "private RGB value retained");
        check(!source.export_version(DxfVersion::AC1009, binary, allow).report.can_write,
              "R12 separate path");
        check(!source.export_version(static_cast<DxfVersion>(1016), binary, allow).report.can_write,
              "unknown target refused");
        auto lw = DxfDocument::create(binary);
        lw.add_lwpolyline({{0, 0, .25, 1, 2, 41}, {10, 0, 0, 2, 3, 42}, {10, 10, -.5, 3, 1, 43}}, true);
        write(lw, prefix + "-lw-source.dxf");
        for (auto v : versions) {
            auto converted = lw.export_version(v, binary, allow);
            check(converted.report.can_write, "LWP target conversion");
            if (v == DxfVersion::AC1012) {
                auto expanded = read(converted.bytes);
                std::size_t count = 0;
                expanded.for_each<DxfPolyline2D>([&](const auto &p) {
                    check(p.vertex_count() == 3 && p.is_closed() && p.vertex(2).bulge == -.5,
                          "R13 expanded closing bulge");
                    ++count;
                });
                check(count == 1 && issue(converted.report, "lwpolyline-to-polyline"),
                      "R13 lightweight expansion");
            }
            bytes(prefix + "-lw-" + std::string(dxf_version_string(v)) + ".dxf", converted.bytes);
        }
        auto columns = DxfDocument::create(binary);
        MTextDefinition m;
        m.text = "列一\\P列二";
        m.height = 2;
        m.rectangle_width = 20;
        m.columns.type = 2;
        m.columns.count = 2;
        m.columns.width = 10;
        m.columns.gutter = 1;
        m.columns.heights = {12, 14};
        columns.add_mtext(m);
        write(columns, prefix + "-columns-source.dxf");
        for (auto v : versions) {
            auto result = columns.export_version(v, binary, allow);
            if (v < DxfVersion::AC1021)
                check(result.report.can_write && issue(result.report, "mtext-columns-decomposed"),
                      "column decomposition reports approximate layout");
            else {
                check(result.report.can_write, "MTEXT column schema conversion");
                bytes(prefix + "-columns-" + std::string(dxf_version_string(v)) + ".dxf", result.bytes);
            }
        }
        auto unregistered_columns = records(save(columns), binary);
        drop(unregistered_columns, {columns.get_symbol(SymbolTable::AppId, "ACAD")->handle});
        check(issue(read(encode(unregistered_columns, binary))
                        .export_version(DxfVersion::AC1021, binary, allow)
                        .report,
                    "mtext-columns-appid-missing"),
              "unregistered column XDATA refused");
        auto multiline = DxfDocument::create(binary);
        AttributeDefinition attr;
        attr.tag = "MULTI";
        attr.text.text = "多行";
        attr.text.height = 2;
        attr.multiline = true;
        MTextDefinition embedded;
        embedded.text = "多行\\P属性";
        embedded.height = 2;
        attr.embedded_mtext = embedded;
        multiline.add_attribute_definition(attr);
        check(issue(multiline.export_version(DxfVersion::AC1014, binary, allow).report,
                    "multiline-attribute-downgrade"),
              "multiline schema refusal");
        auto unknown = records(original, binary);
        append(unknown, {{0, "PRIVATE_OBJECT"}, {5, "F001"}, {330, "C"}, {1, "opaque"}}, binary);
        auto opaque = read(encode(unknown, binary));
        check(!opaque.export_version(DxfVersion::AC1018, binary, allow).report.can_write,
              "unreviewed schema refuses");
        check(opaque.export_version(DxfVersion::AC1032, binary).report.can_write,
              "same-version opaque preservation");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
