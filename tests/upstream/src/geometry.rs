// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{Entity, LwPolyline, LwVertex};
use acadrust::{
    Arc, BoundingBox3D, CadDocument, Circle, DxfReader, DxfWriter, Ellipse, EntityType, Handle,
    Line, Vector2, Vector3,
};
use std::collections::BTreeMap;
use std::f64::consts::{PI, TAU};

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    let normals = [
        Vector3::UNIT_Z,
        -Vector3::UNIT_Z,
        Vector3::UNIT_Y,
        Vector3::UNIT_X,
        Vector3::new(1., 2., 3.),
        Vector3::new(-2., 1., -3.),
        Vector3::new(0., 0., 7.),
        Vector3::ZERO,
        Vector3::new(0.015, 0.015, 1.),
        Vector3::new(0.016, 0.016, 1.),
    ];
    for (i, n) in normals.into_iter().enumerate() {
        let center = Vector3::new(3., -4., 2.);
        let thickness = if i % 2 == 0 { 2.5 } else { -1.75 };
        let mut l = Line::from_points(
            center,
            if i == 7 {
                center
            } else {
                Vector3::new(-8., 6., -3.)
            },
        );
        l.normal = n;
        l.thickness = thickness;
        doc.add_entity(EntityType::Line(l))?;
        for radius in [0., 5., -2.] {
            let mut c = Circle::from_center_radius(center, radius);
            c.normal = n;
            c.thickness = thickness;
            doc.add_entity(EntityType::Circle(c))?;
        }
        for (start, end) in [
            (0., PI / 2.),
            (5.5, 0.25),
            (-1.2, 2.3),
            (0., TAU),
            (0.7, 0.7),
            (-8., 8.),
            (8., -8.),
        ] {
            let mut a =
                Arc::from_center_radius_angles(center, if i == 7 { 0. } else { 5. }, start, end);
            a.normal = n;
            a.thickness = thickness;
            doc.add_entity(EntityType::Arc(a))?;
        }
        for (major, ratio, start, end) in [
            (Vector3::new(4., 3., 2.), 0.5, 0., TAU),
            (Vector3::new(-3., 1., 5.), 1.5, 5.5, 0.25),
            (Vector3::ZERO, 0., 0.7, 0.7),
        ] {
            let mut e = Ellipse::from_center_axes(center, major, ratio);
            e.normal = n;
            e.start_parameter = start;
            e.end_parameter = end;
            doc.add_entity(EntityType::Ellipse(e))?;
        }
        for count in [0, 1, 4] {
            for closed in [false, true] {
                let mut p = LwPolyline::new();
                p.elevation = 2.;
                p.normal = n;
                p.thickness = thickness;
                p.constant_width = 3.;
                p.is_closed = closed;
                for (j, (x, y, b)) in [(0., 0., 1.), (8., 0., -0.5), (8., 6., 2.), (0., 6., -1.)]
                    .into_iter()
                    .take(count)
                    .enumerate()
                {
                    let mut v = LwVertex::with_bulge(Vector2::new(x, y), b);
                    v.start_width = 0.25 + j as f64;
                    v.end_width = 1.5;
                    p.add_vertex(v);
                }
                doc.add_entity(EntityType::LwPolyline(p))?;
            }
        }
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}
fn point(v: &mut Vec<f64>, p: Vector3) {
    v.extend([p.x, p.y, p.z]);
}
fn bounds(v: &mut Vec<f64>, b: BoundingBox3D) {
    point(v, b.min);
    point(v, b.max);
}
fn box_values(b: BoundingBox3D) -> Vec<f64> {
    let mut v = Vec::new();
    bounds(&mut v, b);
    v.extend([b.width(), b.height(), b.depth()]);
    point(&mut v, b.center());
    v.extend([
        b.contains(b.min) as u8 as f64,
        b.contains(b.max) as u8 as f64,
        b.contains(Vector3::new(1000., 1000., 1000.)) as u8 as f64,
    ]);
    let mut expanded = b;
    expanded.expand_to_include(Vector3::new(12., -9., 5.));
    bounds(&mut v, expanded);
    bounds(
        &mut v,
        b.merge(&BoundingBox3D::new(
            Vector3::new(-7., -8., -9.),
            Vector3::new(1., 2., 3.),
        )),
    );
    bounds(&mut v, b.ocs_to_wcs(Vector3::new(1., 2., 3.)));
    v
}
fn values(e: &EntityType) -> Vec<f64> {
    let b = match e {
        EntityType::Line(e) => e.bounding_box(),
        EntityType::Circle(e) => e.bounding_box(),
        EntityType::Arc(e) => e.bounding_box(),
        EntityType::Ellipse(e) => e.bounding_box(),
        EntityType::LwPolyline(e) => e.bounding_box(),
        _ => panic!("Unexpected type"),
    };
    let mut v = box_values(b);
    match e {
        EntityType::Line(l) => {
            v.push(l.length());
            point(&mut v, l.direction());
            point(&mut v, l.midpoint());
        }
        EntityType::Circle(c) => {
            v.extend([c.diameter(), c.circumference(), c.area()]);
            point(&mut v, c.center_wcs());
            let (x, y) = c.axes_wcs();
            point(&mut v, x);
            point(&mut v, y);
            for a in [-1.2, 0., 0.7, PI, TAU] {
                point(&mut v, c.point_at_angle_wcs(a));
            }
        }
        EntityType::Arc(a) => {
            v.extend([a.sweep_angle(), a.arc_length()]);
            point(&mut v, a.start_point());
            point(&mut v, a.end_point());
            point(&mut v, a.midpoint());
            point(&mut v, a.center_wcs());
            point(&mut v, a.start_point_wcs());
            point(&mut v, a.end_point_wcs());
            point(&mut v, a.midpoint_wcs());
            for t in [-1.2, 0., 0.7, PI, TAU] {
                point(&mut v, a.point_at_angle_wcs(t));
            }
        }
        EntityType::Ellipse(e) => v.extend([
            e.major_axis_length(),
            e.minor_axis_length(),
            e.is_full() as u8 as f64,
        ]),
        EntityType::LwPolyline(p) => v.push(p.vertex_count() as f64),
        _ => panic!("Unexpected geometry type"),
    }
    v
}
pub fn verify(input: &str, results: &str) -> acadrust::Result<()> {
    let doc = DxfReader::from_file(input)?.read()?;
    let mut expected: BTreeMap<_, _> = doc
        .entities()
        .map(|e| (e.common().handle, values(e)))
        .collect();
    assert_eq!(expected.len(), 200);
    let text = std::fs::read_to_string(results)?;
    let mut rows = 0;
    for row in text.lines() {
        let mut fields = row.split_whitespace();
        let id = u64::from_str_radix(fields.next().unwrap(), 16).unwrap();
        let want = expected
            .remove(&Handle::new(id))
            .expect("Duplicate or unknown result handle");
        let got: Vec<f64> = fields.map(|f| f.parse().unwrap()).collect();
        assert_eq!(got.len(), want.len());
        for (i, (actual, expected)) in got.into_iter().zip(want).enumerate() {
            assert!(
                actual == expected || (actual - expected).abs() <= 5e-12 * expected.abs().max(1.),
                "handle {id:X} field {i}: C++ {actual} Rust {expected}"
            );
        }
        rows += 1;
    }
    assert_eq!(rows, 200);
    assert!(expected.is_empty());
    Ok(())
}
