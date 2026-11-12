// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace libopencad;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
DxfDocument read(const std::string& data) {
    std::istringstream input(data);
    return DxfDocument::read(input);
}
void boundaries() {
    auto doc = read(
        "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nBLOCK_RECORD\n"
        "0\nBLOCK_RECORD\n5\nA\n100\nAcDbSymbolTableRecord\n100\nAcDbBlockTableRecord\n2\n*MODEL_SPACE\n"
        "0\nBLOCK_RECORD\n5\nb\n2\n*paper_space12\n"
        "0\nBLOCK_RECORD\n5\nC\n2\nPart\n"
        "0\nBLOCK_RECORD\n5\nD\n2\n*paperwork\n"
        "0\nENDTAB\n0\nENDSEC\n"
        "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n5\n40\n330\nB\n"
        "0\nCUSTOM_ENTITY\n5\n41\n330\n000b\n0\nENDBLK\n5\n42\n330\nB\n"
        "0\nBLOCK\n5\n43\n330\nC\n0\nLINE\n5\n44\n330\nC\n0\nENDBLK\n5\n45\n330\nC\n"
        "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
        "0\nPOLYLINE\n5\n50\n102\n{REACTORS\n330\nB\n102\n}\n330\n000a\n67\n1\n"
        "0\nVERTEX\n5\n51\n330\nA\n0\nSEQEND\n5\n52\n330\nA\n"
        "0\nINSERT\n5\n53\n330\nB\n0\nATTRIB\n5\n54\n330\nB\n0\nSEQEND\n5\n55\n330\nB\n"
        "0\nLINE\n5\n56\n330\nDEAD\n0\nLINE\n5\n57\n67\n1\n"
        "0\nLINE\n5\n58\n330\n0\n0\nLINE\n5\n59\n330\nD\n"
        "0\nENDSEC\n0\nSECTION\n2\nOBJECTS\n0\nXRECORD\n5\n60\n330\nA\n"
        "0\nENDSEC\n0\nEOF\n");
    check(doc.model_space_entity_handles() == std::vector<std::string>{"50"}, "model ownership");
    check(doc.paper_space_entity_handles() == std::vector<std::string>({"41", "53"}), "paper and child filtering");
    check(doc.entity_handles_in_block("part") == std::vector<std::string>{"44"}, "named block definition");
    check(doc.entity_handles_in_block("absent").empty(), "unknown block name");
    check(doc.block_record("00a")->is_model_space(), "numeric handle matching");
    check(doc.block_record("B")->is_paper_space(), "paper prefix");
    for (auto handle : {"", "0", "bad handle", "DEAD"})
        check(doc.block_record(handle) == nullptr, "unresolved owner");
    auto copy = doc;
    std::ostringstream saved;
    copy.write(saved);
    check(read(saved.str()).paper_space_entity_handles() == doc.paper_space_entity_handles(), "copy and reopen");
}
void drawing(const char* format, const char* path, const char* result) {
    std::ifstream input(path, std::ios::binary);
    check(input.is_open(), "cannot open fixture");
    std::string bytes(std::istreambuf_iterator<char>{input}, {});
    const auto doc = read(bytes);
    check(doc.is_binary() == (std::string_view(format) == "binary"), "encoding");
    check(doc.model_space_entity_handles() == std::vector<std::string>({"31", "32"}), "model handles");
    check(doc.entity_handles_in_block("*Paper_Space") == std::vector<std::string>({"33", "34"}), "default paper");
    check(doc.entity_handles_in_block("*Paper_Space0") == std::vector<std::string>({"39", "3A"}), "second paper including viewport");
    const auto papers = doc.paper_space_entity_handles();
    check(std::set<std::string>(papers.begin(), papers.end()) ==
          std::set<std::string>({"33", "34", "39", "3A"}), "all paper handles");
    doc.for_each<DxfEntity>([&](const auto& e) {
        check(doc.block_record(e.owner_handle()) != nullptr, "entity owner resolves");
    });
    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(result);
    unsigned count = 0;
    doc.for_each<DxfBlockRecord>([&](const auto& block) {
        ++count;
        check(block.is_model_space() || block.is_paper_space(), "space classification");
        output << block.name() << ' ' << block.handle();
        auto handles = doc.entity_handles_in_block(block.name());
        std::sort(handles.begin(), handles.end(), [](const auto& a, const auto& b) {
            return std::stoull(a, nullptr, 16) < std::stoull(b, nullptr, 16);
        });
        for (const auto& handle : handles) output << ' ' << handle;
        output << '\n';
    });
    output.close();
    check(count == 3, "three block records");
    std::ostringstream saved;
    doc.write(saved);
    check(saved.str() == bytes, "query and save preserve drawing");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) boundaries();
        else {
            check(argc == 4, "expected format input output");
            drawing(argv[1], argv[2], argv[3]);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
