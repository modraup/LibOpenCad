// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <limits>
namespace {
ExtendedDataRecord payload(std::uint64_t self) {
    return {"中文应用",
            {{1000, std::string("中文数据")},
             {1002, std::string("{")},
             {1003, std::string("0")},
             {1004, std::vector<std::uint8_t>{0, 1, 127, 128, 255}},
             {1005, self},
             {1010, Point3{1, 2, 3}},
             {1011, Point3{4, 5, 6}},
             {1012, Point3{-1, -2, -3}},
             {1013, Point3{0, 0, 1}},
             {1040, 1.25},
             {1041, 2.5},
             {1042, .5},
             {1070, std::int16_t{-32768}},
             {1071, std::int32_t{-2147483647}},
             {1002, std::string("}")}}};
}
void verify(const DxfRecord &e) {
    auto all = e.extended_data();
    auto it = std::find_if(all.begin(), all.end(), [](auto &d) { return d.application_name == "中文应用"; });
    check(it != all.end(), "Chinese app");
    auto expected = payload(std::stoull(e.handle(), nullptr, 16));
    check(it->values.size() == expected.values.size(), "XDATA value count");
    for (std::size_t i = 0; i < it->values.size(); ++i)
        check(it->values[i].code == expected.values[i].code &&
                  it->values[i].value == expected.values[i].value,
              "typed XDATA value");
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "XDATA atomic failure");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        std::ifstream input(argv[2], std::ios::binary);
        auto d = DxfDocument::read(input);
        std::vector<std::string> handles;
        d.for_each<DxfEntity>([&](auto &e) {
            verify(e);
            handles.push_back(e.handle());
        });
        check(handles.size() == 2, "entity count");
        auto before = records(save(d), binary);
        auto target = handles[0], neighbor = handles[1];
        auto data = payload(std::stoull(target, nullptr, 16));
        data.values[0].value = std::string("修改后的中文");
        d.set_extended_data(target, data);
        d.set_extended_data(target, {"Extra", {{1071, std::int32_t{2147483647}}}});
        check(d.get_symbol(SymbolTable::AppId, "extra").has_value(), "automatic APPID registration");
        auto after = records(save(d), binary);
        check(at(before, neighbor) == at(after, neighbor), "neighbor unchanged");
        write(d, prefix + "-edited.dxf");
        auto fresh = hex(d.copy_entity(target, EntitySpace::Paper));
        auto copied = d.get_record(fresh)->extended_data();
        check(std::get<std::uint64_t>(copied[0].values[4].value) == std::stoull(fresh, nullptr, 16),
              "embedded MTEXT XDATA self reference remapped");
        write(d, prefix + "-copied.dxf");
        check(d.remove_extended_data(fresh, "中文应用") && !d.remove_extended_data(fresh, "中文应用"),
              "remove one application");
        check(d.get_record(fresh)->extended_data().size() == 1, "retain unrelated app");
        check(d.get_record(target)->extended_data().size() == 2, "source XDATA independent");
        write(d, prefix + "-removed.dxf");
        check(d.remove_entity(fresh), "remove copy");
        write(d, prefix + "-deleted.dxf");
        fails(d, [&] {
            auto bad = data;
            bad.values.push_back({1070, std::int32_t{1}});
            d.set_extended_data(target, bad);
        });
        fails(d, [&] {
            auto bad = data;
            bad.values.push_back({1040, std::numeric_limits<double>::infinity()});
            d.set_extended_data(target, bad);
        });
        fails(d, [&] {
            auto bad = data;
            bad.values.push_back({1004, std::vector<std::uint8_t>(256)});
            d.set_extended_data(target, bad);
        });
        fails(d, [&] { d.set_extended_data("FFFFFFFF", data); });
        auto dict = d.add_extension_dictionary(target);
        auto xr = d.add_xrecord(hex(dict), "raw", {{1001, "PAYLOAD"}, {1000, "not XDATA"}});
        check(d.get_record(hex(xr))->extended_data().empty(), "XRECORD payload excluded");
        fails(d, [&] { d.set_extended_data(hex(xr), data); });
        // Unknown application groups must not turn a nested 1001 into editable XDATA.
        auto raw = records(save(d), binary);
        auto &record = at(raw, neighbor);
        record.insert(record.begin() + 2, {{102, "{PRIVATE"}, {1001, "fake"}, {1005, neighbor}, {102, "}"}});
        auto protected_doc = read(encode(raw, binary));
        verify(*protected_doc.get_record(neighbor));
        fails(protected_doc, [&] { protected_doc.copy_entity(neighbor, EntitySpace::Paper); });
        auto created = DxfDocument::create(binary);
        MTextDefinition mt;
        mt.text = "双栏文字";
        mt.columns.type = 2;
        mt.columns.count = 2;
        mt.columns.width = 8;
        mt.columns.gutter = 2;
        mt.columns.heights = {5, 6};
        auto h = created.add_mtext(mt);
        created.set_extended_data(hex(h), payload(h));
        verify(*created.get_record(hex(h)));
        write(created, prefix + "-created.dxf");
        std::ostringstream converted;
        created.write(converted, !binary);
        auto cross = read(converted.str());
        verify(*cross.get_record(hex(h)));
        write(cross, prefix + "-converted.dxf");
        created.rename_symbol(SymbolTable::AppId, "中文应用", "改名应用");
        check(created.get_record(hex(h))->extended_data()[0].application_name == "改名应用",
              "rename embedded XDATA app reference");
        fails(created, [&] { created.remove_symbol(SymbolTable::AppId, "改名应用"); });
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
