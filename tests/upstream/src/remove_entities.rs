// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Line};
use std::io::Cursor;

fn compare(original: &CadDocument, actual: &CadDocument, reference: &CadDocument,
           deleted: &[u64], binary: bool) -> acadrust::Result<()> {
    assert_eq!(actual.entities().count(), 6 - deleted.len());
    assert_eq!(actual.header, original.header);
    assert_eq!(actual.header.handle_seed, 0x3A);
    assert_eq!(actual.layers, original.layers);
    assert_eq!(actual.line_types, original.line_types);
    assert_eq!(actual.text_styles, original.text_styles);
    assert_eq!(actual.dim_styles, original.dim_styles);
    assert_eq!(actual.app_ids, original.app_ids);
    assert_eq!(actual.vports, original.vports);
    assert_eq!(actual.views, original.views);
    assert_eq!(actual.ucss, original.ucss);
    assert_eq!(actual.classes, original.classes);
    assert_eq!(actual.objects, original.objects);
    for entity in original.entities() {
        let h = entity.common().handle;
        if deleted.contains(&h.value()) {
            assert!(actual.get_entity(h).is_none());
            assert!(reference.get_entity(h).is_none());
        } else {
            assert_eq!(actual.get_entity(h), Some(entity), "survivor {h:?} changed");
            assert_eq!(actual.get_entity(h), reference.get_entity(h));
        }
    }
    assert_eq!(actual.block_records.len(), original.block_records.len());
    for block in original.block_records.iter() {
        // Upstream keeps stale membership internally for undo, but queries filter it.
        let mut expected = block.clone();
        expected.entity_handles.retain(|h| !deleted.contains(&h.value()));
        assert_eq!(actual.block_records.get(&block.name), Some(&expected));
        let handles = |doc: &CadDocument| doc.entities_in_block(&block.name)
            .map(|e| e.common().handle.value()).collect::<Vec<_>>();
        assert_eq!(handles(actual), handles(reference), "space {}", block.name);
    }
    // The Rust writer may reorder entities after swap_remove; compare audit after readback.
    let mut writer = DxfWriter::new(reference);
    writer.set_binary(binary);
    let written = DxfReader::from_reader(Cursor::new(writer.write_to_vec()?))?.read()?;
    assert_eq!(actual.audit(), written.audit());
    Ok(())
}

pub fn verify(before: &str, prefix: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(before)?.read()?;
    let binary = std::fs::read(before)?.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0");
    assert_eq!(original.entities().count(), 6);
    for (suffix, deleted) in [
        ("first", vec![0x34,0x38,0x36]),
        ("second", vec![0x39,0x35,0x37]),
        ("empty", vec![0x34,0x38,0x36,0x37,0x35,0x39]),
    ] {
        let mut reference = original.clone();
        for &value in &deleted {
            let h = Handle::new(value);
            assert_eq!(reference.remove_entity(h).as_ref(), original.get_entity(h));
            assert!(reference.remove_entity(h).is_none());
        }
        assert!(reference.remove_entity(Handle::new(0xDEAD)).is_none());
        let path = format!("{prefix}-{suffix}.dxf");
        assert_eq!(std::fs::read(&path)?.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0"), binary);
        let actual = DxfReader::from_file(path)?.read()?;
        compare(&original, &actual, &reference, &deleted, binary)?;
    }
    let mut reference = original.clone();
    for value in 0x34..=0x39 { reference.remove_entity(Handle::new(value)).unwrap(); }
    let new_handle = reference.add_entity(EntityType::Line(Line::from_coords(9.,8.,7.,6.,5.,4.)))?;
    assert_eq!(new_handle.value(), 0x3A);
    let mut writer = DxfWriter::new(&reference);
    writer.set_binary(binary);
    let expected = DxfReader::from_reader(Cursor::new(writer.write_to_vec()?))?.read()?;
    let readded = DxfReader::from_file(format!("{prefix}-readded.dxf"))?.read()?;
    assert_eq!(readded.entities().count(), 1);
    assert_eq!(readded.get_entity(new_handle), expected.get_entity(new_handle));
    assert_eq!(readded.header.handle_seed, new_handle.value() + 1);
    assert_eq!(readded.model_space_entities().count(), 1);
    assert_eq!(readded.entities_in_block("*Paper_Space").count(), 0);
    assert_eq!(readded.objects, original.objects);
    println!("Deletion subsets, empty spaces and non-reused handles match Rust; all survivors unchanged");
    Ok(())
}
