// SPDX-License-Identifier: MPL-2.0
use acadrust::entities::{Line, Shape};
use acadrust::tables::TextStyle;
use acadrust::types::Transparency;
use acadrust::{CadDocument, DxfReader, DxfWriter, EntityType, Vector3};
pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut d = CadDocument::new();
    for (i, font) in ["ltypeshp.shx", "中文形.shx"].iter().enumerate() {
        let mut s = TextStyle::new(format!("SHAPES{i}"));
        s.is_shape_file = true;
        s.font_file = font.to_string();
        d.text_styles.add(s)?;
    }
    for paper in [false, true] {
        let mut s = Shape::with_name(Vector3::new(1., 2., 3.), "符号", 4.);
        s.rotation = 0.5;
        s.oblique_angle = -0.25;
        s.relative_x_scale = 2.;
        s.thickness = 3.;
        s.normal = Vector3::new(0., 1., 0.);
        s.common.linetype_scale = 2.5;
        s.common.transparency = Transparency::from_percent(0.3);
        s.common.color_name = Some("色本$蓝".into());
        if paper {
            d.add_paper_space_entity(EntityType::Shape(s))?;
        } else {
            d.add_entity(EntityType::Shape(s))?;
        }
    }
    let mut l = Line::from_points(Vector3::ZERO, Vector3::new(3., 4., 5.));
    l.thickness = 2.;
    l.normal = Vector3::new(1., 0., 0.);
    d.add_entity(EntityType::Line(l))?;
    DxfWriter::new(&d).write_to_file(ascii)?;
    DxfWriter::new_binary(&d).write_to_file(binary)
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let src = DxfReader::from_file(input)?.read()?;
    let model = src
        .entities_in_block("*Model_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let neighbor = src
        .entities_in_block("*Paper_Space")
        .next()
        .unwrap()
        .common()
        .handle;
    let read = |s: &str| DxfReader::from_file(format!("{prefix}-{s}.dxf"))?.read();
    let round = read("roundtrip")?;
    for e in src.entities() {
        assert_eq!(round.get_entity(e.common().handle), Some(e));
    }
    let edited = read("edited")?;
    let mut expected = src.get_entity(model).unwrap().clone();
    if let EntityType::Shape(s) = &mut expected {
        s.size = 9.;
        s.rotation = 0.75;
        s.common.transparency = Transparency::ByBlock;
        s.common.linetype_scale = 4.;
        s.common.color_name = None;
    }
    assert_eq!(edited.get_entity(model), Some(&expected));
    assert_eq!(edited.get_entity(neighbor), src.get_entity(neighbor));
    for e in edited.entities() {
        if let EntityType::Line(l) = e {
            assert_eq!(l.thickness, 6.);
            assert_eq!(l.common.graphic_data, None); // Pinned LINE reader drops common graphics; native roundtrip verifies bytes.
            assert_eq!(l.normal, Vector3::new(0., 1., 1.));
        }
    }
    let fresh = read("fresh")?;
    let shapes: Vec<_> = fresh
        .text_styles
        .iter()
        .filter(|s| s.is_shape_file)
        .collect();
    assert_eq!(shapes.len(), 2);
    assert!(shapes.iter().any(|s| s.font_file == "first.shx"));
    assert!(shapes.iter().any(|s| s.font_file == "second.shx"));
    let pattern = fresh.line_types.get("形线型").unwrap();
    let glyph = pattern.elements[0].complex.as_ref().unwrap();
    assert_eq!(glyph.shape_number(), Some(42));
    assert_eq!(glyph.scale, 2.);
    assert!((glyph.rotation - 0.5).abs() < 1e-12);
    assert_eq!(glyph.offset, [1., -2.]);
    assert_eq!(
        glyph.style_handle,
        shapes
            .iter()
            .find(|s| s.font_file == "first.shx")
            .unwrap()
            .handle
    );
    assert_eq!(fresh.entities().count(), 1);
    if let EntityType::Shape(s) = fresh.entities().next().unwrap() {
        assert_eq!(s.shape_name, "新符号");
        assert_eq!(s.common.transparency, Transparency::Explicit(85));
    }
    let deleted = read("deleted")?;
    assert_eq!(deleted.entities().count(), 3);
    println!("Rust verified SHAPE geometry, anonymous STYLE identities, LINE fields and common transparency/scale/color names");
    Ok(())
}
