// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <libopencad/sheet_set.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace libopencad;

static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}

int main(int argc, char **argv) {
    try {
        check(argc == 2, "output prefix");
        std::string prefix = argv[1];

        auto encoded = sheet_set_encode(reinterpret_cast<const std::uint8_t *>("<?xml"), 5);
        check(encoded.size() == 5 && encoded[0] == 0xD0 && encoded[1] == 0xCD && encoded[2] == 0x14 &&
                  encoded[3] == 0xE3 && encoded[4] == 0xE0,
              "cipher");
        std::vector<std::uint8_t> all(256);
        for (int i = 0; i < 256; ++i)
            all[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
        auto round = sheet_set_decode(sheet_set_encode(all.data(), all.size()).data(), all.size());
        check(round == all, "cipher inverse");

        auto db = SheetSetDatabase::make("Demo Set", "A & B <c>");
        auto set = std::string(db.sheet_set().id());
        auto sub = db.add_subset(set, "Architectural", "Arch subset");
        check(sub.has_value(), "subset");
        auto sheet = db.add_sheet(*sub, "A-101", "Ground Floor Plan", "Sheet description");
        check(sheet.has_value(), "sheet");
        check(db.set_layout_reference(*sheet, "Layout",
                                      LayoutReference{"C:\\p\\sheet1.dwg", "Layout1", "6F"}),
              "layout");
        set_custom_property(db.sheet_set(), "Drawn By", "RS", kCustomSheetProp);
        auto back = SheetSetDatabase::from_bytes(db.to_bytes());
        auto s = back.sheet_for("c:/p/SHEET1.dwg", "Layout1");
        check(s, "sheet_for");
        check(back.sheet_value(*s, "Sheet", "NumberAndTitle") == std::optional<std::string>{"A-101 - Ground Floor Plan"},
              "number and title");
        check(back.sheet_value(*s, "Subset", "Name") == std::optional<std::string>{"Architectural"}, "subset name");
        check(back.sheet_value(*s, "Sheet", "Drawn By") == std::optional<std::string>{"RS"}, "custom");
        check(back.sheet_value(*s, "SheetSet", "Description") == std::optional<std::string>{"A & B <c>"},
              "description");
        std::vector<std::string> names;
        for (const auto &c : s->children)
            if (auto n = c.propname())
                names.emplace_back(*n);
        check(names == std::vector<std::string>({"Desc", "Layout", "Number", "SheetViews", "Title"}),
              "named child order");
        auto parsed = parse_sheet_field_code("\\AcSm.16.2 ?Sheet.Drawn By \\f \"%tc4\"");
        check(parsed && std::get<0>(*parsed) == "?Sheet" && std::get<1>(*parsed) == "Drawn By" &&
                  std::get<2>(*parsed) == "%tc4",
              "parse_code");

        auto bytes = db.to_bytes();
        std::ofstream dst(prefix + "-demo.dst", std::ios::binary);
        dst.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

        auto drawing = DxfDocument::create();
        SheetSetData data;
        data.layout_name = "Layout1";
        data.sheet_dwg_name = "sheet1.dwg";
        data.sheet_set_file_name = "C:\\p\\demo.dst";
        data.sheet_set_version = 2;
        data.update_count = 3;
        data.update_time = "2026-11-19";
        drawing.set_sheet_set_data(data);
        auto loaded = drawing.sheet_set_data();
        check(loaded && loaded->layout_name == "Layout1" && loaded->update_count == 3, "sheet set data");
        std::ofstream dxf(prefix + "-linked.dxf", std::ios::binary);
        drawing.write(dxf);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
