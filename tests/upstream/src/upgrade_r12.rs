// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, DxfVersion, DxfWriter, EntityType, Handle};
fn near(x: f64, y: f64) {
    assert!(
        (x - y).abs() <= 1e-9 * x.abs().max(y.abs()).max(1.),
        "{x} != {y}"
    );
}
fn verify_one(prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(format!("{prefix}-source.dxf"))?.read()?;
    let expected = crate::r12_export::geometry(&source)?;
    for version in [
        "AC1012", "AC1014", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032",
    ] {
        for format in ["ascii", "binary"] {
            let actual =
                DxfReader::from_file(format!("{prefix}-{version}-{format}.dxf"))?.read()?;
            assert_eq!(actual.version.to_dxf_string(), version);
            let values = crate::r12_export::geometry(&actual)?;
            assert_eq!(
                expected.len(),
                values.len(),
                "pair count {version} {format}"
            );
            for (i, ((a, x), (b, y))) in expected.iter().zip(&values).enumerate() {
                assert_eq!(a, b, "pair {i} {version} {format}: {x} vs {y}");
                if (10..=59).contains(a) || (210..=239).contains(a) {
                    near(x.trim().parse().unwrap(), y.trim().parse().unwrap());
                } else {
                    assert_eq!(x.trim(), y.trim(), "pair {i} group {a}");
                }
            }
            for original in source.layers.iter() {
                let saved = actual
                    .layers
                    .get(&original.name)
                    .expect("legacy layer preserved");
                let mut expected = original.clone();
                expected.handle = saved.handle;
                assert_eq!(saved, &expected);
            }
            for original in source.line_types.iter() {
                let saved = actual
                    .line_types
                    .get(&original.name)
                    .expect("legacy linetype preserved");
                let mut expected = original.clone();
                expected.handle = saved.handle;
                assert_eq!(saved, &expected);
            }
            for original in source.text_styles.iter() {
                let saved = actual
                    .text_styles
                    .get(&original.name)
                    .expect("legacy text style preserved");
                let mut expected = original.clone();
                expected.handle = saved.handle;
                assert_eq!(saved, &expected);
            }
            let mappings =
                std::fs::read_to_string(format!("{prefix}-{version}-{format}-handles.txt"))
                    .unwrap();
            for line in mappings.lines() {
                let p: Vec<_> = line.split_whitespace().collect();
                if matches!(
                    p[0],
                    "LINE"
                        | "CIRCLE"
                        | "ARC"
                        | "TEXT"
                        | "INSERT"
                        | "POLYLINE"
                        | "DIMENSION"
                        | "POINT"
                        | "SOLID"
                        | "TRACE"
                        | "3DFACE"
                        | "ATTDEF"
                ) {
                    let h = Handle::new(u64::from_str_radix(p[2], 16).unwrap());
                    let e = actual.get_entity(h).expect("mapped entity read by Rust");
                    assert!(!e.common().owner_handle.is_null());
                    let owner = actual
                        .block_records
                        .iter()
                        .find(|b| b.handle == e.common().owner_handle)
                        .expect("owner is block record");
                    assert!(owner.entity_handles.contains(&h));
                }
            }
            assert!(actual
                .entities()
                .any(|e| matches!(e,EntityType::Text(t) if t.value=="中文升级")));
            let modern = DxfWriter::new(&actual)
                .with_version(DxfVersion::AC1032)
                .write_to_vec()?;
            assert!(!modern.is_empty());
        }
    }
    println!("Rust verified R12 upgrade geometry, text, blocks and ownership across eight modern versions and both encodings");
    Ok(())
}

pub fn verify(prefix: &str) -> acadrust::Result<()> {
    for v in [
        "AC1012", "AC1014", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032",
    ] {
        for format in ["ascii", "binary"] {
            let doc = DxfReader::from_file(format!("{prefix}-layout-{v}-{format}.dxf"))?.read()?;
            let mut fonts: Vec<_> = doc.text_styles.iter().filter(|s| s.is_shape_file).collect();
            fonts.sort_by_key(|s| &s.font_file);
            assert_eq!(fonts.len(), 2);
            assert_eq!(fonts[0].font_file, "first.shx");
            assert_eq!(fonts[1].font_file, "second.shx");
            assert_ne!(fonts[0].handle, fonts[1].handle);
            let block = doc.block_records.get("XREF").unwrap();
            assert_eq!(block.xref_path, "assets/part.dwg");
            assert!(block.flags.is_xref);
            assert!(doc
                .entities()
                .any(|e| matches!(e, EntityType::Insert(i) if i.block_name=="XREF")));
            assert!(doc
                .entities()
                .any(|e| matches!(e, EntityType::Shape(s) if s.shape_name=="GLYPH" && s.size==5.)));
            let view = doc
                .entities()
                .find_map(|e| match e {
                    EntityType::Viewport(x) => Some(x),
                    _ => None,
                })
                .unwrap();
            assert_eq!(
                view.common.owner_handle,
                doc.block_records.get("*Paper_Space").unwrap().handle
            );
            near(view.view_height, 55.);
            near(view.twist_angle, 0.25);
            near(view.snap_angle, 0.5);
            assert_eq!(view.circle_sides, 64);
            assert_eq!(view.frozen_layers, [doc.layers.get("0").unwrap().handle]);
        }
    }
    verify_one(prefix)?;
    verify_one(&format!("{prefix}-handleless"))
}
