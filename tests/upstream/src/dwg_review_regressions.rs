// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::primitives::{build_box, build_sphere};
use acadrust::entities::acis::SabWriter;
use acadrust::entities::Solid3D;
use acadrust::objects::XRecordEntry;
use acadrust::{CadDocument, DwgReader, DwgWriter, DxfVersion, EntityType, Handle};
use std::{fs, path::Path};

pub fn generate(root: &str, dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    for version in ["AC1018", "AC1021", "AC1032"] {
        let mut d = DwgReader::from_file(
            Path::new(root)
                .join("modeler")
                .join(format!("{version}.dwg")),
        )?
        .read()?;
        let h = d
            .entities()
            .find(|e| matches!(e, EntityType::Solid3D(_)))
            .unwrap()
            .common()
            .handle;
        if version == "AC1018" {
            d.header.code_page = "ANSI_936".into();
        }
        d.ensure_xrecord(h, "REVIEW_STRING");
        d.xrecord_mut(h, "REVIEW_STRING").unwrap().entries = vec![
            XRecordEntry::string(1, "hello 中文"),
            XRecordEntry::string(3, ""),
            XRecordEntry::string(5, "literal-5"),
            XRecordEntry::string(105, "literal-105"),
            XRecordEntry::string(320, "abc"),
            XRecordEntry::string(329, "def"),
            XRecordEntry::int16(70, 7),
            XRecordEntry::handle(330, h),
        ];
        DwgWriter::write_to_file(Path::new(dir).join(format!("{version}.dwg")), &d)?;
    }
    // Independent writer fixtures: these owners have identical low 32 bits.
    // The current Rust reader truncates AcDs owners; it is not an oracle for this case.
    for version in [DxfVersion::AC1027, DxfVersion::AC1032] {
        let mut d = CadDocument::with_version(version);
        d.header.handle_seed = 0x10000000;
        let low = d.add_entity(EntityType::Solid3D(Solid3D::from_sat(
            &build_box([0., 0., 0.], 1., 2., 3.).to_sat_string(),
        )))?;
        d.header.handle_seed = low.value() + (1u64 << 32);
        let high = d.add_entity(EntityType::Solid3D(Solid3D::from_sat(
            &build_sphere([10., 20., 30.], 7.).to_sat_string(),
        )))?;
        assert_eq!(low.value(), 0x10000000);
        assert_eq!(high.value(), 0x110000000);
        for (name, h) in [("box", low), ("sphere", high)] {
            let Some(EntityType::Solid3D(solid)) = d.get_entity(h) else {
                unreachable!()
            };
            fs::write(
                Path::new(dir).join(format!("wide-{name}.sab")),
                SabWriter::write(&solid.acis_data.parse().unwrap()),
            )?;
        }
        DwgWriter::write_to_file(Path::new(dir).join(format!("{version:?}-wide.dwg")), &d)?;
    }
    Ok(())
}
fn ids(prefix: &str, version: &str) -> Vec<Handle> {
    fs::read_to_string(format!("{prefix}-{version}.ids"))
        .unwrap()
        .split_whitespace()
        .map(|s| Handle::new(s.parse().unwrap()))
        .collect()
}
pub fn verify(mode: &str, root: &str, prefix: &str) -> acadrust::Result<()> {
    match mode {
        "acds" => {
            for v in ["AC1027", "AC1032"] {
                let d = DwgReader::from_file(format!("{prefix}-{v}.dwg"))?.read()?;
                let h = ids(prefix, v)[0];
                let Some(EntityType::Solid3D(s)) = d.get_entity(h) else {
                    panic!("Missing surviving solid")
                };
                assert_eq!(d.entities().count(), 1);
                let expected = build_box([0., 0., 0.], 1., 2., 3.);
                crate::acis::compare(
                    &crate::acis::canonical(&s.acis_data.parse().unwrap()),
                    &crate::acis::canonical(&expected),
                );
            }
            // Invalid single-row owner/length cases intentionally have native assertions:
            // the pinned Rust reader still treats one indexed row as unindexed storage.
        }
        "legacy" => {
            for v in ["AC1018", "AC1021", "AC1032"] {
                let original = DwgReader::from_file(
                    Path::new(root)
                        .join("tests/data/dwg/review")
                        .join(format!("{v}.dwg")),
                )?
                .read()?;
                let d = DwgReader::from_file(format!("{prefix}-{v}.dwg"))?.read()?;
                let hs = ids(prefix, v);
                for e in original.entities() {
                    assert_eq!(
                        d.get_entity(e.common().handle),
                        Some(e),
                        "Original entity changed"
                    );
                }
                assert_eq!(d.entities().count(), original.entities().count() + 1);
                let source = original.xrecord(hs[0], "REVIEW_STRING").unwrap();
                assert_eq!(
                    d.xrecord(hs[0], "REVIEW_STRING").unwrap().entries,
                    source.entries
                );
                let mut expected = source.entries.clone();
                *expected.last_mut().unwrap() = XRecordEntry::handle(330, hs[1]);
                let copy = d.xrecord(hs[1], "REVIEW_STRING").unwrap();
                assert_eq!(
                    copy.entries, expected,
                    "Copy changed string data or object IDs"
                );
                assert_eq!(copy.object_references[0].handle, hs[1]);
                for (h, object) in &original.objects {
                    assert_eq!(d.objects.get(h), Some(object), "Original object changed");
                }
            }
        }
        "references" => {
            for v in ["AC1021", "AC1024", "AC1027", "AC1032"] {
                let before = DwgReader::from_file(format!("{prefix}-{v}.before.dwg"))?.read()?;
                let after = DwgReader::from_file(format!("{prefix}-{v}.after.dwg"))?.read()?;
                let hs = ids(prefix, v);
                assert!(after.get_entity(hs[2]).is_none());
                assert_eq!(after.entities().count() + 1, before.entities().count());
                for e in before.entities() {
                    if e.common().handle != hs[2] {
                        assert_eq!(
                            after.get_entity(e.common().handle),
                            Some(e),
                            "Surviving entity changed"
                        );
                    }
                }
                assert_eq!(before.solid_history_graph(hs[0]).unwrap().root, hs[1]);
                assert_eq!(after.solid_history_graph(hs[0]).unwrap().root, hs[1]);
                assert_eq!(
                    after.objects, before.objects,
                    "Deletion changed surviving metadata/history objects"
                );
            }
        }
        _ => panic!("Unknown DWG review suite"),
    }
    println!("{mode}: review regression output matches Rust");
    Ok(())
}
