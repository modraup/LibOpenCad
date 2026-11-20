// SPDX-License-Identifier: MPL-2.0
use acadrust::objects::ObjectType;
use acadrust::{DxfReader, EntityType};
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let base = read("baseline")?;
    let copied = read("copied")?;
    let deleted = read("deleted")?;
    assert!(base.layers.get("零件").is_some());
    assert!(base.dim_styles.get("工程尺寸").is_some());
    let nested: Vec<_> = base.entities_in_block("装配块").collect();
    assert_eq!(nested.len(), 1);
    if let EntityType::Insert(i) = nested[0] {
        assert_eq!(i.block_name, "零件块");
        assert_eq!(i.attributes.len(), 1);
        assert_eq!(i.attributes[0].value, "A-001");
    } else {
        panic!()
    }
    let mut holes = 0;
    let mut dimensions = 0;
    for e in base.entities() {
        match e {
            EntityType::Hatch(h) => {
                assert_eq!(h.paths.len(), 2);
                holes += 1;
            }
            EntityType::Dimension(d) => {
                assert_eq!(d.base().style_name, "工程尺寸");
                dimensions += 1;
            }
            _ => {}
        }
    }
    assert_eq!((holes, dimensions), (1, 1));
    let blocks: Vec<_> = base
        .block_records
        .iter()
        .filter(|b| b.name.starts_with("*Paper_Space"))
        .collect();
    assert_eq!(blocks.len(), 3);
    assert_eq!(
        base.objects
            .values()
            .filter(|o| matches!(o, ObjectType::Layout(_)))
            .count(),
        4
    );
    let container: Vec<_> = copied.entities_in_block("容器块").collect();
    assert_eq!(container.len(), 1);
    if let EntityType::Insert(i) = container[0] {
        assert_eq!(i.attributes[0].value, "副本编号");
        assert_eq!(i.attributes[0].common.owner_handle, i.common.handle);
    } else {
        panic!()
    }
    assert!(deleted.entities_in_block("容器块").next().is_none());
    assert_eq!(copied.entities().count(), base.entities().count() + 4);
    for e in base.entities() {
        assert_eq!(deleted.get_entity(e.common().handle), Some(e));
    }
    for (h, o) in &base.objects {
        assert_eq!(deleted.objects.get(h), Some(o));
    }
    let layout = copied
        .objects
        .values()
        .find_map(|o| {
            if let ObjectType::Layout(l) = o {
                (l.name == "图纸二").then_some(l)
            } else {
                None
            }
        })
        .unwrap();
    let mut ids = std::collections::HashSet::new();
    for e in copied
        .entities()
        .filter(|e| e.common().owner_handle == layout.block_record)
    {
        if let EntityType::Viewport(v) = e {
            assert!(ids.insert(v.id));
        }
    }
    assert_eq!(ids.len(), 3);
    assert_eq!(
        copied
            .objects
            .values()
            .filter(|o| matches!(o, ObjectType::ObjectContextData(_)))
            .count(),
        2
    );
    assert_eq!(
        deleted
            .objects
            .values()
            .filter(|o| matches!(o, ObjectType::ObjectContextData(_)))
            .count(),
        1
    );
    println!("Rust verified combined drawing: attributed nested blocks, hole hatch, styled dimension, layouts/viewports, image reactors, context graph, copy targets and untouched source records");
    Ok(())
}
