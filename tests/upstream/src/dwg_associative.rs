// SPDX-License-Identifier: MPL-2.0
use acadrust::classes::DxfClassCollection;
use acadrust::objects::AssociativeData;
use acadrust::{DwgReader, DwgWriter, DxfVersion};
use std::{fs, path::Path};
use super::associative;
fn number_classes(d: &mut acadrust::CadDocument) {
    let classes: Vec<_> = d.classes.iter().cloned().collect();
    let mut numbered = DxfClassCollection::new();
    for (i, mut class) in classes.into_iter().enumerate() {
        class.class_number = 500 + i as i16;
        numbered.push_preserving(class);
    }
    d.classes = numbered;
}
pub fn generate(dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    for version in [
        DxfVersion::AC1021,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
        DxfVersion::AC1032,
    ] {
        let mut d = associative::build(version);
        number_classes(&mut d);
        DwgWriter::write_to_file(Path::new(dir).join(format!("{version:?}.dwg")), &d)?;
    }
    Ok(())
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DwgReader::from_file(input)?.read()?;
    let expected = associative::objects(&source);
    assert!(
        expected.len() >= 12,
        "{input}: expected typed associative objects"
    );
    let identity = DwgReader::from_file(format!("{prefix}.identity.dwg"))?.read()?;
    assert_eq!(
        associative::objects(&identity),
        expected,
        "{input} vs {prefix}.identity.dwg: associative objects"
    );
    let edited = DwgReader::from_file(format!("{prefix}.edited.dwg"))?.read()?;
    let renamed = associative::objects(&edited)
        .into_iter()
        .any(|object| matches!(object.data, AssociativeData::Variable(v) if v.name == "宽度"));
    assert!(renamed, "{prefix}.edited.dwg: native variable name");
    Ok(())
}
