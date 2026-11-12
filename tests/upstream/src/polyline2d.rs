// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{Polyline2D, Vertex2D, VertexFlags};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Layer, Vector3};

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(Layer::new("轮廓"))?;
    for (closed, coords) in [
        (true, &[(0., 0.), (8., 0.), (8., 6.), (0., 6.)][..]),
        (false, &[(12., 1.), (16., 2.), (21., 4.)][..]),
    ] {
        let mut path = Polyline2D::new();
        path.common.layer = "轮廓".into();
        path.start_width = 0.25;
        path.end_width = 0.5;
        if closed {
            path.close();
        }
        for (i, &(x, y)) in coords.iter().enumerate() {
            let mut v = Vertex2D::new(Vector3::new(x, y, 0.));
            if i == 0 {
                // A curve-fit tangent requires a heavy VERTEX in modern DXF.
                v.flags = VertexFlags::CURVE_FIT_TANGENT;
                v.curve_tangent = 0.5;
            } else {
                v.start_width = i as f64 * 0.75;
                v.end_width = i as f64 * 0.875;
            }
            if i == coords.len() - 1 && closed {
                v.bulge = -0.25;
            }
            path.add_vertex(v);
        }
        doc.add_entity(EntityType::Polyline2D(path))?;
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
        let (EntityType::Polyline2D(src), EntityType::Polyline2D(actual)) = (src, dst) else {
            panic!("expected traditional 2D polylines")
        };
        assert_eq!(src.is_closed(), i == 0);
        assert_eq!(src.vertices.len(), if i == 0 { 4 } else { 3 });
        let mut expected = src.clone();
        if i == 0 {
            expected.vertices[1].location = Vector3::new(10., 1.5, 0.);
            expected.vertices[3].bulge = 0.75;
        }
        assert_eq!(
            actual, &expected,
            "polyline {i}: geometry, widths, ownership or common data differ"
        );
    }
    Ok(())
}
