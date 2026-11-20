// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{CellValue, CellValueType, Line};
use acadrust::objects::{Field, FieldChildValue, FieldList, ObjectType};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector3};
fn field() -> Field {
    let mut f = Field::default();
    f.evaluator_id = "AcVar".into();
    f.code = "中文字段代码".into();
    f.evaluation_option = 31;
    f.filing_option = 7;
    f.state = 16;
    f.evaluation_status = 2;
    f.evaluation_error_code = -1;
    f.evaluation_error_message = "保留诊断".into();
    f.value = CellValue::text("字段值");
    f.value_string = "显示文字".into();
    f.value_string_length = 4;
    f.child_values = vec![
        FieldChildValue {
            key: "整数".into(),
            value: CellValue::integer(-12),
        },
        FieldChildValue {
            key: "坐标".into(),
            value: CellValue {
                value_type: CellValueType::Point3D,
                raw_type_code: 32,
                point_value: Vector3::new(1., 2., 3.),
                ..CellValue::new()
            },
        },
        FieldChildValue {
            key: "抑制".into(),
            value: CellValue {
                flags: 1,
                ..CellValue::number(99.)
            },
        },
    ];
    f
}
fn add(d: &mut CadDocument, owner: Handle, name: &str, mut f: Field) -> Handle {
    f.handle = d.allocate_handle();
    f.owner = owner;
    let h = f.handle;
    if let Some(ObjectType::Dictionary(dict)) = d.objects.get_mut(&owner) {
        dict.add_entry(name, h);
    }
    d.objects.insert(h, ObjectType::Field(f));
    h
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    for paper in [false, true] {
        let e = EntityType::Line(Line::from_points(Vector3::ZERO, Vector3::UNIT_X));
        let h = if paper {
            d.add_paper_space_entity(e)?
        } else {
            d.add_entity(e)?
        };
        let dict = d.ensure_extension_dictionary(h);
        let mut f = field();
        f.referenced_objects = vec![h];
        let fh = add(&mut d, dict, "字段", f);
        let child = add(&mut d, fh, "", field());
        if let ObjectType::Field(f) = d.objects.get_mut(&fh).unwrap() {
            f.child_fields.push(child);
        }
        let list = FieldList {
            handle: d.allocate_handle(),
            owner: dict,
            unknown: true,
            fields: vec![fh, child],
        };
        if let ObjectType::Dictionary(d) = d.objects.get_mut(&dict).unwrap() {
            d.add_entry("列表", list.handle);
        }
        d.objects.insert(list.handle, ObjectType::FieldList(list));
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn field_at(d: &CadDocument, h: Handle) -> &Field {
    if let ObjectType::Field(f) = &d.objects[&h] {
        f
    } else {
        panic!()
    }
}
fn first(d: &CadDocument, entity: Handle) -> Handle {
    let dict = d.extension_dictionary_handle(entity).unwrap();
    if let ObjectType::Dictionary(dict) = &d.objects[&dict] {
        dict.get("字段").unwrap()
    } else {
        panic!()
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let e = src
        .entities_in_block("*Model_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let neighbor = src
        .entities_in_block("*Paper_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let h = first(&src, e);
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    assert_eq!(round.objects, src.objects);
    let edited = read("edited")?;
    let mut expected = field_at(&src, h).clone();
    expected.code = "修改代码".into();
    expected.child_values[0].value.numeric_value = 42.;
    expected.value = CellValue::number(12.5);
    assert_eq!(field_at(&edited, h), &expected);
    assert_eq!(
        field_at(&edited, first(&edited, neighbor)),
        field_at(&src, first(&src, neighbor))
    );
    let copied = read("copied")?;
    let c = copied
        .entities()
        .find(|x| edited.get_entity(x.common().handle).is_none())
        .unwrap()
        .common()
        .handle;
    let ch = first(&copied, c);
    let f = field_at(&copied, ch);
    assert_eq!(f.referenced_objects, vec![c]);
    assert_ne!(f.child_fields, expected.child_fields);
    assert_eq!(field_at(&copied, f.child_fields[0]).owner, ch);
    let deleted = read("deleted")?;
    assert!(!deleted.objects.contains_key(&ch));
    assert_eq!(field_at(&deleted, h), &expected);
    let fresh = read("fresh")?;
    let e = fresh.entities().next().unwrap().common().handle;
    let f = field_at(&fresh, first(&fresh, e));
    assert_eq!(f.value.text, "新字段");
    assert_eq!(f.child_fields.len(), 1);
    assert_eq!(field_at(&fresh, f.child_fields[0]).value.numeric_value, 7.);
    println!("Rust verified FIELD values, child scopes, FIELDLIST, ownership/remapping, suppression flags and neighbor isolation");
    Ok(())
}
