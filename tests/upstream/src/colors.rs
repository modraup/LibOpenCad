// SPDX-License-Identifier: MPL-2.0
use acadrust::{
    CadDocument, Circle, Color, DxfReader, DxfWriter, EntityType, Layer, Line, Text, Vector3,
};

const SOURCE: [Color; 4] = [
    Color::ByLayer,
    Color::ByBlock,
    Color::Index(34),
    Color::from_rgb(228, 103, 33),
];
const EDITED: [Color; 4] = [
    Color::ByBlock,
    Color::ByLayer,
    Color::from_rgb(18, 160, 231),
    Color::Index(201),
];

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.layers
        .add(Layer::with_color("Colors", Color::Index(5)))?;
    let entities = [
        EntityType::Line(Line::from_coords(0., 0., 0., 3., 1., 0.)),
        EntityType::Circle(Circle::from_coords(5., 1., 0., 2.)),
        EntityType::Line(Line::from_coords(0., 5., 0., 3., 6., 0.)),
        EntityType::Text(Text::with_value("RGB", Vector3::new(5., 5., 0.))),
    ];
    for (mut entity, color) in entities.into_iter().zip(SOURCE) {
        entity.common_mut().layer = "Colors".into();
        entity.common_mut().color = color;
        doc.add_entity(entity)?;
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}

pub fn verify(before: &str, after: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(before)?.read()?;
    let saved = DxfReader::from_file(after)?.read()?;
    assert_eq!(saved.version, original.version);
    assert_eq!(saved.layers.get("Colors").unwrap().color, Color::Index(5));
    assert_eq!(original.entities().count(), 4);
    assert_eq!(saved.entities().count(), 4);
    for (i, (src, actual)) in original.entities().zip(saved.entities()).enumerate() {
        assert_eq!(src.common().color, SOURCE[i]);
        let mut expected = src.clone();
        expected.common_mut().color = EDITED[i];
        assert_eq!(
            actual, &expected,
            "color, identity or geometry differs on entity {i}"
        );
    }
    Ok(())
}
