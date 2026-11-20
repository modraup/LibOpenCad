// SPDX-License-Identifier: MPL-2.0
#include <libopencad/import.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace libopencad;

static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}

static std::string bytes(const DxfDocument &d) {
    std::ostringstream o;
    d.write(o);
    return o.str();
}

static void write_file(const std::string &path, const std::string &data) {
    std::ofstream(path, std::ios::binary) << data;
}

static const char *kAsciiCube = R"(solid cube
  facet normal 0 0 -1
    outer loop
      vertex 0 0 0
      vertex 0 1 0
      vertex 1 1 0
    endloop
  endfacet
  facet normal 0 0 -1
    outer loop
      vertex 0 0 0
      vertex 1 1 0
      vertex 1 0 0
    endloop
  endfacet
  facet normal 0 0 1
    outer loop
      vertex 0 0 1
      vertex 1 1 1
      vertex 0 1 1
    endloop
  endfacet
  facet normal 0 0 1
    outer loop
      vertex 0 0 1
      vertex 1 0 1
      vertex 1 1 1
    endloop
  endfacet
  facet normal 0 -1 0
    outer loop
      vertex 0 0 0
      vertex 1 0 0
      vertex 1 0 1
    endloop
  endfacet
  facet normal 0 -1 0
    outer loop
      vertex 0 0 0
      vertex 1 0 1
      vertex 0 0 1
    endloop
  endfacet
  facet normal 1 0 0
    outer loop
      vertex 1 0 0
      vertex 1 1 0
      vertex 1 1 1
    endloop
  endfacet
  facet normal 1 0 0
    outer loop
      vertex 1 0 0
      vertex 1 1 1
      vertex 1 0 1
    endloop
  endfacet
  facet normal 0 1 0
    outer loop
      vertex 0 1 0
      vertex 0 1 1
      vertex 1 1 1
    endloop
  endfacet
  facet normal 0 1 0
    outer loop
      vertex 0 1 0
      vertex 1 1 1
      vertex 1 1 0
    endloop
  endfacet
  facet normal -1 0 0
    outer loop
      vertex 0 0 0
      vertex 0 0 1
      vertex 0 1 1
    endloop
  endfacet
  facet normal -1 0 0
    outer loop
      vertex 0 0 0
      vertex 0 1 1
      vertex 0 1 0
    endloop
  endfacet
endsolid cube
)";

static std::vector<std::uint8_t> binary_stl(const std::string &header,
                                            const std::vector<std::array<float, 12>> &triangles,
                                            std::uint16_t attribute = 0) {
    std::vector<std::uint8_t> data(80, 0);
    for (std::size_t i = 0; i < header.size() && i < 80; ++i)
        data[i] = static_cast<std::uint8_t>(header[i]);
    const auto count = static_cast<std::uint32_t>(triangles.size());
    data.insert(data.end(), {static_cast<std::uint8_t>(count), static_cast<std::uint8_t>(count >> 8),
                             static_cast<std::uint8_t>(count >> 16), static_cast<std::uint8_t>(count >> 24)});
    for (const auto &tri : triangles) {
        for (float v : tri) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &v, 4);
            data.push_back(static_cast<std::uint8_t>(bits));
            data.push_back(static_cast<std::uint8_t>(bits >> 8));
            data.push_back(static_cast<std::uint8_t>(bits >> 16));
            data.push_back(static_cast<std::uint8_t>(bits >> 24));
        }
        data.push_back(static_cast<std::uint8_t>(attribute));
        data.push_back(static_cast<std::uint8_t>(attribute >> 8));
    }
    return data;
}

static std::size_t mesh_count(const DxfDocument &d) {
    std::size_t n = 0;
    d.for_each<DxfMesh>([&](const auto &) { ++n; });
    return n;
}

static DxfMesh only_mesh(const DxfDocument &d, const char *label) {
    const DxfMesh *found = nullptr;
    d.for_each<DxfMesh>([&](const auto &e) {
        check(!found, (std::string("expected one mesh: ") + label).c_str());
        found = &e;
    });
    check(found, (std::string("missing mesh: ") + label).c_str());
    return *found;
}

int main(int argc, char **argv) try {
    if (argc != 2)
        return 2;
    const std::string prefix = argv[1];

    {
        auto doc = StlImporter::from_bytes(std::string_view(kAsciiCube)).import();
        auto mesh = only_mesh(doc, "cube");
        auto def = mesh.definition();
        check(def.faces.size() == 12, "ascii cube faces");
        check(def.vertices.size() == 8, "ascii cube welded vertices");
        check(mesh.layer().find("cube") != std::string::npos, "ascii cube layer");
        write_file(prefix + "-cube.dxf", bytes(doc));
        write_file(prefix + "-cube.stl", kAsciiCube);
    }
    {
        ImportConfig config;
        config.merge_vertices = false;
        auto doc = StlImporter::from_bytes(std::string_view(kAsciiCube)).with_config(config).import();
        auto def = only_mesh(doc, "nomerge").definition();
        check(def.faces.size() == 12 && def.vertices.size() == 36, "ascii cube without merge");
        write_file(prefix + "-cube-nomerge.dxf", bytes(doc));
    }
    {
        ImportConfig config;
        config.scale_factor = 2.5;
        config.merge_vertices = false;
        const char *stl =
            "solid s\nfacet normal 0 0 1\nouter loop\nvertex 1 2 3\nvertex 4 5 6\nvertex 7 8 9\nendloop\n"
            "endfacet\nendsolid s\n";
        auto doc = StlImporter::from_bytes(std::string_view(stl)).with_config(config).import();
        auto def = only_mesh(doc, "scale").definition();
        check(def.vertices.size() == 3, "scale vertex count");
        check(def.vertices[0].x == 2.5 && def.vertices[0].y == 5.0 && def.vertices[0].z == 7.5, "scale v0");
        check(def.vertices[1].x == 10.0 && def.vertices[1].y == 12.5 && def.vertices[1].z == 15.0, "scale v1");
        write_file(prefix + "-scale.dxf", bytes(doc));
        write_file(prefix + "-scale.stl", stl);
    }
    {
        auto empty = StlImporter::from_bytes(std::string_view("solid empty\nendsolid empty\n")).import();
        check(mesh_count(empty) == 0, "empty ascii solid");
        auto binary_empty = StlImporter::from_bytes(binary_stl("binary stl test", {})).import();
        check(mesh_count(binary_empty) == 0, "empty binary stl");
        write_file(prefix + "-empty.dxf", bytes(binary_empty));
    }
    {
        const char *sci =
            "solid sci\nfacet normal 0.0e+00 0.0e+00 1.0e+00\nouter loop\nvertex 1.5e+01 2.3e-02 0.0e+00\n"
            "vertex 1.0e+00 0.0e+00 0.0e+00\nvertex 0.0e+00 1.0e+00 0.0e+00\nendloop\nendfacet\nendsolid sci\n";
        auto def = only_mesh(StlImporter::from_bytes(std::string_view(sci)).import(), "scientific").definition();
        check(def.faces.size() == 1, "scientific faces");
        check(std::fabs(def.vertices[0].x - 15.0) < 1e-10 && std::fabs(def.vertices[0].y - 0.023) < 1e-10,
              "scientific vertex");
    }
    {
        std::vector<std::array<float, 12>> tris = {{
                                                       0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0,
                                                   },
                                                   {
                                                       0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0,
                                                   }};
        auto data = binary_stl("binary stl test", tris);
        auto doc = StlImporter::from_bytes(data).import();
        auto def = only_mesh(doc, "binary").definition();
        check(def.faces.size() == 2 && def.vertices.size() == 4, "binary welded quad");
        write_file(prefix + "-binary.dxf", bytes(doc));
        write_file(prefix + "-binary.stl", std::string(data.begin(), data.end()));
    }
    {
        std::vector<std::array<float, 12>> tris = {{0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0}};
        const std::uint16_t attr = 0x8000 | (31 << 10);
        auto data = binary_stl("colored", tris, attr);
        auto doc = StlImporter::from_bytes(data).import();
        auto mesh = only_mesh(doc, "color");
        check(mesh.color() == Color::from_rgb(255, 0, 0), "binary facet color");
        check(mesh.layer() == "imported_colored_255_0_0", "colored layer");
        write_file(prefix + "-color.dxf", bytes(doc));
        write_file(prefix + "-color.stl", std::string(data.begin(), data.end()));
    }
    {
        auto data = binary_stl("solid name", {{{0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0}}});
        check(data.size() == 134, "one-triangle binary size");
        auto doc = StlImporter::from_bytes(data).import();
        check(mesh_count(doc) == 1, "solid-prefix binary stays binary");
        data.push_back(0);
        check(mesh_count(StlImporter::from_bytes(data).import()) == 1, "solid-prefix binary with trailer");
    }
    {
        bool refused = false;
        try {
            StlImporter::from_bytes(std::string_view("solid x\nfacet normal 0 0 1\nendsolid x\n")).import();
        } catch (const std::runtime_error &) {
            refused = true;
        }
        check(refused, "truncated facet accepted");
    }
    {
        auto short_bin = binary_stl("x", {{{0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0}}});
        short_bin.resize(100);
        bool refused = false;
        try {
            StlImporter::from_bytes(short_bin).import();
        } catch (const std::runtime_error &) {
            refused = true;
        }
        check(refused, "truncated binary accepted");
    }
    {
        check(detect_format("model.STL") == ImportFormat::Stl, "stl extension");
        check(detect_format("scene.glb") == ImportFormat::Glb, "glb extension");
        bool unsupported = false;
        try {
            detect_format("notes.txt");
        } catch (const std::runtime_error &e) {
            unsupported = std::string(e.what()).find("Unsupported import format") != std::string::npos;
        }
        check(unsupported, "unknown extension");
        auto from_path = import_file(prefix + "-cube.stl");
        check(only_mesh(from_path, "import_file").definition().faces.size() == 12, "import_file cube");
    }
    {
        ImportConfig config;
        config.merge_vertices = true;
        config.merge_tolerance = 1.0;
        const char *stl =
            "solid m\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 3 0 0\nvertex 0 3 0\nendloop\n"
            "endfacet\nfacet normal 0 0 1\nouter loop\nvertex 0.6 0 0\nvertex 3 0 0\nvertex 0 3 0\nendloop\n"
            "endfacet\nendsolid m\n";
        auto def = only_mesh(StlImporter::from_bytes(std::string_view(stl)).with_config(config).import(), "weld").definition();
        check(def.vertices.size() == 3 && def.faces.size() == 2, "cell-boundary weld");
    }

    const char *kObjTriangle = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    {
        auto doc = ObjImporter::from_bytes(std::string_view(kObjTriangle)).import();
        auto def = only_mesh(doc, "obj").definition();
        check(def.vertices.size() == 3 && def.faces.size() == 1, "obj triangle");
        write_file(prefix + "-obj.dxf", bytes(doc));
        write_file(prefix + "-obj.obj", kObjTriangle);
        auto from_path = import_file(prefix + "-obj.obj");
        check(only_mesh(from_path, "import_file obj").definition().faces.size() == 1, "import_file obj");
    }
    {
        const char *obj = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n";
        auto def = only_mesh(ObjImporter::from_bytes(std::string_view(obj)).import(), "obj-quad").definition();
        check(def.faces.size() == 2, "obj fan quad");
    }
    {
        const char *obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 2 0 0\nv 2 1 0\nv 1 1 0\n"
                          "usemtl Red\nf 1 2 3\nusemtl Blue\nf 4 5 6\n";
        auto doc = ObjImporter::from_bytes(std::string_view(obj)).import();
        check(mesh_count(doc) == 2, "obj usemtl groups");
    }
    {
        const char *obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n";
        check(only_mesh(ObjImporter::from_bytes(std::string_view(obj)).import(), "obj-neg").definition().faces.size() == 1,
              "obj negative indices");
    }
    {
        const char *obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99\n";
        check(mesh_count(ObjImporter::from_bytes(std::string_view(obj)).import()) == 0, "obj missing index");
    }

    const char *kDae = R"(<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <library_effects>
    <effect id="eff0">
      <profile_COMMON><technique sid="t"><phong>
        <diffuse><color>0.0 0.0 1.0 1.0</color></diffuse>
      </phong></technique></profile_COMMON>
    </effect>
  </library_effects>
  <library_materials>
    <material id="mat0" name="Blue">
      <instance_effect url="#eff0"/>
    </material>
  </library_materials>
  <library_geometries>
    <geometry id="g0" name="Tri">
      <mesh>
        <source id="p0">
          <float_array id="p0a" count="9">0 0 0 1 0 0 0 1 0</float_array>
          <technique_common>
            <accessor source="#p0a" count="3" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <vertices id="v0">
          <input semantic="POSITION" source="#p0"/>
        </vertices>
        <triangles count="1" material="ms">
          <input semantic="VERTEX" source="#v0" offset="0"/>
          <p>0 1 2</p>
        </triangles>
      </mesh>
    </geometry>
  </library_geometries>
  <library_visual_scenes>
    <visual_scene id="Scene">
      <node name="N">
        <instance_geometry url="#g0">
          <bind_material><technique_common>
            <instance_material symbol="ms" target="#mat0"/>
          </technique_common></bind_material>
        </instance_geometry>
      </node>
    </visual_scene>
  </library_visual_scenes>
</COLLADA>)";
    {
        auto doc = ColladaImporter::from_bytes(std::string_view(kDae)).import();
        auto mesh = only_mesh(doc, "dae");
        check(mesh.definition().vertices.size() == 3 && mesh.definition().faces.size() == 1, "collada triangle");
        check(mesh.layer().find("Blue") != std::string::npos, "collada blue layer");
        check(mesh.color() == Color::from_rgb(0, 0, 255), "collada blue color");
        write_file(prefix + "-dae.dxf", bytes(doc));
        write_file(prefix + "-dae.dae", kDae);
        auto from_path = import_file(prefix + "-dae.dae");
        check(only_mesh(from_path, "import_file dae").layer().find("Blue") != std::string::npos, "import_file dae");
    }
    {
        const char *nested = R"(<?xml version="1.0"?>
<COLLADA>
  <library_geometries>
    <geometry id="geom0" name="Triangle">
      <mesh>
        <source id="p0">
          <float_array id="p0a" count="9">0 0 0 1 0 0 0 1 0</float_array>
        </source>
        <vertices id="v0">
          <input semantic="POSITION" source="#p0"/>
        </vertices>
        <triangles count="1">
          <input semantic="VERTEX" source="#v0" offset="0"/>
          <p>0 1 2</p>
        </triangles>
      </mesh>
    </geometry>
  </library_geometries>
  <library_visual_scenes>
    <visual_scene id="Scene">
      <node name="Parent">
        <translate>10 0 0</translate>
        <node name="Child">
          <instance_geometry url="#geom0"/>
        </node>
      </node>
    </visual_scene>
  </library_visual_scenes>
</COLLADA>)";
        auto def = only_mesh(ColladaImporter::from_bytes(std::string_view(nested)).import(), "dae-nested").definition();
        bool moved = false;
        for (const auto &v : def.vertices)
            if (std::fabs(v.x - 10.0) < 1e-9 && std::fabs(v.y) < 1e-9 && std::fabs(v.z) < 1e-9)
                moved = true;
        check(moved, "collada nested translate");
        write_file(prefix + "-dae-nested.dxf", bytes(ColladaImporter::from_bytes(std::string_view(nested)).import()));
        write_file(prefix + "-dae-nested.dae", nested);
    }

    auto b64_encode = [](const std::vector<std::uint8_t> &data) {
        static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < data.size(); i += 3) {
            const unsigned b0 = data[i];
            const unsigned b1 = i + 1 < data.size() ? data[i + 1] : 0;
            const unsigned b2 = i + 2 < data.size() ? data[i + 2] : 0;
            const unsigned n = (b0 << 16) | (b1 << 8) | b2;
            out.push_back(table[(n >> 18) & 63]);
            out.push_back(table[(n >> 12) & 63]);
            out.push_back(i + 1 < data.size() ? table[(n >> 6) & 63] : '=');
            out.push_back(i + 2 < data.size() ? table[n & 63] : '=');
        }
        return out;
    };
    auto gltf_triangle_json = [&](bool with_node) {
        std::vector<std::uint8_t> buf;
        const float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        const std::uint16_t indices[3] = {0, 1, 2};
        for (float p : positions) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &p, 4);
            buf.push_back(static_cast<std::uint8_t>(bits));
            buf.push_back(static_cast<std::uint8_t>(bits >> 8));
            buf.push_back(static_cast<std::uint8_t>(bits >> 16));
            buf.push_back(static_cast<std::uint8_t>(bits >> 24));
        }
        for (auto i : indices) {
            buf.push_back(static_cast<std::uint8_t>(i));
            buf.push_back(static_cast<std::uint8_t>(i >> 8));
        }
        while (buf.size() % 4)
            buf.push_back(0);
        const auto uri = "data:application/octet-stream;base64," + b64_encode(buf);
        std::string json = std::string("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"") + uri +
                           "\",\"byteLength\":" + std::to_string(buf.size()) +
                           "}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
                           "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
                           "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                           "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
                           "\"meshes\":[{\"name\":\"Triangle\",\"primitives\":[{\"attributes\":{\"POSITION\":0},"
                           "\"indices\":1}]}]";
        if (with_node)
            json += ",\"nodes\":[{\"mesh\":0,\"translation\":[10,0,0]}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0";
        json += "}";
        return json;
    };
    {
        auto json = gltf_triangle_json(false);
        auto doc = GltfImporter::from_bytes(std::string_view(json)).import();
        auto def = only_mesh(doc, "gltf").definition();
        check(def.vertices.size() == 3 && def.faces.size() == 1, "gltf triangle");
        write_file(prefix + "-gltf.dxf", bytes(doc));
        write_file(prefix + "-gltf.gltf", json);
    }
    {
        auto json = gltf_triangle_json(true);
        auto def = only_mesh(GltfImporter::from_bytes(std::string_view(json)).import(), "gltf-node").definition();
        bool moved = false;
        for (const auto &v : def.vertices)
            if (std::fabs(v.x - 10.0) < 1e-5 && v.y == 0.0)
                moved = true;
        check(moved, "gltf node translation");
        write_file(prefix + "-gltf-node.dxf", bytes(GltfImporter::from_bytes(std::string_view(json)).import()));
        write_file(prefix + "-gltf-node.gltf", json);
    }
    {
        const char *huge = R"({
  "asset": {"version": "2.0"},
  "buffers": [{"uri": "data:application/octet-stream;base64,AAAA", "byteLength": 4}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 4}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 2000000000, "type": "VEC3"}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}]
})";
        check(mesh_count(GltfImporter::from_bytes(std::string_view(huge)).import()) == 0, "gltf huge count");
    }
    {
        std::string nested;
        for (int i = 0; i < 200; ++i)
            nested.push_back('[');
        for (int i = 0; i < 200; ++i)
            nested.push_back(']');
        bool refused = false;
        try {
            GltfImporter::from_bytes(std::string_view(nested)).import();
        } catch (const std::runtime_error &e) {
            refused = std::string(e.what()).find("recursion limit exceeded") != std::string::npos;
        }
        check(refused, "gltf recursion limit");
    }

    const char *kFbx = R"(Objects: {
    Geometry: 100, "Geometry::Mesh", "Mesh" {
        Vertices: *9 {
            a: 0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0
        }
        PolygonVertexIndex: *3 {
            a: 0,1,-3
        }
    }
}
)";
    {
        auto def = only_mesh(FbxImporter::from_bytes(std::string_view(kFbx)).import(), "fbx").definition();
        check(def.vertices.size() == 3 && def.faces.size() == 1, "ascii fbx triangle");
        write_file(prefix + "-fbx.dxf", bytes(FbxImporter::from_bytes(std::string_view(kFbx)).import()));
        write_file(prefix + "-fbx.fbx", kFbx);
    }
    {
        const char *parent = R"(Objects: {
    Geometry: 10, "Geometry::Tri", "Mesh" {
        Vertices: *9 {
            a: 0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0
        }
        PolygonVertexIndex: *3 {
            a: 0,1,-3
        }
    }
    Model: 20, "Model::Child", "Mesh" {
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A", 1.0, 0.0, 0.0
        }
    }
    Model: 30, "Model::Parent", "Null" {
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A", 10.0, 0.0, 0.0
        }
    }
}
Connections: {
    C: "OO", 10, 20
    C: "OO", 20, 30
}
)";
        auto def = only_mesh(FbxImporter::from_bytes(std::string_view(parent)).import(), "fbx-parent").definition();
        bool moved = false;
        for (const auto &v : def.vertices)
            if (std::fabs(v.x - 11.0) < 1e-8 && v.y == 0.0 && v.z == 0.0)
                moved = true;
        check(moved, "fbx parent translation");
        write_file(prefix + "-fbx-parent.dxf", bytes(FbxImporter::from_bytes(std::string_view(parent)).import()));
        write_file(prefix + "-fbx-parent.fbx", parent);
    }
    {
        std::vector<std::uint8_t> data;
        const char magic[] = "Kaydara FBX Binary  ";
        data.insert(data.end(), magic, magic + 20);
        data.push_back(0);
        data.push_back(0x1a);
        data.push_back(0);
        const std::uint32_t version = 7300;
        data.push_back(static_cast<std::uint8_t>(version));
        data.push_back(static_cast<std::uint8_t>(version >> 8));
        data.push_back(static_cast<std::uint8_t>(version >> 16));
        data.push_back(static_cast<std::uint8_t>(version >> 24));
        const std::uint32_t end = 27;
        data.push_back(static_cast<std::uint8_t>(end));
        data.push_back(static_cast<std::uint8_t>(end >> 8));
        data.push_back(static_cast<std::uint8_t>(end >> 16));
        data.push_back(static_cast<std::uint8_t>(end >> 24));
        data.insert(data.end(), 8, 0);
        data.push_back(1);
        data.push_back('A');
        bool refused = false;
        try {
            FbxImporter::from_bytes(data).import();
        } catch (const std::runtime_error &) {
            refused = true;
        }
        check(refused, "fbx corrupt offset");
    }

    std::cout << "stl import passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
