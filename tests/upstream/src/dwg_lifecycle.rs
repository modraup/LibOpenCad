// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::acis::{primitives::*, SatDocument};
use acadrust::entities::{AcisData, Line};
use acadrust::objects::*;
use acadrust::{DwgReader, DwgWriter, DxfVersion, EntityType, Handle, Vector3};
use std::{collections::BTreeMap, fs, path::Path};
fn data(e: &EntityType) -> &AcisData {
    match e {
        EntityType::Solid3D(v) => &v.acis_data,
        EntityType::Region(v) => &v.acis_data,
        EntityType::Body(v) => &v.acis_data,
        EntityType::Surface(v) => &v.acis_data,
        _ => panic!("modeler"),
    }
}
fn data_mut(e: &mut EntityType) -> &mut AcisData {
    match e {
        EntityType::Solid3D(v) => &mut v.acis_data,
        EntityType::Region(v) => &mut v.acis_data,
        EntityType::Body(v) => &mut v.acis_data,
        EntityType::Surface(v) => &mut v.acis_data,
        _ => panic!("modeler"),
    }
}
fn mapped(ids: &BTreeMap<Handle, Handle>, h: Handle) -> Handle {
    ids.get(&h).copied().unwrap_or(h)
}
fn remap_entity(e: &mut EntityType, ids: &BTreeMap<Handle, Handle>) {
    let c = e.common_mut();
    c.handle = mapped(ids, c.handle);
    c.owner_handle = mapped(ids, c.owner_handle);
    c.xdictionary_handle = c.xdictionary_handle.map(|h| mapped(ids, h));
    for h in &mut c.reactors {
        *h = mapped(ids, *h);
    }
    let history = match e {
        EntityType::Solid3D(v) => &mut v.history_handle,
        EntityType::Region(v) => &mut v.history_handle,
        EntityType::Body(v) => &mut v.history_handle,
        EntityType::Surface(v) => &mut v.history_handle,
        _ => panic!("modeler"),
    };
    *history = history.map(|h| mapped(ids, h));
    for m in &mut data_mut(e).materials {
        m.material_handle = m.material_handle.map(|h| mapped(ids, h));
    }
}
fn canonical_object(mut o: ObjectType) -> ObjectType {
    if let ObjectType::XRecord(v) = &mut o {
        v.raw_data.clear();
        v.raw_dwg_data = None;
        v.raw_dwg_handle_bits = 0;
        v.raw_dwg_version = None;
    }
    o
}
fn remap_object(mut o: ObjectType, ids: &BTreeMap<Handle, Handle>) -> ObjectType {
    let remap = |h: &mut Handle| *h = mapped(ids, *h);
    match &mut o {
        ObjectType::DynamicBlock(v) => {
            v.handle = mapped(ids, v.handle);
            v.visit_handles_mut(&mut |h| remap(h));
        }
        ObjectType::Dictionary(v) => {
            remap(&mut v.handle);
            remap(&mut v.owner);
            for (_, h) in &mut v.entries {
                remap(h);
            }
            for h in &mut v.reactors {
                remap(h);
            }
            v.xdictionary_handle = v.xdictionary_handle.map(|h| mapped(ids, h));
        }
        ObjectType::XRecord(v) => {
            remap(&mut v.handle);
            remap(&mut v.owner);
            for e in &mut v.entries {
                if let XRecordValue::Handle(h) = &mut e.value {
                    remap(h);
                }
            }
            for r in &mut v.object_references {
                remap(&mut r.handle);
            }
            for h in &mut v.reactors {
                remap(h);
            }
            v.xdictionary_handle = v.xdictionary_handle.map(|h| mapped(ids, h));
        }
        _ => panic!("unsupported owned object"),
    };
    canonical_object(o)
}
fn compare_shape(e: &EntityType, mut expected: SatDocument, version: DxfVersion) {
    if version <= DxfVersion::AC1015 {
        expected.header.version = acadrust::entities::acis::SatVersion::V4_0;
    }
    crate::acis::compare(
        &crate::acis::canonical(&data(e).parse().unwrap()),
        &crate::acis::canonical(&expected),
    );
}
pub fn generate(root: &str, dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    for version in [
        "AC1012", "AC1014", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032",
    ] {
        let mut d = DwgReader::from_file(
            Path::new(root)
                .join("modeler")
                .join(format!("{version}.dwg")),
        )?
        .read()?;
        let mut block = acadrust::tables::BlockRecord::new("MODEL_DETAIL");
        block.handle = d.allocate_handle();
        block.block_entity_handle = d.allocate_handle();
        block.block_end_handle = d.allocate_handle();
        d.block_records.add(block)?;
        if d.version >= DxfVersion::AC1015 {
            d.add_layout("Model-Sheet")?;
        }
        let mut control = Line::from_coords(1., 2., 3., 4., 5., 6.);
        let first = d.entities().map(|e| e.common().handle).min().unwrap();
        control.common.reactors = vec![first, d.header.model_space_block_handle];
        if let Some(graph) = d.solid_history_graph(first) {
            control.common.reactors.push(graph.root);
        }
        d.add_entity(EntityType::Line(control))?;
        DwgWriter::write_to_file(Path::new(dir).join(format!("{version}.dwg")), &d)?;
    }
    Ok(())
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DwgReader::from_file(input)?.read()?;
    let rows = fs::read_to_string(format!("{prefix}.ids"))?
        .lines()
        .map(|line| {
            let mut words = line.split_whitespace();
            (
                words.next().unwrap().to_string(),
                words.next().unwrap().parse::<u64>().unwrap(),
                words.next().unwrap().parse::<u64>().unwrap(),
            )
        })
        .collect::<Vec<_>>();
    let mut maps: BTreeMap<Handle, BTreeMap<Handle, Handle>> = BTreeMap::new();
    for line in fs::read_to_string(format!("{prefix}.map"))?.lines() {
        let v = line
            .split_whitespace()
            .map(|s| Handle::new(s.parse().unwrap()))
            .collect::<Vec<_>>();
        maps.entry(v[0]).or_default().insert(v[1], v[2]);
    }
    let first = *maps.keys().next().unwrap();
    let removed = maps[&first].keys().copied().collect::<Vec<_>>();
    let edit = Handle::new(rows.iter().find(|r| r.0 == "edit").unwrap().1);
    let copied = DwgReader::from_file(format!("{prefix}.copied.dwg"))?.read()?;
    let mut steps = vec!["copied", "moved", "removed", "deleted-source", "edited"];
    if rows.iter().any(|r| r.0 == "block") {
        steps.push("block");
    }
    if rows.iter().any(|r| r.0 == "layout") {
        steps.push("layout");
    }
    for step in steps {
        let d = DwgReader::from_file(format!("{prefix}.{step}.dwg"))?.read()?;
        for old in source.entities() {
            if step == "deleted-source" && old.common().handle == first {
                assert!(d.get_entity(first).is_none());
                continue;
            }
            let mut expected = old.clone();
            if step == "deleted-source" {
                expected
                    .common_mut()
                    .reactors
                    .retain(|h| !removed.contains(h));
            }
            assert_eq!(
                d.get_entity(old.common().handle),
                Some(&expected),
                "{step} source entity"
            );
        }
        for (h, old) in &source.objects {
            if step == "deleted-source" && removed.contains(h) {
                assert!(!d.objects.contains_key(h));
            } else {
                assert_eq!(d.objects.get(h), Some(old), "{step} source object {h:?}");
            }
        }
        if step == "removed" {
            assert_eq!(d.entities().count(), source.entities().count());
            assert_eq!(d.objects.len(), source.objects.len());
            continue;
        }
        for (src, ids) in &maps {
            let mut expected = source.get_entity(*src).unwrap().clone();
            remap_entity(&mut expected, ids);
            let h = expected.common().handle;
            let actual = d.get_entity(h).unwrap();
            let owner = if step == "moved" {
                d.header.model_space_block_handle
            } else if step == "layout" && h == maps[&first][&first] {
                Handle::new(rows.iter().find(|r| r.0 == "layout").unwrap().1)
            } else if step == "block" || step == "layout" {
                Handle::new(rows.iter().find(|r| r.0 == "block").unwrap().1)
            } else {
                d.header.paper_space_block_handle
            };
            expected.common_mut().owner_handle = owner;
            expected.common_mut().entity_mode =
                Some(if owner == d.header.model_space_block_handle {
                    2
                } else if owner == d.header.paper_space_block_handle {
                    1
                } else {
                    0
                });
            if step == "edited" && h == edit {
                compare_shape(actual, build_sphere([10., 20., 30.], 4.), d.version);
                *data_mut(&mut expected) = data(actual).clone();
                expected.common_mut().has_ds_data = actual.common().has_ds_data;
                if !data(actual).wireframe_point_present {
                    let point = data(actual).reference_centre().unwrap_or(Vector3::ZERO);
                    match &mut expected {
                        EntityType::Solid3D(v) => v.point_of_reference = point,
                        EntityType::Region(v) => v.point_of_reference = point,
                        EntityType::Body(v) => v.point_of_reference = point,
                        EntityType::Surface(v) => v.point_of_reference = point,
                        _ => {}
                    }
                }
            }
            assert_eq!(&expected, actual, "{step} copied entity {src:?}");
            for (old, new) in ids {
                if old == src {
                    continue;
                }
                assert_eq!(
                    Some(&remap_object(source.objects[old].clone(), ids)),
                    d.objects
                        .get(new)
                        .map(|o| canonical_object(o.clone()))
                        .as_ref(),
                    "{step} cloned object {old:?}"
                );
            }
            if let Some(graph) = source.solid_history_graph(*src) {
                let cg = d.solid_history_graph(h).unwrap();
                assert_eq!(cg.root, ids[&graph.root]);
                assert_eq!(
                    cg.nodes,
                    graph.nodes.iter().map(|h| ids[h]).collect::<Vec<_>>()
                );
            }
        }
        for (kind, h, original) in &rows {
            let h = Handle::new(*h);
            if kind == "new" || kind == "surface" {
                let actual = d.get_entity(h).unwrap();
                compare_shape(actual, build_box([2., 3., 4.], 5., 6., 7.), d.version);
                if kind == "surface" {
                    let EntityType::Surface(a) = actual else {
                        panic!("surface")
                    };
                    let EntityType::Surface(b) = source.get_entity(Handle::new(*original)).unwrap()
                    else {
                        panic!("source surface")
                    };
                    assert_eq!(a.surface_data, b.surface_data);
                }
            }
        }
        for block in d.block_records.iter() {
            let actual = d
                .entities_in_block(&block.name)
                .map(|e| e.common().handle)
                .collect::<Vec<_>>();
            let all = d
                .entities()
                .filter(|e| e.common().owner_handle == block.handle)
                .map(|e| e.common().handle)
                .collect::<Vec<_>>();
            assert_eq!(actual, all, "{step} space membership");
        }
    }
    let fresh = DwgReader::from_file(format!("{prefix}.fresh.dwg"))?.read()?;
    assert_eq!(
        fresh.entities().count(),
        3 + rows.iter().filter(|r| r.0 == "surface").count()
    );
    for e in fresh.entities() {
        compare_shape(e, build_box([2., 3., 4.], 5., 6., 7.), fresh.version);
    }
    assert_eq!(
        copied.entities().count(),
        source.entities().count()
            + rows
                .iter()
                .filter(|r| matches!(r.0.as_str(), "new" | "surface" | "copy"))
                .count()
    );
    println!("DWG lifecycle: geometry, full owned graphs, references, independent copies and deletion match Rust");
    Ok(())
}
