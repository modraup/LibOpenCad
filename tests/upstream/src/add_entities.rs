// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, Circle, Color, DxfReader, DxfWriter, EntityType, Handle,
    Layer, Line, LineType, LineWeight, Text, Vector3};
use acadrust::tables::LineTypeElement;
use std::io::Cursor;

pub fn verify(format: &str, path: &str) -> acadrust::Result<()> {
    // Construct the same drawing independently through the upstream public API.
    let mut reference = CadDocument::new();
    reference.header.insertion_units = 4;
    let mut layer = Layer::with_color("标注", Color::Index(2));
    layer.handle = reference.allocate_handle();
    assert_eq!(layer.handle.value(), 0x31);
    reference.layers.add(layer)?;
    let mut lt = LineType::new("建筑虚线");
    lt.handle = reference.allocate_handle();
    lt.description = "dash gap".into();
    lt.elements = vec![LineTypeElement::dash(1.), LineTypeElement::space(0.5)];
    lt.pattern_length = 1.5;
    reference.line_types.add(lt)?;
    for n in 0..2 {
        let d = n as f64;
        let mut line = Line::from_coords(1. + 100.*d, 2., 3.+d, 10.+100.*d, 20., 6.+d);
        line.common.color = Color::Rgb { r: 12, g: 34, b: 56 };
        line.common.linetype = "建筑虚线".into();
        line.common.line_weight = LineWeight::from_value(35);
        let mut circle = Circle::from_coords(-2.+100.*d, 4., 1.+d, 3.5+d);
        circle.common.color = Color::ByBlock;
        circle.common.line_weight = LineWeight::from_value(-3);
        circle.common.invisible = n != 0;
        let mut text = Text::with_value(if n == 0 { "模型：尺寸 ^J" } else { "图纸：说明" },
            Vector3::new(3.+100.*d, 5., 7.+d)).with_height(2.5+d);
        text.common.layer = "标注".into();
        text.common.color = Color::Index(4);
        for (i, entity) in [EntityType::Line(line), EntityType::Circle(circle), EntityType::Text(text)]
            .into_iter().enumerate() {
            let handle = if n == 0 { reference.add_entity(entity)? }
                else { reference.add_paper_space_entity(entity)? };
            assert_eq!(handle.value(), 0x33 + n*3 + i as u64);
        }
    }
    let binary = format == "binary";
    let bytes = std::fs::read(path)?;
    assert_eq!(bytes.starts_with(b"AutoCAD Binary DXF\r\n\x1a\0"), binary);
    let actual = DxfReader::from_reader(Cursor::new(bytes))?.read()?;
    let mut writer = DxfWriter::new(&reference);
    writer.set_binary(binary);
    let expected = DxfReader::from_reader(Cursor::new(writer.write_to_vec()?))?.read()?;
    assert_eq!(actual.entities().count(), 6);
    assert_eq!(actual.layers.len(), 2);
    assert_eq!(actual.layers, expected.layers);
    assert_eq!(actual.line_types, expected.line_types);
    assert_eq!(actual.text_styles, expected.text_styles);
    assert_eq!(actual.objects, expected.objects);
    assert_eq!(actual.block_records, expected.block_records);
    assert_eq!(actual.header.insertion_units, 4);
    assert_eq!(actual.header.handle_seed, 0x39);
    for handle in 0x33..=0x38 {
        let h = Handle::new(handle);
        let entity = actual.get_entity(h).expect("missing C++ identity");
        assert_eq!(entity, expected.get_entity(h).unwrap(), "entity {handle:X}: geometry/text/common data");
        assert_eq!(entity.common().owner_handle.value(), if handle < 0x36 { 0x15 } else { 0x18 });
        if handle >= 0x36 { assert_eq!(entity.common().entity_mode, Some(1)); }
    }
    for (space, handles) in [("*Model_Space", vec![0x33,0x34,0x35]), ("*Paper_Space", vec![0x36,0x37,0x38])] {
        assert_eq!(actual.entities_in_block(space).map(|e| e.common().handle.value()).collect::<Vec<_>>(), handles);
    }
    assert_eq!(actual.audit(), expected.audit());
    println!("C++ {format}: six entities, two layers, geometry/text/properties/space ownership match Rust");
    Ok(())
}
