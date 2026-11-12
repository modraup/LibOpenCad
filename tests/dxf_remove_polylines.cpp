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
void write(const DxfDocument& doc, const std::string& path) {
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(path, std::ios::binary); doc.write(out); out.close();
}
DxfDocument source(const std::string& ascii, bool binary) {
    if (!binary) return read(ascii);
    std::istringstream in(ascii);
    std::ostringstream out;
    out.write(detail::binary_signature.data(), detail::binary_signature.size());
    std::string code, value;
    while (std::getline(in, code)) {
        check(bool(std::getline(in, value)), "fixture pair");
        const int number = std::stoi(code);
        detail::write_binary_pair(out, false, number, detail::binary_value_from_text(number, value));
    }
    return read(out.str());
}
void replace(std::string& bytes, const std::string& from, const std::string& to) {
    const auto pos = bytes.find(from);
    check(pos != bytes.npos, "fixture replacement");
    bytes.replace(pos, from.size(), to);
}
template<class Fn> void rejects(DxfDocument& doc, Fn fn) {
    const auto before = save(doc);
    const auto seed = doc.next_handle();
    const auto* record = doc.get_record("31");
    bool failed = false;
    try { fn(); } catch (const std::invalid_argument&) { failed = true; }
    check(failed && save(doc) == before && doc.next_handle() == seed && doc.get_record("31") == record,
          "rejected group deletion must be atomic");
}
template<class Path> std::vector<std::string> identities(const Path& path) {
    std::vector<std::string> result{path.handle()};
    for (std::size_t i = 0; i < path.vertex_count(); ++i) result.push_back(path.vertex(i).handle);
    if constexpr (std::is_same_v<Path,DxfPolyfaceMesh>)
        for (std::size_t i = 0; i < path.face_count(); ++i) result.push_back(path.face(i).handle);
    check(path.seqend(), "expected SEQEND");
    result.push_back(path.seqend()->handle());
    return result;
}
template<class Path> std::vector<std::vector<std::string>> groups(const DxfDocument& doc) {
    std::vector<std::vector<std::string>> result;
    doc.for_each<Path>([&](const auto& path) { result.push_back(identities(path)); });
    return result;
}
std::string marker(bool binary, std::string type, const std::string& handle) {
    using namespace std::string_literals;
    return binary ? "\0\0"s + type + "\0\x05\0"s + handle + '\0'
                  : "  0\r\n"s + type + "\r\n  5\r\n" + handle + "\r\n";
}
template<class Path> void verify(const DxfDocument& doc, const std::vector<std::string>& removed,
                                const std::vector<std::string>& remaining, std::uint64_t seed) {
    check(doc.next_handle() == seed && doc.duplicate_handles().empty(), "seed/uniqueness after deletion");
    check(groups<Path>(doc) == std::vector<std::vector<std::string>>{remaining}, "remaining child identities/order");
    check(doc.model_space_entity_handles() == std::vector<std::string>{remaining.front()} &&
          doc.paper_space_entity_handles().empty(), "remaining space membership");
    for (const auto& handle : removed) {
        check(!doc.get_record(handle) && doc.records_with_handle(handle).empty(), "deleted child still indexed");
        check(doc.ownership_chain(handle).status == OwnershipStatus::DanglingReference, "deleted ownership query");
    }
    for (const auto& handle : remaining) {
        check(doc.get_record(handle) && doc.records_with_handle(handle).size() == 1, "survivor index");
        check(doc.ownership_chain(handle).status == OwnershipStatus::Root, "survivor ownership chain");
    }
    doc.for_each<Path>([&](const auto& path) {
        if constexpr (std::is_same_v<Path,DxfPolygonMesh>) {
            check(path.vertex_count() == 12 && path.m_vertex_count() == 4 && path.n_vertex_count() == 3,
                  "adjacent grid dimensions");
            check(path.is_closed_m() && !path.is_closed_n() && path.layer() == "网格", "adjacent grid closure/layer");
        } else if constexpr (std::is_same_v<Path,DxfPolyfaceMesh>) {
            check(path.vertex_count() == 5 && path.face_count() == 3 && path.layer() == "网面", "adjacent polyface counts/layer");
            const std::array<int,4> indices[] = {{1,-2,5,0},{-1,2,3,4},{3,4,-5,5}};
            for (std::size_t i = 0; i < 3; ++i) {
                const auto face = path.face(i);
                check(face.indices == indices[i] && face.is_triangle() == (i != 1), "face references/hidden edges");
                check(face.owner_handle == path.handle() && face.layer == (i == 0 ? "网面" : "0"), "face ownership/layer");
            }
        } else check(!path.is_closed() && path.vertex_count() == 3, "adjacent open path");
        check(path.seqend()->owner_handle() == path.handle(), "adjacent SEQEND owner");
        for (std::size_t i = 0; i < path.vertex_count(); ++i) {
            const auto v = path.vertex(i);
            check(v.owner_handle == path.handle(), "adjacent VERTEX owner");
            if constexpr (std::is_same_v<Path,DxfPolyline2D>) {
                const Point3 points[] = {{12,1,0},{16,2,0},{21,4,0}};
                check(v.location == points[i] && v.bulge == 0, "adjacent 2D vertices/bulge");
                check(v.start_width == (i ? i*0.75 : 0.25) && v.end_width == (i ? i*0.875 : 0.5), "adjacent widths");
                check(path.default_start_width() == 0.25 && path.default_end_width() == 0.5, "default widths");
            } else if constexpr (std::is_same_v<Path,DxfPolyline3D>) {
                const Point3 points[] = {{12,1,11},{16,2,-4.5},{21,4,9}};
                check(v.position == points[i] && v.flags == 32, "adjacent XYZ/order/flags");
                check(v.layer == (i == 1 ? "测点" : "路径"), "adjacent vertex layers");
            } else if constexpr (std::is_same_v<Path,DxfPolygonMesh>) {
                const auto m = i/3, n = i%3;
                check(path.grid_index(m,n) == i && v.position == Point3{20. + m,double(n),10.*m - n}, "grid XYZ/order");
                check(v.flags == 64 && v.layer == (m == 1 && n == 2 ? "测点" : "网格"), "grid vertex flags/layer");
            } else {
                const Point3 points[] = {{20,0,0},{24,0,0},{24,4,0},{20,4,0},{22,2,3}};
                check(v.position == points[i] && v.flags == 192, "polyface coordinates/order/flags");
            }
        }
    });
}
template<class Path> void fixture(bool binary, const char* input, const std::string& output) {
    std::ifstream in(input, std::ios::binary);
    auto doc = DxfDocument::read(in);
    const auto before = save(doc);
    check(doc.is_binary() == binary, "format");
    const auto original = groups<Path>(doc);
    constexpr bool polygon = std::is_same_v<Path,DxfPolygonMesh>;
    constexpr bool polyface = std::is_same_v<Path,DxfPolyfaceMesh>;
    check(original.size() == 2 && original[0].size() == (polygon ? 14 : polyface ? 10 : 6) &&
          original[1].size() == (polygon ? 14 : polyface ? 10 : 5), "two adjacent groups");
    const auto seed = doc.next_handle();
    auto independent = doc;
    for (std::size_t i = 1; i < original[0].size(); ++i)
        rejects(doc, [&] { doc.remove_entity(original[0][i]); });
    check(doc.remove_entity(original[0].front()), "delete first parent");
    verify<Path>(doc, original[0], original[1], seed);
    verify<Path>(read(save(doc)), original[0], original[1], seed);
    // Compare the entire saved file to one with precisely the first sequence removed.
    auto expected = before;
    const auto begin = expected.find(marker(binary, "POLYLINE", original[0].front()));
    const auto end = expected.find(marker(binary, "POLYLINE", original[1].front()));
    check(begin != expected.npos && end != expected.npos && begin < end, "sequence boundaries");
    expected.erase(begin, end - begin);
    check(save(doc) == expected, "other raw records or order changed");
    write(doc, output);
    const auto once = save(doc);
    for (const auto& h : original[0]) check(!doc.remove_entity(h), "repeated removal must be a no-op");
    check(save(doc) == once, "no-op changed output");
    check(groups<Path>(independent) == original, "copy unaffected");
    // Remove the next parent after index shifts, then persist and allocate above all old children.
    check(doc.remove_entity(original[1].front()), "delete shifted parent");
    check(groups<Path>(doc).empty() && doc.model_space_entity_handles().empty(), "empty model space");
    auto empty = read(save(doc));
    for (const auto& group : original) for (const auto& h : group) check(!empty.get_record(h), "child remains after empty/save");
    check(empty.allocate_handle() == seed, "removed child handle must not be reused");
}
void boundaries() {
    const std::string tables = "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nBLOCK_RECORD\n"
        "0\nBLOCK_RECORD\n5\n15\n2\n*Model_Space\n0\nBLOCK_RECORD\n5\n18\n2\n*Paper_Space\n0\nENDTAB\n0\nENDSEC\n";
    const std::string tail = "0\nENDSEC\n0\nEOF\n";
    for (bool binary : {false,true}) for (int flags : {1,9,16,64}) {
        const std::string head = tables + "0\nSECTION\n2\nENTITIES\n0\nPOLYLINE\n5\n31\n330\n15\n70\n" + std::to_string(flags) + "\n";
        const std::string children = flags == 64
            ? "0\nVERTEX\n5\n34\n330\n31\n70\n192\n10\n1\n20\n2\n30\n3\n"
              "0\nVERTEX\n5\n35\n330\n31\n70\n128\n71\n1\n72\n-2\n73\n1\n"
              "0\nVERTEX\n5\n37\n330\n31\n70\n192\n10\n4\n20\n5\n30\n6\n"
              "0\nVERTEX\n5\n38\n330\n31\n70\n0\n10\n99\n0\nSEQEND\n5\n36\n330\n31\n"
            : "0\nVERTEX\n5\n34\n330\n31\n70\n64\n10\n1\n20\n2\n30\n3\n"
              "0\nVERTEX\n5\n35\n330\n31\n70\n64\n10\n4\n20\n5\n30\n6\n0\nSEQEND\n5\n36\n330\n31\n";
        const std::string next = "0\nPOLYLINE\n5\n32\n330\n18\n70\n" + std::to_string(flags == 1 ? 8 : flags == 16 ? 64 : flags == 64 ? 16 : 0) +
            "\n0\nVERTEX\n5\n40\n330\n32\n0\nSEQEND\n5\n41\n330\n32\n";
        const auto base = head + children + next;
        auto reject = [&](const std::string& bytes) {
            auto doc = source(bytes + tail, binary);
            rejects(doc, [&] { doc.remove_entity("31"); });
        };
        // External references to parent, any vertex or SEQEND block the whole operation.
        for (auto handle : {"31","34","35","36"}) for (auto code : {330,340,1005})
            reject(base + "0\nCUSTOM\n5\nFF\n" + std::to_string(code) + "\n" + handle + "\n");
        auto reactor = source(base + "0\nLINE\n5\nFF\n102\n{ACAD_REACTORS\n330\n36\n102\n}\n" + tail, binary);
        check(reactor.remove_entity("31") && reactor.get_record("FF"), "reactor to SEQEND cleaned");
        check(save(reactor).find("{ACAD_REACTORS") == std::string::npos, "empty reactor group removed");
        reject(base + "0\nENDSEC\n0\nSECTION\n2\nOBJECTS\n0\nDICTIONARY\n5\nFF\n330\n34\n");
        for (const auto& from : {"5\n35\n", "5\n36\n", "5\n40\n"}) {
            auto duplicate = base; replace(duplicate, from, "5\n34\n"); reject(duplicate);
        }
        auto parent_alias = base; replace(parent_alias, "5\n34\n", "5\n31\n"); reject(parent_alias);
        for (auto original_owner : {"5\n34\n330\n31\n", "5\n35\n330\n31\n", "5\n36\n330\n31\n"}) {
            auto wrong = base;
            std::string changed = original_owner; replace(changed, "330\n31\n", "330\n32\n");
            replace(wrong, original_owner, changed); reject(wrong);
        }
        auto malformed = base; replace(malformed, "5\n34\n", "5\nINVALID\n"); reject(malformed);
        auto owner_bad = base; replace(owner_bad, "5\n34\n330\n31\n", "5\n34\n330\nBAD-OWNER\n"); reject(owner_bad);
        auto referenced_by_neighbor = base;
        replace(referenced_by_neighbor, "5\n40\n330\n32\n", "5\n40\n330\n32\n1001\nAPP\n1005\n34\n");
        reject(referenced_by_neighbor);
        for (auto member : {"31","34","35","36"}) {
            auto owned = base;
            replace(owned, "5\n" + std::string(member) + "\n", "5\n" + std::string(member) + "\n102\n{ACAD_XDICTIONARY\n360\nFF\n102\n}\n");
            reject(owned);
        }
        auto good = source(base + tail, binary);
        check(good.remove_entity("0031"), "internal owner references must not block removal");
        for (auto member : {"31","34","35","36","37","38"})
            check(!good.get_record(member), "coordinate, face or unclassified child remains indexed");
        check(good.model_space_entity_handles().empty() && good.paper_space_entity_handles() == std::vector<std::string>{"32"}, "spaces after group removal");
        check(good.get_record("40") && good.get_record("41"), "neighbor children remain indexed");
        check(good.remove_entity("32") && good.paper_space_entity_handles().empty(), "delete paper-space sequence");
        // Sequence boundaries: stop at the next parent even when SEQEND is missing.
        const auto stop = children.find("0\nSEQEND\n");
        auto incomplete = source(head + children.substr(0,stop) + next + tail, binary);
        check(incomplete.remove_entity("31") && incomplete.get_record("40") && incomplete.get_record("41"), "unterminated sequence crossed parent");
        // Legacy records may lack child handles/owners. Remove only the attached sequence.
        auto legacy = source(head + "0\nVERTEX\n10\n1\n20\n2\n0\nSEQEND\n" + next + tail, binary);
        check(legacy.remove_entity("31") && legacy.get_record("32"), "anonymous child deletion");
        auto empty = source(head + "0\nSEQEND\n5\n36\n330\n31\n" + next + tail, binary);
        check(empty.remove_entity("31") && !empty.get_record("36"), "empty sequence deletion");
        // A post-SEQEND orphan must remain outside the deletion group.
        auto orphan = source(head + children + "0\nVERTEX\n5\nEE\n" + next + tail, binary);
        check(orphan.remove_entity("31") && orphan.get_record("EE"), "crossed SEQEND boundary");
        auto high_child = base; replace(high_child, "5\n36\n", "5\nFFFFFFFFFFFFFFFE\n");
        auto exhausted = source(high_child + tail, binary);
        check(exhausted.remove_entity("31"), "deletion must not allocate");
        check(read(save(exhausted)).next_handle() == UINT64_MAX, "highest child allocator persisted");
        // Internal reactors disappear with their own group, not an external dependency.
        auto internal = base; replace(internal, "5\n34\n", "5\n34\n102\n{ACAD_REACTORS\n330\n35\n102\n}\n");
        auto linked = source(internal + tail, binary);
        check(linked.remove_entity("31"), "internal child reference");
        if (flags == 64) {
            // Flags/counts are not the deletion boundary: include every attached raw child.
            for (auto hidden : {"37","38"}) {
                reject(base + "0\nCUSTOM\n5\nFF\n1005\n" + std::string(hidden) + "\n");
                auto conflict = base; replace(conflict, "5\n" + std::string(hidden) + "\n", "5\n35\n"); reject(conflict);
            }
            auto numeric = base; replace(numeric, "71\n1\n72\n-2\n", "71\n31\n72\n-34\n");
            auto indexed = source(numeric + tail, binary);
            check(indexed.remove_entity("31"), "face indices are local numbers, not DXF handles");
            auto counts = base; replace(counts, "70\n64\n", "70\n64\n71\n999\n72\n999\n");
            auto incorrect_counts = source(counts + tail, binary);
            check(incorrect_counts.remove_entity("31") && !incorrect_counts.get_record("38"), "header counts must not limit deletion");
        }
        // BLOCKS sequences use the same atomic whole-group deletion.
        auto raw = base; replace(raw, "2\nENTITIES\n", "2\nBLOCKS\n");
        auto block_sequence=source(raw+tail,binary);check(block_sequence.remove_entity("31") && !block_sequence.get_record("34"),"BLOCKS sequence deletion");
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) boundaries();
        else {
            check(argc == 5, "expected dimension format input output");
            const bool binary = std::string_view(argv[2]) == "binary";
            const std::string_view kind = argv[1];
            if (kind == "2d" || kind == "polyline2d") fixture<DxfPolyline2D>(binary, argv[3], argv[4]);
            else if (kind == "3d" || kind == "polyline3d") fixture<DxfPolyline3D>(binary, argv[3], argv[4]);
            else if (kind == "polygon_mesh") fixture<DxfPolygonMesh>(binary, argv[3], argv[4]);
            else if (kind == "polyface_mesh") fixture<DxfPolyfaceMesh>(binary, argv[3], argv[4]);
            else throw std::invalid_argument("Unknown sequence type");
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
