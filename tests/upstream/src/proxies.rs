// SPDX-License-Identifier: MPL-2.0
use acadrust::classes::{DxfClass, ProxyFlags};
use acadrust::entities::proxy_graphics::{ProxyGraphicRecord, ProxyGraphics, ProxyUnicodeText};
use acadrust::entities::{EntityCommon, ExtendedEntity, ExtendedEntityData, ProxyEntityData};
use acadrust::objects::{
    ObjectType, ProxyObject, ProxyObjectReference, ProxyPayload, ProxyReferenceKind,
};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector3};
fn graphic() -> Vec<u8> {
    let mut records = vec![
        ProxyGraphicRecord::FillOff,
        ProxyGraphicRecord::UnicodeText(ProxyUnicodeText {
            position: Vector3::new(1., 2., 3.),
            normal: Vector3::UNIT_Z,
            direction: Vector3::UNIT_X,
            height: 2.,
            width_factor: 1.,
            oblique_angle: 0.25,
            text: "代理中文😀".into(),
        }),
        ProxyGraphicRecord::Unknown {
            record_type: 999,
            data: vec![1, 2, 3, 4],
        },
    ];
    records.extend(crate::proxy_commands::records());
    ProxyGraphics { records }.encode().unwrap()
}
fn proxy() -> ProxyEntityData {
    ProxyEntityData {
        proxy_id: 498,
        class_id: 500,
        dxf_subclass: String::new(),
        version: 0,
        dwg_version: 32,
        maintenance_version: 106,
        from_dxf: false,
        graphics: ProxyPayload::from_bytes(&graphic()),
        payload: ProxyPayload::from_bits(&[0xab, 0xcf], 10),
        text_payload: ProxyPayload::from_bits(&[0xef], 5),
        object_ids: vec![],
    }
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let mut class = DxfClass::new_entity("CUSTOM_PROXY", "CustomProxy");
    class.proxy_flags = ProxyFlags(127);
    d.classes.add_or_update(class);
    for paper in [false, true] {
        let e = EntityType::Extended(Box::new(ExtendedEntity {
            common: EntityCommon::new(),
            data: ExtendedEntityData::Proxy(proxy()),
        }));
        let h = if paper {
            d.add_paper_space_entity(e)?
        } else {
            d.add_entity(e)?
        };
        let dict = d.ensure_extension_dictionary(h);
        let mut p = ProxyObject {
            handle: d.allocate_handle(),
            owner: dict,
            proxy_id: 499,
            class_id: 600,
            dwg_version: 32,
            maintenance_version: 106,
            payload: ProxyPayload::from_bits(&[0x12, 0x38], 13),
            object_ids: vec![ProxyObjectReference {
                handle: h,
                kind: ProxyReferenceKind::SoftPointer,
            }],
            ..Default::default()
        };
        let id = p.handle;
        if let ObjectType::Dictionary(dd) = d.objects.get_mut(&dict).unwrap() {
            dd.add_entry("代理", id);
        }
        d.objects.insert(id, ObjectType::ProxyObject(p.clone()));
        p.handle = d.allocate_handle();
        p.from_dxf = true;
        p.payload = ProxyPayload::default();
        p.object_ids.clear();
        p.dxf_data = vec![
            (100, "AcDbCustom".into()),
            (90, "7".into()),
            (1, "原始中文".into()),
        ];
        if let ObjectType::Dictionary(dd) = d.objects.get_mut(&dict).unwrap() {
            dd.add_entry("DXF代理", p.handle);
        }
        d.objects.insert(p.handle, ObjectType::ProxyObject(p));
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn value(d: &CadDocument, h: Handle) -> &ProxyEntityData {
    if let EntityType::Extended(e) = d.get_entity(h).unwrap() {
        if let ExtendedEntityData::Proxy(p) = &e.data {
            return p;
        }
    }
    panic!()
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let h = src
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
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    assert_eq!(round.objects, src.objects);
    assert_eq!(value(&round, h), value(&src, h));
    let edited = read("edited")?;
    let p = value(&edited, h);
    assert_eq!(p.payload.data(), vec![0x55, 0x80]);
    assert_eq!(p.payload.bit_count, 9);
    let g = ProxyGraphics::decode(&p.graphics.data()).unwrap();
    let expected = acadrust::entities::proxy_graphics::CheckedProxyGraphics::decode_checked(
        &value(&src, h).graphics.data(),
    )
    .unwrap();
    let actual = acadrust::entities::proxy_graphics::CheckedProxyGraphics::decode_checked(
        &p.graphics.data(),
    )
    .unwrap();
    assert_eq!(actual.records[3..], expected.records[3..]);
    if let ProxyGraphicRecord::UnicodeText(t) = &g.records[1] {
        assert_eq!(t.text, "修改代理😀");
        assert_eq!(t.height, 4.);
    } else {
        panic!()
    }
    assert_eq!(edited.get_entity(neighbor), src.get_entity(neighbor));
    let copied = read("copied")?;
    let copy = copied
        .entities()
        .find(|e| edited.get_entity(e.common().handle).is_none())
        .unwrap()
        .common()
        .handle;
    let dict = copied.extension_dictionary_handle(copy).unwrap();
    if let ObjectType::Dictionary(dict) = &copied.objects[&dict] {
        if let ObjectType::ProxyObject(p) = &copied.objects[&dict.get("代理").unwrap()] {
            assert_eq!(p.object_ids[0].handle, copy);
        } else {
            panic!()
        }
    }
    let deleted = read("deleted")?;
    assert!(deleted.get_entity(copy).is_none());
    let fresh = read("fresh")?;
    let e = fresh.entities().next().unwrap().common().handle;
    assert_eq!(value(&fresh, e).payload.bit_count, 9);
    assert!(
        fresh
            .classes
            .get_by_name("CUSTOM_CPP")
            .unwrap()
            .is_an_entity
    );
    println!("Rust verified proxy bit streams, text streams, Unicode graphics, raw DXF objects, classes and copy references");
    Ok(())
}
