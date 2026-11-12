// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{
    PolygonMeshEntity, PolygonMeshFlags, PolygonMeshVertex, SurfaceSmoothType,
};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Layer, Vector3};

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(Layer::new("网格"))?;
    doc.layers.add(Layer::new("测点"))?;
    for (rows, cols, offset, closure) in [
        (3, 4, 0., PolygonMeshFlags::CLOSED_N),
        (4, 3, 20., PolygonMeshFlags::CLOSED_M),
    ] {
        let mut mesh = PolygonMeshEntity::new();
        mesh.common.layer = "网格".into();
        mesh.m_vertex_count = rows;
        mesh.n_vertex_count = cols;
        mesh.flags.insert(closure);
        mesh.m_smooth_density = 4;
        mesh.n_smooth_density = 6;
        mesh.smooth_type = SurfaceSmoothType::Cubic;
        for m in 0..rows {
            for n in 0..cols {
                let xyz = Vector3::new(offset + f64::from(m), f64::from(n), f64::from(10 * m - n));
                let mut v = PolygonMeshVertex::at(xyz);
                v.flags = 64;
                if (m, n) == (1, 2) {
                    v.common.layer = "测点".into();
                }
                mesh.vertices.push(v);
            }
        }
        doc.add_entity(EntityType::PolygonMesh(mesh))?;
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
        let (EntityType::PolygonMesh(src), EntityType::PolygonMesh(dst)) = (src, dst) else {
            panic!("expected two polygon meshes")
        };
        assert_eq!(
            (src.m_vertex_count, src.n_vertex_count),
            if i == 0 { (3, 4) } else { (4, 3) }
        );
        assert_eq!(src.vertices.len(), 12);
        let mut expected = src.clone();
        if i == 0 {
            assert_eq!(expected.vertices[6].location, Vector3::new(1., 2., 8.));
            expected.vertices[6].location = Vector3::new(1.5, 2.25, -7.);
        }
        assert_eq!(dst, &expected, "unexpected change to mesh {i}");
    }
    Ok(())
}
