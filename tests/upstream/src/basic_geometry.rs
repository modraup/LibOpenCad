// SPDX-License-Identifier: MPL-2.0
// Reference: entities/{point,ray,xline}.rs and the DXF section reader/writer.
use acadrust::entities::{Point, Ray, XLine};
use acadrust::objects::XRecordValue;
use acadrust::{CadDocument, Color, DxfReader, DxfWriter, EntityType, Layer, LineWeight, Vector3};
use std::io::Cursor;

fn fixture() -> acadrust::Result<CadDocument> {
    let mut doc = CadDocument::new();
    doc.layers.add(Layer::new("几何"))?;
    for n in 0..2 {
        let offset = (10 * n) as f64;
        let mut point = Point::from_coords(1. + offset, 2., 3.);
        point.thickness = -2.5;
        point.normal = Vector3::new(0., 1., 0.);
        point.x_axis_angle = 1.25;
        let entities = [
            EntityType::Point(point),
            EntityType::Ray(Ray::new(
                Vector3::new(4. + offset, 5., 6.),
                Vector3::new(3., 4., 0.),
            )),
            EntityType::XLine(XLine::new(
                Vector3::new(7. + offset, 8., 9.),
                Vector3::new(0., 0., -5.),
            )),
        ];
        for mut entity in entities {
            let common = entity.common_mut();
            common.layer = "几何".into();
            common.linetype = "Continuous".into();
            common.color = Color::from_rgb(12, 34, 56);
            common.line_weight = LineWeight::from_value(35);
            common.invisible = n != 0;
            if n == 0 {
                doc.add_entity(entity)?;
            } else {
                doc.add_paper_space_entity(entity)?;
            }
        }
    }
    Ok(doc)
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let doc = fixture()?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}
fn close(a: Vector3, b: Vector3) {
    assert!((a - b).length() < 1e-12, "{a:?} != {b:?}");
}
fn geometry(a: &EntityType, b: &EntityType) {
    match (a, b) {
        (EntityType::Point(a), EntityType::Point(b)) => {
            close(a.location, b.location);
            close(a.normal, b.normal);
            assert_eq!(a.thickness, b.thickness);
            assert_eq!(a.x_axis_angle, b.x_axis_angle);
        }
        (EntityType::Ray(a), EntityType::Ray(b)) => {
            close(a.base_point, b.base_point);
            close(a.direction, b.direction);
        }
        (EntityType::XLine(a), EntityType::XLine(b)) => {
            close(a.base_point, b.base_point);
            close(a.direction, b.direction);
        }
        _ => panic!("unexpected geometry pair"),
    }
}
fn edit(entity: &mut EntityType, copy: bool) {
    match entity {
        EntityType::Point(p) => {
            p.location = if copy {
                Vector3::new(101., 102., 103.)
            } else {
                Vector3::new(-1., -2., -3.)
            };
            if !copy {
                p.thickness = 4.;
                p.normal = Vector3::new(1., 0., 0.);
                p.x_axis_angle = -0.75;
            }
        }
        EntityType::Ray(r) => {
            if !copy {
                r.base_point = Vector3::new(-4., -5., -6.);
            }
            r.set_direction(if copy {
                Vector3::ZERO
            } else {
                Vector3::new(0., 5., 12.)
            });
        }
        EntityType::XLine(x) => {
            if !copy {
                x.base_point = Vector3::new(-7., -8., -9.);
            }
            x.set_direction(if copy {
                Vector3::new(1., 2., 2.)
            } else {
                Vector3::ZERO
            });
        }
        _ => panic!(),
    }
}
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let load = |name: &str| DxfReader::from_file(format!("{prefix}-{name}.dxf"))?.read();
    let created = load("created")?;
    // Materialize writer defaults before comparing independently allocated documents.
    // Rust assigns table handles during serialization; C++ assigns them on insertion.
    let reference = fixture()?;
    let reference =
        DxfReader::from_reader(Cursor::new(DxfWriter::new(&reference).write_to_vec()?))?.read()?;
    assert_eq!(created.entities().count(), 6);
    for (a, b) in created.entities().zip(reference.entities()) {
        geometry(a, b);
        let mut expected = b.common().clone();
        expected.handle = a.common().handle;
        assert_eq!(a.common(), &expected);
    }
    let before = load("before")?;
    let edited = load("edited")?;
    let copied = load("copied")?;
    let restored = load("restored")?;
    let deleted = load("deleted")?;
    let converted = load("converted")?;
    for (doc, model, paper) in [
        (&before, 3, 3),
        (&edited, 3, 3),
        (&copied, 3, 6),
        (&restored, 3, 3),
        (&deleted, 0, 3),
    ] {
        assert_eq!(doc.entities_in_block("*Model_Space").count(), model);
        assert_eq!(doc.entities_in_block("*Paper_Space").count(), paper);
    }
    let model: Vec<_> = before
        .entities_in_block("*Model_Space")
        .map(|e| e.common().handle)
        .collect();
    for original in before.entities() {
        let h = original.common().handle;
        let mut expected = original.clone();
        if model.contains(&h) {
            edit(&mut expected, false);
        }
        let actual = edited.get_entity(h).unwrap();
        geometry(actual, &expected);
        assert_eq!(actual.common(), expected.common());
        assert_eq!(restored.get_entity(h), Some(actual));
        assert_eq!(copied.get_entity(h), Some(actual));
        assert_eq!(converted.get_entity(h), Some(actual));
        if !model.contains(&h) {
            assert_eq!(deleted.get_entity(h), Some(original));
        }
    }
    let new: Vec<_> = copied
        .entities()
        .filter(|e| before.get_entity(e.common().handle).is_none())
        .collect();
    assert_eq!(new.len(), 3);
    for (h, actual) in model.iter().zip(new) {
        let mut expected = edited.get_entity(*h).unwrap().clone();
        edit(&mut expected, true);
        geometry(actual, &expected);
        assert_eq!(
            actual.common().owner_handle,
            before
                .entities_in_block("*Paper_Space")
                .next()
                .unwrap()
                .common()
                .owner_handle
        );
        assert_eq!(actual.common().layer, expected.common().layer);
        assert_eq!(actual.common().color, expected.common().color);
        assert_eq!(actual.common().linetype, expected.common().linetype);
        assert_eq!(actual.common().line_weight, expected.common().line_weight);
        assert_eq!(actual.common().invisible, expected.common().invisible);
        let data = copied.xrecord(actual.common().handle, "Data").unwrap();
        assert_eq!(
            data.get_first_by_code(1).unwrap().value,
            XRecordValue::String("扩展数据".into())
        );
        assert_eq!(
            data.get_first_by_code(340).unwrap().value.as_handle(),
            Some(actual.common().handle)
        );
    }
    assert_eq!(restored.objects, edited.objects);
    // The pinned Rust removal path provides an independent cascade reference.
    let mut expected = edited.clone();
    for h in &model {
        expected.remove_entity(*h).unwrap();
    }
    assert_eq!(deleted.objects, expected.objects);
    let defaults = load("defaults")?;
    let raw = load("raw")?;
    for entity in defaults.entities() {
        match entity {
            EntityType::Point(p) => {
                close(p.location, Vector3::ZERO);
                close(p.normal, Vector3::UNIT_Z);
                assert_eq!((p.thickness, p.x_axis_angle), (0., 0.));
            }
            EntityType::Ray(r) => {
                close(r.base_point, Vector3::ZERO);
                close(r.direction, Vector3::UNIT_X);
            }
            EntityType::XLine(x) => {
                close(x.base_point, Vector3::ZERO);
                close(x.direction, Vector3::UNIT_X);
            }
            _ => panic!(),
        }
    }
    for entity in raw.entities() {
        match entity {
            EntityType::Point(p) => assert_eq!(p.x_axis_angle, 90.),
            EntityType::Ray(r) => close(r.direction, Vector3::new(0.6, 0.8, 0.)),
            EntityType::XLine(x) => close(x.direction, Vector3::new(0.6, 0.8, 0.)),
            _ => panic!(),
        }
    }
    println!(
        "Rust verified POINT/RAY/XLINE creation, editing, copy/delete, defaults and raw directions"
    );
    Ok(())
}
