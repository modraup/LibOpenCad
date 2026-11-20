// SPDX-License-Identifier: MPL-2.0
#include "dxf_fixture_records.hpp"
#include <cmath>
#include <limits>
namespace {
DimStyleDefinition definition(const DxfDocument &d) {
    DimStyleDefinition v;
    v.flags = 0x40;
    v.annotative = true;
    v.dimclrd = 1;
    v.dimdle = 0.125;
    v.dimdli = 3.875;
    v.dimgap = 0.75;
    v.dimlwd = 25;
    v.dimsd1 = true;
    v.dimsd2 = true;
    v.dimclre = 1;
    v.dimexe = 1.375;
    v.dimexo = 0.75;
    v.dimlwe = 50;
    v.dimse1 = true;
    v.dimse2 = true;
    v.dimfxl = 1.125;
    v.dimfxlon = true;
    v.dimasz = 0.305;
    v.dimblk = std::stoull(d.get_symbol(SymbolTable::BlockRecord, "Arrows")->handle, nullptr, 16);
    v.dimblk1 = std::stoull(d.get_symbol(SymbolTable::BlockRecord, "Arrows")->handle, nullptr, 16);
    v.dimblk2 = std::stoull(d.get_symbol(SymbolTable::BlockRecord, "Arrows")->handle, nullptr, 16);
    v.dimldrblk = std::stoull(d.get_symbol(SymbolTable::BlockRecord, "Arrows")->handle, nullptr, 16);
    v.dimsah = true;
    v.dimcen = 0.215;
    v.dimtsz = 0.125;
    v.dimclrt = 1;
    v.dimtxt = 0.305;
    v.dimjust = 1;
    v.dimtad = 1;
    v.dimtvp = 0.125;
    v.dimtih = true;
    v.dimtoh = true;
    v.dimtix = true;
    v.dimsoxd = true;
    v.dimtfill = 2;
    v.dimtfillclr = 5;
    v.dimtmove = 1;
    v.dimtxtdirection = true;
    v.dimtxsty_handle = std::stoull(d.get_symbol(SymbolTable::TextStyle, "Standard")->handle, nullptr, 16);
    v.dimscale = 1.125;
    v.dimlfac = 1.125;
    v.dimlunit = 1;
    v.dimdec = 1;
    v.dimrnd = 0.125;
    v.dimdsep = 44;
    v.dimzin = 1;
    v.dimaunit = 1;
    v.dimadec = 1;
    v.dimazin = 1;
    v.dimalt = true;
    v.dimaltf = 25.525;
    v.dimaltd = 1;
    v.dimaltu = 1;
    v.dimalttd = 1;
    v.dimaltrnd = 0.125;
    return v;
}
void verify(const DxfDocument &d) {
    for (auto name : {"中文标注", "Neighbor"}) {
        auto actual = *d.get_dim_style(name);
        auto expected = definition(d);
        check(actual.dimclrd == expected.dimclrd, "read dimclrd");
        check(std::abs(actual.dimdle - expected.dimdle) < 1e-12, "read dimdle");
        check(std::abs(actual.dimdli - expected.dimdli) < 1e-12, "read dimdli");
        check(std::abs(actual.dimgap - expected.dimgap) < 1e-12, "read dimgap");
        check(actual.dimlwd == expected.dimlwd, "read dimlwd");
        check(actual.dimsd1 == expected.dimsd1, "read dimsd1");
        check(actual.dimsd2 == expected.dimsd2, "read dimsd2");
        check(actual.dimclre == expected.dimclre, "read dimclre");
        check(std::abs(actual.dimexe - expected.dimexe) < 1e-12, "read dimexe");
        check(std::abs(actual.dimexo - expected.dimexo) < 1e-12, "read dimexo");
        check(actual.dimlwe == expected.dimlwe, "read dimlwe");
        check(actual.dimse1 == expected.dimse1, "read dimse1");
        check(actual.dimse2 == expected.dimse2, "read dimse2");
        check(std::abs(actual.dimfxl - expected.dimfxl) < 1e-12, "read dimfxl");
        check(actual.dimfxlon == expected.dimfxlon, "read dimfxlon");
        check(std::abs(actual.dimasz - expected.dimasz) < 1e-12, "read dimasz");
        check(actual.dimblk == expected.dimblk, "read dimblk");
        check(actual.dimblk1 == expected.dimblk1, "read dimblk1");
        check(actual.dimblk2 == expected.dimblk2, "read dimblk2");
        check(actual.dimldrblk == expected.dimldrblk, "read dimldrblk");
        check(actual.dimsah == expected.dimsah, "read dimsah");
        check(std::abs(actual.dimcen - expected.dimcen) < 1e-12, "read dimcen");
        check(std::abs(actual.dimtsz - expected.dimtsz) < 1e-12, "read dimtsz");
        check(actual.dimclrt == expected.dimclrt, "read dimclrt");
        check(std::abs(actual.dimtxt - expected.dimtxt) < 1e-12, "read dimtxt");
        check(actual.dimjust == expected.dimjust, "read dimjust");
        check(actual.dimtad == expected.dimtad, "read dimtad");
        check(std::abs(actual.dimtvp - expected.dimtvp) < 1e-12, "read dimtvp");
        check(actual.dimtih == expected.dimtih, "read dimtih");
        check(actual.dimtoh == expected.dimtoh, "read dimtoh");
        check(actual.dimtix == expected.dimtix, "read dimtix");
        check(actual.dimsoxd == expected.dimsoxd, "read dimsoxd");
        check(actual.dimtfill == expected.dimtfill, "read dimtfill");
        check(actual.dimtfillclr == expected.dimtfillclr, "read dimtfillclr");
        check(actual.dimtmove == expected.dimtmove, "read dimtmove");
        check(actual.dimtxtdirection == expected.dimtxtdirection, "read dimtxtdirection");
        check(actual.dimtxsty_handle == expected.dimtxsty_handle, "read dimtxsty_handle");
        check(std::abs(actual.dimscale - expected.dimscale) < 1e-12, "read dimscale");
        check(std::abs(actual.dimlfac - expected.dimlfac) < 1e-12, "read dimlfac");
        check(actual.dimlunit == expected.dimlunit, "read dimlunit");
        check(actual.dimdec == expected.dimdec, "read dimdec");
        check(std::abs(actual.dimrnd - expected.dimrnd) < 1e-12, "read dimrnd");
        check(actual.dimdsep == expected.dimdsep, "read dimdsep");
        check(actual.dimzin == expected.dimzin, "read dimzin");
        check(actual.dimaunit == expected.dimaunit, "read dimaunit");
        check(actual.dimadec == expected.dimadec, "read dimadec");
        check(actual.dimazin == expected.dimazin, "read dimazin");
        check(actual.dimalt == expected.dimalt, "read dimalt");
        check(std::abs(actual.dimaltf - expected.dimaltf) < 1e-12, "read dimaltf");
        check(actual.dimaltd == expected.dimaltd, "read dimaltd");
        check(actual.dimaltu == expected.dimaltu, "read dimaltu");
        check(actual.dimalttd == expected.dimalttd, "read dimalttd");
        check(std::abs(actual.dimaltrnd - expected.dimaltrnd) < 1e-12, "read dimaltrnd");
        check(actual.flags == 0x40 && actual.annotative, "flags and annotative");
    }
}
template <class F> void fails(DxfDocument &d, F f) {
    auto before = save(d);
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && save(d) == before, "dim style rollback");
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 4, "format input prefix");
        bool binary = std::string(argv[1]) == "binary";
        std::string prefix = argv[3];
        auto made = DxfDocument::create(binary);
        made.add_symbol(SymbolTable::BlockRecord, "Arrows");
        for (auto name : {"中文标注", "Neighbor"})
            made.add_dim_style(name, definition(made));
        verify(made);
        write(made, prefix + "-created.dxf");
        std::ifstream in(argv[2], std::ios::binary);
        auto d = DxfDocument::read(in);
        verify(d);
        auto before = records(save(d), binary);
        auto h = d.get_symbol(SymbolTable::DimStyle, "中文标注")->handle,
             neighbor = d.get_symbol(SymbolTable::DimStyle, "Neighbor")->handle;
        auto value = *d.get_dim_style("中文标注");
        value.dimtfill = 0;
        value.dimtfillclr = 0;
        value.dimjogang = 2;
        value.dimtxtdirection = false;
        value.annotative = false;
        value.dimpost = "修改<>";
        d.set_dim_style("中文标注", value);
        auto result = *d.get_dim_style("中文标注");
        check(result.flags == 0x40 && result.dimtfillclr == 0 && result.dimtfill == 0 &&
                  !result.dimtxtdirection && !result.annotative &&
                  std::abs(result.dimjogang - 1.5707963267948966) < 1e-12,
              "fill flags and angle clamp");
        auto after = records(save(d), binary);
        // DIMSTYLE uses group 105 for identity, so compare neighboring records by their 105.
        auto find = [&](Records &data, const std::string &id) -> Record & {
            for (auto &r : data)
                for (auto &p : r)
                    if (p.first == 105 && p.second == id)
                        return r;
            throw std::runtime_error("missing DIMSTYLE");
        };
        check(find(before, neighbor) == find(after, neighbor), "neighbor dim style unchanged");
        write(d, prefix + "-edited.dxf");
        fails(d, [&] {
            auto bad = value;
            bad.dimtxsty_handle = 0xFFFFFF;
            d.set_dim_style("中文标注", bad);
        });
        fails(d, [&] {
            auto bad = value;
            bad.dimjogang = std::numeric_limits<double>::infinity();
            d.set_dim_style("中文标注", bad);
        });
        auto &r = find(after, neighbor);
        r.erase(std::remove_if(r.begin(), r.end(),
                               [](auto &p) {
                                   return p.first != 0 && p.first != 105 && p.first != 330 &&
                                          p.first != 100 && p.first != 2;
                               }),
                r.end());
        auto omitted = read(encode(after, binary));
        auto defaults = *omitted.get_dim_style("Neighbor");
        check(defaults.dimdli == .38 && defaults.dimexo == .0625 && defaults.dimdec == 4 &&
                  defaults.dimtolj == 1 && defaults.dimtih && !defaults.dimsah,
              "DXF omitted imperial defaults");
        write(omitted, prefix + "-omitted.dxf");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
