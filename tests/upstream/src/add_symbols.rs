// SPDX-License-Identifier: MPL-2.0
use acadrust::tables::LineTypeComplexContent;
use acadrust::{Color, DxfReader, Handle, Layer, LineType};

pub fn verify(before: &str, after: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(before)?.read()?;
    let saved = DxfReader::from_file(after)?.read()?;
    assert_eq!(saved.layers.len(), original.layers.len() + 1);
    assert_eq!(saved.line_types.len(), original.line_types.len() + 1);
    for layer in original.layers.iter() {
        assert_eq!(saved.layers.get(&layer.name).unwrap(), layer);
    }
    for lt in original.line_types.iter() {
        assert_eq!(saved.line_types.get(&lt.name).unwrap(), lt);
    }
    let layer = saved.layers.get("新增图层a").unwrap();
    let lt = saved.line_types.get("新增线型a").unwrap();
    assert_eq!(layer.name, "新增图层A");
    assert_eq!(layer.color, Color::Index(5));
    assert!(layer.is_off());
    assert_eq!(layer.line_type, lt.name);
    assert_eq!(lt.description, "新增划点文字");
    assert_eq!(lt.pattern_length, 3.);
    assert_eq!(
        lt.elements.iter().map(|e| e.length).collect::<Vec<_>>(),
        vec![2., -0.5, 0., -0.5, 0.]
    );
    for e in &lt.elements[..3] {
        assert!(e.complex.is_none());
    }
    let c = lt.elements[3].complex.as_ref().unwrap();
    assert_eq!(
        c.content,
        LineTypeComplexContent::Text {
            text: "给水".into()
        }
    );
    assert_eq!(
        c.style_handle,
        saved.text_styles.get("Standard").unwrap().handle
    );
    assert_eq!(c.scale, 0.5);
    assert!((c.rotation - 0.25).abs() < 1e-12);
    assert_eq!(c.offset, [0.1, -0.2]);
    assert!(c.upright);
    let shape = lt.elements[4].complex.as_ref().unwrap();
    assert_eq!(
        shape.content,
        LineTypeComplexContent::Shape { shape_number: 7 }
    );
    assert_eq!(shape.scale, 2.);
    assert!(shape.absolute_rotation);
    assert_eq!(shape.source_flags, 0x85);
    assert_eq!(
        shape.style_handle,
        saved.text_styles.get("Standard").unwrap().handle
    );
    assert_eq!(layer.handle.value(), lt.handle.value() + 1);
    assert_eq!(saved.header.handle_seed, layer.handle.value() + 1);
    assert_eq!(original.entities().count(), saved.entities().count());
    for entity in original.entities() {
        let mut expected = entity.clone();
        if entity.common().handle == Handle::new(0x31) {
            expected.common_mut().layer = layer.name.clone();
            expected.common_mut().linetype = lt.name.clone();
        }
        assert_eq!(&expected, saved.get_entity(entity.common().handle).unwrap());
    }
    assert_eq!(saved.objects, original.objects);
    assert_eq!(saved.block_records, original.block_records);
    // Rust's add path rejects the same normalized duplicate names as C++.
    let mut copy = saved.clone();
    assert!(copy.layers.add(Layer::new("新增图层a")).is_err());
    assert!(copy.line_types.add(LineType::new("新增线型a")).is_err());
    Ok(())
}
