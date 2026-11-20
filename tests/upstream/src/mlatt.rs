// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, DxfReader, DxfVersion, DxfWriter, EntityType};
use std::io::Cursor;
fn compare(a: &CadDocument, b: &CadDocument) {
    assert_eq!(a.entities().count(), b.entities().count());
    for e in a.entities() {
        assert_eq!(
            Some(e),
            b.get_entity(e.common().handle),
            "entity {}",
            e.common().handle
        );
    }
    for block in a.block_records.iter() {
        assert_eq!(
            block.entity_handles,
            b.block_records.get(&block.name).unwrap().entity_handles,
            "block member order"
        );
    }
}
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(format!("{prefix}-source.dxf"))?.read()?;
    for version in [
        DxfVersion::AC1015,
        DxfVersion::AC1018,
        DxfVersion::AC1021,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
    ] {
        let name = version.to_dxf_string();
        // ASCII oracle requests exact early targets; binary Rust output would
        // promote R2000/R2004. Rust's reader merges the MLATT split automatically.
        let expected = DxfReader::from_reader(Cursor::new(
            DxfWriter::new(&source)
                .with_version(version)
                .write_to_vec()?,
        ))?
        .read()?;
        let actual = DxfReader::from_file(format!("{prefix}-{name}.dxf"))?.read()?;
        assert_eq!(actual.version, version);
        compare(&actual, &expected);
        for e in source.entities() {
            if let EntityType::AttributeDefinition(a) = e {
                if a.is_multiline {
                    assert_eq!(
                        actual.xrecord(a.common.handle, "Keep"),
                        source.xrecord(a.common.handle, "Keep")
                    );
                    let rt = actual
                        .xrecord(a.common.handle, "ACAD_XREC_ROUNDTRIP")
                        .unwrap();
                    assert!(!rt
                        .entries
                        .iter()
                        .any(|v| v.code == 102 && v.value.as_string() == Some("ACAD_MATT_HEIGHT")));
                    assert_eq!(
                        rt.entries,
                        expected
                            .xrecord(a.common.handle, "ACAD_XREC_ROUNDTRIP")
                            .unwrap()
                            .entries
                    );
                }
            }
            if let EntityType::Insert(i) = e {
                let EntityType::Insert(saved) = actual.get_entity(i.common.handle).unwrap() else {
                    panic!()
                };
                assert_eq!(i.attributes.len(), saved.attributes.len());
                for (a, b) in i.attributes.iter().zip(&saved.attributes) {
                    assert_eq!(a.common.handle, b.common.handle);
                    assert_eq!(a.common.owner_handle, b.common.owner_handle);
                    assert_eq!(a.tag, b.tag);
                    assert_eq!(a.value, b.value);
                }
            }
        }
        let up = DxfReader::from_file(format!("{prefix}-{name}-up.dxf"))?.read()?;
        let expected_up = DxfReader::from_reader(Cursor::new(
            DxfWriter::new(&expected)
                .with_version(DxfVersion::AC1032)
                .write_to_vec()?,
        ))?
        .read()?;
        assert_eq!(up.version, DxfVersion::AC1032);
        compare(&up, &expected_up);
        let raw = crate::remove_polylines::raw_string_pairs(&format!("{prefix}-{name}-up.dxf"))?;
        assert!(
            !raw.iter().any(|(code, v)| *code == 3 && v == "ACAD_MLATT"),
            "upgrade removes legacy metadata"
        );
    }
    Ok(())
}
