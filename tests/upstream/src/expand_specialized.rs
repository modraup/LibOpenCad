// SPDX-License-Identifier: MPL-2.0
use acadrust::{objects::ObjectType, types::Transform, DxfReader, DxfWriter, EntityType, Handle};
use std::collections::HashMap;
fn ids(path: &str) -> Vec<Handle> {
    std::fs::read_to_string(path)
        .unwrap()
        .lines()
        .map(|s| Handle::new(u64::from_str_radix(s, 16).unwrap()))
        .collect()
}
fn graph_map(
    source: &acadrust::CadDocument,
    actual: &acadrust::CadDocument,
    old: Handle,
    new: Handle,
) -> HashMap<Handle, Handle> {
    let mut map = HashMap::from([(old, new)]);
    let mut todo = vec![];
    if let (Some(a), Some(b)) = (
        source.extension_dictionary_handle(old),
        actual.extension_dictionary_handle(new),
    ) {
        todo.push((a, b));
    }
    while let Some((a, b)) = todo.pop() {
        if let Some(prev) = map.insert(a, b) {
            assert_eq!(prev, b);
            continue;
        }
        if let (ObjectType::Dictionary(x), ObjectType::Dictionary(y)) =
            (&source.objects[&a], &actual.objects[&b])
        {
            assert_eq!(x.entries.len(), y.entries.len());
            for (n, h) in &x.entries {
                todo.push((*h, y.entries.iter().find(|(m, _)| m == n).unwrap().1));
            }
        }
        if let (ObjectType::XRecord(x), ObjectType::XRecord(y)) =
            (&source.objects[&a], &actual.objects[&b])
        {
            assert_eq!(x.entries.len(), y.entries.len());
            for (x, y) in x.entries.iter().zip(&y.entries) {
                if let (
                    acadrust::objects::XRecordValue::Handle(x),
                    acadrust::objects::XRecordValue::Handle(y),
                ) = (&x.value, &y.value)
                {
                    if source.objects.contains_key(x) && actual.objects.contains_key(y) {
                        todo.push((*x, *y));
                    }
                }
            }
        }
    }
    for (a, b) in &map {
        if *a == old {
            continue;
        }
        if matches!(
            &source.objects[a],
            ObjectType::Dictionary(_) | ObjectType::XRecord(_)
        ) {
            assert_eq!(
                crate::copy_entities::rewrite_object(source.objects[a].clone(), &map),
                actual.objects[b]
            );
        }
    }
    map
}
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    let mut source = DxfReader::from_file(format!("{prefix}.source.dxf"))?.read()?;
    let mut actual = DxfReader::from_file(format!("{prefix}.dxf"))?.read()?;
    let ids = ids(&format!("{prefix}.parts"));
    let origins = crate::expand_specialized::ids(&format!("{prefix}.origins"));
    let EntityType::Insert(root) = source.get_entity(ids[0]).unwrap() else {
        panic!()
    };
    let block = source.block_records.get("ExpandedSpecialized").unwrap();
    let base = if let Some(EntityType::Block(b)) = source.get_entity(block.block_entity_handle) {
        b.base_point
    } else {
        block.base_point
    };
    // The pinned reader treats several specialized BLOCKS members as UNKNOWN.
    // Use the identical original ENTITIES records for the typed geometry oracle,
    // then use upstream INSERT::explode after its documented base-point transform.
    let members: Vec<_> = origins
        .iter()
        .map(|h| {
            let mut e = source.get_entity(*h).unwrap().clone();
            e.as_entity_mut()
                .apply_transform(&Transform::from_translation(-base));
            e
        })
        .collect();
    let expected = root.explode(&members);
    assert_eq!(expected.len(), ids.len() - 1);
    let mut serialized = actual.clone();
    let mut next_viewport_id = source
        .entities()
        .filter_map(|e| match e {
            EntityType::Viewport(v)
                if v.common.owner_handle == actual.header.paper_space_block_handle =>
            {
                Some(v.id)
            }
            _ => None,
        })
        .max()
        .unwrap_or(1)
        .max(1)
        + 1;
    for ((mut e, h), old) in expected.into_iter().zip(&ids[1..]).zip(&origins) {
        let a = actual.get_entity(*h).unwrap();
        let original = source.get_entity(*old).unwrap();
        assert!(actual
            .block_records
            .get("*Paper_Space")
            .unwrap()
            .entity_handles
            .contains(h));
        assert_eq!(a.common().layer, e.common().layer);
        assert_eq!(a.common().color, e.common().color);
        assert_eq!(a.common().linetype, e.common().linetype);
        assert_eq!(a.common().line_weight, e.common().line_weight);
        let map = graph_map(&source, &actual, *old, *h);
        let mut xdata = acadrust::xdata::ExtendedData::new();
        for mut record in original.common().extended_data.records().iter().cloned() {
            for v in &mut record.values {
                if let acadrust::xdata::XDataValue::Handle(h) = v {
                    *h = map.get(h).copied().unwrap_or(*h);
                }
            }
            xdata.add_record(record);
        }
        assert_eq!(
            a.common().extended_data,
            xdata,
            "XDATA geometry must stay fixed; internal handles must remap"
        );
        // The independent native copy/graph checks cover identity/reference fields;
        // compare the serialized geometry and every non-common entity field below.
        *e.common_mut() = a.common().clone();
        e.common_mut().raw_record = None;
        match (&mut e, a) {
            (EntityType::RasterImage(e), EntityType::RasterImage(a)) => {
                if let Some(h) = a.definition_reactor_handle {
                    let ObjectType::ImageDefinitionReactor(r) = &actual.objects[&h] else {
                        panic!()
                    };
                    assert_eq!(r.image_handle, a.common.handle);
                    assert_eq!(r.owner, a.common.handle);
                    assert_ne!(
                        Some(h),
                        if let EntityType::RasterImage(o) = original {
                            o.definition_reactor_handle
                        } else {
                            None
                        }
                    );
                }
                e.definition_reactor_handle = a.definition_reactor_handle;
            }
            (EntityType::Viewport(e), EntityType::Viewport(a)) => {
                assert_eq!(a.id, next_viewport_id);
                next_viewport_id += 1;
                e.id = a.id;
            }
            (EntityType::Extended(e), EntityType::Extended(a)) => {
                if let (
                    acadrust::entities::ExtendedEntityData::Format(e),
                    acadrust::entities::ExtendedEntityData::Format(a),
                ) = (&mut e.data, &a.data)
                {
                    // Format's writer bypasses EntityCommon and uses its raw
                    // prefix. Replace only that prefix with the validated copy
                    // metadata, retaining the independent original class body.
                    if let (Some(raw), Some(actual)) = (&mut e.raw_dxf_codes, &a.raw_dxf_codes) {
                        let body = raw
                            .iter()
                            .position(|(c, v)| *c == 100 && v == "mcsDbObjectFormat")
                            .unwrap();
                        let actual_body = actual
                            .iter()
                            .position(|(c, v)| *c == 100 && v == "mcsDbObjectFormat")
                            .unwrap();
                        let mut fields = actual[..actual_body].to_vec();
                        fields.extend_from_slice(&raw[body..]);
                        *raw = fields;
                    }
                }
            }
            _ => {}
        }
        *serialized.get_entity_mut(*h).unwrap() = e;
    }
    for e in serialized.entities_mut() {
        crate::extended_transform::strip_format_raw_xdata(e);
    }
    let expected_path = format!("{prefix}.rust.dxf");
    DxfWriter::new(&serialized).write_to_file(&expected_path)?;
    let expected = DxfReader::from_file(expected_path)?.read()?;
    for h in &ids[1..] {
        let mut a = actual.get_entity(*h).unwrap().clone();
        let mut b = expected.get_entity(*h).unwrap().clone();
        crate::extended_transform::strip_format_raw_xdata(&mut a);
        crate::extended_transform::strip_format_raw_xdata(&mut b);
        eprintln!("member {:X} {}", h.value(), a.as_entity().entity_type());
        crate::entity_transform::compare(&a, &b);
    }
    for e in source.entities() {
        assert_eq!(
            actual.get_entity(e.common().handle),
            Some(e),
            "Source entity changed"
        );
    }
    let expanded_tables: Vec<_> = ids[1..]
        .iter()
        .copied()
        .filter(|handle| {
            matches!(actual.get_entity(*handle), Some(EntityType::Table(_)))
                && actual.xrecord(*handle, "ACAD_XREC_ROUNDTRIP").is_some()
        })
        .collect();
    if !expanded_tables.is_empty() {
        let mut expected_targets = crate::tables::take_table_recompose(&mut source);
        expected_targets.extend(expanded_tables);
        expected_targets.sort();
        let mut actual_targets = crate::tables::take_table_recompose(&mut actual);
        actual_targets.sort();
        assert_eq!(actual_targets, expected_targets);
    }
    for (h, o) in &source.objects {
        let mut a = actual.objects[h].clone();
        let mut b = o.clone();
        match (&mut a, &mut b) {
            (ObjectType::Layout(a), ObjectType::Layout(b)) => {
                let added:Vec<_>=ids[1..].iter().copied().filter(|h|matches!(actual.get_entity(*h),Some(EntityType::Viewport(v)) if v.common.owner_handle==a.block_record)).collect();
                for h in &added {
                    assert!(a.viewports.contains(h));
                }
                a.viewports.retain(|h| !added.contains(h));
                assert_eq!(a.viewports, b.viewports);
            }
            (ObjectType::UnderlayDefinition(a), ObjectType::UnderlayDefinition(b)) => {
                let added:Vec<_>=ids[1..].iter().copied().filter(|id|matches!(actual.get_entity(*id),Some(EntityType::Underlay(u)) if u.definition_handle==*h)).collect();
                for h in &added {
                    assert!(a.reactors.contains(h));
                }
                a.reactors.retain(|h| !added.contains(h));
                assert_eq!(a.reactors, b.reactors);
            }
            (ObjectType::ImageDefinition(a), ObjectType::ImageDefinition(b)) => {
                let added: Vec<_> = ids[1..]
                    .iter()
                    .filter_map(|id| {
                        if let Some(EntityType::RasterImage(e)) = actual.get_entity(*id) {
                            if e.definition_handle == Some(*h) {
                                e.definition_reactor_handle
                            } else {
                                None
                            }
                        } else {
                            None
                        }
                    })
                    .collect();
                for h in &added {
                    assert!(a.reactors.contains(h));
                }
                a.reactors.retain(|h| !added.contains(h));
                assert_eq!(a.reactors, b.reactors);
            }
            _ => {}
        }
        assert_eq!(a, b, "Source object changed {h:?}");
    }
    Ok(())
}
