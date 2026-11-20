// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include "../src/dxf_binary.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace libopencad;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string save(const DxfDocument& doc) { std::ostringstream out; doc.write(out); return out.str(); }
DxfDocument read(const std::string& bytes) { std::istringstream in(bytes); return DxfDocument::read(in); }
std::string hex(std::uint64_t n) { std::ostringstream out; out << std::hex << std::uppercase << n; return out.str(); }
void write(const DxfDocument& doc, const std::string& path) {
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(path, std::ios::binary); doc.write(out); out.close();
}
template<class Fn> void rejects(DxfDocument& doc, Fn fn) {
    const auto bytes = save(doc);
    const auto seed = doc.next_handle();
    bool failed = false;
    try { fn(); } catch (const std::invalid_argument&) { failed = true; }
    check(failed && save(doc) == bytes && doc.next_handle() == seed, "rejected deletion changed document");
}
void no_op(DxfDocument& doc, std::string_view handle) {
    const auto bytes = save(doc);
    const auto seed = doc.next_handle();
    const auto* layer = doc.get_layer("0");
    check(!doc.remove_entity(handle), "missing handle must return false");
    check(save(doc) == bytes && doc.next_handle() == seed && doc.get_layer("0") == layer, "no-op changed document");
}
// Independent byte comparison of all surviving records, not just known geometry fields.
struct Chunk { std::string type, handle, bytes; };
std::vector<Chunk> chunks(const DxfDocument& doc) {
    const auto bytes = save(doc);
    std::istringstream in(bytes);
    if (doc.is_binary()) in.ignore(detail::binary_signature.size());
    std::vector<Chunk> result;
    std::size_t start = static_cast<std::size_t>(in.tellg());
    while (in.peek() != std::char_traits<char>::eof()) {
        const auto pos = static_cast<std::size_t>(in.tellg());
        int code;
        std::string value;
        if (doc.is_binary()) {
            code = detail::read_binary_code(in, in.get(), false);
            value = detail::read_binary_value(in, code);
        } else {
            std::string code_text;
            std::getline(in, code_text); code = std::stoi(code_text);
            std::getline(in, value); if (!value.empty() && value.back() == '\r') value.pop_back();
        }
        if (code == 0) {
            if (!result.empty()) result.back().bytes = bytes.substr(start, pos - start);
            start = pos;
            result.push_back({value, {}, {}});
        } else if (code == 5 && !result.empty()) result.back().handle = value;
    }
    check(!result.empty(), "record chunks");
    result.back().bytes = bytes.substr(start);
    return result;
}
void survivors(const DxfDocument& before, const DxfDocument& after, const std::vector<std::string>& deleted) {
    auto original = chunks(before);
    original.erase(std::remove_if(original.begin(), original.end(), [&](const auto& c) {
        return (c.type == "LINE" || c.type == "CIRCLE" || c.type == "TEXT") &&
            std::find(deleted.begin(), deleted.end(), c.handle) != deleted.end();
    }), original.end());
    const auto actual = chunks(after);
    check(original.size() == actual.size(), "only requested records removed");
    for (std::size_t i = 0; i < actual.size(); ++i)
        check(original[i].bytes == actual[i].bytes, "surviving raw records/order changed");
}
void verify(const DxfDocument& doc, const std::vector<std::string>& model, const std::vector<std::string>& paper) {
    check(doc.model_space_entity_handles() == model && doc.paper_space_entity_handles() == paper, "space membership/order");
    check(doc.next_handle() == 0x3A && doc.duplicate_handles().empty(), "allocator and identities");
    for (std::uint64_t h = 0x34; h <= 0x39; ++h) {
        const auto key = hex(h);
        const bool exists = std::find(model.begin(), model.end(), key) != model.end() ||
            std::find(paper.begin(), paper.end(), key) != paper.end();
        check(bool(doc.get_record(key)) == exists, "record index after erase");
        check(doc.records_with_handle(key).size() == (exists ? 1 : 0), "all matching records after erase");
        check(doc.ownership_chain(key).status == (exists ? OwnershipStatus::Root : OwnershipStatus::DanglingReference), "ownership query after erase");
    }
    int count = 0;
    doc.for_each<DxfEntity>([&](const auto&) { ++count; });
    check(count == int(model.size() + paper.size()), "typed traversal after erase");
}
void fixture(const char* path, const std::string& prefix) {
    std::ifstream in(path, std::ios::binary);
    const auto original = DxfDocument::read(in);
    verify(original, {"34","35","36"}, {"37","38","39"});
    auto first = original;
    check(first.remove_entity(" 00034 ") && first.remove_entity(std::uint64_t(0x38)) && first.remove_entity("36"), "delete first subset");
    verify(first, {"35"}, {"37","39"});
    no_op(first, "34");
    survivors(original, first, {"34","38","36"});
    verify(read(save(first)), {"35"}, {"37","39"});
    write(first, prefix + "-first.dxf");
    // Complementary subset exercises each type in both spaces and shifted indices.
    auto second = original;
    for (auto h : {"39","35","37"}) check(second.remove_entity(h), "delete second subset");
    verify(second, {"34","36"}, {"38"});
    survivors(original, second, {"39","35","37"});
    write(second, prefix + "-second.dxf");
    for (auto h : {"37","35","39"}) check(first.remove_entity(h), "delete remaining entities");
    verify(first, {}, {});
    survivors(original, first, {"34","35","36","37","38","39"});
    write(first, prefix + "-empty.dxf");
    auto reopened = read(save(first));
    verify(reopened, {}, {});
    check(reopened.add_line({9,8,7}, {6,5,4}) == 0x3A, "never reuse deleted handle");
    write(reopened, prefix + "-readded.dxf");
    verify(original, {"34","35","36"}, {"37","38","39"});
}
void boundaries() {
    for (bool binary : {false, true}) {
        auto doc = DxfDocument::create(binary);
        const auto h = doc.add_line({}, {1,2,3});
        for (auto key : {"", "0", "-31", "+31", "0x31", "no", "FFFFFFFFFFFFFFFFF", "DEAD"}) no_op(doc, key);
        rejects(doc, [&] { doc.remove_entity("10"); }); // layer
        rejects(doc, [&] { doc.remove_entity("15"); }); // block record
        rejects(doc, [&] { doc.remove_entity("C"); }); // root dictionary
        auto copy = doc;
        check(copy.remove_entity(h), "numeric removal");
        check(doc.get_record(hex(h)) && !copy.get_record(hex(h)), "copy isolation");
        check(copy.model_space_entity_handles().empty(), "last entity removal");
        check(read(save(copy)).next_handle() == h+1, "seed after removal");
    }
    const std::string head = "0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nAB\n330\n15\n10\n1\n";
    const std::string tail = "0\nENDSEC\n0\nEOF\n";
    auto duplicate = read(head + "0\nCIRCLE\n5\n00ab\n40\n1\n" + tail);
    rejects(duplicate, [&] { duplicate.remove_entity("aB"); });
    // Cross-section duplicates are equally ambiguous, even though get_record is last-wins.
    auto cross = read(head + "0\nENDSEC\n0\nSECTION\n2\nOBJECTS\n0\nXRECORD\n5\nAB\n" + tail);
    rejects(cross, [&] { cross.remove_entity("AB"); });
    for (auto type : {"CUSTOM_SPLINE", "CUSTOM"}) {
        auto other = read("0\nSECTION\n2\nENTITIES\n0\n" + std::string(type) + "\n5\nAB\n" + tail);
        rejects(other, [&] { other.remove_entity("AB"); });
    }
    const std::string sequence = "0\nPOLYLINE\n5\n100\n0\nVERTEX\n5\n101\n330\n100\n0\nSEQEND\n5\n102\n330\n100\n";
    auto mixed = read(head + sequence + "0\nCUSTOM\n5\n103\n" + tail);
    rejects(mixed, [&] { mixed.remove_entity("101"); });
    rejects(mixed, [&] { mixed.remove_entity("102"); });
    check(mixed.remove_entity("ab"), "case-insensitive hex handle");
    check(mixed.get_record("101")->owner_handle() == "100" && mixed.get_record("102")->type() == "SEQEND" &&
        mixed.get_record("103")->type() == "CUSTOM", "compound and unknown indexes preserved");
    // Any supported raw handle reference must fail atomically, including opaque payloads.
    for (auto code : {320,330,340,350,360,390,480,481,1005}) {
        auto referenced = read(head + "0\nCUSTOM\n5\nFF\n" + std::to_string(code) + "\n00ab\n" + tail);
        rejects(referenced, [&] { referenced.remove_entity("AB"); });
    }
    auto reactor = read(head + "0\nCIRCLE\n5\nFF\n102\n{ACAD_REACTORS\n330\nAB\n102\n}\n" + tail);
    check(reactor.remove_entity("AB") && reactor.get_record("FF"), "reactor permits removal");
    check(save(reactor).find("{ACAD_REACTORS") == std::string::npos, "empty reactor group removed");
    auto child_reference = read(head + "0\nPOLYLINE\n5\n100\n0\nVERTEX\n5\n101\n330\nAB\n0\nSEQEND\n5\n102\n" + tail);
    rejects(child_reference, [&] { child_reference.remove_entity("AB"); });
    auto owned = read(head + "102\n{ACAD_XDICTIONARY\n360\nFF\n102\n}\n" + tail);
    rejects(owned, [&] { owned.remove_entity("AB"); });
    auto object_owner = read(head + "0\nENDSEC\n0\nSECTION\n2\nOBJECTS\n0\nDICTIONARY\n5\nFF\n330\nAB\n" + tail);
    rejects(object_owner, [&] { object_owner.remove_entity("AB"); });
    // Same hex-looking text is not a reference; deleting an orphan needs no repair.
    auto plain_text = read(head + "0\nTEXT\n5\nFF\n1\nAB\n" + tail);
    check(plain_text.remove_entity("AB") && plain_text.get_record("FF"), "plain text is not a handle");
    // Missing/stale HANDSEED must be reconciled on save even after deleting the highest identity.
    for (const auto& prefix : {std::string{}, std::string("0\nSECTION\n2\nHEADER\n9\n$HANDSEED\n5\n10\n0\nENDSEC\n")}) {
        auto old = read(prefix + head + tail);
        check(old.remove_entity("AB") && old.next_handle() == 0xAC, "reconciled seed");
        auto back = read(save(old));
        check(back.allocate_handle() == 0xAC, "persist seed after deleting max handle");
    }
    auto exhausted = read("0\nSECTION\n2\nENTITIES\n0\nTEXT\n5\nFFFFFFFFFFFFFFFF\n1\nlast\n" + tail);
    check(exhausted.remove_entity("FFFFFFFFFFFFFFFF"), "deletion does not allocate a handle");
    check(read(save(exhausted)).next_handle() == UINT64_MAX, "exhausted seed preserved");
    // BLOCKS records are raw but are still top-level entities; preserve block markers.
    auto block = read("0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nBLOCK_RECORD\n0\nBLOCK_RECORD\n5\n15\n2\n*Model_Space\n0\nENDTAB\n0\nENDSEC\n"
        "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n5\n16\n330\n15\n0\nLINE\n5\nAB\n330\n15\n0\nENDBLK\n5\n17\n330\n15\n" + tail);
    check(block.model_space_entity_handles() == std::vector<std::string>{"AB"}, "BLOCKS membership before");
    check(block.remove_entity("AB") && block.model_space_entity_handles().empty(), "remove raw BLOCKS entity");
    check(block.get_record("16") && block.get_record("17"), "block markers retained");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) boundaries();
        else { check(argc == 3, "expected input prefix"); fixture(argv[1], argv[2]); }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
