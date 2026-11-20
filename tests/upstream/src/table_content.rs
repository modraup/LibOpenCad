// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::table::*;
use acadrust::objects::ObjectType;
use acadrust::types::{Color, Handle, LineWeight};
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
fn value(text: &str) -> CellValue {
    CellValue::text(text)
}
fn format(kind: CellStyleType, style: Handle) -> CellStyle {
    let mut s = CellStyle::new();
    s.style_type = kind;
    s.property_flags = CellStylePropertyFlags::DATA_TYPE
        | CellStylePropertyFlags::DATA_FORMAT
        | CellStylePropertyFlags::MARGIN_LEFT
        | CellStylePropertyFlags::MARGIN_VERTICAL_SPACING;
    s.value_data_type = 2;
    s.value_unit_type = 1;
    s.value_format = "%lu2%pr3".into();
    s.content_format_override_flags = 5;
    s.content_property_flags = 9;
    s.background_color = Color::from_index(2);
    s.content_color = Color::from_index(3);
    s.text_style_handle = Some(style);
    s.text_height = 2.5;
    s.rotation = 0.25;
    s.scale = 1.5;
    s.alignment = 6;
    s.margin_left = 1.;
    s.margin_top = 2.;
    s.margin_right = 3.;
    s.margin_bottom = 4.;
    s.horizontal_spacing = 0.5;
    s.vertical_spacing = 0.75;
    s.applied_border_edges = CellEdgeFlags::TOP | CellEdgeFlags::INSIDE_HORIZONTAL;
    s.top_border.color = Color::from_index(4);
    s.top_border.line_weight = LineWeight::from_value(35);
    s.top_border.invisible = true;
    let mut b = CellBorder::new();
    b.border_type = BorderType::Double;
    b.double_spacing = 0.2;
    b.color = Color::from_index(6);
    s.additional_borders.push((32, b));
    s
}
pub(crate) fn scene(d: &CadDocument) -> Table {
    let font = d.text_styles.get("Standard").unwrap().handle;
    let style = d
        .objects
        .values()
        .find_map(|o| {
            if let ObjectType::TableStyle(s) = o {
                if s.name == "Standard" {
                    Some(s.handle)
                } else {
                    None
                }
            } else {
                None
            }
        })
        .unwrap();
    let mut t = Table::new(Vector3::new(1., 2., 3.), 2, 2);
    t.name = "现代内容".into();
    t.description = "格式与模板".into();
    t.table_style_handle = Some(style);
    t.base_style = Some(format(CellStyleType::FormattedTableData, font));
    for (i, col) in t.columns.iter_mut().enumerate() {
        col.name = format!("列{i}");
        col.width = 20. + i as f64;
        col.style_id = 30 + i as i32;
        col.custom_data = 40 + i as i32;
        col.custom_data_items.push(TableCustomData {
            name: "列键".into(),
            value: value("列值"),
        });
        col.style = Some(format(CellStyleType::Column, font));
    }
    for (r, row) in t.rows.iter_mut().enumerate() {
        row.height = 10. + r as f64;
        row.style_id = 50 + r as i32;
        row.custom_data = 60 + r as i32;
        row.custom_data_items.push(TableCustomData {
            name: "行键".into(),
            value: CellValue::integer(21),
        });
        row.style = Some(format(CellStyleType::Row, font));
        for (c, cell) in row.cells.iter_mut().enumerate() {
            cell.tooltip = format!("提示{r}{c}");
            cell.custom_data = 70;
            cell.style_id = 80;
            cell.state = CellStateFlags::CONTENT_LOCKED;
            cell.custom_data_items.push(TableCustomData {
                name: "点".into(),
                value: CellValue {
                    value_type: CellValueType::Point3D,
                    raw_type_code: 32,
                    point_value: Vector3::new(4., 5., 6.),
                    ..CellValue::new()
                },
            });
            cell.set_text(&format!("文字{r}{c}"));
            cell.style = Some(format(CellStyleType::Cell, font));
            let item = &mut cell.contents[0];
            item.format_property_flags = 3;
            item.format_override_flags = 5;
            item.format_value_data_type = 4;
            item.format_value_unit_type = 1;
            item.value_format = "%tc1".into();
            item.text_style_handle = Some(font);
            item.text_height = 3.;
            item.color = Color::from_index(5);
            item.rotation = 0.3;
            item.scale = 2.;
            item.alignment = 4;
        }
    }
    t.rows[1].cells[1]
        .contents
        .push(CellContent::text("第二内容"));
    t.merge_cells(CellRange::new(0, 0, 0, 1));
    t
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let mut block = acadrust::tables::BlockRecord::new("CellBlock");
    block.handle = d.allocate_handle();
    block.block_entity_handle = d.allocate_handle();
    block.block_end_handle = d.allocate_handle();
    let block_handle = block.handle;
    d.block_records.add(block)?;
    let mut t = scene(&d);
    t.rows[1].cells[0]
        .contents
        .push(CellContent::block(block_handle));
    t.rows[0].cells[0].custom_data_items.push(TableCustomData {
        name: "二进制".into(),
        value: CellValue {
            value_type: CellValueType::Date,
            raw_type_code: 8,
            binary_value: (0..=255).collect(),
            ..CellValue::new()
        },
    });
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
    for template in [false, true] {
        let mut object = t.clone();
        object.common.handle = d.allocate_handle();
        object.common.owner_handle = root;
        object.name = if template { "模板" } else { "独立内容" }.into();
        let h = object.common.handle;
        if let Some(ObjectType::Dictionary(dict)) = d.objects.get_mut(&root) {
            dict.add_entry(&object.name, h);
        }
        d.objects.insert(
            h,
            if template {
                ObjectType::TableTemplate(object)
            } else {
                ObjectType::TableContent(object)
            },
        );
    }
    d.add_entity(EntityType::Table(Box::new(t.clone())))?;
    d.add_paper_space_entity(EntityType::Table(Box::new(t)))?;
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
fn object<'a>(d: &'a CadDocument, name: &str) -> &'a Table {
    d.objects
        .values()
        .find_map(|o| match o {
            ObjectType::TableContent(t) | ObjectType::TableTemplate(t) if t.name == name => Some(t),
            _ => None,
        })
        .unwrap()
}
fn table(e: &EntityType) -> &Table {
    if let EntityType::Table(t) = e {
        t
    } else {
        panic!()
    }
}
fn same(a: &Table, b: &Table) {
    assert_eq!(a.name, b.name);
    assert_eq!(a.description, b.description);
    assert_eq!(a.rows, b.rows);
    assert_eq!(a.columns, b.columns);
    assert_eq!(a.base_style, b.base_style);
    assert_eq!(a.merged_ranges, b.merged_ranges);
    assert_eq!(a.table_style_handle, b.table_style_handle);
    assert_eq!(a.field_handles, b.field_handles);
}
fn linked(d: &CadDocument, h: Handle) -> &Table {
    let h = d
        .xrecord(h, "ACAD_XREC_ROUNDTRIP")
        .unwrap()
        .get_first_by_code(360)
        .unwrap()
        .value
        .as_handle()
        .unwrap();
    if let ObjectType::TableContent(t) = &d.objects[&h] {
        t
    } else {
        panic!()
    }
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    let read = |name: &str| DxfReader::from_file(format!("{prefix}-{name}.dxf"))?.read();
    let plain = read("roundtrip")?;
    same(object(&plain, "独立内容"), object(&source, "独立内容"));
    same(object(&plain, "模板"), object(&source, "模板"));
    let edited = read("edited")?;
    let mut expected = object(&source, "模板").clone();
    expected.rows[1].cells[1].contents[1].value = value("修改第二项");
    expected.rows[1].cells[1].contents[1].value.flags = 6;
    expected.rows[1].cells[1]
        .style
        .as_mut()
        .unwrap()
        .margin_left = 9.;
    expected.columns[0].width = 33.;
    expected.base_style.as_mut().unwrap().content_color = Color::from_index(6);
    same(object(&edited, "模板"), &expected);
    same(object(&edited, "独立内容"), object(&source, "独立内容"));
    let made = read("created")?;
    let mut expected = object(&source, "独立内容").clone();
    expected.name = "C++内容".into();
    same(object(&made, "C++内容"), &expected);
    expected.name = "C++模板".into();
    same(object(&made, "C++模板"), &expected);
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
    let synced = read("synced")?;
    let modern = linked(&synced, model);
    assert_eq!(modern.rows[1].height, 24.);
    assert_eq!(modern.columns[0].width, 31.);
    assert_eq!(modern.rows[1].cells[0].contents[0].value.text, "同步文字");
    assert_eq!(modern.rows[1].cells[0].contents[0].value_format, "%tc2");
    assert_eq!(
        modern.rows[1].cells[0].style.as_ref().unwrap().value_format,
        "%lu2%pr5"
    );
    assert_eq!(modern.rows[1].cells[1].contents[1].value.text, "额外内容");
    let entity = table(synced.get_entity(model).unwrap());
    assert_eq!(entity.rows[1].height, 24.);
    assert_eq!(entity.rows[1].cells[0].contents[0].value.text, "同步文字");
    assert_eq!(entity.rows[1].cells[0].contents[0].value_format, "%tc2");
    assert_eq!(
        entity.rows[1].cells[0].style.as_ref().unwrap().value_format,
        "%lu2%pr5"
    );
    assert_eq!(entity.rows[1].cells[1].contents, modern.rows[1].cells[1].contents);
    same(linked(&synced, paper), linked(&source, paper));
    assert_eq!(synced.get_entity(paper), source.get_entity(paper));
    let refreshed = read("refreshed")?;
    let modern = linked(&refreshed, model);
    assert_eq!(modern.rows[1].height, 28.);
    assert_eq!(modern.rows[1].cells[0].contents[0].value.text, "旧接口修改");
    assert_eq!(modern.rows[1].cells[0].contents[0].value_format, "%tc2");
    assert_eq!(modern.rows[1].cells[1].contents[1].value.text, "额外内容");
    let copied = read("copied")?;
    let clone = copied
        .entities()
        .find(|e| {
            matches!(e, EntityType::Table(_)) && refreshed.get_entity(e.common().handle).is_none()
        })
        .unwrap();
    same(
        linked(&copied, clone.common().handle),
        linked(&refreshed, model),
    );
    let copy_edited = read("copy-edited")?;
    assert_eq!(
        linked(&copy_edited, clone.common().handle).rows[1].cells[1].contents[1]
            .value
            .text,
        "独立副本"
    );
    same(linked(&copy_edited, model), linked(&refreshed, model));
    let deleted = read("deleted")?;
    assert!(deleted.get_entity(clone.common().handle).is_none());
    same(linked(&deleted, model), linked(&refreshed, model));
    let fresh = read("new-link")?;
    let t = fresh.entities_in_block("*Model_Space").next().unwrap();
    let c = linked(&fresh, t.common().handle);
    assert_eq!(c.rows[0].cells[0].contents[0].value.text, "新表格");
    assert_eq!(table(t).rows[0].cells[0].contents[0].value.text, "新表格");
    let rgb = read("rgb")?;
    let style = object(&rgb, "模板").base_style.as_ref().unwrap();
    let color = Color::from_index(Color::from_rgb(18, 52, 86).approximate_index());
    assert_eq!(style.background_color, color);
    assert_eq!(style.content_color, color);
    assert_eq!(style.top_border.color, color);
    let inherited = read("inherited")?;
    let t = table(inherited.model_space_entities().next().unwrap());
    assert_eq!(t.rows[0].cells[0].flag, 0x1000 | 0x10000);
    assert_eq!(t.rows[1].cells[1].flag, 0x4000 | 0x80);
    assert!(t.rows[1].cells[1].style.as_ref().unwrap().top_border.invisible);
    assert_eq!(t.rows[1].cells[1].style.as_ref().unwrap().right_border.color, Color::from_index(4));
    println!("Rust verified TABLECONTENT/TABLETEMPLATE formats, multi-content, synchronization, ownership and independent copies");
    Ok(())
}
