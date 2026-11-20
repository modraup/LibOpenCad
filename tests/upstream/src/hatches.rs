// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::hatch::*;
use acadrust::entities::{Hatch, LwPolyline};
use acadrust::types::{Color, Handle};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector2, Vector3};
pub(crate) fn definition(boundary: Handle, curved: bool) -> Hatch {
    let mut v = Hatch::new();
    v.is_associative = !boundary.is_null();
    v.elevation = 2.;
    v.pixel_size = 0.02;
    let mut outer =
        BoundaryPath::with_flags(BoundaryPathFlags::from_bits(if curved { 1 } else { 7 }));
    if !boundary.is_null() {
        outer.boundary_handles.push(boundary);
    }
    if curved {
        outer.edges = vec![
            BoundaryEdge::Line(LineEdge {
                start: Vector2::ZERO,
                end: Vector2::new(4., 0.),
            }),
            BoundaryEdge::CircularArc(CircularArcEdge {
                center: Vector2::new(4., 2.),
                radius: 2.,
                start_angle: -std::f64::consts::FRAC_PI_2,
                end_angle: std::f64::consts::FRAC_PI_2,
                counter_clockwise: true,
            }),
            BoundaryEdge::EllipticArc(EllipticArcEdge {
                center: Vector2::new(2., 4.),
                major_axis_endpoint: Vector2::new(2., 0.),
                minor_axis_ratio: 0.5,
                start_angle: 0.,
                end_angle: std::f64::consts::PI,
                counter_clockwise: true,
            }),
            BoundaryEdge::Spline(SplineEdge {
                degree: 2,
                rational: true,
                periodic: false,
                knots: vec![0., 0., 0., 1., 1., 1.],
                control_points: vec![
                    Vector3::new(0., 4., 1.),
                    Vector3::new(0., 2., 0.5),
                    Vector3::new(0., 0., 1.),
                ],
                fit_points: vec![Vector2::new(0., 4.), Vector2::ZERO],
                start_tangent: Vector2::new(0., -1.),
                end_tangent: Vector2::new(0., -1.),
            }),
        ];
        v.gradient_color.enabled = true;
        v.gradient_color.reserved = 7;
        v.gradient_color.angle = 0.3;
        v.gradient_color.shift = 0.4;
        v.gradient_color.color_tint = 0.6;
        v.gradient_color.name = "LINEAR".into();
        v.gradient_color.add_color(0., Color::from_rgb(12, 34, 56));
        v.gradient_color.add_color(1., Color::from_index(2));
    } else {
        outer.edges.push(BoundaryEdge::Polyline(PolylineEdge {
            vertices: vec![
                Vector3::new(0., 0., 0.),
                Vector3::new(10., 0., 0.25),
                Vector3::new(10., 10., 0.),
                Vector3::new(0., 10., 0.),
            ],
            is_closed: true,
        }));
        v.is_solid = false;
        v.pattern.name = "CUSTOM".into();
        v.pattern_type = HatchPatternType::Custom;
        v.pattern_angle = 0.25;
        v.pattern_scale = 2.;
        v.is_double = true;
        v.pattern.lines = vec![HatchPatternLine {
            angle: 0.5,
            base_point: Vector2::new(1., 2.),
            offset: Vector2::new(3., 4.),
            dash_lengths: vec![2., -1., 0.],
        }];
    }
    v.paths.push(outer);
    let mut hole = BoundaryPath::new();
    hole.edges.push(BoundaryEdge::CircularArc(CircularArcEdge {
        center: if curved {
            Vector2::new(2., 2.)
        } else {
            Vector2::new(5., 5.)
        },
        radius: if curved { 0.5 } else { 1. },
        start_angle: 0.,
        end_angle: std::f64::consts::TAU,
        counter_clockwise: false,
    }));
    v.paths.push(hole);
    v.seed_points = vec![Vector2::new(1., 1.)];
    v
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    for paper in [false, true] {
        let mut p = LwPolyline::from_points(vec![
            Vector2::ZERO,
            Vector2::new(10., 0.),
            Vector2::new(10., 10.),
            Vector2::new(0., 10.),
        ]);
        p.vertices[1].bulge = 0.25;
        p.elevation = 2.;
        p.close();
        let h = if paper {
            d.add_paper_space_entity(EntityType::LwPolyline(p))?
        } else {
            d.add_entity(EntityType::LwPolyline(p))?
        };
        let hatch = EntityType::Hatch(Box::new(definition(h, false)));
        if paper {
            d.add_paper_space_entity(hatch)?;
        } else {
            d.add_entity(hatch)?;
        }
    }
    d.add_entity(EntityType::Hatch(Box::new(definition(Handle::NULL, true))))?;
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn geometry(a: &Hatch, b: &Hatch) {
    let mut b = b.clone();
    b.common = a.common.clone();
    for (ap, bp) in a.paths.iter().zip(b.paths.iter_mut()) {
        bp.boundary_handles = ap.boundary_handles.clone();
        for (a, b) in ap.edges.iter().zip(bp.edges.iter_mut()) {
            if let (BoundaryEdge::EllipticArc(a), BoundaryEdge::EllipticArc(b)) = (a, b) {
                assert!(
                    (a.start_angle - b.start_angle).abs() < 1e-12
                        && (a.end_angle - b.end_angle).abs() < 1e-12
                );
                b.start_angle = a.start_angle;
                b.end_angle = a.end_angle;
            }
        }
    }
    assert_eq!(a, &b);
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let created = read("created")?;
    let hatch = |e: &EntityType| {
        if let EntityType::Hatch(h) = e {
            Some(h.clone())
        } else {
            None
        }
    };
    for space in ["*Model_Space", "*Paper_Space"] {
        let expected: Vec<_> = original
            .entities_in_block(space)
            .filter_map(hatch)
            .collect();
        let actual: Vec<_> = created.entities_in_block(space).filter_map(hatch).collect();
        assert_eq!(actual.len(), expected.len());
        for a in actual {
            let b = expected.iter().find(|b| a.is_solid == b.is_solid).unwrap();
            geometry(&a, b);
        }
    }
    let before = read("before")?;
    let target = before
        .entities_in_block("*Model_Space")
        .find(|e| matches!(e,EntityType::Hatch(h) if !h.is_solid))
        .unwrap()
        .common()
        .handle;
    let edited = read("edited")?;
    for e in before.entities() {
        if e.common().handle == target {
            let EntityType::Hatch(mut expected) = e.clone() else {
                panic!()
            };
            let BoundaryEdge::Polyline(p) = &mut expected.paths[0].edges[0] else {
                panic!()
            };
            p.vertices[1].z = -0.5;
            p.vertices[2].x = 12.;
            expected.pattern.lines[0].dash_lengths = vec![3., -2.];
            expected.pattern_angle = -0.5;
            expected.seed_points.push(Vector2::new(8., 8.));
            geometry(
                &hatch(edited.get_entity(target).unwrap()).unwrap(),
                &expected,
            );
        } else {
            assert_eq!(edited.get_entity(e.common().handle).unwrap(), e);
        }
    }
    let copied = read("copied")?;
    let copy = copied
        .entities()
        .find(|e| before.get_entity(e.common().handle).is_none())
        .unwrap();
    geometry(
        &hatch(copy).unwrap(),
        &hatch(edited.get_entity(target).unwrap()).unwrap(),
    );
    assert_eq!(
        copied
            .xrecord(copy.common().handle, "Data")
            .unwrap()
            .get_first_by_code(340)
            .unwrap()
            .value
            .as_handle(),
        Some(copy.common().handle)
    );
    let modified = read("copymodified")?;
    assert_eq!(modified.get_entity(target), edited.get_entity(target));
    assert_eq!(
        hatch(modified.get_entity(copy.common().handle).unwrap())
            .unwrap()
            .seed_points,
        vec![Vector2::new(9., 9.)]
    );
    let deleted = read("deleted")?;
    assert_eq!(deleted.entities().count(), before.entities().count() - 2);
    assert!(deleted.get_entity(target).is_none());
    for e in deleted.entities() {
        assert_eq!(before.get_entity(e.common().handle).unwrap(), e);
    }
    let turns = read("turns")?;
    let h = turns
        .entities()
        .filter_map(hatch)
        .find(|h| h.is_solid)
        .unwrap();
    let BoundaryEdge::EllipticArc(e) = &h.paths[0].edges[2] else {
        panic!()
    };
    assert!(
        (e.start_angle + 0.75).abs() < 1e-12
            && (e.end_angle - 7.4).abs() < 1e-12
            && !e.counter_clockwise
    );
    println!("Rust verified patterned/gradient HATCH with holes, every edge kind, associations, copy/delete and ellipse parameters");
    Ok(())
}
