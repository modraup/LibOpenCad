// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::primitives::*;
use acadrust::entities::acis::*;
use acadrust::entities::{AcisData, Body, Region, Solid3D, Surface, SurfaceKind};
use acadrust::{CadDocument, DxfReader, DxfVersion, DxfWriter, EntityType, Vector3};
use std::{fs, path::Path};
fn spline(trim: bool, reversed: bool) -> SatDocument {
    let mut d = SatDocument::new_body();
    let curve = d.add_spline_curve(
        true,
        2,
        false,
        &[(0., 3), (1., 3)],
        &[[0., 0., 0.], [1., 2., 3.], [4., 0., 2.]],
        Some(&[1., 0.5, 1.25]),
        0.,
    );
    if reversed {
        d.records[curve as usize].tokens[1] = SatToken::Ident("reversed".into());
    }
    let from = if trim { 0.2 } else { 0. };
    let to = if trim { 0.8 } else { 1. };
    let pts = SatIntCurve::from_record(&d.records[curve as usize])
        .unwrap()
        .sample_range(from, to, 1);
    let mut vs = vec![];
    for (x, y, z) in pts {
        let p = d.add_point(x, y, z);
        vs.push(d.add_vertex(SatPointer::NULL, SatPointer::new(p)));
    }
    let (a, b) = if reversed { (-to, -from) } else { (from, to) };
    let edge = d.add_edge(
        SatPointer::new(vs[0]),
        a,
        SatPointer::new(vs[1]),
        b,
        SatPointer::NULL,
        SatPointer::new(curve),
        Sense::Forward,
    );
    let coedge = d.add_coedge(
        SatPointer::NULL,
        SatPointer::NULL,
        SatPointer::NULL,
        SatPointer::new(edge),
        Sense::Reversed,
        SatPointer::NULL,
    );
    let lp = d.add_loop(SatPointer::NULL, SatPointer::new(coedge), SatPointer::NULL);
    let plane = d.add_plane_surface([0., 0., 0.], [0., 0., 1.], [1., 0., 0.]);
    d.add_face(
        SatPointer::NULL,
        SatPointer::new(lp),
        SatPointer::NULL,
        SatPointer::new(plane),
        Sense::Forward,
        Sidedness::Single,
    );
    d
}
pub fn generate(dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    for version in [DxfVersion::AC1024, DxfVersion::AC1032] {
        let mut d = CadDocument::new();
        d.version = version;
        let shapes = [
            build_box([1., 2., 3.], 4., 5., 6.),
            build_cylinder([1., 2., 3.], 4., 5.),
            build_sphere([1., 2., 3.], 4.),
            build_torus([1., 2., 3.], 7., 2.),
            spline(false, false),
            spline(false, true),
            spline(true, false),
            spline(true, true),
        ];
        for shape in &shapes {
            d.add_entity(EntityType::Region(Region::from_sat(&shape.to_sat_string())))?;
        }
        for shape in &shapes[..4] {
            let sat = shape.to_sat_string();
            d.add_entity(EntityType::Solid3D(Solid3D::from_sat(&sat)))?;
            d.add_entity(EntityType::Body(Body::from_sat(&sat)))?;
            for kind in [SurfaceKind::Plane, SurfaceKind::Generic] {
                let mut s = Surface::new(kind);
                s.acis_data = AcisData::from_sat(&sat);
                d.add_entity(EntityType::Surface(Box::new(s)))?;
            }
        }
        for binary in [false, true] {
            let p = Path::new(dir).join(format!(
                "{version:?}-{}.dxf",
                if binary { "binary" } else { "ascii" }
            ));
            (if binary {
                DxfWriter::new_binary(&d)
            } else {
                DxfWriter::new(&d)
            })
            .write_to_file(p)?;
        }
    }
    Ok(())
}
fn point(a: &mut Vec<f64>, v: Vector3) {
    a.extend([v.x, v.y, v.z]);
}
fn shape(e: &EntityType) -> (String, Vec<f64>) {
    let mut a = vec![];
    let name = match e {
        EntityType::Line(e) => {
            point(&mut a, e.start);
            point(&mut a, e.end);
            "LINE"
        }
        EntityType::Circle(e) => {
            point(&mut a, e.center);
            a.push(e.radius);
            point(&mut a, e.normal);
            "CIRCLE"
        }
        EntityType::Ellipse(e) => {
            point(&mut a, e.center);
            point(&mut a, e.major_axis);
            point(&mut a, e.normal);
            a.extend([e.minor_axis_ratio, e.start_parameter, e.end_parameter]);
            "ELLIPSE"
        }
        EntityType::Spline(e) => {
            a.extend([
                e.degree as f64,
                e.flags.rational as i32 as f64,
                e.knots.len() as f64,
                e.control_points.len() as f64,
                e.fit_points.len() as f64,
            ]);
            a.extend(&e.knots);
            a.extend(&e.weights);
            for v in &e.control_points {
                point(&mut a, *v);
            }
            for v in &e.fit_points {
                point(&mut a, *v);
            }
            point(&mut a, e.normal);
            "SPLINE"
        }
        EntityType::Region(e) => {
            for w in &e.wires {
                for p in &w.points {
                    point(&mut a, *p);
                }
            }
            "WIRE-REGION"
        }
        EntityType::Surface(e) => {
            for w in &e.wires {
                for p in &w.points {
                    point(&mut a, *p);
                }
            }
            "WIRE-SURFACE"
        }
        _ => panic!("unsupported part"),
    };
    (name.into(), a)
}
fn close(a: &[f64], b: &[f64]) {
    assert_eq!(a.len(), b.len());
    for (i, (a, b)) in a.iter().zip(b).enumerate() {
        assert!(
            (a - b).abs() < 1e-10 * a.abs().max(b.abs()).max(1.),
            "modeler explode value {i}: {a} != {b}"
        );
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    for step in [0, 2, 3, 6, 13] {
        let mut rows = vec![];
        let mut serialized = CadDocument::new();
        for e in source.entities() {
            let mut e = e.clone();
            if step != 0 {
                e.apply_transform(&crate::entity_transform::operation(step));
            }
            let parts = e.explode();
            rows.push(("COUNT".into(), vec![parts.len() as f64]));
            rows.extend(parts.iter().map(shape));
            for p in parts {
                if matches!(
                    p,
                    EntityType::Line(_)
                        | EntityType::Circle(_)
                        | EntityType::Ellipse(_)
                        | EntityType::Spline(_)
                ) {
                    serialized.add_entity(p)?;
                }
            }
        }
        let actual = fs::read_to_string(format!("{prefix}.{step}.trace"))?;
        let lines: Vec<_> = actual.lines().collect();
        assert_eq!(lines.len(), rows.len());
        for (line, (name, values)) in lines.into_iter().zip(&rows) {
            let mut words = line.split_whitespace();
            assert_eq!(words.next(), Some(name.as_str()));
            close(
                &words.map(|s| s.parse::<f64>().unwrap()).collect::<Vec<_>>(),
                values,
            );
        }
        let reference_path = format!("{prefix}.{step}.rust-reference.dxf");
        DxfWriter::new(&serialized).write_to_file(&reference_path)?;
        let reference = DxfReader::from_file(&reference_path)?.read()?;
        let expected: Vec<_> = reference.entities().map(shape).collect();
        for encoding in ["ascii", "binary"] {
            let output = DxfReader::from_file(format!("{prefix}.{step}.{encoding}.dxf"))?.read()?;
            assert_eq!(output.entities().count(), expected.len());
            for (e, (name, values)) in output.entities().zip(&expected) {
                let (n, v) = shape(e);
                assert_eq!(&n, name);
                close(&v, values);
            }
        }
    }
    println!("Modeler decomposition: lines, circles, ellipses, rational/trimmed splines and face wires match Rust");
    Ok(())
}
