// SPDX-License-Identifier: MPL-2.0
use acadrust::{Arc, CadDocument, DxfReader, DxfWriter, Ellipse, EntityType, Layer, Vector3};
use std::f64::consts::PI;

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(Layer::new("曲线"))?;
    let mut arc = Arc::from_coords(2., 3., 4., 5., 330_f64.to_radians(), 30_f64.to_radians());
    arc.common.layer = "曲线".into();
    doc.add_entity(EntityType::Arc(arc))?;
    let mut ellipse =
        Ellipse::from_center_axes(Vector3::new(9., 3., 2.), Vector3::new(4., 3., 0.), 0.6);
    ellipse.common.layer = "曲线".into();
    doc.add_entity(EntityType::Ellipse(ellipse))?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}

pub fn verify(before: &str, after: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(before)?.read()?;
    let saved = DxfReader::from_file(after)?.read()?;
    assert_eq!(original.version, saved.version);
    assert_eq!(
        saved.layers.get("曲线").unwrap().handle,
        original.layers.get("曲线").unwrap().handle
    );
    assert_eq!(original.entities().count(), 2);
    assert_eq!(saved.entities().count(), 2);
    let mut seen = [0; 2];
    for entity in saved.entities() {
        let src = original
            .entities()
            .find(|s| s.common().handle == entity.common().handle)
            .unwrap();
        assert_eq!(entity.common().owner_handle, src.common().owner_handle);
        assert_eq!(entity.common().layer, "曲线");
        match (src, entity) {
            (EntityType::Arc(_), EntityType::Arc(a)) => {
                seen[0] += 1;
                assert_eq!(a.center, Vector3::new(4., -2., 3.));
                assert_eq!(a.radius, 7.);
                assert_eq!(a.normal, Vector3::new(0., 1., 0.));
                assert_eq!(a.thickness, 1.5);
                assert!((a.start_angle - 1.5 * PI).abs() < 1e-12);
                assert!((a.end_angle - PI / 4.).abs() < 1e-12);
            }
            (EntityType::Ellipse(_), EntityType::Ellipse(e)) => {
                seen[1] += 1;
                assert_eq!(e.center, Vector3::new(1., 2., 3.));
                assert_eq!(e.major_axis, Vector3::new(6., 0., 2.));
                assert_eq!(e.minor_axis_ratio, 0.25);
                assert_eq!(e.normal, Vector3::new(0., 1., 0.));
                assert_eq!((e.start_parameter, e.end_parameter), (0.125, 3.5));
            }
            _ => panic!("unexpected curve type"),
        }
    }
    assert_eq!(seen, [1, 1]);
    Ok(())
}
