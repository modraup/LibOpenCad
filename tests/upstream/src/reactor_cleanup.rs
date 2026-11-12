// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, DxfWriter, EntityType, Handle};
use acadrust::objects::ObjectType;
use std::collections::HashSet;
use std::io::Cursor;
type Record = Vec<(i32,String)>;

fn records(path: &str) -> acadrust::Result<Vec<Record>> {
    let mut result = Vec::<Record>::new();
    for pair in crate::remove_polylines::raw_string_pairs(path)? {
        if pair.0 == 0 { result.push(Vec::new()); }
        result.last_mut().unwrap().push(pair);
    }
    Ok(result)
}
fn handle(record: &Record) -> Option<u64> {
    if record[0].1 == "SECTION" { return None; }
    record.iter().take_while(|p| p.0 != 100).find(|p| p.0 == 5)
        .map(|p| u64::from_str_radix(p.1.trim(),16).unwrap())
}
fn filter_record(record: &Record, removed: &HashSet<u64>) -> Record {
    let mut result = Vec::new();
    let mut i = 0;
    while i < record.len() {
        if record[i] == (102,"{ACAD_REACTORS".into()) {
            let end = (i+1..record.len()).find(|&j| record[j] == (102,"}".into())).unwrap();
            let keep: Vec<_> = record[i+1..end].iter().filter(|(code,value)| {
                assert_eq!(*code,330); // This verifier's fixtures contain canonical groups.
                !removed.contains(&u64::from_str_radix(value.trim(),16).unwrap())
            }).cloned().collect();
            if !keep.is_empty() {
                result.push(record[i].clone()); result.extend(keep); result.push(record[end].clone());
            }
            i = end+1;
        } else { result.push(record[i].clone()); i += 1; }
    }
    result
}
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let before = format!("{prefix}-before.dxf");
    let original = DxfReader::from_file(&before)?.read()?;
    let raw = records(&before)?;
    let line = original.entities().find(|e| matches!(e,EntityType::Line(_))).unwrap().common().handle;
    let circle = original.entities().find(|e| matches!(e,EntityType::Circle(_))).unwrap().common().handle;
    let parent = Handle::new(0x31);
    let start = raw.iter().position(|r| handle(r) == Some(parent.value())).unwrap();
    let end = (start+1..raw.len()).find(|&i| raw[i][0].1 == "POLYLINE").unwrap();
    let children: HashSet<u64> = raw[start..end].iter().map(|r| handle(r).unwrap()).collect();
    assert!(children.len() >= 6);
    let binary = std::fs::read(&before)?.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0");
    let mut reference = original.clone();
    reference.remove_entity(line).unwrap();
    for (phase, include_group) in [("line",false),("group",true)] {
        let mut removed = HashSet::from([line.value()]);
        if include_group { removed.extend(children.iter().copied()); reference.remove_entity(parent).unwrap(); }
        let path = format!("{prefix}-{phase}.dxf");
        assert_eq!(std::fs::read(&path)?.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0"),binary);
        let actual = DxfReader::from_file(&path)?.read()?;
        let expected_raw: Vec<_> = raw.iter().filter(|r| !handle(r).is_some_and(|h| removed.contains(&h)))
            .map(|r| filter_record(r,&removed)).collect();
        assert_eq!(records(&path)?,expected_raw,"all surviving string/handle data and reactor order");
        assert_eq!(actual.entities().count(),if include_group {2} else {3});
        for entity in original.entities() {
            let h = entity.common().handle;
            if removed.contains(&h.value()) { assert!(actual.get_entity(h).is_none()); continue; }
            let mut expected = entity.clone();
            expected.common_mut().reactors.retain(|h| !removed.contains(&h.value()));
            assert_eq!(actual.get_entity(h),Some(&expected),"geometry/common data {h:?}");
        }
        let mut objects = original.objects.clone();
        for object in objects.values_mut() {
            match object {
                ObjectType::Dictionary(value) => value.reactors.retain(|h| !removed.contains(&h.value())),
                ObjectType::Layout(value) => value.reactors.retain(|h| !removed.contains(&h.value())),
                _ => {}
            }
        }
        assert_eq!(actual.objects,objects);
        assert_eq!(actual.header,original.header);
        assert_eq!(actual.layers,original.layers);
        assert_eq!(actual.line_types,original.line_types);
        for block in original.block_records.iter() {
            let mut expected = block.clone();
            expected.entity_handles.retain(|h| !removed.contains(&h.value()));
            assert_eq!(actual.block_records.get(&block.name),Some(&expected));
        }
        for value in [0xC,0x10,0x15,0x2E] {
            let h = Handle::new(value);
            let expected: Vec<_> = original.object_reactors(h).iter().copied()
                .filter(|h| !removed.contains(&h.value())).collect();
            assert_eq!(actual.object_reactors(h),expected.as_slice());
        }
        if include_group {
            // remove_entity retains old block handles for undo. The pinned writer uses
            // those in valid_handles; supply live memberships to test its filtering rule.
            for block in reference.block_records.iter_mut() {
                block.entity_handles.retain(|h| !removed.contains(&h.value()));
            }
            let mut writer = DxfWriter::new(&reference); writer.set_binary(binary);
            let expected = DxfReader::from_reader(Cursor::new(writer.write_to_vec()?))?.read()?;
            assert_eq!(actual.get_entity(circle).unwrap().common().reactors,
                       expected.get_entity(circle).unwrap().common().reactors);
            assert_eq!(actual.get_entity(circle).unwrap().common().reactors,
                       vec![Handle::new(0x32),Handle::new(0x15),Handle::new(0x32)]);
        }
    }
    println!("Deletion reactors match Rust filtering; remaining geometry, references, duplicates and order preserved");
    Ok(())
}
