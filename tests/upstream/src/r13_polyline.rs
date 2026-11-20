// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, DxfVersion, EntityType, Vector3};
use std::collections::HashSet;

pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let source_path = format!("{prefix}-source.dxf");
    let source = DxfReader::from_file(&source_path)?.read()?;
    let old_sequences = crate::remove_polylines::raw_sequences(&source_path)?;
    let mut previous = None;
    for encoding in ["ascii", "binary"] {
        let path = format!("{prefix}-{encoding}.dxf");
        let actual = DxfReader::from_file(&path)?.read()?;
        assert_eq!(actual.version, DxfVersion::AC1012);
        assert_eq!(actual.entities().count(), source.entities().count());
        let raw = crate::remove_polylines::raw_sequences(&path)?;
        let mut fresh = HashSet::new();
        let mut expanded = 0;
        for old in source.entities() {
            let got = actual.get_entity(old.common().handle).unwrap();
            if let EntityType::LwPolyline(light) = old {
                let EntityType::Polyline2D(heavy) = got else {
                    panic!("expected traditional 2D POLYLINE")
                };
                expanded += 1;
                assert_eq!(
                    heavy.common, light.common,
                    "parent identity, display and extension references"
                );
                assert_eq!(heavy.is_closed(), light.is_closed);
                assert_eq!(heavy.flags.bits() & 128 != 0, light.plinegen);
                assert_eq!(heavy.elevation, light.elevation);
                assert_eq!(heavy.thickness, light.thickness);
                assert_eq!(heavy.normal, light.normal);
                assert_eq!(heavy.vertices.len(), light.vertices.len());
                for (a, b) in light.vertices.iter().zip(&heavy.vertices) {
                    assert_eq!(b.location, Vector3::new(a.location.x, a.location.y, 0.));
                    assert_eq!(b.bulge, a.bulge, "includes closing segment");
                    let explicit = a.start_width != 0. || a.end_width != 0.;
                    assert_eq!(
                        b.start_width,
                        if explicit {
                            a.start_width
                        } else {
                            light.constant_width
                        }
                    );
                    assert_eq!(
                        b.end_width,
                        if explicit {
                            a.end_width
                        } else {
                            light.constant_width
                        }
                    );
                }
                // Rust bakes effective widths and clears defaults if any VERTEX
                // carries a width. Match its reader's documented normalization.
                let variable = light
                    .vertices
                    .iter()
                    .any(|v| v.start_width != 0. || v.end_width != 0.);
                assert_eq!(
                    heavy.start_width,
                    if variable { 0. } else { light.constant_width }
                );
                assert_eq!(heavy.end_width, heavy.start_width);
                // The typed 2D model omits child IDs. Verify raw VERTEX/SEQEND
                // handles with the upstream group-code type layout as well.
                let start = raw
                    .iter()
                    .position(|r| r.0 == "POLYLINE" && r.1 == light.common.handle.value())
                    .unwrap();
                assert_eq!(raw[start].2, light.common.owner_handle.value());
                for i in 0..=light.vertices.len() {
                    let child = &raw[start + i + 1];
                    assert_eq!(
                        child.0,
                        if i == light.vertices.len() {
                            "SEQEND"
                        } else {
                            "VERTEX"
                        }
                    );
                    assert_eq!(child.2, light.common.handle.value());
                    assert!(child.1 > 0xF000 && child.1 < actual.header.handle_seed);
                    assert!(fresh.insert(child.1));
                }
                if light.common.xdictionary_handle.is_some() {
                    assert_eq!(
                        actual.xrecord(light.common.handle, "引用").unwrap(),
                        source.xrecord(light.common.handle, "引用").unwrap()
                    );
                }
            } else {
                assert_eq!(got, old, "neighbor entity unchanged");
            }
        }
        assert_eq!(expanded, 4);
        for record in &old_sequences {
            assert!(
                raw.contains(record),
                "neighbor sequence identities/owners unchanged"
            );
        }
        assert_eq!(actual.header.handle_seed, 0xF001 + fresh.len() as u64);
        for block in source.block_records.iter() {
            assert_eq!(
                actual
                    .block_records
                    .get(&block.name)
                    .unwrap()
                    .entity_handles,
                block.entity_handles,
                "block/space members and ordering"
            );
        }
        if let Some(previous) = previous {
            assert_eq!(
                raw, previous,
                "identical deterministic allocation across transports"
            );
        }
        previous = Some(raw);
    }
    Ok(())
}
