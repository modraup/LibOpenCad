// SPDX-License-Identifier: MPL-2.0
use acadrust::objects::ObjectType;
use acadrust::{DxfReader, DxfVersion, DxfWriter};
use std::io::Cursor;
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    for name in [
        "proxies",
        "fields",
        "table-content",
        "tables",
        "table-geometry",
        "proxy-old",
    ] {
        let source = DxfReader::from_file(format!("{prefix}-{name}-source.dxf"))?.read()?;
        let versions = if name == "proxy-old" {
            [
                DxfVersion::AC1012,
                DxfVersion::AC1014,
                DxfVersion::AC1015,
                DxfVersion::AC1018,
            ]
        } else {
            [
                DxfVersion::AC1021,
                DxfVersion::AC1024,
                DxfVersion::AC1027,
                DxfVersion::AC1032,
            ]
        };
        let mut versions = versions.to_vec();
        if !matches!(name, "proxies" | "proxy-old") {
            versions.extend([
                DxfVersion::AC1012,
                DxfVersion::AC1014,
                DxfVersion::AC1015,
                DxfVersion::AC1018,
            ]);
        }
        for v in versions {
            let actual =
                DxfReader::from_file(format!("{prefix}-{name}-{}.dxf", v.to_dxf_string()))?
                    .read()?;
            let reference = DxfReader::from_reader(Cursor::new(
                DxfWriter::new(&source).with_version(v).write_to_vec()?,
            ))?
            .read()?;
            assert_eq!(actual.version, v);
            assert_eq!(
                actual.entities().count(),
                source.entities().count(),
                "entity inventory {name}"
            );
            for (handle, object) in &source.objects {
                if matches!(
                    object,
                    ObjectType::Field(_)
                        | ObjectType::FieldList(_)
                        | ObjectType::TableContent(_)
                        | ObjectType::TableTemplate(_)
                        | ObjectType::DataObject(_)
                        | ObjectType::ProxyObject(_)
                ) {
                    assert!(
                        actual.objects.contains_key(handle),
                        "missing source object {name} {handle:?}"
                    );
                }
            }
            for e in actual.entities() {
                assert_eq!(
                    Some(e),
                    if name == "proxies" || name == "proxy-old" || v < DxfVersion::AC1024 {
                        reference.get_entity(e.common().handle)
                    } else {
                        source.get_entity(e.common().handle)
                    },
                    "{name} {v:?}"
                );
            }
            for (handle, object) in &actual.objects {
                if matches!(
                    object,
                    ObjectType::Field(_)
                        | ObjectType::FieldList(_)
                        | ObjectType::TableContent(_)
                        | ObjectType::TableTemplate(_)
                        | ObjectType::DataObject(_)
                        | ObjectType::ProxyObject(_)
                ) {
                    assert_eq!(
                        Some(object),
                        if name == "proxies" || name == "proxy-old" || v < DxfVersion::AC1024 {
                            reference.objects.get(handle)
                        } else {
                            source.objects.get(handle)
                        },
                        "{name} {v:?} {handle:?}"
                    );
                }
            }
        }
    }
    for family in [
        "class-versions",
        "drawing-objects",
        "registered-classes",
        "data-objects",
        "object-contexts",
        "extended-entities",
        "more-extended",
        "multileaders",
        "fields",
        "tables",
        "table-content",
        "table-geometry",
    ] {
        let source = DxfReader::from_file(format!("{prefix}-{family}-source.dxf"))?.read()?;
        for v in [
            DxfVersion::AC1012,
            DxfVersion::AC1014,
            DxfVersion::AC1015,
            DxfVersion::AC1018,
            DxfVersion::AC1021,
            DxfVersion::AC1024,
            DxfVersion::AC1027,
            DxfVersion::AC1032,
        ] {
            let path = format!("{prefix}-{family}-{}", v.to_dxf_string());
            let actual = DxfReader::from_file(format!("{path}.dxf"))?.read()?;
            let reference = DxfReader::from_reader(Cursor::new(
                DxfWriter::new(&source).with_version(v).write_to_vec()?,
            ))?
            .read()?;
            let compare = |actual: &acadrust::CadDocument, reference: &acadrust::CadDocument| {
                assert_eq!(
                    actual.entities().count(),
                    reference.entities().count(),
                    "{family} {v:?}"
                );
                for (h, src) in &source.objects {
                    let Some(obj) = actual.objects.get(h) else {
                        panic!("Missing object {h:?} {path}")
                    };
                    if matches!(
                        src,
                        ObjectType::Dictionary(_)
                            | ObjectType::Layout(_)
                            | ObjectType::DictionaryWithDefault(_)
                    ) {
                        continue;
                    }
                    if matches!(
                        src,
                        ObjectType::ClassObject(_)
                            | ObjectType::VisualStyle(_)
                            | ObjectType::GeoData(_)
                            | ObjectType::RegisteredClass(_)
                            | ObjectType::DataObject(_)
                            | ObjectType::ObjectContextData(_)
                            | ObjectType::Field(_)
                            | ObjectType::TableContent(_)
                            | ObjectType::TableTemplate(_)
                    ) {
                        assert_eq!(Some(obj), reference.objects.get(h), "{path} object {h:?}");
                    }
                }
                for e in actual.entities() {
                    let other = reference
                        .get_entity(e.common().handle)
                        .expect("same entity handle");
                    if matches!(family, "tables" | "table-content")
                        && actual.version == reference.version
                    {
                        if let (acadrust::EntityType::Table(a), acadrust::EntityType::Table(b)) =
                            (e, other)
                        {
                            assert_eq!(a.rows, b.rows, "{path} visible cells");
                        }
                    }
                    if family == "registered-classes"
                        || family == "multileaders"
                        || family == "extended-entities"
                        || family == "more-extended"
                    {
                        assert_eq!(e, other, "{path} entity");
                    }
                }
            };
            let retains_layout = v >= DxfVersion::AC1024
                && matches!(
                    family,
                    "tables" | "fields" | "table-content" | "table-geometry"
                );
            compare(
                &actual,
                if v == source.version || retains_layout {
                    &source
                } else {
                    &reference
                },
            );
            let upfile = format!("{path}-up.dxf");
            if (matches!(
                family,
                "tables" | "fields" | "table-content" | "table-geometry"
            ) && v < DxfVersion::AC1021)
                || (!matches!(
                    family,
                    "tables" | "fields" | "table-content" | "table-geometry"
                ) && matches!(v, DxfVersion::AC1018 | DxfVersion::AC1024))
            {
                let up = DxfReader::from_file(upfile)?.read()?;
                let independent = DxfReader::from_reader(Cursor::new(
                    DxfWriter::new(&actual)
                        .with_version(DxfVersion::AC1032)
                        .write_to_vec()?,
                ))?
                .read()?;
                assert_eq!(up.version, DxfVersion::AC1032);
                compare(&up, &independent);
            }
        }
    }
    println!(
        "Rust verified legacy table/FIELD downgrade, modern schema retention and proxy envelopes"
    );
    Ok(())
}

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    use acadrust::classes::DxfClass;
    use acadrust::entities::{CellValue, TableCustomData};
    use acadrust::objects::{
        ClassObject, ClassObjectData as Data, DataLink, DetailViewStyle, ModelDocViewStyle,
        RenderSettings, SectionViewStyle,
    };
    let mut doc = acadrust::CadDocument::new();
    let base = ModelDocViewStyle {
        description: "视图".into(),
        display_name: "显示".into(),
        flags: 7,
        ..Default::default()
    };
    for (cpp, data) in [
        (
            "AcDbRenderSettings",
            Data::RenderSettings(RenderSettings {
                class_version: 2,
                display_index: 9,
                has_predefined: true,
                ..Default::default()
            }),
        ),
        (
            "AcDbDetailViewStyle",
            Data::DetailViewStyle(DetailViewStyle {
                base: base.clone(),
                ..Default::default()
            }),
        ),
        (
            "AcDbSectionViewStyle",
            Data::SectionViewStyle(SectionViewStyle {
                base,
                ..Default::default()
            }),
        ),
        (
            "AcDbDataLink",
            Data::DataLink(DataLink {
                description: "数据".into(),
                data_map: vec![
                    TableCustomData {
                        name: "x".into(),
                        value: CellValue::text("中文"),
                    },
                    TableCustomData {
                        name: "y".into(),
                        value: CellValue::number(12.5),
                    },
                ],
                ..Default::default()
            }),
        ),
    ] {
        let mut obj = ClassObject::new(data);
        let key = obj.dxf_name();
        doc.classes.add_or_update(DxfClass::new(key, cpp));
        obj.handle = doc.allocate_handle();
        obj.owner = doc.header.named_objects_dict_handle;
        if let Some(ObjectType::Dictionary(d)) = doc.objects.get_mut(&obj.owner) {
            d.add_entry(key, obj.handle);
        }
        doc.objects.insert(obj.handle, ObjectType::ClassObject(obj));
    }
    DxfWriter::new(&doc)
        .with_version(DxfVersion::AC1032)
        .write_to_file(ascii)?;
    DxfWriter::new_binary(&doc)
        .with_version(DxfVersion::AC1032)
        .write_to_file(binary)
}
