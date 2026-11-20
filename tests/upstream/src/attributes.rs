// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::attribute_definition::{HorizontalAlignment, VerticalAlignment};
use acadrust::entities::{AttributeDefinition, AttributeEntity, Insert, MText};
use acadrust::tables::BlockRecord;
use acadrust::types::Handle;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
fn definition(multiline: bool) -> AttributeDefinition {
    let mut a = AttributeDefinition::new("标签".into(), "输入".into(), "默认".into());
    a.insertion_point = Vector3::new(1., 2., 3.);
    a.alignment_point = Vector3::new(4., 5., 6.);
    a.height = 3.;
    a.thickness = 2.;
    a.rotation = 0.5;
    a.width_factor = 1.5;
    a.oblique_angle = -0.25;
    a.text_generation_flags = 6;
    a.horizontal_alignment = HorizontalAlignment::Right;
    a.vertical_alignment = VerticalAlignment::Top;
    a.flags.verify = true;
    a.flags.preset = true;
    a.field_length = 12;
    a.lock_position = true;
    if multiline {
        a.is_multiline = true;
        let mut t = MText::with_value("多行\\P默认", Vector3::new(7., 8., 9.));
        t.height = 4.;
        t.rectangle_width = 20.;
        t.rotation = 0.25;
        t.line_spacing_factor = 1.5;
        a.embedded_mtext = Some(Box::new(t));
    }
    a
}
fn populate() -> acadrust::Result<CadDocument> {
    let mut d = CadDocument::new();
    let mut b = BlockRecord::new("Attributes");
    b.handle = d.allocate_handle();
    b.block_entity_handle = d.allocate_handle();
    b.block_end_handle = d.allocate_handle();
    let owner = b.handle;
    d.block_records.add(b)?;
    let mut defs = vec![];
    for multiline in [false, true] {
        let mut a = definition(multiline);
        a.common.owner_handle = owner;
        d.add_entity(EntityType::AttributeDefinition(a.clone()))?;
        defs.push(a);
    }
    for paper in [false, true] {
        let mut i = Insert::new("Attributes", Vector3::new(10., 20., 30.));
        for a in &defs {
            i.attributes.push(AttributeEntity::from_definition(a, None));
        }
        if paper {
            d.add_paper_space_entity(EntityType::Insert(i))?;
        } else {
            d.add_entity(EntityType::Insert(i))?;
        }
    }
    Ok(d)
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let d = populate()?;
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn insert(e: &EntityType) -> &Insert {
    if let EntityType::Insert(i) = e {
        i
    } else {
        panic!()
    }
}
fn geometry(a: &EntityType, b: &EntityType) {
    let mut b = b.clone();
    *b.common_mut() = a.common().clone();
    match (&mut b, a) {
        (EntityType::Insert(b), EntityType::Insert(a)) => {
            assert_eq!(a.attributes.len(), b.attributes.len());
            b.seqend_handle = a.seqend_handle;
            for (b, a) in b.attributes.iter_mut().zip(&a.attributes) {
                b.common = a.common.clone();
                b.attdef_handle = a.attdef_handle;
            }
        }
        _ => {}
    }
    assert_eq!(a, &b);
}
fn edit(i: &mut Insert) {
    let a = &mut i.attributes[0];
    a.value = "修改".into();
    a.insertion_point = Vector3::new(101., 102., 103.);
    a.text_generation_flags = 2;
    a.horizontal_alignment = HorizontalAlignment::Center;
    a.lock_position = false;
    let a = &mut i.attributes[1];
    a.value = "修改\\P多行".into();
    a.line_count = 2;
    a.embedded_mtext.as_mut().unwrap().value = a.value.clone();
    a.embedded_mtext.as_mut().unwrap().rectangle_width = 25.;
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let input = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let made = read("created")?;
    for name in ["Attributes", "*Model_Space", "*Paper_Space"] {
        let a: Vec<_> = made.entities_in_block(name).collect();
        let b: Vec<_> = input.entities_in_block(name).collect();
        assert_eq!(a.len(), b.len());
        for (a, b) in a.into_iter().zip(b) {
            geometry(a, b);
        }
    }
    let before = read("before")?;
    let edited = read("edited")?;
    let copied = read("copied")?;
    let deleted = read("deleted")?;
    let model = before
        .entities_in_block("*Model_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let paper = before
        .entities_in_block("*Paper_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let mut expected = before.get_entity(model).unwrap().clone();
    let EntityType::Insert(i) = &mut expected else {
        panic!()
    };
    edit(i);
    geometry(edited.get_entity(model).unwrap(), &expected);
    assert_eq!(before.get_entity(paper), edited.get_entity(paper));
    let source = insert(edited.get_entity(model).unwrap());
    let clone = copied
        .entities_in_block("*Paper_Space")
        .find(|e| e.common().handle != paper)
        .unwrap();
    geometry(clone, &expected);
    let target = insert(clone);
    for (a, b) in source.attributes.iter().zip(&target.attributes) {
        assert_ne!(a.common.handle, b.common.handle);
        assert_eq!(b.common.owner_handle, target.common.handle);
        let xr = copied.xrecord(b.common.handle, "Data").unwrap();
        assert_eq!(
            xr.get_first_by_code(340).unwrap().value.as_handle(),
            Some(b.common.handle)
        );
        assert_eq!(
            xr.get_first_by_code(341).unwrap().value.as_handle(),
            Some(target.common.handle)
        );
    }
    assert_ne!(source.seqend_handle, target.seqend_handle);
    assert_eq!(copied.get_entity(model), edited.get_entity(model));
    assert_eq!(deleted.get_entity(paper), before.get_entity(paper));
    assert_eq!(deleted.entities_in_block("Attributes").count(), 2);
    assert_eq!(deleted.entities_in_block("*Model_Space").count(), 0);
    for a in &source.attributes {
        assert!(deleted.xrecord(a.common.handle, "Data").is_none());
    }
    assert!(deleted.get_entity(Handle::NULL).is_none());
    println!("Rust verified ATTDEF/ATTRIB text and embedded MTEXT, sequence identities, copy remapping and cascading deletion");
    Ok(())
}
