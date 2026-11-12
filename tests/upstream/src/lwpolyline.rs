// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{LwPolyline, LwVertex};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Layer, Vector2};

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(Layer::new("轮廓"))?;
    let mut path = LwPolyline::new();
    path.common.layer = "轮廓".into();
    path.close();
    path.plinegen = true;
    path.elevation = 2.5;
    path.thickness = 0.25;
    for (i, (x, y, bulge)) in [(0., 0., 0.5), (8., 0., 0.), (8., 6., 0.), (0., 6., -0.25)]
        .into_iter()
        .enumerate()
    {
        let mut v = LwVertex::with_bulge(Vector2::new(x, y), bulge);
        v.vertex_id = 70001 + i as i32;
        v.start_width = i as f64 * 0.125;
        v.end_width = v.start_width + 0.25;
        path.add_vertex(v);
    }
    doc.add_entity(EntityType::LwPolyline(path))?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}

fn polyline(doc: &CadDocument) -> &LwPolyline {
    assert_eq!(doc.entities().count(), 1);
    match doc.entities().next().unwrap() {
        EntityType::LwPolyline(p) => p,
        _ => panic!("expected LWPOLYLINE"),
    }
}

pub fn verify(before: &str, after: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(before)?.read()?;
    let saved = DxfReader::from_file(after)?.read()?;
    assert_eq!(saved.version, original.version);
    assert_eq!(
        saved.layers.get("轮廓").unwrap().handle,
        original.layers.get("轮廓").unwrap().handle
    );
    let mut expected = polyline(&original).clone();
    assert!(expected.is_closed && expected.plinegen);
    assert_eq!(expected.vertex_count(), 4);
    expected.vertices[1].location = Vector2::new(10., 1.5);
    expected.vertices[0].bulge = -0.5;
    expected.vertices[3].bulge = 0.75;
    // All unchanged fields, including the ordered widths, IDs and handles, must match.
    assert_eq!(polyline(&saved), &expected);
    Ok(())
}
