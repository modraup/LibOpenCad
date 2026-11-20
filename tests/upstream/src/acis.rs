// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{primitives::*, *};
use std::{fs, path::Path};

pub fn generate(dir: &str) {
    fs::create_dir_all(dir).unwrap();
    let samples = [
        ("box", build_box([1., 2., 3.], 4., 6., 8.)),
        (
            "planar",
            build_planar_body(
                &[[1., 2., 3.], [5., 2., 3.], [1., 8., 3.], [1., 2., 11.]],
                &[vec![0, 2, 1], vec![0, 1, 3], vec![1, 2, 3], vec![2, 0, 3]],
            )
            .unwrap(),
        ),
        ("wedge", build_wedge([-2., 3., 1.], 5., 6., 7.)),
        ("pyramid", build_pyramid([2., -3., 4.], 8., 9.)),
        ("cylinder", build_cylinder([1., -2., 3.], 4., 5.)),
        ("cone", build_cone([2., 3., 4.], 5., 6.)),
        ("sphere", build_sphere([-1., 2., 3.], 4.)),
        ("torus", build_torus([1., 2., -3.], 7., 2.)),
    ];
    for (name, d) in samples {
        for version in [400, 700, 21800] {
            let mut d = d.clone();
            d.header.version = SatVersion::from_sat_number(version);
            fs::write(
                Path::new(dir).join(format!("{name}-{version}.sat")),
                d.to_sat_string(),
            )
            .unwrap();
            if version >= 700 {
                fs::write(
                    Path::new(dir).join(format!("{name}-{version}.sab")),
                    SabWriter::write(&SatDocument::parse(&d.to_sat_string()).unwrap()),
                )
                .unwrap();
            }
        }
    }
    let legacy="106 0 1 0\n-0 body $-1 $1 $-1 $-1 #\n-1 lump $-1 $-1 $2 $0 #\n-2 shell $-1 $-1 $-1 $-1 $-1 $1 #\nEnd-of-ACIS-data\n";
    fs::write(Path::new(dir).join("legacy-106.sat"), legacy).unwrap();
    let special="700 0 0 1\n@8 acadrust @8 ACIS 7.0 @0 \n1 1e-6 1e-10\n-0 point $-1 -1 $-1 -0 1e-200 1e200 #\n-1 body $-1 -1 $-1 $2 $-1 $-1 #\n-2 lump $-1 -1 $-1 $-1 $-1 $1 #\n-3 body $-1 -1 $1 $4 $-1 $-1 #\n-4 lump $-1 -1 $-1 $-1 $-1 $3 #\n-5 custom-attrib $-1 -1 $-1 $-1 $0 @17 中文标签 # 1 T F unknown { spline 3 } #\nEnd-of-ACIS-data\n";
    fs::write(Path::new(dir).join("multi-700.sat"), special).unwrap();
    fs::write(
        Path::new(dir).join("multi-700.sab"),
        SabWriter::write(&SatDocument::parse(special).unwrap()),
    )
    .unwrap();
}
fn read(path: &str) -> SatDocument {
    SatDocument::parse(&fs::read_to_string(path).unwrap()).unwrap()
}
pub(crate) fn canonical(d: &SatDocument) -> SatDocument {
    SatDocument::parse(&d.to_sat_string()).unwrap()
}
fn number(a: f64, b: f64) {
    assert!(
        (a - b).abs() <= 1e-12 * a.abs().max(b.abs()).max(1.),
        "{a} != {b}"
    );
    if a == 0. && b == 0. {
        assert_eq!(a.is_sign_negative(), b.is_sign_negative());
    }
}
pub(crate) fn compare(a: &SatDocument, b: &SatDocument) {
    assert_eq!(a.header, b.header, "header");
    assert_eq!(a.records.len(), b.records.len());
    for (a, b) in a.records.iter().zip(&b.records) {
        assert_eq!(
            (
                a.index,
                &a.entity_type,
                &a.sub_type,
                a.attribute,
                a.subtype_id
            ),
            (
                b.index,
                &b.entity_type,
                &b.sub_type,
                b.attribute,
                b.subtype_id
            )
        );
        assert_eq!(a.tokens.len(), b.tokens.len(), "{} tokens", a.entity_type);
        for (a, b) in a.tokens.iter().zip(&b.tokens) {
            match (a.as_float(), b.as_float()) {
                (Some(a), Some(b)) => number(a, b),
                _ => assert_eq!(a, b),
            }
        }
    }
}
fn trace(d: &SatDocument) -> Vec<f64> {
    let mut out = Vec::new();
    macro_rules! ptrs {($($p:expr),*)=>{$(out.push($p.0 as f64);)*};}
    macro_rules! xyz {
        ($v:expr) => {{
            let v = $v;
            out.extend([v.0, v.1, v.2]);
        }};
    }
    for r in &d.records {
        out.push(r.index as f64);
        if let Some(v) = SatBody::from_record(r) {
            ptrs!(v.next_body(), v.lump(), v.wire_body(), v.transform());
        }
        if let Some(v) = SatLump::from_record(r) {
            ptrs!(v.next_lump(), v.shell(), v.body());
        }
        if let Some(v) = SatShell::from_record(r) {
            ptrs!(v.next_shell(), v.subshell(), v.face(), v.wire(), v.lump());
        }
        if let Some(v) = SatFace::from_record(r) {
            ptrs!(
                v.attribute(),
                v.next_face(),
                v.first_loop(),
                v.shell(),
                v.subshell(),
                v.surface()
            );
            out.push((v.sense() == Sense::Reversed) as u8 as f64);
            out.push((v.sidedness() == Sidedness::Double) as u8 as f64);
        }
        if let Some(v) = SatLoop::from_record(r) {
            ptrs!(v.next_loop(), v.first_coedge(), v.face());
        }
        if let Some(v) = SatCoedge::from_record(r) {
            ptrs!(v.next(), v.prev(), v.partner(), v.edge());
            out.push((v.sense() == Sense::Reversed) as u8 as f64);
            ptrs!(v.owner_loop(), v.pcurve());
        }
        if let Some(v) = SatEdge::from_record(r) {
            ptrs!(v.start_vertex());
            out.push(v.start_param());
            ptrs!(v.end_vertex());
            out.push(v.end_param());
            ptrs!(v.coedge(), v.curve());
            out.push((v.sense() == Sense::Reversed) as u8 as f64);
        }
        if let Some(v) = SatVertex::from_record(r) {
            ptrs!(v.edge(), v.point());
        }
        if let Some(v) = SatPoint::from_record(r) {
            xyz!(v.position());
        }
        if let Some(v) = SatStraightCurve::from_record(r) {
            xyz!(v.root_point());
            xyz!(v.direction());
        }
        if let Some(v) = SatEllipseCurve::from_record(r) {
            xyz!(v.center());
            xyz!(v.normal());
            xyz!(v.major_axis());
            out.push(v.ratio());
        }
        if let Some(v) = SatPlaneSurface::from_record(r) {
            xyz!(v.root_point());
            xyz!(v.normal());
            xyz!(v.u_direction());
            out.push((v.sense() == Sense::Reversed) as u8 as f64);
        }
        if let Some(v) = SatConeSurface::from_record(r) {
            xyz!(v.center());
            xyz!(v.axis());
            xyz!(v.major_axis());
            out.extend([
                v.ratio(),
                v.sin_half_angle(),
                v.cos_half_angle(),
                v.radius(),
                (v.sense() == Sense::Reversed) as u8 as f64,
            ]);
        }
        if let Some(v) = SatSphereSurface::from_record(r) {
            xyz!(v.center());
            out.push(v.radius());
            xyz!(v.u_direction());
            xyz!(v.pole());
            out.push((v.sense() == Sense::Reversed) as u8 as f64);
        }
        if let Some(v) = SatTorusSurface::from_record(r) {
            xyz!(v.center());
            xyz!(v.normal());
            out.extend([v.major_radius(), v.minor_radius()]);
            xyz!(v.u_direction());
            out.push((v.sense() == Sense::Reversed) as u8 as f64);
        }
    }
    let (m, t, s) = d.placement();
    out.extend(m.into_iter().flatten());
    out.extend(t);
    out.push(s);
    out.push(d.validate().len() as f64);
    out
}
pub fn verify(input: &str, prefix: &str) {
    let original = read(input);
    if original.header.version.major < 7 {
        assert_eq!(
            fs::read(format!("{prefix}.upgrade.sab")).unwrap(),
            SabWriter::write(&original),
            "Pre-v700 SAB upgrade"
        );
        let mut upgraded = original.clone();
        acadrust::entities::acis::upgrade_records_from_v400(&mut upgraded);
        compare(
            &canonical(&upgraded),
            &read(&format!("{prefix}.upgrade.sat")),
        );
    }
    if !input.contains("legacy-") && !input.contains("multi-") {
        compare(&canonical(&original), &read(&format!("{prefix}.built.sat")));
        if original.header.version.major >= 7 {
            assert_eq!(
                fs::read(format!("{prefix}.built.sab")).unwrap(),
                SabWriter::write(&original),
                "Native primitive SAB topology"
            );
        }
    }

    if original.header.version.major >= 7 {
        assert_eq!(
            fs::read(format!("{prefix}.sab")).unwrap(),
            SabWriter::write(&original),
            "SAT to SAB binary tags"
        );
        let source = fs::read(Path::new(input).with_extension("sab")).unwrap();
        let (binary, consumed) = SabReader::read_with_consumed(&source).unwrap();
        assert_eq!(consumed, source.len());
        assert_eq!(
            fs::read(format!("{prefix}.saved.sab")).unwrap(),
            SabWriter::write(&binary),
            "SAB round trip bytes"
        );
        compare(&canonical(&binary), &read(&format!("{prefix}.sab.sat")));
        let actual = fs::read_to_string(format!("{prefix}.sab.trace")).unwrap();
        let actual: Vec<f64> = actual
            .split_whitespace()
            .map(|s| s.parse().unwrap())
            .collect();
        let expected = trace(&binary);
        assert_eq!(expected.len(), actual.len());
        for (a, b) in expected.iter().zip(actual) {
            number(*a, b);
        }
    }
    compare(&canonical(&original), &read(&format!("{prefix}.sat")));
    let actual = fs::read_to_string(format!("{prefix}.trace")).unwrap();
    let values: Vec<f64> = actual
        .split_whitespace()
        .map(|v| v.parse().unwrap())
        .collect();
    let expected = trace(&original);
    assert_eq!(values.len(), expected.len());
    for (a, b) in expected.iter().zip(&values) {
        number(*a, *b);
    }
    let mut moved = original.clone();
    moved.set_placement(
        [[0., -1., 0.], [1., 0.25, 0.], [0., 0., -1.]],
        [10., -20., 30.],
        2.,
    );
    moved.complete_brep_links();
    compare(&canonical(&moved), &read(&format!("{prefix}.moved.sat")));
    let mut stripped = original.clone();
    stripped.strip_for_sab();
    compare(
        &canonical(&stripped),
        &read(&format!("{prefix}.stripped.sat")),
    );
    let split = original.split_bodies();
    assert_eq!(
        fs::read_to_string(format!("{prefix}.count"))
            .unwrap()
            .trim()
            .parse::<usize>()
            .unwrap(),
        split.len()
    );
    for (i, d) in split.iter().enumerate() {
        compare(&canonical(d), &read(&format!("{prefix}.split-{i}.sat")));
    }
    println!("SAT: {} records, {} typed values, placement, link completion, stripping and {} split bodies match Rust",original.records.len(),values.len(),split.len());
}

pub fn generate_sab(dir: &str) {
    let mut d = SatDocument::new();
    d.header.version = SatVersion::from_sat_number(22300);
    d.header.has_history = true;
    let mut r = SatRecord::new(0, "future-custom-attrib");
    let raw = |tag, data| SatToken::Sab { tag, data };
    r.tokens = vec![
        SatToken::Pointer(SatPointer::NULL),
        raw(2, (-7i8).to_le_bytes().to_vec()),
        raw(3, (-1234i16).to_le_bytes().to_vec()),
        raw(4, (-123456i32).to_le_bytes().to_vec()),
        raw(5, (-0.0f32).to_le_bytes().to_vec()),
        raw(6, (1.23456789012345f64).to_le_bytes().to_vec()),
        raw(21, (8765i32).to_le_bytes().to_vec()),
        raw(23, (9007199254740993i64).to_le_bytes().to_vec()),
        raw(
            19,
            [1.25f64, -2.5, 3.75]
                .into_iter()
                .flat_map(f64::to_le_bytes)
                .collect(),
        ),
        raw(
            20,
            [-0.0f64, 1., -1.]
                .into_iter()
                .flat_map(f64::to_le_bytes)
                .collect(),
        ),
        raw(
            22,
            [2.5f64, -3.5]
                .into_iter()
                .flat_map(f64::to_le_bytes)
                .collect(),
        ),
        raw(15, vec![]),
        raw(13, [vec![8], b"new_type".to_vec()].concat()),
        raw(16, vec![]),
        SatToken::True,
        SatToken::False,
    ];
    for (tag, text) in [
        (7, "中文标签".to_string()),
        (8, "a".repeat(300)),
        (9, "b".repeat(66000)),
        (18, "1 0 0 0 1 0 0 0 1 10 20 30 1".to_string()),
    ] {
        let len = text.len();
        let mut data = match tag {
            7 => vec![len as u8],
            8 => (len as u16).to_le_bytes().to_vec(),
            _ => (len as u32).to_le_bytes().to_vec(),
        };
        data.extend(text.as_bytes());
        r.tokens.push(raw(tag, data));
    }
    d.add_record(r);
    fs::write(Path::new(dir).join("raw-22300.sab"), SabWriter::write(&d)).unwrap();
    // A legacy packet needs its synthetic pattern fields removed. Use a
    // simple pre-v700 point instead of relabelling a modern topology body.
    let mut p = SatDocument::new();
    let mut r = SatRecord::new(0, "point");
    r.tokens = vec![
        SatToken::Float(2.),
        SatToken::Float(3.),
        SatToken::Float(4.),
    ];
    p.add_record(r);
    let mut older = SabWriter::write(&p);
    older[15..19].copy_from_slice(&600u32.to_le_bytes());
    fs::write(Path::new(dir).join("legacy-600.sab"), older).unwrap();
}
pub fn verify_sab(input: &str, prefix: &str) {
    let source = fs::read(input).unwrap();
    let (d, n) = SabReader::read_with_consumed(&source).unwrap();
    assert_eq!(n, source.len());
    compare(&canonical(&d), &read(&format!("{prefix}.sat")));
    let actual = fs::read_to_string(format!("{prefix}.trace")).unwrap();
    let actual: Vec<f64> = actual
        .split_whitespace()
        .map(|s| s.parse().unwrap())
        .collect();
    let expected = trace(&d);
    assert_eq!(actual.len(), expected.len());
    for (a, b) in expected.iter().zip(actual) {
        number(*a, b);
    }
    {
        assert_eq!(
            fs::read(format!("{prefix}.sab")).unwrap(),
            SabWriter::write(&d)
        );
    }
    println!("SAB raw numeric widths, coordinates, strings and exact payload bytes match Rust");
}
