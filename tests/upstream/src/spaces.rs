// SPDX-License-Identifier: MPL-2.0
use acadrust::{CadDocument, Circle, DxfReader, DxfWriter, EntityType, Line, Text, Vector3};

pub fn generate(ascii: &str, binary: &str) -> acadrust::Result<()> {
    let mut doc = CadDocument::new();
    doc.add_entity(EntityType::Line(Line::from_coords(0., 0., 0., 10., 2., 0.)))?;
    doc.add_entity(EntityType::Circle(Circle::from_coords(4., 5., 0., 2.)))?;
    doc.add_paper_space_entity(EntityType::Text(Text::with_value(
        "Sheet one",
        Vector3::ZERO,
    )))?;
    doc.add_paper_space_entity(EntityType::Line(Line::from_coords(1., 1., 0., 20., 1., 0.)))?;
    doc.add_layout("Sheet2")?;
    doc.add_entity_to_layout(
        EntityType::Circle(Circle::from_coords(7., 8., 0., 3.)),
        "Sheet2",
    )?;
    DxfWriter::new(&doc).write_to_file(ascii)?;
    DxfWriter::new_binary(&doc).write_to_file(binary)
}

pub fn verify(drawing: &str, result: &str) -> acadrust::Result<()> {
    let doc = DxfReader::from_file(drawing)?.read()?;
    let mut expected = Vec::new();
    for block in doc
        .block_records
        .iter()
        .filter(|b| b.is_model_space() || b.is_paper_space())
    {
        let mut handles: Vec<_> = doc
            .entities_in_block(&block.name)
            .map(|e| {
                assert_eq!(e.common().owner_handle, block.handle);
                e.common().handle.value()
            })
            .collect();
        handles.sort();
        let mut line = format!("{} {:X}", block.name, block.handle.value());
        for handle in handles {
            line.push_str(&format!(" {handle:X}"));
        }
        expected.push(line);
    }
    expected.sort();
    let cpp = std::fs::read_to_string(result)?;
    let mut actual: Vec<_> = cpp.lines().map(str::to_owned).collect();
    actual.sort();
    assert_eq!(actual, expected);
    assert_eq!(doc.model_space_entities().count(), 2);
    assert_eq!(expected.len(), 3);
    println!("All three spaces match C++ by block identity and entity handles");
    Ok(())
}
