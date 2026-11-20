// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::face3d::InvisibleEdgeFlags;
use acadrust::entities::{Face3D, Solid};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Layer, Vector3};

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    d.layers.add(Layer::new("面"))?;
    for paper in [false, true] {
        let a = Vector3::new(0., 0., 1.);
        let b = Vector3::new(6., 0., 2.);
        let c = Vector3::new(0., 4., 3.);
        let e = Vector3::new(6., 4., 4.);
        let mut trace = Solid::new(a, b, c, e);
        trace.is_trace = true;
        let entities = [
            EntityType::Solid(Solid::new(a, b, c, e)),
            EntityType::Solid(Solid::triangle(a, b, c)),
            EntityType::Solid(trace),
            EntityType::Face3D(
                Face3D::new(a, b, e, c).with_invisible_edges(InvisibleEdgeFlags::from_bits(9)),
            ),
            EntityType::Face3D(
                Face3D::triangle(a, b, c).with_invisible_edges(InvisibleEdgeFlags::from_bits(9)),
            ),
        ];
        for mut entity in entities {
            entity.common_mut().layer = "面".into();
            if paper {
                d.add_paper_space_entity(entity)?;
            } else {
                d.add_entity(entity)?;
            }
        }
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn edited(entity: &mut EntityType, copy: bool) {
    match entity {
        EntityType::Solid(s) => {
            if copy {
                s.first_corner = Vector3::new(101., 102., 103.);
            } else {
                let tri = s.is_triangle();
                s.third_corner = Vector3::new(0., 9., 3.);
                if tri {
                    s.fourth_corner = s.third_corner;
                }
                s.thickness = 4.;
                s.normal = Vector3::new(0., 1., 0.);
            }
        }
        EntityType::Face3D(f) => {
            if copy {
                f.first_corner = Vector3::new(101., 102., 103.);
                f.invisible_edges = InvisibleEdgeFlags::from_bits(7);
            } else {
                f.second_corner = Vector3::new(8., 0., 5.);
                f.invisible_edges.set_first_invisible(false);
                f.invisible_edges.set_third_invisible(true);
            }
        }
        _ => panic!(),
    }
}
fn geometry(a: &EntityType, b: &EntityType) {
    match (a, b) {
        (EntityType::Solid(a), EntityType::Solid(b)) => {
            assert_eq!(a.corners(), b.corners());
            assert_eq!(a.boundary_corners(), b.boundary_corners());
            assert_eq!(a.is_triangle(), b.is_triangle());
            assert_eq!(a.is_trace, b.is_trace);
            assert_eq!(a.normal, b.normal);
            assert_eq!(a.thickness, b.thickness);
        }
        (EntityType::Face3D(a), EntityType::Face3D(b)) => {
            assert_eq!(a.corners(), b.corners());
            assert_eq!(a.is_triangle(), b.is_triangle());
            assert_eq!(a.invisible_edges, b.invisible_edges);
            assert!((a.area() - b.area()).abs() < 1e-10);
        }
        _ => panic!(),
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let read = |name: &str| DxfReader::from_file(format!("{prefix}-{name}.dxf"))?.read();
    let input = DxfReader::from_file(input)?.read()?;
    let created = read("created")?;
    assert_eq!(created.entities().count(), 10);
    for (a, b) in created.entities().zip(input.entities()) {
        geometry(a, b);
        let mut common = b.common().clone();
        common.handle = a.common().handle;
        assert_eq!(a.common(), &common);
    }
    let before = read("before")?;
    let after = read("edited")?;
    let copied = read("copied")?;
    let restored = read("restored")?;
    let deleted = read("deleted")?;
    let model: Vec<_> = before
        .entities_in_block("*Model_Space")
        .map(|e| e.common().handle)
        .collect();
    for original in before.entities() {
        let h = original.common().handle;
        let mut expected = original.clone();
        if model.contains(&h) {
            edited(&mut expected, false);
        }
        let actual = after.get_entity(h).unwrap();
        geometry(actual, &expected);
        assert_eq!(actual.common(), expected.common());
        assert_eq!(copied.get_entity(h), Some(actual));
        assert_eq!(restored.get_entity(h), Some(actual));
        if !model.contains(&h) {
            assert_eq!(deleted.get_entity(h), Some(original));
        }
    }
    let clones: Vec<_> = copied
        .entities()
        .filter(|e| before.get_entity(e.common().handle).is_none())
        .collect();
    assert_eq!(clones.len(), 5);
    for (h, clone) in model.iter().zip(clones) {
        let mut expected = after.get_entity(*h).unwrap().clone();
        edited(&mut expected, true);
        geometry(clone, &expected);
        assert_eq!(
            clone.common().owner_handle,
            before
                .entities_in_block("*Paper_Space")
                .next()
                .unwrap()
                .common()
                .owner_handle
        );
        assert_eq!(
            copied
                .xrecord(clone.common().handle, "Data")
                .unwrap()
                .get_first_by_code(340)
                .unwrap()
                .value
                .as_handle(),
            Some(clone.common().handle)
        );
    }
    assert_eq!(after.objects, restored.objects);
    let mut expected = after.clone();
    for h in model {
        expected.remove_entity(h).unwrap();
    }
    assert_eq!(deleted.objects, expected.objects);
    assert_eq!(deleted.entities_in_block("*Model_Space").count(), 0);
    assert_eq!(deleted.entities_in_block("*Paper_Space").count(), 5);
    let defaults = read("defaults")?;
    for entity in defaults.entities() {
        match entity {
            EntityType::Solid(s) => {
                assert!(s.is_triangle());
                assert_eq!(s.third_corner, s.fourth_corner);
                assert_eq!(s.normal, Vector3::UNIT_Z);
                assert_eq!(s.thickness, 0.);
            }
            EntityType::Face3D(f) => {
                assert!(f.is_triangle());
                assert_eq!(f.third_corner, f.fourth_corner);
                assert_eq!(f.invisible_edges.bits(), 0);
            }
            _ => panic!(),
        }
    }
    println!(
        "Rust verified SOLID/TRACE/3DFACE vertices, boundaries, hidden edges, copies and deletion"
    );
    Ok(())
}
