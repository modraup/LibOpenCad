// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::*;
use acadrust::entities::Body;
use acadrust::{CadDocument, DxfReader, DxfVersion, DxfWriter, EntityType};
use std::{fs, path::Path};
fn store(dir: &str, name: &str, d: SatDocument) {
    let path = Path::new(dir).join(name);
    fs::write(path.with_extension("sat"), d.to_sat_string()).unwrap();
    fs::write(path.with_extension("sab"), SabWriter::write(&d)).unwrap();
    for version in [DxfVersion::AC1024, DxfVersion::AC1032] {
        let mut doc = CadDocument::new();
        doc.version = version;
        doc.add_entity(EntityType::Body(Body::from_sat(&d.to_sat_string())))
            .unwrap();
        for binary in [false, true] {
            let path = format!(
                "{}.{}-{}.dxf",
                path.display(),
                if version == DxfVersion::AC1024 {
                    "2010"
                } else {
                    "2018"
                },
                if binary { "binary" } else { "ascii" }
            );
            let w = if binary {
                DxfWriter::new_binary(&doc)
            } else {
                DxfWriter::new(&doc)
            };
            w.write_to_file(path).unwrap();
        }
    }
}
fn raw(records: &str) -> SatDocument {
    SatDocument::parse(&format!(
        "700 0 0 0\n@8 acadrust @8 ACIS 7.0 @0 \n1 1e-6 1e-10\n{records}\nEnd-of-ACIS-data\n"
    ))
    .unwrap()
}
pub fn generate(dir: &str) {
    fs::create_dir_all(dir).unwrap();
    for rational in [false, true] {
        let suffix = if rational { "r" } else { "n" };
        for degree in 1..=3 {
            let mut d = SatDocument::new_body();
            let controls: Vec<_> = (0..=degree)
                .map(|i| [i as f64, ((i * i + 1) % 4) as f64, (i % 2) as f64])
                .collect();
            let weights: Vec<_> = (0..=degree)
                .map(|i| if i % 2 == 0 { 1. } else { 0.5 })
                .collect();
            d.add_spline_curve(
                rational,
                degree,
                true,
                &[(0., degree + 1), (1., degree + 1)],
                &controls,
                Some(&weights),
                0.,
            );
            store(dir, &format!("curve-{suffix}-{degree}"), d);
        }
        for degree in 1..=2 {
            let mut d = SatDocument::new_body();
            let mut control = vec![];
            for v in 0..=degree {
                for u in 0..=degree {
                    control.push([u as f64, v as f64, (u * v) as f64]);
                }
            }
            let weights: Vec<_> = (0..control.len())
                .map(|i| if i % 2 == 0 { 1. } else { 0.5 })
                .collect();
            let h = d.add_spline_surface(
                true,
                rational,
                degree,
                degree,
                true,
                false,
                &[(0., degree + 1), (1., degree + 1)],
                &[(0., degree + 1), (1., degree + 1)],
                &control,
                Some(&weights),
                1e-8,
            );
            d.add_pcurve(
                rational,
                1,
                false,
                &[(0., 2), (1., 2)],
                &[[0., 0.], [1., 1.]],
                Some(&[1., 0.5]),
                0.,
                h,
                (2., -3.),
            );
            store(dir, &format!("surface-{suffix}-{degree}"), d);
        }
        let mut d = SatDocument::new_body();
        let h = d.add_plane_surface([1., 2., 3.], [0., 0., 1.], [1., 0., 0.]);
        d.add_pcurve(
            rational,
            2,
            true,
            &[(0., 3), (1., 3)],
            &[[0., 0.], [1., 2.], [3., -1.]],
            Some(&[1., 0.5, 1.25]),
            1e-7,
            h,
            (2., -3.),
        );
        store(dir, &format!("pcurve-{suffix}"), d);
    }
    let mut d = SatDocument::new_body();
    let knots = [(0., 2), (1., 2)];
    let control = [[0., 0., 0.], [1., 0., 0.], [0., 1., 1.], [1., 1., 2.]];
    let approximation = control.map(|[x, y, z]| [x, y, z + 2.]);
    let base = SplineNet {
        rational: false,
        deg_u: 1,
        deg_v: 1,
        u_closed: false,
        v_closed: false,
        u_knots: &knots,
        v_knots: &knots,
        control: &control,
        weights: None,
        fit_tol: 0.,
    };
    let approx = SplineNet {
        rational: true,
        control: &approximation,
        weights: Some(&[1., 0.5, 1., 1.25]),
        ..base
    };
    d.add_offset_spline_surface(true, true, &base, 2., &approx);
    store(dir, "built-offset", d);
    let mut d = SatDocument::new_body();
    for reverse_curve in [false, true] {
        let c = d.add_spline_curve(
            true,
            2,
            false,
            &[(0., 3), (1., 3)],
            &[[0., 0., 0.], [1., 2., 3.], [4., 0., 2.]],
            Some(&[1., 0.5, 1.25]),
            0.,
        );
        if reverse_curve {
            d.records[c as usize].tokens[1] = SatToken::Ident("reversed".into());
        }
        let points = SatIntCurve::from_record(&d.records[c as usize])
            .unwrap()
            .sample_range(0.2, 0.8, 1);
        let mut vertices = vec![];
        for (x, y, z) in points {
            let p = d.add_point(x, y, z);
            vertices.push(d.add_vertex(SatPointer::NULL, SatPointer::new(p)));
        }
        for sense in [Sense::Forward, Sense::Reversed] {
            d.add_edge(
                SatPointer::new(vertices[0]),
                0.,
                SatPointer::new(vertices[1]),
                0.,
                SatPointer::NULL,
                SatPointer::new(c),
                sense,
            );
            d.add_edge(
                SatPointer::new(vertices[0]),
                0.,
                SatPointer::new(vertices[0]),
                0.,
                SatPointer::NULL,
                SatPointer::new(c),
                sense,
            );
        }
    }
    d.header.version = SatVersion::V4_0;
    store(dir, "legacy-spline", d);
    let profile = "int_int_cur 21500 0 nubs 1 0 2 0 1 1 1 10 0 0 10 0 5";
    for (name,body) in [
 ("revolution",format!("rot_spl_sur 20900 intcurve F {{ {profile} }} I I 0 0 0 0 0 1 2 F -1 F 0 F 0 F 1.5707963267948966")),
 ("revolution-full",format!("rot_spl_sur 20900 intcurve F {{ {profile} }} I I 2 3 4 1 2 3 2 F 0 F 1 F 0 F 6.283185307179586")),
 ("extrusion",format!("cyl_spl_sur {{ {profile} }} I I 1 2 3 0 0 1 2 F 0 F 1 F -2 F 5")),
 ("sum",format!("sum_spl_sur 20900 intcurve F {{ {profile} }} I I straight 1 2 3 0 0 1 I I -1 1 2 2 F -1 F 0 F 0 F 5")),
 ]{store(dir,name,raw(&format!("-0 spline-surface $-1 -1 $-1 forward {{ {body} }} I I I I #")));}
    let net =
        "exactsur full nubs 1 1 open open none none 2 2 0 1 1 1 0 1 1 1 0 0 0 1 0 0 0 1 1 1 1 2 0";
    store(dir,"shared",raw(&format!("-0 spline-surface $-1 -1 $-1 reversed {{ {net} }} I I I I #\n-1 bs3-surface $-1 -1 $-1 forward {{ ref 0 }} I I I I #\n-2 meshsurf-surface $-1 -1 $-1 forward {{ ref 0 }} I I I I #\n-3 intcurve-curve $-1 -1 $-1 reversed {{ exactcur full nubs 1 periodic 2 0 1 1 1 0 0 0 1 2 3 0 }} I I #\n-4 bs3-curve $-1 -1 $-1 forward {{ ref 1 }} I I #")));
    store(dir,"offset",raw(&format!("-0 spline-surface $-1 -1 $-1 forward {{ offsur spline forward {{ {net} }} I I I I 2 forward forward full nubs 1 1 open open none none 2 2 0 1 1 1 0 1 1 1 0 0 2 1 0 2 0 1 3 1 1 4 0 }} I I I I #")));
    store(dir,"support",raw(&format!("-0 intcurve-curve $-1 -1 $-1 forward {{ off_surf_int_cur nullbs spline forward {{ {net} }} I I I I null_surface nubs 1 open 2 0 1 1 1 0 0 1 1 2 0 0 }} I I #")));
    store(dir,"referenced-pcurve",raw("-0 intcurve-curve $-1 -1 $-1 reversed { par_int_cur full nubs 1 open 2 0 1 1 1 0 0 0 1 2 3 0 null_surface null_surface nubs 1 open 2 0 1 1 1 0 0 2 3 nurbs 1 open 2 0 1 1 1 1 2 1 3 4 0.5 I I 0 0 0 } I I #\n-1 intcurve-curve $-1 -1 $-1 forward { ref 0 } I I #\n-2 pcurve $-1 -1 $-1 -1 forward $1 2 -3 #\n-3 pcurve $-1 -1 $-1 2 reversed $0 -2 3 #"));
}
fn nums(out: &mut String, name: &str, values: impl IntoIterator<Item = f64>) {
    out.push_str(name);
    for v in values {
        out.push_str(&format!(" {v:.17e}"));
    }
    out.push('\n');
}
fn curve(out: &mut String, c: Option<(usize, Vec<f64>, Vec<[f64; 4]>)>) {
    match c {
        None => out.push_str("curve none\n"),
        Some((degree, knots, points)) => {
            nums(
                out,
                "curve",
                [degree as f64, knots.len() as f64, points.len() as f64],
            );
            nums(out, "knots", knots);
            nums(out, "controls", points.into_iter().flatten());
        }
    }
}
fn pc(out: &mut String, c: Option<(usize, Vec<f64>, Vec<[f64; 3]>)>) {
    match c {
        None => out.push_str("uv none\n"),
        Some((degree, knots, points)) => {
            nums(
                out,
                "uv",
                [degree as f64, knots.len() as f64, points.len() as f64],
            );
            nums(out, "knots", knots);
            nums(out, "controls", points.into_iter().flatten());
        }
    }
}
fn net(out: &mut String, c: Option<SatBSplineSurface>) {
    match c {
        None => out.push_str("net none\n"),
        Some(c) => {
            nums(
                out,
                "net",
                [
                    if c.rational { 1. } else { 0. },
                    c.degree_u as f64,
                    c.degree_v as f64,
                    c.control_count_u as f64,
                    c.control_count_v as f64,
                ],
            );
            out.push_str(&format!(
                "forms {} {} {} {}\n",
                c.u_closure.as_deref().unwrap_or("-"),
                c.v_closure.as_deref().unwrap_or("-"),
                c.u_singularity.as_deref().unwrap_or("-"),
                c.v_singularity.as_deref().unwrap_or("-")
            ));
            nums(out, "u", c.u_knots);
            nums(out, "v", c.v_knots);
            nums(out, "controls", c.control_points.into_iter().flatten());
            if let Some(t) = c.fit_tolerance {
                nums(out, "tolerance", [t]);
            } else {
                out.push_str("tolerance none\n");
            }
        }
    }
}
fn trace(d: &SatDocument) -> String {
    let mut out = String::new();
    for r in &d.records {
        if let Some(c) = SatIntCurve::from_record(r) {
            nums(
                &mut out,
                "int",
                [
                    r.index as f64,
                    if c.sense() == Sense::Reversed { 1. } else { 0. },
                    if c.is_closed_in(d) { 1. } else { 0. },
                ],
            );
            curve(&mut out, c.bspline());
            curve(&mut out, c.bspline_in(d));
            for n in [0, 7] {
                let points = c.sample(n);
                nums(
                    &mut out,
                    "sample",
                    points.into_iter().flat_map(|(x, y, z)| [x, y, z]),
                );
            }
            for (a, b) in [(-0.2, 0.7), (0.7, -0.2), (3., 4.), (0., 0.)] {
                nums(
                    &mut out,
                    "range",
                    c.sample_range(a, b, 7)
                        .into_iter()
                        .flat_map(|(x, y, z)| [x, y, z]),
                );
            }
            if let Some((surface, uv, offset)) = c.support_in(d) {
                out.push_str("support yes\n");
                net(&mut out, Some(surface));
                pc(&mut out, Some(uv));
                nums(&mut out, "offset", [offset]);
            } else {
                out.push_str("support none\n");
            }
        }
        if let Some(c) = SatPCurve::from_record(r) {
            nums(
                &mut out,
                "pc",
                [
                    r.index as f64,
                    if c.sense() == Sense::Reversed { 1. } else { 0. },
                ],
            );
            pc(&mut out, c.bspline_in(d));
            nums(
                &mut out,
                "sample",
                c.sample(7).into_iter().flat_map(|(u, v)| [u, v]),
            );
            nums(
                &mut out,
                "sample-in",
                c.sample_in(d, 7).into_iter().flat_map(|(u, v)| [u, v]),
            );
        }
        if let Some(s) = SatSplineSurface::from_record(r) {
            nums(
                &mut out,
                "surface",
                [
                    r.index as f64,
                    match s.kind() {
                        SatSplineSurfaceKind::Spline => 0.,
                        SatSplineSurfaceKind::Mesh => 1.,
                        SatSplineSurfaceKind::Bs3 => 2.,
                    },
                    if s.sense() == Sense::Reversed { 1. } else { 0. },
                ],
            );
            net(&mut out, s.bspline(d));
            if let Some((base, offset)) = s.offset_in(d) {
                out.push_str("offset-surface yes\n");
                net(&mut out, Some(base));
                nums(&mut out, "offset", [offset]);
            } else {
                out.push_str("offset-surface none\n");
            }
        }
    }
    for i in 0..16 {
        nums(
            &mut out,
            "subtype",
            [
                i as f64,
                d.subtype_tokens(i).map(|t| t.len() as f64).unwrap_or(-1.),
            ],
        );
    }
    out
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let d = if input.ends_with(".sab") {
        SabReader::read(&fs::read(input)?).unwrap()
    } else if input.ends_with(".dxf") {
        let doc = DxfReader::from_file(input)?.read()?;
        let EntityType::Body(e) = doc.entities().next().unwrap() else {
            panic!("body")
        };
        e.acis_data.parse().unwrap()
    } else {
        SatDocument::parse(&fs::read_to_string(input)?).unwrap()
    };
    if input.contains("/surface-")
        || input.contains("/revolution")
        || input.contains("/extrusion.")
        || input.contains("/sum.")
        || input.contains("/shared.")
        || input.contains("/offset.")
        || input.contains("/built-offset.")
    {
        let surfaces: Vec<_> = d
            .records
            .iter()
            .filter_map(SatSplineSurface::from_record)
            .collect();
        assert!(!surfaces.is_empty());
        for surface in surfaces {
            assert!(
                surface.bspline(&d).is_some(),
                "surface decode acceptance: {input}"
            );
        }
    }
    if input.contains("/support.") {
        assert!(
            d.records
                .iter()
                .filter_map(SatIntCurve::from_record)
                .any(|c| c.support_in(&d).is_some()),
            "curve support acceptance"
        );
    }
    if input.contains("/offset.") || input.contains("/built-offset.") {
        assert!(
            d.records
                .iter()
                .filter_map(SatSplineSurface::from_record)
                .any(|c| c.offset_in(&d).is_some()),
            "offset surface acceptance"
        );
    }
    if input.contains("/pcurve-") || input.contains("/referenced-pcurve.") {
        let curves: Vec<_> = d
            .records
            .iter()
            .filter_map(SatPCurve::from_record)
            .collect();
        assert!(!curves.is_empty());
        for curve in curves {
            assert_eq!(curve.sample_in(&d, 7).len(), 8, "UV sampling acceptance");
        }
    }
    let expected = trace(&d);
    let actual = fs::read_to_string(format!("{prefix}.trace"))?;
    let a: Vec<_> = actual.split_whitespace().collect();
    let b: Vec<_> = expected.split_whitespace().collect();
    assert_eq!(
        a.len(),
        b.len(),
        "trace lengths {input}\nactual {actual}\nexpected {expected}"
    );
    for (i, (a, b)) in a.iter().zip(&b).enumerate() {
        match (a.parse::<f64>(), b.parse::<f64>()) {
            (Ok(x), Ok(y)) => assert!(
                (x.is_nan() && y.is_nan())
                    || x == y
                    || (x - y).abs() <= 1e-11 * x.abs().max(y.abs()).max(1.),
                "token {i}: {a} != {b}: {input}"
            ),
            _ => assert_eq!(a, b, "token {i}: {input}"),
        }
    }
    crate::acis::compare(
        &crate::acis::canonical(&d),
        &SatDocument::parse(&fs::read_to_string(format!("{prefix}.sat"))?).unwrap(),
    );
    let mut upgraded = d.clone();
    upgrade_records_from_v400(&mut upgraded);
    crate::acis::compare(
        &crate::acis::canonical(&upgraded),
        &SatDocument::parse(&fs::read_to_string(format!("{prefix}.upgraded.sat"))?).unwrap(),
    );
    let native = fs::read(format!("{prefix}.sab"))?;
    assert_eq!(native, SabWriter::write(&d), "SAB bytes: {input}");
    if input.ends_with(".dxf") {
        let mut expected = DxfReader::from_file(input)?.read()?;
        let h = expected.entities().next().unwrap().common().handle;
        expected
            .get_entity_mut(h)
            .unwrap()
            .apply_transform(&crate::entity_transform::operation(13));
        for format in ["ascii", "binary"] {
            let actual = DxfReader::from_file(format!("{prefix}.{format}.dxf"))?.read()?;
            let EntityType::Body(a) = actual.entities().next().unwrap() else {
                panic!("body")
            };
            let EntityType::Body(e) = expected.entities().next().unwrap() else {
                panic!("body")
            };
            crate::acis::compare(
                &crate::acis::canonical(&e.acis_data.parse().unwrap()),
                &crate::acis::canonical(&a.acis_data.parse().unwrap()),
            );
        }
    }
    println!("ACIS spline trace and SAT/SAB match Rust: {input}");
    Ok(())
}

pub fn verify_suite(input: &str, prefix: &str) -> acadrust::Result<()> {
    for ext in [
        ".sat",
        ".sab",
        ".2010-ascii.dxf",
        ".2010-binary.dxf",
        ".2018-ascii.dxf",
        ".2018-binary.dxf",
    ] {
        verify(&format!("{input}{ext}"), &format!("{prefix}{ext}"))?;
    }
    if Path::new(&format!("{prefix}.built.sat")).exists() {
        let reference = SatDocument::parse(&fs::read_to_string(format!("{input}.sat"))?).unwrap();
        let built =
            SatDocument::parse(&fs::read_to_string(format!("{prefix}.built.sat"))?).unwrap();
        crate::acis::compare(&crate::acis::canonical(&reference), &built);
        assert_eq!(
            fs::read(format!("{prefix}.built.sab"))?,
            fs::read(format!("{input}.sab"))?,
            "native construction SAB"
        );
    }
    Ok(())
}
