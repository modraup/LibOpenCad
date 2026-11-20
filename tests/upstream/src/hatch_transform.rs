use acadrust::types::Transform;
// SPDX-License-Identifier: MPL-2.0
use crate::entity_transform::{near, point};
use acadrust::entities::hatch::BoundaryEdge;
use acadrust::entities::{Hatch, MirrorOptions};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector2, Vector3};
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(acadrust::Layer::new("Control"))?;
    doc.app_ids.add(acadrust::tables::AppId::new("Geometry"))?;
    let boundary = doc.add_entity(EntityType::Line(acadrust::entities::Line::from_points(
        Vector3::ZERO,
        Vector3::new(10., 0., 0.),
    )))?;
    for n in [
        Vector3::UNIT_Z,
        -Vector3::UNIT_Z,
        Vector3::UNIT_Y,
        Vector3::new(1., 2., 3.),
    ] {
        for curved in [false, true] {
            for case in 0..3 {
                let mut h = crate::hatches::definition(boundary, curved);
                h.normal = n;
                if curved {
                    if let BoundaryEdge::CircularArc(a) = &mut h.paths[0].edges[1] {
                        a.start_angle = 5.5;
                        a.end_angle = 7.8;
                        a.counter_clockwise = case != 1;
                    }
                    if let BoundaryEdge::EllipticArc(a) = &mut h.paths[0].edges[2] {
                        a.major_axis_endpoint = Vector2::new(2., 1.);
                        a.start_angle = 5.8;
                        a.end_angle = 8.4;
                        a.counter_clockwise = case != 2;
                    }
                }
                use acadrust::xdata::{ExtendedDataRecord, XDataValue as X};
                if case > 0 {
                    h.common.extended_data.add_record(ExtendedDataRecord {
                        application_name: "ACAD".into(),
                        values: vec![
                            X::String("nested origin is not metadata".into()),
                            X::ControlString("{".into()),
                            X::Point3D(Vector3::new(90., 80., 70.)),
                            X::ControlString("}".into()),
                        ],
                    });
                    h.record_pattern_origin(if case == 1 {
                        Vector2::ZERO
                    } else {
                        Vector2::new(3., 4.)
                    });
                }
                h.common.extended_data.add_record(ExtendedDataRecord {
                    application_name: "Geometry".into(),
                    values: vec![
                        X::Position3D(Vector3::new(1., 2., 3.)),
                        X::Direction3D(Vector3::UNIT_Y),
                        X::Distance(2.5),
                    ],
                });
                doc.add_entity(EntityType::Hatch(Box::new(h)))?;
            }
        }
    }
    let controls: Vec<_> = doc
        .entities()
        .filter(|e| matches!(e, EntityType::Hatch(_)))
        .step_by(3)
        .take(2)
        .cloned()
        .collect();
    for mut e in controls {
        e.common_mut().handle = Handle::NULL;
        e.common_mut().layer = "Control".into();
        doc.add_paper_space_entity(e)?;
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    let doc = DxfReader::from_file(ascii)?.read()?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}
fn operation(step: usize) -> Transform {
    match step {
        12 => Transform::from_scaling(Vector3::new(0.5, 2., 1.5)),
        14 => crate::entity_transform::operation(5),
        15 => crate::entity_transform::operation(6),
        16 => crate::entity_transform::operation(3),
        17 => Transform::from_translation(Vector3::new(0., 0., 3.)),
        _ => crate::entity_transform::operation(step),
    }
}
fn values(h: &Hatch) -> Vec<f64> {
    let mut v = vec![
        h.elevation,
        h.normal.x,
        h.normal.y,
        h.normal.z,
        h.pattern_angle,
        h.pattern_scale,
        h.gradient_color.angle,
        h.pattern_origin().x,
        h.pattern_origin().y,
        h.stored_pattern_origin().is_some() as u8 as f64,
    ];
    fn p(v: &mut Vec<f64>, p: Vector2) {
        v.extend([p.x, p.y]);
    }
    fn q(v: &mut Vec<f64>, p: Vector3) {
        v.extend([p.x, p.y, p.z]);
    }
    v.push(h.paths.len() as f64);
    for path in &h.paths {
        v.push(path.edges.len() as f64);
        for edge in &path.edges {
            match edge {
                BoundaryEdge::Line(a) => {
                    v.push(0.);
                    p(&mut v, a.start);
                    p(&mut v, a.end);
                }
                BoundaryEdge::CircularArc(a) => {
                    v.push(1.);
                    p(&mut v, a.center);
                    v.extend([
                        a.radius,
                        a.start_angle,
                        a.end_angle,
                        a.counter_clockwise as u8 as f64,
                    ]);
                }
                BoundaryEdge::EllipticArc(a) => {
                    v.push(2.);
                    p(&mut v, a.center);
                    p(&mut v, a.major_axis_endpoint);
                    v.extend([
                        a.minor_axis_ratio,
                        a.start_angle,
                        a.end_angle,
                        a.counter_clockwise as u8 as f64,
                    ]);
                }
                BoundaryEdge::Spline(a) => {
                    v.push(3.);
                    v.push(a.control_points.len() as f64);
                    for x in &a.control_points {
                        q(&mut v, *x);
                    }
                    v.push(a.fit_points.len() as f64);
                    for x in &a.fit_points {
                        p(&mut v, *x);
                    }
                    p(&mut v, a.start_tangent);
                    p(&mut v, a.end_tangent);
                }
                BoundaryEdge::Polyline(a) => {
                    v.push(4.);
                    v.push(a.vertices.len() as f64);
                    for x in &a.vertices {
                        q(&mut v, *x);
                    }
                    v.push(a.is_closed as u8 as f64);
                }
            }
        }
    }
    v.push(h.seed_points.len() as f64);
    for x in &h.seed_points {
        p(&mut v, *x);
    }
    v.push(h.pattern.lines.len() as f64);
    for l in &h.pattern.lines {
        v.push(l.angle);
        p(&mut v, l.base_point);
        p(&mut v, l.offset);
        v.push(l.dash_lengths.len() as f64);
        v.extend(&l.dash_lengths);
    }
    v
}
fn p2(a: Vector2, b: Vector2) {
    near(a.x, b.x);
    near(a.y, b.y);
}
macro_rules! vectors{($a:ident,$b:ident,$($field:ident),*)=>{$(p2($a.$field,$b.$field);$a.$field=$b.$field;)*}}
macro_rules! numbers{($a:ident,$b:ident,$($field:ident),*)=>{$(near($a.$field,$b.$field);$a.$field=$b.$field;)*}}
pub(crate) fn normalize(a: &mut EntityType, b: &EntityType) {
    let (EntityType::Hatch(a), EntityType::Hatch(b)) = (a, b) else {
        panic!()
    };
    numbers!(a, b, elevation, pattern_angle, pattern_scale);
    point(a.normal, b.normal);
    a.normal = b.normal;
    near(a.gradient_color.angle, b.gradient_color.angle);
    a.gradient_color.angle = b.gradient_color.angle;
    assert_eq!(a.paths.len(), b.paths.len());
    for (a, b) in a.paths.iter_mut().zip(&b.paths) {
        assert_eq!(a.edges.len(), b.edges.len());
        for (a, b) in a.edges.iter_mut().zip(&b.edges) {
            match (a, b) {
                (BoundaryEdge::Line(a), BoundaryEdge::Line(b)) => {
                    vectors!(a, b, start, end);
                }
                (BoundaryEdge::CircularArc(a), BoundaryEdge::CircularArc(b)) => {
                    vectors!(a, b, center);
                    numbers!(a, b, radius, start_angle, end_angle);
                }
                (BoundaryEdge::EllipticArc(a), BoundaryEdge::EllipticArc(b)) => {
                    vectors!(a, b, center, major_axis_endpoint);
                    numbers!(a, b, minor_axis_ratio, start_angle, end_angle);
                }
                (BoundaryEdge::Spline(a), BoundaryEdge::Spline(b)) => {
                    assert_eq!(a.control_points.len(), b.control_points.len());
                    for (a, b) in a.control_points.iter_mut().zip(&b.control_points) {
                        point(*a, *b);
                        *a = *b;
                    }
                    assert_eq!(a.fit_points.len(), b.fit_points.len());
                    for (a, b) in a.fit_points.iter_mut().zip(&b.fit_points) {
                        p2(*a, *b);
                        *a = *b;
                    }
                    vectors!(a, b, start_tangent, end_tangent);
                }
                (BoundaryEdge::Polyline(a), BoundaryEdge::Polyline(b)) => {
                    assert_eq!(a.vertices.len(), b.vertices.len());
                    for (a, b) in a.vertices.iter_mut().zip(&b.vertices) {
                        point(*a, *b);
                        *a = *b;
                    }
                }
                _ => panic!("Hatch edge type changed"),
            }
        }
    }
    assert_eq!(a.seed_points.len(), b.seed_points.len());
    for (a, b) in a.seed_points.iter_mut().zip(&b.seed_points) {
        p2(*a, *b);
        *a = *b;
    }
    assert_eq!(a.pattern.lines.len(), b.pattern.lines.len());
    for (a, b) in a.pattern.lines.iter_mut().zip(&b.pattern.lines) {
        numbers!(a, b, angle);
        vectors!(a, b, base_point, offset);
        assert_eq!(a.dash_lengths.len(), b.dash_lengths.len());
        for (a, b) in a.dash_lengths.iter_mut().zip(&b.dash_lengths) {
            near(*a, *b);
            *a = *b;
        }
    }
    // ACAD 1010 is metadata; compare tolerantly here without treating it as geometric XDATA.
    let mut data = a.common.extended_data.clone();
    for br in b.common.extended_data.records() {
        if br.application_name == "ACAD" {
            let mut ar = data.get_record("ACAD").unwrap().clone();
            assert_eq!(ar.values.len(), br.values.len());
            for (av, bv) in ar.values.iter_mut().zip(&br.values) {
                if let (
                    acadrust::xdata::XDataValue::Point3D(ap),
                    acadrust::xdata::XDataValue::Point3D(bp),
                ) = (av, bv)
                {
                    point(*ap, *bp);
                    *ap = *bp;
                }
            }
            data.upsert_record(ar);
        }
    }
    a.common.extended_data = data;
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    assert_eq!(source.entities().count(), 27);
    for step in 0..18 {
        eprintln!("hatch step {step}");
        let t = operation(step);
        let mut expected = source.clone();
        for e in expected.entities_mut() {
            if e.common().layer == "Control" || !matches!(e, EntityType::Hatch(_)) {
                continue;
            }
            if step == 0 {
                e.translate(Vector3::new(3., -4., 5.));
            } else if [5, 6, 7, 11, 14, 15, 16, 17].contains(&step) {
                e.apply_mirror_with_options(
                    &t,
                    MirrorOptions {
                        mirror_hatch: step == 14 || step == 15,
                        ..Default::default()
                    },
                );
            } else {
                e.apply_transform(&t);
            }
        }
        let rows = std::fs::read_to_string(format!("{prefix}.{step}.trace"))?;
        assert_eq!(rows.lines().count(), 26);
        for row in rows.lines() {
            let mut p = row.split_whitespace();
            let handle = p.next().unwrap();
            let h = Handle::new(u64::from_str_radix(handle, 16).unwrap());
            let EntityType::Hatch(h) = expected.get_entity(h).unwrap() else {
                panic!()
            };
            let a: Vec<f64> = p.map(|p| p.parse().unwrap()).collect();
            let b = values(h);
            assert_eq!(a.len(), b.len());
            for (i, (a, b)) in a.iter().zip(&b).enumerate() {
                assert!(
                    (a - b).abs() <= 2e-10 * b.abs().max(1.),
                    "step {step} handle {handle} value {i}: {a} != {b}"
                );
            }
        }
        let path = format!("{prefix}.{step}.rust.dxf");
        DxfWriter::new(&expected).write_to_file(&path)?;
        let expected = DxfReader::from_file(path)?.read()?;
        let actual = DxfReader::from_file(format!("{prefix}.{step}.dxf"))?.read()?;
        assert_eq!(actual.entities().count(), 27);
        for b in expected.entities() {
            crate::entity_transform::compare(actual.get_entity(b.common().handle).unwrap(), b);
        }
        assert_eq!(source.header, actual.header);
        assert_eq!(source.layers, actual.layers);
        assert_eq!(source.objects, actual.objects);
        assert_eq!(source.block_records, actual.block_records);
    }
    Ok(())
}
