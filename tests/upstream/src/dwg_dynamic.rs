// SPDX-License-Identifier: MPL-2.0
use acadrust::objects::*;
use acadrust::{DwgReader, DwgWriter, DxfReader, DxfVersion, EntityType};
use std::{fs, path::Path};
pub fn generate(source: &str, dir: &str) -> acadrust::Result<()> {
    fs::create_dir_all(dir)?;
    let original = DxfReader::from_file(source)?.read()?;
    for version in [
        DxfVersion::AC1021,
        DxfVersion::AC1024,
        DxfVersion::AC1027,
        DxfVersion::AC1032,
    ] {
        let mut d = original.clone();
        d.version = version;
        DwgWriter::write_to_file(Path::new(dir).join(format!("{version:?}.dwg")), &d)?;
    }
    Ok(())
}
pub fn verify(input: &str, prefix: &str) -> acadrust::Result<()> {
    let source = DwgReader::from_file(input)?.read()?;
    let def = source
        .block_records
        .iter()
        .find(|b| b.name == "Dyn主块")
        .map(|b| b.handle)
        .expect("definition");
    assert!(source.is_dynamic_block(def));
    assert_eq!(source.dynamic_block_properties(def, None)?.len(), 20);
    let identity = DwgReader::from_file(format!("{prefix}.identity.dwg"))?.read()?;
    assert!(identity.is_dynamic_block(def));
    assert_eq!(identity.dynamic_block_properties(def, None)?.len(), 20);
    let edited = DwgReader::from_file(format!("{prefix}.edited.dwg"))?.read()?;
    let renamed = edited.objects.values().any(|object| {
        matches!(
            object,
            ObjectType::DynamicBlock(DynamicBlockObject {
                data: DynamicBlockData::LinearParameter(p),
                ..
            }) if p.distance_name == "NativeLinear"
        )
    });
    assert!(renamed, "native linear name");
    assert_eq!(edited.dynamic_block_properties(def, None)?.len(), 20);
    assert!(source
        .entities()
        .any(|e| matches!(e, EntityType::Insert(_))));
    Ok(())
}
