// SPDX-License-Identifier: MPL-2.0
use crate::acis::{canonical, compare};
use acadrust::entities::acis::{primitives::*, SabWriter, SatDocument, SatRecord, SatToken};
use acadrust::entities::{AcisData, Body, Region, Solid3D, Surface, SurfaceKind};
use acadrust::objects::{Dictionary, ObjectType, XRecord, XRecordEntry, XRecordValue};
use acadrust::{CadDocument, DxfReader, DxfVersion, DxfWriter, EntityType, Handle};
use std::{fs, path::Path};
fn data(e: &EntityType) -> &AcisData {
    match e {
        EntityType::Solid3D(e) => &e.acis_data,
        EntityType::Region(e) => &e.acis_data,
        EntityType::Body(e) => &e.acis_data,
        EntityType::Surface(e) => &e.acis_data,
        _ => panic!("modeler entity"),
    }
}
fn entity(kind: usize, d: &SatDocument) -> EntityType {
    let sat = d.to_sat_string();
    match kind {
        0 => EntityType::Solid3D(Solid3D::from_sat(&sat)),
        1 => EntityType::Region(Region::from_sat(&sat)),
        2 => EntityType::Body(Body::from_sat(&sat)),
        _ => {
            let mut e = Surface::new(if kind == 3 {
                SurfaceKind::Generic
            } else {
                SurfaceKind::Plane
            });
            e.acis_data = AcisData::from_sat(&sat);
            e.u_isolines = 7;
            e.v_isolines = 9;
            EntityType::Surface(Box::new(e))
        }
    }
}
pub fn generate(dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    for version in [
        DxfVersion::AC1012,
        DxfVersion::AC1014,
        DxfVersion::AC1015,
        DxfVersion::AC1018,
        DxfVersion::AC1021,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
        DxfVersion::AC1032,
    ] {
        let mut doc = CadDocument::new();
        doc.version = version;
        doc.layers.add(acadrust::Layer::new("中文模型"))?;
        doc.layers.add(acadrust::Layer::new("Control"))?;
        let shapes = [
            build_box([1., 2., 3.], 4., 6., 8.),
            build_cylinder([2., -3., 4.], 3., 7.),
            build_sphere([-2., 1., 3.], 4.),
            build_torus([2., 3., 1.], 7., 2.),
            build_wedge([1., 2., 3.], 4., 5., 6.),
        ];
        for k in 0..if version >= DxfVersion::AC1021 { 5 } else { 3 } {
            for control in [false, true] {
                let mut e = entity(k, &shapes[k]);
                e.common_mut().layer = if control { "Control" } else { "中文模型" }.into();
                // Nonidentity initial placement exercises composition order.
                e.apply_transform(&crate::entity_transform::operation(2));
                let h = if control {
                    doc.add_paper_space_entity(e)?
                } else {
                    doc.add_entity(e)?
                };
                let mut dictionary = Dictionary::new();
                dictionary.handle = doc.allocate_handle();
                dictionary.owner = h;
                dictionary.hard_owner = true;
                let mut record = XRecord::new();
                record.handle = doc.allocate_handle();
                record.owner = dictionary.handle;
                record.entries = vec![
                    XRecordEntry::new(1, XRecordValue::String("模型元数据 A^ 中文".into())),
                    XRecordEntry::new(330, XRecordValue::Handle(h)),
                    XRecordEntry::new(310, XRecordValue::Chunk(vec![0, 128, 255])),
                ];
                dictionary.add_entry("几何说明", record.handle);
                let common = doc.get_entity_mut(h).unwrap().common_mut();
                common.xdictionary_handle = Some(dictionary.handle);
                common.reactors = vec![record.handle];
                doc.objects
                    .insert(dictionary.handle, ObjectType::Dictionary(dictionary));
                doc.objects
                    .insert(record.handle, ObjectType::XRecord(record));
            }
        }
        for binary in [false, true] {
            let path = Path::new(dir).join(format!(
                "{:?}-{}.dxf",
                version,
                if binary { "binary" } else { "ascii" }
            ));
            let writer = if binary {
                DxfWriter::new_binary(&doc)
            } else {
                DxfWriter::new(&doc)
            };
            writer.with_version(version).write_to_file(path)?;
        }
    }
    Ok(())
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    if input.ends_with("-binary.dxf") {
        let ascii = DxfReader::from_file(input.replace("-binary.dxf", "-ascii.dxf"))?.read()?;
        assert_eq!(
            source.version, ascii.version,
            "Old binary fixture must retain actual version"
        );
        for e in ascii.entities() {
            let a = source.get_entity(e.common().handle).unwrap();
            compare(
                &canonical(&data(e).parse().unwrap()),
                &canonical(&data(a).parse().unwrap()),
            );
            assert_eq!(a.common().layer, e.common().layer);
        }
    }
    for e in source.entities() {
        let g = data(e).parse().unwrap();
        let path = format!("{prefix}.before-{:X}.sat", e.common().handle.value());
        compare(
            &canonical(&g),
            &SatDocument::parse(&fs::read_to_string(path)?).unwrap(),
        );
    }
    for step in 1..14 {
        if step == 11 {
            continue;
        }
        let mut expected = source.clone();
        for e in expected.entities_mut() {
            if e.common().layer != "Control" {
                e.apply_transform(&crate::entity_transform::operation(step));
                if step == 10 {
                    if let EntityType::Surface(e) = e {
                        e.u_isolines = -3;
                        e.v_isolines = 32767;
                    }
                }
            }
        }
        for format in ["ascii", "binary"] {
            let path = format!("{prefix}.{step}.{format}.dxf");
            let actual = DxfReader::from_file(&path)?.read()?;
            assert_eq!(actual.version, source.version);
            assert_eq!(actual.entities().count(), source.entities().count());
            for e in expected.entities() {
                let a = actual
                    .get_entity(e.common().handle)
                    .expect("handle preserved");
                assert_eq!(std::mem::discriminant(a), std::mem::discriminant(e));
                assert_eq!(a.common().layer, e.common().layer);
                assert_eq!(a.common().owner_handle, e.common().owner_handle);
                let mut ca = a.common().clone();
                let mut ce = e.common().clone();
                ca.raw_record = None;
                ce.raw_record = None;
                assert_eq!(ca, ce, "Common modeler data changed");
                compare(
                    &canonical(&data(e).parse().unwrap()),
                    &canonical(&data(a).parse().unwrap()),
                );
                if let (EntityType::Solid3D(x), EntityType::Solid3D(y)) = (a, e) {
                    assert_eq!((&x.uid, x.history_handle), (&y.uid, y.history_handle));
                }
                if let (EntityType::Surface(x), EntityType::Surface(y)) = (a, e) {
                    assert_eq!((x.u_isolines, x.v_isolines), (y.u_isolines, y.v_isolines));
                }
            }
        }
    }
    for format in ["ascii", "binary"] {
        let actual = DxfReader::from_file(format!("{prefix}.added.{format}.dxf"))?.read()?;
        for line in fs::read_to_string(format!("{prefix}.added"))?.lines() {
            let mut v = line.split_whitespace();
            let h = u64::from_str_radix(v.next().unwrap(), 16).unwrap();
            let kind: usize = v.next().unwrap().parse().unwrap();
            let a = actual.get_entity(Handle::new(h)).unwrap();
            let d = build_box([1., 2., 3.], 4., 6., 8.);
            let e = entity(kind, &d);
            let mut g = data(&e).parse().unwrap();
            if source.version >= DxfVersion::AC1027 {
                g.strip_for_sab();
            }
            compare(&canonical(&g), &canonical(&data(a).parse().unwrap()));
            assert_eq!(
                a.common().owner_handle,
                actual
                    .block_records
                    .iter()
                    .find(|b| b.is_paper_space())
                    .unwrap()
                    .handle
            );
        }
        for e in source.entities() {
            let a = actual.get_entity(e.common().handle).unwrap();
            compare(
                &canonical(&data(e).parse().unwrap()),
                &canonical(&data(a).parse().unwrap()),
            );
        }
    }
    for format in ["ascii", "binary"] {
        let tagged = DxfReader::from_file(format!("{prefix}.tagged.{format}.dxf"))?.read()?;
        let first = source
            .entities()
            .find(|e| e.common().layer != "Control")
            .unwrap();
        let mut expected = first.clone();
        let mut graph = data(first).parse().unwrap();
        let mut extra = SatRecord::new(0, "custom-attrib");
        extra.tokens = vec![
            SatToken::String("中文 A^ # ".repeat(700)),
            SatToken::Integer(123),
        ];
        graph.add_record(extra);
        if let EntityType::Solid3D(e) = &mut expected {
            e.acis_data = if source.version >= DxfVersion::AC1027 {
                AcisData::from_sab(SabWriter::write(&graph))
            } else {
                AcisData::from_sat_document(&graph)
            };
        } else {
            panic!("first modeler");
        }
        expected.apply_transform(&crate::entity_transform::operation(1));
        compare(
            &canonical(&data(&expected).parse().unwrap()),
            &canonical(
                &data(tagged.get_entity(first.common().handle).unwrap())
                    .parse()
                    .unwrap(),
            ),
        );
        let fresh = DxfReader::from_file(format!("{prefix}.new.{format}.dxf"))?.read()?;
        assert_eq!(fresh.entities().count(), 1);
        let mut sphere = build_sphere([1., 2., 3.], 4.);
        sphere.strip_for_sab();
        compare(
            &canonical(&sphere),
            &canonical(&data(fresh.entities().next().unwrap()).parse().unwrap()),
        );
        let copies = fs::read_to_string(format!("{prefix}.copies"))?;
        for removed in [false, true] {
            let actual = DxfReader::from_file(format!(
                "{prefix}.{}.{format}.dxf",
                if removed { "removed" } else { "copies" }
            ))?
            .read()?;
            let targets = source
                .entities()
                .filter(|e| e.common().layer != "Control")
                .count();
            assert_eq!(
                actual.entities().count(),
                source.entities().count() + if removed { 0 } else { targets }
            );
            for line in copies.lines() {
                let ids: Vec<_> = line
                    .split_whitespace()
                    .map(|h| Handle::new(u64::from_str_radix(h, 16).unwrap()))
                    .collect();
                let mut expected = source.get_entity(ids[0]).unwrap().clone();
                expected.apply_transform(&crate::entity_transform::operation(1));
                let a = actual.get_entity(ids[1]).unwrap();
                compare(
                    &canonical(&data(&expected).parse().unwrap()),
                    &canonical(&data(a).parse().unwrap()),
                );
                assert_eq!(
                    a.common().owner_handle,
                    actual
                        .block_records
                        .iter()
                        .find(|b| b.is_paper_space())
                        .unwrap()
                        .handle
                );
                let original = source.get_entity(ids[0]).unwrap();
                let old_dictionary = original.common().xdictionary_handle.unwrap();
                let new_dictionary = a.common().xdictionary_handle.unwrap();
                assert_ne!(old_dictionary, new_dictionary);
                let ObjectType::Dictionary(dict) = actual.objects.get(&new_dictionary).unwrap()
                else {
                    panic!("copied dictionary")
                };
                assert_eq!(dict.owner, ids[1]);
                assert_eq!(dict.entries.len(), 1);
                let record_handle = dict.entries[0].1;
                let ObjectType::XRecord(record) = actual.objects.get(&record_handle).unwrap()
                else {
                    panic!("copied xrecord")
                };
                assert_eq!(record.owner, new_dictionary);
                assert_eq!(record.entries[1].value, XRecordValue::Handle(ids[1]));
                assert_eq!(a.common().reactors, vec![record_handle]);
                if removed {
                    assert!(actual.get_entity(ids[0]).is_none());
                    assert!(!actual.objects.contains_key(&old_dictionary));
                    for reactor in &original.common().reactors {
                        assert!(!actual.objects.contains_key(reactor));
                    }
                }
            }
            for e in source.entities() {
                if !removed || e.common().layer == "Control" {
                    compare(
                        &canonical(&data(e).parse().unwrap()),
                        &canonical(
                            &data(actual.get_entity(e.common().handle).unwrap())
                                .parse()
                                .unwrap(),
                        ),
                    );
                }
            }
        }
        let cleaned = DxfReader::from_file(format!("{prefix}.cleaned.{format}.dxf"))?.read()?;
        assert_eq!(
            cleaned.entities().count(),
            source
                .entities()
                .filter(|e| e.common().layer == "Control")
                .count()
        );
    }
    println!("Modeler source, 12 transforms and additions match Rust: {input}");
    Ok(())
}
