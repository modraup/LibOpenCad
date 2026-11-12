// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, Handle};
use acadrust::objects::ObjectType;
use std::collections::HashSet;

pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let before = DxfReader::from_file(format!("{prefix}-before.dxf"))?.read()?;
    let actual = DxfReader::from_file(format!("{prefix}-after.dxf"))?.read()?;
    let root = before.entities().find(|e| e.common().xdictionary_handle == Some(Handle::new(0x80))).unwrap().common().handle;
    let survivor = before.entities().find(|e| e.common().xdictionary_handle == Some(Handle::new(0x90))).unwrap().common().handle;
    assert_eq!(before.extension_dictionary_handle(root),Some(Handle::new(0x80)));
    assert!(before.xrecord(root,"中文数据").is_some());
    assert!(before.xrecord(survivor,"保留数据").is_some());
    assert_eq!(before.object_owner(Handle::new(0x83)),Some(Handle::new(0x82)));
    let mut reference = before.clone();
    reference.remove_entity(root).unwrap(); // Unmodified upstream follows the object owner chains.
    let removed: HashSet<_> = (0x80..=0x85).map(Handle::new).chain([root]).collect();
    for h in 0x80..=0x85 {
        assert!(!reference.objects.contains_key(&Handle::new(h)),"upstream cascade {h:X}");
        assert!(!actual.objects.contains_key(&Handle::new(h)),"C++ cascade {h:X}");
    }
    for object in reference.objects.values_mut() {
        if let ObjectType::Dictionary(d) = object { d.reactors.retain(|h| !removed.contains(h)); }
    }
    assert_eq!(actual.objects,reference.objects,"all surviving objects including XRECORD payloads and dictionary entries");
    assert_eq!(actual.entities().count(),1);
    let mut expected = before.get_entity(survivor).unwrap().clone();
    expected.common_mut().reactors.retain(|h| !removed.contains(h));
    assert_eq!(actual.get_entity(survivor),Some(&expected),"neighbor geometry, mesh face indices/hidden edges and common properties");
    assert!(actual.get_entity(root).is_none());
    assert_eq!(actual.extension_dictionary_handle(survivor),Some(Handle::new(0x90)));
    assert_eq!(actual.xrecord(survivor,"保留数据"),before.xrecord(survivor,"保留数据"));
    assert_eq!(actual.header,before.header);
    assert_eq!(actual.layers,before.layers);
    assert_eq!(actual.line_types,before.line_types);
    for block in before.block_records.iter() {
        let mut expected=block.clone(); expected.entity_handles.retain(|h| *h!=root);
        assert_eq!(actual.block_records.get(&block.name),Some(&expected));
    }
    let ObjectType::Dictionary(dictionary) = &actual.objects[&Handle::new(0x90)] else { panic!("surviving dictionary type") };
    assert_eq!(dictionary.reactors,vec![survivor]);
    println!("Extension DICTIONARY/XRECORD cascade matches Rust; neighboring geometry, dictionaries, payloads and ownership preserved");
    Ok(())
}
