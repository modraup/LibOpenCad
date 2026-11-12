// SPDX-License-Identifier: MPL-2.0
use acadrust::{
    CadDocument, Circle, DxfReader, DxfWriter, EntityType, Line, LineType, LineWeight, Text,
    Vector3,
};

type Properties = (&'static str, i16, bool); // linetype, 1/100 mm or special value, invisible
const SOURCE: [Properties; 5] = [
    ("", -1, false),
    ("Dashed", 50, true),
    ("Dotted", -2, false),
    ("ByBlock", -3, true),
    ("Continuous", 211, false),
];
const SAVED: [Properties; 5] = [
    ("Dashed", 35, true),
    ("", -1, false),
    ("ByBlock", -3, true),
    ("Dotted", 0, false),
    ("Continuous", 211, false),
];

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.line_types.add(LineType::dashed())?;
    doc.line_types.add(LineType::dotted())?;
    let entities = [
        EntityType::Line(Line::from_coords(0., 0., 0., 12., 0., 0.)),
        EntityType::Circle(Circle::from_coords(3., 5., 0., 2.)),
        EntityType::Text(Text::with_value("Sample", Vector3::new(0., 9., 0.))),
        EntityType::Line(Line::from_coords(1., 12., 0., 14., 12., 0.)),
        EntityType::Circle(Circle::from_coords(8., 5., 0., 1.)),
    ];
    for (mut entity, (name, weight, invisible)) in entities.into_iter().zip(SOURCE) {
        let common = entity.common_mut();
        common.linetype = name.into();
        common.line_weight = LineWeight::from_value(weight);
        common.invisible = invisible;
        doc.add_entity(entity)?;
    }
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}

pub fn verify(before: &str, after: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(before)?.read()?;
    let saved = DxfReader::from_file(after)?.read()?;
    assert_eq!(saved.version, original.version);
    assert_eq!(original.entities().count(), SOURCE.len());
    assert_eq!(saved.entities().count(), SAVED.len());
    for name in ["Dashed", "Dotted"] {
        let pattern = original.line_types.get(name).unwrap();
        assert!(!pattern.elements.is_empty());
        assert_eq!(saved.line_types.get(name).unwrap(), pattern);
    }
    for (i, src) in original.entities().enumerate() {
        let common = src.common();
        assert_eq!(
            (
                common.linetype.as_str(),
                common.line_weight.value(),
                common.invisible
            ),
            SOURCE[i]
        );
        let actual = saved
            .get_entity(common.handle)
            .expect("entity handle missing from saved drawing");
        let (name, weight, invisible) = SAVED[i];
        let mut expected = src.clone();
        expected.common_mut().linetype = name.into();
        expected.common_mut().line_weight = LineWeight::from_value(weight);
        expected.common_mut().invisible = invisible;
        assert_eq!(actual, &expected, "mismatch on handle {:?}", common.handle);
    }
    Ok(())
}
