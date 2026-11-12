// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::Polyline3D;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Layer, Vector3};

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(Layer::new("路径"))?;
    doc.layers.add(Layer::new("测点"))?;
    let paths: &[&[(f64, f64, f64)]] = &[
        &[(0., 0., -2.), (8., 0., 3.), (8., 6., 7.5), (0., 6., 1.)],
        &[(12., 1., 11.), (16., 2., -4.5), (21., 4., 9.)],
    ];
    for (index, points) in paths.iter().enumerate() {
        let mut path = Polyline3D::new();
        path.common.layer = "路径".into();
        if index == 0 {
            path.close();
        }
        for &(x, y, z) in *points {
            path.add_vertex(Vector3::new(x, y, z));
        }
        path.vertices[1].layer = "测点".into();
        doc.add_entity(EntityType::Polyline3D(path))?;
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
        let (EntityType::Polyline3D(src), EntityType::Polyline3D(actual)) = (src, dst) else {
            panic!("expected traditional 3D polylines")
        };
        assert_eq!(src.flags.closed, i == 0);
        assert_eq!(src.vertex_count(), if i == 0 { 4 } else { 3 });
        let mut expected = src.clone();
        if i == 0 {
            assert_eq!(expected.vertices[1].position, Vector3::new(8., 0., 3.));
            expected.vertices[1].position = Vector3::new(10., 1.5, -6.25);
        }
        // Equality includes parent handle/owner and ordered vertex handles, layers, flags and XYZ.
        assert_eq!(actual, &expected, "unexpected change to 3D polyline {i}");
    }
    Ok(())
}
