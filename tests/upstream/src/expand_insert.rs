// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::Insert;
use acadrust::objects::ObjectType;
use acadrust::{CadDocument, DxfReader, EntityType, Handle};
use std::collections::HashMap;
fn expand(d: &mut CadDocument, e: EntityType, depth: usize) -> Vec<EntityType> {
    assert!(depth < 64);
    if !matches!(e, EntityType::Insert(_)) {
        return vec![e];
    }
    let parts = d.explode_entity(&e);
    assert!(!parts.is_empty(), "Rust refused nested expansion");
    parts
        .into_iter()
        .flat_map(|p| expand(d, p, depth + 1))
        .collect()
}
pub fn verify(prefix: &str) -> acadrust::Result<()> {
    for step in 0..4 {
        eprintln!("expand step {step}");
        let source = DxfReader::from_file(format!("{prefix}.{step}.source.dxf"))?.read()?;
        let actual = DxfReader::from_file(format!("{prefix}.{step}.dxf"))?.read()?;
        let ids = std::fs::read_to_string(format!("{prefix}.{step}.parts"))?;
        let ids: Vec<_> = ids
            .lines()
            .map(|h| Handle::new(u64::from_str_radix(h, 16).unwrap()))
            .collect();
        let root = source.get_entity(ids[0]).unwrap().clone();
        let mut reference = source.clone();
        let expected = if step == 3 {
            let EntityType::Insert(i) = root else {
                panic!()
            };
            i.array_points()
                .into_iter()
                .flat_map(|p| {
                    let mut cell = Insert::new(&i.block_name, p).with_scale(
                        i.x_scale(),
                        i.y_scale(),
                        i.z_scale(),
                    );
                    cell.common = i.common.clone();
                    cell.normal = i.normal;
                    cell.rotation = i.rotation;
                    expand(&mut reference, EntityType::Insert(cell), 0)
                })
                .collect()
        } else {
            expand(&mut reference, root, 0)
        };
        assert_eq!(expected.len(), ids.len() - 1);
        for (index, (mut e, h)) in expected.into_iter().zip(&ids[1..]).enumerate() {
            eprintln!("part {index} {}", e.as_entity().entity_type());
            let a = actual.get_entity(*h).unwrap();
            assert_eq!(a.common().entity_mode, Some(1));
            assert!(actual
                .block_records
                .get("*Paper_Space")
                .unwrap()
                .entity_handles
                .contains(h));
            let mut map = HashMap::from([(e.common().handle, *h)]);
            assert_eq!(
                e.common().xdictionary_handle.is_some(),
                a.common().xdictionary_handle.is_some()
            );
            let mut todo = Vec::new();
            if let (Some(x), Some(y)) =
                (e.common().xdictionary_handle, a.common().xdictionary_handle)
            {
                todo.push((x, y));
            }
            while let Some((x, y)) = todo.pop() {
                if let Some(old) = map.insert(x, y) {
                    assert_eq!(old, y);
                    continue;
                }
                if let (ObjectType::Dictionary(x), ObjectType::Dictionary(y)) =
                    (&reference.objects[&x], &actual.objects[&y])
                {
                    assert_eq!(x.entries.len(), y.entries.len());
                    for (n, h) in &x.entries {
                        todo.push((*h, y.entries.iter().find(|(m, _)| m == n).unwrap().1));
                    }
                }
            }
            for (x, y) in &map {
                if *x != e.common().handle {
                    assert_eq!(
                        crate::copy_entities::rewrite_object(reference.objects[x].clone(), &map),
                        actual.objects[y]
                    );
                }
            }
            e.common_mut().handle = *h;
            e.common_mut().owner_handle = a.common().owner_handle;
            e.common_mut().entity_mode = Some(1);
            e.common_mut().xdictionary_handle = a.common().xdictionary_handle;
            crate::entity_transform::compare(a, &e);
        }
        for e in source.entities() {
            assert_eq!(
                actual.get_entity(e.common().handle),
                Some(e),
                "source entity changed"
            );
        }
        for (h, o) in &source.objects {
            assert_eq!(&actual.objects[h], o, "source object changed");
        }
        assert_eq!(
            actual.entities().count(),
            source.entities().count() + ids.len() - 1
        );
    }
    Ok(())
}
