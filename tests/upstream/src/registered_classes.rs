// SPDX-License-Identifier: MPL-2.0
use acadrust::classes::DxfClass;
use acadrust::entities::{
    EntityCommon, ExtendedEntity, ExtendedEntityData, RegisteredClassEntityData,
};
use acadrust::objects::{
    ObjectType, RegisteredClassObject, SemanticProperty, SemanticPropertyValue as V,
};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType};
fn properties() -> Vec<SemanticProperty> {
    vec![
        (1, V::Text("扩展类中文".into())),
        (290, V::Bool(true)),
        (280, V::Byte(7)),
        (70, V::Int16(-2)),
        (90, V::Int32(-3)),
        (160, V::Int64(123456789012)),
        (40, V::Double(1.25)),
        (310, V::Binary(vec![1, 2, 3])),
    ]
    .into_iter()
    .map(|(code, value)| SemanticProperty {
        subclass: "AcDbCustomData".into(),
        code,
        value,
    })
    .collect()
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    d.classes.add_or_update(DxfClass::new_entity(
        "ACAD_PROXY_ENTITY_WRAPPER",
        "AcDbProxyEntityWrapper",
    ));
    d.classes.add_or_update(DxfClass::new(
        "ACAD_PROXY_OBJECT_WRAPPER",
        "AcDbProxyObjectWrapper",
    ));
    for paper in [false, true] {
        let e = EntityType::Extended(Box::new(ExtendedEntity {
            common: EntityCommon::new(),
            data: ExtendedEntityData::RegisteredClass(RegisteredClassEntityData {
                dxf_name: "ACAD_PROXY_ENTITY_WRAPPER".into(),
                cpp_class_name: "AcDbProxyEntityWrapper".into(),
                properties: properties(),
                ..Default::default()
            }),
        }));
        let h = if paper {
            d.add_paper_space_entity(e)?
        } else {
            d.add_entity(e)?
        };
        let dict = d.ensure_extension_dictionary(h);
        let id = d.allocate_handle();
        let mut properties = properties();
        properties.push(SemanticProperty {
            subclass: "AcDbCustomData".into(),
            code: 340,
            value: V::Handle(h),
        });
        let o = RegisteredClassObject {
            handle: id,
            owner: dict,
            dxf_name: "ACAD_PROXY_OBJECT_WRAPPER".into(),
            cpp_class_name: "AcDbProxyObjectWrapper".into(),
            properties,
            ..Default::default()
        };
        if let ObjectType::Dictionary(v) = d.objects.get_mut(&dict).unwrap() {
            v.add_entry("自定义", id);
        }
        d.objects.insert(id, ObjectType::RegisteredClass(o));
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn properties_of(e: &EntityType) -> &[SemanticProperty] {
    if let EntityType::Extended(e) = e {
        if let ExtendedEntityData::RegisteredClass(v) = &e.data {
            return &v.properties;
        }
    }
    panic!()
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    for e in src.entities() {
        assert_eq!(round.get_entity(e.common().handle), Some(e));
    }
    assert_eq!(round.objects, src.objects);
    let edit = read("edited")?;
    let h = src
        .entities_in_block("*Model_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    assert_eq!(
        properties_of(edit.get_entity(h).unwrap())[0].value,
        V::Text("修改扩展".into())
    );
    let copied = read("copied")?;
    let copy = copied
        .entities()
        .find(|e| src.get_entity(e.common().handle).is_none())
        .unwrap();
    assert_eq!(
        properties_of(copy),
        properties_of(edit.get_entity(h).unwrap())
    );
    let dict = copied
        .extension_dictionary_handle(copy.common().handle)
        .unwrap();
    if let ObjectType::Dictionary(v) = &copied.objects[&dict] {
        if let ObjectType::RegisteredClass(o) = &copied.objects[&v.get("自定义").unwrap()] {
            assert_eq!(
                o.properties.last().unwrap().value,
                V::Handle(copy.common().handle)
            );
        } else {
            panic!()
        }
    }
    let deleted = read("deleted")?;
    assert!(deleted.get_entity(copy.common().handle).is_none());
    assert_eq!(deleted.entities().count(), 2);
    let fresh = read("fresh")?;
    assert_eq!(fresh.entities().count(), 1);
    assert!(fresh
        .objects
        .values()
        .any(|o| matches!(o, ObjectType::RegisteredClass(_))));
    println!("Rust verified registered classes, typed property widths, private class strings and owned-reference copy/delete");
    Ok(())
}
