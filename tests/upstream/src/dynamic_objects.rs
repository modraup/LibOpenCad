// SPDX-License-Identifier: MPL-2.0
use acadrust::objects::*;
use acadrust::{CadDocument, DxfReader};
fn objects(d: &CadDocument) -> Vec<DynamicBlockObject> {
    let mut values: Vec<_> = d
        .objects
        .values()
        .filter_map(|object| match object {
            ObjectType::DynamicBlock(value)
                if !matches!(value.data, DynamicBlockData::EvaluationGraph(_)) =>
            {
                Some(value.clone())
            }
            _ => None,
        })
        .collect();
    values.sort_by_key(|object| object.handle.value());
    values
}
pub fn verify(source: &str, out: &str) -> acadrust::Result<()> {
    let original = DxfReader::from_file(source)?.read()?;
    let expected = objects(&original);
    assert!(
        expected.len() >= 20,
        "{}: expected typed dynamic objects",
        source
    );
    for suffix in ["-ascii.dxf", "-binary.dxf"] {
        let restored = DxfReader::from_file(format!("{out}{suffix}"))?.read()?;
        assert_eq!(
            objects(&restored),
            expected,
            "{source} vs {out}{suffix}: dynamic objects"
        );
    }
    Ok(())
}
