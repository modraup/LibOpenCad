// SPDX-License-Identifier: MPL-2.0
use acadrust::objects::*;
use acadrust::{CadDocument, DxfReader, DxfWriter, Handle, Vector3};
fn insert(d: &mut CadDocument, name: &str, mut o: ObjectType) -> Handle {
    let id = d.allocate_handle();
    let root = d.header.named_objects_dict_handle;
    match &mut o {
        ObjectType::DataObject(v) => {
            v.handle = id;
            v.owner = root;
        }
        ObjectType::ClassObject(v) => {
            v.handle = id;
            v.owner = root;
        }
        _ => panic!(),
    }
    if let ObjectType::Dictionary(v) = d.objects.get_mut(&root).unwrap() {
        v.add_entry(name, id);
    }
    d.objects.insert(id, o);
    id
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let root = d.header.named_objects_dict_handle;
    let buffer = insert(
        &mut d,
        "索引缓冲",
        ObjectType::DataObject(DataObject::new(DataObjectData::IdBuffer(IdBuffer {
            object_ids: vec![root],
            ..Default::default()
        }))),
    );
    for data in [
        DataObjectData::BreakData(BreakData {
            version: 2,
            point_references: vec![
                BreakPointReference {
                    reference_type: 1,
                    flags: 2,
                    identifier: 4,
                    first_point: Vector3::new(1., 2., 3.),
                    second_point: Vector3::new(4., 5., 6.),
                    ..Default::default()
                },
                BreakPointReference {
                    identifier: 9,
                    ..Default::default()
                },
            ],
            ..Default::default()
        }),
        DataObjectData::Index(Index {
            last_updated_julian_day: 2460000,
            last_updated_milliseconds: 12345,
        }),
        DataObjectData::LayerIndex(LayerIndex {
            last_updated_julian_day: 2460001,
            last_updated_milliseconds: 23456,
            entries: vec![
                LayerIndexEntry {
                    layer_count: 1,
                    name: "中文图层".into(),
                    id_buffer: buffer,
                },
                LayerIndexEntry {
                    layer_count: 2,
                    name: "其他图层".into(),
                    id_buffer: buffer,
                },
            ],
        }),
        DataObjectData::BreakPointRef,
        DataObjectData::AcDsRecord,
        DataObjectData::AcDsSchema,
        DataObjectData::Dummy,
        DataObjectData::LongTransaction,
        DataObjectData::ObjectPointer,
        DataObjectData::PartialViewingFilter(PartialViewingFilter),
    ] {
        let name = data.dxf_name().to_string();
        insert(&mut d, &name, ObjectType::DataObject(DataObject::new(data)));
    }
    let file = PointCloudDefinition {
        class_version: 3,
        source_filename: "点云.rcs".into(),
        is_loaded: true,
        point_count: 123456789012,
        extents_min: Vector3::new(-1., -2., -3.),
        extents_max: Vector3::new(10., 20., 30.),
    };
    let ramp = PointCloudColorRamp {
        id: "渐变".into(),
        class_version: 2,
        name: "高程".into(),
        colors: vec![
            PointCloudRampColor {
                color: 0x123456,
                visible: true,
            },
            PointCloudRampColor {
                color: 0x654321,
                visible: false,
            },
        ],
    };
    for data in [
        ClassObjectData::PointCloudDefinition(file.clone()),
        ClassObjectData::PointCloudDefinitionEx(file),
        ClassObjectData::PointCloudDefinitionReactor(PointCloudDefinitionReactor {
            class_version: 2,
        }),
        ClassObjectData::PointCloudDefinitionReactorEx(PointCloudDefinitionReactor {
            class_version: 3,
        }),
        ClassObjectData::PointCloudColorMap(PointCloudColorMap {
            class_version: 2,
            default_intensity_scheme: "强度".into(),
            default_elevation_scheme: "高程".into(),
            default_classification_scheme: "分类".into(),
            color_ramps: vec![ramp.clone()],
            classification_color_ramps: vec![ramp],
        }),
        ClassObjectData::NavisworksModelDefinition(NavisworksModelDefinition {
            flags: 3,
            path: "模型.nwd".into(),
            status: true,
            extents_min: Vector3::ZERO,
            extents_max: Vector3::new(10., 20., 30.),
            host_drawing_visibility: false,
        }),
        ClassObjectData::ContextDataManager(ContextDataManager {
            object_context: buffer,
            sub_managers: vec![ContextDataSubManager {
                handle: buffer,
                entries: vec![ContextDataEntry {
                    item: buffer,
                    name: "比例数据".into(),
                }],
            }],
        }),
    ] {
        let name = data.dxf_name().to_string();
        insert(
            &mut d,
            &name,
            ObjectType::ClassObject(ClassObject {
                data,
                ..Default::default()
            }),
        );
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn named(d: &CadDocument, n: &str) -> Handle {
    if let ObjectType::Dictionary(v) = &d.objects[&d.header.named_objects_dict_handle] {
        v.get(n).unwrap()
    } else {
        panic!()
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    for (h, o) in &src.objects {
        if matches!(o, ObjectType::DataObject(_) | ObjectType::ClassObject(_)) {
            assert_eq!(round.objects.get(h), Some(o));
        }
    }
    let edit = read("edited")?;
    if let ObjectType::DataObject(v) = &edit.objects[&named(&edit, "BREAKDATA")] {
        if let DataObjectData::BreakData(v) = &v.data {
            assert_eq!(v.point_references[0].first_point.z, 99.);
            assert_eq!(v.point_references[0].subentity_object, named(&edit, "BREAKDATA"));
            assert_eq!(v.point_references[0].subentity_type, 3);
            assert_eq!(v.point_references[0].subentity_index, 17);
            assert_eq!(v.point_references[1].identifier, 9);
        }
    }
    if let ObjectType::ClassObject(v) = &edit.objects[&named(&edit, "ACDBPOINTCLOUDCOLORMAP")] {
        if let ClassObjectData::PointCloudColorMap(v) = &v.data {
            assert_eq!(v.color_ramps[0].colors[1].color, 7);
            assert_eq!(v.classification_color_ramps[0].colors[1].color, 0x654321);
        }
    }
    let fresh = read("fresh")?;
    let clouds: Vec<_> = fresh.entities().collect();
    assert_eq!(clouds.len(), 2);
    for e in clouds {
        if let acadrust::EntityType::Extended(e) = e {
            if let acadrust::entities::ExtendedEntityData::PointCloudEx(v) = &e.data {
                assert_eq!(v.croppings.len(), 2);
                assert_eq!(v.croppings[0].points[1].z, 3.);
                assert!(v.croppings[1].inverted);
                assert_eq!(v.hidden_regions, vec![4, 8]);
                let reactor = &fresh.objects[&v.reactor_handle];
                if let ObjectType::ClassObject(r) = reactor {
                    assert_eq!(r.owner, e.common.handle);
                } else {
                    panic!()
                }
            } else {
                panic!()
            }
        }
    }
    let deleted = read("deleted")?;
    assert_eq!(deleted.entities().count(), 1);
    assert_eq!(deleted.objects.values().filter(|o|matches!(o,ObjectType::ClassObject(c) if matches!(c.data,ClassObjectData::PointCloudDefinitionReactorEx(_)))).count(),1);
    println!("Rust verified data/index objects, repeated class fields, cloud definitions/ramps, two crops, owned reactors and deletion");
    Ok(())
}
