// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, DxfVersion, DxfWriter, EntityType};
use std::io::Cursor;
fn near(x: f64, y: f64) {
    assert!(
        (x - y).abs() < 1e-9 * x.abs().max(y.abs()).max(1.),
        "{x} != {y}"
    );
}
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    for suffix in ["", "-hatch", "-leader", "-multi", "-mline", "-dim", "-mesh"] {
        let source = DxfReader::from_file(format!("{prefix}{suffix}-source.dxf"))?.read()?;
        let expected = DxfWriter::new(&source)
            .with_version(DxfVersion::AC1009)
            .write_to_vec()?;
        let expected = DxfReader::from_reader(Cursor::new(expected))?.read()?;
        let actual = DxfReader::from_file(format!("{prefix}{suffix}-r12.dxf"))?.read()?;
        if suffix == "-mesh" {
            assert_eq!(actual.entities().filter(|e| matches!(e, EntityType::Face3D(_))).count(),4);
        }
        if suffix == "-leader" {
            assert_eq!(actual.version, DxfVersion::AC1009);
            assert_eq!(
                actual
                    .entities()
                    .filter(|e| matches!(e, EntityType::Line(_)))
                    .count(),
                4
            );
            assert_eq!(
                actual
                    .entities()
                    .filter(|e| matches!(e,EntityType::Text(t) if t.value=="关联文字"))
                    .count(),
                1
            );
            assert!(!actual
                .entities()
                .any(|e| matches!(e, EntityType::Leader(_) | EntityType::Solid(_))));
        }
        if suffix == "-multi" {
            assert_eq!(actual.version, DxfVersion::AC1009);
            let solids = actual
                .entities()
                .filter(|e| matches!(e, EntityType::Solid(_)))
                .count();
            assert_eq!(
                solids, 2,
                "two default arrows; custom and oversized arrows omitted"
            );
            let lines = actual
                .entities()
                .filter(|e| matches!(e, EntityType::Line(_)))
                .count();
            assert_eq!(
                lines, 6,
                "two broken shaft pieces, second shaft, two landings and model shaft"
            );
            for value in ["左侧", "右侧", "仅文字"] {
                assert!(actual
                    .entities()
                    .any(|e| matches!(e,EntityType::Text(t) if t.value==value)));
            }
        }
        if suffix == "-mline" {
            let lines: Vec<_> = actual
                .entities()
                .map(|e| match e {
                    EntityType::Line(l) => l,
                    _ => panic!("unexpected MLINE cap/fill or other primitive"),
                })
                .collect();
            assert_eq!(lines.len(), 14);
            // The positive track turns at the same offset vertex for both segments.
            let joint_x = 10. - 2. / 2f64.sqrt();
            let joint_y = 2. / 2f64.sqrt();
            assert!(lines.iter().any(|l| (l.start.x).abs() < 1e-9
                && (l.start.y - 2.).abs() < 1e-9
                && (l.end.x - joint_x).abs() < 1e-9
                && (l.end.y - joint_y).abs() < 1e-9));
            assert!(lines.iter().any(|l| (l.start.x - joint_x).abs() < 1e-9
                && (l.start.y - joint_y).abs() < 1e-9
                && (l.end.x - 8.).abs() < 1e-9
                && (l.end.y - 10.).abs() < 1e-9));
            assert_eq!(
                lines
                    .iter()
                    .filter(|l| l.start.z == 5. && l.end.z == 5.)
                    .count(),
                4
            );
        }
        if suffix == "-dim" {
            assert_eq!(actual.version, DxfVersion::AC1009);
            let lines: Vec<_> = actual
                .entities()
                .filter_map(|e| {
                    if let EntityType::Line(l) = e {
                        Some(l)
                    } else {
                        None
                    }
                })
                .collect();
            assert_eq!(
                lines.len(),
                9,
                "five radial extension/leader lines and four jog segments"
            );
            assert!(lines
                .iter()
                .any(|l| l.start.x == 1. && l.start.y == 2. && l.end.x == 6. && l.end.z == 3.));
            assert!(lines
                .iter()
                .any(|l| l.start.x == 3. && l.start.y == 4. && l.end.x == 6. && l.end.y == 8.));
            assert_eq!(
                actual
                    .entities()
                    .filter(|e| matches!(e, EntityType::Dimension(_)))
                    .count(),
                1
            );
            assert!(!actual.entities().any(|e| matches!(
                e,
                EntityType::Solid(_) | EntityType::Arc(_) | EntityType::Insert(_)
            )));
            let labels: Vec<_> = actual
                .entities()
                .filter_map(|e| {
                    if let EntityType::Text(t) = e {
                        Some(t.value.as_str())
                    } else {
                        None
                    }
                })
                .collect();
            assert_eq!(labels.len(), 3, "single space suppresses text");
            for expected in ["弧长 7.8540 / 7.8540", "31.4159", "R10.0000"] {
                assert!(labels.contains(&expected), "{labels:?}");
            }
        }
        let a = crate::r12_export::geometry(&expected)?;
        let b = crate::r12_export::geometry(&actual)?;
        assert_eq!(a.len(), b.len(), "R12 pair count {suffix}");
        for (i, ((ca, va), (cb, vb))) in a.iter().zip(&b).enumerate() {
            assert_eq!(ca, cb, "pair {i}: {va} vs {vb}");
            if (10..=59).contains(ca) || (210..=239).contains(ca) {
                near(va.trim().parse().unwrap(), vb.trim().parse().unwrap());
            } else {
                assert_eq!(va.trim(), vb.trim(), "pair {i} group {ca}");
            }
        }
    }
    let source = DxfReader::from_file(format!("{prefix}-mtext-source.dxf"))?.read()?;
    let expected: Vec<_> = source
        .entities()
        .filter(|e| matches!(e, EntityType::MText(_)))
        .flat_map(|e| e.explode())
        .collect();
    for version in ["AC1012", "AC1014", "AC1015", "AC1018"] {
        let actual = DxfReader::from_file(format!("{prefix}-mtext-{version}.dxf"))?.read()?;
        let actual: Vec<_> = actual
            .entities()
            .filter(|e| matches!(e, EntityType::Text(_)))
            .collect();
        assert_eq!(expected.len(), actual.len());
        for (a, b) in expected.iter().zip(actual) {
            let (EntityType::Text(a), EntityType::Text(b)) = (a, b) else {
                panic!("TEXT expected")
            };
            assert_eq!(a.value, b.value);
            assert_eq!(a.style, b.style);
            assert_eq!(a.common.color, b.common.color);
            near(a.insertion_point.x, b.insertion_point.x);
            near(a.insertion_point.y, b.insertion_point.y);
            near(a.insertion_point.z, b.insertion_point.z);
            near(a.height, b.height);
            near(a.rotation, b.rotation);
            near(a.width_factor, b.width_factor);
            assert_eq!(a.horizontal_alignment, b.horizontal_alignment);
            assert_eq!(a.vertical_alignment, b.vertical_alignment);
            near(a.normal.x, b.normal.x);
            near(a.normal.y, b.normal.y);
            near(a.normal.z, b.normal.z);
            assert_eq!(a.common.owner_handle, b.common.owner_handle);
        }
    }
    println!("Rust verified R12 curve, hatch, leader, MLINE and dimension geometry and text, and MTEXT decomposition across R13-R2004");
    Ok(())
}
