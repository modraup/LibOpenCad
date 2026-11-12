// SPDX-License-Identifier: MPL-2.0
use acadrust::{DxfReader, Handle};

fn compare(drawing: &str, result: &str) -> acadrust::Result<()> {
    let doc = DxfReader::from_file(drawing)?.read()?;
    let cpp = std::fs::read_to_string(result)?;
    assert_eq!(cpp.lines().count(), 10);
    for line in cpp.lines() {
        let fields: Vec<_> = line.split_whitespace().collect();
        let handle = Handle::new(u64::from_str_radix(fields[0], 16).unwrap());
        let expected_owner;
        let expected_type;
        if let Some(entity) = doc.get_entity(handle) {
            expected_owner = entity.common().owner_handle;
            expected_type = entity.as_entity().entity_type();
        } else if doc.layers.iter().any(|e| e.handle == handle) {
            expected_type = "LAYER";
            expected_owner = doc.layers.handle();
        } else if doc.line_types.iter().any(|e| e.handle == handle) {
            expected_type = "LTYPE";
            expected_owner = doc.line_types.handle();
        } else if doc.dim_styles.iter().any(|e| e.handle == handle) {
            expected_type = "DIMSTYLE";
            expected_owner = doc.dim_styles.handle();
        } else if doc.block_records.iter().any(|e| e.handle == handle) {
            expected_type = "BLOCK_RECORD";
            expected_owner = doc.block_records.handle();
        } else if doc.objects.contains_key(&handle) {
            expected_type = "DICTIONARY";
            expected_owner = doc.object_owner(handle).unwrap();
        } else {
            assert_eq!(fields, [fields[0], "missing"]);
            continue;
        }
        assert_eq!(fields[1], expected_type);
        assert_eq!(
            u64::from_str_radix(fields[2], 16).unwrap(),
            expected_owner.value()
        );
    }
    Ok(())
}
pub fn verify(drawing: &str, prefix: &str) -> acadrust::Result<()> {
    compare(drawing, &format!("{prefix}.txt"))?;
    compare(
        &format!("{prefix}-duplicate.dxf"),
        &format!("{prefix}-duplicate.txt"),
    )?;
    Ok(())
}
