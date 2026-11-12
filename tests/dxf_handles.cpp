// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

using libopencad::DxfDocument;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

DxfDocument read(const std::string& data) {
    std::istringstream input(data);
    return DxfDocument::read(input);
}

std::string save(const DxfDocument& doc) {
    std::ostringstream output;
    doc.write(output);
    return output.str();
}

void write_file(const std::string& path, const std::string& data) {
    std::ofstream output;
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.open(path, std::ios::binary);
    output << data;
    output.close();
}

void check_boundaries() {
    const std::string end = "0\nENDSEC\n";
    const std::string eof = "0\nEOF\n";
    auto section = [&](std::string name, std::string body) {
        return "0\nSECTION\n2\n" + name + "\n" + body + end + eof;
    };
    // Test each source in isolation so a higher handle elsewhere cannot mask a missed identity.
    for (const auto& input : {
        section("TABLES", "0\nTABLE\n2\nLAYER\n5\nabc\n0\nENDTAB\n"),
        section("TABLES", "0\nTABLE\n2\nDIMSTYLE\n0\nDIMSTYLE\n105\nabc\n5\nDEAD\n0\nENDTAB\n"),
        section("BLOCKS", "0\nBLOCK\n5\n10\n0\nLINE\n5\nabc\n0\nENDBLK\n5\n11\n"),
        section("BLOCKS", "0\nBLOCK\n5\n10\n0\nENDBLK\n5\nabc\n"),
        section("ENTITIES", "0\nPOLYLINE\n5\n10\n0\nVERTEX\n5\nabc\n0\nSEQEND\n5\n11\n"),
        section("ENTITIES", "0\nPOLYLINE\n5\n10\n0\nVERTEX\n5\n11\n0\nSEQEND\n5\nabc\n"),
        section("OBJECTS", "0\nCUSTOM\n102\n{APP\n5\nBEEF\n102\n}\n5\nabc\n330\nFFFF\n"
                           "100\nCustomData\n5\nDEAD\n1001\nAPP\n1005\nCAFE\n")}) {
        auto doc = read(input);
        check(doc.allocate_handle() == 0xabd, "record identity or reference misclassified");
        check(read(save(doc)).next_handle() == 0xabe, "missing HEADER was not populated");
    }
    for (const auto& vars : {"", "9\n$HANDSEED\n5\ninvalid\n", "9\n$HANDSEED\n5\n0\n"}) {
        auto doc = read(section("HEADER", vars));
        check(doc.allocate_handle() == 0x10, "allocator floor");
        check(read(save(doc)).next_handle() == 0x11, "missing or invalid seed was not updated");
    }
    const auto limit = std::numeric_limits<std::uint64_t>::max();
    auto near_limit = read(section("HEADER", "9\n$HANDSEED\n5\nFFFFFFFFFFFFFFFE\n"));
    check(near_limit.allocate_handle() == limit - 1, "last reservable handle");
    auto occupied_limit = read(section("OBJECTS", "0\nXRECORD\n5\nFFFFFFFFFFFFFFFF\n"));
    for (auto* doc : {&near_limit, &occupied_limit}) {
        auto before = save(*doc);
        try {
            doc->allocate_handle();
            throw std::runtime_error("64-bit handle exhaustion did not throw");
        } catch (const std::overflow_error&) {}
        check(doc->next_handle() == limit && save(*doc) == before, "overflow changed the drawing");
    }
}

// String-valued DXF pairs used to extend the existing upstream-generated drawing.
std::string pair(bool binary, int code, const std::string& value) {
    if (binary) return std::string{static_cast<char>(code & 255), static_cast<char>(code >> 8)} + value + '\0';
    std::ostringstream out;
    out << std::setw(3) << code << "\r\n" << value << "\r\n";
    return out.str();
}

void check_drawing(bool binary, const char* fixture, const std::string& prefix) {
    std::ifstream input(fixture, std::ios::binary);
    check(input.is_open(), "cannot open drawing");
    std::string drawing(std::istreambuf_iterator<char>{input}, {});
    auto p = [&](int code, const std::string& value) { return pair(binary, code, value); };
    const auto tail = p(0, "ENDSEC") + p(0, "EOF");
    auto pos = drawing.rfind(tail);
    check(pos != std::string::npos, "missing end of OBJECTS");
    drawing.insert(pos, p(0, "XRECORD") + p(5, "123456789A") + p(330, "C") +
                        p(100, "AcDbXrecord") + p(1, "handle allocation test"));
    const auto header_var = p(9, "$HANDSEED");
    const auto original = header_var + p(5, "37");
    const auto seed_pos = drawing.find(original);
    check(seed_pos != std::string::npos, "missing original seed");
    for (std::string seed : {"31", "123456789A", "1234568000"}) {
        auto with_seed = [&](const std::string& value) {
            auto bytes = drawing;
            return bytes.replace(seed_pos, original.size(), header_var + p(5, value));
        };
        const auto before = with_seed(seed);
        auto doc = read(before);
        check(doc.is_binary() == binary && save(doc) == before, "unmodified round trip");
        std::uint64_t first = seed == "1234568000" ? 0x1234568000 : 0x123456789B;
        const std::string path = prefix + "-" + seed;
        write_file(path + ".dxf", before);
        for (unsigned i = 0; i < 4; ++i) {
            check(doc.next_handle() == first + i, "candidate handle");
            check(doc.allocate_handle() == first + i, "reserved handle");
        }
        const auto after = save(doc);
        check(after == with_seed(seed == "1234568000" ? "1234568004" : "123456789F"),
              "saved bytes must differ only in HANDSEED");
        write_file(path + "-saved.dxf", after);
        check(read(after).allocate_handle() == first + 4, "reservation lost on reopen");
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 1) check_boundaries();
        else {
            check(argc == 4, "expected format fixture prefix");
            check_drawing(std::string_view(argv[1]) == "binary", argv[2], argv[3]);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
