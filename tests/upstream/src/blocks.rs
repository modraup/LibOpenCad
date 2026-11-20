// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{Circle, Insert, Line};
use acadrust::tables::BlockRecord;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
fn block(d: &mut CadDocument, name: &str) -> acadrust::Result<acadrust::types::Handle> {
    let mut b = BlockRecord::new(name);
    b.handle = d.allocate_handle();
    b.block_entity_handle = d.allocate_handle();
    b.block_end_handle = d.allocate_handle();
    b.base_point = Vector3::new(1., 2., 3.);
    b.description = "嵌套块".into();
    let h = b.handle;
    d.block_records.add(b)?;
    Ok(h)
}
fn insert(name: &str) -> Insert {
    let mut i = Insert::new(name, Vector3::new(10., 20., 30.)).with_scale(-2., 3., 4.);
    i.rotation = 0.5;
    i.column_count = 2;
    i.row_count = 3;
    i.column_spacing = 12.;
    i.row_spacing = 14.;
    i
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let leaf = block(&mut d, "部件")?;
    let assembly = block(&mut d, "Assembly")?;
    let mut l = Line::from_points(Vector3::ZERO, Vector3::new(4., 5., 6.));
    l.common.owner_handle = leaf;
    d.add_entity(EntityType::Line(l))?;
    let mut c = Circle::from_center_radius(Vector3::new(1., 2., 3.), 5.);
    c.common.owner_handle = leaf;
    d.add_entity(EntityType::Circle(c))?;
    let mut nested = insert("部件");
    nested.common.owner_handle = assembly;
    d.add_entity(EntityType::Insert(nested))?;
    d.add_entity(EntityType::Insert(insert("Assembly")))?;
    d.add_paper_space_entity(EntityType::Insert(insert("Assembly")))?;
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn verify_geometry(a: &EntityType, b: &EntityType) {
    let mut b = b.clone();
    *b.common_mut() = a.common().clone();
    assert_eq!(a, &b);
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let input = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let created = read("created")?;
    for name in ["部件", "Assembly", "*Model_Space", "*Paper_Space"] {
        let a: Vec<_> = created.entities_in_block(name).collect();
        let b: Vec<_> = input.entities_in_block(name).collect();
        assert_eq!(a.len(), b.len());
        for (a, b) in a.into_iter().zip(b) {
            verify_geometry(a, b);
        }
    }
    for name in ["部件", "Assembly"] {
        let b = created.block_records.get(name).unwrap();
        assert_eq!(b.base_point, Vector3::new(1., 2., 3.));
        assert_eq!(b.description, "嵌套块");
    }
    let before = read("before")?;
    let edited = read("edited")?;
    let copied = read("copied")?;
    let deleted = read("deleted")?;
    let renamed = read("renamed")?;
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
    for e in before.entities() {
        let mut expected = e.clone();
        if e.common().handle == model {
            let EntityType::Insert(i) = &mut expected else {
                panic!()
            };
            i.insert_point = Vector3::new(7., 8., 9.);
            i.set_x_scale(1e-12);
            i.rotation = -0.25;
            i.column_count = 1;
            i.row_count = 1;
        }
        verify_geometry(edited.get_entity(e.common().handle).unwrap(), &expected);
        assert_eq!(
            copied.get_entity(e.common().handle),
            edited.get_entity(e.common().handle)
        );
    }
    let clone = copied
        .entities()
        .find(|e| before.get_entity(e.common().handle).is_none())
        .unwrap();
    verify_geometry(clone, edited.get_entity(model).unwrap());
    assert_eq!(
        clone.common().owner_handle,
        before.get_entity(paper).unwrap().common().owner_handle
    );
    assert_eq!(
        copied
            .xrecord(clone.common().handle, "Data")
            .unwrap()
            .get_first_by_code(340)
            .unwrap()
            .value
            .as_handle(),
        Some(clone.common().handle)
    );
    assert_eq!(deleted.entities().count(), before.entities().count() - 1);
    assert_eq!(deleted.get_entity(paper), before.get_entity(paper));
    let mut expected = before.clone();
    expected.remove_entity(model).unwrap();
    assert_eq!(deleted.objects, expected.objects);
    assert!(renamed.block_records.get("部件").is_none());
    let b = renamed.block_records.get("零件").unwrap();
    assert_eq!(
        b.entity_handles,
        before.block_records.get("部件").unwrap().entity_handles
    );
    let EntityType::Insert(i) = renamed.entities_in_block("Assembly").next().unwrap() else {
        panic!()
    };
    assert_eq!(i.block_name, "零件");
    println!("Rust verified named block contents, nested INSERT/MINSERT, metadata, editing, copy/delete and rename references");
    Ok(())
}
