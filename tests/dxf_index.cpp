// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

using namespace libopencad;
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
DxfDocument read(const std::string& bytes) {
    std::istringstream input(bytes);
    return DxfDocument::read(input);
}
void boundaries() {
    auto doc = read(
        "0\nSECTION\n2\nHEADER\n9\n$HANDSEED\n5\nFFFF\n0\nENDSEC\n"
        "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nDIMSTYLE\n5\n10\n"
        "0\nDIMSTYLE\n105\nAb\n5\nDEAD\n330\n10\n0\nENDTAB\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n0\nPOLYLINE\n5\n100\n"
        "0\nVERTEX\n5\n101\n330\n100\n0\nSEQEND\n5\n102\n330\n100\n"
        "0\nLINE\n5\n200\n102\n{REACTORS\n5\nFFFF\n330\nFFFF\n102\n}\n330\nAB\n"
        "0\nCIRCLE\n5\n0200\n330\n10\n0\nENDSEC\n"
        "0\nSECTION\n2\nOBJECTS\n0\nCUSTOM\n5\n300\n100\nCustomData\n5\nFFFF\n"
        "1001\nAPP\n1005\nFFFF\n0\nCUSTOM\n5\n00AB\n330\n10\n0\nENDSEC\n0\nEOF\n");
    check(doc.records_with_handle("00ab").front()->type() == "DIMSTYLE", "DIMSTYLE uses 105");
    check(doc.get_record("Ab")->type() == "CUSTOM", "cross-section duplicate");
    check(doc.get_record("10")->type() == "TABLE", "table header");
    check(doc.get_record("101")->type() == "VERTEX", "child identity");
    check(doc.get_record("102")->owner_handle() == "100", "SEQEND owner");
    check(doc.get_record("300")->type() == "CUSTOM", "unknown object");
    for (auto invalid : {"", "0", "FFFF", "DEAD", "garbage", "10000000000000000"})
        check(doc.get_record(invalid) == nullptr && doc.records_with_handle(invalid).empty(), "invalid or reference indexed");
    check(doc.duplicate_handles() == std::vector<std::uint64_t>({0xab, 0x200}), "duplicate detection");
    const auto matches = doc.records_with_handle("200");
    check(matches.size() == 2 && matches[0]->type() == "LINE" && matches[1]->type() == "CIRCLE", "duplicate order");
    check(matches[0]->owner_handle() == "AB", "reactor owner skipped");
    check(doc.get_record("200") == matches.back(), "last identity wins");
    auto copy = doc;
    check(copy.get_record("101") != doc.get_record("101"), "copy must own its records");
    auto moved = std::move(copy);
    check(moved.get_record("102")->type() == "SEQEND", "moved child index");
    moved = read("0\nEOF\n");
    check(moved.get_record("101") == nullptr, "assignment clears index");
    doc.for_each<DxfCircle>([](auto& c) { c.set_radius(7); });
    check(doc.get_record("200")->type() == "CIRCLE", "edit preserves lookup");
    auto reserved = doc.allocate_handle();
    check(reserved == 0xffff && doc.get_record("FFFF") == nullptr, "reservation is not a record");
    std::ostringstream out;
    doc.write(out);
    auto reopened = read(out.str());
    check(reopened.duplicate_handles() == doc.duplicate_handles(), "duplicates survive write");
    check(reopened.get_record("101")->owner_handle() == "100", "child survives write");
}
void write_file(const std::string& path, const std::string& bytes) {
    std::ofstream out;
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out.open(path, std::ios::binary);
    out << bytes;
    out.close();
}
void report(const DxfDocument& doc, const std::string& path) {
    std::ostringstream out;
    for (auto h : {"10", "11", "1B", "15", "18", "35", "31", "32", "39", "C"}) {
        const auto* record = doc.get_record(h);
        if (record) out << h << ' ' << record->type() << ' ' << record->owner_handle() << '\n';
        else out << h << " missing\n";
    }
    write_file(path, out.str());
}
void fixture(const std::string& format, const char* path, const std::string& prefix) {
    std::ifstream input(path, std::ios::binary);
    check(input.is_open(), "fixture open");
    std::string bytes(std::istreambuf_iterator<char>{input}, {});
    const auto doc = read(bytes);
    check(doc.duplicate_handles().empty(), "fixture identities unique");
    check(doc.is_binary() == (format == "binary"), "fixture encoding");
    report(doc, prefix + ".txt");
    // Give the model-space CIRCLE the preceding LINE's identity.
    const auto pattern = format == "binary" ? std::string("CIRCLE\0\x05\0" "32\0", 12)
                                             : std::string("CIRCLE\r\n  5\r\n32\r\n");
    const auto pos = bytes.find(pattern);
    check(pos != bytes.npos, "find circle identity");
    const auto digit = pos + pattern.find("32");
    bytes[digit + 1] = '1';
    auto duplicate = read(bytes);
    check(duplicate.duplicate_handles() == std::vector<std::uint64_t>{0x31}, "fixture duplicate");
    check(duplicate.get_record("31")->type() == "CIRCLE", "duplicate lookup");
    write_file(prefix + "-duplicate.dxf", bytes);
    report(duplicate, prefix + "-duplicate.txt");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) boundaries();
        else {
            check(argc == 4, "expected format fixture prefix");
            fixture(argv[1], argv[2], argv[3]);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
