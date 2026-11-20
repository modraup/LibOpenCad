// SPDX-License-Identifier: MPL-2.0
#include <libopencad/dxf_document.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
using namespace libopencad;
static void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
static std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}
int main(int argc, char **argv) try {
    if (argc != 3)
        return 2;
    bool binary = std::string(argv[1]) == "binary";
    auto d = DxfDocument::create(binary);
    auto cube = MeshDefinition::create_box({-1, -2, -3}, {2, 4, 6});
    check(cube.vertices.size() == 8 && cube.faces.size() == 6 && cube.edges.size() == 12, "Box topology");
    auto plane = MeshDefinition::create_plane({1, 2, 3}, {4, 1, 2}, {0, 3, 1}, 2, 3);
    auto cylinder = MeshDefinition::create_cylinder({1, -2, 3}, 2.5, -4, 7, 2, true, true);
    auto merged = cube;
    merged.merge(plane);
    merged.flip_normals();
    merged.translate({2, 3, 4});
    merged.scale({1, 1, 1}, -2);
    merged.scale_uniform(.5);
    auto self = cube;
    self.merge(self);
    check(self.vertices.size() == 16 && self.faces.size() == 12 && self.edges.size() == 24,
          "Self merge invalidated input");
    for (auto m : {cube, plane, cylinder, merged, MeshDefinition{}}) {
        if (!m.edges.empty()) {
            auto edge = m.find_edge(m.edges.front().end, m.edges.front().start);
            check(edge.has_value(), "Reversed edge lookup");
            check(m.set_edge_crease(*edge, 1.5), "Crease edit");
            check(m.has_creases(), "Missing crease");
            auto clear = m;
            clear.clear_creases();
            check(!clear.has_creases(), "Creases not cleared");
        }
        check(!m.set_edge_crease(m.edges.size(), 1.0), "Out-of-range crease accepted");
        if (m.vertices.empty()) {
            check(!m.bounding_box() && !m.center(), "Empty mesh bounds");
        } else {
            check(m.center() == m.bounding_box()->center(), "Mesh center");
            for (auto p : m.vertices)
                check(m.bounding_box()->contains(p), "Bounds omit vertex");
        }
        d.add_mesh(m);
    }
    auto original = bytes(d);
    d.for_each<DxfMesh>([&](auto &e) {
        auto m = e.definition();
        if (m.vertices.empty())
            return;
        m.faces.push_back({{m.vertices.size(), 0, 1}});
        bool refused = false;
        try {
            e.set_definition(m);
        } catch (const std::invalid_argument &) {
            refused = true;
        }
        check(refused, "Invalid mesh topology accepted");
    });
    check(bytes(d) == original, "Failed mesh edit changed source");
    bool rejected = false;
    try {
        MeshDefinition::create_plane({}, Vector3::UNIT_X, Vector3::UNIT_Y, 0, 3);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "Zero subdivision accepted");
    // Count-driven reads must reject truncation before allocating from the count.
    std::istringstream broken("0\nSECTION\n2\nENTITIES\n0\nMESH\n5\nA\n100\nAcDbEntity\n8\n0\n100\nAcDbSubDMe"
                              "sh\n71\n2\n72\n1\n91\n0\n92\n2147483647\n0\nENDSEC\n0\nEOF\n");
    auto broken_doc = DxfDocument::read(broken);
    std::ostringstream encoded;
    broken_doc.write(encoded, binary);
    std::istringstream wire(encoded.str());
    auto truncated = DxfDocument::read(wire);
    unsigned invalid_count = 0;
    truncated.for_each<DxfMesh>([&](const auto &e) {
        bool refused = false;
        try {
            (void)e.definition();
        } catch (const std::runtime_error &) {
            refused = true;
        }
        check(refused, "Truncated MESH count accepted");
        ++invalid_count;
    });
    check(invalid_count == 1, "Malformed mesh missing");
    d.for_each<DxfMesh>([&](auto &e) {
        auto original = e.definition();
        for (int kind = 0; kind < 3; ++kind) {
            auto invalid = original;
            if (kind == 0)
                invalid.override_option = 1;
            if (kind == 1)
                invalid.faces.push_back({{}});
            if (kind == 2)
                invalid.edges.push_back({invalid.vertices.size(), 0, {}});
            bool refused = false;
            try {
                e.set_definition(invalid);
            } catch (const std::invalid_argument &) {
                refused = true;
            }
            check(refused, "Invalid MESH definition accepted");
        }
    });
    check(bytes(d) == original, "Rejected MESH definition changed document");
    auto first = d.model_space_entity_handles().front();
    auto copy = d.copy_entity(first, EntitySpace::Paper);
    check(d.paper_space_entity_handles().size() == 1, "Mesh copy owner");
    check(d.remove_entity(copy), "Mesh deletion failed");
    check(d.paper_space_entity_handles().empty(), "Deleted mesh member retained");
    std::istringstream old("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1021\n0\nENDSEC\n0\nEOF\n");
    auto legacy = DxfDocument::read(old);
    rejected = false;
    try {
        legacy.add_mesh(cube);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "Legacy MESH accepted");
    std::ofstream out(argv[2], std::ios::binary);
    d.write(out);
    out.close();
    check(bool(out), "Save failed");
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
