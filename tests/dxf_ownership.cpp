// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

using namespace libopencad;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
DxfDocument read(const std::string& bytes) {
    std::istringstream input(bytes);
    return DxfDocument::read(input);
}
void expect(const DxfDocument& doc, std::string_view start, OwnershipStatus status,
            std::vector<std::string> path, std::string_view reference) {
    const auto chain = doc.ownership_chain(start);
    check(chain.status == status && chain.reference == reference, "terminal status/reference");
    std::vector<std::string> actual;
    for (const auto* node : chain.records) actual.push_back(node->handle());
    check(actual == path, "ancestor chain");
}
void boundaries() {
    const auto doc = read(
        "0\nSECTION\n2\nOBJECTS\n"
        "0\nCUSTOM\n5\n10\n330\n0\n"
        "0\nCUSTOM\n5\n11\n102\n{APP\n330\nFFFF\n102\n}\n330\n0010\n"
        "0\nCUSTOM\n5\n12\n330\n11\n"
        "0\nCUSTOM\n5\n13\n100\nPayload\n330\nFFFF\n"
        "0\nCUSTOM\n5\n14\n330\nnope\n"
        "0\nCUSTOM\n5\n15\n330\n\n"
        "0\nCUSTOM\n5\n16\n330\nFFFFFFFFFFFFFFFFF\n"
        "0\nCUSTOM\n5\n17\n330\nFFFF\n"
        "0\nCUSTOM\n5\n18\n330\n0018\n"
        "0\nCUSTOM\n5\n19\n330\n1A\n0\nCUSTOM\n5\n1A\n330\n19\n"
        "0\nCUSTOM\n5\n1B\n330\n19\n"
        "0\nCUSTOM\n5\n1C\n330\n10\n0\nCUSTOM\n5\n001c\n330\n13\n"
        "0\nCUSTOM\n5\n1D\n330\n1C\n"
        "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
        "0\nPOLYLINE\n5\n20\n330\n10\n0\nVERTEX\n5\n21\n330\n20\n"
        "0\nSEQEND\n5\n22\n330\n20\n0\nENDSEC\n0\nEOF\n");
    expect(doc, "12", OwnershipStatus::Root, {"12", "11", "10"}, "0");
    expect(doc, "13", OwnershipStatus::MissingOwner, {"13"}, "");
    for (auto value : {"", "0", "xyz"})
        expect(doc, value, OwnershipStatus::InvalidHandle, {}, value);
    expect(doc, "FFFF", OwnershipStatus::DanglingReference, {}, "FFFF");
    expect(doc, "14", OwnershipStatus::InvalidHandle, {"14"}, "nope");
    expect(doc, "15", OwnershipStatus::InvalidHandle, {"15"}, "");
    expect(doc, "16", OwnershipStatus::InvalidHandle, {"16"}, "FFFFFFFFFFFFFFFFF");
    expect(doc, "17", OwnershipStatus::DanglingReference, {"17"}, "FFFF");
    expect(doc, "18", OwnershipStatus::Cycle, {"18"}, "0018");
    expect(doc, "1B", OwnershipStatus::Cycle, {"1B", "19", "1A"}, "19");
    expect(doc, "1c", OwnershipStatus::AmbiguousHandle, {}, "1c");
    expect(doc, "1D", OwnershipStatus::AmbiguousHandle, {"1D"}, "1C");
    for (auto child : {"21", "22"})
        expect(doc, child, OwnershipStatus::Root, {child, "20", "10"}, "0");
    auto copy = doc;
    check(copy.ownership_chain("12").records.front() != doc.get_record("12"), "copied chain ownership");
    std::ostringstream saved;
    doc.write(saved);
    expect(read(saved.str()), "1D", OwnershipStatus::AmbiguousHandle, {"1D"}, "1C");
    // Iterative traversal must also handle deep graphs without using the call stack.
    std::ostringstream long_input;
    long_input << "0\nSECTION\n2\nOBJECTS\n" << std::hex;
    for (unsigned i = 1; i <= 10000; ++i)
        long_input << "0\nCUSTOM\n5\n" << i << "\n330\n" << i - 1 << '\n';
    long_input << "0\nENDSEC\n0\nEOF\n";
    const auto deep = read(long_input.str());
    check(deep.ownership_chain("2710").records.size() == 10000, "deep chain");
    check(deep.ownership_chain("2710").status == OwnershipStatus::Root, "deep root");
}
std::string pair(bool binary, unsigned code, const std::string& value) {
    if (binary) return std::string{static_cast<char>(code & 255), static_cast<char>(code >> 8)} + value + '\0';
    std::ostringstream out;
    out << std::setw(3) << code << "\r\n" << value << "\r\n";
    return out.str();
}
void write_file(const std::string& path, const std::string& bytes) {
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(path, std::ios::binary);
    out << bytes;
    out.close();
}
void fixture(bool binary, const char* input_path, const std::string& prefix) {
    std::ifstream input(input_path, std::ios::binary);
    check(input.is_open(), "input fixture");
    std::string bytes(std::istreambuf_iterator<char>{input}, {});
    auto p = [&](unsigned code, const std::string& value) { return pair(binary, code, value); };
    const auto end = bytes.rfind(p(0, "ENDSEC") + p(0, "EOF"));
    check(end != bytes.npos, "OBJECTS ending");
    std::string extra;
    for (const auto& link : {std::pair{"1000", "1001"}, {"1001", "C"}, {"1002", "DEAD"},
                            {"1003", "1004"}, {"1004", "1003"}, {"1005", "1005"}, {"1006", "31"}})
        extra += p(0, "DICTIONARY") + p(5, link.first) + p(330, link.second) + p(100, "AcDbDictionary");
    bytes.insert(end, extra);
    const auto circle = p(0, "CIRCLE") + p(5, "32");
    const auto pos = bytes.find(circle);
    check(pos != bytes.npos, "circle identity");
    bytes.replace(pos, circle.size(), p(0, "CIRCLE") + p(5, "31"));
    const auto doc = read(bytes);
    check(doc.is_binary() == binary, "format");
    expect(doc, "1000", OwnershipStatus::Root, {"1000", "1001", "C"}, "0");
    expect(doc, "1002", OwnershipStatus::DanglingReference, {"1002"}, "DEAD");
    expect(doc, "1003", OwnershipStatus::Cycle, {"1003", "1004"}, "1003");
    expect(doc, "1005", OwnershipStatus::Cycle, {"1005"}, "1005");
    expect(doc, "1006", OwnershipStatus::AmbiguousHandle, {"1006"}, "31");
    std::ostringstream out;
    doc.write(out);
    check(out.str() == bytes, "validation changes drawing");
    write_file(prefix + ".dxf", out.str());
    std::ostringstream result;
    // Rust compares all chain links independently, then checks its own audit.
    for (auto start : {"33", "10", "1000", "1002", "1003", "1005", "1006"}) {
        const auto chain = doc.ownership_chain(start);
        for (const auto* node : chain.records)
            result << start << ' ' << node->handle() << ' ' << node->owner_handle() << '\n';
    }
    write_file(prefix + ".txt", result.str());
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) boundaries();
        else {
            check(argc == 4, "expected format fixture prefix");
            fixture(std::string_view(argv[1]) == "binary", argv[2], argv[3]);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
