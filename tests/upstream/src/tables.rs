// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::table::*;
use acadrust::objects::{ObjectType, TableStyle};
use acadrust::types::{Color, Handle, LineWeight};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
fn style() -> TableStyle {
    let mut s = TableStyle::new("中文表格");
    s.description = "明细样式".into();
    s.version = 1;
    s.horizontal_margin = 0.5;
    s.vertical_margin = 0.75;
    s.header_suppressed = true;
    s.data_row_style.text_height = 3.;
    s.data_row_style.text_color = Color::from_true_color_value(0x123456);
    s.data_row_style.fill_enabled = true;
    s.data_row_style.fill_color = Color::from_true_color_value(0xabcdef);
    s.data_row_style.top_border.color = Color::from_true_color_value(0x112233);
    s.data_row_style.top_border.line_weight = LineWeight::from_value(35);
    s.title_row_style.text_height = 5.;
    s.header_row_style.text_style_name.clear();
    s.header_row_style.text_height = 4.;
    s.header_row_style.right_border.is_invisible = true;
    s
}
pub(crate) fn definition(h: Handle) -> Table {
    let mut t = Table::new(Vector3::new(1., 2., 3.), 3, 3);
    t.table_style_handle = Some(h);
    t.horizontal_direction = Vector3::new(0., 1., 0.);
    t.data_version = 2;
    t.value_flags = 7;
    for (i, r) in t.rows.iter_mut().enumerate() {
        r.height = 10. + i as f64;
    }
    for (i, c) in t.columns.iter_mut().enumerate() {
        c.width = 20. + i as f64;
    }
    for r in 0..3 {
        for c in 0..3 {
            t.set_cell_text(r, c, &format!("项目{r}{c}"));
        }
    }
    t.rows[1].cells[0].contents[0].value = CellValue::number(12.5);
    t.rows[1].cells[1].contents[0].value = CellValue::integer(-12);
    let value = &mut t.rows[1].cells[2].contents[0].value;
    value.value_type = CellValueType::Date;
    value.raw_type_code = 8;
    value.text.clear();
    value.binary_value = vec![0, 1, 127, 128, 255];
    t.rows[2].cells[2].rotation = 0.5;
    t.rows[2].cells[2].state = CellStateFlags::from_bits_retain(7);
    let mut cs = CellStyle::new();
    cs.property_flags = CellStylePropertyFlags::ALIGNMENT
        | CellStylePropertyFlags::BACKGROUND_COLOR
        | CellStylePropertyFlags::CONTENT_COLOR
        | CellStylePropertyFlags::TEXT_STYLE
        | CellStylePropertyFlags::TEXT_HEIGHT;
    cs.alignment = 6;
    cs.fill_enabled = true;
    cs.background_color = Color::from_true_color_value(0x445566);
    cs.content_color = Color::from_index(3);
    cs.text_style_name = "Standard".into();
    cs.text_height = 6.;
    cs.top_border.invisible = true;
    cs.right_border.color = Color::from_true_color_value(0x778899);
    t.rows[2].cells[2].style = Some(cs);
    t.merge_cells(CellRange::new(0, 0, 0, 1));
    t.legacy_style_override = Some(LegacyTableStyleOverride {
        flags: 1 | 8 | 32 | 0x20000,
        title_suppressed: Some(true),
        horizontal_cell_margin: Some(1.25),
        row_colors: vec![Color::from_index(5)],
        text_style_names: vec!["Standard".into()],
        ..Default::default()
    });
    t
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let mut s = style();
    s.handle = d.allocate_handle();
    s.owner_handle = d
        .objects
        .values()
        .find_map(|o| {
            if let ObjectType::TableStyle(s) = o {
                Some(s.owner_handle)
            } else {
                None
            }
        })
        .unwrap();
    let h = s.handle;
    if let Some(ObjectType::Dictionary(dict)) = d.objects.get_mut(&s.owner_handle) {
        dict.add_entry(&s.name, h);
    }
    d.objects.insert(h, ObjectType::TableStyle(s));
    d.add_entity(EntityType::Table(Box::new(definition(h))))?;
    d.add_paper_space_entity(EntityType::Table(Box::new(definition(h))))?;
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn table(e: &EntityType) -> &Table {
    if let EntityType::Table(t) = e {
        t
    } else {
        panic!()
    }
}
fn same(a: &Table, b: &Table) {
    assert_eq!(a.insertion_point, b.insertion_point);
    assert_eq!(a.normal, b.normal);
    assert_eq!(a.horizontal_direction, b.horizontal_direction);
    assert_eq!(a.data_version, b.data_version);
    assert_eq!(a.value_flags, b.value_flags);
    assert_eq!(a.merged_ranges, b.merged_ranges);
    assert_eq!(a.legacy_style_override, b.legacy_style_override);
    assert_eq!(a.rows.len(), b.rows.len());
    assert_eq!(a.columns.len(), b.columns.len());
    for (a, b) in a.columns.iter().zip(&b.columns) {
        assert_eq!(a.width, b.width);
    }
    for (a, b) in a.rows.iter().zip(&b.rows) {
        assert_eq!(a.height, b.height);
        for (a, b) in a.cells.iter().zip(&b.cells) {
            assert_eq!(a.cell_type, b.cell_type);
            assert_eq!(a.state, b.state);
            assert_eq!(a.rotation, b.rotation);
            assert_eq!(a.merged, b.merged);
            assert_eq!(a.merge_width, b.merge_width);
            assert_eq!(a.merge_height, b.merge_height);
            assert_eq!(a.flag, b.flag);
            assert_eq!(a.style, b.style);
            assert_eq!(a.contents.len(), b.contents.len());
            for (a, b) in a.contents.iter().zip(&b.contents) {
                assert_eq!(a.value, b.value);
            }
        }
    }
}
fn get_style(d: &CadDocument) -> &TableStyle {
    d.objects
        .values()
        .find_map(|o| {
            if let ObjectType::TableStyle(s) = o {
                if s.name == "中文表格" {
                    Some(s)
                } else {
                    None
                }
            } else {
                None
            }
        })
        .unwrap()
}
fn edit(t: &mut Table) {
    t.rows[1].cells[0].contents[0].value.numeric_value = 88.25;
    t.rows[1].cells[1].set_text("已修改");
    t.rows[2].cells[2].style.as_mut().unwrap().background_color = Color::ByBlock;
    t.rows[1].height = 18.;
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let created = read("created")?;
    for space in ["*Model_Space", "*Paper_Space"] {
        same(
            table(created.entities_in_block(space).next().unwrap()),
            table(source.entities_in_block(space).next().unwrap()),
        );
    }
    let a = get_style(&created);
    let b = get_style(&source);
    assert_eq!(a.description, b.description);
    assert_eq!(a.version, b.version);
    assert_eq!(a.flags, b.flags);
    assert_eq!(a.title_suppressed, b.title_suppressed);
    assert_eq!(a.header_suppressed, b.header_suppressed);
    assert_eq!(a.horizontal_margin, b.horizontal_margin);
    assert_eq!(a.vertical_margin, b.vertical_margin);
    assert_eq!(a.data_row_style, b.data_row_style);
    assert_eq!(a.title_row_style, b.title_row_style);
    assert_eq!(a.header_row_style, b.header_row_style);
    let edited = read("edited")?;
    let mut expected_doc = source.clone();
    let model = source
        .entities_in_block("*Model_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    if let Some(EntityType::Table(t)) = expected_doc.get_entity_mut(model) {
        edit(t);
    }
    for space in ["*Model_Space", "*Paper_Space"] {
        same(
            table(edited.entities_in_block(space).next().unwrap()),
            table(expected_doc.entities_in_block(space).next().unwrap()),
        );
    }
    let styled = read("styled")?;
    assert_eq!(get_style(&styled).data_row_style.text_height, 8.);
    assert_eq!(get_style(&styled).data_row_style.text_color, Color::ByLayer);
    let raw_copied = read("raw-copied")?;
    let raw_source = styled.entities_in_block("*Model_Space").next().unwrap();
    let raw_copy = raw_copied
        .entities()
        .find(|e| {
            matches!(e, EntityType::Table(_)) && styled.get_entity(e.common().handle).is_none()
        })
        .unwrap();
    same(table(raw_copy), table(raw_source));
    let source_content = styled
        .xrecord(raw_source.common().handle, "ACAD_XREC_ROUNDTRIP")
        .unwrap()
        .get_first_by_code(360)
        .unwrap()
        .value
        .as_handle()
        .unwrap();
    let copied_content = raw_copied
        .xrecord(raw_copy.common().handle, "ACAD_XREC_ROUNDTRIP")
        .unwrap()
        .get_first_by_code(360)
        .unwrap()
        .value
        .as_handle()
        .unwrap();
    assert_ne!(source_content, copied_content);
    let raw_deleted = read("raw-deleted")?;
    assert!(!raw_deleted.objects.contains_key(&copied_content));
    assert_eq!(raw_deleted.objects, styled.objects);
    let before = read("before")?;
    let copied = read("copied")?;
    let original = before.entities_in_block("*Model_Space").next().unwrap();
    let clone = copied
        .entities()
        .find(|e| {
            matches!(e, EntityType::Table(_)) && before.get_entity(e.common().handle).is_none()
        })
        .unwrap();
    same(table(clone), table(original));
    assert_eq!(
        copied
            .xrecord(clone.common().handle, "Data")
            .unwrap()
            .get_first_by_code(340)
            .unwrap()
            .value
            .as_handle(),
        Some(clone.common().handle)
    );
    let changed = read("copy-edited")?;
    same(
        table(changed.get_entity(original.common().handle).unwrap()),
        table(original),
    );
    assert_eq!(
        table(changed.get_entity(clone.common().handle).unwrap()).rows[1].height,
        99.
    );
    let deleted = read("deleted")?;
    assert_eq!(
        deleted
            .entities()
            .filter(|e| matches!(e, EntityType::Table(_)))
            .count(),
        1
    );
    same(
        table(deleted.entities_in_block("*Paper_Space").next().unwrap()),
        table(before.entities_in_block("*Paper_Space").next().unwrap()),
    );
    let merged = read("merged")?;
    let t = table(merged.entities_in_block("*Model_Space").next().unwrap());
    assert_eq!(t.merged_ranges, vec![CellRange::new(1, 1, 2, 2)]);
    assert_eq!(t.rows[0].cells[1].merged, 0);
    assert_eq!(t.rows[0].cells[0].merge_width, 1);
    let points = read("points")?;
    let t = table(points.entities_in_block("*Model_Space").next().unwrap());
    assert_eq!(t.horizontal_direction, Vector3::new(0., 1., 0.));
    assert_eq!(
        t.rows[1].cells[0].contents[0].value.point_value,
        Vector3::new(4., 5., 6.)
    );
    println!("Rust verified ACAD_TABLE cells, merged ranges, values, style scopes, neighbor preservation, copy and deletion");
    Ok(())
}
