// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::table::Table;
use acadrust::objects::{ObjectType, TableStyle};
use acadrust::types::{Color, Handle, LineWeight};
use acadrust::{CadDocument, DxfReader, DxfWriter, Vector3};
fn style(d: &CadDocument, name: &str) -> Handle {
    d.objects
        .values()
        .find_map(|o| {
            if let ObjectType::TableStyle(s) = o {
                (s.name == name).then_some(s.handle)
            } else {
                None
            }
        })
        .unwrap()
}
fn get(d: &CadDocument, h: Handle) -> &TableStyle {
    if let ObjectType::TableStyle(s) = &d.objects[&h] {
        s
    } else {
        panic!()
    }
}
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    let base = style(&d, "Standard");
    let owner = get(&d, base).owner_handle;
    for name in ["中文样式", "邻居样式"] {
        let mut s = TableStyle::new(name);
        s.handle = d.allocate_handle();
        s.owner_handle = owner;
        s.ensure_cell_styles(|_| Handle::NULL);
        s.create_cell_style("自定义", Some("_DATA"));
        let cell = &mut s.cell_style_mut("自定义").unwrap().cell_style;
        cell.background_color = Color::from_true_color_value(0x123456);
        cell.content_format.content_color = Color::from_true_color_value(0xabcdef);
        cell.content_format.text_height = 3.5;
        cell.content_format.rotation = 0.25;
        cell.set_margins(1.25, 2.5);
        cell.borders[0].border.color = Color::from_true_color_value(0x112233);
        cell.borders[0].border.line_weight = LineWeight::from_value(35);
        cell.borders[0].border.is_invisible = false;
        cell.borders[1].index_mask = 0;
        let mut t = Table::new(Vector3::ZERO, 1, 1);
        t.common.handle = d.allocate_handle();
        t.set_cell_text(0, 0, "模板内容");
        s.modern_cell_style_handle = t.common.handle;
        d.objects
            .insert(t.common.handle, ObjectType::TableTemplate(t));
        if let ObjectType::Dictionary(dict) = d.objects.get_mut(&owner).unwrap() {
            dict.add_entry(name, s.handle);
        }
        d.objects.insert(s.handle, ObjectType::TableStyle(s));
    }
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DxfReader::from_file(input)?.read()?;
    let h = style(&source, "中文样式");
    let neighbor = style(&source, "邻居样式");
    let round = DxfReader::from_file(format!("{prefix}-roundtrip.dxf"))?.read()?;
    assert_eq!(
        get(&round, h).modern_overrides,
        get(&source, h).modern_overrides
    );
    let edited = DxfReader::from_file(format!("{prefix}-edited.dxf"))?.read()?;
    let mut expected = get(&source, h).clone();
    let cell = &mut expected.cell_style_mut("自定义").unwrap().cell_style;
    cell.background_color = Color::from_true_color_value(0x654321);
    cell.content_format.text_height = 7.;
    cell.borders[0].border.is_invisible = true;
    let data = &mut expected.cell_style_mut("_DATA").unwrap().cell_style;
    data.content_format.text_height = 9.;
    data.set_margins(4., 5.);
    data.borders[0].border.is_invisible = false;
    assert_eq!(get(&edited, h).modern_overrides, expected.modern_overrides);
    let actual = get(&edited, h);
    assert_eq!(actual.data_row_style.text_height, 9.);
    assert_eq!(actual.horizontal_margin, 4.);
    assert_eq!(actual.vertical_margin, 5.);
    assert!(actual.data_row_style.top_border.is_invisible);
    assert_eq!(get(&edited, neighbor), get(&source, neighbor));
    let th = actual.template().unwrap();
    if let ObjectType::TableTemplate(t) = &edited.objects[&th] {
        assert_eq!(t.rows[0].cells[0].contents[0].value.text, "C++模板");
    } else {
        panic!()
    }
    let fresh = DxfReader::from_file(format!("{prefix}-fresh.dxf"))?.read()?;
    for object in fresh.objects.values() {
        if let ObjectType::DataObject(v) = object {
            if matches!(v.data, acadrust::objects::DataObjectData::CellStyleMap(_)) {
                assert!(v.reactors.contains(&v.owner));
            }
        }
    }
    let s = get(&fresh, style(&fresh, "新样式"));
    let c = s.cell_style("新单元").unwrap();
    assert_eq!(c.id, 101);
    assert_eq!(c.cell_style.content_format.text_height, 6.);
    assert_eq!(
        c.cell_style.background_color,
        Color::from_true_color_value(0x135790)
    );
    assert!(s.template().is_some());
    println!("Rust verified named cell styles, RGB, margins, inverted grid flags, template links and neighbor isolation");
    Ok(())
}
