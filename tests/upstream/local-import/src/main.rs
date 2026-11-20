// SPDX-License-Identifier: MPL-2.0
// Standalone oracle against the local acadrust checkout (Stage 7 import).
use acadrust::entities::EntityType;
use acadrust::io::import::collada::ColladaImporter;
use acadrust::io::import::fbx::FbxImporter;
use acadrust::io::import::gltf::GltfImporter;
use acadrust::io::import::obj::ObjImporter;
use acadrust::io::import::stl::StlImporter;
use acadrust::io::import::ImportConfig;
use acadrust::types::Color;
use acadrust::DxfReader;
use std::env;
use std::fs;
use std::path::Path;

fn read_dxf(path: &Path) -> acadrust::CadDocument {
    DxfReader::from_file(path).unwrap().read().unwrap()
}

struct MeshView {
    layer: String,
    color: Color,
    vertices: Vec<(f64, f64, f64)>,
    faces: Vec<Vec<usize>>,
}

fn meshes(doc: &acadrust::CadDocument) -> Vec<MeshView> {
    doc.entities()
        .filter_map(|entity| match entity {
            EntityType::Mesh(mesh) => Some(MeshView {
                layer: mesh.common.layer.clone(),
                color: mesh.common.color,
                vertices: mesh.vertices.iter().map(|v| (v.x, v.y, v.z)).collect(),
                faces: mesh.faces.iter().map(|f| f.vertices.clone()).collect(),
            }),
            _ => None,
        })
        .collect()
}

fn near(a: f64, b: f64) -> bool {
    (a - b).abs() <= 1e-9 || ((a - b).abs() / a.abs().max(b.abs()).max(1.0)) <= 1e-9
}

fn same_mesh(native: &MeshView, rust: &MeshView, label: &str) {
    assert_eq!(native.layer, rust.layer, "{label} layer");
    assert_eq!(native.color, rust.color, "{label} color");
    assert_eq!(native.faces, rust.faces, "{label} faces");
    assert_eq!(native.vertices.len(), rust.vertices.len(), "{label} vertex count");
    for (i, (a, b)) in native.vertices.iter().zip(&rust.vertices).enumerate() {
        assert!(near(a.0, b.0) && near(a.1, b.1) && near(a.2, b.2), "{label} vertex {i}");
    }
}

fn file(prefix: &str, stem: &str, ext: &str) -> std::path::PathBuf {
    Path::new(&format!("{prefix}-{stem}.{ext}")).to_path_buf()
}

fn compare_docs(native: acadrust::CadDocument, rust: acadrust::CadDocument, label: &str) {
    let rust_meshes = meshes(&rust);
    let native_meshes = meshes(&native);
    assert_eq!(native_meshes.len(), rust_meshes.len(), "{label} mesh count");
    for (native_mesh, rust_mesh) in native_meshes.iter().zip(&rust_meshes) {
        same_mesh(native_mesh, rust_mesh, label);
    }
}

fn compare_stl(prefix: &str, stem: &str, config: ImportConfig) {
    let bytes = fs::read(file(prefix, stem, "stl")).unwrap();
    let rust = StlImporter::from_bytes(bytes).with_config(config).import().unwrap();
    let native = read_dxf(&file(prefix, stem, "dxf"));
    compare_docs(native, rust, stem);
}

fn main() {
    let prefix = env::args().nth(1).expect("output prefix");
    compare_stl(&prefix, "cube", ImportConfig::default());
    let mut no_merge = ImportConfig::default();
    no_merge.merge_vertices = false;
    let rust = StlImporter::from_bytes(fs::read(file(&prefix, "cube", "stl")).unwrap())
        .with_config(no_merge)
        .import()
        .unwrap();
    let native = read_dxf(&file(&prefix, "cube-nomerge", "dxf"));
    same_mesh(&meshes(&native)[0], &meshes(&rust)[0], "cube-nomerge");
    let mut scale = ImportConfig::default();
    scale.scale_factor = 2.5;
    scale.merge_vertices = false;
    compare_stl(&prefix, "scale", scale);
    compare_stl(&prefix, "binary", ImportConfig::default());
    compare_stl(&prefix, "color", ImportConfig::default());
    let empty = read_dxf(&file(&prefix, "empty", "dxf"));
    assert!(meshes(&empty).is_empty(), "empty binary");

    compare_docs(
        read_dxf(&file(&prefix, "obj", "dxf")),
        ObjImporter::from_bytes(fs::read(file(&prefix, "obj", "obj")).unwrap())
            .import()
            .unwrap(),
        "obj",
    );
    compare_docs(
        read_dxf(&file(&prefix, "dae", "dxf")),
        ColladaImporter::from_bytes(fs::read(file(&prefix, "dae", "dae")).unwrap())
            .import()
            .unwrap(),
        "dae",
    );
    compare_docs(
        read_dxf(&file(&prefix, "dae-nested", "dxf")),
        ColladaImporter::from_bytes(fs::read(file(&prefix, "dae-nested", "dae")).unwrap())
            .import()
            .unwrap(),
        "dae-nested",
    );
    compare_docs(
        read_dxf(&file(&prefix, "gltf", "dxf")),
        GltfImporter::from_bytes(fs::read(file(&prefix, "gltf", "gltf")).unwrap())
            .import()
            .unwrap(),
        "gltf",
    );
    compare_docs(
        read_dxf(&file(&prefix, "gltf-node", "dxf")),
        GltfImporter::from_bytes(fs::read(file(&prefix, "gltf-node", "gltf")).unwrap())
            .import()
            .unwrap(),
        "gltf-node",
    );
    compare_docs(
        read_dxf(&file(&prefix, "fbx", "dxf")),
        FbxImporter::from_bytes(fs::read(file(&prefix, "fbx", "fbx")).unwrap())
            .import()
            .unwrap(),
        "fbx",
    );
    compare_docs(
        read_dxf(&file(&prefix, "fbx-parent", "dxf")),
        FbxImporter::from_bytes(fs::read(file(&prefix, "fbx-parent", "fbx")).unwrap())
            .import()
            .unwrap(),
        "fbx-parent",
    );
    println!("import oracle passed");
}
