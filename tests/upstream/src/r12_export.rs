// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, DxfReader, DxfVersion, DxfWriter, EntityType};
use std::io::Cursor;
// Canonicalize both Rust-read documents through the same R12 writer. Compare
// all block/entity pairs, including inline vertices, attributes and paper flags.
// No generated modern handle/owner identities participate in this comparison.
pub(crate) fn geometry(doc: &CadDocument) -> acadrust::Result<Vec<(i32, String)>> {
    let bytes = DxfWriter::new(doc)
        .with_version(DxfVersion::AC1009)
        .write_to_vec()?;
    let text = String::from_utf8_lossy(&bytes);
    let lines: Vec<_> = text.lines().collect();
    let mut section = String::new();
    let mut section_name = false;
    let mut pairs = vec![];
    for pair in lines.chunks_exact(2) {
        let code = pair[0].trim().parse::<i32>().unwrap();
        let value = pair[1].to_string();
        if code == 0 && value == "SECTION" {
            section_name = true;
            continue;
        }
        if section_name && code == 2 {
            section = value;
            section_name = false;
            continue;
        }
        if code == 0 && value == "ENDSEC" {
            section.clear();
            continue;
        }
        if section == "BLOCKS" || section == "ENTITIES" {
            pairs.push((code, value));
        }
    }
    Ok(pairs)
}
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(format!("{prefix}-source.dxf"))?.read()?;
    let expected_bytes = DxfWriter::new(&source)
        .with_version(DxfVersion::AC1009)
        .write_to_vec()?;
    let expected = DxfReader::from_reader(Cursor::new(expected_bytes))?.read()?;
    let native = DxfReader::from_file(format!("{prefix}-r12.dxf"))?.read()?;
    assert_eq!(native.version, DxfVersion::AC1009);
    let a = geometry(&expected)?;
    let b = geometry(&native)?;
    assert_eq!(a.len(), b.len(), "geometry pair count");
    for (i, ((ca, va), (cb, vb))) in a.iter().zip(&b).enumerate() {
        assert_eq!(ca, cb, "pair {i}: {va:?} vs {vb:?}");
        let is_real =
            (10..=59).contains(ca) || (110..=149).contains(ca) || (210..=239).contains(ca);
        if is_real {
            let x: f64 = va.trim().parse().unwrap();
            let y: f64 = vb.trim().parse().unwrap();
            assert!(
                (x - y).abs() <= 1e-10 * x.abs().max(y.abs()).max(1.),
                "pair {i} group {ca}: {x} != {y}"
            );
        } else {
            assert_eq!(va.trim(), vb.trim(), "pair {i} group {ca}");
        }
    }
    let layers: Vec<_> = native.layers.iter().map(|v| v.name.clone()).collect();
    assert!(layers.contains(&"__".to_string()) && layers.contains(&"___1".to_string()));
    assert!(native
        .entities()
        .any(|e| matches!(e,EntityType::Text(t) if t.value.contains("中文"))));
    let mut closed = 0;
    for e in native.entities() {
        if let EntityType::Polyline2D(p) = e {
            if p.vertices.len() == 3 && p.vertices[2].bulge == -0.25 {
                closed += 1;
                assert_eq!(p.vertices[2].start_width, 0.5);
                assert_eq!(p.vertices[2].end_width, 0.6);
            }
        }
    }
    assert_eq!(closed, 2);
    let lt = native.line_types.get("__").unwrap();
    assert_eq!(lt.elements.len(), 12);
    assert!(!DxfWriter::new_binary(&source)
        .with_version(DxfVersion::AC1009)
        .write_to_vec()
        .is_ok());
    println!("Rust verified native R12 structure, Chinese text/name mapping, blocks/attributes, all primitive/mesh geometry, widths/bulges/hidden edges and paper membership");
    Ok(())
}
