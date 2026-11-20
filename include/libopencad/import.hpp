// SPDX-License-Identifier: MPL-2.0
// Port of acadrust src/io/import. STL, COLLADA, OBJ, glTF/GLB and FBX.
#pragma once

#include "dxf_document.hpp"

#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace libopencad {

enum class ImportFormat { Stl, Collada, Obj, Gltf, Glb, Fbx };

struct ImportConfig {
    std::string layer_prefix = "imported";
    Color default_color = Color::by_layer();
    bool merge_vertices = true;
    double merge_tolerance = 1e-9;
    double scale_factor = 1.0;
};

ImportFormat detect_format(const std::filesystem::path &path);
DxfDocument import_file(const std::filesystem::path &path, const ImportConfig &config = {});

class StlImporter {
public:
    static StlImporter from_file(const std::filesystem::path &path);
    static StlImporter from_bytes(std::vector<std::uint8_t> data);
    static StlImporter from_bytes(std::string_view text);
    static StlImporter from_reader(std::istream &input);
    StlImporter &with_config(ImportConfig config);
    DxfDocument import() const;

private:
    std::vector<std::uint8_t> data_;
    ImportConfig config_{};
    explicit StlImporter(std::vector<std::uint8_t> data);
};

class ObjImporter {
public:
    static ObjImporter from_file(const std::filesystem::path &path);
    static ObjImporter from_bytes(std::vector<std::uint8_t> data);
    static ObjImporter from_bytes(std::string_view text);
    static ObjImporter from_reader(std::istream &input);
    ObjImporter &with_config(ImportConfig config);
    DxfDocument import() const;

private:
    std::vector<std::uint8_t> data_;
    std::optional<std::filesystem::path> base_dir_;
    ImportConfig config_{};
    explicit ObjImporter(std::vector<std::uint8_t> data, std::optional<std::filesystem::path> base_dir);
};

class ColladaImporter {
public:
    static ColladaImporter from_file(const std::filesystem::path &path);
    static ColladaImporter from_bytes(std::vector<std::uint8_t> data);
    static ColladaImporter from_bytes(std::string_view text);
    static ColladaImporter from_reader(std::istream &input);
    ColladaImporter &with_config(ImportConfig config);
    DxfDocument import() const;

private:
    std::vector<std::uint8_t> data_;
    ImportConfig config_{};
    explicit ColladaImporter(std::vector<std::uint8_t> data);
};

class GltfImporter {
public:
    static GltfImporter from_file(const std::filesystem::path &path);
    static GltfImporter from_bytes(std::vector<std::uint8_t> data);
    static GltfImporter from_bytes(std::string_view text);
    static GltfImporter from_reader(std::istream &input);
    GltfImporter &with_config(ImportConfig config);
    DxfDocument import() const;

private:
    std::vector<std::uint8_t> data_;
    std::optional<std::filesystem::path> base_dir_;
    ImportConfig config_{};
    explicit GltfImporter(std::vector<std::uint8_t> data, std::optional<std::filesystem::path> base_dir);
};

class FbxImporter {
public:
    static FbxImporter from_file(const std::filesystem::path &path);
    static FbxImporter from_bytes(std::vector<std::uint8_t> data);
    static FbxImporter from_bytes(std::string_view text);
    static FbxImporter from_reader(std::istream &input);
    FbxImporter &with_config(ImportConfig config);
    DxfDocument import() const;

private:
    std::vector<std::uint8_t> data_;
    ImportConfig config_{};
    explicit FbxImporter(std::vector<std::uint8_t> data);
};

} // namespace libopencad
