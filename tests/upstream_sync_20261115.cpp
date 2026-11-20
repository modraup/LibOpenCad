// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cstring>
using namespace libopencad::acis;
static void output(const std::string &name, const DxfDocument &d, bool binary) {
    std::ofstream f(name, std::ios::binary);
    d.write(f, binary);
}
static SabToken real(double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, 8);
    SabToken t{6, {}};
    for (int i = 0; i < 8; ++i)
        t.data.push_back(std::uint8_t(bits >> (8 * i)));
    return t;
}
int main(int argc, char **argv) try {
    if (argc != 2)
        return 2;
    std::string prefix = argv[1];
    auto base = DxfDocument::create();
    auto line = base.add_line({1, 2, 3}, {4, 5, 6});
    HatchDefinition hatch;
    hatch.paths.push_back({2, {}, {}});
    base.add_hatch(hatch);
    auto control = base.add_modeler_entity(ModelerKind::Solid3D, build_box({1, 2, 3}, 4, 5, 6));
    auto raw = records(save(base), false);
    unsigned users = 0;
    for (auto &r : raw)
        if (r.front().second == "SECTION")
            for (std::size_t i = 0; i + 1 < r.size(); ++i) {
                if (r[i].first == 9 && r[i].second.rfind("$USERI", 0) == 0) {
                    auto n = std::stoi(r[i].second.substr(6));
                    r[i + 1].second = std::to_string(-n);
                    ++users;
                }
                if (r[i].first == 9 && r[i].second.rfind("$USERR", 0) == 0) {
                    auto n = std::stoi(r[i].second.substr(6));
                    r[i + 1].second = std::to_string(n * 1.25);
                    ++users;
                }
            }
    check(users == 10, "default user variables");
    auto d = read(encode(raw, false));
    for (bool binary : {false, true})
        output(prefix + (binary ? ".binary.dxf" : ".ascii.dxf"), d, binary);
    check(d.modeler_geometry(control).has_value(), "nonempty modeler control");
    // Repair APPID conflicts with entity, table entry, control and an earlier APPID.
    auto layer = base.get_symbol(SymbolTable::Layer, "0")->handle;
    auto dim = base.symbols(SymbolTable::DimStyle).front().handle;
    std::string app_control;
    for (auto &r : raw)
        if (r.front().second == "TABLE" && std::find(r.begin(), r.end(), Pair{2, "APPID"}) != r.end())
            app_control = identity(r);
    for (auto target : {hex(line), layer, dim, app_control}) {
        auto collision = raw;
        for (auto &r : collision)
            if (r.front().second == "APPID") {
                for (auto &[c, v] : r)
                    if (c == 5) {
                        v = target;
                        break;
                    }
            }
        auto damaged = read(encode(collision, false));
        check(!damaged.duplicate_handles().empty(), "collision fixture");
        damaged.repair();
        check(damaged.duplicate_handles().empty(), "APPID collision repair");
        check(damaged.get_record(target) != nullptr, "original identity retained");
        for (bool binary : {false, true})
            output(prefix + ".collision-" + target + (binary ? ".binary.dxf" : ".ascii.dxf"), damaged,
                   binary);
    }
    base.add_symbol(SymbolTable::AppId, "SECOND");
    auto apps = records(save(base), false);
    auto same = base.get_symbol(SymbolTable::AppId, "ACAD")->handle;
    for (auto &r : apps)
        if (r.front().second == "APPID")
            for (auto &[c, v] : r)
                if (c == 5) {
                    v = same;
                    break;
                }
    auto duplicated = read(encode(apps, false));
    duplicated.repair();
    check(duplicated.duplicate_handles().empty(), "APPID versus APPID repair");
    // Every empty representation stays absent, including whitespace and terminator-only text.
    unsigned i = 0;
    for (auto version : {"AC1015", "AC1024", "AC1027"})
        for (auto text : {"", "   ", "End-of-ACIS-data", "End-of-ASM-data"}) {
            Records empty{{{0, "SECTION"}, {2, "HEADER"}, {9, "$ACADVER"}, {1, version}},
                          {{0, "ENDSEC"}},
                          {{0, "SECTION"}, {2, "ENTITIES"}}};
            unsigned handle = 0x800;
            for (auto type : {"3DSOLID", "REGION", "BODY", "SURFACE", "PLANESURFACE"}) {
                std::string cipher = text;
                for (char &c : cipher)
                    if (c != ' ')
                        c = char(c >= '@' && c <= '_' ? 159 - c : c ^ 0x5f);
                empty.push_back({{0, type},
                                 {5, hex(handle++)},
                                 {100, "AcDbEntity"},
                                 {8, "0"},
                                 {100, "AcDbModelerGeometry"},
                                 {70, "1"},
                                 {1, cipher}});
                if (std::string(type).find("SURFACE") != std::string::npos)
                    empty.back().insert(empty.back().end(), {{100, "AcDbSurface"}, {71, "0"}, {72, "0"}});
                if (std::string(type) == "PLANESURFACE")
                    empty.back().push_back({100, "AcDbPlaneSurface"});
            }
            empty.push_back({{0, "ENDSEC"}});
            empty.push_back({{0, "EOF"}});
            auto doc = read(encode(empty, false));
            for (auto h = 0x800; h < 0x805; ++h)
                check(!doc.modeler_geometry(h), "empty ACIS must be absent");
            for (bool binary : {false, true})
                output(prefix + ".empty-" + std::to_string(i) + (binary ? ".binary.dxf" : ".ascii.dxf"), doc,
                       binary);
            ++i;
        }
    auto geometry = SatDocument::parse(
        "700 0 1 0\n@8 acadrust @8 ACIS 7.0 @0 \n1 1e-6 1e-10\nprobe $-1 -1 #\nEnd-of-ACIS-data\n");
    auto &tokens = geometry.records[0].tokens;
    for (double v : {6.123233995737218e-17, 2.5e15, -0.0, .125})
        tokens.emplace_back(real(v));
    const std::string literal = "1 0 0 no_rotate no_reflect no_shear \t";
    SabToken string{18, {}};
    for (int j = 0; j < 4; ++j)
        string.data.push_back(std::uint8_t(literal.size() >> (8 * j)));
    string.data.insert(string.data.end(), literal.begin(), literal.end());
    tokens.emplace_back(string);
    tokens.emplace_back(SatString{"abc "});
    geometry.records[0].raw_text.reset();
    auto sab = SabWriter::write(geometry);
    auto decoded = SabReader::read(sab);
    auto text = decoded.to_sat_string();
    check(text.find("6.123233995737218e-017 2.5e+015 -0 0.125") != std::string::npos,
          "ACIS scalar number form");
    check(text.find("1 0 0 no_rotate no_reflect no_shear @4 abc ") != std::string::npos,
          "literal versus counted string");
    {
        std::ofstream f(prefix + ".sab", std::ios::binary);
        f.write(reinterpret_cast<const char *>(sab.data()), sab.size());
    }
    {
        std::ofstream f(prefix + ".sat");
        f << text;
    }
    std::cout << "Upstream 2026-11-15 regressions passed\n";
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
