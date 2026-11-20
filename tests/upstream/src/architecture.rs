// SPDX-License-Identifier: MPL-2.0
use acadrust::classes::arch::{continuous_walls, door_size, window_size, ArchKind, NumberRole};
use acadrust::entities::{EntityType, ExtendedEntityData};
use acadrust::objects::ObjectType;
use acadrust::types::{Color, DxfVersion, Handle, LineWeight, Vector3};
use acadrust::xdata::XDataValue;
use acadrust::{DwgReader, DxfReader, DxfWriter};
use std::io::Cursor;

fn arch_entity<'a>(
    doc: &'a acadrust::CadDocument,
    name: &str,
) -> Option<&'a acadrust::classes::arch::ArchEntityData> {
    doc.entities().find_map(move |entity| {
        let EntityType::Extended(entity) = entity else {
            return None;
        };
        let ExtendedEntityData::Arch(data) = &entity.data else {
            return None;
        };
        (data.dxf_name == name).then_some(data)
    })
}

fn arch_layer(doc: &acadrust::CadDocument, name: &str) -> String {
    doc.entities()
        .find_map(|entity| {
            let EntityType::Extended(entity) = entity else {
                return None;
            };
            let ExtendedEntityData::Arch(data) = &entity.data else {
                return None;
            };
            (data.dxf_name == name).then(|| entity.common.layer.clone())
        })
        .expect(name)
}

fn has_arch_object(doc: &acadrust::CadDocument, name: &str) -> bool {
    doc.objects.values().any(|object| {
        matches!(
            object,
            ObjectType::Arch(object) if object.dxf_name == name
        )
    })
}

fn check_constructed_dwg(
    path: &str,
    class_name: &str,
    layer_name: &str,
    color: i16,
    line_weight: i16,
    styles: &[&str],
) -> acadrust::Result<acadrust::CadDocument> {
    let doc = DwgReader::from_file(path)?.read()?;
    assert_eq!(doc.version, DxfVersion::AC1032, "{path}");
    assert_eq!(doc.entities().count(), 1, "{path}");
    let layer = doc.layers.get(layer_name).expect(layer_name);
    assert_eq!(layer.color, Color::from_index(color), "{path}");
    assert_eq!(
        layer.line_weight,
        LineWeight::from_value(line_weight),
        "{path}"
    );
    assert_eq!(arch_layer(&doc, class_name), layer_name, "{path}");
    let class = doc.classes.get_by_name(class_name).expect(class_name);
    assert!(class.is_an_entity, "{path}");
    assert_eq!(class.item_class_id, 498, "{path}");
    assert_eq!(class.proxy_flags.0, 893, "{path}");
    assert_eq!(class.dwg_version, 21, "{path}");
    assert!(
        class.application_name.contains("AecArchBase80"),
        "{path}: {}",
        class.application_name
    );
    for style in styles {
        assert!(has_arch_object(&doc, style), "{path} missing {style}");
    }
    Ok(doc)
}

pub fn verify(out: &str) -> acadrust::Result<()> {
    let start = Vector3::new(-120.0, 50.0, 0.0);
    let end = Vector3::new(2880.0, 4050.0, 0.0);
    let wall = check_constructed_dwg(
        &format!("{out}-wall.dwg"),
        "AEC_WALL",
        "A-Wall",
        113,
        50,
        &[
            "AEC_MATERIAL_DEF",
            "AEC_ENDCAP_STYLE",
            "AEC_FRAME_DEF",
            "AEC_CLEANUP_GROUP_DEF",
            "AEC_WALL_STYLE",
        ],
    )?;
    let data = arch_entity(&wall, "AEC_WALL").expect("wall");
    assert_eq!(data.kind, ArchKind::Wall);
    let geometry = data.straight_wall_geometry().expect("wall geometry");
    assert!((geometry.start - start).length() < 1e-6, "{:?}", geometry.start);
    assert!((geometry.end - end).length() < 1e-6, "{:?}", geometry.end);
    assert!((geometry.width - 175.0).abs() < 1e-6);
    assert!((geometry.height - 2800.0).abs() < 1e-6);

    let writer = DxfWriter::new(&wall);
    assert!(
        writer
            .omitted_architecture_records()
            .iter()
            .all(|item| item.name != "AEC_WALL"),
        "constructed wall should keep proxy graphics"
    );
    assert!(
        writer
            .omitted_architecture_records()
            .iter()
            .any(|item| item.name == "AEC_WALL_STYLE"),
        "style objects without a DXF body are omitted"
    );
    let bytes = writer.write_to_vec()?;
    let text = String::from_utf8(bytes.clone()).expect("ascii dxf");
    assert!(text.contains("ACAD_PROXY_ENTITY"));
    let saved = DxfReader::from_reader(Cursor::new(bytes))?.read()?;
    let EntityType::Extended(entity) = saved
        .entities()
        .find(|entity| matches!(entity, EntityType::Extended(_)))
        .expect("proxy entity")
    else {
        panic!("proxy entity")
    };
    assert!(matches!(entity.data, ExtendedEntityData::Proxy(_)));
    let mut r12 = Vec::new();
    let error = DxfWriter::new(&wall)
        .with_version(DxfVersion::AC1009)
        .write_to_writer(&mut r12)
        .unwrap_err();
    assert!(
        error.to_string().contains("Arch entity AEC_WALL"),
        "{error}"
    );
    assert!(r12.is_empty());

    let edited = DwgReader::from_file(format!("{out}-wall-edited.dwg"))?.read()?;
    let geometry = arch_entity(&edited, "AEC_WALL")
        .and_then(|data| data.straight_wall_geometry())
        .expect("edited wall");
    assert!((geometry.start - Vector3::new(10.0, 20.0, 0.0)).length() < 1e-6);
    assert!((geometry.end - Vector3::new(10.0, 80.0, 0.0)).length() < 1e-6);
    assert!((geometry.width - 8.0).abs() < 1e-6);
    assert!((geometry.height - 96.0).abs() < 1e-6);

    let door = check_constructed_dwg(
        &format!("{out}-door.dwg"),
        "AEC_DOOR",
        "A-Door",
        31,
        25,
        &["AEC_MATERIAL_DEF", "AEC_DOOR_STYLE"],
    )?;
    let size = door_size(arch_entity(&door, "AEC_DOOR").expect("door")).expect("door size");
    assert!((size.width - 2600.0).abs() < 1e-6);
    assert!((size.height - 2100.0).abs() < 1e-6);

    let window = check_constructed_dwg(
        &format!("{out}-window.dwg"),
        "AEC_WINDOW",
        "A-Glaz",
        151,
        25,
        &["AEC_MATERIAL_DEF", "AEC_WINDOW_STYLE"],
    )?;
    let size = window_size(arch_entity(&window, "AEC_WINDOW").expect("window")).expect("window size");
    assert!((size.width - 1800.0).abs() < 1e-6);
    assert!((size.height - 2400.0).abs() < 1e-6);

    let wall_handle = Handle::from(0x1A00);
    for suffix in ["-omit-ascii.dxf", "-omit-binary.dxf"] {
        let restored = DxfReader::from_file(format!("{out}{suffix}"))?.read()?;
        assert!(restored.get_entity(wall_handle).is_none(), "{suffix}");
        assert!(
            restored.entities().any(|entity| matches!(entity, EntityType::Line(_))),
            "{suffix}: line kept"
        );
        assert!(
            restored.classes.get_by_name("AEC_WALL").is_some(),
            "{suffix}: class kept"
        );
        assert!(
            restored.entities().all(|entity| {
                !matches!(
                    entity,
                    EntityType::Extended(entity)
                        if matches!(&entity.data, ExtendedEntityData::Arch(data) if data.dxf_name == "AEC_WALL")
                )
            }),
            "{suffix}: AEC_WALL still present"
        );
    }
    for suffix in ["-proxy-ascii.dxf", "-proxy-binary.dxf"] {
        let restored = DxfReader::from_file(format!("{out}{suffix}"))?.read()?;
        let EntityType::Extended(entity) = restored.get_entity(wall_handle).expect(suffix) else {
            panic!("{suffix}: missing proxy")
        };
        let ExtendedEntityData::Proxy(data) = &entity.data else {
            panic!("{suffix}: {:?}", entity.data)
        };
        assert_eq!(data.graphics.data(), &[1, 2, 3, 4, 5, 6, 7, 8], "{suffix}");
    }
    let raw = DxfReader::from_file(format!("{out}-raw-ascii.dxf"))?.read()?;
    let EntityType::Extended(entity) = raw.get_entity(wall_handle).expect("raw ascii") else {
        panic!("raw ascii")
    };
    let ExtendedEntityData::Arch(data) = &entity.data else {
        panic!("raw ascii {:?}", entity.data)
    };
    assert_eq!(data.dxf_name, "AEC_WALL");
    assert!(data.raw_dxf_codes.is_some());
    let binary_raw = DxfReader::from_file(format!("{out}-raw-binary.dxf"))?.read()?;
    assert!(binary_raw.get_entity(wall_handle).is_some());
    let mut r12 = Vec::new();
    let error = DxfWriter::new(&raw)
        .with_version(DxfVersion::AC1009)
        .write_to_writer(&mut r12)
        .unwrap_err();
    assert!(
        error.to_string().contains("Arch entity AEC_WALL"),
        "{error}"
    );
    assert!(r12.is_empty());

    let wall_data = arch_entity(&wall, "AEC_WALL").expect("wall decode");
    let decoded = wall_data.decode();
    assert_eq!(decoded.schema, Some(569));
    assert!(decoded.placement.expect("wall placement").rigid);
    assert!(!decoded.strings.is_empty());
    assert!(!decoded.handles.is_empty());
    assert!(wall_data
        .fields
        .iter()
        .any(|field| field.role == NumberRole::Width && (field.value - 175.0).abs() < 1e-6));
    let style = wall.objects.values().find_map(|object| {
        let ObjectType::Arch(object) = object else {
            return None;
        };
        (object.dxf_name == "AEC_WALL_STYLE").then(|| object.decode())
    });
    let style = style.expect("wall style");
    assert_eq!(style.schema, Some(547));
    assert!(style.placement.is_none());
    assert!(style.strings.iter().any(|text| text == "Unnamed"));

    let door_decoded = arch_entity(&door, "AEC_DOOR").expect("door").decode();
    assert_eq!(door_decoded.schema, Some(558));
    assert!(door_decoded.placement.expect("door placement").rigid);
    let window_decoded = arch_entity(&window, "AEC_WINDOW").expect("window").decode();
    assert_eq!(window_decoded.schema, Some(559));
    assert!(window_decoded.placement.expect("window placement").rigid);

    let translated = DwgReader::from_file(format!("{out}-translated.dwg"))?.read()?;
    let geometry = arch_entity(&translated, "AEC_WALL")
        .and_then(|data| data.straight_wall_geometry())
        .expect("translated wall");
    assert!((geometry.start - Vector3::new(10.0, 20.0, 0.0)).length() < 1e-6);
    assert!((geometry.end - Vector3::new(8010.0, 20.0, 0.0)).length() < 1e-6);
    assert!((geometry.width - 240.0).abs() < 1e-6);

    let continuous = DwgReader::from_file(format!("{out}-continuous.dwg"))?.read()?;
    let chains = continuous_walls(&continuous);
    assert_eq!(chains.len(), 1, "continuous chain count");
    assert_eq!(chains[0].handles.len(), 3);
    assert_eq!(chains[0].points.len(), 4);
    let edited = DwgReader::from_file(format!("{out}-continuous-edited.dwg"))?.read()?;
    let chains = continuous_walls(&edited);
    assert_eq!(chains.len(), 1);
    assert!((chains[0].points[0] - Vector3::new(240.0, 0.0, 0.0)).length() < 1e-6);
    let gap = DwgReader::from_file(format!("{out}-continuous-gap.dwg"))?.read()?;
    assert_eq!(continuous_walls(&gap).len(), 2);

    let style_handle = Handle::from(0x1000);
    for suffix in ["-nod-omit-ascii.dxf", "-nod-omit-binary.dxf"] {
        let restored = DxfReader::from_file(format!("{out}{suffix}"))?.read()?;
        assert!(
            restored.objects.get(&style_handle).is_none(),
            "{suffix}: omitted style survived"
        );
        assert!(
            restored.classes.get_by_name("AEC_WALL_STYLE").is_some(),
            "{suffix}: class dropped"
        );
        let ObjectType::Dictionary(dictionary) = restored
            .objects
            .get(&restored.header.named_objects_dict_handle)
            .expect(suffix)
        else {
            panic!("{suffix}: NOD")
        };
        assert!(
            dictionary.get("AEC test style").is_none(),
            "{suffix}: NOD still names the omitted style"
        );
    }
    for suffix in ["-xdata-omit-ascii.dxf", "-xdata-omit-binary.dxf"] {
        let restored = DxfReader::from_file(format!("{out}{suffix}"))?.read()?;
        let EntityType::Line(line) = restored
            .entities()
            .find(|entity| matches!(entity, EntityType::Line(_)))
            .expect(suffix)
        else {
            panic!("{suffix}: line")
        };
        let record = line
            .common
            .extended_data
            .records()
            .iter()
            .find(|record| record.application_name == "AECBASE")
            .expect(suffix);
        assert!(
            matches!(&record.values[1], XDataValue::Handle(handle) if handle.is_null()),
            "{suffix}: {:?}",
            record.values
        );
        assert!(
            matches!(&record.values[2], XDataValue::Handle(handle) if !handle.is_null()),
            "{suffix}: layer handle dropped"
        );
    }
    Ok(())
}
