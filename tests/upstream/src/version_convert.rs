// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, DxfReader, DxfVersion, DxfWriter, EntityType};
use std::io::Cursor;
const VERSIONS: [DxfVersion; 8] = [
    DxfVersion::AC1012,
    DxfVersion::AC1014,
    DxfVersion::AC1015,
    DxfVersion::AC1018,
    DxfVersion::AC1021,
    DxfVersion::AC1024,
    DxfVersion::AC1027,
    DxfVersion::AC1032,
];
fn reference(d: &CadDocument, v: DxfVersion) -> acadrust::Result<CadDocument> {
    // The Rust binary writer promotes early targets. Its ASCII writer supplies
    // the requested version's field rules, independently of native transport.
    DxfReader::from_reader(Cursor::new(
        DxfWriter::new(d).with_version(v).write_to_vec()?,
    ))?
    .read()
}
fn compare(a: &CadDocument, b: &CadDocument) {
    assert_eq!(a.entities().count(), b.entities().count());
    for x in a.entities() {
        assert_eq!(
            Some(x),
            b.get_entity(x.common().handle),
            "entity {}",
            x.common().handle
        );
    }
    for block in a.block_records.iter() {
        let expected = b.block_records.get(&block.name).unwrap();
        assert_eq!(
            block.entity_handles, expected.entity_handles,
            "{} members",
            block.name
        );
        assert_eq!(block.units, expected.units, "{} units", block.name);
        assert_eq!(block.explodable, expected.explodable);
        assert_eq!(block.scale_uniformly, expected.scale_uniformly);
    }
}
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(format!("{prefix}-source.dxf"))?.read()?;
    for target in VERSIONS {
        let name = target.to_dxf_string();
        let actual = DxfReader::from_file(format!("{prefix}-{name}.dxf"))?.read()?;
        let expected = reference(&source, target)?;
        assert_eq!(actual.version, target);
        assert_eq!(actual.header.insertion_units, 4);
        compare(&actual, &expected);
        assert_eq!(
            actual.vports, expected.vports,
            "{name} VPORT version fields"
        );
        for style in actual.dim_styles.iter() {
            let mut expected_style = expected.dim_styles.get(&style.name).unwrap().clone();
            if target <= DxfVersion::AC1014 {
                // Upstream emits its independent name slot without resolving
                // the modern handle. Verify our explicit conversion via Rust's
                // reader and the source block, then compare all other fields.
                assert!(expected_style.dimblk_name.is_empty());
                let arrow = source.block_records.get("UnitBlock").unwrap();
                assert_eq!(
                    source.dim_styles.get(&style.name).unwrap().dimblk,
                    arrow.handle
                );
                assert_eq!(style.dimblk_name, arrow.name);
                expected_style.dimblk_name = arrow.name.clone();
            }
            assert_eq!(style, &expected_style, "{name} DIMSTYLE version fields");
        }
        assert_eq!(
            actual.line_types, expected.line_types,
            "{name} LTYPE version fields"
        );
        assert_eq!(actual.summary_info.title, expected.summary_info.title);
        assert_eq!(
            actual.header.dim_fixed_ext_line_on,
            expected.header.dim_fixed_ext_line_on
        );
        assert_eq!(actual.header.dim_arc_symbol, expected.header.dim_arc_symbol);
        let up = DxfReader::from_file(format!("{prefix}-{name}-up.dxf"))?.read()?;
        assert_eq!(up.version, DxfVersion::AC1032);
        compare(&up, &reference(&actual, DxfVersion::AC1032)?);
        assert_eq!(
            up.dim_styles.get("Standard").unwrap().dimblk,
            source.block_records.get("UnitBlock").unwrap().handle,
            "arrow restored from legacy name"
        );
        // XRECORD value group numbers are data, never common-property fields.
        let line = actual
            .entities()
            .find(|e| matches!(e, EntityType::Line(_)))
            .unwrap();
        let record = actual.xrecord(line.common().handle, "Literal").unwrap();
        assert_eq!(
            record.entries,
            source
                .xrecord(line.common().handle, "Literal")
                .unwrap()
                .entries
        );
    }
    let promoted = DxfReader::from_file(format!("{prefix}-promoted.dxf"))?.read()?;
    let expected = DxfReader::from_reader(Cursor::new(
        DxfWriter::new_binary(&source)
            .with_version(DxfVersion::AC1012)
            .write_to_vec()?,
    ))?
    .read()?;
    assert_eq!(promoted.version, DxfVersion::AC1024);
    compare(&promoted, &expected);
    for family in ["lw", "columns"] {
        let source = DxfReader::from_file(format!("{prefix}-{family}-source.dxf"))?.read()?;
        for target in VERSIONS {
            if (family == "lw" && target == DxfVersion::AC1012)
                || (family == "columns" && target < DxfVersion::AC1021)
            {
                continue;
            }
            let actual =
                DxfReader::from_file(format!("{prefix}-{family}-{}.dxf", target.to_dxf_string()))?
                    .read()?;
            let expected = reference(&source, target)?;
            compare(&actual, &expected);
        }
    }
    Ok(())
}
