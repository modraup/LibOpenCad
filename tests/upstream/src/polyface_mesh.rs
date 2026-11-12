// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{PolyfaceFace, PolyfaceMesh};
use acadrust::{CadDocument, Color, DxfReader, DxfWriter, EntityType, Layer};

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(Layer::new("网面"))?;
    for x in [0., 20.] {
        let mut mesh = PolyfaceMesh::new();
        mesh.common.layer = "网面".into();
        for (dx, y, z) in [
            (0., 0., 0.),
            (4., 0., 0.),
            (4., 4., 0.),
            (0., 4., 0.),
            (2., 2., 3.),
        ] {
            mesh.add_vertex_xyz(x + dx, y, z);
        }
        let mut side = PolyfaceFace::triangle(1, -2, 5);
        side.color = Some(Color::from_index(3));
        side.common.layer = "网面".into();
        mesh.add_face(side);
        mesh.add_face(PolyfaceFace::quad(-1, 2, 3, 4));
        mesh.add_face(PolyfaceFace::quad(3, 4, -5, 5)); // repeated last vertex encodes a triangle
        doc.add_entity(EntityType::PolyfaceMesh(mesh))?;
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}

pub fn verify(before: &str, after: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(before)?.read()?;
    let saved = DxfReader::from_file(after)?.read()?;
    assert_eq!(original.version, saved.version);
    assert_eq!(original.entities().count(), 2);
    assert_eq!(saved.entities().count(), 2);
    for (i, (src, dst)) in original.entities().zip(saved.entities()).enumerate() {
        let (EntityType::PolyfaceMesh(src), EntityType::PolyfaceMesh(dst)) = (src, dst) else {
            panic!("expected two polyface meshes")
        };
        assert_eq!((src.vertex_count(), src.face_count()), (5, 3));
        let mut expected = src.clone();
        if i == 0 {
            let tri = &mut expected.faces[0];
            [tri.index1, tri.index2, tri.index3] = [-1, 3, -5];
            let quad = &mut expected.faces[1];
            [quad.index1, quad.index2, quad.index3, quad.index4] = [2, -3, 4, -1];

            let tri = &dst.faces[0];
            assert!(tri.is_triangle());
            assert_eq!(tri.vertex_indices(), [1, 3, 5]);
            assert!(
                tri.is_edge1_invisible() && !tri.is_edge2_invisible() && tri.is_edge3_invisible()
            );
            let quad = &dst.faces[1];
            assert!(quad.is_quad());
            assert_eq!(quad.vertex_indices(), [2, 3, 4, 1]);
            assert!(
                !quad.is_edge1_invisible()
                    && quad.is_edge2_invisible()
                    && !quad.is_edge3_invisible()
                    && quad.is_edge4_invisible()
            );
        }
        assert!(dst.faces[2].is_triangle());
        assert_eq!(dst, &expected, "unexpected change to polyface mesh {i}");
    }
    Ok(())
}
