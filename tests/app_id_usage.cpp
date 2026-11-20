// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
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
std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
void write_file(const std::string &path, const DxfDocument &d) {
    std::ofstream out(path, std::ios::binary);
    d.write(out);
    out.close();
    check(bool(out), "write failed");
}
DxfDocument read_bytes(const std::string &s) {
    std::istringstream in(s);
    return DxfDocument::read(in);
}
std::string hex(std::uint64_t h) {
    std::ostringstream o;
    o << std::hex << std::uppercase << h;
    return o.str();
}
std::uint64_t app(const DxfDocument &d, std::string_view name) {
    auto s = d.get_symbol(SymbolTable::AppId, name);
    check(s.has_value(), "missing APPID");
    return std::stoull(s->handle, nullptr, 16);
}
bool has(const std::vector<std::uint64_t> &v, std::uint64_t h) {
    return std::find(v.begin(), v.end(), h) != v.end();
}
DxfDocument populated(bool binary) {
    auto d = DxfDocument::create(binary);
    check(d.unused_app_ids().empty(), "default writer APPIDs are in use");
    d.add_symbol(SymbolTable::AppId, "USED");
    d.add_symbol(SymbolTable::AppId, "RAW");
    d.add_symbol(SymbolTable::AppId, "IDLE");
    d.add_symbol(SymbolTable::AppId, "OWNED");
    auto line = d.add_line({0, 0, 0}, {1, 0, 0});
    d.set_extended_data(hex(line), {"used", {{1000, std::string("n")}}});
    auto extra = d.add_line({0, 1, 0}, {1, 1, 0});
    d.set_extended_data(hex(extra), {"RAW", {{1071, std::int32_t{1}}}});
    d.add_extension_dictionary(d.get_symbol(SymbolTable::AppId, "OWNED")->handle);
    return d;
}
void expect_idle(const DxfDocument &d) {
    auto unused = d.unused_app_ids();
    check(unused.size() == 1 && unused.front() == app(d, "IDLE"), "only IDLE is unused");
    check(!has(unused, app(d, "USED")) && !has(unused, app(d, "RAW")) && !has(unused, app(d, "OWNED")),
          "named or owning APPIDs stay");
    check(!has(unused, app(d, "ACAD")) && !has(unused, app(d, "AcadAnnotative")) &&
              !has(unused, app(d, "AcCmTransparency")) && !has(unused, app(d, "AcAecLayerStandard")),
          "writer APPIDs stay");
}
void run_drawing(DxfDocument d, const std::string &prefix) {
    expect_idle(d);
    write_file(prefix + "-source.dxf", d);
    auto loaded = read_bytes(bytes(d));
    expect_idle(loaded);
    write_file(prefix + "-roundtrip.dxf", loaded);
    auto before = bytes(d);
    (void)d.unused_app_ids();
    check(bytes(d) == before, "unused_app_ids is a query");
}
} // namespace
int main(int argc, char **argv) try {
    if (argc != 2)
        return 2;
    const std::string prefix = argv[1];
    run_drawing(populated(false), prefix + "-ascii");
    run_drawing(populated(true), prefix + "-binary");
    auto exported = populated(false).export_version(DxfVersion::AC1015, false, {true});
    check(exported.report.can_write && !exported.bytes.empty(), "AC1015 export");
    auto old = read_bytes(exported.bytes);
    check(old.version() == DxfVersion::AC1015, "AC1015 version");
    run_drawing(std::move(old), prefix + "-ascii-ac1015");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
