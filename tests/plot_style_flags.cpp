// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace libopencad;
namespace {
void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
std::string bytes(const DxfDocument &d, bool binary = false) {
    std::ostringstream o;
    d.write(o, binary);
    return o.str();
}
void write_file(const std::string &path, const DxfDocument &d, bool binary = false) {
    std::ofstream out(path, std::ios::binary);
    d.write(out, binary);
    out.close();
    check(bool(out), "write failed");
}
DxfDocument read_bytes(const std::string &s) {
    std::istringstream in(s);
    return DxfDocument::read(in);
}
bool has_group(const std::string &ascii, const std::string &code, const std::string &value) {
    return ascii.find(code + "\r\n" + value + "\r\n") != std::string::npos;
}
DxfDocument with_flag(std::uint8_t flag, bool binary) {
    auto d = DxfDocument::create(binary);
    d.add_line({0, 0, 0}, {10, 10, 0});
    d.for_each<DxfLine>([&](auto &e) {
        auto p = e.display_properties();
        p.plot_style_mode = flag;
        p.plot_style = 0;
        e.set_display_properties(p);
        check(e.display_properties().plot_style_mode == flag, "plot-style mode");
        check(e.display_properties().plot_style == 0, "plot-style handle");
    });
    return d;
}
void expect_flag(const DxfDocument &d, std::uint8_t flag, const char *stage) {
    int lines = 0;
    d.for_each<DxfLine>([&](const auto &e) {
        auto p = e.display_properties();
        check(p.plot_style_mode == flag, stage);
        check(p.plot_style == 0, stage);
        ++lines;
    });
    check(lines == 1, stage);
}
void run_flag(std::uint8_t flag, const std::string &prefix) {
    for (bool binary : {false, true}) {
        auto d = with_flag(flag, binary);
        auto saved = bytes(d, binary);
        if (!binary) {
            check(has_group(saved, "380", std::to_string(flag)), "ASCII 380");
            check(!has_group(saved, "390", "0"), "no zero 390");
        }
        expect_flag(read_bytes(saved), flag, "roundtrip");
        write_file(prefix + (binary ? "-binary" : "-ascii") + ".dxf", d, binary);
        write_file(prefix + (binary ? "-binary" : "-ascii") + "-roundtrip.dxf", read_bytes(saved), binary);
    }
}
} // namespace
int main(int argc, char **argv) try {
    if (argc != 2)
        return 2;
    const std::string prefix = argv[1];
    run_flag(1, prefix + "-byblock");
    run_flag(2, prefix + "-named");
    auto bylayer = DxfDocument::create(false);
    bylayer.add_line({0, 0, 0}, {1, 0, 0});
    bylayer.for_each<DxfLine>([](auto &e) {
        auto p = e.display_properties();
        p.plot_style_mode = 0;
        e.set_display_properties(p);
    });
    auto bylayer_bytes = bytes(bylayer);
    check(!has_group(bylayer_bytes, "380", "0"), "ByLayer omits 380");
    expect_flag(read_bytes(bylayer_bytes), 0, "ByLayer");
    auto source = with_flag(1, false);
    auto ascii = bytes(source);
    auto marker = ascii.find("380\r\n1\r\n");
    check(marker != std::string::npos, "380=1 marker");
    ascii.replace(marker, 8, "380\r\n9\r\n");
    expect_flag(read_bytes(ascii), 0, "380=9 falls back");
    auto exported = source.export_version(DxfVersion::AC1015, false, {true});
    check(exported.report.can_write && !exported.bytes.empty(), "AC1015 export");
    auto old = read_bytes(exported.bytes);
    check(old.version() == DxfVersion::AC1015, "AC1015 version");
    expect_flag(old, 1, "AC1015 380");
    write_file(prefix + "-ascii-ac1015.dxf", old);
    write_file(prefix + "-ascii-ac1015-roundtrip.dxf", read_bytes(bytes(old)));
    bool refused = false;
    try {
        auto d = DxfDocument::create(false);
        d.add_line({0, 0, 0}, {1, 0, 0});
        d.for_each<DxfLine>([](auto &e) {
            auto p = e.display_properties();
            p.plot_style_mode = 4;
            e.set_display_properties(p);
        });
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    check(refused, "plot-style mode 4");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
