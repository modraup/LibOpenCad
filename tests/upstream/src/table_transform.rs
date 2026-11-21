// SPDX-License-Identifier: MPL-2.0
use crate::entity_transform::{near, point};
use acadrust::entities::table::*;
use acadrust::objects::{Field, ObjectType};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Handle, Vector3};
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers.add(acadrust::Layer::new("Control"))?;
    doc.app_ids.add(acadrust::tables::AppId::new("Geometry"))?;
    let style = doc
        .objects
        .values()
        .find_map(|o| {
            if let ObjectType::TableStyle(s) = o {
                Some(s.handle)
            } else {
                None
            }
        })
        .unwrap();
    let root = doc.header.named_objects_dict_handle;
    let mut block = acadrust::tables::BlockRecord::new("单元格块");
    block.handle = doc.allocate_handle();
    block.block_entity_handle = doc.allocate_handle();
    block.block_end_handle = doc.allocate_handle();
    let bh = block.handle;
    doc.block_records.add(block)?;
    let mut field = Field::default();
    field.handle = doc.allocate_handle();
    field.owner = root;
    field.evaluator_id = "AcVar".into();
    field.code = "中文字段".into();
    field.value = CellValue::text("字段结果");
    let fh = field.handle;
    doc.objects.insert(fh, ObjectType::Field(field));
    let ObjectType::Dictionary(dict) = doc.objects.get_mut(&root).unwrap() else {
        panic!()
    };
    dict.add_entry("字段", fh);
    for normal in [
        Vector3::UNIT_Z,
        Vector3::new(1., -2., 3.),
        Vector3::ZERO,
        Vector3::new(1e-14, 0., 1e-14),
    ] {
        for kind in 0..3 {
            let mut t = if kind == 0 {
                crate::tables::definition(style)
            } else {
                crate::table_content::scene(&doc)
            };
            t.normal = normal;
            t.horizontal_direction = if normal == Vector3::ZERO {
                Vector3::ZERO
            } else {
                Vector3::new(2., 1., 3.)
            };
            if kind == 2 {
                t.rows[1].cells[0].contents.push(CellContent::block(bh));
                let mut f = CellContent::new();
                f.content_type = TableCellContentType::Field;
                f.field_handle = Some(fh);
                f.value = CellValue::text("字段显示");
                t.rows[1].cells[1].contents.push(f);
                t.rows[1].cells[1].geometries.push(CellContentGeometry {
                    distance_to_top_left: Vector3::new(1., 2., 3.),
                    distance_to_center: Vector3::new(4., 5., 6.),
                    width: 7.,
                    height: 8.,
                    outer_width: 9.,
                    outer_height: 10.,
                    flags: 11,
                });
                t.rows[1].cells[1].geometry_width_with_gap = 12.;
                t.rows[1].cells[1].geometry_height_with_gap = 13.;
            }
            use acadrust::xdata::{ExtendedDataRecord, XDataValue as X};
            t.common.extended_data.add_record(ExtendedDataRecord {
                application_name: "Geometry".into(),
                values: vec![
                    X::Position3D(Vector3::new(1., 2., 3.)),
                    X::Direction3D(Vector3::UNIT_Y),
                    X::Distance(2.5),
                ],
            });
            doc.add_entity(EntityType::Table(Box::new(t)))?;
        }
    }
    let controls: Vec<_> = doc.entities().take(3).cloned().collect();
    for mut e in controls {
        e.common_mut().handle = Handle::NULL;
        e.common_mut().layer = "Control".into();
        doc.add_paper_space_entity(e)?;
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    let doc = DxfReader::from_file(ascii)?.read()?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}
fn values(v: &Table) -> Vec<f64> {
    let mut a = vec![];
    for p in [v.insertion_point, v.horizontal_direction, v.normal] {
        a.extend([p.x, p.y, p.z]);
    }
    a.push(v.rows.len() as f64);
    a.extend(v.rows.iter().map(|r| r.height));
    a.push(v.columns.len() as f64);
    a.extend(v.columns.iter().map(|c| c.width));
    a
}
fn dimensions(a: &mut Table, b: &Table) {
    assert_eq!(a.rows.len(), b.rows.len());
    assert_eq!(a.columns.len(), b.columns.len());
    for (a, b) in a.rows.iter_mut().zip(&b.rows) {
        near(a.height, b.height);
        a.height = b.height;
    }
    for (a, b) in a.columns.iter_mut().zip(&b.columns) {
        near(a.width, b.width);
        a.width = b.width;
    }
}
pub(crate) fn normalize(a: &mut EntityType, b: &EntityType) {
    let (EntityType::Table(a), EntityType::Table(b)) = (a, b) else {
        panic!()
    };
    point(a.insertion_point, b.insertion_point);
    a.insertion_point = b.insertion_point;
    point(a.normal, b.normal);
    a.normal = b.normal;
    point(a.horizontal_direction, b.horizontal_direction);
    a.horizontal_direction = b.horizontal_direction;
    dimensions(a, b);
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    assert_eq!(source.entities().count(), 15);
    let mut fields = 0;
    let mut blocks = 0;
    let mut geometries = 0;
    for e in source.entities() {
        let EntityType::Table(t) = e else { panic!() };
        for cell in t.rows.iter().flat_map(|r| &r.cells) {
            geometries += cell.geometries.len();
            for content in &cell.contents {
                if let Some(h) = content.field_handle {
                    assert!(matches!(source.objects.get(&h), Some(ObjectType::Field(_))));
                    fields += 1;
                }
                if content.block_handle.is_some() {
                    blocks += 1;
                }
            }
        }
    }
    assert!(
        fields >= 5 && blocks >= 5 && geometries >= 5,
        "Missing reference/geometry fixture coverage"
    );
    let mut expected_targets: Vec<_> = source
        .entities()
        .map(|entity| entity.common().handle)
        .filter(|handle| source.xrecord(*handle, "ACAD_XREC_ROUNDTRIP").is_some())
        .collect();
    expected_targets.sort();
    let mut expected_header = source.header.clone();
    expected_header.handle_seed += 1;
    for step in 0..16 {
        eprintln!("table step {step}");
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
        assert_eq!(rows.lines().count(), 15);
        for row in rows.lines() {
            let mut p = row.split_whitespace();
            let handle = p.next().unwrap();
            let h = Handle::new(u64::from_str_radix(handle, 16).unwrap());
            let EntityType::Table(v) = expected.get_entity(h).unwrap() else {
                panic!()
            };
            let a: Vec<f64> = p.map(|p| p.parse().unwrap()).collect();
            let b = values(v);
            assert_eq!(a.len(), b.len());
            for (i, (a, b)) in a.iter().zip(&b).enumerate() {
                assert!(
                    (a - b).abs() <= 2e-10 * b.abs().max(1.),
                    "step {step} handle {handle} value {i}: {a} != {b}"
                );
            }
        }
        let path = format!("{prefix}.{step}.rust.dxf");
        DxfWriter::new(&expected).write_to_file(&path)?;
        let expected = DxfReader::from_file(path)?.read()?;
        let mut actual = DxfReader::from_file(format!("{prefix}.{step}.dxf"))?.read()?;
        assert_eq!(actual.entities().count(), 15);
        for b in expected.entities() {
            crate::entity_transform::compare(actual.get_entity(b.common().handle).unwrap(), b);
        }
        assert_eq!(actual.header, expected_header);
        let mut targets = crate::tables::take_table_recompose(&mut actual);
        targets.sort();
        assert_eq!(targets, expected_targets);
        assert_eq!(source.layers, actual.layers);
        assert_eq!(source.block_records, actual.block_records);
        assert_eq!(actual.objects.len(), source.objects.len());
        for (h, b) in &expected.objects {
            let a = actual.objects.get(h).unwrap();
            if let (ObjectType::TableContent(a), ObjectType::TableContent(b)) = (a, b) {
                let mut a = a.clone();
                dimensions(&mut a, b);
                // Rust rewrites modern content from the merged legacy entity and
                // can add legacy DATA_TYPE/DATA_FORMAT style flags on save.
                // Our dimension-only sync must preserve the original payload.
                let ObjectType::TableContent(original) = source.objects.get(h).unwrap() else {
                    panic!()
                };
                let mut preserved = original.clone();
                for (row, expected) in preserved.rows.iter_mut().zip(&b.rows) {
                    row.height = expected.height;
                }
                for (column, expected) in preserved.columns.iter_mut().zip(&b.columns) {
                    column.width = expected.width;
                }
                assert_eq!(a, preserved, "TABLECONTENT {h:?}");
            } else {
                assert_eq!(a, source.objects.get(h).unwrap(), "Unchanged object {h:?}");
            }
        }
    }
    Ok(())
}
