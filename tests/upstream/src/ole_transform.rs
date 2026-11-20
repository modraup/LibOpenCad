// SPDX-License-Identifier: MPL-2.0
use crate::entity_transform::point;
use acadrust::compound_file::BinaryRecord;
use acadrust::entities::ole2frame::{OleFrameEnvelope, OleObjectType};
use acadrust::entities::{Light, Ole2Frame};
use acadrust::objects::{Dictionary, ObjectType, XRecord, XRecordEntry, XRecordValue};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector3};
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(acadrust::Layer::new("Control"))?;
    doc.app_ids.add(acadrust::tables::AppId::new("Geometry"))?;
    let raw = std::fs::read(
        std::path::Path::new(ascii)
            .parent()
            .unwrap()
            .join("ole-source-storage.bin"),
    )?;
    let mut handles = vec![];
    for kind in 0..5 {
        for corners in 0..4 {
            let mut e = Ole2Frame::new();
            match kind {
                0 => {}
                1 => {
                    e.set_payload(&(0..257).map(|i| i as u8).collect::<Vec<_>>());
                }
                2 => {
                    e.set_payload(&raw);
                }
                3 => {
                    e.set_payload(&raw);
                    e.envelope = OleFrameEnvelope::Geometry {
                        marker: 0x1234,
                        extension_records: BinaryRecord::split(&[9, 8, 7, 0, 255], 4096),
                    };
                }
                _ => {
                    let mut data = vec![255; 100];
                    data.extend_from_slice(&raw);
                    e.set_payload(&data);
                    assert!(matches!(e.envelope, OleFrameEnvelope::Legacy { .. }));
                }
            }
            let (a, b) = match corners {
                0 => (Vector3::new(-5., 20., 2.), Vector3::new(10., -5., 2.)),
                1 => (Vector3::new(6., -7., 8.), Vector3::new(-4., 3., -2.)),
                2 => (Vector3::new(3., 4., 5.), Vector3::new(3., 4., 5.)),
                _ => (Vector3::new(1e-14, -2e-14, 3e-14), Vector3::ZERO),
            };
            e.upper_left_corner = a;
            e.lower_right_corner = b;
            e.source_application = format!("中文对象{kind}-{corners}");
            e.ole_object_type = match corners % 3 {
                0 => OleObjectType::Link,
                1 => OleObjectType::Embedded,
                _ => OleObjectType::Static,
            };
            e.lock_aspect = if corners % 2 == 0 { 1 } else { 255 };
            e.common.color = acadrust::Color::from_index(3);
            handles.push(doc.add_entity(EntityType::Ole2Frame(Box::new(e)))?);
        }
    }
    for h in &handles {
        let mut dict = Dictionary::new();
        dict.handle = doc.allocate_handle();
        dict.owner = *h;
        dict.hard_owner = true;
        let mut record = XRecord::new();
        record.handle = doc.allocate_handle();
        record.owner = dict.handle;
        record.entries = vec![
            XRecordEntry::new(1, XRecordValue::String("嵌入数据引用".into())),
            XRecordEntry::new(330, XRecordValue::Handle(*h)),
            XRecordEntry::new(310, XRecordValue::Chunk(vec![0, 127, 128, 255])),
        ];
        dict.add_entry("载荷", record.handle);
        let common = doc.get_entity_mut(*h).unwrap().common_mut();
        common.xdictionary_handle = Some(dict.handle);
        common.reactors = vec![record.handle];
        use acadrust::xdata::{ExtendedDataRecord, XDataValue as X};
        common.extended_data.add_record(ExtendedDataRecord {
            application_name: "Geometry".into(),
            values: vec![
                X::Position3D(Vector3::new(1., 2., 3.)),
                X::Direction3D(Vector3::UNIT_Y),
                X::Distance(2.5),
                X::Handle(record.handle),
                X::BinaryData(vec![0, 255, 10, 13]),
            ],
        });
        doc.objects
            .insert(dict.handle, ObjectType::Dictionary(dict));
        doc.objects
            .insert(record.handle, ObjectType::XRecord(record));
    }
    for h in [handles[8], handles[12]] {
        let mut control = doc.get_entity(h).unwrap().clone();
        let c = control.common_mut();
        c.handle = Handle::NULL;
        c.layer = "Control".into();
        c.xdictionary_handle = None;
        c.reactors.clear();
        if let EntityType::Ole2Frame(e) = &mut control {
            e.is_paper_space = true;
        }
        doc.add_paper_space_entity(control)?;
    }
    let mut light = Light::new();
    light.common.layer = "Control".into();
    doc.add_entity(EntityType::Light(Box::new(light)))?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    let doc = DxfReader::from_file(ascii)?.read()?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}
pub(crate) fn normalize(a: &mut EntityType, b: &EntityType) {
    let (EntityType::Ole2Frame(a), EntityType::Ole2Frame(b)) = (a, b) else {
        panic!()
    };
    point(a.upper_left_corner, b.upper_left_corner);
    point(a.lower_right_corner, b.lower_right_corner);
    a.upper_left_corner = b.upper_left_corner;
    a.lower_right_corner = b.lower_right_corner;
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    assert_eq!(source.entities().count(), 23);
    let mut compound = 0;
    let mut geometry = 0;
    let mut dictionaries = 0;
    for e in source.entities() {
        if let EntityType::Ole2Frame(e) = e {
            if e.storage.compound_file.is_some() {
                compound += 1;
            }
            if matches!(e.envelope, OleFrameEnvelope::Geometry { .. }) {
                geometry += 1;
            }
            if e.common.xdictionary_handle.is_some() {
                dictionaries += 1;
            }
        }
    }
    assert_eq!((compound, geometry, dictionaries), (14, 5, 20));
    for step in 0..16 {
        eprintln!("ole step {step}");
        let t = crate::entity_transform::operation(if step == 14 {
            4
        } else if step == 15 {
            6
        } else {
            step
        });
        let mut expected = source.clone();
        for e in expected.entities_mut() {
            if e.common().layer == "Control" {
                continue;
            }
            if step == 0 {
                e.translate(Vector3::new(3., -4., 5.));
            } else if [5, 6, 7, 11, 14, 15].contains(&step) {
                e.apply_mirror(&t);
                if step == 15 {
                    e.apply_mirror(&t);
                }
            } else {
                e.apply_transform(&t);
            }
        }
        let rows = std::fs::read_to_string(format!("{prefix}.{step}.trace"))?;
        assert_eq!(rows.lines().count(), 22);
        for row in rows.lines() {
            let mut p = row.split_whitespace();
            let h = Handle::new(u64::from_str_radix(p.next().unwrap(), 16).unwrap());
            let EntityType::Ole2Frame(e) = expected.get_entity(h).unwrap() else {
                panic!()
            };
            let v: Vec<f64> = p.map(|s| s.parse().unwrap()).collect();
            assert_eq!(v.len(), 6);
            point(Vector3::new(v[0], v[1], v[2]), e.upper_left_corner);
            point(Vector3::new(v[3], v[4], v[5]), e.lower_right_corner);
        }
        // Rust serializes geometric wrapper metadata from its new corners.
        // Compare decoded geometry/storage and separately require C++ raw payload preservation.
        let path = format!("{prefix}.{step}.rust.dxf");
        DxfWriter::new(&expected).write_to_file(&path)?;
        let expected = DxfReader::from_file(path)?.read()?;
        let actual = DxfReader::from_file(format!("{prefix}.{step}.dxf"))?.read()?;
        assert_eq!(actual.entities().count(), 23);
        for e in expected.entities() {
            let a = actual.get_entity(e.common().handle).unwrap();
            if let EntityType::Ole2Frame(frame) = a {
                crate::entity_transform::compare(a, e);
                let EntityType::Ole2Frame(old) = source.get_entity(e.common().handle).unwrap()
                else {
                    panic!()
                };
                assert_eq!(frame.storage, old.storage);
                assert_eq!(frame.envelope, old.envelope);
                assert!(!frame.rewrites_source_storage());
                // SourceStorage equality deliberately ignores bytes. Reset only
                // the corners so encoded_payload compares storage bytes including slack.
                let mut original_corners = (**frame).clone();
                original_corners.upper_left_corner = old.upper_left_corner;
                original_corners.lower_right_corner = old.lower_right_corner;
                assert_eq!(original_corners.encoded_payload(), old.encoded_payload());
            } else {
                assert_eq!(a, source.get_entity(e.common().handle).unwrap());
            }
        }
        assert_eq!(actual.header, source.header);
        assert_eq!(actual.layers, source.layers);
        assert_eq!(actual.block_records, source.block_records);
        assert_eq!(actual.objects, source.objects);
    }
    Ok(())
}
