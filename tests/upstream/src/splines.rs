// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{Helix, HelixConstraint, Spline};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
use std::io::Cursor;
fn spline() -> Spline {
    let mut s = Spline::new();
    s.degree = 2;
    s.flags.rational = true;
    s.flags.planar = true;
    s.dxf_flags = 0x800;
    s.knots = vec![0., 0., 0., 1., 2., 2., 2.];
    s.control_points = vec![
        Vector3::ZERO,
        Vector3::new(2., 3., 1.),
        Vector3::new(4., 2., 2.),
        Vector3::new(6., 0., 3.),
    ];
    s.weights = vec![1., 2., 3., 1.];
    s.fit_points = vec![Vector3::ZERO, Vector3::new(6., 0., 3.)];
    s.knot_tolerance = 1e-7;
    s.control_tolerance = 2e-7;
    s.fit_tolerance = 3e-7;
    s.begin_tangent = Vector3::UNIT_X;
    s.end_tangent = Vector3::UNIT_Y;
    s
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    for paper in [false, true] {
        let s = spline();
        let mut h = Helix::new();
        h.spline = s.clone();
        h.radius = 3.;
        h.turns = 4.;
        h.turn_height = 2.;
        h.axis_base_point = Vector3::new(1., 2., 3.);
        h.start_point = Vector3::new(4., 5., 6.);
        for e in [EntityType::Spline(s), EntityType::Helix(Box::new(h))] {
            if paper {
                d.add_paper_space_entity(e)?;
            } else {
                d.add_entity(e)?;
            }
        }
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn edit(e: &mut EntityType, copy: bool) {
    let s = match e {
        EntityType::Spline(s) => s,
        EntityType::Helix(h) => &mut h.spline,
        _ => panic!(),
    };
    if copy {
        s.control_points[0] = Vector3::new(101., 102., 103.);
        return;
    }
    s.control_points[1].x += 2.;
    s.knots[3] = 0.5;
    s.weights[1] = 0.75;
    s.fit_points[0].z = 4.;
    s.cv_frame_visible = true;
    s.dxf_flags |= 512;
    s.dwg_flags1 |= 2;
    if let EntityType::Helix(h) = e {
        h.start_point = Vector3::new(7., 8., 9.);
        h.radius = 9.;
        h.turns = 1.5;
        h.maintenance_version = 1;
        h.handedness = false;
        h.constraint = HelixConstraint::Height;
    }
}
fn geometry(a: &EntityType, b: &EntityType) {
    match (a, b) {
        (EntityType::Spline(a), EntityType::Spline(b)) => {
            let mut b = b.clone();
            b.common = a.common.clone();
            assert_eq!(a, &b);
        }
        (EntityType::Helix(a), EntityType::Helix(b)) => {
            let mut b = b.clone();
            b.common = a.common.clone();
            assert_eq!(a, &b);
        }
        _ => panic!(),
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let read = |name: &str| DxfReader::from_file(format!("{prefix}-{name}.dxf"))?.read();
    let input = DxfReader::from_file(input)?.read()?;
    let created = read("created")?;
    assert_eq!(created.entities().count(), 4);
    // The saved input predates AcDbVariableDictionary. New drawings use
    // the current default seed; compare common data with that explicit map.
    let first = CadDocument::new().next_handle();
    for (index, (a, b)) in created.entities().zip(input.entities()).enumerate() {
        geometry(a, b);
        let mut expected = b.common().clone();
        expected.handle = acadrust::Handle::new(first + index as u64);
        assert_eq!(a.common(), &expected);
    }
    let before = read("before")?;
    let after = read("edited")?;
    let copied = read("copied")?;
    let restored = read("restored")?;
    let deleted = read("deleted")?;
    let ids: Vec<_> = before
        .entities_in_block("*Model_Space")
        .map(|e| e.common().handle)
        .collect();
    for src in before.entities() {
        let h = src.common().handle;
        let mut expected = src.clone();
        if ids.contains(&h) {
            edit(&mut expected, false);
        }
        let actual = after.get_entity(h).unwrap();
        geometry(actual, &expected);
        assert_eq!(actual.common(), expected.common());
        assert_eq!(copied.get_entity(h), Some(actual));
        assert_eq!(restored.get_entity(h), Some(actual));
        if !ids.contains(&h) {
            assert_eq!(deleted.get_entity(h), Some(src));
        }
    }
    let clones: Vec<_> = copied
        .entities()
        .filter(|e| before.get_entity(e.common().handle).is_none())
        .collect();
    assert_eq!(clones.len(), 2);
    for (id, c) in ids.iter().zip(clones) {
        let mut expected = after.get_entity(*id).unwrap().clone();
        edit(&mut expected, true);
        geometry(c, &expected);
        assert_eq!(
            c.common().owner_handle,
            before
                .entities_in_block("*Paper_Space")
                .next()
                .unwrap()
                .common()
                .owner_handle
        );
        assert_eq!(
            copied
                .xrecord(c.common().handle, "Data")
                .unwrap()
                .get_first_by_code(340)
                .unwrap()
                .value
                .as_handle(),
            Some(c.common().handle)
        );
    }
    assert_eq!(after.objects, restored.objects);
    let mut expected = after.clone();
    for id in ids {
        expected.remove_entity(id).unwrap();
    }
    assert_eq!(deleted.objects, expected.objects);
    assert_eq!(deleted.entities_in_block("*Model_Space").count(), 0);
    assert_eq!(deleted.entities_in_block("*Paper_Space").count(), 2);
    let periodic = read("periodic")?;
    let mut expected = CadDocument::new();
    let mut s = spline();
    s.degree = 3;
    s.flags.closed = true;
    s.flags.periodic = true;
    s.flags.planar = false;
    s.dxf_flags = 0;
    s.knots = vec![0., 1., 2., 3., 4.];
    s.fit_points.clear();
    s.weights = vec![1., 2., 3., 4.];
    expected.add_entity(EntityType::Spline(s))?;
    let expected =
        DxfReader::from_reader(Cursor::new(DxfWriter::new(&expected).write_to_vec()?))?.read()?;
    let (EntityType::Spline(a), EntityType::Spline(b)) = (
        periodic.entities().next().unwrap(),
        expected.entities().next().unwrap(),
    ) else {
        panic!()
    };
    assert_eq!(a.knots, b.knots);
    assert_eq!(a.degree, b.degree);
    assert_eq!(a.dxf_flags, b.dxf_flags);
    assert_eq!(a.control_points.len(), b.control_points.len());
    for (a, b) in a.control_points.iter().zip(&b.control_points) {
        assert!((*a - *b).length() < 1e-12);
    }
    for (a, b) in a.weights.iter().zip(&b.weights) {
        assert!((a - b).abs() < 1e-12);
    }
    let fit = read("fit")?;
    let mut reference = CadDocument::new();
    for method in 0..3 {
        let mut s = Spline::new();
        s.degree = 3;
        s.knot_parameterization = method;
        s.fit_points = vec![Vector3::ZERO, Vector3::new(1.,3.,0.), Vector3::new(4.,2.,1.), Vector3::new(7.,0.,0.)];
        s.end_tangent = Vector3::new(1.,-2.,0.);
        reference.add_entity(EntityType::Spline(s))?;
    }
    let reference = DxfReader::from_reader(Cursor::new(DxfWriter::new(&reference).write_to_vec()?))?.read()?;
    assert_eq!(fit.entities().count(), 3);
    for (actual, expected) in fit.entities().zip(reference.entities()) {
        let (EntityType::Spline(a), EntityType::Spline(b)) = (actual, expected) else { panic!("expected cubic splines") };
        assert_eq!(a.dxf_flags, b.dxf_flags);
        assert_eq!(a.knots, b.knots);
        assert_eq!(a.control_points.len(), b.control_points.len());
        for (p, q) in a.control_points.iter().zip(&b.control_points) {
            assert!((*p - *q).length() < 1e-11);
        }
    }
    println!(
        "Rust verified SPLINE/HELIX fields and independent subclass edits, copies and deletion"
    );
    Ok(())
}
