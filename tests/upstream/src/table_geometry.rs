// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::table::{CellContentGeometry, Table};
use acadrust::objects::{DataObject, DataObjectData, ObjectType, TableGeometry, TableGeometryCell};
use acadrust::types::Handle;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
fn point(seed: f64) -> CellContentGeometry {
    CellContentGeometry {
        distance_to_top_left: Vector3::new(seed, seed + 1., seed + 2.),
        distance_to_center: Vector3::new(-seed, seed + 3., seed + 4.),
        width: seed + 5.,
        height: seed + 6.,
        outer_width: seed + 7.,
        outer_height: seed + 8.,
        flags: -7,
    }
}
fn geometry() -> TableGeometry {
    TableGeometry {
        rows: 2,
        columns: 2,
        cells: (0..4)
            .map(|i| TableGeometryCell {
                geometry_data_flag: 10 + i,
                width_with_gap: 20. + i as f64,
                height_with_gap: 30. + i as f64,
                table_geometry: Handle::NULL,
                geometry: vec![point(i as f64 + 1.), point(i as f64 + 11.)],
            })
            .collect(),
    }
}
fn table() -> Table {
    let g = geometry();
    let mut t = Table::new(Vector3::new(1., 2., 3.), 2, 2);
    for r in 0..2 {
        for c in 0..2 {
            t.set_cell_text(r, c, &format!("单元{r}{c}"));
            let cell = &mut t.rows[r].cells[c];
            let cached = &g.cells[r * 2 + c];
            cell.geometry_data_flag = cached.geometry_data_flag;
            cell.geometry_width_with_gap = cached.width_with_gap;
            cell.geometry_height_with_gap = cached.height_with_gap;
            cell.geometries = cached.geometry.clone();
        }
    }
    t
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let root = d
        .objects
        .values()
        .find_map(|o| {
            if let ObjectType::Dictionary(v) = o {
                if v.owner.is_null() {
                    Some(v.handle)
                } else {
                    None
                }
            } else {
                None
            }
        })
        .unwrap();
    let mut object = DataObject::new(DataObjectData::TableGeometry(geometry()));
    object.handle = d.allocate_handle();
    object.owner = root;
    let h = object.handle;
    if let ObjectType::Dictionary(dict) = d.objects.get_mut(&root).unwrap() {
        dict.add_entry("独立几何", h);
    }
    d.objects.insert(h, ObjectType::DataObject(object));
    d.add_entity(EntityType::Table(Box::new(table())))?;
    d.add_paper_space_entity(EntityType::Table(Box::new(table())))?;
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn object(d: &CadDocument, h: Handle) -> &TableGeometry {
    if let ObjectType::DataObject(DataObject {
        data: DataObjectData::TableGeometry(g),
        ..
    }) = &d.objects[&h]
    {
        g
    } else {
        panic!()
    }
}
fn link(d: &CadDocument, h: Handle) -> Handle {
    d.xrecord(h, "ACAD_XREC_ROUNDTRIP")
        .unwrap()
        .get_first_by_code(361)
        .unwrap()
        .value
        .as_handle()
        .unwrap()
}
fn table_ref(d: &CadDocument, h: Handle) -> &Table {
    if let EntityType::Table(t) = d.get_entity(h).unwrap() {
        t
    } else {
        panic!()
    }
}
fn restored(d: &CadDocument, h: Handle) {
    let g = object(d, link(d, h));
    let t = table_ref(d, h);
    assert_eq!(g.rows as usize, t.rows.len());
    assert_eq!(g.columns as usize, t.columns.len());
    for (cell, cache) in t.rows.iter().flat_map(|r| &r.cells).zip(&g.cells) {
        assert_eq!(cell.geometry_data_flag, cache.geometry_data_flag);
        assert_eq!(cell.geometry_width_with_gap, cache.width_with_gap);
        assert_eq!(cell.geometry_height_with_gap, cache.height_with_gap);
        assert_eq!(cell.geometries, cache.geometry);
    }
}
fn named(d: &CadDocument, name: &str) -> Handle {
    d.objects
        .values()
        .find_map(|o| {
            if let ObjectType::Dictionary(v) = o {
                v.get(name)
            } else {
                None
            }
        })
        .unwrap()
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let model = source
        .entities_in_block("*Model_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let paper = source
        .entities_in_block("*Paper_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let geometry_only = read("geometry-only")?;
    restored(&geometry_only, model);
    let mut roundtrip = read("roundtrip")?;
    let mut targets = crate::tables::take_table_recompose(&mut roundtrip);
    targets.sort();
    let mut expected_targets = [model, paper];
    expected_targets.sort();
    assert_eq!(targets, expected_targets);
    assert_eq!(roundtrip.objects, source.objects);
    restored(&roundtrip, model);
    restored(&roundtrip, paper);
    let edited = read("edited")?;
    let mut expected = object(&source, link(&source, model)).clone();
    expected.cells[0].geometry_data_flag = -123;
    expected.cells[0].width_with_gap = 99.;
    expected.cells[0].geometry[1].distance_to_center = Vector3::new(4., 5., 6.);
    expected.cells[0].geometry[1].outer_height = 88.;
    expected.cells[0].table_geometry = link(&source, model);
    assert_eq!(object(&edited, link(&edited, model)), &expected);
    restored(&edited, model);
    assert_eq!(edited.get_entity(paper), source.get_entity(paper));
    assert_eq!(
        object(&edited, link(&edited, paper)),
        object(&source, link(&source, paper))
    );
    let created = read("created")?;
    assert_eq!(
        object(&created, named(&created, "C++几何")),
        object(&source, named(&source, "独立几何"))
    );
    let synchronized = read("synchronized")?;
    expected.cells[3].geometry[0].distance_to_top_left = Vector3::new(7., 8., 9.);
    assert_eq!(object(&synchronized, link(&synchronized, model)), &expected);
    restored(&synchronized, model);
    let copied = read("copied")?;
    let clone = copied
        .entities()
        .find(|e| synchronized.get_entity(e.common().handle).is_none())
        .unwrap()
        .common()
        .handle;
    let copy_geometry = link(&copied, clone);
    let mut copied_expected = expected.clone();
    copied_expected.cells[0].table_geometry = copy_geometry;
    assert_eq!(object(&copied, copy_geometry), &copied_expected);
    restored(&copied, clone);
    let changed = read("copy-edited")?;
    assert_eq!(
        object(&changed, copy_geometry).cells[0].geometry[0].width,
        123.
    );
    assert_eq!(object(&changed, link(&changed, model)), &expected);
    let deleted = read("deleted")?;
    assert!(deleted.get_entity(clone).is_none());
    assert!(!deleted.objects.contains_key(&copy_geometry));
    assert_eq!(object(&deleted, link(&deleted, model)), &expected);
    let resized = read("resized")?;
    let g = object(&resized, link(&resized, model));
    assert_eq!((g.rows, g.columns, g.cells.len()), (2, 3, 6));
    assert!(g
        .cells
        .iter()
        .all(|c| c.geometry.is_empty() && c.width_with_gap == 0.));
    restored(&resized, model);
    let fresh = read("fresh")?;
    let h = fresh
        .entities_in_block("*Model_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    assert_eq!(object(&fresh, link(&fresh, h)), &geometry());
    restored(&fresh, h);
    let mismatch = read("mismatch-fixed")?;
    let g = object(&mismatch, link(&mismatch, model));
    assert!(g
        .cells
        .iter()
        .all(|c| c.geometry.is_empty() && c.width_with_gap == 0.));
    restored(&mismatch, model);
    let partial = read("partial-fixed")?;
    let g = object(&partial, link(&partial, model));
    assert!(g.cells[..2].iter().all(|c| c.geometry.is_empty()));
    assert!(g.cells[2..].iter().all(|c| c.geometry.is_empty()));
    restored(&partial, model);
    let sections = read("sections")?;
    let h = sections
        .entities_in_block("*Model_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    assert_eq!(object(&sections, link(&sections, h)), &geometry());
    restored(&sections, h);
    println!("Rust verified TABLEGEOMETRY fields, ACAD_TABLE cache restoration, links, copy isolation, deletion and grid invalidation");
    Ok(())
}
